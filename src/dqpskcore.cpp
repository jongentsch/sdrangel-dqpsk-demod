// SPDX-License-Identifier: GPL-3.0-or-later
#include "dqpskcore.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace dqpsk {
namespace {
constexpr double pi = 3.14159265358979323846;
double wrap(double x, double period) { return x - period * std::floor(x / period + .5); }
}
Core::Core(const Config& config) { reset(config); }
void Core::reset(const Config& config) {
    if (!std::isfinite(config.symbolRate) || config.symbolRate < 1000 || config.symbolRate > 12000 ||
        !std::isfinite(config.rolloff) || config.rolloff < .05 || config.rolloff > 1 ||
        !std::isfinite(config.carrierOffset) || std::abs(config.carrierOffset) > 8000 ||
        !std::isfinite(config.minimumPowerDB)) throw std::invalid_argument("Invalid DQPSK configuration");
    m_config = config;
    const int half = int(6 * sampleRate / config.symbolRate);
    m_rrc.resize(2 * half + 1);
    double energy = 0;
    for (int i = -half; i <= half; ++i) {
        const double t = i * config.symbolRate / sampleRate, a = config.rolloff;
        double h;
        if (std::abs(t) < 1e-9) h = 1 + a * (4 / pi - 1);
        else if (std::abs(std::abs(t) - 1 / (4 * a)) < 1e-8)
            h = a / std::sqrt(2.) * ((1 + 2/pi)*std::sin(pi/(4*a)) + (1 - 2/pi)*std::cos(pi/(4*a)));
        else h = (std::sin(pi*t*(1-a)) + 4*a*t*std::cos(pi*t*(1+a))) / (pi*t*(1-16*a*a*t*t));
        m_rrc[i+half] = float(h); energy += h*h;
    }
    for (auto& v : m_rrc) v /= float(std::sqrt(energy));
    if(!config.rrcEnabled)m_rrc={1.f};
    m_filter.assign(m_rrc.size(), {}); m_filterPosition = 0;
    m_buffer.clear(); m_buffer.reserve(50512); m_bufferStart = 0; m_sequence = 0;
    m_ncoPhase = 0; m_nextSymbol = -1; m_previous = {}; m_havePrevious = false;
}
void Core::feed(const Complex* samples, size_t count, const std::function<void(const Block&)>& output) {
    const double increment = -2*pi*m_config.carrierOffset/sampleRate;
    for (size_t i = 0; i < count; ++i) {
        const auto mixed = samples[i] * Complex(float(std::cos(m_ncoPhase)), float(std::sin(m_ncoPhase)));
        m_ncoPhase = wrap(m_ncoPhase + increment, 2*pi);
        m_filter[m_filterPosition] = mixed;
        Complex filtered = {};
        size_t pos = m_filterPosition;
        for (size_t tap = 0; tap < m_rrc.size(); ++tap) {
            filtered += m_filter[pos] * m_rrc[tap];
            pos = pos ? pos-1 : m_filter.size()-1;
        }
        m_filterPosition = (m_filterPosition + 1) % m_filter.size();
        m_buffer.push_back(filtered);
        if (m_buffer.size() == 50256) {
            output(process());
            m_buffer.erase(m_buffer.begin(), m_buffer.begin()+50000);
            m_bufferStart += 50000;
        }
    }
}
Complex Core::interpolate(double position) const {
    const size_t i = size_t(std::floor(position));
    const float mu = float(position-i);
    return m_buffer[i]*(1-mu) + m_buffer[i+1]*mu;
}
double Core::amplitudeCV(double offset) const {
    const double step = sampleRate/m_config.symbolRate;
    double sum = 0, sum2 = 0; int n = 0;
    // A deterministic sparse subset is enough to find the open symbol eye.
    for (double p = 128+offset; p < 50120; p += step*4) {
        const double a = std::abs(interpolate(p)); sum += a; sum2 += a*a; ++n;
    }
    const double mean = sum/n;
    return std::sqrt(std::max(0., sum2/n-mean*mean)) / std::max(mean, 1e-20);
}
Block Core::process() {
    Block b; b.sequence = m_sequence++;
    const double step = sampleRate/m_config.symbolRate;
    double best = 0, score = 1e100;
    for (int i = 0; i < 64; ++i) {
        const double off = i*step/64, cv = amplitudeCV(off);
        if (cv < score) { score = cv; best = off; }
    }
    double lo = best-step/64, hi = best+step/64;
    for (int i = 0; i < 12; ++i) {
        const double a = lo+(hi-lo)/3, c = hi-(hi-lo)/3;
        if (amplitudeCV(a) < amplitudeCV(c)) hi = c; else lo = a;
    }
    best = (lo+hi)/2; b.amplitudeCV = amplitudeCV(best);
    const double target = double(m_bufferStart)+128+best;
    if (m_nextSymbol < 0) m_nextSymbol = target;
    else m_nextSymbol += wrap(target-m_nextSymbol, step);
    // Guard against a negative correction placing the carried symbol outside the buffer.
    while (m_nextSymbol < double(m_bufferStart)+1) m_nextSymbol += step;
    std::vector<Complex> differential;
    differential.reserve(size_t(m_config.symbolRate)+2);
    double sumPower = 0;
    while (m_nextSymbol < double(m_bufferStart)+50128) {
        const Complex s = interpolate(m_nextSymbol-double(m_bufferStart));
        sumPower += std::norm(s);
        if (m_havePrevious) {
            Complex d = s*std::conj(m_previous);
            d /= std::max(std::abs(d), 1e-20f);
            if (differential.empty()) b.firstSample = m_nextSymbol;
            differential.push_back(d);
        }
        m_previous = s; m_havePrevious = true; m_nextSymbol += step;
    }
    b.samplesPerSymbol = step;
    b.powerDB = 10*std::log10(std::max(sumPower/std::max<size_t>(differential.size(),1), 1e-30));
    std::complex<double> fourth = {};
    for (auto d : differential) { const auto d2 = std::complex<double>(d)*std::complex<double>(d); fourth += d2*d2; }
    fourth /= double(std::max<size_t>(differential.size(),1));
    b.fourthMoment = std::abs(fourth);
    const double residual = std::arg((m_config.pi4 ? -1. : 1.)*fourth)/4;
    const double base = m_config.pi4 ? pi/4 : 0;
    b.carrierHz = m_config.carrierOffset + residual*m_config.symbolRate/(2*pi);
    double errors2 = 0; size_t counts[4] = {};
    for (auto d : differential) {
        const double phase = std::arg(d)-residual;
        int label = int(std::llround((phase-base)/(pi/2)))%4; if (label < 0) label += 4;
        const double error = wrap(phase-base-label*pi/2, 2*pi);
        b.labels.push_back(uint8_t(label));
        ++counts[label];
        b.phaseErrors.push_back(int8_t(std::clamp(std::lround(error/(pi/4)*127), -127L, 127L)));
        errors2 += error*error;
    }
    b.phaseRMSDegrees = std::sqrt(errors2/std::max<size_t>(differential.size(),1))*180/pi;
    int occupied = 0; for(auto count:counts) if(count > differential.size()/100) ++occupied;
    b.locked = occupied >= 3 && b.fourthMoment > .70 && b.phaseRMSDegrees < 15 && b.amplitudeCV < .35 && b.powerDB > m_config.minimumPowerDB;
    return b;
}
std::vector<uint8_t> packLabels(const std::vector<uint8_t>& labels) {
    std::vector<uint8_t> packed((labels.size()+3)/4,0);
    for (size_t i=0;i<labels.size();++i) packed[i/4] |= (labels[i]&3) << (6-2*(i%4));
    return packed;
}
std::vector<uint8_t> packBits(const std::vector<uint8_t>& labels, const std::vector<int>& mapping) {
    if (mapping.size()!=4) throw std::invalid_argument("Four dibit labels required");
    std::vector<uint8_t> mapped; mapped.reserve(labels.size());
    for (auto label:labels) mapped.push_back(uint8_t(mapping[label&3]));
    return packLabels(mapped);
}
}

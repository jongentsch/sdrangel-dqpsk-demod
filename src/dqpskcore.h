// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <complex>
#include <cstdint>
#include <functional>
#include <vector>

namespace dqpsk {
using Complex = std::complex<float>;
struct Config {
    double symbolRate = 9600;
    double rolloff = 1.0;
    double carrierOffset = 300;
    double minimumPowerDB = -80;
    bool pi4 = true;
    bool rrcEnabled = true;
};
struct Block {
    uint64_t sequence = 0;
    double firstSample = 0;
    double samplesPerSymbol = 0;
    double carrierHz = 0;
    double fourthMoment = 0;
    double phaseRMSDegrees = 0;
    double amplitudeCV = 0;
    double powerDB = -200;
    bool locked = false;
    std::vector<uint8_t> labels;
    std::vector<int8_t> phaseErrors;
};
// Input is continuous, channel-filtered complex 50 kS/s, not FM audio.
// State and symbol timing are retained across arbitrary feed boundaries.
class Core {
public:
    static constexpr double sampleRate = 50000;
    explicit Core(const Config& config = {});
    void reset(const Config& config);
    void feed(const Complex* samples, size_t count, const std::function<void(const Block&)>& output);
    size_t bufferedSamples() const { return m_buffer.size(); }
private:
    Config m_config;
    std::vector<float> m_rrc;
    std::vector<Complex> m_filter;
    size_t m_filterPosition = 0;
    std::vector<Complex> m_buffer;
    uint64_t m_bufferStart = 0, m_sequence = 0;
    double m_ncoPhase = 0, m_nextSymbol = -1;
    Complex m_previous = {};
    bool m_havePrevious = false;
    Complex interpolate(double localPosition) const;
    double amplitudeCV(double offset) const;
    Block process();
};
std::vector<uint8_t> packLabels(const std::vector<uint8_t>& labels);
std::vector<uint8_t> packBits(const std::vector<uint8_t>& labels, const std::vector<int>& mapping);
}

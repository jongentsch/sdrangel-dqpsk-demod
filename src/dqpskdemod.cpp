// SPDX-License-Identifier: GPL-3.0-or-later
#include "dqpskdemod.h"
#include "dqpskcore.h"
#include "device/deviceapi.h"
#include "dsp/channelsamplesink.h"
#include "dsp/downchannelizer.h"
#include "dsp/interpolator.h"
#include "dsp/dspcommands.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QUuid>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>

namespace {
constexpr double pi = 3.14159265358979323846;
QString utcNow() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs); }
QByteArray jsonLine(const QJsonObject& o) { return QJsonDocument(o).toJson(QJsonDocument::Compact)+'\n'; }
QString b64(const std::vector<uint8_t>& v) { return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(v.data()),qsizetype(v.size())).toBase64()); }
class Sink final : public ChannelSampleSink {
public:
    dqpsk::Core core;
    Interpolator resampler;
    Real distance = 0, ratio = 1;
    dqpsk::Complex oscillator = {1,0}, rotation = {1,0};
    int renormalize = 0;
    std::function<void(const dqpsk::Block&)> output;
    void configure(int rate, int offset, const QJsonObject& s) {
        dqpsk::Config c;
        c.symbolRate=s["symbolRate"].toDouble(); c.rolloff=s["rolloff"].toDouble();
        c.carrierOffset=s["carrierOffsetHz"].toDouble(); c.minimumPowerDB=s["squelchDB"].toDouble();
        c.pi4=s["pi4"].toBool();c.rrcEnabled=s["rrcEnabled"].toBool();core.reset(c);
        resampler.create(32, rate, std::min(18000.,rate*.4));
        ratio=Real(rate/50000.); distance=0;
        rotation=std::polar(1.f,float(-2*pi*offset/rate)); oscillator={1,0}; renormalize=0;
    }
    void feed(const SampleVector::const_iterator& begin, const SampleVector::const_iterator& end) override {
        std::vector<dqpsk::Complex> out; out.reserve(4096);
        for (auto it=begin;it!=end;++it) {
            const Complex mixed = Complex(it->real()/SDR_RX_SCALEF,it->imag()/SDR_RX_SCALEF)*oscillator;
            oscillator*=rotation;
            if (++renormalize==4096) { oscillator/=std::abs(oscillator); renormalize=0; }
            Complex sample;
            if (resampler.decimate(&distance,mixed,&sample)) {
                distance+=ratio; out.push_back(sample);
                if (out.size()==4096) { core.feed(out.data(),out.size(),output); out.clear(); }
            }
        }
        if (!out.empty()) core.feed(out.data(),out.size(),output);
    }
};
}
QJsonObject DQPSKDemod::defaults() {
    return {{"inputFrequencyOffset",0},{"symbolRate",9600.},{"rolloff",1.},
        {"carrierOffsetHz",300.},{"pi4",true},{"rrcEnabled",true},{"mode","pi4-dqpsk"},{"squelchDB",-80.},
        {"dibitMap",QJsonArray{0,1,3,2}},{"title","Differential QPSK Symbols"},
        {"outputDirectory",QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)+"/SDRangel/symbols"}};
}
DQPSKDemod::DQPSKDemod(DeviceAPI* device) : ChannelAPI("org.sdrangel.channel.dqpskdemod",StreamSingleSink),m_device(nullptr),m_settings(defaults()) {
    setObjectName("DQPSKDemod");
    m_report={{"state","idle"},{"recording",false},{"locked",false},{"symbolCount",0.},{"note","Channel bits only; protocol and dibit assignment unverified"}};
    m_worker=std::thread(&DQPSKDemod::worker,this);
    setDeviceAPI(device);
}
DQPSKDemod::~DQPSKDemod() {
    setDeviceAPI(nullptr);
    { std::lock_guard<std::mutex> lock(m_mutex); m_quit=true; m_running=false; m_captureRequested=false; }
    m_ready.notify_all(); if(m_worker.joinable()) m_worker.join();
}
void DQPSKDemod::setDeviceAPI(DeviceAPI* device) {
    // DeviceAPI renumeration calls this for every registered channel. Rebinding
    // to the same owner must be a no-op, not another registration cycle.
    if(device==m_device)return;
    if (m_device) { m_device->removeChannelSinkAPI(this); m_device->removeChannelSink(this,device==nullptr); }
    m_device=device;
    if (m_device) { m_device->addChannelSink(this); m_device->addChannelSinkAPI(this); }
}
QJsonObject DQPSKDemod::settings() const { std::lock_guard<std::mutex> lock(m_mutex); return m_settings; }
QJsonObject DQPSKDemod::report() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto r=m_report;
    r["uid"]=QString::number(getUID()); r["deviceSetIndex"]=getDeviceSetIndex(); r["channelIndex"]=getIndexInDeviceSet();
    r["basebandSampleRate"]=m_sampleRate; r["hardwareCenterHz"]=double(m_hardwareCenter);
    r["inputFrequencyOffset"]=m_settings["inputFrequencyOffset"];
    r["running"]=m_running; r["recording"]=m_captureActive||m_captureRequested;
    r["droppedInputSamples"]=double(m_droppedSamples); r["queuedSamples"]=double(m_queuedSamples);
    return r;
}
void DQPSKDemod::getTitle(QString& title) { title=settings()["title"].toString(); }
qint64 DQPSKDemod::getCenterFrequency() const { return qint64(settings()["inputFrequencyOffset"].toDouble()); }
void DQPSKDemod::setCenterFrequency(qint64 frequency) { QString error; configure({{"inputFrequencyOffset",double(frequency)}},error); }
bool DQPSKDemod::configure(const QJsonObject& patch,QString& error) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if(m_captureActive||m_captureRequested) { error="Stop symbol capture before changing settings"; return false; }
        auto proposed=m_settings;
        if(patch.contains("mode")) {
            const auto mode=patch["mode"].toString();
            if(mode!="pi4-dqpsk"&&mode!="dqpsk"&&mode!="h-dqpsk") {error="Unknown modulation mode";return false;}
            proposed["pi4"]=mode!="dqpsk";
            if(mode=="h-dqpsk"&&!patch.contains("symbolRate"))proposed["symbolRate"]=6000.;
            if(mode=="h-dqpsk"&&!patch.contains("rrcEnabled"))proposed["rrcEnabled"]=false;
        }
        for(auto it=patch.begin();it!=patch.end();++it) {
            if(!proposed.contains(it.key())) { error="Unknown setting: "+it.key(); return false; }
            proposed[it.key()]=it.value();
        }
        if(!proposed["rrcEnabled"].isBool()||!proposed["mode"].isString()||(proposed["mode"].toString()=="dqpsk")==proposed["pi4"].toBool()){error="Inconsistent phase model settings";return false;}
        const auto num=[&](const char* k,double lo,double hi) { return proposed[k].isDouble()&&std::isfinite(proposed[k].toDouble())&&proposed[k].toDouble()>=lo&&proposed[k].toDouble()<=hi; };
        if(!num("symbolRate",1000,12000)||!num("rolloff",.05,1)||!num("carrierOffsetHz",-8000,8000)||!num("squelchDB",-160,0)||!num("inputFrequencyOffset",-100000000,100000000)||!proposed["pi4"].isBool()||!proposed["title"].isString()||!proposed["outputDirectory"].isString()) {
            error="Invalid setting type or range"; return false;
        }
        if(proposed["title"].toString().size()>128||proposed["outputDirectory"].toString().size()>1024||std::abs(proposed["carrierOffsetHz"].toDouble())+proposed["symbolRate"].toDouble()*(1+proposed["rolloff"].toDouble())/2>17000) {error="Title/path too long, or signal would exceed the 36 kHz channel filter";return false;}
        if(m_sampleRate>=50000&&std::abs(proposed["inputFrequencyOffset"].toDouble())+18000>m_sampleRate/2.) {error="Channel lies outside the current receive span";return false;}
        auto map=proposed["dibitMap"].toArray(); std::set<int> values;
        for(auto v:map) { if(!v.isDouble()||v.toDouble()!=v.toInt()) {error="Dibit mapping must be integers";return false;} values.insert(v.toInt()); }
        if(map.size()!=4||values!=std::set<int>{0,1,2,3}) { error="Dibit map must be a permutation of 0,1,2,3";return false; }
        m_settings=proposed; ++m_generation; m_queue.clear(); m_queuedSamples=0;
    }
    m_ready.notify_all(); emit settingsChanged(); return true;
}
QByteArray DQPSKDemod::serialize() const {
    auto s=settings();s["format"]="dqpsk-settings-v1";s["workspaceIndex"]=workspaceIndex;s["geometry"]=QString::fromLatin1(geometry.toBase64());s["hidden"]=hidden;
    return QJsonDocument(s).toJson(QJsonDocument::Compact);
}
bool DQPSKDemod::deserialize(const QByteArray& data) {
    auto o=QJsonDocument::fromJson(data).object(); if(o.take("format").toString()!="dqpsk-settings-v1") return false;
    workspaceIndex=o.take("workspaceIndex").toInt();geometry=QByteArray::fromBase64(o.take("geometry").toString().toLatin1());hidden=o.take("hidden").toBool();
    QString error; return configure(o,error);
}
void DQPSKDemod::feed(const SampleVector::const_iterator& begin,const SampleVector::const_iterator& end,bool) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if(!m_running||m_sampleRate<50000) return;
    const size_t count=size_t(end-begin), limit=size_t(std::min(m_sampleRate,2000000));
    if(m_queuedSamples+count>limit) {
        m_droppedSamples+=m_queuedSamples+count; m_queue.clear();m_queuedSamples=0;++m_generation;
        m_report["lastGap"]="Input queue overflow: decoder reset; missing samples are not joined";return;
    }
    m_queue.emplace_back(begin,end);m_queuedSamples+=count;m_ready.notify_one();
}
void DQPSKDemod::start() { { std::lock_guard<std::mutex> lock(m_mutex);m_running=true;++m_generation;m_report["state"]="acquiring"; }m_ready.notify_all(); }
void DQPSKDemod::stop() { { std::lock_guard<std::mutex> lock(m_mutex);m_running=false;m_captureRequested=false;m_queue.clear();m_queuedSamples=0;m_report["state"]="idle";m_report["locked"]=false; }m_ready.notify_all(); }
bool DQPSKDemod::handleMessage(const Message& message) {
    if(DSPSignalNotification::match(message)) {
        const auto& n=static_cast<const DSPSignalNotification&>(message);
        {std::lock_guard<std::mutex> lock(m_mutex);m_sampleRate=n.getSampleRate();m_hardwareCenter=n.getCenterFrequency();++m_generation;m_queue.clear();m_queuedSamples=0;}
        m_ready.notify_all();return true;
    }
    return false;
}
bool DQPSKDemod::startCapture(const QJsonObject& request,QString& error) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if(m_captureActive||m_captureRequested) {error="A symbol capture is already active";return false;}
    if(!m_running||m_sampleRate<50000) {error="Start the receive device with at least 50 kS/s baseband before capturing symbols";return false;}
    const double seconds=request.value("seconds").toDouble(30),maxBytes=request.value("maxBytes").toDouble(64*1024*1024);
    if(!std::isfinite(seconds)||seconds<1||seconds>1200||!std::isfinite(maxBytes)||maxBytes<16384||maxBytes>256*1024*1024) {error="Capture needs 1–1200 seconds and 16 KiB–256 MiB maximum";return false;}
    const QString dir=request.value("directory").toString(m_settings["outputDirectory"].toString());
    if(!QDir::isAbsolutePath(dir)) {error="Capture directory must be an absolute path";return false;}
    m_captureRequest={{"seconds",seconds},{"maxBytes",maxBytes},{"directory",dir}};m_captureRequested=true;m_ready.notify_all();return true;
}
void DQPSKDemod::stopCapture() { {std::lock_guard<std::mutex> lock(m_mutex);m_captureRequested=false;}m_ready.notify_all(); }
void DQPSKDemod::worker() {
    Sink sink;DownChannelizer channelizer(&sink);
    uint64_t generation=0,segment=0,symbolCount=0;int sampleRate=0;QJsonObject activeSettings;QString segmentStartUTC;
    QFile file;std::chrono::steady_clock::time_point deadline;double maxBytes=0;
    auto closeCapture=[&](const QString& reason) {
        if(file.isOpen()) {file.write(jsonLine({{"type","end"},{"reason",reason},{"utc",utcNow()},{"symbolsTotal",double(symbolCount)}}));file.flush();file.close();}
        std::lock_guard<std::mutex> lock(m_mutex);m_captureRequested=false;m_captureActive=false;m_report["captureStopReason"]=reason;
    };
    sink.output=[&](const dqpsk::Block& b) {
        symbolCount+=b.labels.size();
        QJsonArray points;
        for(size_t i=0;i<b.labels.size();i+=std::max<size_t>(1,b.labels.size()/64)) {
            const double phase=(activeSettings["pi4"].toBool()?pi/4:0)+b.labels[i]*pi/2+b.phaseErrors[i]*pi/(4*127);
            points.append(QJsonArray{std::cos(phase),std::sin(phase)});if(points.size()==64)break;
        }
        QJsonObject r={{"state",b.locked?"locked":"acquiring"},{"locked",b.locked},{"symbolCount",double(symbolCount)},
            {"fourthMoment",b.fourthMoment},{"phaseRMSDegrees",b.phaseRMSDegrees},{"amplitudeCV",b.amplitudeCV},
            {"carrierHzRelativeTuning",b.carrierHz},{"powerDBAfterMatchedFilter",b.powerDB},{"constellation",points},{"segment",double(segment)}};
        {std::lock_guard<std::mutex> lock(m_mutex);for(auto it=r.begin();it!=r.end();++it)m_report[it.key()]=it.value();if(!m_running){m_report["state"]="idle";m_report["locked"]=false;}}
        if(!file.isOpen())return;
        QJsonObject o={{"type",b.locked?"symbols":"gap"},{"segment",double(segment)},{"sequence",double(b.sequence)},
            {"firstSample",b.firstSample},{"samplesPerSymbol",b.samplesPerSymbol},{"count",double(b.labels.size())},
            {"carrierHzRelativeTuning",b.carrierHz},{"fourthMoment",b.fourthMoment},{"phaseRMSDegrees",b.phaseRMSDegrees},{"amplitudeCV",b.amplitudeCV}};
        if(b.locked) {
            std::vector<int> map;for(auto v:activeSettings["dibitMap"].toArray())map.push_back(v.toInt());
            o["phaseLabels2bitBase64"]=b64(dqpsk::packLabels(b.labels));o["provisionalBitsBase64"]=b64(dqpsk::packBits(b.labels,map));
            o["phaseErrorsInt8Base64"]=QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(b.phaseErrors.data()),qsizetype(b.phaseErrors.size())).toBase64());
        } else o["reason"]="No valid differential-QPSK lock; no fabricated channel bits";
        const auto line=jsonLine(o);
        if(file.size()+line.size()+8192>maxBytes) {closeCapture("byte limit reached");return;}
        if(file.write(line)!=line.size()) {closeCapture("write failed");return;}file.flush();
        std::lock_guard<std::mutex> lock(m_mutex);m_report["captureBytes"]=double(file.size());
    };
    while(true) {
        SampleVector samples;QJsonObject settingsCopy,capture;uint64_t wantedGeneration;int rate;bool running,requested,quit;qint64 center;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_ready.wait_for(lock,std::chrono::milliseconds(100),[&]{return m_quit||!m_queue.empty()||m_generation!=generation||m_captureRequested!=file.isOpen();});
            quit=m_quit;running=m_running;requested=m_captureRequested;capture=m_captureRequest;settingsCopy=m_settings;
            wantedGeneration=m_generation;rate=m_sampleRate;center=m_hardwareCenter;
            if(!m_queue.empty()) {samples=std::move(m_queue.front());m_queue.pop_front();m_queuedSamples-=samples.size();}
        }
        if(quit) {closeCapture("channel destroyed");break;}
        if(file.isOpen()&&(!requested||!running||std::chrono::steady_clock::now()>=deadline)) {closeCapture(!running?"receiver stopped":!requested?"stopped":"time limit reached");requested=false;}
        if(wantedGeneration!=generation&&rate<50000){generation=wantedGeneration;sampleRate=0;std::lock_guard<std::mutex> lock(m_mutex);m_report["state"]=rate>0?"baseband sample rate must be at least 50 kS/s":"idle";m_report["locked"]=false;continue;}
        if(wantedGeneration!=generation&&rate>0) {
            if(file.isOpen()) file.write(jsonLine({{"type","gap"},{"reason","DSP discontinuity: new segment"},{"nextSegment",double(segment+1)}}));
            activeSettings=settingsCopy;sampleRate=rate;++segment;generation=wantedGeneration;segmentStartUTC=utcNow();
            channelizer.setBasebandSampleRate(rate);channelizer.setChannelization(50000,qint64(settingsCopy["inputFrequencyOffset"].toDouble()));
            sink.configure(channelizer.getChannelSampleRate(),channelizer.getChannelFrequencyOffset(),settingsCopy);
            if(file.isOpen()) file.write(jsonLine({{"type","segment"},{"segment",double(segment)},{"utc",utcNow()},{"basebandSampleRate",rate},{"settings",activeSettings}}));
        }
        if(requested&&!file.isOpen()&&running&&rate>0) {
            const QString dir=capture["directory"].toString();
            const QString path=QDir(dir).filePath("dqpsk-"+QString::number(getUID())+"-"+QDateTime::currentDateTimeUtc().toString("yyyyMMddTHHmmsszzz")+"-"+QUuid::createUuid().toString(QUuid::Id128).left(8)+".jsonl");
            file.setFileName(path);
            if(!QDir().mkpath(dir)||!file.open(QIODevice::WriteOnly|QIODevice::NewOnly)) {closeCapture("cannot create capture file");continue;}
            maxBytes=capture["maxBytes"].toDouble();deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(qint64(1000*capture["seconds"].toDouble()));
            file.write(jsonLine({{"format","sdrangel-dqpsk-symbols-v1"},{"type","header"},{"utc",utcNow()},
                {"uid",QString::number(getUID())},{"hardwareCenterHz",double(center)},{"basebandSampleRate",rate},
                {"demodSampleRate",50000},{"segment",double(segment)},{"segmentStartUTC",segmentStartUTC},{"settings",activeSettings},{"maximumBytes",maxBytes},{"maximumSeconds",capture["seconds"]},
                {"clockNote","Sample positions are relative to the current DSP segment; RRC filter group delay retained. UTC is host processing time, not calibrated RF arrival."},
                {"interpretation","Physical phase decisions plus provisional bit mapping; no protocol, byte boundaries, payload or CRC claimed."}}));
            std::lock_guard<std::mutex> lock(m_mutex);m_captureActive=true;m_report["captureFile"]=path;m_report["captureBytes"]=double(file.size());
        }
        if(running&&!samples.empty()&&sampleRate>0) channelizer.feed(samples.begin(),samples.end());
    }
}

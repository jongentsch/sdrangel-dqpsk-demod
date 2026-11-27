// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonObject>
#include <QMutex>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include "channel/channelapi.h"
#include "dsp/basebandsamplesink.h"

class DQPSKDemod : public ChannelAPI, public BasebandSampleSink {
    Q_OBJECT
public:
    explicit DQPSKDemod(DeviceAPI* device);
    ~DQPSKDemod() override;
    void destroy() override { delete this; }
    void setDeviceAPI(DeviceAPI* device) override;
    DeviceAPI* getDeviceAPI() override { return m_device; }
    void getIdentifier(QString& id) override { id = "DQPSKDemod"; }
    QString getIdentifier() const override { return "DQPSKDemod"; }
    void getTitle(QString& title) override;
    qint64 getCenterFrequency() const override;
    void setCenterFrequency(qint64 frequency) override;
    int getChannelSampleRate() const override { return 50000; }
    QByteArray serialize() const override;
    bool deserialize(const QByteArray& data) override;
    int getNbSinkStreams() const override { return 1; }
    int getNbSourceStreams() const override { return 0; }
    int getStreamIndex() const override { return 0; }
    qint64 getStreamCenterFrequency(int, bool) const override { return getCenterFrequency(); }
    void feed(const SampleVector::const_iterator& begin, const SampleVector::const_iterator& end, bool) override;
    void start() override;
    void stop() override;
    void pushMessage(Message* message) override { m_inputMessageQueue.push(message); }
    QString getSinkName() override { return "DQPSKDemod"; }
    QJsonObject settings() const;
    QJsonObject report() const;
    bool configure(const QJsonObject& patch, QString& error);
    bool startCapture(const QJsonObject& request, QString& error);
    void stopCapture();
    int workspaceIndex = 0;
    QByteArray geometry;
    bool hidden = false;
signals:
    void settingsChanged();
private:
    bool handleMessage(const Message& message) override;
    void worker();
    static QJsonObject defaults();
    DeviceAPI* m_device;
    mutable std::mutex m_mutex;
    std::condition_variable m_ready;
    std::deque<SampleVector> m_queue;
    size_t m_queuedSamples = 0;
    bool m_quit = false, m_running = false;
    uint64_t m_generation = 1, m_droppedSamples = 0;
    int m_sampleRate = 0;
    qint64 m_hardwareCenter = 0;
    QJsonObject m_settings, m_report;
    bool m_captureRequested = false, m_captureActive = false;
    QJsonObject m_captureRequest;
    std::thread m_worker;
};

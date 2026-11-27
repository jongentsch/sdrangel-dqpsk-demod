// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QPointer>
#include <QTcpServer>
#include <QVector>
#include <QMap>
#include "plugin/plugininterface.h"
class DQPSKDemod;
class ControlServer : public QObject {
    Q_OBJECT
public:
    explicit ControlServer(QObject* parent);
    QVector<QPointer<DQPSKDemod>> channels;
    QMap<QString,QPointer<ChannelGUI>> panels;
private:
    QTcpServer m_server;
    QJsonObject dispatch(const QByteArray& method,const QString& path,const QJsonObject& body,int& status);
};
class DQPSKPlugin : public QObject, public PluginInterface {
    Q_OBJECT
    Q_INTERFACES(PluginInterface)
    Q_PLUGIN_METADATA(IID "org.sdrangel.channel.dqpskdemod")
public:
    explicit DQPSKPlugin(QObject* parent=nullptr);
    const PluginDescriptor& getPluginDescriptor() const override;
    void initPlugin(PluginAPI* api) override;
    void createRxChannel(DeviceAPI* device,BasebandSampleSink** sink,ChannelAPI** api) const override;
    ChannelGUI* createRxChannelGUI(DeviceUISet* device,BasebandSampleSink* sink) const override;
private:
    ControlServer* m_control=nullptr;
    static const PluginDescriptor descriptor;
};

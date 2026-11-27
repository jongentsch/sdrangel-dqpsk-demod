// SPDX-License-Identifier: GPL-3.0-or-later
#include "dqpskplugin.h"
#include "dqpskdemod.h"
#include "dqpskgui.h"
#include "plugin/pluginapi.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QBuffer>

ControlServer::ControlServer(QObject* parent):QObject(parent) {
    m_server.setMaxPendingConnections(8);
    if(!m_server.listen(QHostAddress::LocalHost,8093)) qWarning("DQPSK: loopback control port 8093 unavailable; native GUI remains usable");
    connect(&m_server,&QTcpServer::newConnection,this,[this] {
        while(auto* socket=m_server.nextPendingConnection()) {
            socket->setParent(this);socket->setReadBufferSize(32768);
            auto buffer=std::make_shared<QByteArray>();
            connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
            QTimer::singleShot(3000,socket,[socket]{socket->disconnectFromHost();});
            connect(socket,&QTcpSocket::readyRead,this,[this,socket,buffer] {
                buffer->append(socket->readAll());if(buffer->size()>32768){socket->disconnectFromHost();return;}
                int end=buffer->indexOf("\r\n\r\n");if(end<0)return;
                const auto lines=buffer->left(end).split('\n');const auto first=lines[0].trimmed().split(' ');
                if(first.size()!=3){socket->disconnectFromHost();return;}
                int contentLength=0;bool invalid=false;
                for(auto line:lines.mid(1)) {
                    line=line.trimmed();const auto key=line.left(line.indexOf(':')).toLower();const auto value=line.mid(line.indexOf(':')+1).trimmed();
                    if(key=="content-length"){bool ok;contentLength=value.toInt(&ok);invalid|=!ok||contentLength<0||contentLength>16384;}
                    if(key=="transfer-encoding"||key=="origin")invalid=true;
                }
                if(invalid){socket->disconnectFromHost();return;}
                if(buffer->size()<end+4+contentLength)return;
                QJsonObject body;int status=200;
                if(contentLength){QJsonParseError error;auto doc=QJsonDocument::fromJson(buffer->mid(end+4,contentLength),&error);if(error.error!=QJsonParseError::NoError||!doc.isObject())status=400;else body=doc.object();}
                const auto path=QUrl(QString::fromUtf8(first[1])).path();const auto parts=path.split('/',Qt::SkipEmptyParts);
                QByteArray payload;QByteArray contentType="application/json";
                if(status==200&&first[0]=="GET"&&parts.size()==3&&parts[0]=="channels"&&parts[2]=="panel.png") {
                    auto panel=panels.value(parts[1]);
                    if(panel){QBuffer image(&payload);image.open(QIODevice::WriteOnly);panel->grab().save(&image,"PNG");contentType="image/png";}
                    else {status=404;payload="{\"error\":\"Native panel not available\"}";}
                }else{
                    auto result=status==400?QJsonObject{{"error","Expected a JSON object"}}:dispatch(first[0],path,body,status);
                    payload=QJsonDocument(result).toJson(QJsonDocument::Compact);
                }
                socket->write("HTTP/1.1 "+QByteArray::number(status)+" "+(status<400?"OK":"Error")+"\r\nContent-Type: "+contentType+"\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: "+QByteArray::number(payload.size())+"\r\n\r\n"+payload);
                socket->disconnectFromHost();buffer->clear();
            });
        }
    });
}
QJsonObject ControlServer::dispatch(const QByteArray& method,const QString& path,const QJsonObject& body,int& status) {
    auto parts=path.split('/',Qt::SkipEmptyParts);
    if(method=="GET"&&(parts.isEmpty()||parts==QStringList{"channels"})) {
        QJsonArray all;for(auto p:channels)if(p)all.append(QJsonObject{{"settings",p->settings()},{"report",p->report()}});
        return {{"plugin","DQPSKDemod"},{"version","0.1.0"},{"receiveOnly",true},{"channels",all}};
    }
    if(parts.size()<2||parts[0]!="channels"){status=404;return {{"error","Unknown endpoint"}};}
    DQPSKDemod* channel=nullptr;for(auto p:channels)if(p&&QString::number(p->getUID())==parts[1])channel=p;
    if(!channel){status=404;return {{"error","Channel not found"}};}
    if(method=="GET"&&parts.size()==2)return {{"settings",channel->settings()},{"report",channel->report()}};
    QString error;bool ok=false;
    if(method=="PATCH"&&parts.size()==3&&parts[2]=="settings")ok=channel->configure(body,error);
    else if(method=="POST"&&parts.size()==4&&parts[2]=="capture"&&parts[3]=="start")ok=channel->startCapture(body,error);
    else if(method=="POST"&&parts.size()==4&&parts[2]=="capture"&&parts[3]=="stop"){channel->stopCapture();ok=true;}
    else {status=404;return {{"error","Unknown endpoint"}};}
    if(!ok){status=409;return {{"error",error}};}
    return {{"settings",channel->settings()},{"report",channel->report()}};
}
const PluginDescriptor DQPSKPlugin::descriptor={QStringLiteral("DQPSKDemod"),QStringLiteral("Differential QPSK Symbol Demodulator"),QStringLiteral("0.1.0"),QStringLiteral("SDRangel DQPSK contributors"),QStringLiteral("https://github.com/jongentsch/sdrangel-dqpsk-demod"),true,QStringLiteral("https://github.com/jongentsch/sdrangel-dqpsk-demod")};
DQPSKPlugin::DQPSKPlugin(QObject* parent):QObject(parent){}
const PluginDescriptor& DQPSKPlugin::getPluginDescriptor() const{return descriptor;}
void DQPSKPlugin::initPlugin(PluginAPI* api){m_control=new ControlServer(this);api->registerRxChannel("org.sdrangel.channel.dqpskdemod","DQPSKDemod",this);}
void DQPSKPlugin::createRxChannel(DeviceAPI* device,BasebandSampleSink** sink,ChannelAPI** api) const {
    if(!sink&&!api)return;auto* c=new DQPSKDemod(device);if(m_control)m_control->channels.append(c);if(sink)*sink=c;if(api)*api=c;
}
ChannelGUI* DQPSKPlugin::createRxChannelGUI(DeviceUISet* device,BasebandSampleSink* sink) const{
    auto* channel=dynamic_cast<DQPSKDemod*>(sink);if(!channel)return nullptr;
    auto* gui=new DQPSKGUI(device,channel);if(m_control)m_control->panels.insert(QString::number(channel->getUID()),gui);return gui;
}

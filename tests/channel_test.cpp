// SPDX-License-Identifier: GPL-3.0-or-later
// Loads the actual DLL and feeds a native SDRangel capture through its complete channel path.
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPluginLoader>
#include <QThread>
#include <QtEndian>
#include <iostream>
#include "plugin/plugininterface.h"
#include "plugin/pluginmanager.h"
#include "channel/channelapi.h"
#include "dsp/basebandsamplesink.h"
#include "dsp/dspcommands.h"
#include "device/deviceapi.h"
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);if(argc!=4){std::cerr<<"plugin native.sdriq seconds\n";return 2;}
    QPluginLoader loader(QString::fromLocal8Bit(argv[1]));auto* plugin=qobject_cast<PluginInterface*>(loader.instance());
    if(!plugin){std::cerr<<loader.errorString().toStdString();return 3;}
    PluginManager manager;plugin->initPlugin(manager.getPluginAPI());
    DeviceAPI owner(DeviceAPI::StreamSingleRx,0,nullptr,nullptr,nullptr);
    BasebandSampleSink* sink=nullptr;ChannelAPI* channel=nullptr;plugin->createRxChannel(&owner,&sink,&channel);
    channel->setDeviceAPI(&owner); // Regression: same-owner rebinding must not recurse.
    QFile in(QString::fromLocal8Bit(argv[2]));if(!in.open(QIODevice::ReadOnly))return 4;
    auto h=in.read(32);if(h.size()!=32)return 4;
    const auto rate=qFromLittleEndian<quint32>(h.constData());const auto center=qFromLittleEndian<quint64>(h.constData()+4);
    if(qFromLittleEndian<quint32>(h.constData()+20)!=24)return 5;
    QJsonObject settings{{"format","dqpsk-settings-v1"},{"inputFrequencyOffset",62500},{"title","DQPSK native channel validation"}};
    if(!channel->deserialize(QJsonDocument(settings).toJson()))return 6;
    sink->pushMessage(new DSPSignalNotification(int(rate),qint64(center)));app.processEvents();sink->start();
    std::cout<<"Native channel running UID "<<channel->getUID()<<std::endl;
    QElapsedTimer timer;timer.start();uint64_t read=0;const int seconds=QString::fromLocal8Bit(argv[3]).toInt();
    while(timer.elapsed()<seconds*1000){
        const uint64_t target=uint64_t(timer.elapsed())*rate/1000;
        if(read+8192<=target){
            auto bytes=in.read(8192*8);if(bytes.size()!=8192*8)break;SampleVector samples(8192);
            for(int i=0;i<8192;++i)samples[i]=Sample(qFromLittleEndian<qint32>(bytes.constData()+i*8),qFromLittleEndian<qint32>(bytes.constData()+i*8+4));
            sink->feed(samples.begin(),samples.end(),false);read+=8192;
        }else QThread::msleep(1);
        app.processEvents();
    }
    sink->stop();app.processEvents();channel->destroy();std::cout<<"Native channel stopped"<<std::endl;return 0;
}

// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QPluginLoader>
#include <QTimer>
#include <iostream>
#include <QJsonDocument>
#include <QJsonObject>
#include "plugin/plugininterface.h"
#include "channel/channelapi.h"
#include "channel/channelgui.h"
#include "dsp/basebandsamplesink.h"
int main(int argc,char** argv){
    qInstallMessageHandler([](QtMsgType,const QMessageLogContext&,const QString& text){std::cerr<<text.toStdString()<<'\n';});
    std::cerr<<"before QApplication\n";QApplication app(argc,argv);std::cerr<<"after QApplication\n";if(argc!=3)return 2;
    QPluginLoader loader(QString::fromLocal8Bit(argv[1]));auto* plugin=qobject_cast<PluginInterface*>(loader.instance());if(!plugin)return 3;
    std::cerr<<"plugin loaded\n";BasebandSampleSink* sink=nullptr;ChannelAPI* channel=nullptr;plugin->createRxChannel(nullptr,&sink,&channel);std::cerr<<"channel created\n";
    channel->deserialize(QJsonDocument(QJsonObject{{"format","dqpsk-settings-v1"},{"outputDirectory","C:/Radio/Symbols"}}).toJson());
    auto* gui=plugin->createRxChannelGUI(nullptr,sink);std::cerr<<"GUI created\n";if(!gui)return 4;
    gui->setAttribute(Qt::WA_DontShowOnScreen,true);gui->show();std::cerr<<"GUI shown off-screen\n";app.processEvents();std::cerr<<"events processed\n";const bool ok=gui->grab().save(QString::fromLocal8Bit(argv[2]));std::cerr<<"image saved\n";
    std::cerr<<"deleting GUI\n";delete gui;std::cerr<<"destroying channel\n";channel->destroy();std::cerr<<"returning\n";return ok?0:5;
}

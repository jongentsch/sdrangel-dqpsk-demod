// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include <QPluginLoader>
#include <QDebug>
#include "plugin/plugininterface.h"
int main(int argc,char** argv){QCoreApplication app(argc,argv);if(argc!=2)return 2;QPluginLoader loader(QString::fromLocal8Bit(argv[1]));auto* object=loader.instance();if(!object){qCritical()<<loader.errorString();return 1;}auto* plugin=qobject_cast<PluginInterface*>(object);if(!plugin)return 3;qInfo()<<plugin->getPluginDescriptor().displayedName<<plugin->getPluginDescriptor().version;return 0;}

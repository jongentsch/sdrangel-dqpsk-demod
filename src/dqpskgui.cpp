// SPDX-License-Identifier: GPL-3.0-or-later
#include "dqpskgui.h"
#include "dqpskdemod.h"
#include "device/deviceuiset.h"
#include "gui/rollupcontents.h"
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>
void PhasePlot::paintEvent(QPaintEvent*) {
    QPainter p(this);p.fillRect(rect(),QColor(10,22,31));p.setRenderHint(QPainter::Antialiasing);
    const auto c=rect().center();const double r=std::min(width(),height())*.40;
    p.setPen(QColor(50,80,95));p.drawEllipse(QPointF(c),r,r);p.drawLine(0,c.y(),width(),c.y());p.drawLine(c.x(),0,c.x(),height());
    p.setPen(Qt::NoPen);p.setBrush(QColor(70,220,235));
    for(auto value:points){auto v=value.toArray();if(v.size()==2)p.drawEllipse(QPointF(c.x()+r*v[0].toDouble(),c.y()-r*v[1].toDouble()),2.5,2.5);}
    p.setPen(QColor(170,190,200));p.drawText(8,17,"Differential phase");
}
DQPSKGUI::DQPSKGUI(DeviceUISet* device,DQPSKDemod* channel):ChannelGUI(nullptr),m_channel(channel),m_marker(this) {
    setAttribute(Qt::WA_DeleteOnClose,true);setWindowOwner(channel);setDeviceType(DeviceType::DeviceRx);
    auto* panel=new QWidget(getRollupContents());panel->setObjectName("DQPSK symbols");
    auto* layout=new QVBoxLayout(panel);auto* form=new QFormLayout;
    auto spin=[&](double lo,double hi,int decimals){auto* s=new QDoubleSpinBox(panel);s->setRange(lo,hi);s->setDecimals(decimals);return s;};
    m_offset=new QSpinBox(panel);m_offset->setRange(-100000000,100000000);m_offset->setSingleStep(100);m_offset->setSuffix(" Hz");form->addRow("Tuning offset",m_offset);
    m_rate=spin(1000,12000,3);form->addRow("Symbols / second",m_rate);
    m_rolloff=spin(.05,1,2);m_rolloff->setSingleStep(.05);form->addRow("RRC roll-off",m_rolloff);
    m_rrc=new QCheckBox("Enable RRC matched filter",panel);form->addRow("Receive pulse filter",m_rrc);
    m_carrier=spin(-8000,8000,2);m_carrier->setSuffix(" Hz");form->addRow("Coarse carrier correction",m_carrier);
    m_squelch=spin(-160,0,1);m_squelch->setSuffix(" dB");form->addRow("Minimum matched-filter power",m_squelch);
    m_format=new QComboBox(panel);m_format->addItems({"pi/4-DQPSK","DQPSK","H-DQPSK (P25 II symbols)"});form->addRow("Phase model",m_format);
    connect(m_format,&QComboBox::activated,this,[this](int index){if(index==2){m_rate->setValue(6000);m_rolloff->setValue(1);m_rrc->setChecked(false);m_mapping->setCurrentIndex(0);}});
    m_mapping=new QComboBox(panel);m_mapping->addItems({"Gray: 0,1,3,2","Alternate: 2,1,3,0","Binary: 0,1,2,3"});form->addRow("Provisional dibit map",m_mapping);
    layout->addLayout(form);auto* apply=new QPushButton("Apply demodulator settings",panel);layout->addWidget(apply);
    m_plot=new PhasePlot(panel);m_plot->setMinimumSize(260,150);layout->addWidget(m_plot);
    m_status=new QLabel(panel);m_status->setMinimumHeight(68);m_status->setWordWrap(true);m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);layout->addWidget(m_status);
    m_directory=new QLineEdit(panel);m_directory->setPlaceholderText("Symbol capture directory");layout->addWidget(m_directory);
    auto* capture=new QHBoxLayout;m_seconds=new QSpinBox(panel);m_seconds->setRange(1,1200);m_seconds->setValue(30);m_seconds->setSuffix(" s");capture->addWidget(m_seconds);
    m_record=new QPushButton("Capture symbols",panel);capture->addWidget(m_record);layout->addLayout(capture);
    auto* note=new QLabel("Receive only. Exports phase symbols, confidence and provisional bits; protocol and byte boundaries remain unverified. Control API: 127.0.0.1:8093",panel);note->setWordWrap(true);layout->addWidget(note);
    connect(apply,&QPushButton::clicked,this,[this] {
        QJsonArray map=m_mapping->currentIndex()==0?QJsonArray{0,1,3,2}:m_mapping->currentIndex()==1?QJsonArray{2,1,3,0}:QJsonArray{0,1,2,3};
        QString error;if(!m_channel->configure({{"inputFrequencyOffset",m_offset->value()},{"symbolRate",m_rate->value()},{"rolloff",m_rolloff->value()},
            {"carrierOffsetHz",m_carrier->value()},{"squelchDB",m_squelch->value()},{"pi4",m_format->currentIndex()!=1},{"mode",m_format->currentIndex()==2?"h-dqpsk":m_format->currentIndex()==1?"dqpsk":"pi4-dqpsk"},{"rrcEnabled",m_rrc->isChecked()},{"dibitMap",map},{"outputDirectory",m_directory->text()}},error))m_status->setText(error);
    });
    connect(m_record,&QPushButton::clicked,this,[this]{
        if(m_channel->report()["recording"].toBool())m_channel->stopCapture();
        else {QString error;if(!m_channel->startCapture({{"seconds",m_seconds->value()},{"directory",m_directory->text()}},error))m_status->setText(error);}
    });
    m_marker.setColor(getTitleColor());m_marker.setBandwidth(36000);m_marker.setVisible(true);if(device)device->addChannelMarker(&m_marker);
    connect(&m_marker,&ChannelMarker::changedByCursor,this,[this]{m_channel->setCenterFrequency(m_marker.getCenterFrequency());displaySettings();});
    connect(m_channel,&DQPSKDemod::settingsChanged,this,&DQPSKGUI::displaySettings);
    auto* timer=new QTimer(this);connect(timer,&QTimer::timeout,this,&DQPSKGUI::tick);timer->start(500);
    displaySettings();getRollupContents()->arrangeRollups();setTitleColor(getTitleColor());resize(430,700);
}
DQPSKGUI::~DQPSKGUI(){
    // WorkspaceWindow publishes title/visibility during hide. Do that while the
    // derived virtual methods and channel pointer are still alive.
    hide();
}
void DQPSKGUI::displaySettings(){
    const auto s=m_channel->settings();m_offset->setValue(s["inputFrequencyOffset"].toInt());m_rate->setValue(s["symbolRate"].toDouble());m_rolloff->setValue(s["rolloff"].toDouble());m_carrier->setValue(s["carrierOffsetHz"].toDouble());m_squelch->setValue(s["squelchDB"].toDouble());m_format->setCurrentIndex(s["mode"].toString()=="h-dqpsk"?2:s["pi4"].toBool()?0:1);m_rrc->setChecked(s["rrcEnabled"].toBool());
    auto map=s["dibitMap"].toArray();m_mapping->setCurrentIndex(map==QJsonArray{0,1,3,2}?0:map==QJsonArray{2,1,3,0}?1:2);
    m_directory->setText(s["outputDirectory"].toString());m_marker.setCenterFrequency(m_offset->value());m_marker.setTitle(s["title"].toString());setTitle(s["title"].toString());publishWindowState();
}
void DQPSKGUI::tick(){auto r=m_channel->report();m_plot->points=r["constellation"].toArray();m_plot->update();
    m_status->setText(QString("%1 | %2 symbols\nPhase RMS %3° | concentration %4 | carrier %5 Hz\n%6").arg(r["state"].toString()).arg(qulonglong(r["symbolCount"].toDouble())).arg(r["phaseRMSDegrees"].toDouble(),0,'f',2).arg(r["fourthMoment"].toDouble(),0,'f',3).arg(r["carrierHzRelativeTuning"].toDouble(),0,'f',2).arg(r["captureFile"].toString()));
    m_record->setText(r["recording"].toBool()?"Stop symbol capture":"Capture symbols");setStatusText(r["state"].toString());
}
void DQPSKGUI::resetToDefaults(){QString error;m_channel->configure({{"symbolRate",9600.},{"rolloff",1.},{"carrierOffsetHz",300.},{"pi4",true},{"mode","pi4-dqpsk"},{"rrcEnabled",true},{"dibitMap",QJsonArray{0,1,3,2}}},error);}
void DQPSKGUI::setWorkspaceIndex(int n){m_channel->workspaceIndex=n;}
int DQPSKGUI::getWorkspaceIndex()const{return m_channel->workspaceIndex;}
void DQPSKGUI::setGeometryBytes(const QByteArray& b){m_channel->geometry=b;}
QByteArray DQPSKGUI::getGeometryBytes()const{return m_channel->geometry;}
QString DQPSKGUI::getTitle()const{return m_channel->settings()["title"].toString();}
void DQPSKGUI::zetHidden(bool h){m_channel->hidden=h;}
bool DQPSKGUI::getHidden()const{return m_channel->hidden;}
QByteArray DQPSKGUI::serialize()const{return m_channel->serialize();}
bool DQPSKGUI::deserialize(const QByteArray& b){return m_channel->deserialize(b);}

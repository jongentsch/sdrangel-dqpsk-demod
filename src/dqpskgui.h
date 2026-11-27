// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "channel/channelgui.h"
#include "dsp/channelmarker.h"
#include "util/messagequeue.h"
#include <QJsonArray>
class DQPSKDemod;
class DeviceUISet;
class QSpinBox;
class QDoubleSpinBox;
class QComboBox;
class QLineEdit;
class QPushButton;
class QLabel;
class QCheckBox;
class PhasePlot:public QWidget {
public:
    using QWidget::QWidget;
    QJsonArray points;
protected:
    void paintEvent(QPaintEvent*) override;
};
class DQPSKGUI:public ChannelGUI {
    Q_OBJECT
public:
    DQPSKGUI(DeviceUISet* device,DQPSKDemod* channel);
    ~DQPSKGUI() override;
    void destroy() {delete this;}
    void resetToDefaults() override;
    void setWorkspaceIndex(int n) override;
    int getWorkspaceIndex() const override;
    void setGeometryBytes(const QByteArray& b) override;
    QByteArray getGeometryBytes() const override;
    QString getTitle() const override;
    QColor getTitleColor() const override {return QColor(64,190,220);}
    void zetHidden(bool h) override;
    bool getHidden() const override;
    ChannelMarker& getChannelMarker() override {return m_marker;}
    int getStreamIndex() const override {return 0;}
    void setStreamIndex(int) override {}
    MessageQueue* getInputMessageQueue() override {return &m_queue;}
    QByteArray serialize() const override;
    bool deserialize(const QByteArray& b) override;
private:
    void displaySettings();
    void tick();
    DQPSKDemod* m_channel;
    ChannelMarker m_marker;
    MessageQueue m_queue;
    QSpinBox *m_offset,*m_seconds;
    QDoubleSpinBox *m_rate,*m_rolloff,*m_carrier,*m_squelch;
    QComboBox *m_format,*m_mapping;
    QCheckBox* m_rrc;
    QLineEdit* m_directory;
    QPushButton* m_record;
    QLabel* m_status;
    PhasePlot* m_plot;
};

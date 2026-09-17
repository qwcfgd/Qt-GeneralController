#pragma once
#include <QThread>
#include "infrastructure/ChannelWorker.h"
namespace host {
class ChannelModel : public QObject {
    Q_OBJECT
public:
    explicit ChannelModel(ChannelSettings,QObject *parent=nullptr);
    ~ChannelModel() override;
signals:
    void settingsRequested(host::ChannelSettings);
    void connectionRequested(host::ChannelSettings);
    void disconnectionRequested();
    void refreshRequested();
    void previewRequested(host::ChannelSettings);
    void cancelRequested();
    void scanRequested();
    void bindingChanged(QString,quint32);
    void ready();
    void hardwareChanged(communication::HardwareChannels);
    void stateChanged(communication::ConnectionState,QString);
    void healthChanged(communication::Health,QString);
    void framesReceived(host::FrameBatch);
    void logMessage(QString);
    void taskChanged(host::TaskState,int,QString);
    void scanChanged(bool,QString);
    void commandFinished();
private: QThread m_thread;ChannelWorker *m_worker;
};
}

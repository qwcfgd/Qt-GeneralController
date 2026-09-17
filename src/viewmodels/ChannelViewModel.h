#pragma once
#include "model/ChannelModel.h"
#include "model/FrameTableModel.h"
#include <QSet>
namespace host {
class ChannelViewModel : public QObject {
    Q_OBJECT
public:
    explicit ChannelViewModel(ChannelSettings,QObject *parent=nullptr);
    const ChannelSettings &settings()const{return m_settings;}
    communication::HardwareChannels hardware()const;
    void setReservations(const QSet<quint32> &);
    // 0: idle, 1: healthy, 2: communication lost.
    int communicationIndicator()const;
    bool reservesHardware()const{return connected() || m_connecting;}
    communication::ConnectionState state()const{return m_state;}
    communication::Health health()const{return m_health;}
    QString healthDetail()const{return m_healthDetail;}
    QString error()const{return m_error;}
    QString taskText()const{return m_taskText;}
    QString scanText()const{return m_scanText;}
    int progress()const{return m_progress;}
    TaskState taskState()const{return m_task;}
    bool pending()const{return m_pending;}
    bool scanning()const{return m_scanning;}
    bool connected()const{return m_state==communication::ConnectionState::Connected;}
    bool busy()const{return m_pending || m_scanning || m_task==TaskState::Running || m_state==communication::ConnectionState::Connecting || m_state==communication::ConnectionState::Disconnecting;}
    bool hardwareLocked()const{return connected() || busy();}
    bool canConnect()const;
    bool canStart()const;
    bool canScan()const;
    QString startHint()const;
    FrameTableModel *frames(){return &m_frames;}
    QStringList logs()const{return m_logs;}
    bool setSettings(const ChannelSettings &);
    bool chooseImage(bool flash,const QString &path);
    static QStringList imageCandidates(const QString &path);
    bool exportLogs(const QString &,QString &error)const;
public slots:
    void toggleConnection();
    void refresh();
    void start();
    void cancel();
    void scanHeaders();
    void clearLogs();
    void log(const QString &);
signals:
    void changed();
    void hardwareListChanged();
    void settingsChanged();
    void logAdded(QString);
    void logsCleared();
private:
    ChannelSettings m_settings;
    ChannelModel *m_model;
    FrameTableModel m_frames;
    communication::HardwareChannels m_hardware;
    communication::ConnectionState m_state=communication::ConnectionState::Missing;
    communication::Health m_health=communication::Health::Removed;
    QString m_healthDetail="等待设备",m_error,m_taskText="等待开始",m_scanText;
    TaskState m_task=TaskState::Idle;int m_progress=0;
    QSet<quint32> m_reserved;
    bool m_lost=false,m_wasConnected=false,m_manualDisconnect=false,m_connecting=false;
    bool m_pending=false,m_scanning=false,m_ready=false,m_backendSimulation=false;
    QStringList m_logs;
};
}

#pragma once
#include <QWidget>
#include <QElapsedTimer>
#include "viewmodels/ChannelViewModel.h"
class QComboBox;class QLineEdit;class QCheckBox;class QLabel;
class QPushButton;class QProgressBar;class QTableView;class QPlainTextEdit;
namespace host {
class ChannelPage : public QWidget {
    Q_OBJECT
public:
    explicit ChannelPage(ChannelViewModel *,QWidget *parent=nullptr);
    ChannelViewModel *viewModel()const{return m_vm;}
private:
    void build();
    void loadSettings();
    void applyForm();
    void refreshHardware();
    void refreshPorts(bool deviceChanged=false);
    void editProtocol();
    void editDownload();
    void render();
    void browseImage(bool flash);
    void exportFrames();
    void exportLogs();
    ChannelViewModel *m_vm;
    bool m_loading=false,m_applying=false,m_refreshing=false;
    QComboBox *m_mode,*m_hardware,*m_software,*m_bitrate;
    QLineEdit *m_flashPath,*m_appPath,*m_flashAddress,*m_appAddress;
    QCheckBox *m_reconnect,*m_follow,*m_rxdEnabled;
    QLabel *m_state,*m_busHealth,*m_device,*m_handle,*m_flashInfo,*m_appInfo,*m_hint,*m_task,*m_count,*m_elapsedText,*m_error;
    QPushButton *m_connect,*m_refresh,*m_start,*m_cancel,*m_scan,*m_protocol,*m_downloadSettings,*m_browseFlash;
    QProgressBar *m_progress;
    QTableView *m_table;
    QPlainTextEdit *m_log;
    QWidget *m_parameters,*m_images;
    QElapsedTimer m_elapsed;
    TaskState m_lastTask=TaskState::Idle;
};
}

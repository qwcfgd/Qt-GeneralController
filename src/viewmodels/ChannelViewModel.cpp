#include "ChannelViewModel.h"
#include <QDir>
#include <QSaveFile>
#include <QCoreApplication>
#include "protocol/FlashJob.h"
namespace host {
using namespace communication;
ChannelViewModel::ChannelViewModel(ChannelSettings settings,QObject *parent):
    QObject(parent),m_settings(std::move(settings)),m_model(new ChannelModel(m_settings,this)),m_frames(this),m_backendSimulation(m_settings.simulation) {
    connect(m_model,&ChannelModel::bindingChanged,this,[this](QString key,quint32 handle){
        m_settings.hardwareKey=key;m_settings.handle=handle;emit settingsChanged();
    });
    connect(m_model,&ChannelModel::ready,this,[this](){m_ready=true;emit changed();});
    connect(m_model,&ChannelModel::hardwareChanged,this,[this](HardwareChannels list){m_hardware=std::move(list);emit hardwareListChanged();emit changed();});
    connect(m_model,&ChannelModel::stateChanged,this,[this](ConnectionState state,QString detail){
        if(state==ConnectionState::Connected){m_wasConnected=true;m_lost=false;m_manualDisconnect=false;}
        else if(m_wasConnected && !m_manualDisconnect && (state==ConnectionState::Missing || state==ConnectionState::Fault))m_lost=true;
        m_state=state;m_error=state==ConnectionState::Fault?detail:QString();
        if(state!=ConnectionState::Connected){m_health=Health::Removed;m_healthDetail=connectionText(state);}
        emit changed();
    });
    connect(m_model,&ChannelModel::healthChanged,this,[this](Health h,QString detail){if(m_health!=h && !detail.isEmpty())log(detail);
        m_health=h;m_healthDetail=std::move(detail);emit changed();});
    connect(m_model,&ChannelModel::framesReceived,&m_frames,&FrameTableModel::append);
    connect(m_model,&ChannelModel::logMessage,this,&ChannelViewModel::log);
    connect(m_model,&ChannelModel::taskChanged,this,[this](TaskState state,int progress,QString text){
        m_task=state;m_progress=progress;m_taskText=std::move(text);emit changed();
    });
    connect(m_model,&ChannelModel::scanChanged,this,[this](bool scanning,QString text){
        m_scanning=scanning;m_scanText=std::move(text);emit changed();
    });
    connect(m_model,&ChannelModel::commandFinished,this,[this](){m_pending=false;m_connecting=false;emit changed();});
}
bool ChannelViewModel::setSettings(const ChannelSettings &s) {
    if(busy() || s.bus!=m_settings.bus)return false;
    auto old=m_settings.toJson(),next=s.toJson();
    for(const auto &key:{"flashPath","applicationPath","flashAddress","applicationAddress","flashRequired","repeatDownloadEnabled","repeatDownloadCount","repeatDownloadIntervalMs","downloadProfile"}){old.remove(key);next.remove(key);}
    const bool configurationChanged=old!=next;
    if(connected() && configurationChanged)return false;
    const bool modeChanged=m_settings.simulation!=s.simulation;
    m_settings=s;
    if(modeChanged){m_settings.hardwareKey.clear();m_settings.handle=0;}
    SoftwareChannelConfiguration config;QString error;
    const bool valid=s.toConfiguration(config,error);m_error=error;
    if(valid && (m_backendSimulation!=m_settings.simulation || (m_lost && configurationChanged))){m_backendSimulation=m_settings.simulation;m_pending=true;m_model->settingsRequested(m_settings);}
    emit settingsChanged();emit changed();return valid;
}
communication::HardwareChannels ChannelViewModel::hardware() const {
    auto result=m_hardware;
    for(auto &h:result)if(m_reserved.contains(h.handle))h.available=false;
    return result;
}
void ChannelViewModel::setReservations(const QSet<quint32> &ports) {
    if(m_reserved==ports)return;
    m_reserved=ports;emit hardwareListChanged();emit changed();
}
int ChannelViewModel::communicationIndicator()const {
    if(connected())return (m_health==Health::Ready || m_health==Health::Sleeping)?1:2;
    return m_lost?2:0;
}
bool ChannelViewModel::canConnect() const {
    if(!m_ready || busy() || connected() || (m_settings.hardwareKey.isEmpty() && !m_settings.handle))return false;
    SoftwareChannelConfiguration c;QString error;if(!m_settings.toConfiguration(c,error))return false;
    int matches=0;
    for(const auto &h:hardware()) {
        if(!h.available)continue;
        if((m_settings.hardwareKey.isEmpty() && !m_settings.handle) ||
           ((!m_settings.hardwareKey.isEmpty()?m_settings.hardwareKey==h.key:m_settings.handle==h.handle)))++matches;
    }
    return matches==1;
}
bool ChannelViewModel::canStart() const{return startHint().isEmpty();}
QString ChannelViewModel::startHint() const {
    if(busy())return "通道正在执行操作";
    SoftwareChannelConfiguration c;QString error;if(!m_settings.toConfiguration(c,error))return error;
    if(!connected())return "请先连接软件通道";
    if(!m_settings.simulation){
        if(m_settings.bus!=Bus::Lin)return "真实 CAN 下载尚未完成适配；当前支持 PLIN / LIN 真实下载";
        if(m_health!=Health::Ready&&m_health!=Health::Sleeping)return "硬件通道状态异常";
        boot::FlashProfile profile;if(!boot::FlashProfile::fromJson(m_settings.downloadProfile,profile,error))return error;
        if(profile.keyLibrary.trimmed().isEmpty())return "请配置已授权的安全访问 DLL";
        if(!profile.keyLibrary.trimmed().isEmpty()&&profile.keyProvider!="external-generatekeyex")return "下载设置中请选择 External GenerateKeyEx 算法";
        const QDir app(QCoreApplication::applicationDirPath());
        const auto key=QDir::isRelativePath(profile.keyLibrary)?app.absoluteFilePath(profile.keyLibrary):profile.keyLibrary;
        if(!profile.keyLibrary.trimmed().isEmpty()&&(!QFileInfo::exists(key)||!QFileInfo::exists(app.filePath("seedkey/SeedkeyBridge32.exe"))))return "Seedkey DLL 或 32 位调用程序缺失，请使用完整发布目录";
    }
    if(!imageReady(m_settings.applicationPath))return "请选择有效的 Application 镜像";
    if(m_settings.flashRequired && !imageReady(m_settings.flashPath))return "此配置需要有效的 Flash Driver 镜像";
    return {};
}
bool ChannelViewModel::canScan()const {
    return connected() && !busy() && !m_settings.simulation && m_settings.bus==Bus::Lin && (m_health==Health::Ready || m_health==Health::Sleeping);
}
void ChannelViewModel::toggleConnection() {
    if(m_pending)return;
    if(connected()){m_manualDisconnect=true;m_wasConnected=false;m_lost=false;m_pending=true;m_model->disconnectionRequested();}
    else if(canConnect()){m_connecting=true;m_pending=true;m_error.clear();m_model->connectionRequested(m_settings);}
    emit changed();
}
void ChannelViewModel::refresh(){if(!m_pending){m_pending=true;m_model->refreshRequested();emit changed();}}
void ChannelViewModel::start(){if(canStart()){m_pending=true;m_model->previewRequested(m_settings);emit changed();}}
void ChannelViewModel::cancel(){if(m_task==TaskState::Running || m_scanning)m_model->cancelRequested();}
void ChannelViewModel::scanHeaders(){if(canScan()){m_pending=true;m_model->scanRequested();emit changed();}}
void ChannelViewModel::log(const QString &text){
    const QString line=QDateTime::currentDateTime().toString("HH:mm:ss.zzz")+"  "+text;
    m_logs.append(line);while(m_logs.size()>ChannelPageInitialValues::logCapacity)m_logs.removeFirst();emit logAdded(line);
}
void ChannelViewModel::clearLogs(){m_logs.clear();emit logsCleared();}
bool ChannelViewModel::chooseImage(bool flash,const QString &path) {
    if(path.isEmpty() || busy())return false;
    if(!imageReady(path)){m_error="镜像必须是可读取的非空 BIN / HEX 文件。";log(m_error);emit changed();return false;}
    auto s=m_settings;if(flash)s.flashPath=QFileInfo(path).absoluteFilePath();else s.applicationPath=QFileInfo(path).absoluteFilePath();
    return setSettings(s);
}
QStringList ChannelViewModel::imageCandidates(const QString &path) {
    QFileInfo f(path);if(f.isFile())return imageReady(path)?QStringList{f.absoluteFilePath()}:QStringList{};
    if(!f.isDir())return {};
    QStringList results;QDir dir(f.absoluteFilePath());
    for(const auto &info:dir.entryInfoList(QDir::Files|QDir::Readable,QDir::Name)) {
        if(imageReady(info.absoluteFilePath()))results.append(info.absoluteFilePath());
        if(results.size()>=1000)break;
    }return results;
}
bool ChannelViewModel::exportLogs(const QString &path,QString &error) const {
    error.clear();QSaveFile f(path);const QByteArray data=(m_logs.join('\n')+'\n').toUtf8();
    if(!f.open(QIODevice::WriteOnly) || f.write(data)!=data.size() || !f.commit()){error="日志导出失败。";return false;}return true;
}
}

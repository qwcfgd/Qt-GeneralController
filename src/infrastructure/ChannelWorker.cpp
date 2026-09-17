#include "ChannelWorker.h"
#include <QThread>
#include "protocol/PluginKey.h"
namespace host {
using namespace communication;
static void stampFrame(FrameRecord &record,const QElapsedTimer &clock){
    record.timestamp=QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    record.relativeTime=QString::number(clock.nsecsElapsed()/1000);
}
class PreviewBackend final : public HardwareBackend {
public:
    explicit PreviewBackend(Bus bus):m_bus(bus){}
    Bus bus() const override{return m_bus;}
    HardwareChannels scan(QString &error) override {
        error.clear();HardwareChannels list;
        for(int port=1;port<=2;++port) {
            HardwareChannel h;h.bus=m_bus;h.handle=(m_bus==Bus::Can?0xf100:0xf200)+port;
            h.key=QString("preview:%1:%2").arg(m_bus==Bus::Can?"CAN":"LIN").arg(port);
            h.label=m_bus==Bus::Can?"模拟 CAN 双通道适配器":"模拟 LIN 双通道适配器";
            h.deviceId=1;h.controller=port;h.persistentIdentity=true;list.append(h);
        }return list;
    }
    bool open(const HardwareChannel &,const SoftwareChannelConfiguration &,QString &error) override{error.clear();return true;}
    bool close(QString &error) override{error.clear();return true;}
    Health health(QString &detail) override{detail="模拟通道已就绪";return Health::Ready;}
private:Bus m_bus;
};
void ChannelWorker::initialize() {
    Q_ASSERT(QThread::currentThread()==thread());
    m_traceClock.start();
    m_receive=new QTimer(this);m_receive->setInterval(20);connect(m_receive,&QTimer::timeout,this,&ChannelWorker::receive);
    m_scan=new QTimer(this);connect(m_scan,&QTimer::timeout,this,&ChannelWorker::scanTick);
    m_repeat=new QTimer(this);m_repeat->setSingleShot(true);m_repeat->setTimerType(Qt::PreciseTimer);
    connect(m_repeat,&QTimer::timeout,this,&ChannelWorker::beginPreviewRound);
    createSession();m_receive->start();emit ready();
}
void ChannelWorker::createSession() {
    m_session.reset();m_can.reset();m_lin.reset();
    SoftwareChannelConfiguration config;QString error;
    if(!m_settings.toConfiguration(config,error)){emit logMessage(error);return;}
    std::unique_ptr<HardwareBackend> backend;
    if(m_settings.simulation){backend.reset(new PreviewBackend(m_settings.bus));}
    else if(m_settings.bus==Bus::Lin){m_lin.reset(new tstPeakLin());backend.reset(new PeakLinBackend(*m_lin));}
    else {m_can.reset(new tstPeakCan());backend.reset(new PeakCanBackend(*m_can));}
    m_session.reset(new SoftwareChannel(config,std::move(backend)));
    connect(m_session.get(),&SoftwareChannel::hardwareChanged,this,&ChannelWorker::hardwareChanged);
    connect(m_session.get(),&SoftwareChannel::stateChanged,this,[this](ConnectionState s,const QString &detail){
        if(s==ConnectionState::Connected) {
            const auto c=m_session->configuration();
            m_settings.hardwareKey=c.hardwareKey;m_settings.handle=c.preferredHandle;
            emit bindingChanged(c.hardwareKey,c.preferredHandle);
        }
        emit stateChanged(s,detail);
        emit logMessage(connectionText(s)+(detail.isEmpty()?QString():" · "+detail));
    });
    connect(m_session.get(),&SoftwareChannel::healthChanged,this,&ChannelWorker::healthChanged);
    connect(m_session.get(),&SoftwareChannel::connectionClosing,this,[this](){
        cancelTask();if(m_scanning)stopScan("扫描已停止：连接正在关闭");
    });
    m_session->startMonitoring();
}
bool ChannelWorker::apply(const ChannelSettings &settings) {
    SoftwareChannelConfiguration c;QString error;
    if(!settings.toConfiguration(c,error)){emit logMessage(error);return false;}
    if(m_session && m_session->state()==ConnectionState::Connected){emit logMessage("请先断开后修改硬件及协议配置。");return false;}
    const bool recreate=!m_session || m_settings.simulation!=settings.simulation;
    m_settings=settings;
    if(recreate)createSession();
    else {
        if(!m_session->configure(c)){emit logMessage(m_session->error());return false;}
        m_session->poll();
    }
    return bool(m_session);
}
void ChannelWorker::updateSettings(ChannelSettings s){apply(s);emit commandFinished();}
void ChannelWorker::connectChannel(ChannelSettings s) {
    if(apply(s) && m_session) m_session->connectChannel();
    emit commandFinished();
}
void ChannelWorker::disconnectChannel(){if(m_session)m_session->disconnectChannel();emit commandFinished();}
void ChannelWorker::refresh(){if(m_session)m_session->poll();emit commandFinished();}
void ChannelWorker::startPreview(ChannelSettings s) {
    SoftwareChannelConfiguration config;QString error;
    if(!s.toConfiguration(config,error)){emit logMessage(error);emit commandFinished();return;}
    if(m_running || m_scanning || s.simulation!=m_settings.simulation || !m_session ||
       m_session->state()!=ConnectionState::Connected || !imageReady(s.applicationPath) ||
       (s.flashRequired && !imageReady(s.flashPath))) {
        emit logMessage("无法开始下载：请检查连接和镜像。");emit commandFinished();return;
    }
    m_settings=s;m_progress=0;m_round=0;m_totalRounds=s.repeatDownloadEnabled?s.repeatDownloadCount:1;m_running=true;
    emit logMessage(QString(s.simulation?"开始模拟下载，共 %1 次；不访问物理总线。":"开始真实 LIN 下载，共 %1 次。").arg(m_totalRounds));
    beginPreviewRound();emit commandFinished();
}
void ChannelWorker::beginPreviewRound() {
    if(!m_running)return;
    if(!m_session || m_session->state()!=ConnectionState::Connected){failPreview("连接已关闭");return;}
    if(!imageReady(m_settings.applicationPath) || (m_settings.flashRequired && !imageReady(m_settings.flashPath))){
        failPreview("镜像文件不存在或不可读取");return;
    }
    m_waiting=false;m_progress=0;++m_round;
    emit logMessage(QString("开始第 %1/%2 次下载").arg(m_round).arg(m_totalRounds));
    emit taskChanged(TaskState::Running,0,QString("第 %1/%2 次 · 检查镜像").arg(m_round).arg(m_totalRounds));
    beginDownload();
}
void ChannelWorker::failPreview(const QString &reason) {
    if(m_job)m_job->cancel();
    m_repeat->stop();m_running=m_waiting=false;
    emit taskChanged(TaskState::Failed,m_progress,"下载停止 · "+reason);emit logMessage("重复下载已停止："+reason);
}
void ChannelWorker::completeRound(const QString &result) {
    if(!m_running)return;
    m_progress=100;
    emit logMessage(QString("第 %1/%2 次下载完成").arg(m_round).arg(m_totalRounds));
    if(m_round<m_totalRounds){
        m_waiting=true;
        emit taskChanged(TaskState::Running,100,QString("第 %1/%2 次完成 · 等待 %3 s").arg(m_round).arg(m_totalRounds).arg(m_settings.repeatDownloadIntervalMs/1000.0,0,'f',1));
        m_repeat->start(m_settings.repeatDownloadIntervalMs);
    }else{
        m_running=false;
        emit taskChanged(TaskState::Completed,100,QString("%1 · 共 %2 次").arg(result).arg(m_totalRounds));
        emit logMessage(result);
    }
}
void ChannelWorker::clearProtocol(){
    if(m_job)m_job->cancel();
    m_job.reset();m_uds.reset();m_transport.reset();m_ecu.reset();m_canTransport.reset();m_canEcu.reset();
}
void ChannelWorker::beginDownload(){
    clearProtocol();
    boot::FirmwareImage application,driver;QString error;
    if(!boot::FirmwareImage::load(m_settings.applicationPath,m_settings.applicationAddress.toUInt(nullptr,16),application,error)||
       (m_settings.flashRequired&&!boot::FirmwareImage::load(m_settings.flashPath,m_settings.flashAddress.toUInt(nullptr,16),driver,error))){failPreview(error);return;}
    boot::FlashProfile profile;
    if(!boot::FlashProfile::fromJson(m_settings.downloadProfile,profile,error)){failPreview(error);return;}
    profile.name=m_settings.profileId;profile.session=quint8(m_settings.programmingSession);profile.securityLevel=quint8(m_settings.securityLevel);
    std::unique_ptr<boot::KeyProvider> key;
    if(m_settings.simulation)key=std::make_unique<boot::SimulationKey>();
    else {
        if(profile.keyLibrary.trimmed().isEmpty()){failPreview("请配置已授权的安全访问 DLL");return;}
        if(profile.keyProvider!="external-generatekeyex"){failPreview("真实下载请选用 External GenerateKeyEx");return;}
        profile.simulationOnly=false;key=std::make_unique<boot::PluginKey>(profile.keyLibrary);
    }
    if(!m_settings.simulation){profile.simulationOnly=false;if(m_lin)m_lin->clearMsg();}
    if(m_settings.bus==Bus::Lin){
        const auto nad=quint8(m_settings.nad.toUInt(nullptr,16));
        if(m_settings.simulation)m_ecu.reset(new boot::SimulatedLinEcu(nad,profile));
        const int slotMs=m_settings.simulation?1:qMax(20,(150000+m_settings.bitrate-1)/m_settings.bitrate);
        m_transport.reset(new boot::LinTransport(nad,slotMs,qMax(1000,slotMs*3),[this](quint8 id,const QByteArray &data,QString &error){
            if(m_ecu){
                if(!m_ecu->write(id,data,error))return false;
                if(id==0x3d){const auto frame=m_ecu->takeResponseFrame();if(!frame.isEmpty())m_transport->receiveFrame(id,frame);}
                return true;
            }
            if(!m_lin){error="LIN hardware unavailable";return false;}
            TLINMsg message={};message.FrameId=id;message.Length=8;message.ChecksumType=cstClassic;
            message.Direction=id==0x3c?dirPublisher:dirSubscriber;
            for(int i=0;i<8;++i)message.Data[i]=i<data.size()?BYTE(data[i]):BYTE(0xff);
            if(!m_lin->sendRaw(message)){error=m_lin->lastErrorText();return false;}return true;
        }));
        m_transport->requireTransmitConfirmation(!m_settings.simulation);
        connect(m_transport.get(),&boot::DiagnosticTransport::cancelled,this,[this]{if(m_ecu)m_ecu->clearWire();});
        connect(m_transport.get(),&boot::LinTransport::trace,this,[this](bool tx,quint8 id,const QByteArray &bytes){
            // A 0x3D header opens one receive event. Its eventual response or
            // no-response timeout is the only row shown for that event.
            if(tx&&bytes.isEmpty())return;
            if(!m_settings.simulation){
                // Record every requested bus transmission immediately. The later
                // PLIN 0x3C queue event is a separate RxD readback.
                if(!tx)return;
                FrameRecord f;stampFrame(f,m_traceClock);f.channel=m_settings.softwareId;
                f.direction="TX";f.identifier=QString("0x%1").arg(id,2,16,QChar('0')).toUpper();
                f.data=QString::fromLatin1(bytes.toHex(' ')).toUpper();f.length=bytes.size();
                f.status="发送";emit framesReceived({f});return;
            }
            FrameRecord f;stampFrame(f,m_traceClock);f.channel=m_settings.softwareId;
            f.direction="SIM "+QString(tx?(bytes.isEmpty()?"HEADER":"TX"):"RX");
            f.identifier=QString("0x%1").arg(id,2,16,QChar('0')).toUpper();f.data=QString::fromLatin1(bytes.toHex(' ')).toUpper();f.length=bytes.size();
            f.status="LIN UDS · Classic checksum";emit framesReceived({f});
        });
    }else{
        boot::CanOptions options;options.txId=m_settings.requestId.toUInt(nullptr,16);
        options.rxId=m_settings.responseId.toUInt(nullptr,16);options.extended=m_settings.extendedId;
        if(!boot::CanOptions::fromJson(m_settings.canNetwork,options,error)){failPreview(error);return;}
        if(!m_settings.simulation){failPreview("真实 CAN 下载须完成目标适配与发送确认验证");return;}
        m_canEcu.reset(new boot::SimulatedCanEcu(options,profile));
        m_canTransport.reset(new boot::CanTransport(options,[this](const boot::CanFrame &frame,quint64 token,QString &error){
            return m_canEcu->writeFromHost(frame,token,error);
        }));
        m_canEcu->attach(*m_canTransport);
        connect(m_canEcu.get(),&boot::SimulatedCanEcu::failed,m_canTransport.get(),&boot::CanTransport::linkFailed);
        connect(m_canTransport.get(),&boot::CanTransport::trace,this,[this](bool tx,const boot::CanFrame &frame){
            FrameRecord f;stampFrame(f,m_traceClock);f.channel=m_settings.softwareId;
            f.direction=tx?"SIM TX":"SIM RX";f.identifier=QString("0x%1").arg(frame.id,frame.extended?8:3,16,QChar('0')).toUpper();
            f.data=QString::fromLatin1(frame.data.toHex(' ')).toUpper();f.length=frame.data.size();
            f.status=frame.extended?"CAN ISO-TP · 29 bit":"CAN ISO-TP · 11 bit";emit framesReceived({f});
        });
    }
    boot::DiagnosticTransport *network=m_settings.bus==Bus::Lin?static_cast<boot::DiagnosticTransport*>(m_transport.get()):m_canTransport.get();
    boot::SessionOptions timing{m_settings.p2Ms,m_settings.p2StarMs,m_settings.maxPendingMs,m_settings.testerPresentEnabled?m_settings.testerPresentMs:0};
    m_uds.reset(new boot::UdsSession(*network,timing));
    connect(m_uds.get(),&boot::UdsSession::notice,this,&ChannelWorker::logMessage);
    emit logMessage(QString("镜像快照 %1 B · SHA256 %2 · 配置 %3").arg(application.size).arg(QString::fromLatin1(application.sha256.toHex())).arg(profile.name));
    m_job.reset(new boot::FlashJob(*m_uds,profile,std::move(key)));
    connect(m_job.get(),&boot::FlashJob::notice,this,&ChannelWorker::logMessage);
    connect(m_job.get(),&boot::FlashJob::stepEvent,this,&ChannelWorker::logMessage);
    connect(m_job.get(),&boot::FlashJob::progress,this,[this](int value,const QString &step){
        if(!m_running)return;m_progress=value;emit taskChanged(TaskState::Running,value,QString("第 %1/%2 次 · %3").arg(m_round).arg(m_totalRounds).arg(step));
    });
    connect(m_job.get(),&boot::FlashJob::finished,this,[this](bool ok,const QString &why){if(ok)completeRound(why);else failPreview(why);});
    if(!m_job->start(application,driver,error))failPreview(error);
}

void ChannelWorker::cancelTask() {
    if(m_scanning)stopScan("扫描已取消");
    if(!m_running)return;
    if(m_job)m_job->cancel();
    m_repeat->stop();m_running=m_waiting=false;
    emit taskChanged(TaskState::Cancelled,m_progress,"下载已取消");
    emit logMessage("用户取消或连接关闭，下载任务已停止。");
}
void ChannelWorker::receive() {
    if(!m_session || m_session->state()!=ConnectionState::Connected || m_settings.simulation)return;
    FrameBatch batch;
    if(m_lin) {
        TLINRcvMsg messages[64]={};const auto n=m_lin->recvRaw(64,messages);
        const auto error=m_lin->lastErrorCode();
        if(error!=errOK && error!=errRcvQueueEmpty) {
            const auto text=m_lin->lastErrorText();if(m_running)failPreview(text);if(text!=m_receiveError){emit logMessage(text);m_receiveError=text;}return;
        }
        m_receiveError.clear();
        for(DWORD i=0;i<n;++i) {
            const auto &m=messages[i];if(m.Type!=mstStandard){
                if(m.Type>=mstOverrun){++m_scanErrors;emit logMessage("LIN 接收队列溢出。");if(m_running)failPreview("LIN 接收队列溢出");}continue;
            }
            const auto noResponseFlags=MSG_ERR_SLAVE_NOT_RESPONDING|MSG_ERR_TIMEOUT;
            const bool absent=(m.ErrorFlags&noResponseFlags) && !(m.ErrorFlags&~noResponseFlags);
            const bool bad=(m.ErrorFlags!=0 && !absent) || (m.ErrorFlags==0 && (m.Length<1 || m.Length>8));
            const bool valid=!m.ErrorFlags && m.Length>=1 && m.Length<=8;
            if(m_transport&&(m.FrameId&0x3f)==0x3c)m_transport->confirmTransmitted(QByteArray(reinterpret_cast<const char*>(m.Data),qMin(int(m.Length),8)),valid);
            if(m_transport)m_transport->receiveFrame(m.FrameId&0x3f,QByteArray(reinterpret_cast<const char*>(m.Data),qMin(int(m.Length),8)),absent,bad);
            FrameRecord record;stampFrame(record,m_traceClock);record.channel=m_settings.softwareId;
            const auto frameId=quint8(m.FrameId&0x3f);const bool transmitReadback=frameId==0x3c;
            record.direction="RX";record.identifier=QString("0x%1").arg(frameId,2,16,QChar('0')).toUpper();
            record.length=m.Length;
            if(valid)record.data=QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(m.Data),qMin(int(m.Length),8)).toHex(' ')).toUpper();
            record.status=transmitReadback&&valid?"发送回读":(absent?"无从节点响应":(bad?QString("LIN 错误 0x%1").arg(m.ErrorFlags,0,16):"有效响应"));
            record.error=bad;record.warning=absent;
            if(!transmitReadback||m_settings.rxdEnabled)batch.append(record);
            if(m_scanning){++m_scanEvents;if(valid)++m_scanResponses;if(bad)++m_scanErrors;}
        }
    } else if(m_can) {
        TPCANMsg messages[64]={};TPCANTimestamp stamps[64]={};const auto n=m_can->recvRaw(64,messages,stamps);
        for(DWORD i=0;i<n;++i) {
            const auto &m=messages[i];FrameRecord r;stampFrame(r,m_traceClock);
            r.channel=m_settings.softwareId;r.direction="RX";r.identifier=QString("0x%1").arg(m.ID,0,16).toUpper();
            r.length=m.LEN;r.data=QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(m.DATA),qMin(int(m.LEN),8)).toHex(' ')).toUpper();
            r.status=(m.MSGTYPE&PCAN_MESSAGE_STATUS)?"硬件状态":"CAN";r.warning=m.MSGTYPE&PCAN_MESSAGE_STATUS;batch.append(r);
        }
    }
    if(!batch.isEmpty())emit framesReceived(batch);
}
void ChannelWorker::startHeaderScan() {
    QString detail;const auto health=m_lin?m_lin->hardwareHealth(&detail):Health::Removed;
    if(m_scanning || m_running || !m_lin || !m_session || m_session->state()!=ConnectionState::Connected ||
       (health!=Health::Ready && health!=Health::Sleeping)) {
        emit logMessage("扫描需要处于活动状态的真实 LIN 主机连接。"+detail);emit commandFinished();return;
    }
    m_lin->clearMsg();m_scanning=true;m_scanId=0;m_scanSent=m_scanEvents=m_scanResponses=m_scanErrors=0;
    m_scan->setInterval(qMax(100,2000000/m_settings.bitrate));
    emit scanChanged(true,"扫描 00–3B、3D · 仅帧头");emit logMessage("开始帧头扫描；不发送控制数据或 UDS 请求。");
    m_scan->start();scanTick();emit commandFinished();
}
void ChannelWorker::scanTick() {
    if(!m_scanning)return;
    if(m_scanId==0x3c)++m_scanId;
    if(m_scanId>0x3d) {
        receive();
        m_scanErrors+=qAbs(m_scanSent-m_scanEvents);
        stopScan(QString("扫描完成 · %1 个帧头 / %2 个事件 / %3 个有效响应 / %4 个错误")
                 .arg(m_scanSent).arg(m_scanEvents).arg(m_scanResponses).arg(m_scanErrors));
        return;
    }
    TLINMsg message={};message.FrameId=BYTE(m_scanId++);message.Length=8;
    message.Direction=dirSubscriberAutoLength;message.ChecksumType=message.FrameId==0x3d?cstClassic:cstAuto;
    if(!m_lin->sendRaw(message)){stopScan("扫描失败："+m_lin->lastErrorText());return;}
    ++m_scanSent;
}
void ChannelWorker::stopScan(const QString &text){m_scan->stop();m_scanning=false;emit scanChanged(false,text);emit logMessage(text);}
void ChannelWorker::shutdown() {
    clearProtocol();
    if(m_receive)m_receive->stop();
    if(m_scan)m_scan->stop();
    if(m_repeat)m_repeat->stop();
    m_running=m_scanning=m_waiting=false;
    m_session.reset();m_can.reset();m_lin.reset();
}
}

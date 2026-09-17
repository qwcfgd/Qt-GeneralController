#include "HostTypes.h"
#include <QRegularExpression>
#include "protocol/FlashJob.h"
#include "protocol/CanTransport.h"
namespace host {
ChannelSettings ChannelSettings::defaults(communication::Bus bus) {
    ChannelSettings s;s.bus=bus;
    if(bus==communication::Bus::Lin){s.softwareId=ChannelPageInitialValues::linName;s.profileId=ChannelPageInitialValues::linProfile;s.bitrate=ChannelPageInitialValues::linBitrate;}
    return s;
}
static bool hex(const QString &text,quint32 maximum,quint32 &number) {
    static const QRegularExpression pattern("^(?:0[xX])?[0-9a-fA-F]{1,8}$");
    const QString input=text.trimmed();bool ok=false;
    if(!pattern.match(input).hasMatch())return false;
    number=input.toUInt(&ok,16);return ok && number<=maximum;
}
bool ChannelSettings::toConfiguration(communication::SoftwareChannelConfiguration &c,QString &error) const {
    error.clear();c={};c.bus=bus;c.softwareId=softwareId.trimmed();
    c.hardwareKey=hardwareKey;c.preferredHandle=handle;c.bitrate=bitrate;c.autoReconnect=autoReconnect;
    c.uds.profileId=profileId.trimmed();c.uds.p2Ms=p2Ms;c.uds.p2StarMs=p2StarMs;
    c.uds.testerPresentMs=testerPresentEnabled?testerPresentMs:0;c.uds.maxPendingMs=maxPendingMs;
    c.uds.programmingSession=quint8(programmingSession);c.uds.securityLevel=quint8(securityLevel);
    if(c.softwareId.isEmpty() || c.softwareId.size()>64 || c.uds.profileId.isEmpty() || c.uds.profileId.size()>80)
        error="请填写有效的软件通道名称和 UDS 配置名称。";
    else if(p2Ms<1 || p2Ms>60000 || p2StarMs<1 || p2StarMs>600000 || testerPresentMs<0 ||
            testerPresentMs>600000 || (testerPresentEnabled && testerPresentMs==0) || maxPendingMs<qMax(p2Ms,p2StarMs) || maxPendingMs>3600000 ||
            programmingSession<1 || programmingSession>0x7f || securityLevel<1 || securityLevel>0x7d || !(securityLevel&1))
        error="请检查 UDS 时序、会话及安全级别；安全请求级别需为奇数。";
    if(error.isEmpty() && (repeatDownloadCount<1 || repeatDownloadCount>10000 || repeatDownloadIntervalMs<0 || repeatDownloadIntervalMs>86400000))
        error="重复下载次数应为 1–10000，等待间隔应为 0–86400000 ms。";
    quint32 request=0,response=0,functional=0,nadValue=0,address=0;
    if(error.isEmpty() && bus==communication::Bus::Can) {
        const quint32 limit=extendedId?0x1fffffff:0x7ff;
        if(!hex(requestId,limit,request) || !hex(responseId,limit,response) || !hex(functionalId,limit,functional) ||
           request==response)error="CAN ID 无效，或请求与响应 ID 相同。";
        const QList<int> supported={1000000,800000,500000,250000,125000,100000,50000,20000,10000,5000};
        if(!supported.contains(bitrate))error="请选择支持的 CAN 波特率。";
        c.transport.requestId=request;c.transport.responseId=response;c.transport.functionalId=functional;
        c.transport.extendedId=extendedId;
    }
    if(error.isEmpty() && bus==communication::Bus::Lin) {
        if(!hex(nad,0x7d,nadValue) || nadValue==0)error="LIN NAD 必须为 01–7D（十六进制）。";
        if(bitrate<1000 || bitrate>20000)error="LIN 波特率范围为 1000–20000 bit/s。";
        c.transport.nad=quint8(nadValue);c.linMode=2;
    }
    if(error.isEmpty() && (!hex(applicationAddress,0xffffffff,address) || !hex(flashAddress,0xffffffff,address)))
        error="镜像基址必须为 32 位十六进制地址。";
    boot::CanOptions canOptions;canOptions.txId=request;canOptions.rxId=response;canOptions.extended=extendedId;
    if(error.isEmpty() && bus==communication::Bus::Can && !boot::CanOptions::fromJson(canNetwork,canOptions,error))return false;
    boot::FlashProfile profile;
    if(error.isEmpty()&&!boot::FlashProfile::fromJson(downloadProfile,profile,error))return false;
    return error.isEmpty() && c.isValid();
}
QJsonObject ChannelSettings::toJson() const {
    return {{"bus",bus==communication::Bus::Can?"CAN":"LIN"},{"softwareId",softwareId},{"profileId",profileId},
        {"canNetwork",canNetwork},{"downloadProfile",downloadProfile},{"hardwareKey",hardwareKey},{"handle",int(handle)},{"bitrate",bitrate},{"simulation",simulation},
        {"autoReconnect",autoReconnect},{"extendedId",extendedId},{"requestId",requestId},{"responseId",responseId},
        {"functionalId",functionalId},{"nad",nad},{"p2Ms",p2Ms},{"p2StarMs",p2StarMs},
        {"testerPresentEnabled",testerPresentEnabled},{"testerPresentMs",testerPresentMs},{"maxPendingMs",maxPendingMs},{"programmingSession",programmingSession},
        {"securityLevel",securityLevel},{"flashPath",flashPath},{"applicationPath",applicationPath},
        {"flashAddress",flashAddress},{"applicationAddress",applicationAddress},{"flashRequired",flashRequired},{"rxdEnabled",rxdEnabled},{"repeatDownloadEnabled",repeatDownloadEnabled},
        {"repeatDownloadCount",repeatDownloadCount},{"repeatDownloadIntervalMs",repeatDownloadIntervalMs}};
}
bool ChannelSettings::fromJson(const QJsonObject &o,ChannelSettings &result,QString &error) {
    if(o.value("bus")!="CAN" && o.value("bus")!="LIN"){error="配置中的总线类型无效。";return false;}
    ChannelSettings s=defaults(o.value("bus")=="CAN"?communication::Bus::Can:communication::Bus::Lin);
    const auto text=[&o](const char *key,const QString &fallback){return o.value(key).toString(fallback);};
    s.softwareId=text("softwareId",s.softwareId);s.profileId=text("profileId",s.profileId);
    if(o.contains("downloadProfile")&&!o.value("downloadProfile").isObject()){error="downloadProfile must be an object";return false;}
    if(o.contains("canNetwork")&&!o.value("canNetwork").isObject()){error="canNetwork must be an object";return false;}
    if(o.contains("canNetwork"))s.canNetwork=o.value("canNetwork").toObject();
    if(o.contains("downloadProfile")){
        s.downloadProfile=o.value("downloadProfile").toObject();
        if(!s.downloadProfile.contains("flow"))s.downloadProfile["flow"]="app";
        const auto defaults=ChannelPageInitialValues::initialDownloadProfile();
        if(!s.downloadProfile.contains("stepEnabled"))
            s.downloadProfile["stepEnabled"]=ChannelPageInitialValues::initialStepEnabled(s.downloadProfile["flow"].toString());
        if(!s.downloadProfile.contains("negativeResponseChecks"))
            s.downloadProfile["negativeResponseChecks"]=s.downloadProfile.value("feedbackChecks").isObject()
                ?s.downloadProfile.value("feedbackChecks"):defaults["negativeResponseChecks"];
        if(!s.downloadProfile.contains("timeoutChecks"))s.downloadProfile["timeoutChecks"]=defaults["timeoutChecks"];
        s.downloadProfile.remove("feedbackChecks");
    }
    s.hardwareKey=text("hardwareKey",s.hardwareKey);s.handle=quint32(o.value("handle").toInt(int(s.handle)));
    s.bitrate=o.value("bitrate").toInt(s.bitrate);s.simulation=o.value("simulation").toBool(s.simulation);
    s.autoReconnect=o.value("autoReconnect").toBool(s.autoReconnect);s.extendedId=o.value("extendedId").toBool(s.extendedId);
    s.requestId=text("requestId",s.requestId);s.responseId=text("responseId",s.responseId);
    s.functionalId=text("functionalId",s.functionalId);s.nad=text("nad",s.nad);
    s.p2Ms=o.value("p2Ms").toInt(s.p2Ms);s.p2StarMs=o.value("p2StarMs").toInt(s.p2StarMs);
    s.testerPresentMs=o.value("testerPresentMs").toInt(s.testerPresentMs);
    s.testerPresentEnabled=o.value("testerPresentEnabled").toBool(s.testerPresentEnabled);
    s.maxPendingMs=o.value("maxPendingMs").toInt(s.maxPendingMs);
    s.programmingSession=o.value("programmingSession").toInt(s.programmingSession);
    s.securityLevel=o.value("securityLevel").toInt(s.securityLevel);
    s.flashPath=text("flashPath",s.flashPath);s.applicationPath=text("applicationPath",s.applicationPath);
    s.flashAddress=text("flashAddress",s.flashAddress);s.applicationAddress=text("applicationAddress",s.applicationAddress);
    s.flashRequired=o.value("flashRequired").toBool(s.flashRequired);
    s.rxdEnabled=o.value("rxdEnabled").toBool(s.rxdEnabled);
    s.repeatDownloadEnabled=o.value("repeatDownloadEnabled").toBool(s.repeatDownloadEnabled);
    s.repeatDownloadCount=o.value("repeatDownloadCount").toInt(s.repeatDownloadCount);
    s.repeatDownloadIntervalMs=o.value("repeatDownloadIntervalMs").toInt(s.repeatDownloadIntervalMs);
    communication::SoftwareChannelConfiguration c;
    if(!s.toConfiguration(c,error))return false;
    result=s;return true;
}
QString hardwareDeviceKey(const communication::HardwareChannel &h) {
    // SDK handles identify ports. Group only when the SDK provides an owner identity.
    if(h.key.startsWith("preview:") || h.bus==communication::Bus::Lin || h.persistentIdentity)
        return h.key.section(':',0,-2);
    return h.key;
}
QString connectionText(communication::ConnectionState state) {
    using S=communication::ConnectionState;
    switch(state){case S::Missing:return "未发现硬件";case S::Available:return "未连接";
    case S::Connecting:return "正在连接";case S::Connected:return "已连接";
    case S::Disconnecting:return "正在断开";case S::Fault:return "连接异常";}
    return {};
}
bool imageReady(const QString &path) {
    QFileInfo f(path);const QString ext=f.suffix().toLower();
    return !path.trimmed().isEmpty() && f.exists() && f.isFile() && f.isReadable() && f.size()>0 &&
        (ext=="bin" || ext=="hex");
}
QString imageDescription(const QString &path) {
    if(path.isEmpty())return "尚未选择镜像";
    if(!imageReady(path))return "请选择可读取且非空的 BIN / HEX 文件";
    const QFileInfo f(path);
    return QString("%1  ·  %2 KiB").arg(f.suffix().toUpper()).arg(f.size()/1024.0,0,'f',1);
}
}

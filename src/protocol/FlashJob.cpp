#include "FlashJob.h"
#include <QDateTime>
#include <QJsonValue>
namespace boot {
QByteArray SimulationKey::calculate(quint8 level,const QByteArray &seed,QString &error){
    if(seed.size()!=4){error="Simulation seed must be 4 bytes";return {};}
    QByteArray key=seed;for(int i=0;i<key.size();++i)key[i]=char(quint8(key[i])^0xa5^level);return key;
}
QJsonObject FlashProfile::toJson() const{
    return {{"name",name},{"simulationOnly",simulationOnly},{"keyProvider",keyProvider},{"session",session},{"securityLevel",securityLevel},
        {"eraseRoutine",eraseRoutine},{"verifyRoutine",verifyRoutine},{"dependencyRoutine",dependencyRoutine},{"identityDid",identityDid},
        {"consecutiveFrameByteLimit",consecutiveFrameByteLimit},{"resetWaitMs",resetWaitMs},{"flow",flow},
        {"stepEnabled",stepEnabled},{"negativeResponseChecks",negativeResponseChecks},{"timeoutChecks",timeoutChecks},
        {"keyLibrary",keyLibrary}};
}
bool FlashProfile::fromJson(const QJsonObject &o,FlashProfile &out,QString &error){
    FlashProfile p;auto bad=[&]{error="Invalid flash profile";return false;};
    const auto defaults=p.toJson();
    for(auto it=o.begin();it!=o.end();++it)if(!defaults.contains(it.key())&&it.key()!="feedbackChecks"&&it.key()!="blockDataLimit"&&it.key()!="maxPduBytes"&&it.key()!="verifyEnabled"&&it.key()!="dependencyEnabled"&&it.key()!="identityEnabled")return bad();
    if(o.contains("name")){if(!o["name"].isString())return bad();p.name=o["name"].toString();}
    if(o.contains("keyProvider")){if(!o["keyProvider"].isString())return bad();p.keyProvider=o["keyProvider"].toString();}
    if(o.contains("simulationOnly")){if(!o["simulationOnly"].isBool())return bad();p.simulationOnly=o["simulationOnly"].toBool();}
    auto number=[&](const char *key,int fallback,int lo,int hi,int &v){v=fallback;if(!o.contains(key))return true;const auto x=o[key];if(!x.isDouble()||x.toDouble()!=x.toInt(-1))return false;v=x.toInt();return v>=lo&&v<=hi;};
    for(const auto key:{"flow","keyLibrary"})if(o.contains(key)&&!o[key].isString())return bad();
    p.flow=o.value("flow").toString(p.flow);p.keyLibrary=o.value("keyLibrary").toString();
    if(p.flow!="legacy"&&p.flow!="app"&&p.flow!="boot")return bad();
    for(const auto key:{"feedbackChecks","stepEnabled","negativeResponseChecks","timeoutChecks"})
        if(o.contains(key)&&!o[key].isObject())return bad();
    const auto validateChecks=[&](const QJsonObject &checks){
        for(auto it=checks.begin();it!=checks.end();++it){
            bool found=false;for(const auto &step:downloadSteps())if(it.key()==step.id)found=true;
            if(!found||!it.value().isBool())return false;
        }
        return true;
    };
    const auto legacyChecks=o["feedbackChecks"].toObject();
    p.stepEnabled=o["stepEnabled"].toObject();
    p.negativeResponseChecks=o.contains("negativeResponseChecks")?o["negativeResponseChecks"].toObject():legacyChecks;
    p.timeoutChecks=o["timeoutChecks"].toObject();
    if(!validateChecks(p.stepEnabled)||!validateChecks(p.negativeResponseChecks)||!validateChecks(p.timeoutChecks))return bad();
    // Obsolete UI switches are accepted only so an old profile can be opened and
    // re-saved. They no longer control the download sequence.
    for(const auto key:{"verifyEnabled","dependencyEnabled","identityEnabled"})
        if(o.contains(key)&&!o[key].isBool())return bad();
    int v;
    if(!number("session",p.session,1,127,v))return bad();p.session=quint8(v);
    if(!number("securityLevel",p.securityLevel,1,125,v)||!(v&1))return bad();p.securityLevel=quint8(v);
    if(!number("eraseRoutine",p.eraseRoutine,0,65535,v))return bad();p.eraseRoutine=quint16(v);
    if(!number("verifyRoutine",p.verifyRoutine,0,65535,v))return bad();p.verifyRoutine=quint16(v);
    if(!number("dependencyRoutine",p.dependencyRoutine,0,65535,v))return bad();p.dependencyRoutine=quint16(v);
    if(!number("identityDid",p.identityDid,0,65535,v))return bad();p.identityDid=quint16(v);
    // Migrate both historical names. blockDataLimit excluded SID/BSC; maxPduBytes
    // already included them. The new name describes the complete segmented PDU.
    if(o.contains("blockDataLimit")){if(!number("blockDataLimit",4093,1,4093,v))return bad();p.consecutiveFrameByteLimit=v+2;}
    if(o.contains("maxPduBytes")){if(!number("maxPduBytes",p.consecutiveFrameByteLimit,3,4095,v))return bad();p.consecutiveFrameByteLimit=v;}
    if(!number("consecutiveFrameByteLimit",p.consecutiveFrameByteLimit,3,4095,v))return bad();p.consecutiveFrameByteLimit=v;
    if(!number("resetWaitMs",p.resetWaitMs,0,60000,v))return bad();p.resetWaitMs=v;
    if(p.name.isEmpty()||p.name.size()>80||(p.keyProvider!="simulation-xor-v1"&&p.keyProvider!="external-generatekeyex")||(p.keyProvider=="simulation-xor-v1"&&!p.simulationOnly)||
       p.eraseRoutine==p.verifyRoutine||p.eraseRoutine==p.dependencyRoutine||p.verifyRoutine==p.dependencyRoutine)return bad();
    out=p;error.clear();return true;
}
static QByteArray routine(quint16 id){return QByteArray::fromHex("3101")+char(id>>8)+char(id);}
static QByteArray positive(QByteArray request,int length){
    const auto sid=quint8(request[0]);request[0]=char(sid+0x40);
    if(request.size()>1&&(sid==0x10||sid==0x11||sid==0x28||sid==0x3e||sid==0x85))
        request[1]=char(quint8(request[1])&0x7f);
    return request.left(length);
}
FlashJob::FlashJob(UdsSession &session,FlashProfile profile,std::unique_ptr<KeyProvider> key,QObject *parent)
 :QObject(parent),m_session(session),m_profile(std::move(profile)),m_key(std::move(key)){}
bool FlashJob::start(const FirmwareImage &app,const FirmwareImage &driver,QString &error){
    FlashProfile valid;
    if(m_running||m_session.busy()||!FlashProfile::fromJson(m_profile.toJson(),valid,error)||!m_key||app.segments.isEmpty()){if(error.isEmpty())error="Job busy or missing application/key provider";return false;}
    m_segments=driver.segments;m_driverCount=m_segments.size();m_segments+=app.segments;m_total=0;
    for(int i=0;i<m_segments.size();++i){
        const auto &s=m_segments[i];const quint64 end=quint64(s.address)+quint64(s.data.size());
        if(s.data.isEmpty()||end>0x100000000ULL){error="Invalid image segment";return false;}
        // RAM driver and application may share numeric addresses on different ECU memory mappings.
        for(int j=(i<m_driverCount?0:m_driverCount);j<i;++j){const auto &p=m_segments[j];
            if(quint64(p.address)<end&&quint64(s.address)<quint64(p.address)+quint64(p.data.size())){error="Overlapping image segments";return false;}}
        m_total+=s.data.size();
    }
    ++m_epoch;m_index=0;m_acked=0;m_ignored=0;m_running=true;
    if(m_profile.flow=="legacy")enterProgramming();else preSteps(0);return true;
}
void FlashJob::ask(const QByteArray &request,const QByteArray &expected,std::function<void(const QByteArray &)> next){
    if(!m_running)return;const auto epoch=m_epoch;
    if(!m_session.request(request,expected,[this,epoch,next](bool ok,const QByteArray &pdu,const QString &error){
        if(!m_running||epoch!=m_epoch)return;if(!ok){fail(error);return;}next(pdu);
    }))fail("UDS request rejected by session");
}
void FlashJob::step(const QString &id,const QByteArray &request,const QByteArray &expected,std::function<void(const QByteArray &)> next,bool requiredData){
    if(!m_running)return;report(id);const auto epoch=m_epoch;
    const auto text=stepText(id);
    if(!m_profile.enabled(id)){emit stepEvent(text+"未使能跳过");next({});return;}
    const bool checkNegative=m_profile.checksNegativeResponse(id);
    const bool checkTimeout=m_profile.checksTimeout(id);
    const bool suppressed=suppressesPositiveResponse(id);
    const qint64 started=QDateTime::currentMSecsSinceEpoch();emit stepEvent(text+"发送");
    if(!m_session.request(request,expected,[this,epoch,id,text,started,suppressed,checkNegative,checkTimeout,requiredData,next](bool ok,const QByteArray &pdu,const QString &error){
        if(!m_running||epoch!=m_epoch)return;
        const bool negative=pdu.size()==3&&quint8(pdu[0])==0x7f&&quint8(pdu[2])!=0x78;
        const bool timeout=error.contains("timeout",Qt::CaseInsensitive)||error.contains("deadline",Qt::CaseInsensitive);
        if(negative){
            const auto nrc=QString::number(quint8(pdu[2]),16).rightJustified(2,'0').toUpper();
            emit stepEvent(text+"负响应NRC(0x"+nrc+")");
        }
        else if(timeout)emit stepEvent(text+QString("响应超时(%1ms)").arg(qMax<qint64>(0,QDateTime::currentMSecsSinceEpoch()-started)));
        else if(ok)emit stepEvent(text+(suppressed&&pdu.isEmpty()?"正响应抑制完成":"正响应"));
        else emit stepEvent(text+"响应异常");
        if(!ok){
            if(!checkNegative&&negative&&!requiredData){++m_ignored;emit notice(id+" · 已忽略负响应");next(pdu);return;}
            if(!checkTimeout&&timeout&&!requiredData){++m_ignored;emit notice(id+" · 已忽略响应超时");next(pdu);return;}
            fail(requiredData&&negative?id+" requires ECU response data: "+error:error);return;
        }
        next(pdu);
    },suppressed))fail("UDS request rejected by session");
}
void FlashJob::enterProgramming(){
    const QByteArray req=QByteArray(1,char(0x10))+char(m_profile.flow=="legacy"?m_profile.session:2);
    step("boot1002",req,positive(req,2),[this](const QByteArray &){seed();});
}
void FlashJob::preSteps(int index){
    if(index==2){unlock(1,"pre2701","pre2702",[this]{preSteps(3);});return;}
    static const char *ids[]={"pre1001","pre1003","","pre220101","pre31010203","pre1003control","pre8502","pre2803"};
    static const char *requests[]={"1001","1003","","220101","31010203","1083","8582","288301"};
    if(index==8){enterProgramming();return;}
    const auto req=QByteArray::fromHex(requests[index]);
    const int echo=quint8(req[0])==0x31?4:quint8(req[0])==0x22?3:2;
    step(ids[index],req,positive(req,echo),[this,index](const QByteArray &){preSteps(index+1);});
}
void FlashJob::unlock(quint8 level,const QString &seedId,const QString &keyId,std::function<void()> next){
    const QByteArray req=QByteArray(1,char(0x27))+char(level);
    step(seedId,req,positive(req,2),[this,level,seedId,keyId,next](const QByteArray &pdu){
        const auto seed=pdu.mid(2);
        if(seed.isEmpty()&&!m_profile.enabled(seedId)){
            if(!m_profile.enabled(keyId)){next();return;}
            if(!m_profile.keyLibrary.trimmed().isEmpty()){fail("Security seed step disabled while 27 DLL key step is enabled");return;}
            const QByteArray keyReq=QByteArray(1,char(0x27))+char(level+1)+char(0);
            step(keyId,keyReq,positive(keyReq,2),[next](const QByteArray &){next();});return;
        }
        if(seed.isEmpty()){fail("Empty security seed");return;}
        bool unlocked=true;for(unsigned char b:seed)if(b)unlocked=false;
        if(unlocked){next();return;}
        QString error;const auto key=m_key->calculate(level,seed,error);
        if(key.isEmpty()||!error.isEmpty()){fail("Key provider: "+error);return;}
        const QByteArray req=QByteArray(1,char(0x27))+char(level+1)+key;
        step(keyId,req,positive(req,2),[next](const QByteArray &){next();});
    },true);
}
void FlashJob::seed(){
    unlock(m_profile.securityLevel,"bootSeed","bootKey",[this]{
        if(m_profile.flow=="legacy"){nextSegment();return;}
        const auto req=QByteArray::fromHex("2ef1840101");
        step("fingerprint",req,QByteArray::fromHex("6ef184"),[this](const QByteArray &){nextSegment();});
    });
}
void FlashJob::nextSegment(){
    if(m_index>=m_segments.size()){finalSteps();return;}
    const auto &s=m_segments[m_index];m_offset=0;m_sequence=1;m_transferPacket=0;
    if(m_index<m_driverCount){report("Flash driver");download();return;}
    report("Erase application segment");
    const auto req=routine(m_profile.eraseRoutine)+char(0x44)+be32(s.address)+be32(quint32(s.data.size()));
    step("erase",req,positive(req,4)+char(0),[this](const QByteArray &){download();});
}
void FlashJob::download(){
    const auto &s=m_segments[m_index];report("RequestDownload");
    const auto downloadId=m_index<m_driverCount?"driver34":"app34";
    const auto req=QByteArray::fromHex("340044")+be32(s.address)+be32(quint32(s.data.size()));
    step(downloadId,req,QByteArray(1,char(0x74)),[this,downloadId](const QByteArray &pdu){
        if(pdu.isEmpty()&&!m_profile.enabled(downloadId)){
            m_blockSize=qMax(1,qMin(m_profile.consecutiveFrameByteLimit,m_session.maximumPdu())-2);block();return;
        }
        if(pdu.size()<3){fail("Short RequestDownload response");return;}
        const int format=quint8(pdu[1]),n=format>>4;
        if((format&15)||n<1||n>4||pdu.size()!=2+n){fail("Invalid maxNumberOfBlockLength format");return;}
        const auto max=readBe(pdu.mid(2));if(max<=2){fail("ECU block length leaves no payload");return;}
        m_blockSize=int(qMin<quint32>(max,quint32(qMin(m_profile.consecutiveFrameByteLimit,m_session.maximumPdu()))))-2;block();
    },true);
}
void FlashJob::block(){
    const auto &s=m_segments[m_index];
    const auto blockId=QString(m_index<m_driverCount?"driver36":"app36");
    if(!m_profile.enabled(blockId))m_offset=s.data.size();
    if(m_offset>=s.data.size()){
        report("RequestTransferExit");step(m_index<m_driverCount?"driver37":"app37",QByteArray(1,char(0x37)),QByteArray(1,char(0x77)),[this](const QByteArray &){verify();});return;
    }
    m_blockLength=qMin(m_blockSize,int(s.data.size())-m_offset);
    const auto req=QByteArray(1,char(0x36))+char(m_sequence)+s.data.mid(m_offset,m_blockLength);
    ++m_transferPacket;
    step(m_index<m_driverCount?"driver36":"app36",req,QByteArray(1,char(0x76))+char(m_sequence),[this](const QByteArray &){
        m_offset+=m_blockLength;m_acked+=m_blockLength;++m_sequence;report("TransferData step completed");block();
    });
}
void FlashJob::verify(){
    const auto &s=m_segments[m_index];report("Verify segment CRC32");
    const auto req=routine(m_profile.verifyRoutine)+be32(s.address)+be32(quint32(s.data.size()))+be32(crc32(s.data));
    step(m_index<m_driverCount?"driverVerify":"appVerify",req,positive(req,4)+char(0),[this](const QByteArray &){++m_index;nextSegment();});
}
void FlashJob::finalSteps(){
    auto reset=[this]{
        step("reset",QByteArray::fromHex("1101"),QByteArray::fromHex("5101"),[this](const QByteArray &){
            const auto epoch=m_epoch;
            QTimer::singleShot(m_profile.resetWaitMs,this,[this,epoch]{
                if(!m_running||epoch!=m_epoch)return;
                if(m_profile.flow=="legacy")identify();else postSteps(0);
            });
        });
    };
    const auto req=routine(m_profile.dependencyRoutine);
    step("dependency",req,positive(req,4)+char(0),[reset](const QByteArray &){reset();});
}
void FlashJob::postSteps(int index){
    static const char *ids[]={"post1003","post14","post2800","post8501","post1001"};
    static const char *requests[]={"1003","14ffffff","288001","8581","1081"};
    if(index==5){identify();return;}
    const auto req=QByteArray::fromHex(requests[index]);
    step(ids[index],req,positive(req,index==1?1:2),[this,index](const QByteArray &){postSteps(index+1);});
}
void FlashJob::identify(){
    const auto req=QByteArray(1,char(0x22))+char(m_profile.identityDid>>8)+char(m_profile.identityDid);
    const auto expected=positive(req,3)+(m_profile.simulationOnly?QByteArray("SIM-ECU"):QByteArray());
    step("identity",req,expected,[this](const QByteArray &){done();});
}
void FlashJob::done(){
    m_running=false;
    const auto text=m_ignored?QString("流程结束 · 忽略 %1 个负响应或超时，结果未全部确认").arg(m_ignored):QString("下载流程完成");
    emit progress(100,text);emit finished(true,text);
}
void FlashJob::report(const QString &step){emit progress(int(qMin<qint64>(99,m_acked*100/qMax<qint64>(1,m_total))),step);}
QString FlashJob::stepText(const QString &id) const{
    for(int i=0;i<downloadSteps().size();++i){
        if(id!=downloadSteps()[i].id)continue;
        const auto prefix=QString("step%1 ").arg(i+1,2,10,QChar('0'));
        if(id=="driver36"||id=="app36")return prefix+QString("36传输数据第%1包").arg(m_transferPacket);
        QString value=QString::fromUtf8(downloadSteps()[i].label);value.replace(" · ","");
        return prefix+value;
    }
    return id+" ";
}
void FlashJob::fail(const QString &error){if(!m_running)return;m_running=false;++m_epoch;m_session.cancel();emit finished(false,error);}
void FlashJob::cancel(){++m_epoch;m_running=false;m_session.cancel();}
}

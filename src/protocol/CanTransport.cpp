#include "CanTransport.h"
namespace boot {
int CanTransport::separationUs(int value){
    if(value>=0&&value<=0x7f)return value*1000;
    if(value>=0xf1&&value<=0xf9)return (value-0xf0)*100;
    return -1;
}
bool CanOptions::valid(QString &error) const {
    const quint32 max=extended?0x1fffffff:0x7ff;
    if(txId>max||rxId>max||txId==rxId||blockSize<0||blockSize>255||CanTransport::separationUs(stMin)<0||
       maxWaitFrames<0||maxWaitFrames>255||nAsMs<1||nAsMs>60000||nBsMs<1||nBsMs>60000||
       nCrMs<1||nCrMs>60000||receiveCapacity<8||receiveCapacity>4095||padding<0||padding>255){
        error="Invalid Classic CAN ISO-TP configuration";return false;
    }
    error.clear();return true;
}
QJsonObject CanOptions::toJson() const{
    return {{"blockSize",blockSize},{"stMin",stMin},{"maxWaitFrames",maxWaitFrames},{"nAsMs",nAsMs},
      {"nBsMs",nBsMs},{"nCrMs",nCrMs},{"receiveCapacity",receiveCapacity},{"padding",padding}};
}
bool CanOptions::fromJson(const QJsonObject &json,CanOptions &out,QString &error){
    CanOptions options=out;
    const auto defaults=options.toJson();
    for(auto it=json.begin();it!=json.end();++it){
        if(!defaults.contains(it.key())||!it.value().isDouble()||it.value().toDouble()!=it.value().toInt(-1)){
            error="Unknown or non-integer CAN network setting";return false;
        }
    }
    options.blockSize=json.value("blockSize").toInt(options.blockSize);
    options.stMin=json.value("stMin").toInt(options.stMin);
    options.maxWaitFrames=json.value("maxWaitFrames").toInt(options.maxWaitFrames);
    options.nAsMs=json.value("nAsMs").toInt(options.nAsMs);
    options.nBsMs=json.value("nBsMs").toInt(options.nBsMs);
    options.nCrMs=json.value("nCrMs").toInt(options.nCrMs);
    options.receiveCapacity=json.value("receiveCapacity").toInt(options.receiveCapacity);
    options.padding=json.value("padding").toInt(options.padding);
    if(!options.valid(error))return false;out=options;return true;
}
CanTransport::CanTransport(CanOptions options,Write write,QObject *parent)
 :DiagnosticTransport(parent),m_options(options),m_write(std::move(write)){
    for(auto timer:{&m_as,&m_bs,&m_cr,&m_cf}){timer->setSingleShot(true);timer->setTimerType(Qt::PreciseTimer);}
    connect(&m_as,&QTimer::timeout,this,[this]{fail("CAN N_As/N_Ar transmit confirmation timeout");});
    connect(&m_bs,&QTimer::timeout,this,[this]{fail("CAN N_Bs flow control timeout");});
    connect(&m_cr,&QTimer::timeout,this,[this]{fail("CAN N_Cr consecutive frame timeout");});
    connect(&m_cf,&QTimer::timeout,this,&CanTransport::sendCf);
}
bool CanTransport::send(const QByteArray &pdu,QString &error){
    if(!m_options.valid(error))return false;
    if(busy()){error="CAN ISO-TP busy";return false;}
    if(pdu.isEmpty()||pdu.size()>maximumPdu()){error="Classic CAN PDU length must be 1..4095";return false;}
    m_listening=true;m_tx=pdu;m_txState=FramePending;m_txOffset=0;m_txSequence=1;m_waitFrames=0;
    m_peerStUs=0;m_lastCf.invalidate();m_earlyFc.clear();
    QByteArray frame;
    if(pdu.size()<=7){frame=QByteArray(1,char(pdu.size()))+pdu;submit(frame,Single);}
    else {frame=QByteArray(1,char(0x10|(pdu.size()>>8)))+char(pdu.size())+pdu.left(6);m_txOffset=6;submit(frame,First);}
    return true;
}
void CanTransport::cancel(){
    ++m_epoch;m_listening=false;m_as.stop();m_bs.stop();m_cr.stop();m_cf.stop();
    m_tx.clear();m_rx.clear();m_earlyFc.clear();m_rxLength=0;m_pending=None;m_txState=Idle;
    m_token=0;m_lastCf.invalidate();emit cancelled();
}
void CanTransport::fail(const QString &error){cancel();emit failed(error);}
void CanTransport::submit(QByteArray bytes,Kind kind){
    if(m_pending!=None){fail("CAN adapter already has an outstanding frame");return;}
    while(bytes.size()<8)bytes.append(char(m_options.padding));
    const CanFrame frame{m_options.txId,m_options.extended,false,false,false,bytes};
    m_pending=kind;m_token=++m_serial;const auto token=m_token,epoch=m_epoch;
    m_as.start(m_options.nAsMs);
    // Always return from send() before publishing frames or completing a PDU.
    QTimer::singleShot(0,this,[this,frame,token,epoch]{
        if(epoch!=m_epoch||token!=m_token)return;
        emit trace(true,frame);
        if(epoch!=m_epoch||token!=m_token)return;
        QString error;if(!m_write(frame,token,error))fail(error.isEmpty()?"CAN write failed":error);
    });
}
void CanTransport::confirmTransmitted(quint64 token,bool success){
    if(!token||token!=m_token||m_pending==None)return;
    if(!success){fail("CAN transmit confirmation failed");return;}
    const auto kind=m_pending;m_as.stop();m_pending=None;m_token=0;
    if(kind==Overflow){fail("CAN receive capacity exceeded");return;}
    if(kind==FlowControl){if(m_rxLength)m_cr.start(m_options.nCrMs);return;}
    if(kind==Single){txComplete();return;}
    if(kind==First){
        m_txState=WaitingFc;m_bs.start(m_options.nBsMs);m_lastCf.start();
    }else{
        m_txOffset+=m_pendingCount;m_txSequence=(m_txSequence+1)&15;m_lastCf.start();
        if(m_txOffset>=m_tx.size()){txComplete();return;}
        if(m_peerBlock&&--m_remainingBlock==0){m_txState=WaitingFc;m_bs.start(m_options.nBsMs);}
        else {m_txState=SendingCf;scheduleCf();}
    }
    if(m_txState==WaitingFc&&!m_earlyFc.isEmpty()){const auto fc=m_earlyFc;m_earlyFc.clear();flowControl(fc);}
}
void CanTransport::txComplete(){m_tx.clear();m_txState=Idle;emit sent();}
void CanTransport::flowControl(const QByteArray &bytes){
    if(bytes.size()<3){fail("Short CAN flow control frame");return;}
    const int status=quint8(bytes[0])&15;
    if(status==2){fail("CAN peer overflow");return;}
    if(status==1){
        if(++m_waitFrames>m_options.maxWaitFrames){fail("CAN WAIT frame limit exceeded");return;}
        m_bs.start(m_options.nBsMs);return;
    }
    if(status!=0){fail("Invalid CAN flow status");return;}
    const int separation=separationUs(quint8(bytes[2]));
    if(separation<0){fail("Reserved CAN STmin value");return;}
    m_bs.stop();m_waitFrames=0;m_peerBlock=quint8(bytes[1]);m_remainingBlock=m_peerBlock;m_peerStUs=separation;
    m_txState=SendingCf;scheduleCf();
}
void CanTransport::scheduleCf(){
    if(m_txState!=SendingCf)return;
    const qint64 elapsed=m_lastCf.isValid()?m_lastCf.nsecsElapsed()/1000:0;
    const auto remaining=qMax<qint64>(0,m_peerStUs-elapsed);
    m_cf.start(int((remaining+999)/1000));
}
void CanTransport::sendCf(){
    if(m_txState!=SendingCf)return;
    if(m_lastCf.isValid()&&m_lastCf.nsecsElapsed()/1000<m_peerStUs){scheduleCf();return;}
    m_pendingCount=qMin(7,int(m_tx.size())-m_txOffset);
    const auto frame=QByteArray(1,char(0x20|m_txSequence))+m_tx.mid(m_txOffset,m_pendingCount);
    m_txState=FramePending;submit(frame,Consecutive);
}
void CanTransport::sendFc(bool overflow){
    m_cr.stop();
    submit(QByteArray(1,char(overflow?0x32:0x30))+char(m_options.blockSize)+char(m_options.stMin),overflow?Overflow:FlowControl);
}
void CanTransport::receiveFrame(const CanFrame &frame){
    if(!m_listening||frame.id!=m_options.rxId||frame.extended!=m_options.extended||frame.rtr||frame.fd)return;
    if(frame.error){fail("CAN receive error");return;}
    const auto &bytes=frame.data;
    if(bytes.isEmpty()||bytes.size()>8){fail("Invalid Classic CAN DLC");return;}
    emit trace(false,frame);
    const int pci=quint8(bytes[0]),kind=pci>>4;
    if(kind==3){
        if(m_txState==WaitingFc){flowControl(bytes);return;}
        // A real adapter can report peer FC before its own TX echo.
        if(m_pending==First||(m_pending==Consecutive&&m_peerBlock&&m_remainingBlock==1)){
            if(!m_earlyFc.isEmpty()){fail("Duplicate early CAN flow control");return;}
            m_earlyFc=bytes;return;
        }
        if(m_txState!=Idle)fail("Unexpected CAN flow control");
        return;
    }
    if(m_txState!=Idle){fail("CAN response arrived before request transmission completed");return;}
    if(kind==0){
        const int size=pci&15;
        if(m_rxLength||size<1||size>7||bytes.size()<size+1){fail("Invalid CAN single frame");return;}
        emit received(bytes.mid(1,size));return;
    }
    if(kind==1){
        if(m_rxLength||bytes.size()!=8){fail("Invalid CAN first frame");return;}
        const int length=((pci&15)<<8)|quint8(bytes[1]);
        if(length<8){fail("Unsupported CAN FF length (8..4095 required)");return;}
        if(length>m_options.receiveCapacity){sendFc(true);return;}
        m_rxLength=length;m_rx=bytes.mid(2,6);m_rxSequence=1;m_rxBlock=0;
        emit responseStarted(m_rx);
        if(!m_listening)return;
        sendFc(false);return;
    }
    if(kind==2){
        if(!m_rxLength||(pci&15)!=m_rxSequence){fail("CAN consecutive frame sequence mismatch");return;}
        const int count=qMin(7,m_rxLength-int(m_rx.size()));
        if(bytes.size()<count+1){fail("Short CAN consecutive frame");return;}
        m_rx+=bytes.mid(1,count);m_rxSequence=(m_rxSequence+1)&15;
        if(m_rx.size()==m_rxLength){
            const auto pdu=m_rx;m_rx.clear();m_rxLength=0;m_cr.stop();emit received(pdu);return;
        }
        if(m_options.blockSize&&++m_rxBlock==m_options.blockSize){m_rxBlock=0;sendFc(false);}
        else m_cr.start(m_options.nCrMs);
        return;
    }
    fail("Unsupported CAN PCI");
}
}

#include "LinTransport.h"
namespace boot {
QList<QByteArray> LinCodec::encode(quint8 nad,const QByteArray &pdu){
    QList<QByteArray> frames;if(nad<1||nad>0x7d||pdu.isEmpty()||pdu.size()>4095)return frames;
    int offset=0,sn=1;QByteArray f(8,char(0xff));f[0]=char(nad);
    if(pdu.size()<=6){f[1]=char(pdu.size());for(int i=0;i<pdu.size();++i)f[2+i]=pdu[i];frames.append(f);return frames;}
    f[1]=char(0x10|(pdu.size()>>8));f[2]=char(pdu.size());for(int i=0;i<5;++i)f[3+i]=pdu[i];frames.append(f);offset=5;
    while(offset<pdu.size()){f.fill(char(0xff));f[0]=char(nad);f[1]=char(0x20|sn);sn=(sn+1)&15;
        for(int i=0;i<6&&offset<pdu.size();++i)f[2+i]=pdu[offset++];frames.append(f);}
    return frames;
}
LinCodec::Result LinCodec::accept(quint8 nad,const QByteArray &f,QByteArray &pdu,QString &error){
    pdu.clear();error.clear();if(f.size()!=8){reset();error="LIN diagnostic frame must have 8 bytes";return Invalid;}
    if(quint8(f[0])!=nad)return Ignored;
    auto bad=[&](const char *why){reset();error=why;return Invalid;};
    const int pci=quint8(f[1]),kind=pci>>4;
    if(kind==0){
        if(m_length)return bad("Unexpected single frame during segmented response");
        if(pci<1||pci>6)return bad("Invalid LIN SF length");pdu=f.mid(2,pci);return Complete;
    }
    if(kind==1){
        if(m_length)return bad("Unexpected first frame during segmented response");
        const int len=((pci&15)<<8)|quint8(f[2]);
        if(len<7)return bad("Invalid LIN FF length");
        m_length=len;m_bytes=f.mid(3,5);m_sequence=1;return Partial;
    }
    if(kind==2){
        if(!m_length||(pci&15)!=m_sequence)return bad("LIN consecutive frame sequence mismatch");
        m_sequence=(m_sequence+1)&15;m_bytes+=f.mid(2,qMin(6,m_length-int(m_bytes.size())));
        if(m_bytes.size()==m_length){pdu=m_bytes;reset();return Complete;}return Partial;
    }
    return bad("Unsupported LIN PCI");
}
LinTransport::LinTransport(quint8 nad,int slotMs,int crMs,Write write,QObject *parent)
 :DiagnosticTransport(parent),m_nad(nad),m_write(std::move(write)){
    m_slot.setInterval(qMax(1,slotMs));m_slot.setTimerType(Qt::PreciseTimer);
    m_as.setSingleShot(true);m_as.setInterval(1000);
    connect(&m_as,&QTimer::timeout,this,[this]{fail("LIN N_As transmit confirmation timeout");});
    m_cr.setSingleShot(true);m_cr.setInterval(qMax(1,crMs));
    connect(&m_slot,&QTimer::timeout,this,&LinTransport::tick);
    connect(&m_cr,&QTimer::timeout,this,[this]{fail("LIN N_Cr timeout");});
}
bool LinTransport::send(const QByteArray &pdu,QString &error){
    if(m_active){error="LIN transport busy";return false;}
    const auto frames=LinCodec::encode(m_nad,pdu);
    if(frames.isEmpty()){error="Invalid NAD or PDU length (1..4095)";return false;}
    m_decoder.reset();for(const auto &f:frames)m_tx.enqueue(f);
    m_active=true;m_confirm=false;m_slot.start();return true;
}
void LinTransport::cancel(){m_active=m_confirm=m_awaitTx=false;m_as.stop();m_lastTx.clear();m_slot.stop();m_cr.stop();m_tx.clear();m_decoder.reset();emit cancelled();}
void LinTransport::fail(const QString &error){cancel();emit failed(error);}
void LinTransport::tick(){
    if(!m_active||m_awaitTx)return;
    if(m_confirm){m_confirm=false;emit sent();if(!m_active)return;}
    const bool request=!m_tx.isEmpty();const quint8 id=request?0x3c:0x3d;
    const auto bytes=request?m_tx.dequeue():QByteArray();
    if(request){
        m_lastTx=bytes;
        if(m_requireConfirm){m_awaitTx=true;m_as.start();}
        else if(m_tx.isEmpty())m_confirm=true;
    }
    emit trace(true,id,bytes);
    QString error;if(!m_write(id,bytes,error))fail(error.isEmpty()?"LIN write failed":error);
}
void LinTransport::confirmTransmitted(const QByteArray &bytes,bool success){
    if(!m_active||!m_awaitTx||bytes!=m_lastTx)return;
    if(!success){fail("LIN transmit confirmation error");return;}
    m_as.stop();m_awaitTx=false;if(m_tx.isEmpty())m_confirm=true;
}
void LinTransport::receiveFrame(quint8 id,const QByteArray &bytes,bool noResponse,bool bad){
    if(!m_active||id!=0x3d||noResponse)return;
    if(bad){fail("LIN receive/checksum error");return;}
    emit trace(false,id,bytes);
    QByteArray pdu;QString error;const auto result=m_decoder.accept(m_nad,bytes,pdu,error);
    if(result==LinCodec::Invalid)fail(error);
    else if(result==LinCodec::Partial){m_cr.start();if((quint8(bytes[1])>>4)==1)emit responseStarted(bytes.mid(3,5));}
    else if(result==LinCodec::Complete){m_cr.stop();emit received(pdu);}
}
}

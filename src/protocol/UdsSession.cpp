#include "UdsSession.h"
namespace boot {
UdsSession::UdsSession(DiagnosticTransport &transport,SessionOptions options,QObject *parent)
 :QObject(parent),m_transport(transport),m_options(options){
    m_options.p2Ms=qMax(1,m_options.p2Ms);m_options.p2StarMs=qMax(1,m_options.p2StarMs);
    m_options.maxPendingMs=qMax(m_options.p2Ms,m_options.maxPendingMs);
    m_response.setSingleShot(true);m_limit.setSingleShot(true);
    connect(&transport,&DiagnosticTransport::sent,this,[this]{
        if(!m_active)return;m_sent=true;
        m_limit.start(m_options.maxPendingMs);m_response.start(m_options.p2Ms);
    });
    connect(&transport,&DiagnosticTransport::responseStarted,this,[this](const QByteArray &prefix){
        if(!m_active||!m_sent||prefix.isEmpty())return;
        const auto sid=quint8(m_current.bytes[0]);
        if(quint8(prefix[0])==quint8(sid+0x40)||(prefix.size()>=2&&quint8(prefix[0])==0x7f&&quint8(prefix[1])==sid))m_response.stop();
    });
    connect(&transport,&DiagnosticTransport::received,this,&UdsSession::response);
    connect(&transport,&DiagnosticTransport::failed,this,[this](const QString &e){if(m_active)finish(false,{},e);});
    connect(&m_response,&QTimer::timeout,this,[this]{
        if(m_current.suppressed&&!m_pending)finish(true,{},{});else finish(false,{},"UDS P2/P2* timeout");
    });
    connect(&m_limit,&QTimer::timeout,this,[this]{finish(false,{},m_sent?"UDS absolute response deadline":"UDS transmit timeout");});
    connect(&m_keepalive,&QTimer::timeout,this,[this]{
        if(m_established&&!busy())request(QByteArray::fromHex("3e80"),QByteArray::fromHex("7e00"),
          [this](bool ok,const QByteArray &,const QString &error){if(!ok){m_established=false;emit notice("TesterPresent: "+error);}},true);
    });
    if(options.testerPresentMs>0)m_keepalive.start(options.testerPresentMs);
}
bool UdsSession::request(QByteArray b,QByteArray expected,Reply reply,bool suppressed){
    if(b.isEmpty()||b.size()>maximumPdu()||expected.isEmpty()||m_queue.size()>=32)return false;
    m_queue.enqueue({std::move(b),std::move(expected),std::move(reply),suppressed});
    const auto epoch=m_epoch;QTimer::singleShot(0,this,[this,epoch]{if(epoch==m_epoch)pump();});return true;
}
void UdsSession::pump(){
    if(m_active||m_queue.isEmpty())return;
    m_current=m_queue.dequeue();m_active=true;m_sent=m_pending=false;m_limit.start(60000);
    emit trace(true,m_current.bytes);
    QString error;if(!m_transport.send(m_current.bytes,error))finish(false,{},error);
}
void UdsSession::response(const QByteArray &pdu){
    if(!m_active||!m_sent||pdu.isEmpty())return;
    emit trace(false,pdu);
    const quint8 sid=quint8(m_current.bytes[0]);
    if(quint8(pdu[0])==0x7f){
        if(pdu.size()<2||quint8(pdu[1])!=sid)return;
        if(pdu.size()!=3){finish(false,pdu,"Malformed negative response");return;}
        const auto nrc=quint8(pdu[2]);
        if(nrc==0x78){m_pending=true;m_response.start(m_options.p2StarMs);emit notice("UDS response pending (78)");return;}
        finish(false,pdu,QString("UDS service %1 NRC %2").arg(sid,2,16,QChar('0')).arg(nrc,2,16,QChar('0')));return;
    }
    if(quint8(pdu[0])!=quint8(sid+0x40))return;
    if(!pdu.startsWith(m_current.expected)){finish(false,pdu,"UDS positive response echo mismatch");return;}
    if(sid==0x10){
        if(pdu.size()!=2&&pdu.size()!=6){finish(false,pdu,"Invalid session timing response");return;}
        if(pdu.size()==2){m_established=true;finish(true,pdu,{});return;}
        const int p2=(quint8(pdu[2])<<8)|quint8(pdu[3]);
        const int star=((quint8(pdu[4])<<8)|quint8(pdu[5]))*10;
        if(p2<=0||star<=0||p2>m_options.maxPendingMs||star>m_options.maxPendingMs){finish(false,pdu,"Session timing out of configured bounds");return;}
        // Include client/transport margin; never reduce the configured client timeout.
        m_options.p2Ms=qMax(m_options.p2Ms,p2);m_options.p2StarMs=qMax(m_options.p2StarMs,star);m_established=true;
    }
    if(sid==0x11)m_established=false;
    finish(true,pdu,{});
}
void UdsSession::finish(bool ok,const QByteArray &pdu,const QString &error){
    if(!m_active)return;
    m_transport.cancel();m_response.stop();m_limit.stop();m_active=m_sent=false;
    auto reply=std::move(m_current.reply);m_current={};
    if(!ok){m_queue.clear();m_established=false;}
    if(m_options.testerPresentMs>0)m_keepalive.start(m_options.testerPresentMs);
    const auto epoch=m_epoch;if(reply)reply(ok,pdu,error);
    QTimer::singleShot(0,this,[this,epoch]{if(epoch==m_epoch)pump();});
}
void UdsSession::cancel(){
    ++m_epoch;m_response.stop();m_limit.stop();m_transport.cancel();m_queue.clear();m_current={};m_active=m_sent=m_established=false;
}
}

#include "SimulatedCanEcu.h"
namespace boot {
static CanOptions reversed(CanOptions options){
    qSwap(options.rxId,options.txId);return options;
}
SimulatedCanEcu::SimulatedCanEcu(CanOptions options,FlashProfile profile,QObject *parent)
 :QObject(parent),uds(std::move(profile)),m_endpoint(reversed(options),[this](const CanFrame &frame,quint64 token,QString &){
    const auto epoch=m_epoch;
    QTimer::singleShot(1,this,[this,frame,token,epoch]{
        if(epoch!=m_epoch||!m_host)return;
        m_endpoint.confirmTransmitted(token);
        if(epoch==m_epoch&&m_host)m_host->receiveFrame(frame);
    });return true;
 }){
    m_endpoint.listen();
    connect(&m_endpoint,&DiagnosticTransport::received,this,[this](const QByteArray &request){
        for(const auto &reply:uds.handle(request))m_replies.enqueue(reply);
        QTimer::singleShot(0,this,&SimulatedCanEcu::pump);
    });
    connect(&m_endpoint,&DiagnosticTransport::sent,this,[this]{QTimer::singleShot(0,this,&SimulatedCanEcu::pump);});
    connect(&m_endpoint,&DiagnosticTransport::failed,this,&SimulatedCanEcu::failed);
}
void SimulatedCanEcu::attach(CanTransport &host){
    m_host=&host;
    connect(&host,&DiagnosticTransport::cancelled,this,&SimulatedCanEcu::clearWire);
}
bool SimulatedCanEcu::writeFromHost(const CanFrame &frame,quint64 token,QString &error){
    if(!m_host){error="Simulated CAN host not attached";return false;}
    const auto epoch=m_epoch;
    QTimer::singleShot(1,this,[this,frame,token,epoch]{
        if(epoch!=m_epoch||!m_host)return;
        m_host->confirmTransmitted(token);
        if(epoch==m_epoch&&m_host)m_endpoint.receiveFrame(frame);
    });return true;
}
void SimulatedCanEcu::clearWire(){
    ++m_epoch;m_replies.clear();m_endpoint.cancel();m_endpoint.listen();
}
void SimulatedCanEcu::pump(){
    if(m_replies.isEmpty()||!m_host)return;
    if(m_endpoint.busy()){QTimer::singleShot(1,this,&SimulatedCanEcu::pump);return;}
    QString error;
    if(!m_endpoint.send(m_replies.dequeue(),error))emit failed(error);
}
}

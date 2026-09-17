#include "SimulatedLinEcu.h"
namespace boot {
SimulatedLinEcu::SimulatedLinEcu(quint8 nad,FlashProfile profile):SimulatedUdsEcu(std::move(profile)),m_nad(nad){}
void SimulatedLinEcu::clearWire(){m_decoder.reset();m_responses.clear();}
void SimulatedLinEcu::queue(const QByteArray &pdu){
    auto frames=LinCodec::encode(m_nad,pdu);
    if(faults.badResponseSequence&&frames.size()>1)frames[1][1]=char(0x22);
    for(const auto &f:frames)m_responses.enqueue(f);
}
bool SimulatedLinEcu::write(quint8 id,const QByteArray &bytes,QString &error){
    if(id==0x3d){++headers;return true;}
    if(id!=0x3c){error="Simulator accepts diagnostic IDs 3C/3D only";return false;}
    QByteArray pdu;const auto status=m_decoder.accept(m_nad,bytes,pdu,error);
    if(status==LinCodec::Invalid)return false;
    if(status!=LinCodec::Complete)return true;
    for(const auto &reply:handle(pdu))queue(reply);return true;
}
QByteArray SimulatedLinEcu::takeResponseFrame(){return m_responses.isEmpty()?QByteArray():m_responses.dequeue();}
}

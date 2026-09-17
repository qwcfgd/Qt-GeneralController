#pragma once
#include "SimulatedUdsEcu.h"
#include "LinTransport.h"
#include <QQueue>
namespace boot {
class SimulatedLinEcu : public SimulatedUdsEcu {
public:
    explicit SimulatedLinEcu(quint8 nad,FlashProfile profile={});
    bool write(quint8 id,const QByteArray &bytes,QString &error);
    QByteArray takeResponseFrame();
    int headers=0;
    void clearWire();
private:
    void queue(const QByteArray &);
    quint8 m_nad;LinCodec m_decoder;QQueue<QByteArray> m_responses;
};
}

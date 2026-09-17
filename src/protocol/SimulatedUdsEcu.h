#pragma once
#include "FlashJob.h"
#include <QMap>
namespace boot {
class SimulatedUdsEcu {
public:
    struct Faults {
        int dropService=-1,negativeService=-1,negativeCode=0x22,pendingService=-1,pendingCount=0;
        bool badBlockEcho=false,badBlockLength=false,badVerify=false,badResponseSequence=false;
    } faults;

    explicit SimulatedUdsEcu(FlashProfile profile={}):m_profile(std::move(profile)){}
    QList<QByteArray> handle(const QByteArray &request);
    QByteArray memory(quint32 address) const{return m_memory.value(address);}
    QList<QByteArray> requests;
    int resets=0,maxBlockLength=258;
private:
    QByteArray process(const QByteArray &);
    FlashProfile m_profile;
    QMap<quint32,QByteArray> m_memory;QByteArray m_pending;quint32 m_address=0,m_length=0;
    quint8 m_sequence=1,m_seedLevel=0;bool m_programming=false,m_unlocked=false,m_seedRequested=false,m_transfer=false;
};
}

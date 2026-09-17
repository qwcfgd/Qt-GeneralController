#include "SimulatedUdsEcu.h"
namespace boot {
QList<QByteArray> SimulatedUdsEcu::handle(const QByteArray &pdu){
    QList<QByteArray> replies;if(pdu.isEmpty())return replies;
    requests.append(pdu);const int sid=quint8(pdu[0]);
    const bool canSuppress=sid==0x10||sid==0x11||sid==0x28||sid==0x3e||sid==0x85;
    const bool suppressPositive=canSuppress&&pdu.size()>1&&(quint8(pdu[1])&0x80);
    if(faults.dropService==sid)return replies;
    if(faults.negativeService==sid){replies.append(QByteArray(1,char(0x7f))+char(sid)+char(faults.negativeCode));return replies;}
    if(faults.pendingService==sid)for(int i=0;i<faults.pendingCount;++i)replies.append(QByteArray(1,char(0x7f))+char(sid)+char(0x78));
    auto normalized=pdu;if(suppressPositive)normalized[1]=char(quint8(normalized[1])&0x7f);
    const auto reply=process(normalized);if(!reply.isEmpty()&&!suppressPositive)replies.append(reply);return replies;
}
QByteArray SimulatedUdsEcu::process(const QByteArray &r){
    const int sid=quint8(r[0]);
    auto nrc=[&](int code){return QByteArray(1,char(0x7f))+char(sid)+char(code);};
    auto positive=[&](int length){QByteArray p=r.left(length);p[0]=char(sid+0x40);return p;};
    if(sid==0x10){
        if(r.size()!=2)return nrc(0x13);
        if(quint8(r[1])!=m_profile.session&&(m_profile.flow=="legacy"||(quint8(r[1])!=1&&quint8(r[1])!=3&&quint8(r[1])!=2)))return nrc(0x12);
        m_programming=true;m_unlocked=m_seedRequested=m_transfer=false;m_pending.clear();
        return positive(2)+QByteArray::fromHex("003201f4"); // P2=50 ms, P2*=5000 ms.
    }
    if(sid==0x3e){if(r==QByteArray::fromHex("3e80"))return {};if(r==QByteArray::fromHex("3e00"))return QByteArray::fromHex("7e00");return nrc(0x12);}
    if(sid==0x22){
        if(r.size()!=3)return nrc(0x13);
        if(readBe(r.mid(1))!=m_profile.identityDid&&(m_profile.flow=="legacy"||readBe(r.mid(1))!=0x0101))return nrc(0x31);
        return positive(3)+QByteArray("SIM-ECU");
    }
    if(m_profile.flow!="legacy"){
        if(sid==0x85&&r.size()==2&&(r[1]==1||r[1]==2))return positive(2);
        if(sid==0x28&&r.size()==3&&(r[1]==0||r[1]==3)&&r[2]==1)return positive(2);
        if(r==QByteArray::fromHex("14ffffff"))return positive(1);
        if(r==QByteArray::fromHex("31010203"))return positive(4)+char(0);
        if(r==QByteArray::fromHex("2ef1840101"))return positive(3);
    }
    if(!m_programming)return nrc(0x7f);
    if(sid==0x27){
        if(r.size()<2)return nrc(0x13);const int level=quint8(r[1]);
        if(level==m_profile.securityLevel||(m_profile.flow!="legacy"&&level==1)){
            if(r.size()!=2)return nrc(0x13);m_seedRequested=true;m_seedLevel=quint8(level);
            return positive(2)+(m_unlocked?QByteArray(4,0):QByteArray::fromHex("12345678"));
        }
        if(level==m_seedLevel+1){
            if(!m_seedRequested)return nrc(0x24);
            // Independent fixture vector construction (not a call to the host KeyProvider).
            const auto expected=QByteArray::fromHex("12345678");QByteArray key;
            for(unsigned char b:expected)key.append(char(b^0xa5^m_seedLevel));
            m_seedRequested=false;if(r.mid(2)!=key)return nrc(0x35);m_unlocked=true;return positive(2);
        }
        return nrc(0x12);
    }
    if(!m_unlocked)return nrc(0x33);
    if(sid==0x31){
        if(r.size()<4||quint8(r[1])!=1)return nrc(0x13);
        const int rid=readBe(r.mid(2,2));
        if(rid==m_profile.eraseRoutine){
            if(r.size()!=13||quint8(r[4])!=0x44)return nrc(0x13);
            const auto address=readBe(r.mid(5,4)),length=readBe(r.mid(9,4));
            if(!length||length>64*1024*1024||quint64(address)+length>0x100000000ULL)return nrc(0x31);
            m_memory.remove(address);return positive(4)+char(0);
        }
        if(rid==m_profile.verifyRoutine){
            if(r.size()!=16)return nrc(0x13);const auto address=readBe(r.mid(4,4)),length=readBe(r.mid(8,4)),crc=readBe(r.mid(12,4));
            if(faults.badVerify||!m_memory.contains(address)||quint32(m_memory[address].size())!=length||crc32(m_memory[address])!=crc)return positive(4)+char(1);
            return positive(4)+char(0);
        }
        if(rid==m_profile.dependencyRoutine){if(r.size()!=4)return nrc(0x13);return positive(4)+char(m_memory.isEmpty()?1:0);}
        return nrc(0x31);
    }
    if(sid==0x34){
        if(r.size()!=11||r.mid(1,2)!=QByteArray::fromHex("0044"))return nrc(0x13);
        if(m_transfer)return nrc(0x24);
        m_address=readBe(r.mid(3,4));m_length=readBe(r.mid(7,4));
        if(!m_length||m_length>64*1024*1024||quint64(m_address)+m_length>0x100000000ULL)return nrc(0x31);
        m_pending.clear();m_transfer=true;m_sequence=1;
        if(faults.badBlockLength)return QByteArray::fromHex("741002");
        return QByteArray::fromHex("7420")+char(maxBlockLength>>8)+char(maxBlockLength);
    }
    if(sid==0x36){
        if(!m_transfer)return nrc(0x24);
        if(r.size()<3||r.size()>maxBlockLength)return nrc(0x13);
        if(quint8(r[1])!=m_sequence)return nrc(0x73);
        if(quint64(m_pending.size())+quint64(r.size()-2)>m_length)return nrc(0x71);
        m_pending+=r.mid(2);++m_sequence;
        auto p=positive(2);if(faults.badBlockEcho)p[1]=char(quint8(p[1])+1);return p;
    }
    if(sid==0x37){
        if(r.size()!=1)return nrc(0x13);
        if(!m_transfer||quint32(m_pending.size())!=m_length)return nrc(0x24);
        m_memory[m_address]=m_pending;m_transfer=false;m_pending.clear();return positive(1);
    }
    if(sid==0x11){
        if(r!=QByteArray::fromHex("1101"))return nrc(0x12);
        if(m_transfer)return nrc(0x24);++resets;m_programming=m_unlocked=false;return positive(2);
    }
    return nrc(0x11);
}
}

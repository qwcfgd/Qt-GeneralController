#pragma once
#include "DiagnosticTransport.h"
#include <QElapsedTimer>
#include <QJsonObject>
#include <QTimer>
#include <functional>
namespace boot {
struct CanFrame {
    quint32 id=0;
    bool extended=false,rtr=false,error=false,fd=false;
    QByteArray data;
};
struct CanOptions {
    quint32 txId=0x715,rxId=0x795;
    bool extended=false;
    int blockSize=8,stMin=1,maxWaitFrames=3,nAsMs=1000,nBsMs=1000,nCrMs=1000,receiveCapacity=4095,padding=0xff;
    bool valid(QString &) const;
    static bool fromJson(const QJsonObject &,CanOptions &,QString &);
    QJsonObject toJson() const;
};
class CanTransport final : public DiagnosticTransport {
    Q_OBJECT
public:
    // A successful write only queues a frame. The adapter must later confirm its token.
    using Write=std::function<bool(const CanFrame &,quint64 token,QString &error)>;
    CanTransport(CanOptions,Write,QObject *parent=nullptr);
    bool send(const QByteArray &,QString &) override;
    void cancel() override;
    int maximumPdu() const override{return 4095;}
    bool busy() const{return m_txState!=Idle||m_pending!=None||m_rxLength>0;}
    void listen(){m_listening=true;}
    void receiveFrame(const CanFrame &);
    void confirmTransmitted(quint64 token,bool success=true);
    void linkFailed(const QString &error){fail(error);}
    static int separationUs(int stMin);
signals:
    void trace(bool transmit,boot::CanFrame frame);
private:
    enum TxState {Idle,FramePending,WaitingFc,SendingCf};
    enum Kind {None,Single,First,Consecutive,FlowControl,Overflow};
    void submit(QByteArray,Kind);
    void flowControl(const QByteArray &);
    void scheduleCf();
    void sendCf();
    void sendFc(bool overflow=false);
    void fail(const QString &);
    void txComplete();
    CanOptions m_options;Write m_write;
    QTimer m_as,m_bs,m_cr,m_cf;
    QByteArray m_tx,m_rx,m_earlyFc;
    QElapsedTimer m_lastCf;
    int m_txOffset=0,m_txSequence=1,m_remainingBlock=0,m_peerBlock=0,m_peerStUs=0,m_waitFrames=0;
    int m_rxLength=0,m_rxSequence=1,m_rxBlock=0,m_pendingCount=0;
    quint64 m_serial=0,m_token=0,m_epoch=0;
    TxState m_txState=Idle;Kind m_pending=None;bool m_listening=false;
};
}
Q_DECLARE_METATYPE(boot::CanFrame)

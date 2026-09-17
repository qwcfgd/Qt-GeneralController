#pragma once
#include "CanTransport.h"
#include "SimulatedUdsEcu.h"
#include <QPointer>
#include <QQueue>
namespace boot {
class SimulatedCanEcu final : public QObject {
    Q_OBJECT
public:
    SimulatedCanEcu(CanOptions hostOptions,FlashProfile,QObject *parent=nullptr);
    void attach(CanTransport &host);
    bool writeFromHost(const CanFrame &,quint64 token,QString &error);
    void clearWire();
    SimulatedUdsEcu uds;
signals:
    void failed(QString);
private:
    void pump();
    QPointer<CanTransport> m_host;
    CanTransport m_endpoint;
    QQueue<QByteArray> m_replies;
    quint64 m_epoch=0;
};
}

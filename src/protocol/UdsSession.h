#pragma once
#include "DiagnosticTransport.h"
#include <QQueue>
#include <QTimer>
#include <QElapsedTimer>
#include <functional>
namespace boot {
struct SessionOptions {int p2Ms=1000,p2StarMs=5500,maxPendingMs=60000,testerPresentMs=0;};
class UdsSession final : public QObject {
    Q_OBJECT
public:
    using Reply=std::function<void(bool,const QByteArray &,const QString &)>;
    UdsSession(DiagnosticTransport &,SessionOptions,QObject *parent=nullptr);
    bool request(QByteArray bytes,QByteArray expected,Reply reply,bool suppressed=false);
    void cancel();
    bool busy() const{return m_active||!m_queue.isEmpty();}
    int maximumPdu() const{return m_transport.maximumPdu();}
signals:
    void trace(bool transmit,QByteArray pdu);
    void notice(QString);
private:
    struct Request {QByteArray bytes,expected;Reply reply;bool suppressed=false;};
    void pump();
    void response(const QByteArray &);
    void finish(bool,const QByteArray &,const QString &);
    DiagnosticTransport &m_transport;SessionOptions m_options;QQueue<Request> m_queue;Request m_current;
    QTimer m_response,m_limit,m_keepalive;bool m_active=false,m_sent=false,m_established=false,m_pending=false;quint64 m_epoch=0;
};
}

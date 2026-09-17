#pragma once
#include <QObject>
#include <QByteArray>
namespace boot {
class DiagnosticTransport : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual bool send(const QByteArray &pdu,QString &error)=0;
    virtual void cancel()=0;
    virtual int maximumPdu() const=0;
signals:
    void sent();
    void responseStarted(QByteArray prefix);
    void cancelled();
    void received(QByteArray);
    void failed(QString);
};
}

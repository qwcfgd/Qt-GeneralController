#pragma once
#include "FirmwareImage.h"
#include "UdsSession.h"
#include "DownloadSteps.h"
#include <QJsonObject>
#include <memory>
namespace boot {
class KeyProvider {
public:
    virtual ~KeyProvider()=default;
    virtual QByteArray calculate(quint8 level,const QByteArray &seed,QString &error)=0;
};
// Explicitly a fixture algorithm. It must never be selected for an unconfigured physical ECU.
class SimulationKey final : public KeyProvider {
public: QByteArray calculate(quint8,const QByteArray &,QString &) override;
};
class ZeroKey final : public KeyProvider {
public: QByteArray calculate(quint8,const QByteArray &,QString &error) override {error.clear();return QByteArray(1,char(0));}
};
struct FlashProfile {
    QString name="Simulated UDS ECU",keyProvider="simulation-xor-v1";
    bool simulationOnly=true;
    QString flow="legacy",keyLibrary;
    QJsonObject stepEnabled,negativeResponseChecks,timeoutChecks;
    bool enabled(const QString &id)const{
        for(const auto &step:downloadSteps())if(id==step.id)return stepEnabled.value(id).toBool(flow!="boot"||!step.appOnly);
        return false;
    }
    bool checksNegativeResponse(const QString &step)const{return negativeResponseChecks.value(step).toBool(flow=="legacy");}
    bool checksTimeout(const QString &step)const{
        return !suppressesPositiveResponse(step)&&timeoutChecks.value(step).toBool(flow=="legacy");
    }
    quint8 session=2,securityLevel=0x11;
    quint16 eraseRoutine=0xff00,verifyRoutine=0xff01,dependencyRoutine=0xff02,identityDid=0xf180;
    int consecutiveFrameByteLimit=4095,resetWaitMs=10;
    static bool fromJson(const QJsonObject &,FlashProfile &,QString &);
    QJsonObject toJson() const;
};
class FlashJob final : public QObject {
    Q_OBJECT
public:
    FlashJob(UdsSession &,FlashProfile,std::unique_ptr<KeyProvider>,QObject *parent=nullptr);
    ~FlashJob() override{cancel();}
    bool start(const FirmwareImage &application,const FirmwareImage &driver,QString &error);
    void cancel();
    bool running() const{return m_running;}
signals:
    void progress(int,QString);
    void finished(bool,QString);
    void notice(QString);
    void stepEvent(QString);
private:
    void ask(const QByteArray &,const QByteArray &,std::function<void(const QByteArray &)>);
    void seed();
    void enterProgramming();
    void preSteps(int);
    void postSteps(int);
    void unlock(quint8,const QString &,const QString &,std::function<void()>);
    void identify();
    void done();
    void step(const QString &,const QByteArray &,const QByteArray &,std::function<void(const QByteArray &)>,bool requiredData=false);
    void nextSegment();
    void download();
    void block();
    void verify();
    void finalSteps();
    void fail(const QString &);
    void report(const QString &);
    QString stepText(const QString &) const;
    UdsSession &m_session;FlashProfile m_profile;std::unique_ptr<KeyProvider> m_key;
    QVector<Segment> m_segments;int m_driverCount=0,m_index=0,m_offset=0,m_blockSize=0,m_blockLength=0;
    quint8 m_sequence=1;quint64 m_epoch=0;qint64 m_total=0,m_acked=0;bool m_running=false;
    int m_ignored=0,m_transferPacket=0;
};
}

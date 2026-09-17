#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextStream>
#include <QThread>
#include "communication/SoftwareChannel.h"
using namespace communication;

// Opt-in header-only probe. No LIN publisher data or UDS requests are sent.
int main(int argc, char **argv)
{
    QCoreApplication app(argc,argv);
    QCommandLineParser p; p.addHelpOption();
    p.addOption({"scan","Send headers for IDs 00..3B and 3D. Excludes master request and reserved IDs."});
    p.addOption({"bitrate","LIN bitrate in bit/s.","bps","19200"});
    p.addOption({"rounds","Number of scan rounds (1..5).","count","2"});
    p.addOption({"report","JSON evidence file.","file","plin-scan.json"});
    p.process(app);
    QTextStream out(stdout);
    if (!p.isSet("scan")) {p.showHelp(0);}
    bool baudOk=false, roundsOk=false;
    const int baud=p.value("bitrate").toInt(&baudOk), rounds=p.value("rounds").toInt(&roundsOk);
    if (!baudOk || baud<1000 || baud>20000 || !roundsOk || rounds<1 || rounds>5) return 2;
    PLinApiClass api; tstPeakLin driver(nullptr,&api);
    auto found=driver.availableHardware();
    if (driver.lastErrorCode()!=errOK || found.size()!=1 || !found.first().available) {
        out<<"Require exactly one available PLIN. "<<driver.lastErrorText()<<Qt::endl; return 3;
    }
    SoftwareChannelConfiguration cfg;cfg.softwareId="LIN-header-scan";cfg.bus=Bus::Lin;
    cfg.bitrate=baud;cfg.hardwareKey=found.first().key;cfg.preferredHandle=found.first().handle;
    SoftwareChannel channel(cfg,std::unique_ptr<HardwareBackend>(new PeakLinBackend(driver)));
    if (!channel.connectChannel()) {out<<channel.error()<<Qt::endl;return 4;}
    QString healthDetail;
    const auto initialHealth=driver.hardwareHealth(&healthDetail);
    out<<found.first().label<<" bitrate="<<baud<<" health="<<int(initialHealth)<<" "<<healthDetail<<Qt::endl;
    if (initialHealth!=Health::Ready) {channel.disconnectChannel();return 5;}
    driver.clearMsg();
    QJsonArray records;
    int sent=0,observed=0,noResponse=0,responses=0,failures=0,statusEvents=0;
    QElapsedTimer clock;clock.start();
    // At least 100 ms at 19.2 kbit/s; scaled at lower baud rates.
    const int windowMs=qMax(100,2000000/baud);
    for(int round=1;round<=rounds && failures==0;++round) {
        for(int id=0;id<=0x3d && failures==0;++id) {
            if (id==0x3c) continue;
            TLINMsg tx={};tx.FrameId=static_cast<BYTE>(id);tx.Length=8;
            tx.Direction=dirSubscriberAutoLength;tx.ChecksumType=id==0x3d?cstClassic:cstAuto;
            BYTE expectedPid=tx.FrameId;api.GetPID(&expectedPid);
            if(!driver.sendRaw(tx)) {out<<"WRITE FAIL "<<driver.lastErrorText()<<Qt::endl;++failures;break;}
            ++sent;bool matched=false;
            QElapsedTimer slot;slot.start();
            while(slot.elapsed()<windowMs) {
                TLINRcvMsg rx[64]={};const DWORD count=driver.recvRaw(64,rx);
                if(driver.lastErrorCode()!=errOK && driver.lastErrorCode()!=errRcvQueueEmpty) {
                    out<<"READ FAIL "<<driver.lastErrorText()<<Qt::endl;++failures;break;
                }
                for(DWORD i=0;i<count;++i) {
                    const auto &m=rx[i];
                    if(m.Type!=mstStandard) {++statusEvents;continue;}
                    const bool matching=(m.FrameId==expectedPid && m.hHw==found.first().handle && m.TimeStamp>0);
                    const bool absent=(m.ErrorFlags & MSG_ERR_SLAVE_NOT_RESPONDING)!=0;
                    const bool good=(m.ErrorFlags==0 && m.Length>=1 && m.Length<=8);
                    const bool expectedAbsent=(absent && (m.ErrorFlags & ~MSG_ERR_SLAVE_NOT_RESPONDING)==0);
                    if(!matching || (!good && !expectedAbsent)) ++failures;
                    if(matching) {matched=true;++observed;}
                    if(expectedAbsent) ++noResponse;
                    if(good) ++responses;
                    const QByteArray data(reinterpret_cast<const char*>(m.Data),qMin(int(m.Length),8));
                    QJsonObject record{{"round",round},{"id",id},{"pid",int(m.FrameId)},
                        {"length",int(m.Length)},{"direction",int(m.Direction)},{"checksumType",int(m.ChecksumType)},
                        {"checksum",int(m.Checksum)},{"errorFlags",int(m.ErrorFlags)},
                        {"hardwareTimestampUs",QString::number(m.TimeStamp)},{"elapsedMs",clock.elapsed()},
                        {"result",good?"response":(expectedAbsent?"no-response":"error")},
                        {"data",good?QString::fromLatin1(data.toHex(' ')):QString()}};
                    records.append(record);
                    out<<"round="<<round<<" id=0x"<<QString::number(id,16).rightJustified(2,'0')
                       <<" pid=0x"<<QString::number(m.FrameId,16)<<" length="<<int(m.Length)
                       <<" flags=0x"<<QString::number(m.ErrorFlags,16)<<" "
                       <<(good?"RESPONSE":(expectedAbsent?"NO_RESPONSE":"ERROR"))
                       <<" timestamp-us="<<m.TimeStamp<<Qt::endl;
                }
                QThread::msleep(2);
            }
            if(!matched) {out<<"No hardware receive event for ID "<<id<<Qt::endl;++failures;}
        }
    }
    const auto finalHealth=driver.hardwareHealth(&healthDetail);
    if(finalHealth!=Health::Ready)++failures;
    const bool closed=channel.disconnectChannel();if(!closed)++failures;
    QJsonObject summary{{"utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"device",found.first().label},{"dll",api.libraryPath()},{"bitrate",baud},{"rounds",rounds},
        {"headerOnly",true},{"sent",sent},{"observed",observed},{"noResponse",noResponse},{"responses",responses},
        {"failures",failures},{"statusEvents",statusEvents},{"finalHealth",int(finalHealth)},
        {"healthDetail",healthDetail},{"closed",closed},{"records",records}};
    QSaveFile file(p.value("report"));
    if(!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(summary).toJson())<0 || !file.commit()) {
        out<<"Cannot save report"<<Qt::endl;return 6;
    }
    out<<"SUMMARY sent="<<sent<<" observed="<<observed<<" no-response="<<noResponse
       <<" responses="<<responses<<" failures="<<failures<<Qt::endl;
    return failures?1:0;
}

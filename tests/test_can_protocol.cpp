#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QDir>
#include "protocol/SimulatedCanEcu.h"
#include "protocol/SimulatedLinEcu.h"
using namespace boot;
static QByteArray hx(const char *s){return QByteArray::fromHex(s);}
static bool until(const std::function<bool()> &condition,int timeout=3000){
    QElapsedTimer elapsed;elapsed.start();
    while(!condition()&&elapsed.elapsed()<timeout)QTest::qWait(1);
    return condition();
}
static CanFrame incoming(const CanOptions &o,const QByteArray &bytes){return {o.rxId,o.extended,false,false,false,bytes};}
struct ManualCan {
    bool acknowledge=true;QList<CanFrame> writes;QList<quint64> tokens;CanTransport transport;
    explicit ManualCan(CanOptions o={}):transport(o,[this](const CanFrame &f,quint64 token,QString &){
        writes.append(f);tokens.append(token);if(acknowledge)transport.confirmTransmitted(token);return true;
    }){}
};
struct Pair : QObject {
    CanTransport a,b;QList<CanFrame> wire;
    Pair(CanOptions first,CanOptions second)
      :a(first,[this](const CanFrame &f,quint64 t,QString &){
          wire.append(f);QTimer::singleShot(1,this,[this,f,t]{a.confirmTransmitted(t);b.receiveFrame(f);});return true;
       }),b(second,[this](const CanFrame &f,quint64 t,QString &){
          wire.append(f);QTimer::singleShot(1,this,[this,f,t]{b.confirmTransmitted(t);a.receiveFrame(f);});return true;
       }){a.listen();b.listen();}
};
struct CanBench {
    SimulatedCanEcu ecu;CanTransport network;UdsSession session;
    CanBench(CanOptions options={},FlashProfile profile={})
     :ecu(options,profile),network(options,[this](const CanFrame &f,quint64 t,QString &e){return ecu.writeFromHost(f,t,e);}),
      session(network,{100,5500,60000,0}){
        ecu.attach(network);QObject::connect(&ecu,&SimulatedCanEcu::failed,&network,&CanTransport::linkFailed);
    }
};
class CanProtocolTest : public QObject {
    Q_OBJECT
    static FirmwareImage image(int n,quint32 address=0xff00){
        FirmwareImage result;QByteArray bytes(n,0);for(int i=0;i<n;++i)bytes[i]=char(i*17+3);
        result.segments.append({address,bytes});result.size=n;return result;
    }
private slots:
    void initTestCase(){qRegisterMetaType<CanFrame>();}
    void fixedTransmitVectorsAndBs(){
        CanOptions options;ManualCan link(options);QString error;
        QSignalSpy sent(&link.transport,&DiagnosticTransport::sent);
        QVERIFY(link.transport.send(hx("1002"),error));
        QVERIFY(until([&]{return sent.size()==1;}));
        QCOMPARE(link.writes[0].id,quint32(0x715));QCOMPARE(link.writes[0].data,hx("021002ffffffffff"));
        link.transport.cancel();link.writes.clear();
        QVERIFY(link.transport.send(hx("000102030405060708090a0b0c0d0e0f10111213"),error));
        QVERIFY(until([&]{return link.writes.size()==1;}));
        QCOMPARE(link.writes[0].data,hx("1014000102030405"));
        link.transport.receiveFrame(incoming(options,hx("300100")));
        QVERIFY(until([&]{return link.writes.size()==2;}));
        QCOMPARE(link.writes[1].data,hx("21060708090a0b0c"));
        QTest::qWait(10);QCOMPARE(link.writes.size(),2);
        link.transport.receiveFrame(incoming(options,hx("300000")));
        QVERIFY(until([&]{return sent.size()==2;}));
        QCOMPARE(link.writes[2].data,hx("220d0e0f10111213"));
    }
    void fixedReceiveVectorsAndFlowControl(){
        CanOptions options;options.blockSize=1;options.stMin=0;
        ManualCan link(options);QSignalSpy received(&link.transport,&DiagnosticTransport::received);link.transport.listen();
        link.transport.receiveFrame(incoming(options,hx("1014000102030405")));
        QVERIFY(until([&]{return link.writes.size()==1;}));
        QCOMPARE(link.writes[0].data,hx("300100ffffffffff"));
        link.transport.receiveFrame(incoming(options,hx("21060708090a0b0c")));
        QVERIFY(until([&]{return link.writes.size()==2;}));
        link.transport.receiveFrame(incoming(options,hx("220d0e0f10111213")));
        QCOMPARE(received.size(),1);QCOMPARE(received[0][0].toByteArray(),hx("000102030405060708090a0b0c0d0e0f10111213"));
        link.transport.receiveFrame(incoming(options,hx("03620101")));QCOMPARE(received.size(),2);
        QCOMPARE(received[1][0].toByteArray(),hx("620101"));
    }
    void roundtripBoundaries_data(){
        QTest::addColumn<int>("length");QTest::addColumn<int>("bs");QTest::addColumn<bool>("extended");
        for(int n:{1,7,8,13,14,118,4095})for(bool ext:{false,true})
            QTest::newRow(qPrintable(QString("%1-%2").arg(n).arg(ext)))<<n<<(n%3==0?0:n%3==1?1:8)<<ext;
    }
    void roundtripBoundaries(){
        QFETCH(int,length);QFETCH(int,bs);QFETCH(bool,extended);
        CanOptions a;a.extended=extended;a.txId=extended?0x18da10f1:0x715;a.rxId=extended?0x18daf110:0x795;
        a.blockSize=bs;a.stMin=0;auto b=a;qSwap(b.txId,b.rxId);
        Pair link(a,b);QSignalSpy got(&link.b,&DiagnosticTransport::received),done(&link.a,&DiagnosticTransport::sent);
        QByteArray data(length,0);for(int i=0;i<length;++i)data[i]=char(i);QString error;
        QVERIFY(link.a.send(data,error));QVERIFY(until([&]{return got.size()==1;},10000));
        QCOMPARE(done.size(),1);QCOMPARE(got[0][0].toByteArray(),data);
        int sn=1;
        for(const auto &f:link.wire){
            QCOMPARE(f.extended,extended);QCOMPARE(f.data.size(),8);
            if(f.id==a.txId&&(quint8(f.data[0])>>4)==2){QCOMPARE(quint8(f.data[0])&15,sn);sn=(sn+1)&15;}
        }
    }
    void flowControlFaults_data(){
        QTest::addColumn<QByteArray>("fc");QTest::addColumn<QString>("expected");
        QTest::newRow("overflow")<<hx("320000")<<QString("overflow");
        QTest::newRow("invalid-status")<<hx("330000")<<QString("status");
        QTest::newRow("reserved-stmin")<<hx("300080")<<QString("STmin");
        QTest::newRow("short-fc")<<hx("3000")<<QString("Short");
    }
    void flowControlFaults(){
        QFETCH(QByteArray,fc);QFETCH(QString,expected);CanOptions o;ManualCan link(o);QString e;
        QSignalSpy failure(&link.transport,&DiagnosticTransport::failed),sent(&link.transport,&DiagnosticTransport::sent);
        QVERIFY(link.transport.send(QByteArray(32,'x'),e));QVERIFY(until([&]{return link.writes.size()==1;}));
        link.transport.receiveFrame(incoming(o,fc));QCOMPARE(failure.size(),1);QVERIFY(failure[0][0].toString().contains(expected));QCOMPARE(sent.size(),0);
        const int count=link.writes.size();QTest::qWait(30);QCOMPARE(link.writes.size(),count);
    }
    void waitsAreBoundedAndCtsResumes(){
        CanOptions o;o.maxWaitFrames=2;ManualCan link(o);QString e;QSignalSpy failure(&link.transport,&DiagnosticTransport::failed),sent(&link.transport,&DiagnosticTransport::sent);
        QVERIFY(link.transport.send(QByteArray(14,'a'),e));QVERIFY(until([&]{return link.writes.size()==1;}));
        link.transport.receiveFrame(incoming(o,hx("310000")));link.transport.receiveFrame(incoming(o,hx("310000")));
        QCOMPARE(link.writes.size(),1);link.transport.receiveFrame(incoming(o,hx("300000")));
        QVERIFY(until([&]{return sent.size()==1;}));QCOMPARE(failure.size(),0);
        link.transport.cancel();QVERIFY(link.transport.send(QByteArray(14,'b'),e));
        QVERIFY(until([&]{return link.writes.size()==4;}));
        for(int i=0;i<3;++i)link.transport.receiveFrame(incoming(o,hx("310000")));
        QCOMPARE(failure.size(),1);QVERIFY(failure[0][0].toString().contains("WAIT"));
    }
    void transportTimeouts_data(){
        QTest::addColumn<QString>("mode");
        for(const char *s:{"N_As","N_Bs","N_Cr","N_Ar"})QTest::newRow(s)<<QString(s);
    }
    void transportTimeouts(){
        QFETCH(QString,mode);CanOptions o;o.nAsMs=o.nBsMs=o.nCrMs=25;ManualCan link(o);QString e;
        QSignalSpy failure(&link.transport,&DiagnosticTransport::failed);
        if(mode=="N_As"){link.acknowledge=false;QVERIFY(link.transport.send(hx("1002"),e));}
        if(mode=="N_Bs")QVERIFY(link.transport.send(QByteArray(12,0),e));
        if(mode=="N_Cr"||mode=="N_Ar"){
            link.acknowledge=mode!="N_Ar";link.transport.listen();link.transport.receiveFrame(incoming(o,hx("100e010203040506")));
        }
        QVERIFY(until([&]{return failure.size()==1;},300));QVERIFY(failure[0][0].toString().contains(mode));
        const int n=link.writes.size();QTest::qWait(30);QCOMPARE(link.writes.size(),n);
    }
    void receiveFaults_data(){
        QTest::addColumn<QByteArray>("frame");QTest::addColumn<bool>("first");
        QTest::newRow("empty")<<QByteArray()<<false;
        QTest::newRow("fd-dlc")<<QByteArray(9,0)<<false;
        QTest::newRow("zero-sf")<<hx("00")<<false;
        QTest::newRow("sf-too-long")<<hx("0801020304050607")<<false;
        QTest::newRow("short-sf")<<hx("036201")<<false;
        QTest::newRow("short-ff")<<hx("1008010203")<<false;
        QTest::newRow("ff-length-zero")<<hx("1000000000000000")<<false;
        QTest::newRow("cf-without-ff")<<hx("2101020304050607")<<false;
        QTest::newRow("wrong-sn")<<hx("220708090a0b0c0d")<<true;
        QTest::newRow("short-cf")<<hx("2107")<<true;
        QTest::newRow("unexpected-sf")<<hx("025002")<<true;
    }
    void receiveFaults(){
        QFETCH(QByteArray,frame);QFETCH(bool,first);CanOptions o;ManualCan link(o);QSignalSpy failure(&link.transport,&DiagnosticTransport::failed),got(&link.transport,&DiagnosticTransport::received);
        link.transport.listen();
        if(first){link.transport.receiveFrame(incoming(o,hx("1014010203040506")));QVERIFY(until([&]{return link.writes.size()==1;}));}
        link.transport.receiveFrame(incoming(o,frame));QCOMPARE(failure.size(),1);QCOMPARE(got.size(),0);
    }
    void capacityOverflowAndIdentifierFiltering(){
        CanOptions o;o.receiveCapacity=8;ManualCan link(o);link.transport.listen();
        QSignalSpy failure(&link.transport,&DiagnosticTransport::failed),got(&link.transport,&DiagnosticTransport::received);
        auto f=incoming(o,hx("025002"));f.id++;link.transport.receiveFrame(f);
        f.id=o.rxId;f.extended=!o.extended;link.transport.receiveFrame(f);
        f.extended=o.extended;f.rtr=true;link.transport.receiveFrame(f);
        f.rtr=false;f.fd=true;link.transport.receiveFrame(f);QCOMPARE(got.size(),0);QCOMPARE(failure.size(),0);
        link.transport.receiveFrame(incoming(o,hx("1009010203040506")));
        QVERIFY(until([&]{return failure.size()==1;}));QCOMPARE(link.writes[0].data.left(1),hx("32"));
    }
    void cancellationInvalidatesQueuedWritesAndConfirmations(){
        CanOptions o;ManualCan link(o);link.acknowledge=false;QString error;QSignalSpy sent(&link.transport,&DiagnosticTransport::sent);
        QVERIFY(link.transport.send(hx("1002"),error));link.transport.cancel();QTest::qWait(10);QVERIFY(link.writes.isEmpty());
        QVERIFY(link.transport.send(hx("1002"),error));QVERIFY(until([&]{return link.writes.size()==1;}));const auto old=link.tokens[0];
        link.transport.cancel();QVERIFY(link.transport.send(hx("1003"),error));QVERIFY(until([&]{return link.writes.size()==2;}));
        link.transport.confirmTransmitted(old);QCOMPARE(sent.size(),0);
        link.transport.confirmTransmitted(link.tokens[1]);QCOMPARE(sent.size(),1);
        link.transport.cancel();const int count=link.writes.size();link.transport.receiveFrame(incoming(o,hx("1009010203040506")));
        QTest::qWait(20);QCOMPARE(link.writes.size(),count);
    }
    void earlyFlowControlAndLinkFailure(){
        CanOptions o;ManualCan link(o);link.acknowledge=false;QString e;
        QSignalSpy sent(&link.transport,&DiagnosticTransport::sent),failed(&link.transport,&DiagnosticTransport::failed);
        QVERIFY(link.transport.send(QByteArray(8,'a'),e));QVERIFY(until([&]{return link.writes.size()==1;}));
        link.transport.receiveFrame(incoming(o,hx("300000")));QCOMPARE(link.writes.size(),1);
        link.transport.confirmTransmitted(link.tokens[0]);QVERIFY(until([&]{return link.writes.size()==2;}));
        link.transport.confirmTransmitted(link.tokens[1],false);QCOMPARE(failed.size(),1);QCOMPARE(sent.size(),0);
        const int n=link.writes.size();QTest::qWait(30);QCOMPARE(link.writes.size(),n);
        CanTransport rejected(o,[](const CanFrame &,quint64,QString &error){error="driver rejected";return false;});
        QSignalSpy rejection(&rejected,&DiagnosticTransport::failed);QVERIFY(rejected.send(hx("1002"),e));
        QVERIFY(until([&]{return rejection.size()==1;}));QCOMPARE(rejection[0][0].toString(),QString("driver rejected"));
    }
    void separationIsNeverShortened_data(){
        QTest::addColumn<int>("stmin");
        for(int st:{0,5,0x7f,0xf1,0xf9})QTest::newRow(qPrintable(QString::number(st)))<<st;
    }
    void separationIsNeverShortened(){
        QFETCH(int,stmin);CanOptions o;ManualCan link(o);QElapsedTimer clock;clock.start();QList<qint64> times;
        connect(&link.transport,&CanTransport::trace,&link.transport,[&](bool tx,const CanFrame &f){
            if(tx&&(quint8(f.data[0])>>4)==2)times.append(clock.nsecsElapsed()/1000);
        });
        QString e;QSignalSpy done(&link.transport,&DiagnosticTransport::sent);
        QVERIFY(link.transport.send(QByteArray(30,0),e));QVERIFY(until([&]{return link.writes.size()==1;}));
        link.transport.receiveFrame(incoming(o,QByteArray::fromHex("3000")+char(stmin)));
        QVERIFY(until([&]{return done.size()==1;},1500));QCOMPARE(times.size(),4);
        for(int i=1;i<times.size();++i)QVERIFY2(times[i]-times[i-1]>=CanTransport::separationUs(stmin),qPrintable(QString::number(times[i]-times[i-1])));
    }
    void optionsValidation(){
        CanOptions o;QString error;QVERIFY(o.valid(error));
        for(const QJsonObject &bad:{QJsonObject{{"blockSize",256}},QJsonObject{{"stMin",0x80}},QJsonObject{{"padding",256}},
          QJsonObject{{"receiveCapacity",4096}},QJsonObject{{"nBsMs",0}},QJsonObject{{"stMin",1.5}},QJsonObject{{"unknown",1}}})
            QVERIFY(!CanOptions::fromJson(bad,o,error));
        QVERIFY(CanOptions::fromJson({{"stMin",0xf9},{"blockSize",0}},o,error));
        auto json=o.toJson();CanOptions b;QVERIFY(CanOptions::fromJson(json,b,error));QCOMPARE(json,b.toJson());
        o.txId=0x800;QVERIFY(!o.valid(error));o.extended=true;QVERIFY(o.valid(error));o.rxId=o.txId;QVERIFY(!o.valid(error));
    }
    void fullUdsFlow_data(){QTest::addColumn<bool>("extended");QTest::newRow("11-bit")<<false;QTest::newRow("29-bit")<<true;}
    void fullUdsFlow(){
        QFETCH(bool,extended);CanOptions o;o.extended=extended;o.txId=extended?0x18da10f1:0x715;o.rxId=extended?0x18daf110:0x795;
        FlashProfile profile;CanBench b(o,profile);FlashJob job(b.session,profile,std::make_unique<SimulationKey>());
        auto app=image(1024);app.segments.append({0x20000,hx("1234567890")});auto driver=image(64,0x10000000);
        QJsonArray transcript,frames;QSignalSpy result(&job,&FlashJob::finished),progress(&job,&FlashJob::progress);
        connect(&b.session,&UdsSession::trace,&job,[&](bool tx,const QByteArray &pdu){transcript.append(QJsonObject{{"direction",tx?"TX":"RX"},{"uds",QString::fromLatin1(pdu.toHex(' '))}});});
        connect(&b.network,&CanTransport::trace,&job,[&](bool tx,const CanFrame &f){frames.append(QJsonObject{{"direction",tx?"TX":"RX"},{"id",QString::number(f.id,16)},{"extended",f.extended},{"bytes",QString::fromLatin1(f.data.toHex(' '))}});});
        QString error;QVERIFY(job.start(app,driver,error));QVERIFY(until([&]{return result.size()==1;},10000));
        QVERIFY2(result[0][0].toBool(),qPrintable(result[0][1].toString()));
        for(const auto &s:app.segments)QCOMPARE(b.ecu.uds.memory(s.address),s.data);
        QCOMPARE(b.ecu.uds.memory(driver.segments[0].address),driver.segments[0].data);QCOMPARE(b.ecu.uds.resets,1);QCOMPARE(progress.last()[0].toInt(),100);
        QList<int> expected={0x10,0x27,0x27,0x34,0x36,0x37,0x31,0x31,0x34,0x36,0x36,0x36,0x36,0x37,0x31,0x31,0x34,0x36,0x37,0x31,0x31,0x11,0x22};
        QCOMPARE(b.ecu.uds.requests.size(),expected.size());
        for(int i=0;i<expected.size();++i)QCOMPARE(int(quint8(b.ecu.uds.requests[i][0])),expected[i]);
        QCOMPARE(b.ecu.uds.requests[7],hx("3101ff00440000ff0000000400"));QCOMPARE(b.ecu.uds.requests.last(),hx("22f180"));
        QVERIFY(job.start(app,driver,error));QVERIFY(until([&]{return result.size()==2;},10000));QVERIFY(result[1][0].toBool());QCOMPARE(b.ecu.uds.resets,2);
        QDir().mkpath(QCoreApplication::applicationDirPath()+"/artifacts");
        QFile output(QCoreApplication::applicationDirPath()+QString("/artifacts/can-uds-%1.json").arg(extended?29:11));
        QVERIFY(output.open(QIODevice::WriteOnly));output.write(QJsonDocument(QJsonObject{{"completedRounds",2},{"memoryMatches",true},{"uds",transcript},{"canFrames",frames}}).toJson());
    }
    void udsFaults_data(){
        QTest::addColumn<QString>("fault");
        for(const char *s:{"NRC","pending","bad-echo","bad-crc","bad-length","timeout"})QTest::newRow(s)<<QString(s);
    }
    void udsFaults(){
        QFETCH(QString,fault);CanBench b;FlashProfile p;
        if(fault=="NRC")b.ecu.uds.faults.negativeService=0x34;
        if(fault=="pending"){b.ecu.uds.faults.pendingService=0x31;b.ecu.uds.faults.pendingCount=3;}
        if(fault=="bad-echo")b.ecu.uds.faults.badBlockEcho=true;
        if(fault=="bad-crc")b.ecu.uds.faults.badVerify=true;
        if(fault=="bad-length")b.ecu.uds.faults.badBlockLength=true;
        if(fault=="timeout")b.ecu.uds.faults.dropService=0x36;
        FlashJob job(b.session,p,std::make_unique<SimulationKey>());QSignalSpy done(&job,&FlashJob::finished);QString e;
        QVERIFY(job.start(image(64),{},e));QVERIFY(until([&]{return done.size()==1;},3000));QCOMPARE(done[0][0].toBool(),fault=="pending");
        if(fault!="pending")QCOMPARE(b.ecu.uds.resets,0);
        const int n=b.ecu.uds.requests.size();QTest::qWait(30);QCOMPARE(b.ecu.uds.requests.size(),n);
    }
    void cancellationAndIndependentCanLin(){
        FlashProfile p,q;q.session=3;q.securityLevel=1;CanOptions a,c;c.txId=0x700;c.rxId=0x780;c.blockSize=1;c.stMin=2;
        CanBench first(a,p),second(c,q);SimulatedLinEcu ecu(0x15,q);
        LinTransport lin(0x15,1,100,[&](quint8 id,const QByteArray &bytes,QString &error){
            if(!ecu.write(id,bytes,error))return false;
            if(id==0x3d){auto f=ecu.takeResponseFrame();if(!f.isEmpty())lin.receiveFrame(id,f);}return true;
        });
        UdsSession linSession(lin,{100,5500,60000,0});
        FlashJob x(first.session,p,std::make_unique<SimulationKey>()),y(second.session,q,std::make_unique<SimulationKey>()),z(linSession,q,std::make_unique<SimulationKey>());
        QSignalSpy xd(&x,&FlashJob::finished),yd(&y,&FlashJob::finished),zd(&z,&FlashJob::finished);QString e;
        QVERIFY(x.start(image(4096),{},e));QVERIFY(y.start(image(128),{},e));QVERIFY(z.start(image(64),{},e));
        QVERIFY(until([&]{return first.ecu.uds.requests.size()>4;}));x.cancel();const int n=first.ecu.uds.requests.size();
        QVERIFY(until([&]{return yd.size()==1&&zd.size()==1;},3000));
        QCOMPARE(first.ecu.uds.requests.size(),n);QCOMPARE(xd.size(),0);QVERIFY(yd[0][0].toBool());QVERIFY(zd[0][0].toBool());
        QCOMPARE(second.ecu.uds.requests.first(),hx("1003"));QCOMPARE(ecu.requests.first(),hx("1003"));
        QVERIFY(x.start(image(32),{},e));QVERIFY(until([&]{return xd.size()==1;},3000));QVERIFY(xd[0][0].toBool());
    }
};
QTEST_GUILESS_MAIN(CanProtocolTest)
#include "test_can_protocol.moc"

#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include "protocol/SimulatedLinEcu.h"
using namespace boot;
static QByteArray hex(const char *s){return QByteArray::fromHex(s);}
class ManualTransport : public DiagnosticTransport {
public:
    QList<QByteArray> requests;bool active=false;quint64 generation=0;
    bool send(const QByteArray &b,QString &) override{
        requests.append(b);active=true;const auto g=generation;
        QTimer::singleShot(0,this,[this,g]{if(active&&g==generation)emit sent();});return true;
    }
    void cancel() override{active=false;++generation;}
    int maximumPdu() const override{return 4095;}
    void reply(const QByteArray &b){emit received(b);}
};
struct LinBench {
    SimulatedLinEcu ecu;LinTransport transport;UdsSession session;
    explicit LinBench(quint8 nad=1,FlashProfile p={},SessionOptions timing={100,5500,60000,0})
     :ecu(nad,p),transport(nad,1,60,[this](quint8 id,const QByteArray &bytes,QString &error){
        if(!ecu.write(id,bytes,error))return false;
        if(id==0x3d){const auto f=ecu.takeResponseFrame();if(!f.isEmpty())transport.receiveFrame(id,f);}return true;
      }),session(transport,timing){
        QObject::connect(&transport,&DiagnosticTransport::cancelled,&transport,[this]{ecu.clearWire();});
    }
};
class ProtocolTest : public QObject {
    Q_OBJECT
    QTemporaryDir temp;
    QString file(const QString &name,const QByteArray &bytes){
        const auto path=temp.path()+"/"+name;QFile f(path);if(!f.open(QIODevice::WriteOnly))return {};
        f.write(bytes);return path;
    }
    static QByteArray record(int type,int offset,const QByteArray &data){
        QByteArray b;b+=char(data.size());b+=char(offset>>8);b+=char(offset);b+=char(type);b+=data;
        int sum=0;for(unsigned char c:b)sum+=c;b+=char(-sum);return ":"+b.toHex().toUpper()+"\n";
    }
    static FirmwareImage image(int count=4096,quint32 address=0xff00){
        FirmwareImage i;QByteArray b(count,0);for(int n=0;n<count;++n)b[n]=char(n*17+3);
        i.segments.append({address,b});i.size=count;return i;
    }
private slots:
    void codecFixedVectors(){
        QCOMPARE(LinCodec::encode(1,hex("1002")),QList<QByteArray>{hex("01021002ffffffff")});
        const auto frames=LinCodec::encode(0x15,hex("3400440000ff0000001000"));
        QCOMPARE(frames.size(),2);
        QCOMPARE(frames[0],hex("15100b3400440000"));
        QCOMPARE(frames[1],hex("1521ff0000001000"));
        LinCodec c;QString e;QByteArray p;
        QCOMPARE(c.accept(0x15,hex("15100b3400440000"),p,e),LinCodec::Partial);
        QCOMPARE(c.accept(0x15,hex("1521ff0000001000"),p,e),LinCodec::Complete);
        QCOMPARE(p,hex("3400440000ff0000001000"));
        QCOMPARE(c.accept(1,hex("02025002ffffffff"),p,e),LinCodec::Ignored);
        QCOMPARE(c.accept(1,hex("0107100200000000"),p,e),LinCodec::Invalid);
        QCOMPARE(c.accept(1,hex("0121000000000000"),p,e),LinCodec::Invalid);
        QCOMPARE(c.accept(1,hex("0110070102030405"),p,e),LinCodec::Partial);
        QCOMPARE(c.accept(1,hex("01220607ffffffff"),p,e),LinCodec::Invalid);
        QCOMPARE(c.accept(1,hex("01025002ffffffff"),p,e),LinCodec::Complete);
    }
    void codecBoundariesAndSequenceWrap_data(){
        QTest::addColumn<int>("length");
        for(int n:{1,6,7,11,12,101,4095})QTest::newRow(qPrintable(QString::number(n)))<<n;
    }
    void codecBoundariesAndSequenceWrap(){
        QFETCH(int,length);QByteArray p(length,0);for(int i=0;i<length;++i)p[i]=char(i);
        const auto frames=LinCodec::encode(0x7d,p);QVERIFY(!frames.isEmpty());LinCodec decoder;QByteArray out;QString error;
        for(int i=0;i<frames.size();++i){
            QCOMPARE(frames[i].size(),8);
            if(i>0)QCOMPARE(quint8(frames[i][1]),quint8(0x20|(i&15)));
            QCOMPARE(decoder.accept(0x7d,frames[i],out,error),i==frames.size()-1?LinCodec::Complete:LinCodec::Partial);
        }
        QCOMPARE(out,p);QVERIFY(LinCodec::encode(1,QByteArray(4096,0)).isEmpty());QVERIFY(LinCodec::encode(0,p).isEmpty());
    }
    void firmwareParsesSparseAndExtendedHex(){
        FirmwareImage i;QString error;
        const auto bytes=record(4,0,hex("0001"))+record(0,0x20,hex("aabb"))+record(0,0x22,hex("ccdd"))+
          record(0,0x100,hex("55"))+record(5,0,hex("00010020"))+record(1,0,{});
        QVERIFY2(FirmwareImage::load(file("sparse.hex",bytes),0,i,error),qPrintable(error));
        QCOMPARE(i.segments.size(),2);QCOMPARE(i.size,qint64(5));QCOMPARE(i.segments[0].address,quint32(0x10020));
        QCOMPARE(i.segments[0].data,hex("aabbccdd"));QCOMPARE(i.segments[1].address,quint32(0x10100));QCOMPARE(i.sha256.size(),32);
        const auto seg=record(2,0,hex("1234"))+record(0,0x20,hex("1122"))+record(3,0,hex("12340020"))+record(1,0,{});
        QVERIFY(FirmwareImage::load(file("segment.hex",seg),0,i,error));QCOMPARE(i.segments[0].address,quint32(0x12360));
        QVERIFY(FirmwareImage::load(file("image.bin",hex("010203")),0xfffffffd,i,error));
        QCOMPARE(i.segments[0].address,quint32(0xfffffffd));QCOMPARE(i.size,qint64(3));
        QVERIFY(!FirmwareImage::load(file("overflow.bin",hex("010203")),0xfffffffe,i,error));QVERIFY(i.segments.isEmpty());
        const auto production=record(0,0x20,hex("1122"))+record(1,0,{});
        QByteArray compatible=production;compatible.replace("\n",QByteArray("\r\r\n"));compatible+=QByteArray(":00000008F8\n");
        QVERIFY2(FirmwareImage::load(file("production.hex",compatible),0,i,error),qPrintable(error));QCOMPARE(i.size,qint64(2));
        QCOMPARE(crc32("123456789"),quint32(0xcbf43926));
    }
    void firmwareRejectsMalformed_data(){
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("checksum")<<QByteArray(":010000000100\n:00000001FF\n");
        QTest::newRow("count")<<QByteArray(":0200000001FD\n:00000001FF\n");
        QTest::newRow("nonhex")<<QByteArray(":010000000GFF\n:00000001FF\n");
        QTest::newRow("missing-eof")<<record(0,0,hex("01"));
        QTest::newRow("after-eof")<<(record(1,0,{})+record(0,0,hex("01")));
        QTest::newRow("overlap")<<(record(0,0,hex("0102"))+record(0,1,hex("0203"))+record(1,0,{}));
        QTest::newRow("overflow")<<(record(4,0,hex("ffff"))+record(0,0xffff,hex("0102"))+record(1,0,{}));
        QTest::newRow("no-data")<<record(1,0,{});
        QTest::newRow("bad-type")<<(record(6,0,hex("01"))+record(1,0,{}));
        QTest::newRow("bad-extended")<<(record(4,1,hex("0001"))+record(1,0,{}));
    }
    void firmwareRejectsMalformed(){
        QFETCH(QByteArray,bytes);FirmwareImage i;QString error;
        QVERIFY(!FirmwareImage::load(file("bad.hex",bytes),0,i,error));QVERIFY(!error.isEmpty());QVERIFY(i.segments.isEmpty());
    }
    void profileAndKeyVectors(){
        SimulationKey key;QString error;
        QCOMPARE(key.calculate(0x11,hex("12345678"),error),hex("a680e2cc"));
        QVERIFY(key.calculate(0x11,hex("1234"),error).isEmpty());
        FlashProfile p,q;QVERIFY(FlashProfile::fromJson(p.toJson(),q,error));QCOMPARE(q.toJson(),p.toJson());
        QVERIFY(p.toJson().contains("consecutiveFrameByteLimit"));QVERIFY(!p.toJson().contains("maxPduBytes"));
        QVERIFY(FlashProfile::fromJson(QJsonObject{{"maxPduBytes",128}},q,error));QCOMPARE(q.consecutiveFrameByteLimit,128);
        QVERIFY(FlashProfile::fromJson(QJsonObject{{"blockDataLimit",128}},q,error));QCOMPARE(q.consecutiveFrameByteLimit,130);
        for(const QJsonObject &o:{QJsonObject{{"securityLevel",2}},QJsonObject{{"blockDataLimit",4094}},
             QJsonObject{{"keyProvider","unknown"}},QJsonObject{{"simulationOnly",false}},QJsonObject{{"session",2.5}},
             QJsonObject{{"unknown",1}},QJsonObject{{"eraseRoutine",0xff01}}})QVERIFY(!FlashProfile::fromJson(o,q,error));
    }
    void bootAdvancesOnlyAfterMatchingPositiveResponseAndZeroKey(){
        ManualTransport transport;UdsSession session(transport,{100,100,1000,0});FlashProfile profile;
        profile.flow="boot";profile.resetWaitMs=0;
        for(const auto &step:downloadSteps()){
            profile.negativeResponseChecks[step.id]=true;
            profile.timeoutChecks[step.id]=true;
        }
        FlashJob job(session,profile,std::make_unique<ZeroKey>());QSignalSpy sent(&transport,&DiagnosticTransport::sent);QString error;
        QVERIFY2(job.start(image(8),{},error),qPrintable(error));
        QTRY_COMPARE(sent.size(),1);QCOMPARE(transport.requests.size(),1);QCOMPARE(transport.requests[0],hex("1002"));
        transport.reply(hex("620101"));QTest::qWait(20);QCOMPARE(transport.requests.size(),1);
        transport.reply(hex("5002"));QTRY_COMPARE(sent.size(),2);QCOMPARE(transport.requests.size(),2);QCOMPARE(transport.requests[1],hex("2711"));
        transport.reply(hex("671112345678"));QTRY_COMPARE(sent.size(),3);QCOMPARE(transport.requests.size(),3);
        QCOMPARE(transport.requests[2],hex("271200"));
        job.cancel();
    }
    void sessionPendingEchoAndAbsoluteDeadline(){
        ManualTransport t;UdsSession s(t,{30,45,140,0});bool done=false,ok=false;QString failure;
        QVERIFY(s.request(hex("3101ff00"),hex("7101ff00"),[&](bool success,const QByteArray &,const QString &e){done=true;ok=success;failure=e;}));
        QTRY_COMPARE(t.requests.size(),1);QTest::qWait(1);t.reply(hex("7f3178"));
        QTest::qWait(35);QVERIFY(!done);t.reply(hex("7f3178"));t.reply(hex("7101ff00"));QVERIFY(done&&ok);
        done=false;QVERIFY(s.request(hex("3601aa"),hex("7601"),[&](bool success,const QByteArray &,const QString &e){done=true;ok=success;failure=e;}));
        QTRY_COMPARE(t.requests.size(),2);QTest::qWait(1);t.reply(hex("7602"));QVERIFY(done&&!ok);QVERIFY(failure.contains("echo"));
        done=false;QVERIFY(s.request(hex("3101ff00"),hex("7101ff00"),[&](bool success,const QByteArray &,const QString &e){done=true;ok=success;failure=e;}));
        QTRY_COMPARE(t.requests.size(),3);QTest::qWait(1);
        QTimer pending;pending.setInterval(10);connect(&pending,&QTimer::timeout,&t,[&]{t.reply(hex("7f3178"));});pending.start();
        QTRY_VERIFY_WITH_TIMEOUT(done,500);QVERIFY(!ok);QVERIFY(failure.contains("absolute"));pending.stop();
    }
    void sessionUnrelatedDoesNotExtendAndCancellationDropsQueue(){
        ManualTransport t;UdsSession s(t,{60,100,500,0});int callbacks=0;QString error;
        QVERIFY(s.request(hex("220101"),hex("620101"),[&](bool ok,const QByteArray &,const QString &e){QVERIFY(!ok);++callbacks;error=e;}));
        QTRY_COMPARE(t.requests.size(),1);QTest::qWait(1);
        t.reply(hex("5002003201f4"));t.reply(hex("7f1078"));
        QTRY_COMPARE_WITH_TIMEOUT(callbacks,1,500);QVERIFY(error.contains("timeout"));
        QVERIFY(s.request(hex("1002"),hex("5002"),[&](bool,const QByteArray &,const QString &){++callbacks;}));
        QVERIFY(s.request(hex("220101"),hex("620101"),[&](bool,const QByteArray &,const QString &){++callbacks;}));
        s.cancel();QTest::qWait(30);QCOMPARE(t.requests.size(),1);QCOMPARE(callbacks,1);
        t.reply(hex("5002003201f4"));QCOMPARE(callbacks,1);
    }
    void keepaliveEnabledIdleAndNeverInterleavesPending(){
        ManualTransport t;UdsSession s(t,{80,120,1000,20});bool sessionReady=false;
        QVERIFY(s.request(hex("1002"),hex("5002"),[&](bool ok,const QByteArray &,const QString &){sessionReady=ok;}));
        QTRY_COMPARE(t.requests.size(),1);QTest::qWait(1);t.reply(hex("50020032000a"));QVERIFY(sessionReady);
        bool finished=false;QVERIFY(s.request(hex("3101ff00"),hex("7101ff00"),[&](bool,const QByteArray &,const QString &){finished=true;}));
        QTRY_COMPARE(t.requests.size(),2);QTest::qWait(1);t.reply(hex("7f3178"));QTest::qWait(50);
        QCOMPARE(t.requests.size(),2);QVERIFY(!finished);t.reply(hex("7101ff00"));QVERIFY(finished);
        QTRY_COMPARE_WITH_TIMEOUT(t.requests.size(),3,200);QCOMPARE(t.requests.last(),hex("3e80"));
        s.cancel();const int before=t.requests.size();QTest::qWait(60);QCOMPARE(t.requests.size(),before);
        ManualTransport disabled;UdsSession off(disabled,{80,120,1000,0});
        off.request(hex("1002"),hex("5002"),[](bool,const QByteArray &,const QString &){});
        QTRY_COMPARE(disabled.requests.size(),1);QTest::qWait(1);disabled.reply(hex("50020032000a"));
        QTest::qWait(80);QCOMPARE(disabled.requests.size(),1);
    }
    void linTimeoutAndBadFrame(){
        LinTransport t(1,1,25,[](quint8,const QByteArray &,QString &){return true;});
        QSignalSpy errors(&t,&DiagnosticTransport::failed),sent(&t,&DiagnosticTransport::sent);QString error;
        QVERIFY(t.send(hex("220101"),error));QTRY_COMPARE(sent.size(),1);
        t.receiveFrame(0x3d,hex("0110076201010102"));QTRY_COMPARE_WITH_TIMEOUT(errors.size(),1,200);
        QVERIFY(errors[0][0].toString().contains("N_Cr"));
        QVERIFY(t.send(hex("1002"),error));QTRY_COMPARE(sent.size(),2);
        t.receiveFrame(0x3d,{},true);QCOMPARE(errors.size(),1);
        t.receiveFrame(0x3d,hex("01025002ffffffff"),false,true);QCOMPARE(errors.size(),2);
    }
    void transmitConfirmationAndTimeout(){
        QList<QByteArray> writes;
        LinTransport t(1,1,100,[&](quint8 id,const QByteArray &bytes,QString &){if(id==0x3c)writes.append(bytes);return true;});
        t.requireTransmitConfirmation(true,100);QSignalSpy sent(&t,&DiagnosticTransport::sent),failed(&t,&DiagnosticTransport::failed);QString error;
        QVERIFY(t.send(hex("3400440000ff0000001000"),error));
        QTRY_COMPARE(writes.size(),1);QCOMPARE(sent.size(),0);
        t.confirmTransmitted(hex("0000000000000000"));QCOMPARE(writes.size(),1);
        t.confirmTransmitted(writes[0]);QTRY_COMPARE(writes.size(),2);QCOMPARE(sent.size(),0);
        t.confirmTransmitted(writes[1]);QTRY_COMPARE(sent.size(),1);t.cancel();
        t.requireTransmitConfirmation(true,30);QVERIFY(t.send(hex("1002"),error));
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(),1,300);QVERIFY(failed[0][0].toString().contains("N_As"));
    }
    void segmentedResponseStopsP2OnlyForMatchingService(){
        ManualTransport t;UdsSession s(t,{60,100,300,0});bool done=false,ok=false;
        s.request(hex("22f180"),hex("62f180"),[&](bool success,const QByteArray &,const QString &){done=true;ok=success;});
        QTRY_COMPARE(t.requests.size(),1);QTest::qWait(1);
        emit t.responseStarted(hex("62f1800102"));QTest::qWait(90);QVERIFY(!done);
        t.reply(hex("62f18001020304"));QVERIFY(done&&ok);
        done=false;s.request(hex("22f180"),hex("62f180"),[&](bool success,const QByteArray &,const QString &){done=true;ok=success;});
        QTRY_COMPARE(t.requests.size(),2);QTest::qWait(1);emit t.responseStarted(hex("6300010203"));
        QTRY_VERIFY_WITH_TIMEOUT(done,200);QVERIFY(!ok);
    }
    void fullLinFlashDriverSparseApplicationAndRepeat(){
        FlashProfile profile;LinBench bench(0x15,profile);FlashJob job(bench.session,profile,std::make_unique<SimulationKey>());
        QJsonArray transcript;
        connect(&bench.session,&UdsSession::trace,&job,[&](bool tx,const QByteArray &pdu){
            transcript.append(QJsonObject{{"direction",tx?"TX":"RX"},{"uds",QString::fromLatin1(pdu.toHex(' ')).toUpper()}});
        });
        auto app=image();app.segments.append({0x20000,hex("1234567890")});app.size+=5;
        const auto driver=image(64,0x10000000);QString error;QSignalSpy result(&job,&FlashJob::finished),progress(&job,&FlashJob::progress);
        QVERIFY2(job.start(app,driver,error),qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(result.size(),1,10000);QVERIFY2(result[0][0].toBool(),qPrintable(result[0][1].toString()));
        QCOMPARE(bench.ecu.resets,1);
        for(const auto &s:app.segments)QCOMPARE(bench.ecu.memory(s.address),s.data);
        QCOMPARE(bench.ecu.memory(driver.segments[0].address),driver.segments[0].data);
        QCOMPARE(progress.last()[0].toInt(),100);QVERIFY(bench.ecu.headers>0);
        QCOMPARE(bench.ecu.requests.first(),hex("1002"));QCOMPARE(bench.ecu.requests.last(),hex("22f180"));
        const auto &requests=bench.ecu.requests;
        // Independently assert the required complete service order, including every data block.
        QList<int> order={0x10,0x27,0x27,0x34,0x36,0x37,0x31,0x31,0x34};
        for(int n=0;n<16;++n)order.append(0x36);
        order.append({0x37,0x31,0x31,0x34,0x36,0x37,0x31,0x31,0x11,0x22});
        QCOMPARE(requests.size(),order.size());
        for(int n=0;n<order.size();++n)QCOMPARE(int(quint8(requests[n][0])),order[n]);
        QCOMPARE(requests[3],hex("3400441000000000000040"));
        QCOMPARE(requests[7],hex("3101ff00440000ff0000001000"));
        QCOMPARE(requests[8],hex("3400440000ff0000001000"));
        QCOMPARE(requests[27],hex("3101ff00440002000000000005"));
        QCOMPARE(requests[28],hex("3400440002000000000005"));
        QVERIFY(job.start(app,driver,error));QTRY_COMPARE_WITH_TIMEOUT(result.size(),2,10000);QVERIFY(result[1][0].toBool());QCOMPARE(bench.ecu.resets,2);
        const auto directory=QCoreApplication::applicationDirPath()+"/artifacts";QDir().mkpath(directory);
        QFile flow(directory+"/lin-uds-flow.json");QVERIFY(flow.open(QIODevice::WriteOnly));
        flow.write(QJsonDocument(QJsonObject{{"backend","SimulatedLinEcu"},{"nad",21},{"completedRounds",bench.ecu.resets},{"memoryMatches",true},{"events",transcript}}).toJson());
    }
    void blockCounterWrapAndEcuMaximum(){
        FlashProfile profile;profile.consecutiveFrameByteLimit=102;LinBench bench(1,profile);bench.ecu.maxBlockLength=3;
        FlashJob job(bench.session,profile,std::make_unique<SimulationKey>());QSignalSpy done(&job,&FlashJob::finished);QString error;
        QVERIFY(job.start(image(260),{},error));QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,10000);QVERIFY2(done[0][0].toBool(),qPrintable(done[0][1].toString()));
        QList<QByteArray> blocks;for(const auto &r:bench.ecu.requests)if(quint8(r[0])==0x36)blocks.append(r);
        QCOMPARE(blocks.size(),260);QCOMPARE(quint8(blocks[254][1]),quint8(255));QCOMPARE(quint8(blocks[255][1]),quint8(0));QCOMPARE(quint8(blocks[256][1]),quint8(1));
        for(const auto &r:blocks)QCOMPARE(r.size(),3);
    }
    void faultInjection_data(){
        QTest::addColumn<QString>("fault");
        for(const char *s:{"timeout","negative","echo","length","verify","sequence","pending"})QTest::newRow(s)<<QString(s);
    }
    void faultInjection(){
        QFETCH(QString,fault);LinBench b;FlashProfile p;FlashJob job(b.session,p,std::make_unique<SimulationKey>());
        if(fault=="timeout")b.ecu.faults.dropService=0x36;
        if(fault=="negative"){b.ecu.faults.negativeService=0x34;b.ecu.faults.negativeCode=0x31;}
        if(fault=="echo")b.ecu.faults.badBlockEcho=true;
        if(fault=="length")b.ecu.faults.badBlockLength=true;
        if(fault=="verify")b.ecu.faults.badVerify=true;
        if(fault=="sequence")b.ecu.faults.badResponseSequence=true;
        if(fault=="pending"){b.ecu.faults.pendingService=0x31;b.ecu.faults.pendingCount=3;}
        QSignalSpy result(&job,&FlashJob::finished),progress(&job,&FlashJob::progress);QString error;
        QVERIFY(job.start(image(48),{},error));QTRY_COMPARE_WITH_TIMEOUT(result.size(),1,3000);
        QCOMPARE(result[0][0].toBool(),fault=="pending");
        if(fault!="pending"){
            QVERIFY(!result[0][1].toString().isEmpty());QVERIFY(progress.last()[0].toInt()<100);
            if(fault!="sequence")QCOMPARE(b.ecu.resets,0); // Corrupt segmented post-reset DID is detected after reset.
            const int n=b.ecu.requests.size();QTest::qWait(50);QCOMPARE(b.ecu.requests.size(),n);
        }
    }
    void cancelMidTransferAndRestart(){
        LinBench b;FlashProfile p;FlashJob job(b.session,p,std::make_unique<SimulationKey>());QString error;
        QSignalSpy result(&job,&FlashJob::finished);
        QVERIFY(job.start(image(4096),{},error));QTRY_VERIFY_WITH_TIMEOUT(b.ecu.requests.size()>=5,1000);
        job.cancel();const int n=b.ecu.requests.size();QTest::qWait(50);QCOMPARE(b.ecu.requests.size(),n);QCOMPARE(result.size(),0);QCOMPARE(b.ecu.resets,0);
        QVERIFY(job.start(image(32),{},error));QTRY_COMPARE_WITH_TIMEOUT(result.size(),1,3000);QVERIFY(result[0][0].toBool());
    }
    void independentLinChannels(){
        FlashProfile a,b;b.session=3;b.securityLevel=1;b.eraseRoutine=0xfe00;b.verifyRoutine=0xfe01;b.dependencyRoutine=0xfe02;
        LinBench first(1,a),second(0x15,b);FlashJob x(first.session,a,std::make_unique<SimulationKey>()),y(second.session,b,std::make_unique<SimulationKey>());
        QSignalSpy xd(&x,&FlashJob::finished),yd(&y,&FlashJob::finished);QString error;
        auto ai=image(100),bi=image(200);QVERIFY(x.start(ai,{},error));QVERIFY(y.start(bi,{},error));
        QTRY_COMPARE_WITH_TIMEOUT(xd.size(),1,3000);QTRY_COMPARE_WITH_TIMEOUT(yd.size(),1,3000);
        QVERIFY(xd[0][0].toBool());QVERIFY(yd[0][0].toBool());QCOMPARE(first.ecu.memory(0xff00),ai.segments[0].data);QCOMPARE(second.ecu.memory(0xff00),bi.segments[0].data);
        QCOMPARE(first.ecu.requests.first(),hex("1002"));QCOMPARE(second.ecu.requests.first(),hex("1003"));
    }
};
QTEST_GUILESS_MAIN(ProtocolTest)
#include "test_protocol.moc"

#include <QtTest>
#include <QQueue>
#include <cstring>
#include "communication/SoftwareChannel.h"
using namespace communication;

class FakeCanApi : public PCANBasicClass {
public:
    FakeCanApi():PCANBasicClass(false){}
    bool isLoaded() const override {return true;}
    int opens=0,resets=0,releases=0,reads=0;
    QVector<TPCANHandle> handles={PCAN_USBBUS1};
    QHash<quint32,int> openedPorts,resetPorts,releasedPorts;
    TPCANStatus initializeResult=PCAN_ERROR_OK,resetResult=PCAN_ERROR_OK,filterResult=PCAN_ERROR_OK;
    QQueue<TPCANMsg> frames;
    TPCANMsg lastWrite={};int writes=0;
    TPCANStatus Write(TPCANHandle,TPCANMsg *message) override {++writes;lastWrite=*message;return PCAN_ERROR_OK;}
    TPCANStatus Initialize(TPCANHandle h,TPCANBaudrate,TPCANType,DWORD,WORD) override {++openedPorts[h];++opens;return initializeResult;}
    TPCANStatus Reset(TPCANHandle h) override {++resetPorts[h];++resets;return resetResult;}
    TPCANStatus Uninitialize(TPCANHandle h) override {++releasedPorts[h];++releases;return PCAN_ERROR_OK;}
    TPCANStatus SetValue(TPCANHandle,TPCANParameter,void*,DWORD) override {return filterResult;}
    TPCANStatus GetValue(TPCANHandle,TPCANParameter parameter,void *data,DWORD) override {
        if (parameter==PCAN_ATTACHED_CHANNELS_COUNT) *static_cast<DWORD*>(data)=handles.size();
        else if (parameter==PCAN_ATTACHED_CHANNELS) {
            for(int i=0;i<handles.size();++i){
                auto &h=static_cast<TPCANChannelInformation*>(data)[i];h={};
                h.channel_handle=handles[i];h.channel_condition=PCAN_CHANNEL_AVAILABLE;h.controller_number=i;
                h.device_type=PCAN_USB;h.device_id=11;std::strcpy(h.device_name,"Fake PCAN");
            }
        }
        return PCAN_ERROR_OK;
    }
    TPCANStatus Read(TPCANHandle,TPCANMsg *message,TPCANTimestamp *stamp) override {
        ++reads;if(frames.isEmpty())return PCAN_ERROR_QRCVEMPTY;
        *message=frames.dequeue();if(stamp)*stamp={};return PCAN_ERROR_OK;
    }
    TPCANStatus GetStatus(TPCANHandle) override {return PCAN_ERROR_BUSOFF;}
    TPCANStatus GetErrorText(TPCANStatus,WORD,LPSTR buffer) override {std::strcpy(buffer,"fake");return PCAN_ERROR_OK;}
};
class FakeLinApi : public PLinApiClass {
public:
    FakeLinApi():PLinApiClass(false){}
    bool isLoaded() const override {return true;}
    QVector<HLINHW> handles={21};
    QHash<quint32,int> initializedPorts,resetPorts,disconnectedPorts;
    int registers=0,connects=0,initializes=0,disconnects=0,removes=0,reads=0;
    bool occupied=false;
    TLINError initResult=errOK,readResult=errOK,disconnectResult=errOK,filterResult=errOK;
    quint64 filter=0;
    TLINHardwareState busState=hwsActive;
    QQueue<TLINRcvMsg> frames;
    TLINMsg lastWrite={};int writes=0,checksumCalls=0;
    TLINError Write(HLINCLIENT,HLINHW,TLINMsg *message) override {++writes;lastWrite=*message;return errOK;}
    TLINError GetPID(BYTE *id) override {if(*id==0x3d)*id=0x7d;return errOK;}
    TLINError CalculateChecksum(TLINMsg *message) override {++checksumCalls;message->Checksum=0xa5;return errOK;}
    TLINError GetAvailableHardware(HLINHW *out,WORD bytes,int *count) override {
        *count=handles.size();
        if(!out)return errOK;
        if(bytes<handles.size()*sizeof(HLINHW))return errBufferInsufficient;
        for(int i=0;i<handles.size();++i)out[i]=handles[i];return errOK;
    }
    TLINError GetHardwareParam(HLINHW hw,TLINHardwareParam p,void *out,WORD bytes) override {
        if(!handles.contains(hw))return errIllegalHardware;
        if(p==hwpName){std::strncpy(static_cast<char*>(out),"Fake PLIN",bytes);return errOK;}
        if(p==hwpConnectedClients){
            std::memset(out,0,bytes);if(occupied)static_cast<BYTE*>(out)[0]=3;return errOK;
        }
        int value=p==hwpChannelNumber?handles.indexOf(hw):p==hwpSerialNumber?1234:(p==hwpDeviceNumber?1:(p==hwpScheduleActive?-1:0));
        std::memcpy(out,&value,sizeof(value));return errOK;
    }
    TLINError RegisterClient(LPSTR,DWORD,HLINCLIENT *client) override {++registers;*client=42;return errOK;}
    TLINError ConnectClient(HLINCLIENT,HLINHW) override {++connects;return errOK;}
    TLINError InitializeHardware(HLINCLIENT,HLINHW h,TLINHardwareMode,WORD) override {++initializedPorts[h];++initializes;return initResult;}
    TLINError SetClientFilter(HLINCLIENT,HLINHW,uint64 mask) override {filter=mask;return filterResult;}
    TLINError SetFrameEntry(HLINCLIENT,HLINHW,TLINFrameEntry*) override {return errOK;}
    TLINError ResetHardwareConfig(HLINCLIENT,HLINHW h) override {++resetPorts[h];return errOK;}
    TLINError DisconnectClient(HLINCLIENT,HLINHW h) override {++disconnectedPorts[h];++disconnects;return disconnectResult;}
    TLINError RemoveClient(HLINCLIENT) override {++removes;return errOK;}
    TLINError ResetClient(HLINCLIENT) override {return errOK;}
    TLINError ReadMulti(HLINCLIENT,TLINRcvMsg *out,int capacity,int *count) override {
        ++reads;*count=0;if(readResult!=errOK)return readResult;
        while(!frames.isEmpty() && *count<capacity){out[*count]=frames.dequeue();++*count;}
        return *count?errOK:errRcvQueueEmpty;
    }
    TLINError GetStatus(HLINHW hw,TLINHardwareStatus *out) override {
        if(!handles.contains(hw))return errIllegalHardware;
        *out={};out->Mode=modMaster;out->Status=busState;return errOK;
    }
    TLINError GetErrorText(TLINError,BYTE,LPSTR text,WORD) override {std::strcpy(text,"fake");return errOK;}
};
class FakeBackend : public HardwareBackend {
public:
    Bus bus() const override {return Bus::Lin;}
    HardwareChannels items;
    bool failOpen=false,failScan=false,failClose=false;
    int opened=0,closed=0;
    quint32 lastHandle=0;
    Health currentHealth=Health::Ready;
    HardwareChannels scan(QString &error) override {error=failScan?"scan failed":"";return items;}
    bool open(const HardwareChannel &h,const SoftwareChannelConfiguration &,QString &error) override {
        ++opened;lastHandle=h.handle;error=failOpen?"open failed":"";return !failOpen;
    }
    bool close(QString &error) override {++closed;error=failClose?"close failed":"";return !failClose;}
    Health health(QString &detail) override {detail="fake bus state";return currentHealth;}
};
static HardwareChannel hw(quint32 handle=21,QString key="lin:1234:0") {
    HardwareChannel h;h.bus=Bus::Lin;h.handle=handle;h.key=key;h.label="PLIN";h.persistentIdentity=true;return h;
}
static SoftwareChannelConfiguration cfg(QString id="LIN-A") {
    SoftwareChannelConfiguration c;c.softwareId=id;c.bus=Bus::Lin;c.bitrate=19200;c.uds.profileId=id;return c;
}
class CommunicationTest : public QObject {
    Q_OBJECT
private slots:
    void multiPortLinDoesNotReinitializeActiveSibling() {
        FakeLinApi api;api.handles={21,22};
        tstPeakLin a(nullptr,&api),b(nullptr,&api);
        auto ca=cfg("LIN01"),cb=cfg("LIN02");ca.preferredHandle=21;cb.preferredHandle=22;
        SoftwareChannel one(ca,std::unique_ptr<HardwareBackend>(new PeakLinBackend(a)));
        SoftwareChannel two(cb,std::unique_ptr<HardwareBackend>(new PeakLinBackend(b)));
        QVERIFY(one.connectChannel());QCOMPARE(api.initializedPorts[21],1);
        two.poll();QCOMPARE(two.hardware().size(),2);QVERIFY(!two.hardware()[0].available);QVERIFY(two.hardware()[1].available);
        QVERIFY(two.connectChannel());QCOMPARE(api.initializedPorts[21],1);QCOMPARE(api.initializedPorts[22],1);
        for(int i=0;i<10;++i){one.poll();two.poll();QVERIFY(one.connectChannel());QVERIFY(two.connectChannel());}
        QCOMPARE(api.initializedPorts[21],1);QCOMPARE(api.initializedPorts[22],1);QCOMPARE(api.resetPorts[21],0);
        QVERIFY(two.disconnectChannel());QCOMPARE(api.resetPorts[21],0);QCOMPARE(api.disconnectedPorts[21],0);
        QCOMPARE(one.state(),ConnectionState::Connected);QVERIFY(a.isOpen());
        QVERIFY(two.connectChannel());QCOMPARE(api.initializedPorts[21],1);QCOMPARE(api.initializedPorts[22],2);
        QVERIFY(one.disconnectChannel());QCOMPARE(two.state(),ConnectionState::Connected);QVERIFY(b.isOpen());
    }
    void multiPortCanDoesNotReinitializeActiveSibling() {
        FakeCanApi api;api.handles={PCAN_USBBUS1,PCAN_USBBUS2};
        tstPeakCan a(nullptr,&api),b(nullptr,&api);
        SoftwareChannelConfiguration ca;ca.softwareId="CAN01";ca.bus=Bus::Can;ca.bitrate=500000;ca.uds.profileId="CAN A";ca.preferredHandle=PCAN_USBBUS1;
        auto cb=ca;cb.softwareId="CAN02";cb.preferredHandle=PCAN_USBBUS2;
        SoftwareChannel one(ca,std::unique_ptr<HardwareBackend>(new PeakCanBackend(a)));
        SoftwareChannel two(cb,std::unique_ptr<HardwareBackend>(new PeakCanBackend(b)));
        QVERIFY(one.connectChannel());two.poll();QVERIFY(!two.hardware()[0].available);QVERIFY(two.hardware()[1].available);
        QVERIFY(two.connectChannel());
        for(int i=0;i<10;++i){one.poll();two.poll();QVERIFY(one.connectChannel());QVERIFY(two.connectChannel());}
        QCOMPARE(api.openedPorts[PCAN_USBBUS1],1);QCOMPARE(api.openedPorts[PCAN_USBBUS2],1);
        QVERIFY(two.disconnectChannel());QCOMPARE(api.resetPorts[PCAN_USBBUS1],0);QCOMPARE(api.releasedPorts[PCAN_USBBUS1],0);
        QCOMPARE(one.state(),ConnectionState::Connected);QVERIFY(a.isOpen());
        QVERIFY(two.connectChannel());QCOMPARE(api.openedPorts[PCAN_USBBUS1],1);QCOMPARE(api.openedPorts[PCAN_USBBUS2],2);
    }
    void rawCanSupportsExtendedIdsAndRejectsInvalidFrames() {
        FakeCanApi api;tstPeakCan driver(nullptr,&api);QVERIFY(driver.startDevice());
        TPCANMsg msg={};msg.ID=0x18daf110;msg.MSGTYPE=PCAN_MESSAGE_EXTENDED;msg.LEN=8;
        QVERIFY(driver.sendRaw(msg));QCOMPARE(api.lastWrite.ID,DWORD(0x18daf110));QCOMPARE(api.writes,1);
        msg.MSGTYPE=PCAN_MESSAGE_STANDARD;QVERIFY(!driver.sendRaw(msg));
        msg.ID=0x123;msg.LEN=9;QVERIFY(!driver.sendRaw(msg));QCOMPARE(api.writes,1);
    }
    void rawLinDiagnosticUsesClassicChecksumAndProtectedId() {
        FakeLinApi api;tstPeakLin driver(nullptr,&api);driver.setHardwareHandle(21);driver.setDevMode(modMaster);driver.setDevBaudrate(19200);
        QVERIFY(driver.startDevice());
        TLINMsg msg={};msg.FrameId=0x3d;msg.Length=8;msg.Direction=dirSubscriber;msg.ChecksumType=cstEnhanced;
        QVERIFY(driver.sendRaw(msg));QCOMPARE(api.lastWrite.FrameId,BYTE(0x7d));QCOMPARE(api.lastWrite.ChecksumType,TLINChecksumType(cstClassic));
        msg.Length=9;QVERIFY(!driver.sendRaw(msg));
        msg.Length=8;msg.FrameId=0x7d;QVERIFY(!driver.sendRaw(msg));QCOMPARE(api.writes,1);
    }
    void rawLinHeaderScanNeverPublishesData() {
        FakeLinApi api;tstPeakLin driver(nullptr,&api);
        driver.setHardwareHandle(21);driver.setDevMode(modMaster);driver.setDevBaudrate(19200);
        QVERIFY(driver.startDevice());
        TLINMsg msg={};msg.FrameId=0x20;msg.Length=8;
        msg.Direction=dirSubscriberAutoLength;msg.ChecksumType=cstAuto;
        QVERIFY(driver.sendRaw(msg));QCOMPARE(api.lastWrite.Direction,TLINDirection(dirSubscriberAutoLength));
        QCOMPARE(api.lastWrite.ChecksumType,TLINChecksumType(cstAuto));QCOMPARE(api.checksumCalls,0);
        msg.FrameId=0x3d;QVERIFY(driver.sendRaw(msg));
        QCOMPARE(api.lastWrite.FrameId,BYTE(0x7d));QCOMPARE(api.lastWrite.ChecksumType,TLINChecksumType(cstClassic));
        QCOMPARE(api.checksumCalls,0);
        msg.FrameId=0x20;msg.Direction=dirPublisher;msg.ChecksumType=cstEnhanced;
        QVERIFY(driver.sendRaw(msg));QCOMPARE(api.checksumCalls,1);
        msg.Direction=dirDisabled;QVERIFY(!driver.sendRaw(msg));QCOMPARE(api.writes,3);
    }
    void canEmptyQueueAndCapacity() {
        FakeCanApi api;tstPeakCan d(nullptr,&api);QVERIFY(d.startDevice());
        StrtCanBuf output[2]={};output[1].id=0xdead;
        QCOMPARE(d.recvMsg(1,output),DWORD(0));
        QCOMPARE(api.reads,1);
        TPCANMsg frame={};frame.ID=0x123;frame.LEN=1;frame.DATA[0]=0xaa;
        api.frames.enqueue(frame);api.frames.enqueue(frame);
        QCOMPARE(d.recvMsg(1,output),DWORD(1));
        QCOMPARE(output[0].data[0],BYTE(0xaa));QCOMPARE(output[1].id,DWORD(0xdead));
        QCOMPARE(api.frames.size(),1);
    }
    void canReleaseAfterResetFailure() {
        FakeCanApi api;tstPeakCan d(nullptr,&api);QVERIFY(d.startDevice());
        api.resetResult=PCAN_ERROR_ILLHW;
        QVERIFY(!d.stopDevice());QCOMPARE(api.releases,1);QVERIFY(!d.isOpen());
        QVERIFY(d.stopDevice());QCOMPARE(api.releases,1);
    }
    void canInitRollbackAndBusOff() {
        FakeCanApi api;tstPeakCan d(nullptr,&api);
        api.filterResult=PCAN_ERROR_ILLPARAMVAL;QVERIFY(!d.startDevice());QCOMPARE(api.releases,1);
        api.filterResult=PCAN_ERROR_OK;QVERIFY(d.startDevice());
        QCOMPARE(d.hardwareHealth(),Health::BusWarning);QVERIFY(d.isDeviceActive());
        StrtCanBuf bad={};bad.len=9;QVERIFY(!d.sendMsg(bad));
    }
    void linEnumerationAndRepeatedInitialization() {
        FakeLinApi api;tstPeakLin d(nullptr,&api);
        QCOMPARE(d.availableHardware().size(),1);
        QVERIFY(d.setHardwareHandle(21));QVERIFY(d.setDevMode(modMaster));QVERIFY(d.setDevBaudrate(19200));
        for(int i=0;i<20;++i) {QVERIFY(d.startDevice());QVERIFY(d.startDevice());QVERIFY(d.stopDevice());QVERIFY(d.stopDevice());}
        QCOMPARE(api.registers,20);QCOMPARE(api.initializes,20);QCOMPARE(api.removes,20);
        api.handles={31};QVERIFY(d.setHardwareHandle(31));QVERIFY(d.startDevice());QCOMPARE(api.initializes,21);
    }
    void linFailedOpenAndRemovedClose() {
        FakeLinApi api;tstPeakLin d(nullptr,&api);d.setHardwareHandle(21);d.setDevMode(modMaster);d.setDevBaudrate(19200);
        api.initResult=errIllegalHardwareState;QVERIFY(!d.startDevice());QCOMPARE(api.disconnects,1);QCOMPARE(api.removes,1);
        api.initResult=errOK;QVERIFY(d.startDevice());
        api.handles.clear();api.disconnectResult=errIllegalHardware;
        QVERIFY(!d.stopDevice());QCOMPARE(api.removes,2);QVERIFY(!d.isOpen());
    }
    void linReceiveFilterAndRollback() {
        FakeLinApi api;tstPeakLin d(nullptr,&api);d.setHardwareHandle(21);d.setDevMode(modMaster);d.setDevBaudrate(19200);
        api.filterResult=errIllegalClient;QVERIFY(!d.startDevice());QCOMPARE(api.removes,1);
        api.filterResult=errOK;QVERIFY(d.startDevice());QCOMPARE(api.filter,quint64(0xffffffffffffffffULL));
    }
    void channelScanFailureClearsStaleList() {
        auto *backend=new FakeBackend;backend->items={hw()};
        SoftwareChannel s(cfg(),std::unique_ptr<HardwareBackend>(backend));
        QSignalSpy hardware(&s,&SoftwareChannel::hardwareChanged);
        s.poll();QCOMPARE(s.hardware().size(),1);
        backend->failScan=true;s.poll();QCOMPARE(s.hardware().size(),1);
        s.poll();QVERIFY(s.hardware().isEmpty());QCOMPARE(hardware.count(),2);
        QCOMPARE(s.state(),ConnectionState::Fault);QCOMPARE(s.error(),QString("scan failed"));
    }
    void linOccupiedChannelAndSleepingBus() {
        FakeLinApi api;tstPeakLin d(nullptr,&api);d.setHardwareHandle(21);d.setDevMode(modMaster);d.setDevBaudrate(19200);
        api.occupied=true;QVERIFY(!d.startDevice());QCOMPARE(api.registers,0);
        api.occupied=false;QVERIFY(d.startDevice());
        api.busState=hwsSleep;QCOMPARE(d.hardwareHealth(),Health::Sleeping);QVERIFY(d.isDeviceActive());
    }
    void linReadPreservesFramesAndStopsOnErrors() {
        FakeLinApi api;tstPeakLin d(nullptr,&api);d.setHardwareHandle(21);d.setDevMode(modMaster);d.setDevBaudrate(19200);
        QVERIFY(d.startDevice());TLINRcvMsg msg={};msg.Type=mstStandard;msg.FrameId=0x3d;msg.Length=8;msg.Data[0]=0x12;
        api.frames.enqueue(msg);msg.Data[0]=0x34;api.frames.enqueue(msg);
        StrtLinSchdRxBuf out={};QCOMPARE(d.recvMsg(2,out),DWORD(2));
        QCOMPARE(out.data[0][0],BYTE(0x12));QCOMPARE(out.data[1][0],BYTE(0x34));QCOMPARE(api.reads,1);
        QCOMPARE(d.recvMsg(2,out),DWORD(0));QCOMPARE(out.slotlen,DWORD(0));
        api.readResult=errIllegalClient;QCOMPARE(d.recvMsg(255,out),DWORD(0));QCOMPARE(api.reads,3);
    }
    void channelReplugUsesIdentityAndNewHandle() {
        auto *backend=new FakeBackend;backend->items={hw()};
        SoftwareChannel session(cfg(),std::unique_ptr<HardwareBackend>(backend));
        QSignalSpy closing(&session,&SoftwareChannel::connectionClosing);
        QVERIFY(session.connectChannel());auto generation=session.generation();
        backend->items.clear();session.poll();
        QCOMPARE(session.state(),ConnectionState::Missing);QCOMPARE(closing.count(),1);
        QVERIFY(session.generation()>generation);QCOMPARE(backend->closed,1);
        backend->items={hw(31)};session.poll();QVERIFY(session.connectChannel());QCOMPARE(backend->lastHandle,quint32(31));
    }
    void channelFailureRollbackAndConfigurationIsolation() {
        auto *a=new FakeBackend;a->items={hw()};a->failOpen=true;
        auto ca=cfg("LIN-A");ca.uds.p2Ms=500;
        SoftwareChannel one(ca,std::unique_ptr<HardwareBackend>(a));
        QVERIFY(!one.connectChannel());QCOMPARE(a->closed,1);
        auto *b=new FakeBackend;b->items={hw(22,"lin:5678:0")};
        SoftwareChannel two(cfg("LIN-B"),std::unique_ptr<HardwareBackend>(b));
        QCOMPARE(one.configuration().uds.p2Ms,500);QCOMPARE(two.configuration().uds.p2Ms,1000);
        QVERIFY(two.connectChannel());
        auto changed=two.configuration();changed.uds.p2Ms=5;
        QVERIFY(!two.configure(changed));QCOMPARE(two.configuration().uds.p2Ms,1000);
    }
    void channelLeaseAndScanFailure() {
        auto *a=new FakeBackend;a->items={hw()};
        SoftwareChannel one(cfg("LIN-A"),std::unique_ptr<HardwareBackend>(a));
        auto *b=new FakeBackend;b->items={hw()};
        SoftwareChannel two(cfg("LIN-B"),std::unique_ptr<HardwareBackend>(b));
        QVERIFY(one.connectChannel());QVERIFY(!two.connectChannel());QCOMPARE(b->opened,0);
        a->failScan=true;one.poll();QCOMPARE(one.state(),ConnectionState::Connected);
        one.poll();QCOMPARE(one.state(),ConnectionState::Fault);
        QVERIFY(two.connectChannel());
    }
    void channelAmbiguousAndDifferentDevice() {
        auto *b=new FakeBackend;b->items={hw(),hw(22,"lin:other:0")};
        SoftwareChannel s(cfg(),std::unique_ptr<HardwareBackend>(b));
        QVERIFY(!s.connectChannel());QCOMPARE(b->opened,0);
        auto c=s.configuration();c.preferredHandle=21;QVERIFY(s.configure(c));QVERIFY(s.connectChannel());
        b->items={hw(21,"lin:replacement:0")};s.poll();QVERIFY(s.state()!=ConnectionState::Connected);
        QVERIFY(!s.connectChannel());QCOMPARE(b->opened,1);
    }
    void missingPersistentIdentityRequiresManualReconnect() {
        auto *b=new FakeBackend;auto h=hw();h.persistentIdentity=false;b->items={h};
        auto c=cfg();c.autoReconnect=true;SoftwareChannel s(c,std::unique_ptr<HardwareBackend>(b));
        QVERIFY(s.connectChannel());b->items.clear();s.poll();h.handle=31;b->items={h};
        QTest::qWait(1050);s.poll();QCOMPARE(s.state(),ConnectionState::Available);QCOMPARE(b->opened,1);
        QVERIFY(s.connectChannel());QCOMPARE(b->lastHandle,quint32(31));QCOMPARE(b->opened,2);
    }
    void channelAutoReconnectAndManualDisconnect() {
        auto *b=new FakeBackend;b->items={hw()};
        auto c=cfg();c.autoReconnect=true;
        SoftwareChannel s(c,std::unique_ptr<HardwareBackend>(b));
        QVERIFY(s.connectChannel());b->items.clear();s.poll();b->items={hw(30)};
        QTest::qWait(1050);s.poll();QCOMPARE(s.state(),ConnectionState::Connected);
        QCOMPARE(b->lastHandle,quint32(30));
        QVERIFY(s.disconnectChannel());QTest::qWait(1050);s.poll();QCOMPARE(b->opened,2);
        QCOMPARE(s.state(),ConnectionState::Available);
    }
};
QTEST_GUILESS_MAIN(CommunicationTest)
#include "test_communication.moc"

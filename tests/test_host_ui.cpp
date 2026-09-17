#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QPushButton>
#include <QComboBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QTabWidget>
#include <QTableView>
#include <QSortFilterProxyModel>
#include <QToolButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QApplication>
#include <QScreen>
#include <QDialog>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QtMath>
#include <QTabBar>
#include <QMenu>
#include <QAction>
#include <QPointer>
#include <QLabel>
#include <QStatusBar>
#include "views/MainWindow.h"
using namespace host;
using communication::Bus;
class HostUiTest : public QObject {
    Q_OBJECT
    QTemporaryDir m_temp;
    QString artifactDir() const {
        const QString path=qEnvironmentVariable("HOST_ARTIFACT_DIR",QCoreApplication::applicationDirPath()+"/artifacts");
        QDir().mkpath(path);return path;
    }
    QString fixture(const QString &name="application.bin",int size=4096) {
        const QString path=m_temp.path()+"/"+name;
        QFile file(path);if(!file.open(QIODevice::WriteOnly))return {};
        file.write(QByteArray(size,char(0x5a)));file.close();return path;
    }
    ChannelSettings preview(Bus bus) {
        auto s=ChannelSettings::defaults(bus);s.simulation=true;s.applicationPath=fixture();
        s.flashPath=fixture(bus==Bus::Can?"can-driver.bin":"lin-driver.bin",64);
        s.hardwareKey=bus==Bus::Can?"preview:CAN:1":"preview:LIN:1";s.handle=bus==Bus::Can?0xf101:0xf201;
        s.p2Ms=30;s.p2StarMs=60;
        return s;
    }
private slots:
    void initTestCase(){
        QVERIFY(m_temp.isValid());QApplication::setStyle("Fusion");QApplication::setFont(QFont("Microsoft YaHei UI",9));
        QDir().mkpath(QCoreApplication::applicationDirPath()+"/artifacts");
    }
    void independentProfilesAndInvalidParameters() {
        auto can=preview(Bus::Can),lin=preview(Bus::Lin);
        lin.p2Ms=800;lin.nad="15";lin.profileId="LIN target B";
        communication::SoftwareChannelConfiguration a,b;QString error;
        QVERIFY(can.toConfiguration(a,error));QVERIFY(lin.toConfiguration(b,error));
        QCOMPARE(a.uds.p2Ms,30);QCOMPARE(b.uds.p2Ms,800);QCOMPARE(b.transport.nad,quint8(0x15));
        lin.nad="7F";QVERIFY(!lin.toConfiguration(b,error));QVERIFY(!error.isEmpty());
        can.requestId="XYZ";QVERIFY(!can.toConfiguration(a,error));
        can.requestId="18DAF110";QVERIFY(!can.toConfiguration(a,error));
        can.extendedId=true;QVERIFY(can.toConfiguration(a,error));
        can.securityLevel=0x12;QVERIFY(!can.toConfiguration(a,error));
        can.securityLevel=0x11;can.applicationAddress="100000000";QVERIFY(!can.toConfiguration(a,error));
    }
    void settingsRoundTripAndFailedSavePreservesFile() {
        auto can=preview(Bus::Can),lin=preview(Bus::Lin);lin.p2Ms=789;lin.profileId="LIN 独立配置";lin.testerPresentEnabled=true;lin.testerPresentMs=1750;
        SettingsStore store(m_temp.path()+"/config/channels.json");QString error;
        QVERIFY(store.save({can,lin},error));QVector<ChannelSettings> read;
        QVERIFY(store.load(read,error));QCOMPARE(read.size(),2);QCOMPARE(read[1].p2Ms,789);
        QVERIFY(read[1].testerPresentEnabled);QCOMPARE(read[1].testerPresentMs,1750);
        QCOMPARE(read[0].p2Ms,30);QCOMPARE(read[1].profileId,QString("LIN 独立配置"));
        QFile f(m_temp.path()+"/config/channels.json");QVERIFY(f.open(QIODevice::ReadOnly));auto before=f.readAll();f.close();
        lin.nad="GG";QVERIFY(!store.save({can,lin},error));
        QVERIFY(f.open(QIODevice::ReadOnly));QCOMPARE(f.readAll(),before);f.close();
        QVERIFY(f.open(QIODevice::WriteOnly|QIODevice::Truncate));f.write("{broken");f.close();
        const auto prior=read;QVERIFY(!store.load(read,error));QCOMPARE(read[1].p2Ms,prior[1].p2Ms);
    }
    void relativeImagePathsSurviveRelocationAndDifferentCwd(){
        const auto folder=m_temp.path()+"/portable";QVERIFY(QDir().mkpath(folder+"/fixtures"));
        QFile image(folder+"/fixtures/app.bin");QVERIFY(image.open(QIODevice::WriteOnly));image.write("test image");image.close();
        auto settings=preview(Bus::Lin);settings.applicationPath=folder+"/fixtures/app.bin";settings.flashPath.clear();
        SettingsStore store(folder+"/channels.json");QString error;QVERIFY(store.save({settings},error));
        QFile json(folder+"/channels.json");QVERIFY(json.open(QIODevice::ReadOnly));
        QVERIFY(json.readAll().contains("fixtures/app.bin"));json.close();
        const auto moved=m_temp.path()+"/移动目录 with spaces";QVERIFY(QDir().rename(folder,moved));
        const auto old=QDir::currentPath();QVERIFY(QDir::setCurrent(m_temp.path()));
        QVector<ChannelSettings> loaded;const bool ok=SettingsStore(moved+"/channels.json").load(loaded,error);
        QDir::setCurrent(old);QVERIFY2(ok,qPrintable(error));QCOMPARE(loaded.size(),1);
        QCOMPARE(loaded[0].applicationPath,moved+"/fixtures/app.bin");QVERIFY(imageReady(loaded[0].applicationPath));
        QVERIFY(loaded[0].flashPath.isEmpty());
        loaded[0].applicationPath=fixture("external.bin",32);QVERIFY(SettingsStore(moved+"/channels.json").save(loaded,error));
        QVector<ChannelSettings> roundTrip;QVERIFY(SettingsStore(moved+"/channels.json").load(roundTrip,error));
        QCOMPARE(roundTrip[0].applicationPath,loaded[0].applicationPath);
    }
    void imageSelectionCancelAndDirectoryCandidates() {
        QTemporaryDir images;QVERIFY(images.isValid());
        for(const QString &name:{"one.bin","two.HEX","ignored.txt","empty.bin"}) {
            QFile f(images.path()+"/"+name);QVERIFY(f.open(QIODevice::WriteOnly));if(name!="empty.bin")f.write("sample");
        }
        const auto candidates=ChannelViewModel::imageCandidates(images.path());QCOMPARE(candidates.size(),2);
        ChannelViewModel vm(preview(Bus::Lin));const QString before=vm.settings().applicationPath;
        QVERIFY(!vm.chooseImage(false,QString()));QCOMPARE(vm.settings().applicationPath,before);
        QVERIFY(!vm.chooseImage(false,images.path()+"/empty.bin"));QCOMPARE(vm.settings().applicationPath,before);
        QVERIFY(vm.chooseImage(false,candidates.first()));QCOMPARE(vm.settings().applicationPath,candidates.first());
        const auto summary=imageDescription(candidates.first());
        QVERIFY(summary.contains(QFileInfo(candidates.first()).suffix().toUpper()));
        QVERIFY(!summary.contains(QFileInfo(candidates.first()).fileName()));
    }
    void boundedFramesLiteralFilterAndExport() {
        FrameTableModel firstFrame;FrameRecord first;
        first.timestamp="12:34:56.789";first.relativeTime="123456";firstFrame.append({first});
        QCOMPARE(firstFrame.data(firstFrame.index(0,0)).toString(),QString("12:34:56.789"));
        QCOMPARE(firstFrame.data(firstFrame.index(0,1)).toString(),QString("0.000"));
        FrameTableModel model;FrameBatch frames;
        for(int i=0;i<FrameTableModel::Capacity+20;++i)
            frames.append({QString::number(i),"=SUM(A1)","RX","0x20","12 34","有效响应",2});
        model.append(frames);QCOMPARE(model.rowCount(),FrameTableModel::Capacity);
        QVERIFY(QRegularExpression("^\\d{2}:\\d{2}:\\d{2}\\.\\d{3}$").match(model.data(model.index(0,0)).toString()).hasMatch());
        QCOMPARE(model.data(model.index(0,1)).toString(),QString("0.020"));
        QCOMPARE(model.headerData(1,Qt::Horizontal).toString(),QString("相对时间 / ms"));
        QCOMPARE(model.headerData(4,Qt::Horizontal).toString(),QString("ID"));
        QCOMPARE(model.data(model.index(0,0),Qt::TextAlignmentRole).toInt(),int(Qt::AlignCenter));
        QSortFilterProxyModel proxy;proxy.setSourceModel(&model);proxy.setFilterKeyColumn(-1);proxy.setFilterFixedString("[");
        QCOMPARE(proxy.rowCount(),0);proxy.setFilterFixedString("0x20");QCOMPARE(proxy.rowCount(),model.rowCount());
        QString error;const auto path=m_temp.path()+"/frames.csv";QVERIFY(model.exportCsv(path,error));
        QFile f(path);QVERIFY(f.open(QIODevice::ReadOnly));QVERIFY(f.readAll().contains("'=SUM(A1)"));
        model.clear();QCOMPARE(model.rowCount(),0);QCOMPARE(proxy.rowCount(),0);
    }
    void threadedChannelsStayIndependentAndDisconnectCancels() {
        ChannelViewModel can(preview(Bus::Can)),lin(preview(Bus::Lin));
        QTRY_VERIFY_WITH_TIMEOUT(can.canConnect() && lin.canConnect(),3000);
        can.toggleConnection();lin.toggleConnection();
        QTRY_VERIFY_WITH_TIMEOUT(can.connected() && lin.connected() && !can.pending() && !lin.pending(),3000);
        auto changed=can.settings();changed.p2Ms=1900;QVERIFY(!can.setSettings(changed));QCOMPARE(can.settings().p2Ms,30);
        QVERIFY(can.canStart());can.start();can.start();
        QTRY_COMPARE_WITH_TIMEOUT(can.taskState(),TaskState::Running,2000);
        QTRY_VERIFY_WITH_TIMEOUT(can.progress()>0,2000);QCOMPARE(lin.progress(),0);
        QVERIFY(lin.connected());QVERIFY(lin.frames()->rowCount()==0);
        can.toggleConnection();
        QTRY_VERIFY_WITH_TIMEOUT(!can.connected() && !can.pending(),2000);
        QTRY_COMPARE(can.taskState(),TaskState::Cancelled);
        const int stopped=can.progress();QTest::qWait(250);QCOMPARE(can.progress(),stopped);
        QVERIFY(lin.connected());lin.toggleConnection();QTRY_VERIFY(!lin.connected() && !lin.pending());
        QTRY_VERIFY(can.canConnect());can.toggleConnection();QTRY_VERIFY(can.connected() && !can.pending());
        QVERIFY(can.canStart());can.toggleConnection();QTRY_VERIFY(!can.connected() && !can.pending());
    }
    void modeSwitchRetainsUsableSelection() {
        MainWindow window(m_temp.path()+"/mode.json",true);
        auto vm=window.canChannel();auto page=window.findChild<ChannelPage*>("canPage");
        auto mode=page->findChild<QComboBox*>("modeCombo");
        QTRY_VERIFY(vm->canConnect());mode->setCurrentIndex(0);QTRY_VERIFY(!vm->pending());
        QTRY_VERIFY(vm->hardware().isEmpty() || !vm->hardware().first().key.startsWith("preview:"));
        mode->setCurrentIndex(1);QTRY_VERIFY(vm->canConnect());
        QVERIFY(vm->settings().hardwareKey.startsWith("preview:CAN:"));
        vm->toggleConnection();QTRY_VERIFY(vm->connected() && !vm->pending());
        vm->toggleConnection();QTRY_VERIFY(!vm->connected() && !vm->pending());
    }
    void widgetsPreviewCancelCompleteAndSave() {
        MainWindow window(m_temp.path()+"/ui-config.json",true);window.resize(1440,1000);window.setAttribute(Qt::WA_DontShowOnScreen);window.show();QTest::qWait(120);
        auto lin=window.linChannel();auto can=window.canChannel();
        auto quick=lin->settings();quick.p2Ms=30;quick.p2StarMs=60;QVERIFY(lin->setSettings(quick));
        auto tabs=window.findChild<QTabWidget*>("channelTabs");QVERIFY(tabs);tabs->setCurrentIndex(1);
        auto page=window.findChild<ChannelPage*>("linPage");QVERIFY(page);
        auto connectButton=page->findChild<QPushButton*>("connectButton");
        auto start=page->findChild<QPushButton*>("startButton");auto cancel=page->findChild<QPushButton*>("cancelButton");
        QTRY_VERIFY_WITH_TIMEOUT(lin->canConnect(),3000);QVERIFY(lin->chooseImage(true,fixture("ui-driver.bin",64)));
        QVERIFY(lin->chooseImage(false,fixture("ui-cancel.bin",262144)));
        QTest::mouseClick(connectButton,Qt::LeftButton);QTRY_VERIFY(lin->connected() && !lin->pending());
        QVERIFY(!page->findChild<QComboBox*>("hardwareCombo")->isEnabled());
        QVERIFY(page->findChild<QLineEdit*>("applicationPath")->isEnabled());QVERIFY(start->isEnabled());
        QTest::mouseClick(start,Qt::LeftButton);QTRY_COMPARE(lin->taskState(),TaskState::Running);
        QTRY_VERIFY(lin->progress()>0);QVERIFY(!start->isEnabled());QVERIFY(cancel->isEnabled());
        QTest::mouseClick(cancel,Qt::LeftButton);QTRY_COMPARE(lin->taskState(),TaskState::Cancelled);
        QVERIFY(start->isEnabled());QCOMPARE(can->progress(),0);
        QVERIFY(lin->chooseImage(false,fixture()));
        QTest::mouseClick(start,Qt::LeftButton);QTRY_COMPARE_WITH_TIMEOUT(lin->taskState(),TaskState::Completed,8000);
        QCOMPARE(lin->progress(),100);QVERIFY(!cancel->isEnabled());QVERIFY(start->isEnabled());
        QVERIFY(lin->frames()->rowCount()>0);
        const QString folder=artifactDir()+"/";
        QVERIFY(window.grab().save(folder+"bootloader-lin.png"));
        QFile metadata(folder+"display.json");QVERIFY(metadata.open(QIODevice::WriteOnly));
        metadata.write(QJsonDocument(QJsonObject{{"dpr",window.devicePixelRatioF()},{"width",window.width()},
            {"height",window.height()},{"qt",qVersion()}}).toJson());metadata.close();
        tabs->setCurrentIndex(0);QTest::qWait(50);
        QVERIFY(window.grab().save(folder+"bootloader-can.png"));QCOMPARE(can->progress(),0);
        tabs->setCurrentIndex(1);
        QTest::mouseClick(connectButton,Qt::LeftButton);QTRY_VERIFY(!lin->connected() && !lin->pending());
        auto profile=lin->settings();profile.profileId="LIN 验收配置";QVERIFY(lin->setSettings(profile));
        QCOMPARE(lin->settings().profileId,QString("LIN 验收配置"));QCOMPARE(can->settings().profileId,QString("CAN UDS"));
        QTest::mouseClick(window.findChild<QPushButton*>("saveSettings"),Qt::LeftButton);
        QVector<ChannelSettings> saved;QString error;SettingsStore store(m_temp.path()+"/ui-config.json");
        QVERIFY(store.load(saved,error));QCOMPARE(saved.size(),2);QCOMPARE(saved[1].profileId,QString("LIN 验收配置"));
    }
    void linProtocolRepeatsAndRejectsBadHexBeforeTransmit() {
        auto settings=preview(Bus::Lin);settings.applicationPath=fixture("lin-stage3.bin",128);
        settings.repeatDownloadEnabled=true;settings.repeatDownloadCount=2;settings.repeatDownloadIntervalMs=20;
        settings.downloadProfile=QJsonObject{{"blockDataLimit",32}};
        ChannelWorker worker(settings);QSignalSpy tasks(&worker,&ChannelWorker::taskChanged),frames(&worker,&ChannelWorker::framesReceived);
        worker.initialize();worker.connectChannel(settings);worker.startPreview(settings);
        QTRY_VERIFY_WITH_TIMEOUT(!tasks.isEmpty()&&qvariant_cast<TaskState>(tasks.last()[0])==TaskState::Completed,5000);
        QCOMPARE(tasks.last()[1].toInt(),100);QVERIFY(frames.size()>0);
        int resets=0;const auto resetPrefix=settings.nad.rightJustified(2,'0').toUpper()+" 02 11 01";
        for(const auto &args:frames)for(const auto &f:qvariant_cast<FrameBatch>(args[0])){
            QVERIFY(QRegularExpression("^\\d{2}:\\d{2}:\\d{2}\\.\\d{3}$").match(f.timestamp).hasMatch());
            QVERIFY(QRegularExpression("^\\d+$").match(f.relativeTime).hasMatch());
            QVERIFY(f.direction!="SIM HEADER");
            if(f.direction=="SIM TX"&&f.data.startsWith(resetPrefix))++resets;
        }
        QCOMPARE(resets,2);
        settings.applicationPath=fixture("invalid-stage3.hex",16);frames.clear();worker.startPreview(settings);
        QTRY_COMPARE(qvariant_cast<TaskState>(tasks.last()[0]),TaskState::Failed);QCOMPARE(frames.size(),0);
        settings.applicationPath=fixture("cancel-stage3.bin",4096);worker.startPreview(settings);
        QTRY_VERIFY_WITH_TIMEOUT(frames.size()>4,1000);worker.disconnectChannel();
        QTRY_COMPARE(qvariant_cast<TaskState>(tasks.last()[0]),TaskState::Cancelled);
        const int count=frames.size();QTest::qWait(100);QCOMPARE(frames.size(),count);worker.shutdown();
        QString error;ChannelSettings restored;QVERIFY(ChannelSettings::fromJson(settings.toJson(),restored,error));
        auto migrated=settings.downloadProfile;migrated["flow"]="app";
        migrated["stepEnabled"]=ChannelPageInitialValues::initialStepEnabled("app");
        migrated["negativeResponseChecks"]=ChannelPageInitialValues::initialDownloadProfile()["negativeResponseChecks"];
        migrated["timeoutChecks"]=ChannelPageInitialValues::initialDownloadProfile()["timeoutChecks"];
        QCOMPARE(restored.downloadProfile,migrated);
    }
    void canDownloadUsesIsoTpAndPersistsProfile() {
        MainWindow window(m_temp.path()+"/can-stage4.json",true);
        window.resize(1348,684);window.setAttribute(Qt::WA_DontShowOnScreen);window.show();
        auto vm=window.canChannel();QTRY_VERIFY(vm->canConnect());
        auto settings=vm->settings();settings.extendedId=true;settings.requestId="18DA10F1";settings.responseId="18DAF110";
        settings.flashRequired=false;settings.p2Ms=30;settings.p2StarMs=60;
        settings.applicationPath=fixture("can-stage4.bin",128);
        settings.canNetwork=QJsonObject{{"blockSize",1},{"stMin",2},{"maxWaitFrames",1}};
        QVERIFY(vm->setSettings(settings));vm->toggleConnection();QTRY_VERIFY(vm->connected()&&!vm->pending());
        vm->start();QTRY_COMPARE_WITH_TIMEOUT(vm->taskState(),TaskState::Completed,5000);
        QCOMPARE(vm->progress(),100);
        bool flow=false,consecutive=false,requestId=false,responseId=false;
        for(int row=0;row<vm->frames()->rowCount();++row){
            const auto id=vm->frames()->data(vm->frames()->index(row,4)).toString();
            const auto data=vm->frames()->data(vm->frames()->index(row,6)).toString();
            requestId|=id.contains("18DA10F1");responseId|=id.contains("18DAF110");
            flow|=data.startsWith("30 ");consecutive|=data.startsWith("21 ");
        }
        QVERIFY(flow&&consecutive&&requestId&&responseId);
        QVERIFY(window.grab().save(artifactDir()+"/bootloader-can-completed.png"));
        vm->toggleConnection();QTRY_VERIFY(!vm->connected()&&!vm->pending());
        QString error;SettingsStore store(m_temp.path()+"/can-network.json");QVERIFY(store.save({vm->settings()},error));
        QVector<ChannelSettings> saved;QVERIFY(store.load(saved,error));QCOMPARE(saved[0].canNetwork,settings.canNetwork);
        auto invalid=settings;invalid.canNetwork=QJsonObject{{"stMin",128}};QVERIFY(!vm->setSettings(invalid));
    }
    void startupWindowFitsWorkArea() {
        MainWindow window(m_temp.path()+"/startup.json",true);
        window.setAttribute(Qt::WA_DontShowOnScreen);window.show();QTest::qWait(100);
        const auto available=window.screen()->availableGeometry();
        QVERIFY2(window.width()<=available.width(),qPrintable(QString("width %1 > %2").arg(window.width()).arg(available.width())));
        QVERIFY2(window.height()<=available.height(),qPrintable(QString("height %1 > %2").arg(window.height()).arg(available.height())));
    }
    void dynamicChannelDialogAndStartup() {
        MainWindow window(m_temp.path()+"/dynamic.json",true);
        window.setAttribute(Qt::WA_DontShowOnScreen);window.show();
        auto tabs=window.findChild<QTabWidget*>("channelTabs");
        QCOMPARE(tabs->count(),2);QCOMPARE(tabs->tabText(0),QString("CAN01"));QCOMPARE(tabs->tabText(1),QString("LIN01"));
        QVERIFY(window.linChannel()!=nullptr);
        QTimer::singleShot(30,&window,[&](){
            auto dialog=window.findChild<QDialog*>("createChannelDialog");QVERIFY(dialog);
            QCOMPARE(dialog->findChild<QLineEdit*>("channelName")->text(),QString("CAN02"));
            dialog->reject();
        });
        QTest::mouseClick(window.findChild<QPushButton*>("createChannel"),Qt::LeftButton);
        QCOMPARE(tabs->count(),2);
        QTimer::singleShot(30,&window,[&](){
            auto dialog=window.findChild<QDialog*>("createChannelDialog");QVERIFY(dialog);
            dialog->findChild<QComboBox*>("channelType")->setCurrentIndex(1);
            auto name=dialog->findChild<QLineEdit*>("channelName");QCOMPARE(name->text(),QString("LIN02"));
            name->setText("LIN 台架 A");
            QTest::mouseClick(dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok),Qt::LeftButton);
        });
        QTest::mouseClick(window.findChild<QPushButton*>("createChannel"),Qt::LeftButton);
        QCOMPARE(tabs->count(),3);QCOMPARE(tabs->tabText(2),QString("LIN 台架 A"));
        QString error;auto duplicate=preview(Bus::Can);duplicate.softwareId="lin 台架 a";
        QVERIFY(!window.addChannel(duplicate,error));QCOMPARE(tabs->count(),3);
        auto extra=preview(Bus::Can);extra.softwareId=window.nextChannelName(Bus::Can);
        QVERIFY(window.addChannel(extra,error));QCOMPARE(tabs->count(),4);QCOMPARE(tabs->tabText(3),QString("CAN02"));
        QTRY_VERIFY(!window.canChannel()->busy());
        QTest::mouseClick(window.findChild<QPushButton*>("saveSettings"),Qt::LeftButton);
        QVector<ChannelSettings> saved;QVERIFY(SettingsStore(m_temp.path()+"/dynamic.json").load(saved,error));QCOMPARE(saved.size(),4);
        MainWindow restarted(m_temp.path()+"/dynamic.json",true);
        QCOMPARE(restarted.channels().size(),2);QCOMPARE(restarted.canChannel()->settings().softwareId,QString("CAN01"));
        QCOMPARE(restarted.linChannel()->settings().softwareId,QString("LIN01"));
        QVector<ChannelSettings> preserved;QVERIFY(SettingsStore(m_temp.path()+"/dynamic.json").load(preserved,error));QCOMPARE(preserved.size(),4);
        QVERIFY(restarted.restoreChannels(m_temp.path()+"/dynamic.json",error));QCOMPARE(restarted.channels().size(),4);
        QCOMPARE(restarted.channels()[2]->settings().softwareId,QString("LIN 台架 A"));
    }
    void availablePortsExcludeOccupiedAndRestoreAfterClose() {
        MainWindow window(m_temp.path()+"/ports.json",true);QString error;
        auto s=preview(Bus::Can);s.softwareId="CAN02";auto second=window.addChannel(s,error);QVERIFY(second);
        auto first=window.canChannel();auto tabs=window.findChild<QTabWidget*>("channelTabs");
        auto firstPage=qobject_cast<ChannelPage*>(tabs->widget(0)),secondPage=qobject_cast<ChannelPage*>(tabs->widget(2));
        auto ports=secondPage->findChild<QComboBox*>("softwareChannelCombo");
        QTRY_COMPARE(ports->count(),2);QTRY_VERIFY(first->canConnect());
        const auto firstHandle=first->settings().handle;first->toggleConnection();
        QTRY_VERIFY(first->connected() && !first->pending());
        QTRY_COMPARE(ports->count(),1);QVERIFY(second->settings().handle!=firstHandle);
        QTRY_VERIFY(second->canConnect());second->toggleConnection();
        QTRY_VERIFY(second->connected() && !second->pending());QVERIFY(first->connected());
        QCOMPARE(first->settings().handle,firstHandle);
        auto thirdSettings=preview(Bus::Can);thirdSettings.softwareId="CAN03";
        auto third=window.addChannel(thirdSettings,error);QVERIFY(third);
        auto thirdPorts=qobject_cast<ChannelPage*>(tabs->widget(3))->findChild<QComboBox*>("softwareChannelCombo");
        QTRY_VERIFY(thirdPorts->currentData().toString().isEmpty());QVERIFY(!third->canConnect());
        QVERIFY(!window.restoreChannels(m_temp.path()+"/dynamic.json",error));QCOMPARE(window.channels().size(),4);QVERIFY(first->connected());
        QVERIFY(!firstPage->findChild<QComboBox*>("softwareChannelCombo")->isEnabled());
        second->toggleConnection();QTRY_VERIFY(!second->connected() && !second->pending());QVERIFY(first->connected());
        QTRY_COMPARE(ports->count(),1);QTRY_VERIFY(third->canConnect());
        first->toggleConnection();QTRY_VERIFY(!first->connected() && !first->pending());
        QTRY_COMPARE(ports->count(),2);
        QCOMPARE(first->communicationIndicator(),0);QCOMPARE(tabs->tabToolTip(0),QString("未通讯"));
    }
    void communicationLossIndicatorLatchesUntilReconnected() {
        MainWindow window(m_temp.path()+"/indicator.json",true);
        auto vm=window.canChannel();auto model=vm->findChild<ChannelModel*>();QVERIFY(model);
        auto tabs=window.findChild<QTabWidget*>("channelTabs");
        QTRY_VERIFY(vm->canConnect());QCOMPARE(vm->communicationIndicator(),0);
        model->stateChanged(communication::ConnectionState::Connected,{});
        model->healthChanged(communication::Health::Ready,{});
        QCOMPARE(vm->communicationIndicator(),1);QCOMPARE(tabs->tabToolTip(0),QString("通讯正常"));
        auto color=tabs->tabIcon(0).pixmap(16,16).toImage().pixelColor(8,8);QVERIFY(color.green()>color.red());
        model->stateChanged(communication::ConnectionState::Missing,"removed");
        QCOMPARE(vm->communicationIndicator(),2);QCOMPARE(tabs->tabToolTip(0),QString("通讯丢失"));
        color=tabs->tabIcon(0).pixmap(16,16).toImage().pixelColor(8,8);QVERIFY(color.red()>color.green());
        model->stateChanged(communication::ConnectionState::Available,{});
        QCOMPARE(vm->communicationIndicator(),2);
        model->stateChanged(communication::ConnectionState::Connected,{});
        model->healthChanged(communication::Health::Ready,{});
        QCOMPARE(vm->communicationIndicator(),1);
        model->healthChanged(communication::Health::Sleeping,"idle bus");QCOMPARE(vm->communicationIndicator(),1);
        model->healthChanged(communication::Health::BusWarning,"bus warning");QCOMPARE(vm->communicationIndicator(),2);
        model->stateChanged(communication::ConnectionState::Missing,"removed");
        auto ports=vm->hardware();auto other=ports.first();other.key="preview:SECOND:1";other.handle=0xff41;other.label="Second adapter";ports.append(other);
        model->hardwareChanged(ports);
        auto devices=window.findChild<ChannelPage*>("canPage")->findChild<QComboBox*>("hardwareCombo");
        const int row=devices->findData(hardwareDeviceKey(other));QVERIFY(row>=0);
        devices->setCurrentIndex(row);QCOMPARE(vm->settings().hardwareKey,other.key);
        QCOMPARE(vm->communicationIndicator(),2); // Explicit selection is allowed; only a successful connection clears red.
    }
    void protocolDialogKeepaliveEnableAndCancel() {
        MainWindow window(m_temp.path()+"/uds.json",true);QString error;
        auto vm=window.linChannel();QVERIFY(vm);
        auto page=window.findChild<ChannelPage*>("linPage");
        window.setAttribute(Qt::WA_DontShowOnScreen);window.show();QTRY_VERIFY(vm->canConnect());
        QVERIFY(!page->findChild<QLineEdit*>("requestId"));QVERIFY(!page->findChild<QSpinBox*>("p2Ms"));
        QTimer::singleShot(30,&window,[&](){
            auto dialog=page->findChild<QDialog*>("protocolDialog");QVERIFY(dialog);
            auto enable=dialog->findChild<QCheckBox*>("testerPresentEnabled");auto period=dialog->findChild<QSpinBox*>("testerPresentMs");
            QVERIFY(!enable->isChecked());QVERIFY(!period->isEnabled());enable->setChecked(true);QVERIFY(period->isEnabled());
            period->setValue(1700);dialog->findChild<QSpinBox*>("p2Ms")->setValue(789);
            QTest::mouseClick(dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok),Qt::LeftButton);
        });
        QTest::mouseClick(page->findChild<QPushButton*>("protocolSettings"),Qt::LeftButton);
        QVERIFY(vm->settings().testerPresentEnabled);QCOMPARE(vm->settings().testerPresentMs,1700);QCOMPARE(vm->settings().p2Ms,789);
        QCOMPARE(window.canChannel()->settings().p2Ms,1000);
        QTimer::singleShot(30,&window,[&](){
            auto dialog=page->findChild<QDialog*>("protocolDialog");QVERIFY(dialog);
            dialog->findChild<QCheckBox*>("testerPresentEnabled")->setChecked(false);
            dialog->findChild<QSpinBox*>("p2Ms")->setValue(1);dialog->reject();
        });
        QTest::mouseClick(page->findChild<QPushButton*>("protocolSettings"),Qt::LeftButton);
        QVERIFY(vm->settings().testerPresentEnabled);QCOMPARE(vm->settings().p2Ms,789);
        auto disabled=vm->settings();disabled.testerPresentEnabled=false;QVERIFY(vm->setSettings(disabled));
        communication::SoftwareChannelConfiguration cfg;QVERIFY(disabled.toConfiguration(cfg,error));QCOMPARE(cfg.uds.testerPresentMs,0);QCOMPARE(disabled.testerPresentMs,1700);
    }
    void compactDownloadRegionsRemainVisible() {
        MainWindow window(m_temp.path()+"/compact.json",true);
        window.setAttribute(Qt::WA_DontShowOnScreen);
        window.findChild<QTabWidget*>("channelTabs")->setCurrentIndex(1);
        const qreal dpr=window.devicePixelRatioF();
        // 1366x768 physical desktop, with space for taskbar, title bar and borders.
        const QSize client(qFloor(1348/dpr),qFloor(684/dpr));
        window.resize(client);window.show();QTest::qWait(150);
        QVERIFY2(window.width()<=client.width() && window.height()<=client.height(),
            qPrintable(QString("minimum %1x%2 exceeds %3x%4 at DPR %5").arg(window.width()).arg(window.height()).arg(client.width()).arg(client.height()).arg(dpr)));
        auto page=window.findChild<ChannelPage*>("linPage");
        for(const QString &name:{"imageRegion","taskRegion","startButton","applicationPath","flashPath","downloadProgress","frameTable","eventLog"}){
            auto w=page->findChild<QWidget*>(name);QVERIFY(w);QVERIFY(w->isVisible());
            const QRect rect(w->mapTo(&window,QPoint()),w->size());
            QVERIFY2(window.rect().contains(rect),qPrintable(name+" outside window"));
            QVERIFY2(w->visibleRegion().contains(w->rect()),qPrintable(name+" clipped"));
        }
        QVERIFY(page->findChild<QTableView*>("frameTable")->viewport()->height()>=24);
        QVERIFY(!page->findChild<QScrollArea*>());
        auto version=window.findChild<QLabel*>("versionBadge");QVERIFY(version);QVERIFY(version->parentWidget()==window.statusBar());
        const auto pos=version->mapTo(&window,QPoint());
        QVERIFY(pos.x()>window.width()/2);QVERIFY(pos.y()>window.height()-50);QCOMPARE(version->text(),QString("ReleaseVer: 1.0"));
        QVERIFY(window.grab().save(artifactDir()+"/bootloader-1366.png"));
        QFile metadata(artifactDir()+"/display.json");QVERIFY(metadata.open(QIODevice::WriteOnly));
        metadata.write(QJsonDocument(QJsonObject{{"dpr",dpr},{"clientWidth",window.width()},{"clientHeight",window.height()},{"qt",qVersion()}}).toJson());
    }
    void deleteChannelMenuReleasesOnlySelectedPort() {
        MainWindow window(m_temp.path()+"/delete.json",true);window.setAttribute(Qt::WA_DontShowOnScreen);window.show();
        QString error;QVERIFY(window.removeChannel(window.linChannel()));
        auto first=window.canChannel();auto firstSettings=first->settings();firstSettings.flashRequired=false;QVERIFY(first->setSettings(firstSettings));
        auto s=preview(Bus::Can);s.softwareId="CAN02";
        auto second=window.addChannel(s,error);QVERIFY(second);QTRY_VERIFY(first->canConnect());
        QVERIFY(first->chooseImage(false,fixture()));first->toggleConnection();QTRY_VERIFY(first->connected() && !first->pending());
        QTRY_VERIFY(second->canConnect());second->toggleConnection();QTRY_VERIFY(second->connected() && !second->pending());
        const auto released=first->settings().handle,remaining=second->settings().handle;
        first->start();QTRY_VERIFY(first->taskState()==TaskState::Running && !first->pending());
        QPointer<ChannelViewModel> removed=first;
        auto tabs=window.findChild<QTabWidget*>("channelTabs");auto bar=tabs->tabBar();
        QTimer::singleShot(30,&window,[&](){
            auto menu=window.findChild<QMenu*>("channelContextMenu");QVERIFY(menu);
            auto action=menu->findChild<QAction*>("deleteChannelAction");QVERIFY(action);QCOMPARE(action->text(),QString("删除通道"));
            QTest::mouseClick(menu,Qt::LeftButton,Qt::NoModifier,menu->actionGeometry(action).center());
        });
        QVERIFY(QMetaObject::invokeMethod(bar,"customContextMenuRequested",Qt::DirectConnection,Q_ARG(QPoint,bar->tabRect(0).center())));
        QVERIFY(removed.isNull());QCOMPARE(window.channels().size(),1);QCOMPARE(tabs->tabText(0),QString("CAN02"));
        QVERIFY(second->connected());QCOMPARE(second->settings().handle,remaining);
        s.softwareId="CAN03";auto replacement=window.addChannel(s,error);QVERIFY(replacement);
        QTRY_VERIFY(replacement->canConnect());QCOMPARE(replacement->settings().handle,released);
        replacement->toggleConnection();QTRY_VERIFY(replacement->connected() && !replacement->pending());QVERIFY(second->connected());
        QVERIFY(window.removeChannel(replacement));QVERIFY(window.removeChannel(second));QCOMPARE(tabs->count(),0);
        QTest::mouseClick(window.findChild<QPushButton*>("saveSettings"),Qt::LeftButton);
        QVector<ChannelSettings> empty;QVERIFY(SettingsStore(m_temp.path()+"/delete.json").load(empty,error));QVERIFY(empty.isEmpty());
        QVERIFY(window.restoreChannels(m_temp.path()+"/delete.json",error));QCOMPARE(window.channels().size(),0);
        QCOMPARE(window.nextChannelName(Bus::Can),QString("CAN01"));
    }
    void repeatDownloadSequencingAndStopConditions() {
        auto settings=preview(Bus::Can);settings.applicationPath=fixture("repeat-can.bin",64);settings.repeatDownloadEnabled=true;settings.repeatDownloadCount=3;settings.repeatDownloadIntervalMs=140;
        ChannelWorker worker(settings);worker.initialize();worker.connectChannel(settings);
        TaskState state=TaskState::Idle;QString text;int rounds=0;QElapsedTimer waitTime;QList<qint64> intervals;
        connect(&worker,&ChannelWorker::logMessage,this,[&](const QString &message){
            if(message.startsWith("开始第 ")){++rounds;if(waitTime.isValid()){intervals.append(waitTime.elapsed());waitTime.invalidate();}}
        });
        connect(&worker,&ChannelWorker::taskChanged,this,[&](TaskState next,int,const QString &detail){
            state=next;text=detail;if(detail.contains("等待"))waitTime.start();
        });
        worker.startPreview(settings);QTRY_COMPARE_WITH_TIMEOUT(state,TaskState::Completed,9000);
        QCOMPARE(rounds,3);QCOMPARE(intervals.size(),2);for(auto ms:intervals)QVERIFY2(ms>=140,qPrintable(QString::number(ms)));
        QTest::qWait(180);QCOMPARE(rounds,3);
        settings.repeatDownloadEnabled=false;rounds=0;worker.startPreview(settings);
        QTRY_COMPARE_WITH_TIMEOUT(state,TaskState::Completed,3000);QCOMPARE(rounds,1);
        settings.repeatDownloadEnabled=true;settings.repeatDownloadIntervalMs=200;
        rounds=0;worker.startPreview(settings);QTRY_VERIFY_WITH_TIMEOUT(text.contains("等待"),3000);
        worker.cancelTask();QCOMPARE(state,TaskState::Cancelled);QTest::qWait(250);QCOMPARE(rounds,1);
        rounds=0;worker.startPreview(settings);QTRY_VERIFY_WITH_TIMEOUT(text.contains("等待"),3000);
        worker.disconnectChannel();QCOMPARE(state,TaskState::Cancelled);QTest::qWait(250);QCOMPARE(rounds,1);
        worker.connectChannel(settings);rounds=0;worker.startPreview(settings);QTRY_VERIFY_WITH_TIMEOUT(text.contains("等待"),3000);
        QVERIFY(QFile::remove(settings.applicationPath));QTRY_COMPARE_WITH_TIMEOUT(state,TaskState::Failed,3000);QCOMPARE(rounds,1);
        QTest::qWait(250);QCOMPARE(rounds,1);worker.shutdown();
    }
    void downloadDialogPersistsAndWaitingCanBeCancelled() {
        MainWindow window(m_temp.path()+"/download.json",true);window.setAttribute(Qt::WA_DontShowOnScreen);window.show();
        auto vm=window.canChannel();auto page=window.findChild<ChannelPage*>("canPage");
        auto noDriver=vm->settings();noDriver.flashRequired=false;noDriver.p2Ms=30;noDriver.p2StarMs=60;QVERIFY(vm->setSettings(noDriver));
        QVERIFY(!page->findChild<QPushButton*>("browseFlashDirectory"));QVERIFY(!page->findChild<QPushButton*>("browseApplicationDirectory"));
        QTRY_VERIFY(vm->canConnect());QVERIFY(vm->chooseImage(false,fixture()));
        vm->toggleConnection();QTRY_VERIFY(vm->connected() && !vm->pending());
        auto button=page->findChild<QPushButton*>("downloadSettings");QVERIFY(button->isEnabled());
        QTimer::singleShot(30,&window,[&](){
            auto dialog=page->findChild<QDialog*>("downloadSettingsDialog");QVERIFY(dialog);
            auto enable=dialog->findChild<QCheckBox*>("repeatDownloadEnabled");
            auto count=dialog->findChild<QSpinBox*>("repeatDownloadCount"),interval=dialog->findChild<QSpinBox*>("repeatDownloadIntervalMs");
            QVERIFY(!count->isEnabled());enable->setChecked(true);QVERIFY(count->isEnabled());QVERIFY(interval->isEnabled());
            count->setValue(2);interval->setValue(500);
            QVERIFY(dialog->grab().save(artifactDir()+"/download-settings.png"));
            QTest::mouseClick(dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok),Qt::LeftButton);
        });
        QTest::mouseClick(button,Qt::LeftButton);
        QVERIFY(vm->settings().repeatDownloadEnabled);QCOMPARE(vm->settings().repeatDownloadCount,2);QCOMPARE(vm->settings().repeatDownloadIntervalMs,500);
        QTimer::singleShot(30,&window,[&](){
            auto dialog=page->findChild<QDialog*>("downloadSettingsDialog");QVERIFY(dialog);
            dialog->findChild<QSpinBox*>("repeatDownloadCount")->setValue(99);dialog->reject();
        });
        QTest::mouseClick(button,Qt::LeftButton);QCOMPARE(vm->settings().repeatDownloadCount,2);
        QTest::mouseClick(window.findChild<QPushButton*>("saveSettings"),Qt::LeftButton);
        QVector<ChannelSettings> saved;QString error;QVERIFY(SettingsStore(m_temp.path()+"/download.json").load(saved,error));
        QVERIFY(saved[0].repeatDownloadEnabled);QCOMPARE(saved[0].repeatDownloadCount,2);QCOMPARE(saved[0].repeatDownloadIntervalMs,500);
        QTest::mouseClick(page->findChild<QPushButton*>("startButton"),Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(vm->taskState()==TaskState::Running && vm->taskText().contains("次完成 · 等待"),7000);QCOMPARE(vm->taskState(),TaskState::Running);
        QVERIFY(!button->isEnabled());QVERIFY(!page->findChild<QPushButton*>("startButton")->isEnabled());
        auto cancel=page->findChild<QPushButton*>("cancelButton");QVERIFY(cancel->isEnabled());
        QTest::mouseClick(cancel,Qt::LeftButton);QTRY_COMPARE(vm->taskState(),TaskState::Cancelled);
        const int frames=vm->frames()->rowCount();QTest::qWait(600);QCOMPARE(vm->frames()->rowCount(),frames);QVERIFY(vm->connected());QVERIFY(button->isEnabled());
    }
    void realLinPageHeaderScan() {
        if(qEnvironmentVariableIntValue("HOST_LIN_HARDWARE_TEST")!=1)QSKIP("Opt-in physical LIN test");
        MainWindow window(m_temp.path()+"/hardware.json");window.resize(1440,1000);window.setAttribute(Qt::WA_DontShowOnScreen);window.show();
        auto vm=window.linChannel();auto page=window.findChild<ChannelPage*>("linPage");
        QTRY_VERIFY_WITH_TIMEOUT(vm->canConnect(),4000);
        QTest::mouseClick(page->findChild<QPushButton*>("connectButton"),Qt::LeftButton);
        QTRY_VERIFY(vm->connected() && !vm->pending());QTRY_VERIFY(vm->canScan());
        QVERIFY(!page->findChild<QPushButton*>("startButton")->isEnabled());
        QElapsedTimer idle;idle.start();
        while(idle.elapsed()<12000){
            QTest::qWait(100);QVERIFY(vm->connected());QCOMPARE(vm->communicationIndicator(),1);
        }
        QCOMPARE(vm->health(),communication::Health::Sleeping);QVERIFY(vm->canScan());
        qInfo()<<"Idle LIN stayed connected and green for"<<idle.elapsed()<<"ms; health Sleeping";
        QVERIFY(window.grab().save(artifactDir()+"/lin-sleep-connected.png"));
        QTest::mouseClick(page->findChild<QPushButton*>("scanHeaders"),Qt::LeftButton);
        QTRY_VERIFY(vm->scanning());QTRY_VERIFY_WITH_TIMEOUT(!vm->scanning(),10000);
        QCOMPARE(vm->frames()->rowCount(),61);
        int valid=0;
        for(int i=0;i<vm->frames()->rowCount();++i){
            const QString status=vm->frames()->data(vm->frames()->index(i,6)).toString();
            QVERIFY2(!status.contains("错误"),qPrintable(status));if(status=="有效响应")++valid;
        }
        qInfo()<<"Physical LIN responses:"<<valid;
        QTRY_COMPARE(vm->health(),communication::Health::Ready);QCOMPARE(vm->communicationIndicator(),1);
        QVERIFY(window.grab().save(artifactDir()+"/bootloader-lin-hardware.png"));
        QString error;QVERIFY(vm->frames()->exportCsv(artifactDir()+"/lin-page-frames.csv",error));
        QTest::mouseClick(page->findChild<QPushButton*>("connectButton"),Qt::LeftButton);
        QTRY_VERIFY(!vm->connected() && !vm->pending());
        QVERIFY(page->findChild<QComboBox*>("hardwareCombo")->isEnabled());
    }
};
QTEST_MAIN(HostUiTest)
#include "test_host_ui.moc"


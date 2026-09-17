#include <QtTest>
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QTabWidget>
#include "views/MainWindow.h"
using namespace host;
class ReleaseSmoke : public QObject {
    Q_OBJECT
    QString root() const{return QCoreApplication::applicationDirPath();}
    QString output() const{return qEnvironmentVariable("HOST_VERIFY_OUTPUT",root()+"/verification");}
private slots:
    void initTestCase(){
        QApplication::setStyle("Fusion");QApplication::setFont(QFont("Microsoft YaHei UI",9));
        QVERIFY(QDir().mkpath(output()));
        QVERIFY2(QFile::exists(root()+"/profiles/simulation.json"),"Run this verifier from the packaged release directory.");
    }
    void shippedExecutableStarts(){
        QProcess executable;executable.setWorkingDirectory(QDir::tempPath());
        executable.start(root()+"/QtBootloader.exe",{"--version"});
        QVERIFY(executable.waitForStarted(5000));
        const bool completed=executable.waitForFinished(10000);
        if(!completed){executable.kill();executable.waitForFinished();}
        QVERIFY2(completed,"Packaged executable did not answer --version");
        QCOMPARE(executable.exitStatus(),QProcess::NormalExit);QCOMPARE(executable.exitCode(),0);
        QVERIFY(executable.readAllStandardOutput().contains(MainWindowInitialValues::version));
    }
    void relocatedProfilesAndThreeChannelDownload(){
        QTemporaryDir config;QVERIFY(config.isValid());
        MainWindow window(config.path()+"/channels.json");
        window.resize(1348,684);window.setAttribute(Qt::WA_DontShowOnScreen);window.show();
        QCOMPARE(window.channels().size(),2);QCOMPARE(window.canChannel()->settings().softwareId,QString("CAN01"));
        QCOMPARE(window.linChannel()->settings().softwareId,QString("LIN01"));
        QVERIFY(!window.canChannel()->connected());QString error;
        QVERIFY2(window.restoreChannels(root()+"/profiles/simulation.json",error),qPrintable(error));
        QCOMPARE(window.channels().size(),3);
        for(auto vm:window.channels()){
            const auto s=vm->settings();QVERIFY(s.simulation);
            QVERIFY(s.applicationPath.startsWith(root()+"/profiles/fixtures/"));
            QVERIFY(imageReady(s.applicationPath));QVERIFY(imageReady(s.flashPath));
            QTRY_VERIFY_WITH_TIMEOUT(vm->canConnect(),5000);
            vm->toggleConnection();QTRY_VERIFY_WITH_TIMEOUT(vm->connected()&&!vm->pending(),5000);
            vm->start();
        }
        for(auto vm:window.channels()){
            QTRY_COMPARE_WITH_TIMEOUT(vm->taskState(),TaskState::Completed,15000);
            QCOMPARE(vm->progress(),100);QVERIFY(vm->frames()->rowCount()>0);
        }
        QJsonArray channels;QVector<ChannelSettings> settings;
        for(auto vm:window.channels()){
            channels.append(QJsonObject{{"channel",vm->settings().softwareId},{"progress",vm->progress()},
              {"frames",vm->frames()->rowCount()},{"result","Completed"},{"physicalBusUsed",false}});
            settings.append(vm->settings());
        }
        auto tabs=window.findChild<QTabWidget*>("channelTabs");QVERIFY(tabs);
        for(int i=0;i<tabs->count();++i){
            tabs->setCurrentIndex(i);QTest::qWait(30);
            QVERIFY(window.grab().save(output()+QString("/release-channel-%1.png").arg(i+1)));
        }
        QVERIFY(QDir().mkpath(root()+"/config"));QTemporaryDir saved(root()+"/config/verify-XXXXXX");QVERIFY(saved.isValid());
        const auto path=saved.path()+"/channels.json";QVERIFY(SettingsStore(path).save(settings,error));
        QFile savedFile(path);QVERIFY(savedFile.open(QIODevice::ReadOnly));
        const auto bytes=savedFile.readAll();
        QVERIFY(!bytes.contains(root().toUtf8()));QVERIFY(bytes.contains("profiles/fixtures/"));
        QVector<ChannelSettings> restored;QVERIFY(SettingsStore(path).load(restored,error));
        QCOMPARE(restored.size(),3);QCOMPARE(restored[2].applicationPath,settings[2].applicationPath);
        QFile result(output()+"/release-verification.json");QVERIFY(result.open(QIODevice::WriteOnly));
        result.write(QJsonDocument(QJsonObject{{"qtVersion",qVersion()},{"packageRoot",root()},
          {"cwd",QDir::currentPath()},{"dpr",window.devicePixelRatioF()},
          {"profilesRelocatable",true},{"settingsRoundTrip",true},{"channels",channels}}).toJson());
        for(auto vm:window.channels()){vm->toggleConnection();QTRY_VERIFY(!vm->connected()&&!vm->pending());}
    }
};
QTEST_MAIN(ReleaseSmoke)
#include "release_smoke.moc"

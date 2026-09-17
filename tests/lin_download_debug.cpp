#include <QCoreApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTextStream>
#include <QTimer>
#include <QDir>
#include "viewmodels/ChannelViewModel.h"
#include "infrastructure/SettingsStore.h"
#include "protocol/FirmwareImage.h"
using namespace host;
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);QCommandLineParser parser;parser.addHelpOption();
    parser.addOption({"config","One explicit online LIN channel configuration.","file"});
    parser.addOption({"output","Evidence directory.","directory"});
    parser.addOption({"execute","Execute one actual LIN download. Without this flag, validate files only."});
    parser.process(app);QTextStream out(stdout);
    if(!parser.isSet("config")||!parser.isSet("output"))return 2;
    QVector<ChannelSettings> loaded;QString error;
    if(!SettingsStore(parser.value("config")).load(loaded,error)||loaded.size()!=1){out<<error<<Qt::endl;return 2;}
    auto settings=loaded[0];
    if(settings.simulation||settings.bus!=communication::Bus::Lin||settings.repeatDownloadEnabled){out<<"Require one online LIN channel without repetition"<<Qt::endl;return 2;}
    boot::FirmwareImage application,driver;
    if(!boot::FirmwareImage::load(settings.applicationPath,settings.applicationAddress.toUInt(nullptr,16),application,error)||
       (settings.flashRequired&&!boot::FirmwareImage::load(settings.flashPath,settings.flashAddress.toUInt(nullptr,16),driver,error))){out<<error<<Qt::endl;return 2;}
    const QString folder=QFileInfo(parser.value("output")).absoluteFilePath();QDir().mkpath(folder);
    auto imageJson=[](const boot::FirmwareImage &image){
        QJsonArray segments;for(const auto &s:image.segments)segments.append(QJsonObject{{"address",QString::number(s.address,16)},{"bytes",s.data.size()}});
        return QJsonObject{{"segments",segments},{"bytes",image.size},{"sha256",QString::fromLatin1(image.sha256.toHex())}};
    };
    QJsonObject report{{"settings",settings.toJson()},{"application",imageJson(application)},{"driver",imageJson(driver)},
        {"physicalBusUsed",parser.isSet("execute")}};
    out<<QJsonDocument(report).toJson(QJsonDocument::Compact)<<Qt::endl;
    auto saveReport=[&](const QString &result){report["result"]=result;QFile file(folder+"/result.json");if(file.open(QIODevice::WriteOnly))file.write(QJsonDocument(report).toJson());};
    if(!parser.isSet("execute")){saveReport("Files validated; no hardware access");return 0;}
    PLinApiClass api;tstPeakLin inventory(nullptr,&api);const auto hardware=inventory.availableHardware();
    communication::HardwareChannels candidates;
    for(const auto &h:hardware)if(h.available&&(settings.hardwareKey.isEmpty()||settings.hardwareKey==h.key))candidates.append(h);
    if(candidates.size()!=1){saveReport("No unique available PLIN channel");out<<inventory.lastErrorText()<<" Available matches: "<<candidates.size()<<Qt::endl;return 3;}
    settings.hardwareKey=candidates[0].key;settings.handle=candidates[0].handle;report["settings"]=settings.toJson();
    QFile events(folder+"/events.jsonl");if(!events.open(QIODevice::WriteOnly))return 2;
    auto event=[&](QJsonObject object){object["utc"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);events.write(QJsonDocument(object).toJson(QJsonDocument::Compact));events.write("\n");events.flush();};
    ChannelViewModel vm(settings);bool connecting=false,started=false,finished=false;int exitCode=4;
    QObject::connect(&vm,&ChannelViewModel::logAdded,&app,[&](QString line){event({{"type","log"},{"text",line}});out<<line<<Qt::endl;});
    auto model=vm.findChild<ChannelModel*>();
    QObject::connect(model,&ChannelModel::framesReceived,&app,[&](const FrameBatch &batch){
        for(const auto &f:batch)event({{"type","frame"},{"direction",f.direction},{"id",f.identifier},{"data",f.data},{"status",f.status},{"hardwareTime",f.timestamp}});
    });
    auto finish=[&](const QString &reason,int code){
        if(finished)return;finished=true;exitCode=code;report["progress"]=vm.progress();saveReport(reason);
        out<<"RESULT: "<<reason<<Qt::endl;if(vm.connected())vm.toggleConnection();QTimer::singleShot(500,&app,&QCoreApplication::quit);
    };
    QObject::connect(&vm,&ChannelViewModel::changed,&app,[&]{
        if(finished)return;
        if(started&&(vm.taskState()==TaskState::Completed||vm.taskState()==TaskState::Failed||vm.taskState()==TaskState::Cancelled)){
            finish(vm.taskText(),vm.taskState()==TaskState::Completed?0:1);return;
        }
        if(!connecting&&vm.canConnect()){connecting=true;vm.toggleConnection();return;}
        if(connecting&&!started&&vm.connected()&&!vm.pending()){
            if(!vm.canStart()){finish(vm.startHint(),4);return;}
            started=true;vm.start();
        }
    });
    QTimer::singleShot(10000,&app,[&]{if(!started)finish("Connection did not become ready",4);});
    QTimer::singleShot(900000,&app,[&]{vm.cancel();finish("Debug run deadline exceeded",5);});
    app.exec();events.close();return exitCode;
}

#include <QCoreApplication>
#include <QTextStream>
#include <QCommandLineParser>
#include <QTimer>
#include "communication/SoftwareChannel.h"
using namespace communication;
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"open","Initialize and close available PLIN channels (no frames transmitted)."});
    parser.addOption({"verify-reconnect","After observed removal and return, exercise the explicit connect command once in this process."});
    parser.addOption({"observe","Observe first channel for unplug/replug; duration in seconds.","seconds","0"});
    parser.process(app);
    QTextStream out(stdout);
    PLinApiClass api;
    out<<"PLIN DLL: "<<api.libraryPath()<<" loaded="<<api.isLoaded()<<" error="<<api.libraryError()<<Qt::endl;
    if(!api.isLoaded())return 2;
    tstPeakLin driver(nullptr,&api);
    const auto found=driver.availableHardware();
    if(driver.lastErrorCode()!=errOK){out<<driver.lastErrorText()<<Qt::endl;return 3;}
    out<<"Available channels: "<<found.size()<<Qt::endl;
    for(const auto &h:found)out<<h.label<<" key="<<h.key<<" free="<<h.available<<" persistent-identity="<<h.persistentIdentity<<Qt::endl;
    if(!parser.isSet("open") && parser.value("observe")=="0")return 0;
    if(found.isEmpty())return 4;
    int failures=0;
    for(const auto &h:found) {
        if(!h.available){out<<"SKIP occupied: "<<h.label<<Qt::endl;++failures;continue;}
        auto config=SoftwareChannelConfiguration();
        config.softwareId=QString("LIN-smoke-%1").arg(h.handle);config.bus=Bus::Lin;
        config.bitrate=19200;config.preferredHandle=h.handle;config.hardwareKey=h.key;
        config.uds.profileId=config.softwareId;
        SoftwareChannel channel(config,std::unique_ptr<HardwareBackend>(new PeakLinBackend(driver)));
        for(int cycle=0;cycle<3;++cycle){
            if(!channel.connectChannel()){out<<"OPEN FAIL "<<channel.error()<<Qt::endl;++failures;break;}
            QString detail;const auto health=driver.hardwareHealth(&detail);
            TLINRcvMsg messages[16]={};const auto received=driver.recvRaw(16,messages);
            out<<"cycle="<<cycle+1<<" handle="<<h.handle<<" health="<<int(health)<<" read="<<received<<" detail="<<detail<<Qt::endl;
            if(!channel.disconnectChannel()){out<<"CLOSE FAIL "<<channel.error()<<Qt::endl;++failures;}
        }
    }
    int seconds=parser.value("observe").toInt();
    if(seconds>0 && found.first().available){
        auto config=SoftwareChannelConfiguration();config.softwareId="LIN-observe";config.bus=Bus::Lin;
        config.bitrate=19200;config.hardwareKey=found.first().key;config.autoReconnect=true;
        SoftwareChannel channel(config,std::unique_ptr<HardwareBackend>(new PeakLinBackend(driver)));
        QObject::connect(&channel,&SoftwareChannel::stateChanged,&app,[&out](ConnectionState s,const QString &detail){
            out<<"state="<<int(s)<<" "<<detail<<Qt::endl;
        });
        QElapsedTimer elapsed;elapsed.start();
        QObject::connect(&channel,&SoftwareChannel::stateChanged,&app,[&out,&elapsed,&channel](ConnectionState s,const QString &){
            out<<"elapsed-ms="<<elapsed.elapsed()<<" state="<<int(s)<<" generation="<<channel.generation()<<Qt::endl;
        });
        QObject::connect(&channel,&SoftwareChannel::healthChanged,&app,[&out,&elapsed](Health h,const QString &detail){
            out<<"elapsed-ms="<<elapsed.elapsed()<<" health="<<int(h)<<" detail="<<detail<<Qt::endl;
        });
        bool removalObserved=false, reconnectAttempted=false, reconnectSucceeded=false;
        const bool verifyReconnect=parser.isSet("verify-reconnect");
        QObject::connect(&channel,&SoftwareChannel::stateChanged,&app,[&](ConnectionState state,const QString &){
            if (state==ConnectionState::Missing || state==ConnectionState::Fault) removalObserved=true;
            if (verifyReconnect && removalObserved && state==ConnectionState::Available && !reconnectAttempted) {
                reconnectAttempted=true;
                // Exercise the explicit command outside the poll callback, as a UI button would.
                QTimer::singleShot(0,&app,[&](){
                    out<<"EXPLICIT RECONNECT CHECK in same process"<<Qt::endl;
                    reconnectSucceeded=channel.connectChannel();
                    out<<"reconnect="<<reconnectSucceeded<<" generation="<<channel.generation()
                       <<" detail="<<channel.error()<<Qt::endl;
                    app.quit();
                });
            }
        });
        channel.startMonitoring();
        if(!channel.connectChannel())return 5;
        QTimer::singleShot(seconds*1000,&app,&QCoreApplication::quit);
        out<<"OBSERVING "<<seconds<<" seconds; no LIN frames are transmitted."<<Qt::endl;
        app.exec();
        if (verifyReconnect && (!removalObserved || !reconnectSucceeded)) {
            out<<"RECONNECT CHECK INCOMPLETE: removal or successful reconnect not observed"<<Qt::endl;
            ++failures;
        }
        channel.disconnectChannel();
    }
    out<<"PLIN smoke failures: "<<failures<<Qt::endl;
    return failures?1:0;
}

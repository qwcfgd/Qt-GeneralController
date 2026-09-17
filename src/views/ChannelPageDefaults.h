#pragma once
#include "communication/ChannelTypes.h"
#include <QJsonObject>
#include <QString>
#include "protocol/DownloadSteps.h"
namespace host {
// Public sample values only; configure target-specific parameters locally.
// Single source for new pages and missing JSON fields. Model and ViewModel receive
// these values through ChannelSettings; widgets never maintain a separate default.
struct ChannelPageInitialValues {
    static constexpr const char *onlineLabel="online",*simulationLabel="simulation";
    communication::Bus bus=communication::Bus::Can;
    QString softwareId="CAN01",profileId="CAN UDS";
    inline static const QString linName="LIN01",linProfile="LIN UDS";
    static constexpr int linBitrate=19200;
    static constexpr bool feedbackChecked=false,defaultSimulation=false,defaultRxdEnabled=false;
    static QJsonObject initialStepEnabled(const QString &flow){
        QJsonObject enabled;
        for(const auto &step:boot::downloadSteps())enabled[step.id]=flow!="boot"||!step.appOnly;
        return enabled;
    }
    static QJsonObject initialDownloadProfile(){
        QJsonObject checks,timeouts;for(const auto &step:boot::downloadSteps()){
            checks[step.id]=feedbackChecked;timeouts[step.id]=!boot::suppressesPositiveResponse(step.id)&&feedbackChecked;
        }
        return {{"flow","app"},{"stepEnabled",initialStepEnabled("app")},{"negativeResponseChecks",checks},{"timeoutChecks",timeouts},{"simulationOnly",false},
            {"keyProvider","external-generatekeyex"},{"keyLibrary",""},
            {"eraseRoutine",0xff00},{"verifyRoutine",0xff01},{"dependencyRoutine",0xff02},
            {"identityDid",0xf180},{"consecutiveFrameByteLimit",4095},{"resetWaitMs",1000}};
    }
    QJsonObject downloadProfile=initialDownloadProfile();
    QJsonObject canNetwork={{"blockSize",8},{"stMin",1},{"maxWaitFrames",3},
        {"nAsMs",1000},{"nBsMs",1000},{"nCrMs",1000},{"receiveCapacity",4095},{"padding",255}};
    QString hardwareKey;quint32 handle=0;
    int bitrate=500000;bool simulation=defaultSimulation,autoReconnect=false,extendedId=false;
    QString requestId="7E0",responseId="7E8",functionalId="7DF",nad="01";
    int p2Ms=1000,p2StarMs=5500,testerPresentMs=2000,maxPendingMs=60000;
    int programmingSession=2,securityLevel=0x01;
    QString flashPath,applicationPath,flashAddress="00000000",applicationAddress="00010000";
    bool flashRequired=true,testerPresentEnabled=false,rxdEnabled=ChannelPageInitialValues::defaultRxdEnabled;
    bool repeatDownloadEnabled=false;
    int repeatDownloadCount=2,repeatDownloadIntervalMs=1000;
    static constexpr bool followFrames=true;
    static constexpr int logCapacity=500,elapsedRefreshMs=250;
};
}

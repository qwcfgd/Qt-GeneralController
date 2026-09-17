#pragma once
#include "communication/ChannelTypes.h"
#include <QDateTime>
#include <QFileInfo>
#include <QJsonObject>
#include "views/ChannelPageDefaults.h"
namespace host {
struct ChannelSettings : ChannelPageInitialValues {
    static ChannelSettings defaults(communication::Bus bus);
    bool toConfiguration(communication::SoftwareChannelConfiguration &out,QString &error) const;
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &,ChannelSettings &,QString &error);
};
struct FrameRecord {
    QString timestamp,channel,direction,identifier,data,status;
    int length=0;bool error=false,warning=false;
    QString relativeTime;
};
using FrameBatch=QVector<FrameRecord>;
enum class TaskState {Idle,Running,Completed,Cancelled,Failed};
QString hardwareDeviceKey(const communication::HardwareChannel &);
QString connectionText(communication::ConnectionState);
QString imageDescription(const QString &path);
bool imageReady(const QString &path);
}
Q_DECLARE_METATYPE(host::ChannelSettings)
Q_DECLARE_METATYPE(host::FrameBatch)
Q_DECLARE_METATYPE(host::TaskState)

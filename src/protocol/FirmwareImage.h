#pragma once
#include <QByteArray>
#include <QString>
#include <QVector>
namespace boot {
struct Segment { quint32 address=0; QByteArray data; };
struct FirmwareImage {
    QVector<Segment> segments;
    QByteArray sha256;
    qint64 size=0;
    static bool load(const QString &path,quint32 binaryBase,FirmwareImage &out,QString &error);
};
quint32 crc32(const QByteArray &bytes);
QByteArray be32(quint32 value);
quint32 readBe(const QByteArray &bytes);
}

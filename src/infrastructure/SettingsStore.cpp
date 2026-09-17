#include "SettingsStore.h"
#include <QFile>
#include <QCoreApplication>
#include <QSaveFile>
#include <QDir>
#include <QSet>
#include <QJsonDocument>
#include <QJsonArray>
namespace host {
static bool within(const QString &relative){
    return !QDir::isAbsolutePath(relative)&&relative!=".."&&!relative.startsWith("../");
}
static QString portableImagePath(const QString &path,const QString &settingsPath){
    if(path.isEmpty()||QDir::isRelativePath(path))return path;
    const QDir folder(QFileInfo(settingsPath).absolutePath());
    const auto relative=folder.relativeFilePath(path);
    if(within(relative))return relative;
    const QDir application(QCoreApplication::applicationDirPath());
    if(within(application.relativeFilePath(path))&&within(application.relativeFilePath(folder.absolutePath())))return relative;
    return path; // External user images retain their absolute location.
}

bool SettingsStore::load(QVector<ChannelSettings> &settings,QString &error) const {
    error.clear();QFile f(m_path);if(!f.exists())return true;
    if(!f.open(QIODevice::ReadOnly) || f.size()>1024*1024){error="无法读取通道配置。";return false;}
    QJsonParseError parse;const auto doc=QJsonDocument::fromJson(f.readAll(),&parse);
    const auto root=doc.object();
    if(parse.error!=QJsonParseError::NoError || (root.value("version").toInt()!=1 && root.value("version").toInt()!=2) ||
       !root.value("channels").isArray()){error="通道配置格式无效。";return false;}
    QVector<ChannelSettings> loaded;QSet<QString> names;
    for(const auto &item:root.value("channels").toArray()) {
        ChannelSettings s;if(!ChannelSettings::fromJson(item.toObject(),s,error))return false;
        const QDir folder(QFileInfo(m_path).absolutePath());
        for(QString *image:{&s.flashPath,&s.applicationPath})
            if(!image->isEmpty()&&QDir::isRelativePath(*image))*image=QDir::cleanPath(folder.absoluteFilePath(*image));
        const QString name=s.softwareId.trimmed().toCaseFolded();
        if(names.contains(name)){error="软件通道名称不能重复。";return false;}
        names.insert(name);s.softwareId=s.softwareId.trimmed();loaded.append(s);
    }
    if(loaded.size()>64){error="配置需包含 0–64 个软件通道。";return false;}
    settings=loaded;return true;
}
bool SettingsStore::save(const QVector<ChannelSettings> &settings,QString &error) const {
    error.clear();QJsonArray list;QSet<QString> names;
    for(const auto &s:settings) {
        communication::SoftwareChannelConfiguration c;
        if(!s.toConfiguration(c,error))return false;
        if(names.contains(c.softwareId.toCaseFolded())){error="软件通道名称不能重复。";return false;}
        names.insert(c.softwareId.toCaseFolded());
        auto json=s.toJson();
        json["flashPath"]=portableImagePath(s.flashPath,m_path);
        json["applicationPath"]=portableImagePath(s.applicationPath,m_path);
        list.append(json);
    }
    if(settings.size()>64){error="配置需包含 0–64 个软件通道。";return false;}
    if(!QDir().mkpath(QFileInfo(m_path).absolutePath())){error="无法创建配置目录。";return false;}
    QSaveFile f(m_path);const QByteArray bytes=QJsonDocument(QJsonObject{{"version",2},{"channels",list}}).toJson();
    if(!f.open(QIODevice::WriteOnly) || f.write(bytes)!=bytes.size() || !f.commit()){
        error="保存配置失败，原文件保持不变。";return false;
    }return true;
}
}

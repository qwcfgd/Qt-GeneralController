#pragma once
#include "domain/HostTypes.h"
namespace host {
class SettingsStore {
public:
    explicit SettingsStore(QString path):m_path(std::move(path)){}
    bool load(QVector<ChannelSettings> &,QString &error) const;
    bool save(const QVector<ChannelSettings> &,QString &error) const;
private: QString m_path;
};
}

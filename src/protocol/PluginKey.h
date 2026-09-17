#pragma once
#include "FlashJob.h"
#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif
namespace boot {
// The supplied GenerateKeyEx DLL is PE32. Call its exact C ABI in a small,
// isolated 32-bit process, preserving the original DLL and its algorithm.
class PluginKey final : public KeyProvider {
public:
    explicit PluginKey(QString path):m_path(std::move(path)){}
    QByteArray calculate(quint8 level,const QByteArray &seed,QString &error) override {
        error.clear();
        if(seed.isEmpty()||seed.size()>4093||seed.size()%16){
            error="External GenerateKeyEx requires a non-empty seed of complete 16-byte blocks";return {};
        }
        const auto root=QCoreApplication::applicationDirPath();
        const QString dll=QDir::isRelativePath(m_path)?QDir(root).absoluteFilePath(m_path):m_path;
        QProcess process;process.setWorkingDirectory(root);
#ifdef Q_OS_WIN
        process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args){args->flags|=CREATE_NO_WINDOW;});
#endif
        process.start(root+"/seedkey/SeedkeyBridge32.exe",{dll,QString::number(level),QString::fromLatin1(seed.toHex())});
        if(!process.waitForStarted(3000)){error="Seedkey bridge: "+process.errorString();return {};}
        if(!process.waitForFinished(3000)){process.kill();process.waitForFinished();error="Seedkey DLL timeout";return {};}
        if(process.exitStatus()!=QProcess::NormalExit||process.exitCode()!=0){
            error="GenerateKeyEx failed: "+QString::fromLocal8Bit(process.readAllStandardError()).trimmed();return {};
        }
        const auto result=process.readAllStandardOutput().trimmed();
        const auto key=QByteArray::fromHex(result);
        if(result.size()!=32||key.size()!=16||key.toHex()!=result.toLower()){error="Invalid key result from bridge";return {};}
        return key;
    }
private:QString m_path;
};
}

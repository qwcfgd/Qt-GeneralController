#pragma once
#include <QVector>
#include <QString>
namespace boot {
struct DownloadStep {const char *id;const char *label;bool appOnly;};
inline const QVector<DownloadStep> &downloadSteps(){
    static const QVector<DownloadStep> steps={
        {"pre1001","10 01 · 默认会话",true},{"pre1003","10 03 · 扩展会话",true},
        {"pre2701","27 01 · APP 获取 seed",true},{"pre2702","27 02 · APP 发送 key",true},
        {"pre220101","22 01 01 · 读取信息",true},{"pre31010203","31 01 02 03 · 编程条件检查",true},
        {"pre1003control","10 83 · 通讯控制会话",true},{"pre8502","85 82 · 关闭 DTC",true},
        {"pre2803","28 83 01 · 关闭应用通讯",true},
        {"boot1002","10 02 · 编程会话",false},{"bootSeed","27 xx · Boot 获取 seed",false},
        {"bootKey","27 xx · Boot 发送 key",false},{"fingerprint","2E F1 84 01 01 · 写入指纹",false},
        {"driver34","34 · Driver 请求下载",false},{"driver36","36 · Driver 传输数据",false},
        {"driver37","37 · Driver 传输结束",false},{"driverVerify","31 · Driver 校验",false},
        {"erase","31 01 FF00 · 擦除 Application",false},
        {"app34","34 · Application 请求下载",false},{"app36","36 · Application 传输数据",false},
        {"app37","37 · Application 传输结束",false},{"appVerify","31 · Application 校验",false},
        {"dependency","31 · 依赖检查",false},{"reset","11 01 · ECU 复位",false},
        {"post1003","10 03 · 复位后扩展会话",false},{"post14","14 FF FF FF · 清除 DTC",false},
        {"post2800","28 80 01 · 恢复应用通讯",false},{"post8501","85 81 · 恢复 DTC",false},
        {"post1001","10 81 · 恢复默认会话",false},{"identity","22 · 最终身份读取",false}};
    return steps;
}
inline bool suppressesPositiveResponse(const QString &id){
    return id=="pre1003control"||id=="pre8502"||id=="pre2803"||
        id=="post2800"||id=="post8501"||id=="post1001";
}
}

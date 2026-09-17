# 使用说明

启动 QtBootloader.exe，通过通道页选择 CAN / LIN 及 online / simulation 模式。选择 simulation 后连接通道，即可载入模拟镜像进行协议验证。

## 模拟下载

源码示例配置位于 profiles/，发布包使用 profiles/simulation.json。镜像相对路径以配置文件目录解析。测试固件只用于模拟，不能写入实际设备。

APP / Boot 流程、步骤反馈、超时、镜像基址和 RID / DID 可在下载设置中配置。公开默认值是示例值；真实目标参数需要单独填写并保存至本地配置。

## 外部安全访问算法

公开仓库和公开发布包不包含任何私有算法 DLL 或真实 seed/key 向量。simulation 使用模拟算法；online 下载要求明确提供已授权的外部 DLL，缺少 DLL 时不启动下载。

通用外部提供者标识为 external-generatekeyex。旧配置需要在下载设置中重新选择提供者并保存。DLL 相对路径以 EXE 目录解析。可选桥接程序位于 seedkey/SeedkeyBridge32.exe，使用 GenerateKeyEx 接口；接口位数、参数约定和输入长度限制必须与使用者提供的 DLL 兼容。

为保护本地目标参数，请将实机配置、算法 DLL 和验证记录保存在 private/ 或仓库外。公开打包脚本不携带这些资料。

## 发布自检

使用清洁构建生成公开包。在解压目录运行：

```powershell
.\VerifyRelease.exe -o verification-results.xml,xml
```

自检只验证启动、模拟下载和配置保存，不调用私有算法或物理总线。输出中可能包含本机路径，不应直接上传。

## 硬件模式

硬件通信还需要匹配的 PEAK 驱动及授权通信模块。online LIN 下载前需配置实际 NAD、会话、安全级别、时序、例程和镜像地址。CAN 下载目前仅支持 simulation。默认值和模拟通过结果不能替代实机适配与验证。

配置初值位置见 [默认配置](Default-Configuration.md)。

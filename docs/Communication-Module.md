# 共享通讯模块

源码位于 communication-provider/resource/communication。两个工程直接编译同一份 driverCan、driverLin 及连接模块；不复制两套维护中的驱动代码。

## 数据与职责

| 类型 | 职责 |
|---|---|
| HardwareChannel | bus、SDK 句柄、设备编号、SDK 通道编号、身份 key、可用性、身份是否可用于持续识别。 |
| SoftwareChannelConfiguration | 软件通道 ID、硬件绑定、波特率（统一 bit/s）、LIN 模式、重连策略、独立 UDS / transport 配置。 |
| UdsConfiguration | Profile ID、P2/P2*、TesterPresent、待响应总超时、编程会话、安全级别。协议阶段再扩充地址、例程等目标参数。 |
| HardwareBackend | 同一接口的 scan / open / close / health，隔离 CAN、LIN SDK。 |
| SoftwareChannel | 周期检测、连接状态、进程内硬件占用、失败回滚、连接代次、重连意图和状态通知。 |

CAN 与 LIN 的 UDS 配置是各实例的值对象，没有共享可变全局 Profile。此模块不实现 UDS 服务、镜像或刷写业务。HardwareChannel.controller 显示 SDK 实际编号，不按控件索引推导；LIN 原始发送接受 6 位帧 ID，由 SDK 生成 PID 和校验，0x3C/0x3D 强制 Classic checksum。

## 状态与资源

- 默认每 500 ms poll。一次查询失败保留上次列表；第二次失败清除可用列表，并关闭活动连接。启动时查询失败直接给出空列表和故障信息。
- connectionClosing 在 SDK 释放前触发，消费者应在该信号中停止发送、调度、协议队列和任务。
- generation 在连接、断开、丢失时递增。后续异步业务需携带 generation，并在交付结果时比较当前代次。
- Connected 仅表示通道已初始化；总线是否休眠、VBAT 是否缺失、ECU 是否有应答分别处理。禁止把“长期无报文”直接视为 USB 掉线。
- configure 只允许离线且非操作中的通道。无效配置不覆盖旧配置；调用方必须检查返回值，不得在配置失败后继续连接。
- 连接失败释放已获取资源；手动断开取消自动重连意图；重复断开幂等。两个软件通道不能占用相同总线的同一 SDK 句柄。
- 自动重连需有可靠的持续身份；SDK 返回序列号 0 时使用运行期 key，自动发现后等待手动连接。存在身份冲突时不猜测设备。
- 使用 PLIN 时，先检查已有客户端；不会重置他人使用的硬件。拔出后也会尝试 DisconnectClient 和 RemoveClient，随后清空本地句柄。
- PLIN DLL 保留到进程退出，以避开实测 PLIN-API 3.1.x 动态卸载在调试器下的 INVALID_HANDLE 异常。保持 DLL 映射不等于保持设备连接：客户端、调度及硬件资源仍在每次断开时释放。更新 DLL 后需重启应用。

## 线程与所有权

所有同一 SoftwareChannel、backend 和 driver 的调用必须在同一线程串行执行。SoftwareChannel 的 QTimer 属于其 QObject，可在定时器启动前随对象移动到 worker；驱动适配器是普通 C++ 对象，调用方负责线程归属。

PeakCanBackend / PeakLinBackend 借用驱动引用，SoftwareChannel 拥有 backend。驱动的生命期必须长于 backend / SoftwareChannel。驱动默认拥有 SDK 包装器；通过构造参数注入的测试 SDK 由调用方管理。析构顺序应为消费者停止 → 软件通道释放 → 驱动析构 → 外部 SDK 析构。

旧工程维持原来的主线程串行调用模型；阶段 2 的新 Bootloader UI 已将硬件操作放到每通道的独立 worker，后续协议沿用该线程归属。阶段 1 未把旧工程全部业务迁移到线程，也没有引入阻塞等待型协议下载循环。


## 旧界面接入

硬件下拉框存储 SDK 句柄和身份 key，显示文字不参与绑定解析。下拉框始终允许“未绑定硬件通道”；用户明确取消绑定后，周期发现不能擅自重新选择。只有一个 PLIN 时选用旧业务的 LIN2，LIN1 保持未绑定；原有 LIN1 依赖 LIN2 的规则继续有效。连接按钮只操作当前选择的通道，全部已选择通道连接后提供统一断开。新 Bootloader 页面会直接展示独立软件通道，不沿用旧 AVM 双 LIN 业务约束。

## 阶段 2 帧头验证补充

sendRaw 支持 dirSubscriberAutoLength，用于请求帧头并识别未知长度响应；仅 Publisher 计算发送校验，Subscriber 不发送数据字段。独立扫描工具 plin_scan 及新 LIN 页复用这一接口，0x3D 仍强制 Classic checksum。无应答与校验 / 总线错误在界面分别显示，详见主工程的 LIN-Frame-Validation.md。

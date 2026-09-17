# View 初始配置的唯一来源

ChannelPage 对应的初值头文件：src/views/ChannelPageDefaults.h。
MainWindow 对应的初值头文件：src/views/MainWindowDefaults.h。

ChannelSettings 继承 ChannelPageInitialValues，不再复制字段初值；创建通道时，ViewModel 接收 ChannelSettings，Model 将同一值传给 worker，View 从 ViewModel 的 settings 加载全部控件。下载设置保存后经过同一配置对象传入 worker，已连接的通道无需重新初始化即可修改任务选项。

| 修改内容 | 位置 |
|---|---|
| CAN / LIN 名称、波特率、ID / NAD、在线或模拟初始模式 | ChannelPageInitialValues 普通成员及 linName / linProfile / linBitrate / defaultSimulation |
| P2、P2*、保活使能及周期、编程 / 安全级别 | ChannelPageInitialValues 普通成员 |
| 镜像路径、BIN 基址、Driver 使能、重复下载参数 | ChannelPageInitialValues 普通成员 |
| App 初始流程、反馈框、RID / DID、连续帧字节上限、复位等待、27 DLL 路径 | initialDownloadProfile() 及 feedbackChecked |
| CAN 网络层选项 | canNetwork |
| 报文跟随、文本缓存数、耗时刷新周期 | followFrames / logCapacity / elapsedRefreshMs |
| 主窗口尺寸、字体、标题、版本和默认配置文件路径 | MainWindowInitialValues |

修改并重新编译后，新建通道、ViewModel、Model / worker 自动使用这些初值。已有配置文件显式保存的参数会覆盖初值，避免更新程序时改写用户目标配置；缺省的普通参数使用当前初值。旧配置缺失流程 / 反馈字段时迁移为 APP 和当前 feedbackChecked 初值。

协议库的 FlashProfile / CanOptions / SessionOptions 保留独立协议层默认值，供非 UI 调用及历史协议测试使用；实际页面运行时由 ChannelSettings 提供参数。协议层不依赖 QWidget，默认配置头只包含 Qt Core 类型与步骤元数据。

构建使用 stage6-qt6 / stage6-qt5 预设。Windows 图标在 resources/app.rc 中引用根目录 Bootloader.ico，同时 Qt 窗口通过 qrc 使用同一文件。

# General Bootloader Controller

基于 Qt 的 CAN / LIN 诊断工作台，支持通道管理、报文显示和 UDS 模拟下载。

此仓库不包含私有安全访问算法、真实 seed/key 向量、目标实机配置或内部验证报告。公开配置和 fixtures 仅用于模拟，不代表任何实机参数。

## 构建与验证

需要 Qt、MinGW 和另行提供的通信模块（定义 `peak_communication` 目标及 `peak_deploy` 函数）。配置时指定该模块目录：

```powershell
cmake --preset stage6-qt6 -DCOMMUNICATION_SOURCE_DIR="<通信模块目录>"
cmake --build --preset stage6-qt6
ctest --preset stage6-qt6
```

Qt 5 使用 `stage6-qt5`。本机工具链位置在 CMakePresets.json 中配置。

默认不构建安全访问桥接程序，也不复制算法 DLL。需要通用 32 位桥接程序时，显式设置 `BUILD_SEEDKEY_BRIDGE=ON` 和 `SEEDKEY_CXX32=<32位编译器路径>`；算法及其运行依赖由使用者从授权渠道单独提供。

公开打包脚本不收集算法目录、内部验证日志或源机器路径。旧构建产物可能含已撤下的资料，应使用清洁构建生成新包。

- [使用说明](docs/User-Guide.md)
- [默认配置](docs/Default-Configuration.md)
- [通信模块说明](docs/Communication-Module.md)

`private/`、`build/` 和 `dist/` 是本地目录，不应上传或直接整体分享。

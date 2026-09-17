# 模拟镜像
这些文件是程序生成的测试数据，不能刷入真实 ECU。
- application.bin：4096 字节，测试基址 0x0000FF00。
- flash-driver.bin：256 字节，测试基址 0x10000000。
- application-sparse.hex：48 字节有效数据，两个段 0x00010020 / 0x00010100；HEX 内地址优先，不填充地址间隙。
载入 ../stage3-lin-simulation.json 后，在 LIN01 中连接模拟通道并开始模拟下载。配置含本机绝对镜像路径，移动工程后请重新选择文件。

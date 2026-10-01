# Qt多线程TCP工业设备监控系统

本系统采用“两套独立Qt工程＋共享协议库”的结构：

```text
TcpMonitorSystem/
├─ CommonProtocol/          共享静态协议库及协议单元测试
├─ TcpDeviceMonitorClient/  独立监控客户端工程
├─ DeviceSimulator/         独立设备模拟器工程
└─ tests/                   系统TCP集成测试
```

## 同时运行

1. 第一个Qt Creator窗口打开`DeviceSimulator/CMakeLists.txt`并运行模拟器。
2. 第二个Qt Creator窗口打开`TcpDeviceMonitorClient/CMakeLists.txt`并运行客户端。
3. 客户端连接`127.0.0.1:45454`。

两个程序属于不同工程和不同运行会话，启动或调试其中一个不会关闭另一个。

## 测试层次

- `CommonProtocol`：CRC、协议编解码、半包、粘包和损坏包恢复测试。
- `TcpDeviceMonitorClient`：SQLite遥测和报警写入测试。
- 根目录：真实服务端与客户端工作对象之间的TCP集成测试。

## 整改验证

Windows / Qt MinGW环境可运行 `./scripts/verify.ps1 -LifecycleCycles 100`，独立构建两端应用并执行协议、网络、数据库和窗口生命周期测试。Qt与工具路径可以通过脚本参数指定。

运行时状态栏显示待存储数量、峰值、成功/失败样本数、过载丢弃数和最慢写入耗时。验证范围及限制见 [VALIDATION.md](VALIDATION.md)。

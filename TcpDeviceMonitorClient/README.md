# TCP设备监控客户端

这是独立的Qt客户端工程。它通过网络工作线程连接设备模拟器，完成心跳、自动重连、协议解析、状态显示、命令下发、实时曲线和SQLite异步存储。

## 运行

1. 用一个Qt Creator窗口打开本目录的`CMakeLists.txt`。
2. 选择Qt 6.10.3 MinGW套件并运行`TcpDeviceMonitor`。
3. 连接`127.0.0.1:45454`。

设备模拟器位于同级的`DeviceSimulator`工程，应在另一个Qt Creator窗口中运行。

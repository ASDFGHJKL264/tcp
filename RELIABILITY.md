# TCP 可靠性优化与验证

保留监控端、模拟器两套独立 Qt 工程，以及 CommonProtocol 共享库。

## 命令结果

- 每台设备最多一条待确认命令；使用序号与设备 ID 匹配 ACK，忽略重复、不匹配或非法 ACK。
- ACK 等待 3 秒；接受后等待后续遥测状态 5 秒。只有两者均满足才报告确认成功。
- 启动要求 Running 且无故障；停止要求 Stopped；复位要求 Stopped 且故障码为零。
- 超时和断线表示未确认/结果未知，不保证命令未执行。控制指令不自动重发。
- 模拟器故障未复位时拒绝启动。协议不提供执行事务 ID，遥测确认表示观察到预期状态，不是物理动作的强事务证明。

## 连接和退出

- 连接中、连接成功、等待重连时均可使用原按钮取消/断开。
- 主动断开先关闭重连意图和定时器，再 abort；切换服务器先清理旧会话。
- 单次连接等待 5 秒，重连间隔 1、2、4、8、16、30 秒，之后保持 30 秒。
- 心跳周期 2 秒，匹配心跳 ACK；超过 6.5 秒未确认，在下一次心跳检查中断开。
- 网络/数据库对象在所属线程清理；等待线程真正结束后再销毁 QThread，不使用 terminate。

## 资源限制

- Socket 读缓冲 128KiB，每次事件处理 64KiB；剩余数据交还事件循环后继续读。
- 协议缓存上限约 1.06MiB，超限丢弃并报错；校验 CRC、类型、长度及有限浮点数。
- 发送积压约 64KiB 时断开慢连接；模拟器最多 16 个客户端。
- 两端界面日志最多 2000 段；监控端文件日志 5MiB 轮转，最多 10 个文件。轮转失败时跳过文件写入。

## 自动化测试

Qt Creator 可分别打开：

1. 根目录 CMakeLists.txt：NetworkIntegrationTests、ReliabilityTests。
2. TcpDeviceMonitorClient/CMakeLists.txt：监控端、DatabaseIntegrationTests、WindowLifecycleTests。
3. CommonProtocol/CMakeLists.txt：MonitorProtocolTests。
4. DeviceSimulator/CMakeLists.txt：模拟器。

测试属于独立可执行程序；正常使用选择 TcpDeviceMonitor 或 DeviceSimulator，不选择测试目标。
在各自构建目录运行 ctest --output-on-failure。需 Qt Core/Widgets/Network/Sql/Test、SQLite 插件及相应编译器运行库。

新增测试覆盖：错序/错设备/重复 ACK、重复控制拦截、ACK 和状态各自超时、拒绝命令、断线取消待处理命令、重连与取消、连接中取消、心跳丢失、缓存超限及 CRC 恢复、NaN 拒绝、实际 QThread 清理、主窗口带连接反复关闭。

## 边界

本次本机验证：Qt 6.10.3 / MinGW 13.1 / Windows；监控端、模拟器独立编译成功。NetworkIntegrationTests、ReliabilityTests、DatabaseIntegrationTests、WindowLifecycleTests、MonitorProtocolTests 均通过。窗口生命周期测试使用 offscreen 平台，不代表人工视觉验收。

最终窗口测试改为显式注入临时数据目录（正式运行默认路径不变），连续 10 轮通过，每轮包含 3 次带连接的创建/关闭。可靠性测试报告位于 build/reliability/reliability-results.txt，共 11 项通过（包含初始化、清理及数据驱动用例）。

这是本机自动化验证，不等于长期现场验收。日志文件写入仍在 UI 线程；数据库事件队列尚未加入全链路背压，异常高频服务端仍需额外压力测试。SQLite 保留策略与设备实际动作确认需按业务继续设计。未承诺工业级可靠性。

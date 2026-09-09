# CommonProtocol

客户端和模拟器共同使用的静态协议库，只维护一份协议实现。

协议帧包含：包头、版本、消息类型、序号、设备ID、负载长度、负载和CRC16。
`StreamParser`负责半包缓存、粘包拆分、垃圾字节跳过和CRC错误恢复。

可以单独用Qt Creator打开本目录的`CMakeLists.txt`并运行`MonitorProtocolTests`。

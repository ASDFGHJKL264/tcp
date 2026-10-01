# Windows 发布与安装

发布版本：2026.09.30。安装包包含监控客户端和设备模拟器，使用 Release 构建，并部署 Qt、MinGW 运行库及 SQLite 插件。

双击安装包，默认安装至 `D:\TCP工业设备监控系统`，创建两个桌面快捷方式。先打开 TCP 设备模拟器，再打开监控客户端连接 `127.0.0.1:45454`。便携 ZIP 解压后也可直接运行两个 exe。

更新前关闭程序。安装脚本校验 SHA-256，覆盖前将同名旧程序文件备份到安装目录的 `_update-backups`；不删除已有数据、日志或用户配置。客户端数据仍采用系统 AppData 路径，更新不会迁移或清除这些数据。安装包不包含个人数据库、日志、Qt Creator 本机配置。

复现打包：先运行 `scripts/verify.ps1 -LifecycleCycles 100`，再执行 `python scripts/package.py`。工具路径可通过 `--qt-prefix`、`--compiler-bin`、`--cmake`、`--ninja`、`--sevenzip`、`--rar` 指定。打包需要已有 7-Zip 与 WinRAR SFX 模块，不会自动安装工具。

每次打包生成独立 `dist/release-*`，`dist/latest-release.json` 记录产物路径及哈希。`release-manifest.json` 保存源码文件哈希和构建信息，`SHA256.json` 校验运行文件。解出的 `install.ps1` 支持 `-InstallDirectory`、`-NoShortcuts`、`-Quiet`，用于隔离目录验证。当前为本地自解压安装器，没有卸载注册项或代码签名，不等同现场长期验收。

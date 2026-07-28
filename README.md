# XiaoMiao VPS Monitor

面向学而思小喵 ESP32 掌机的 VPS 状态监控固件。发布包同时提供
[bmorcelli/Launcher](https://github.com/bmorcelli/Launcher) 共存安装和完整 4 MB
直刷安装；已有 Launcher 的设备优先使用共存包。

当前支持以下监控后端：

- Nezha Monitor v1：`GET /api/v1/server`
- CF-Server-Monitor-Pro：`GET /api/server?id=...`

基础版已在 ESP32-WROVER-B 小喵掌机上完成 Launcher 启动、NVS 配置、2.4 GHz Wi-Fi、
HTTPS 证书校验和哪吒接口实机验证。`v1.1.0` 在此基础上增加巡检交互和扩展指标；
发布前已完成构建和镜像静态验证，实机交互验证状态见下文。

## 功能

- 总览最多显示 32 台服务器，明确高亮当前选择并显示在线数量
- 显示 CPU、内存、磁盘、Swap、1/5/15 分钟负载和运行时间
- 显示进程、TCP/UDP 连接、温度、虚拟化、实时速度和累计流量
- CF-Server-Monitor-Pro 显示月流量、套餐额度、重置日和到期天数
- CPU、内存、磁盘和离线状态支持持续超限本地告警
- 配置网页可测试 Wi-Fi、URL、TLS/HTTP、认证、JSON 和节点发现，不保存配置
- 总览与详情分离，刷新后按服务器 ID 保持当前选择
- 使用设备热点和手机网页完成首次配置
- Wi-Fi、面板地址和 Token 保存到 ESP32 NVS，不依赖 TF 卡数据分区
- HTTPS 使用公共根证书包校验证书，不调用 `setInsecure()`
- 禁止自动跟随 HTTP 重定向，避免把监控凭据转发到其他主机

## 硬件与固件环境

- ESP32-WROVER-B，4 MB Flash，8 MB PSRAM
- ST7735 128 x 160 TFT
- 六键输入：方向键、A、B
- 可选的 bmorcelli/Launcher 多应用环境
- Arduino-ESP32 2.0.17 / ESP-IDF 4.4

引脚配置位于 [`include/hardware.h`](include/hardware.h) 和
[`include/User_Setup.h`](include/User_Setup.h)。不同批次硬件请先核对屏幕控制器和引脚，
不要直接假定兼容。

## 安装

从 [v1.1.0 Release](https://github.com/sym090701/xiaomiao-vps-monitor/releases/tag/v1.1.0)
选择与安装方式匹配的 BIN：

| 安装方式 | 发布包 | 是否保留 Launcher | 写入方式 |
| --- | --- | --- | --- |
| Launcher 共存（推荐） | [`XiaoMiao-VPS-Monitor-v1.1.0-Launcher.bin`](https://github.com/sym090701/xiaomiao-vps-monitor/releases/download/v1.1.0/XiaoMiao-VPS-Monitor-v1.1.0-Launcher.bin) | 是 | TF 卡 `/boot/` |
| 完整 4 MB 直刷 | [`XiaoMiao-VPS-Monitor-v1.1.0-Full-4MB.bin`](https://github.com/sym090701/xiaomiao-vps-monitor/releases/download/v1.1.0/XiaoMiao-VPS-Monitor-v1.1.0-Full-4MB.bin) | 否 | esptool，地址 `0x0` |

两个 BIN 不能混用刷写方式。Release 同时提供 `SHA256SUMS`，下载后应先核验文件。

```text
8593144d6ce0767429302eae4ec2c9513230a4b13aab6239061f4dcb821febf8  XiaoMiao-VPS-Monitor-v1.1.0-Launcher.bin
1ff98be69dee433389e83d041c602a6105ebb896c90bc71b4ce715fdd9c11a8b  XiaoMiao-VPS-Monitor-v1.1.0-Full-4MB.bin
```

### Launcher 共存安装

1. 下载 `XiaoMiao-VPS-Monitor-v1.1.0-Launcher.bin`。
2. 将 BIN 复制到 TF 卡 `/boot/`。
3. 开机进入 Launcher，打开 `/boot/`，选择 BIN 并确认安装。
4. 首次启动后按屏幕提示完成配网。

> [!WARNING]
> 共存包中的 BIN 是应用镜像。不要用 esptool 将它写入 `0x0` 或 `0x10000`，否则会
> 覆盖启动程序或 Launcher。`v1.1.0` 至少需要 `0x120000`（1152 KiB）应用槽位；
> 当前 BIN 为 `1131904` 字节，在该槽位剩余 `47744` 字节；其他设备以实际 Launcher
> 分区管理结果为准。

### 完整 4 MB 直刷

完整直刷 BIN 不需要 TF 卡或 Launcher，适合把掌机专门用作 VPS 监控屏。它会覆盖 bootloader、
分区表、NVS、Launcher、已安装应用和 Flash 内的其他数据。

1. 安装 [esptool](https://docs.espressif.com/projects/esptool/en/latest/esp32/installation.html)：

   ```bash
   python3 -m pip install --user esptool
   ```

2. 先完整备份原有 4 MB Flash（将串口名替换为实际设备）：

   ```bash
   python3 -m esptool --chip esp32 --port /dev/cu.usbmodemXXXX --baud 115200 \
     read_flash 0x0 0x400000 backup-original-4MB.bin
   ```

3. 下载完整镜像后写入 `0x0`：

   ```bash
   python3 -m esptool --chip esp32 --port /dev/cu.usbmodemXXXX --baud 115200 \
     write_flash --flash_size 4MB 0x0 XiaoMiao-VPS-Monitor-v1.1.0-Full-4MB.bin
   ```

   Windows 将 `python3` 和串口名分别替换为 `py`、`COM3`。

4. 首次启动后按屏幕提示完成配网。

恢复原备份：

```bash
python3 -m esptool --chip esp32 --port /dev/cu.usbmodemXXXX --baud 115200 \
  write_flash --flash_size 4MB 0x0 backup-original-4MB.bin
```

> [!CAUTION]
> 只有完整 4 MB 文件 `XiaoMiao-VPS-Monitor-v1.1.0-Full-4MB.bin` 才能从 `0x0` 写入。
> 刷写前确认备份文件大小为 `4194304` 字节，并将备份另存到电脑或其他磁盘。

### 发布包验证状态

- `v1.1.0` Launcher BIN 与本仓库标签源码构建结果一致，镜像结构和槽位尺寸已验证；
  新交互尚未记录实体机验证。
- 完整 4 MB 镜像已验证尺寸、组件偏移、分区表、SHA256 和无预置凭据；该完整镜像
  尚未执行破坏性的实机整片刷写。
- CF-Server-Monitor-Pro 支持已完成源码、构建和接口契约检查，尚未记录实机后端验证。

## 首次配置

首次启动会创建 `XiaoMiao-VPS-xxxxxx` 热点。热点密码显示在掌机屏幕上，连接后访问：

```text
http://192.168.4.1
```

填写 2.4 GHz Wi-Fi、后端类型、面板根地址、Token、刷新间隔和告警阈值，保存后设备自动重启。
“测试当前连接”只执行诊断，不保存配置或重启，也不会回显已保存的密码和 Token。
长按 B 约 1.5 秒可随时重新开启配置热点。

### Nezha Monitor

1. 在哪吒面板创建 API Token / PAT。
2. Token 只授予 `nezha:inventory:read` 权限。
3. 面板地址填写根地址，例如 `https://status.example.com`，不要附加
   `/api/v1/server`。
4. 在 Token 栏填写 PAT。固件会发送 `Authorization: Bearer <PAT>`。

### CF-Server-Monitor-Pro

1. 填写 Worker 根地址。
2. 公开面板的 Token 栏留空；私有面板填写部署时的 `API_SECRET`。
3. 私有面板使用 Basic Auth 的 `admin:API_SECRET` 请求。
4. 自动发现节点失败时，从详情页 URL 的 `?id=...` 取得节点 ID，多个 ID 用逗号分隔。

CF-Server-Monitor-Pro 当前没有节点列表 JSON API，显式填写 ID 更稳定。

## 按键

| 按键 | 功能 |
| --- | --- |
| 总览：上 / 下（左 / 右也可） | 选择服务器，到首尾停止 |
| 总览：A | 进入所选服务器详情 |
| 总览：短按 B | 立即刷新 |
| 详情：左 / 右 | 循环切换服务器 |
| 详情：上 / 下 | 切换资源、网络、高级指标和可用的 CF 套餐页 |
| 详情：A | 立即刷新 |
| 详情：短按 B | 返回总览 |
| 长按 B 1.5 秒 | 开启配置热点 |
| 长按 A+B 1.5 秒 | 重启；共存安装可随后按 Launcher 提示返回主界面 |

## 编译

安装 [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html)，
在项目目录执行：

```bash
pio run
```

普通构建生成 Launcher 可安装的应用镜像：

```text
.pio/build/xiaomiao/firmware.bin
```

它不是完整 4 MB 直刷镜像，不能直接写入 `0x0`。依赖版本固定在
[`platformio.ini`](platformio.ini)。构建前建议确认 BIN 不超过目标 Launcher 应用槽位：

```bash
stat -f '%z bytes' .pio/build/xiaomiao/firmware.bin  # macOS
stat -c '%s bytes' .pio/build/xiaomiao/firmware.bin  # Linux
```

同时生成发布使用的 Launcher BIN、完整 4 MB BIN 和 `SHA256SUMS`：

```bash
./scripts/build-release.sh
```

默认输出到 `dist/`。完整镜像按以下布局合并并以 `0xFF` 填充到 4 MiB：

```text
0x001000  bootloader.bin
0x008000  partitions.bin
0x00E000  boot_app0.bin
0x010000  firmware.bin
```

## TLS 证书包

[`data/cert/x509_crt_bundle.bin`](data/cert/x509_crt_bundle.bin) 来自 `certifi` 公共 CA 集，
采用 Arduino-ESP32 2.0.17 所需的 ESP-IDF 4.4 格式：

```text
2 字节大端证书数量 + 顺序证书记录
```

ESP-IDF 5.x 使用 32 位小端偏移表，不能直接传给此版本的 `setCACertBundle()`；格式错误
会导致越界解析和看门狗重启。更新证书包时必须使用与目标 Arduino-ESP32 版本匹配的生成器。

## 安全与限制

- HTTP 明文地址仍可使用，但会暴露 Token，不建议。
- 配置网页不会回显已经保存的 Wi-Fi 密码或 Token。
- 未启用 Flash Encryption 的 ESP32 不能防止物理读取 NVS；请使用最小权限 Token。
- 单次 HTTP 响应上限为 512 KiB。
- 哪吒在线状态依据 `last_active`，CF 在线状态依据 `last_updated` 和离线阈值。
- CF 公开首页结构变化可能影响节点自动发现，此时应手动填写节点 ID。

## 目录

```text
include/       硬件、配置和数据结构声明
src/           显示、配网、NVS 和后端客户端实现
data/cert/     HTTPS 公共根证书包
scripts/       两种发布 BIN 的可复现构建脚本
platformio.ini 固定的构建环境和依赖
```

## 故障排查

- 保存后重新进入配置热点：检查串口是否已取得局域网 IP；如果已连接，问题通常发生在
  HTTPS 或后端请求阶段，而不是配网。
- `Invalid mbox`：必须在启动 WebServer 前初始化 Arduino Wi-Fi/LwIP。
- `TG1WDT_SYS_RESET` 出现在 `setCACertBundle()`：检查证书包是否误用了 ESP-IDF 5.x 格式。
- 共存安装无法返回 Launcher：重启后按照 Launcher 启动画面提示按键，不要重刷整机镜像。
- 直刷后没有 Launcher：这是完整 4 MB 包的预期行为；如需恢复，写回刷机前的 4 MB 备份。

## 后端项目

- [Nezha Monitor](https://github.com/nezhahq/nezha)
- [CF-Server-Monitor-Pro](https://github.com/a63414262/CF-Server-Monitor-Pro)

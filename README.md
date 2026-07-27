# XiaoMiao VPS Monitor

面向学而思小喵 ESP32 掌机的 VPS 状态监控固件。发布包同时提供
[bmorcelli/Launcher](https://github.com/bmorcelli/Launcher) 共存安装和完整 4 MB
直刷安装；已有 Launcher 的设备优先使用共存包。

当前支持以下监控后端：

- Nezha Monitor v1：`GET /api/v1/server`
- CF-Server-Monitor-Pro：`GET /api/server?id=...`

Launcher 应用已在 ESP32-WROVER-B 小喵掌机上完成实机验证，包括 Launcher 启动、
NVS 配置、2.4 GHz Wi-Fi、HTTPS 证书校验和哪吒接口请求。

## 功能

- 在 128 x 160 TFT 上显示 CPU、内存、磁盘、负载、运行时间和在线状态
- 显示实时上下行速度、累计流量、系统和架构信息
- 最多加载 32 台服务器
- 左右切换节点，上下切换资源页和网络页
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

从 [v1.0.0 Release](https://github.com/sym090701/xiaomiao-vps-monitor/releases/tag/v1.0.0)
选择与安装方式匹配的 ZIP：

| 安装方式 | 发布包 | 是否保留 Launcher | 写入方式 |
| --- | --- | --- | --- |
| Launcher 共存（推荐） | [`xiaomiao-vps-monitor-launcher-v1.0.0-2026-07-28.zip`](https://github.com/sym090701/xiaomiao-vps-monitor/releases/download/v1.0.0/xiaomiao-vps-monitor-launcher-v1.0.0-2026-07-28.zip) | 是 | TF 卡 `/boot/` |
| 完整 4 MB 直刷 | [`xiaomiao-vps-monitor-direct-flash-v1.0.0-2026-07-28.zip`](https://github.com/sym090701/xiaomiao-vps-monitor/releases/download/v1.0.0/xiaomiao-vps-monitor-direct-flash-v1.0.0-2026-07-28.zip) | 否 | esptool，地址 `0x0` |

两个包不能混用刷写命令。解压后先阅读包内 `README.txt`，并使用 `SHA256SUMS`
核验内部文件。

ZIP SHA256：

```text
616b3e4e6d2fcb3ca4069638f00b02c241491b3b514f2960233cc56c14c86b7a  xiaomiao-vps-monitor-launcher-v1.0.0-2026-07-28.zip
70ad1842c3be59149516e7e483d833037a95f5264121c5c9baa39c65de2c9ddf  xiaomiao-vps-monitor-direct-flash-v1.0.0-2026-07-28.zip
```

### Launcher 共存安装

1. 下载并解压 Launcher 共存包。
2. 将 `TF-card_boot` 内的 BIN 复制到 TF 卡 `/boot/`。
3. 开机进入 Launcher，打开 `/boot/`，选择 BIN 并确认安装。
4. 首次启动后按屏幕提示完成配网。

> [!WARNING]
> 共存包中的 BIN 是应用镜像。不要用 esptool 将它写入 `0x0` 或 `0x10000`，否则会
> 覆盖启动程序或 Launcher。当前实机所需应用槽位为 `0x110000`；其他设备以实际
> 分区表为准。

### 完整 4 MB 直刷

直刷包不需要 TF 卡或 Launcher，适合把掌机专门用作 VPS 监控屏。它会覆盖 bootloader、
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

3. 解压直刷包，在其目录运行：

   ```bash
   ./flash.sh /dev/cu.usbmodemXXXX
   ```

   Windows 使用 `flash-windows.bat COM3`。也可以手动将
   `XiaoMiao-VPS-Monitor-Full-4MB.bin` 写入 `0x0`；不要把其中的
   `flash_parts/firmware.bin` 单独写入 `0x0`。

4. 首次启动后按屏幕提示完成配网。

恢复原备份：

```bash
python3 -m esptool --chip esp32 --port /dev/cu.usbmodemXXXX --baud 115200 \
  write_flash --flash_size 4MB 0x0 backup-original-4MB.bin
```

> [!CAUTION]
> 只有完整 4 MB 文件 `XiaoMiao-VPS-Monitor-Full-4MB.bin` 才能从 `0x0` 写入。
> 刷写前确认备份文件大小为 `4194304` 字节，并将备份另存到电脑或其他磁盘。

### 发布包验证状态

- Launcher 应用 BIN 与本仓库 `v1.0.0` 源码构建结果一致，并已完成实机启动及哪吒后端验证。
- 完整 4 MB 镜像已验证尺寸、组件偏移、分区表、内部 SHA256 和无预置凭据；该完整镜像
  尚未执行破坏性的实机整片刷写。
- CF-Server-Monitor-Pro 支持已完成源码、构建和接口契约检查，尚未记录实机后端验证。

## 首次配置

首次启动会创建 `XiaoMiao-VPS-xxxxxx` 热点。热点密码显示在掌机屏幕上，连接后访问：

```text
http://192.168.4.1
```

填写 2.4 GHz Wi-Fi、后端类型、面板根地址、Token 和刷新间隔，保存后设备自动重启。
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
| 左 / 右 | 切换 VPS |
| 上 / 下 | 切换资源页、网络页 |
| A | 立即刷新 |
| 长按 B 1.5 秒 | 开启配置热点 |
| 长按 A+B 1.5 秒 | 重启；共存安装可随后按 Launcher 提示返回主界面 |

## 编译

安装 [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html)，
在项目目录执行：

```bash
pio run
```

源码编译生成的是 Launcher 可安装的应用镜像：

```text
.pio/build/xiaomiao/firmware.bin
```

它不是完整 4 MB 直刷镜像，不能直接写入 `0x0`。依赖版本固定在
[`platformio.ini`](platformio.ini)。构建前建议确认 BIN 不超过目标 Launcher 应用槽位：

```bash
stat -f '%z bytes' .pio/build/xiaomiao/firmware.bin  # macOS
stat -c '%s bytes' .pio/build/xiaomiao/firmware.bin  # Linux
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

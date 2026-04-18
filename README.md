# USB Network Bridge for ESP32-P4

本项目将 ESP32-P4 作为 **USB NCM 网卡设备** 接到电脑，由电脑侧网络共享（ICS）提供 DHCP/IP，从而让 ESP32-P4 通过电脑的 Wi-Fi 出网。

---

## 1. 项目介绍

### 1.1 目标

- 使用 ESP32-P4 的 USB OTG（GPIO39/40）模拟一块标准 USB 网卡（NCM）
- 电脑识别后，把 Wi-Fi 网络共享给这块 USB 网卡
- ESP32-P4 获取 IP 后，可直接使用 ESP-IDF 常规网络能力（HTTP/MQTT/TCP/UDP/HTTPS 等）

### 1.2 核心特性

- 基于 `esp_tinyusb` + `tinyusb_net`
- 基于 `esp_netif` + lwIP 建立网络收发路径
- DHCP 客户端自动从电脑侧获取 IPv4 地址
- 无需编写 PC 专用驱动（使用 USB 标准类驱动）

---

## 2. 代码原理（重点）

### 2.1 整体数据路径

1. **USB 枚举阶段**  
   `tinyusb_driver_install()` 启动 USB 设备栈，向主机提供 NCM 类描述符。  
   电脑将其识别为“以太网适配器（UsbNcm Host Device / 以太网 X）”。

2. **主机 -> ESP32-P4（下行）**  
   主机发来的以太帧进入 `usb_recv_callback()`，再通过 `esp_netif_receive()` 送入 lwIP。

3. **ESP32-P4 -> 主机（上行）**  
   lwIP 发包时调用 netif transmit 回调 `netif_transmit()`，再通过 `tinyusb_net_send_sync()` 发回 USB 主机。

4. **DHCP 获取 IP**  
   USB 链路 attach 后，ESP 侧 DHCP 客户端向主机共享网络请求地址；  
   成功后触发 `IP_EVENT_ETH_GOT_IP`，日志打印 `DHCP acquired: ...`。

### 2.2 关键设计点

- **双 MAC 设计**：USB NCM MAC 与 lwIP netif MAC 必须不同（代码中已处理），避免二层冲突。
- **事件驱动**：  
  - USB attach/detach：`usb_event_handler()`  
  - IP 获取/丢失：`ip_event_handler()`
- **成功状态标志**：`usb_wifi_bridge_is_connected()` 返回是否已拿到 IPv4。

---

## 3. 主要代码说明

### 3.1 文件结构

```text
USB_WIFI/
├── components/
│   └── usb_wifi_bridge/
│       ├── include/usb_wifi_bridge.h
│       ├── usb_wifi_bridge.c
│       └── CMakeLists.txt
├── main/
│   ├── USB_WIFI.c
│   └── CMakeLists.txt
└── README.md
```

### 3.2 关键函数（`components/usb_wifi_bridge/usb_wifi_bridge.c`）

- `usb_wifi_bridge_init()`  
  初始化 `esp_netif`、事件循环、TinyUSB 设备栈，创建并启动 USB 网络接口。

- `init_netif()`  
  创建 esp-netif + lwIP 绑定，注册 IP 事件，初始化 NCM 网络类。

- `usb_recv_callback()`  
  接收来自主机的数据帧并转发到 lwIP。

- `netif_transmit()`  
  将 lwIP 发出的数据帧转发到 USB 主机。

- `usb_event_handler()`  
  处理 USB attach/detach。

- `ip_event_handler()`  
  处理 DHCP 成功/丢失事件，输出关键日志并维护连接状态。

### 3.3 应用入口（`main/USB_WIFI.c`）

- `app_main()` 调用 `usb_wifi_bridge_init()` 启动桥接，并打印运行提示。

---

## 4. 使用方法

### 4.1 编译烧录

```bash
idf.py set-target esp32p4
idf.py build
idf.py flash monitor
```

### 4.2 硬件连接

- USB OTG 口：GPIO39 (D-) / GPIO40 (D+)
- 建议两根线：
  - 一根用于串口日志/烧录
  - 一根用于 USB OTG 数据（NCM 网卡）

### 4.3 Windows 网络共享（ICS）

1. 打开 `ncpa.cpl`（网络连接）
2. 找到正在联网的 Wi-Fi 适配器 -> 属性 -> 共享
3. 勾选“允许其他网络用户通过此计算机的 Internet 连接来连接”
4. 在家庭网络连接中选择 ESP 对应的“以太网 X（UsbNcm Host Device）”
5. 应用后等待 DHCP

> 提示：Windows 常显示为“以太网 X”，不一定显示“NCM”字样。

---

## 5. 调试文档（日志判读）

### 5.1 正常启动关键日志

```text
USB_WIFI_BRIDGE: Initializing USB NCM device ...
TinyUSB: TinyUSB Driver installed on port 1
USB_WIFI_BRIDGE: USB NCM MAC: xx:xx:xx:xx:xx:xx
USB_WIFI_BRIDGE: LWIP Netif MAC: xx:xx:xx:xx:xx:xx
USB_WIFI_BRIDGE: USB host attached, waiting for DHCP from PC
USB_WIFI_BRIDGE: DHCP acquired: 192.168.137.xx, gateway: 192.168.137.1
```

### 5.2 成功判据

出现以下日志即判定成功：

- `USB host attached, waiting for DHCP from PC`
- `DHCP acquired: ...`

第二条是最终成功标志，说明 ESP32-P4 已拿到电脑共享网络地址。

### 5.3 常见日志说明

| 日志 | 含义 | 是否故障 |
| --- | --- | --- |
| `No Device descriptor provided, using default` | 使用默认 USB 描述符 | 否 |
| `String index (...) is out of bounds` | 主机字符串索引探测异常/兼容性提示 | 多数可忽略 |
| `USB net TX failed: ESP_ERR_TIMEOUT` | 链路初始阶段发送超时 | 偶发可接受，持续出现需排查 |
| `Lost IP address from USB host` | 已获取 IP 后又丢失（线缆/共享重置） | 需要排查 |

---

## 6. 故障排查

### 6.1 电脑侧看不到 USB 网卡

- 确认使用的是 OTG 数据口，不是仅串口下载口
- 确认线材是数据线（非纯充电线）
- 在 `ncpa.cpl` 中插拔 OTG 线观察“以太网 X”是否出现/消失

### 6.2 有 USB 网卡但拿不到 DHCP

- 先确认网卡描述是 `UsbNcm Host Device`
- `ipconfig /all` 确认该网卡地址是否为 `192.168.137.1`（ICS 典型值）
- 取消并重新启用 ICS，重新选择共享目标网卡
- 临时禁用 Hyper-V/VMware/VirtualBox/WSL 虚拟网卡，避免 ICS 绑定错误

### 6.3 拿到 IP 但访问失败

- 检查电脑防火墙策略
- 检查电脑当前 Wi-Fi 是否可上网
- 观察是否频繁出现 `Lost IP address from USB host`

---

## 7. 拿到 IP 后如何联网开发

拿到 `DHCP acquired` 后，上层网络开发与普通联网工程一致，可直接使用：

- `esp_http_client`（HTTP/HTTPS）
- BSD Socket（TCP/UDP）
- MQTT、NTP、WebSocket 等

无需修改这些上层 API，只是底层出口从 Wi-Fi STA 变成 USB NCM netif。

---

## 8. 依赖

- ESP-IDF v5.4.2+
- `espressif/esp_tinyusb`
- ESP32-P4

---

## 9. 参考

- [ESP-IDF USB Device 文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s2/api-reference/peripherals/usb_device.html)
- [TinyUSB 文档](https://docs.tinyusb.org/)
- [USB CDC-NCM 规范](https://www.usb.org/document-library/network-control-model-devices-specification-v10)

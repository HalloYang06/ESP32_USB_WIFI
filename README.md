# USB Network Bridge for ESP32-P4

通过 USB 线将 ESP32-P4 连接到电脑，让 ESP32-P4 作为 USB 网卡从电脑获取网络（电脑端开启网络共享/ICS）。

## 功能特性

- 使用USB NCM (Network Control Model) 协议
- ESP32-P4作为USB网络设备
- 从电脑端 DHCP 获取 IP（电脑网络共享）
- 支持Windows/Linux/macOS

## 硬件连接

- **USB OTG**: GPIO39 (D-), GPIO40 (D+)
- 使用USB数据线连接ESP32-P4开发板到电脑

## 使用方法

### 1. 编译和烧录

```bash
idf.py set-target esp32p4
idf.py build
idf.py flash monitor
```

### 2. 连接电脑

1. 用USB线连接ESP32-P4的USB OTG口到电脑
2. 电脑会识别出新的网络适配器
3. 在电脑上开启网络共享（把 WiFi 共享到该 USB 适配器）
4. 等待 ESP32-P4 获取 DHCP 地址

### 3. 电脑端配置

#### Windows
1. 打开"网络连接"
2. 找到你的WiFi适配器
3. 右键 → 属性 → 共享
4. 勾选"允许其他网络用户通过此计算机的Internet连接来连接"
5. 选择USB网络适配器

#### Linux
```bash
# 查看新的网络接口
ip link

# 配置DHCP（假设接口名为usb0）
sudo dhclient usb0
```

#### macOS
1. 系统偏好设置 → 共享
2. 选择"互联网共享"
3. 共享来源：WiFi
4. 共享到：USB网络适配器

## 组件API

### 初始化

```c
#include "usb_wifi_bridge.h"

usb_wifi_bridge_config_t config = {
    .enable_ipv6 = false,
};

esp_err_t ret = usb_wifi_bridge_init(&config);
```

### 检查连接状态

```c
// true 表示已从电脑 DHCP 获取到 IPv4
bool connected = usb_wifi_bridge_is_connected();
```

## 项目结构

```
USB_WIFI/
├── components/
│   └── usb_wifi_bridge/          # USB WiFi桥接组件
│       ├── include/
│       │   └── usb_wifi_bridge.h # 组件头文件
│       ├── usb_wifi_bridge.c     # 组件实现
│       └── CMakeLists.txt
├── main/
│   ├── USB_WIFI.c                # 主程序
│   ├── CMakeLists.txt
│   ├── Kconfig.projbuild         # WiFi配置选项
│   └── idf_component.yml
├── sdkconfig.defaults            # 默认配置
└── CMakeLists.txt
```

## 故障排除

### 电脑无法识别USB设备
- 检查USB线是否支持数据传输
- 确认GPIO39/40连接正确
- 查看串口日志确认TinyUSB初始化成功

### ESP32-P4 无法获取 IP
- 检查电脑端是否已开启网络共享（ICS）
- 检查共享目标是否选中 USB 网卡
- 重插 USB 线后观察串口日志中的 DHCP 信息

### 网络不通
- 确认电脑端已正确配置网络共享
- 检查防火墙设置
- 尝试重新插拔USB线

## 依赖

- ESP-IDF v5.4.2+
- esp_tinyusb组件（已在idf_component.yml中配置）
- ESP32-P4芯片

## 参考文档

- [ESP-IDF USB Device文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s2/api-reference/peripherals/usb_device.html)
- [TinyUSB官方文档](https://docs.tinyusb.org/)
- [USB NCM规范](https://www.usb.org/document-library/network-control-model-devices-specification-v10)

idf_component_register(SRCS "usb_wifi_bridge.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_event esp_tinyusb esp_system esp_common esp_netif esp_rom)

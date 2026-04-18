/*
 * USB Network Bridge Component
 *
 * Exposes ESP32-P4 as a USB NCM network interface and acquires IP
 * configuration from the USB host (e.g. PC Internet Connection Sharing).
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool enable_ipv6;             /*!< Reserve for optional IPv6 setup */
} usb_wifi_bridge_config_t;

#define USB_WIFI_BRIDGE_CONFIG_DEFAULT() { \
    .enable_ipv6 = false, \
}

/**
 * @brief Initialize and start USB network bridge
 *
 * This function initializes USB NCM device class and binds it to esp-netif.
 * The host side should provide DHCP service (e.g. Windows ICS).
 *
 * @param config Pointer to configuration structure
 * @return
 *      - ESP_OK on success
 *      - ESP_FAIL on failure
 */
esp_err_t usb_wifi_bridge_init(const usb_wifi_bridge_config_t *config);

/**
 * @brief Check whether USB netif already has IPv4 address from host DHCP
 *
 * @return true if IP is acquired, false otherwise
 */
bool usb_wifi_bridge_is_connected(void);

#ifdef __cplusplus
}
#endif

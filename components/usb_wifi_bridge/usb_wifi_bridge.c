/*
 * USB NCM to esp-netif bridge implementation for ESP32-P4
 */

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "usb_wifi_bridge.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_check.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_defaults.h"
#include "lwip/esp_netif_net_stack.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_net.h"

static const char *TAG = "USB_WIFI_BRIDGE";
static esp_netif_t *s_netif = NULL;
static bool s_has_ip = false;
static bool s_link_up = false;

static void l2_free(void *h, void *buffer)
{
    (void)h;
    free(buffer);
}

static esp_err_t netif_transmit(void *h, void *buffer, size_t len)
{
    (void)h;
    if (!s_link_up) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = tinyusb_net_send_sync(buffer, (uint16_t)len, NULL, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "USB net TX failed: %s", esp_err_to_name(ret));
        return ESP_ERR_ESP_NETIF_TX_FAILED;
    }
    return ESP_OK;
}

static esp_err_t usb_recv_callback(void *buffer, uint16_t len, void *ctx)
{
    (void)ctx;
    if (s_netif == NULL || !s_link_up) {
        return ESP_OK;
    }

    void *buf_copy = malloc(len);
    ESP_RETURN_ON_FALSE(buf_copy != NULL, ESP_ERR_NO_MEM, TAG, "No memory for RX frame copy");
    memcpy(buf_copy, buffer, len);
    return esp_netif_receive(s_netif, buf_copy, len, NULL);
}

static void ip_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;

    if (event_id == IP_EVENT_ETH_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        if (event == NULL || event->esp_netif != s_netif) {
            return;
        }
        s_has_ip = true;
        ESP_LOGI(TAG, "DHCP acquired: " IPSTR ", gateway: " IPSTR,
                 IP2STR(&event->ip_info.ip), IP2STR(&event->ip_info.gw));
        return;
    }

    if (event_id == IP_EVENT_ETH_LOST_IP) {
        s_has_ip = false;
        ESP_LOGW(TAG, "Lost IP address from USB host");
    }
}

static void usb_event_handler(tinyusb_event_t *event, void *arg)
{
    (void)arg;
    if (s_netif == NULL) {
        return;
    }

    switch (event->id) {
    case TINYUSB_EVENT_ATTACHED:
        s_link_up = true;
        esp_netif_action_connected(s_netif, 0, 0, NULL);
        ESP_LOGI(TAG, "USB host attached, waiting for DHCP from PC");
        break;
    case TINYUSB_EVENT_DETACHED:
        s_link_up = false;
        s_has_ip = false;
        esp_netif_action_disconnected(s_netif, 0, 0, NULL);
        ESP_LOGW(TAG, "USB host detached");
        break;
    default:
        break;
    }
}

static esp_err_t init_netif(void)
{
    uint8_t usb_mac[6];
    uint8_t lwip_mac[6];
    ESP_RETURN_ON_ERROR(esp_read_mac(usb_mac, ESP_MAC_ETH), TAG, "Failed to read base MAC");
    usb_mac[0] = (uint8_t)((usb_mac[0] | 0x02) & 0xFE); // locally administered unicast MAC
    memcpy(lwip_mac, usb_mac, sizeof(lwip_mac));
    lwip_mac[5] ^= 0x01; // must differ from host-side USB NIC MAC to avoid L2 ambiguity

    esp_netif_inherent_config_t base_cfg = ESP_NETIF_INHERENT_DEFAULT_ETH();
    base_cfg.if_key = "USB_NCM_DEF";
    base_cfg.if_desc = "usb_ncm";
    base_cfg.route_prio = 60;

    esp_netif_driver_ifconfig_t driver_cfg = {
        .handle = (void *)1,                // singleton driver handle, must be non-NULL
        .transmit = netif_transmit,
        .driver_free_rx_buffer = l2_free
    };

    struct esp_netif_netstack_config lwip_netif_config = {
        .lwip = {
            .init_fn = ethernetif_init,
            .input_fn = ethernetif_input
        }
    };

    esp_netif_config_t cfg = {
        .base = &base_cfg,
        .driver = &driver_cfg,
        .stack = &lwip_netif_config
    };

    s_netif = esp_netif_new(&cfg);
    ESP_RETURN_ON_FALSE(s_netif != NULL, ESP_FAIL, TAG, "Failed to create esp-netif");
    ESP_RETURN_ON_ERROR(esp_netif_set_mac(s_netif, lwip_mac), TAG, "Failed to set esp-netif MAC");

    ESP_LOGI(TAG, "USB NCM MAC: %02x:%02x:%02x:%02x:%02x:%02x",
             usb_mac[0], usb_mac[1], usb_mac[2], usb_mac[3], usb_mac[4], usb_mac[5]);
    ESP_LOGI(TAG, "LWIP Netif MAC: %02x:%02x:%02x:%02x:%02x:%02x",
             lwip_mac[0], lwip_mac[1], lwip_mac[2], lwip_mac[3], lwip_mac[4], lwip_mac[5]);

    esp_err_t ret = esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, ip_event_handler, NULL);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to register IP_EVENT_ETH_GOT_IP");
    ret = esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_LOST_IP, ip_event_handler, NULL);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to register IP_EVENT_ETH_LOST_IP");

    tinyusb_net_config_t net_config = {
        .on_recv_callback = usb_recv_callback,
        .free_tx_buffer = NULL,
        .on_init_callback = NULL,
        .user_context = NULL
    };
    memcpy(net_config.mac_addr, usb_mac, sizeof(usb_mac));
    ESP_RETURN_ON_ERROR(tinyusb_net_init(&net_config), TAG, "Failed to initialize TinyUSB NCM class");

    return ESP_OK;
}

esp_err_t usb_wifi_bridge_init(const usb_wifi_bridge_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Configuration is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    (void)config;
    if (s_netif != NULL) {
        ESP_LOGW(TAG, "USB bridge already initialized");
        return ESP_OK;
    }

    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    ESP_LOGI(TAG, "Initializing USB NCM device (GPIO39/40 for USB OTG)");
    const tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(usb_event_handler);
    ESP_RETURN_ON_ERROR(tinyusb_driver_install(&tusb_cfg), TAG, "Failed to install TinyUSB driver");

    ESP_RETURN_ON_ERROR(init_netif(), TAG, "Failed to initialize network stack over USB");

    // Driver is already started, bring esp-netif up and enable DHCP client.
    esp_netif_action_start(s_netif, 0, 0, NULL);
    ESP_LOGI(TAG, "USB network bridge initialized. Enable Internet sharing on PC side.");
    return ESP_OK;
}

bool usb_wifi_bridge_is_connected(void)
{
    return s_has_ip;
}

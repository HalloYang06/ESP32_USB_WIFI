#include <stdio.h>
#include "usb_wifi_bridge.h"
#include "esp_log.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting USB Network Bridge");

    usb_wifi_bridge_config_t config = USB_WIFI_BRIDGE_CONFIG_DEFAULT();

    esp_err_t ret = usb_wifi_bridge_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize USB WiFi bridge: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "USB bridge started, connect USB cable (GPIO39/40) to PC");
    ESP_LOGI(TAG, "Enable PC Internet sharing for the USB network adapter");
}

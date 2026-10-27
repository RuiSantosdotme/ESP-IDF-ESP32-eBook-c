/*  
  Rui Santos & Sara Santos - Random Nerd Tutorials
  Learn more with our eBook » https://RandomNerdTutorials.com/esp-idf-esp32-ebook/
  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files.
  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
*/

#include <stdio.h>
#include <string.h>
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "ESP-NOW_RECEIVER";

// Sample data structure (must match sender)
typedef struct {
    uint32_t seq;
    float    temperature;
    float    humidity;
    char     message[32];
} __attribute__((packed)) espnow_data_t;

// ESP-NOW receive callback function, prints received data and RSSI
static void receive_callback(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    if (len != sizeof(espnow_data_t)) {
        ESP_LOGW(TAG, "Received wrong data length: %d", len);
        return;
    }

    espnow_data_t *recv_data = (espnow_data_t *)data;

    ESP_LOGI(TAG, "------ Received Data ------");
    ESP_LOGI(TAG, "From: " MACSTR, MAC2STR(recv_info->src_addr));
    ESP_LOGI(TAG, "Seq: %lu", recv_data->seq);
    ESP_LOGI(TAG, "Temperature: %.1f °C", recv_data->temperature);
    ESP_LOGI(TAG, "Humidity: %.1f %%", recv_data->humidity);
    ESP_LOGI(TAG, "Message: %s", recv_data->message);
    ESP_LOGI(TAG, "RSSI: %d dBm", recv_info->rx_ctrl->rssi);
}

void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize Wi-Fi in STA mode
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Get and print MAC Address
    uint8_t my_mac[6];
    esp_wifi_get_mac(ESP_MAC_WIFI_STA, my_mac);
    ESP_LOGI(TAG, "ESP-NOW MAC Address: " MACSTR, MAC2STR(my_mac));

    // Initialize ESP-NOW
    ESP_ERROR_CHECK(esp_now_init());
    // Register ESP-NOW receive callback
    ESP_ERROR_CHECK(esp_now_register_recv_cb(receive_callback));

    ESP_LOGI(TAG, "ESP-NOW Receiver started. Waiting for data...");
}
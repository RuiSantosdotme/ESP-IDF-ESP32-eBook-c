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

static const char *TAG = "ESP-NOW_SENDER";

// REPLACE THE NEXT LINE WITH THE RECEIVER MAC ADDRESS
uint8_t receiver_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Number of sample data sent
static uint32_t seq_num = 0;

// Sample data structure (must match receiver)
typedef struct {
    uint32_t seq;
    float    temperature;
    float    humidity;
    char     message[32];
} __attribute__((packed)) espnow_data_t;

// Callback function to handle send status
static void send_callback(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    if (tx_info && tx_info->src_addr) {
        ESP_LOGI(TAG, "Sent to " MACSTR " | Status: %s", 
                 MAC2STR(tx_info->src_addr), 
                 status == ESP_NOW_SEND_SUCCESS ? "Success" : "Failed");
    } else {
        ESP_LOGI(TAG, "Send status: %s", 
                 status == ESP_NOW_SEND_SUCCESS ? "Success" : "Failed");
    }
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

    // Initialize Wi-Fi
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Initialize ESP-NOW
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(send_callback));

    // Add receiver as peer
    esp_now_peer_info_t peer_info = {};
    memcpy(peer_info.peer_addr, receiver_mac, 6);
    peer_info.channel = 0;
    peer_info.ifidx = ESP_MAC_WIFI_STA;
    peer_info.encrypt = false;
    ESP_ERROR_CHECK(esp_now_add_peer(&peer_info));

    ESP_LOGI(TAG, "ESP-NOW Sender started. Receiver MAC: " MACSTR, MAC2STR(receiver_mac));

    espnow_data_t data = {0};

    // Main loop to send sample data every 10 seconds
    while (1) {
        // Prepare sample data
        data.seq = ++seq_num;
        data.temperature = 25.5 + (seq_num % 10) * 0.1;
        data.humidity = 60.0 + (seq_num % 20);
        snprintf(data.message, sizeof(data.message), "Hello from Sender!");

        // Send sample data via ESP-NOW protocol
        esp_err_t result = esp_now_send(receiver_mac, (uint8_t *)&data, sizeof(data));
        
        // Log the result on the Terminal of the send operation
        if (result == ESP_OK) {
            ESP_LOGI(TAG, "Queued seq=%lu, Temp=%.1f°C, Hum=%.1f%%", 
                     data.seq, data.temperature, data.humidity);
        } else {
            ESP_LOGE(TAG, "Failed to queue: %s", esp_err_to_name(result));
        }

        // Send new sample data every 10 seconds
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
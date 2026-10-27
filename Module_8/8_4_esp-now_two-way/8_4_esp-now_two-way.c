/*  
  Rui Santos & Sara Santos - Random Nerd Tutorials
  Learn more with our eBook » https://RandomNerdTutorials.com/esp-idf-esp32-ebook/
  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files.
  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
*/

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "BOARD_1";
// You can change the TAG to "BOARD_2" on the other board to help distinguish the logs
//static const char *TAG = "BOARD_2";     

#define ESPNOW_CHANNEL    1      // Both boards MUST use the same channel
#define SEND_INTERVAL_MS  10000  // Send a packet every 10 seconds
#define RX_QUEUE_LEN      10     // Length of the queue to hold received packets

// 1 = broadcast (same code, no MAC Adress needed)
// 0 = unicast (requires MAC address of the other board)
#define USE_BROADCAST 1 

#if USE_BROADCAST
    static const uint8_t peer_mac[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
#else
    // REPLACE WITH THE MAC ADDRESS OF THE OTHER BOARD
    static const uint8_t peer_mac[ESP_NOW_ETH_ALEN] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
#endif

// Sample data structure (same structure on both boards)
typedef struct {
    uint32_t msg_n;
    float    temperature;
    float    humidity;
    char     message[32];
} __attribute__((packed)) espnow_data_t;

// This structure is used to pass data from the receive callback to the RX task
typedef struct {
    uint8_t       src_mac[ESP_NOW_ETH_ALEN];
    int8_t        rssi;
    espnow_data_t payload;
} rx_event_t;

static QueueHandle_t rx_queue;
static uint8_t my_mac[ESP_NOW_ETH_ALEN];
static uint32_t msg_num = 0;   // Number of packets this board has sent

// Callback function called when a packet is sent. It logs the status of the send operation
static void send_callback(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    (void)tx_info;
    ESP_LOGI(TAG, "Send status: %s", status == ESP_NOW_SEND_SUCCESS ? "Success" : "Failed");
}

// Callback function called when data is received. It copies the data into a queue for processing in the RX task
static void receive_callback(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    if (len != sizeof(espnow_data_t)) {
        ESP_LOGW(TAG, "Received wrong data length: %d", len);
        return;
    }

    rx_event_t evt;
    memcpy(evt.src_mac, recv_info->src_addr, ESP_NOW_ETH_ALEN);
    evt.rssi = recv_info->rx_ctrl->rssi;
    memcpy(&evt.payload, data, sizeof(espnow_data_t));
    evt.payload.message[sizeof(evt.payload.message) - 1] = '\0';

    if (xQueueSend(rx_queue, &evt, 0) != pdTRUE) {
        ESP_LOGW(TAG, "RX queue full, packet dropped");
    }
}

// Task that processes received data from the RX queue and logs it
static void rx_task(void *arg)
{
    rx_event_t evt;

    while (1) {
        // Wait indefinitely for data to arrive in the RX queue
        if (xQueueReceive(rx_queue, &evt, portMAX_DELAY) == pdTRUE) {
            uint32_t msg_n  = evt.payload.msg_n;
            float    temp = evt.payload.temperature;
            float    hum  = evt.payload.humidity;
            // Log the received data
            printf("\n");
            ESP_LOGI(TAG, "------ RECEIVED DATA ------");
            ESP_LOGI(TAG, "From MAC Address: " MACSTR, MAC2STR(evt.src_mac));
            ESP_LOGI(TAG, "Message #: %" PRIu32, msg_n);
            ESP_LOGI(TAG, "Temperature: %.1f °C", temp);
            ESP_LOGI(TAG, "Humidity: %.1f %%", hum);
            ESP_LOGI(TAG, "Message: %s", evt.payload.message);
            ESP_LOGI(TAG, "RSSI: %d dBm", evt.rssi);
            ESP_LOGI(TAG, "------ END ------");
            printf("\n");
        }
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

    // Initialize Wi-Fi in STA mode
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Both boards must be on the same channel to hear each other
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));

    // Get and print this board's MAC address
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, my_mac));
    ESP_LOGI(TAG, "ESP-NOW MAC Address: " MACSTR, MAC2STR(my_mac));

    // Create a queue to hold received data and start the RX task
    rx_queue = xQueueCreate(RX_QUEUE_LEN, sizeof(rx_event_t));
    if (rx_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create RX queue");
        return;
    }
    xTaskCreate(rx_task, "espnow_rx", 4096, NULL, 5, NULL);

    // Initialize ESP-NOW and register both callbacks
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(send_callback));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(receive_callback));

    // Add the other board (or the broadcast address) as a peer so we can send to it
    esp_now_peer_info_t peer_info = {};
    memcpy(peer_info.peer_addr, peer_mac, ESP_NOW_ETH_ALEN);
    peer_info.channel = 0;
    peer_info.ifidx = WIFI_IF_STA;
    peer_info.encrypt = false;
    ESP_ERROR_CHECK(esp_now_add_peer(&peer_info));

    // Log that the two-way communication has started
    ESP_LOGI(TAG, "ESP-NOW two-way communication started.");
    ESP_LOGI(TAG, "Peer MAC Address: " MACSTR "%s", MAC2STR(peer_mac), USE_BROADCAST ? " (broadcast)" : "");

    espnow_data_t data = {0};

    while (1) {
        // Prepare sample data to send
        data.msg_n = ++msg_num;
        data.temperature = 25.5f + (msg_num % 10) * 0.1f;
        data.humidity = 60.0f + (msg_num % 20);
        // Create a message string that includes the board's TAG
        snprintf(data.message, sizeof(data.message), "Hello from %s", TAG);

        // Send the data to the other board (or broadcast)
        esp_err_t result = esp_now_send(peer_mac, (uint8_t *)&data, sizeof(data));

        // Log the result of the send operation
        if (result == ESP_OK) {
            ESP_LOGI(TAG, "Queued msg_n=%" PRIu32 ", Temp=%.1f°C, Hum=%.1f%%",
                     data.msg_n, data.temperature, data.humidity);
        } else {
            ESP_LOGE(TAG, "Failed to queue: %s", esp_err_to_name(result));
        }

        // Wait for the next send interval
        vTaskDelay(pdMS_TO_TICKS(SEND_INTERVAL_MS));
    }
}
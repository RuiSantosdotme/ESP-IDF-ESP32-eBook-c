/*  
  Rui Santos & Sara Santos - Random Nerd Tutorials
  Learn more with our eBook » https://RandomNerdTutorials.com/esp-idf-esp32-ebook/
  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files.
  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
*/

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"

// REPLACE WITH YOUR SSID AND PASSWORD
#define WIFI_SSID   "REPLACE_WITH_YOUR_SSID"
#define WIFI_PASS   "REPLACE_WITH_YOUR_PASSWORD"

// REPLACE WITH YOUR TIME ZONE - https://time.now/developer/api/timezone
#define TIME_ZONE   "Europe/Lisbon"

// time.now endpoint with timezone query
#define API_URL     "https://time.now/developer/api/timezone/" TIME_ZONE

// Tag for logging
static const char *TAG = "http_get";

// Buffer for HTTP response
#define BUF_SIZE    512
static char response_buf[BUF_SIZE];
static int  response_len = 0;

// HTTP event handler to collect response data from the request
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        int copy = evt->data_len;
        if (response_len + copy >= BUF_SIZE) copy = BUF_SIZE - response_len - 1;
        memcpy(response_buf + response_len, evt->data, copy);
        response_len += copy;
        response_buf[response_len] = '\0';
    }
    return ESP_OK;
}

// Function that extracts a JSON field (no library needed)
// Finds "key":"value" or "key":value and copies the value 
static int get_json_value(const char *json, const char *key, char *out, int out_size)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *p = strstr(json, search);
    if (!p) return 0;
    p += strlen(search);
    while (*p == ' ') p++;
    int is_string = (*p == '"');
    if (is_string) p++;
    int i = 0;
    while (*p && i < out_size - 1) {
        if (is_string && *p == '"') break;
        if (!is_string && (*p == ',' || *p == '}')) break;
        out[i++] = *p++;
    }
    out[i] = '\0';
    return i;
}

// Function that makes an HTTP GET request to the API, parses the JSON response, and prints the date and time
static void get_and_print_time(void)
{
    // Reset response buffer
    response_len = 0;
    memset(response_buf, 0, BUF_SIZE);

    // Configure HTTP client
    esp_http_client_config_t config = {
        .url               = API_URL,
        .event_handler     = http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    // Initialize HTTP client and perform the HTTP GET request
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    // Check if the request was successful (ESP_OK and status code 200) and print the results
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status == 200) {
            // Extract fields (values) from the JSON response.
            char datetime[40], day_of_week_num[4], tz[40], abbr[8], dst[8];
            get_json_value(response_buf, "datetime",     datetime,        sizeof(datetime));
            get_json_value(response_buf, "day_of_week",  day_of_week_num, sizeof(day_of_week_num));
            get_json_value(response_buf, "timezone",     tz,              sizeof(tz));
            get_json_value(response_buf, "abbreviation",  abbr,            sizeof(abbr));
            get_json_value(response_buf, "dst",           dst,             sizeof(dst));

            // Split "YYYY-MM-DDTHH:MM:SS.ffffff+HH:MM" into date and time
            char date[16] = "", time_str[24] = "";
            char *t_sep = strchr(datetime, 'T');
            if (t_sep) {
                int date_len = t_sep - datetime;
                if (date_len >= (int)sizeof(date)) date_len = sizeof(date) - 1;
                memcpy(date, datetime, date_len);
                date[date_len] = '\0';
                strncpy(time_str, t_sep + 1, sizeof(time_str) - 1);
                time_str[sizeof(time_str) - 1] = '\0';
                for (int i = 8; time_str[i] != '\0'; i++) {
                    if (time_str[i] == '.' || time_str[i] == '+' || time_str[i] == '-') {
                        time_str[i] = '\0';
                        break;
                    }
                }
            }

            // day_of_week from time.now: 1=Monday (...) 7=Sunday
            static const char *weekday_names[] = {
                "", "Monday", "Tuesday", "Wednesday",
                "Thursday", "Friday", "Saturday", "Sunday"
            };
            int dow = atoi(day_of_week_num);
            const char *day_name = (dow >= 1 && dow <= 7) ? weekday_names[dow] : "Unknown";

            // Print the results
            ESP_LOGI(TAG, "------------------------------------");
            ESP_LOGI(TAG, "Timezone: %s (%s)", tz, abbr);
            ESP_LOGI(TAG, "Date    : %s (%s)", date, day_name);
            ESP_LOGI(TAG, "Time    : %s", time_str);
            ESP_LOGI(TAG, "DST     : %s", (strcmp(dst, "true") == 0) ? "yes" : "no");
            ESP_LOGI(TAG, "------------------------------------");
        } else {
            ESP_LOGW(TAG, "HTTP status %d", status);
        }
    } else {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
    }
    // Cleanup HTTP client
    esp_http_client_cleanup(client);
}

// Event group to signal when connected to Wi-Fi
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

// Wi-Fi and IP event handler
static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi STA started. Connecting to %s...", WIFI_SSID);
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Wi-Fi disconnected. Retrying connection...");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Got IP Address: " IPSTR, IP2STR(&e->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// Initializes Wi-Fi in station mode and waits for connection
static void wifi_init(void)
{
    // Create event group to signal when connected
    s_wifi_event_group = xEventGroupCreate();

    // Initialize TCP/IP stack and event loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create default Wi-Fi STA interface
    esp_netif_create_default_wifi_sta();
    // Initialize Wi-Fi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        wifi_event_handler, NULL, NULL);

    // Configure Wi-Fi STA
    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid     = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to Wi-Fi...");

    // Wait until connected
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);
}

// Task that periodically makes an HTTP GET request to the API and prints the time
static void http_get_task(void *arg)
{
    while (true) {
        get_and_print_time();
        // Wait for 60 seconds before the next request
        vTaskDelay(pdMS_TO_TICKS(60000)); 
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
    wifi_init();

    // Start the HTTP GET time fetching task
    xTaskCreate(http_get_task, "http_get_task", 8192, NULL, 5, NULL);
}
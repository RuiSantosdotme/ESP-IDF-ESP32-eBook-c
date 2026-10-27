/*  
  Rui Santos & Sara Santos - Random Nerd Tutorials
  Learn more with our eBook » https://RandomNerdTutorials.com/esp-idf-esp32-ebook/
  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files.
  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
*/

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "i2c_bus.h"
#include "bme280.h"
#include "sdkconfig.h"

// Replace with your Wi-Fi credentials
#define WIFI_SSID      "MEO-203DA0"
#define WIFI_PASS      "0a8a4862c1"

// I2C Configuration
#define I2C_PORT        0     // I2C port number (0 or 1)
#define I2C_SDA_PIN     8     // SDA Pin number
#define I2C_SCL_PIN     9     // SCL Pin number
#define I2C_FREQ_HZ     100000    // I2C clock frequency (100kHz)

// REPLACE WITH YOUR BME280 I2C ADDRESS 
#define BME280_I2C_ADDR       0x76    // I2C address (usually 0x76 or 0x77)

static const char *TAG = "web_server";

// Initialize the I2C bus and BME280 sensor handle
static i2c_bus_handle_t i2c_bus = NULL;
static bme280_handle_t  bme280  = NULL;

// HTTP GET handler for root "/" - send main HTML page with sensor readings
static esp_err_t root_get_handler(httpd_req_t *req)
{
    float temperature = 0.0f;
    float humidity    = 0.0f;
    float pressure    = 0.0f;

    // Read BME280 sensor data
    if (bme280_read_temperature(bme280, &temperature) != ESP_OK ||
        bme280_read_humidity(bme280, &humidity) != ESP_OK ||
        bme280_read_pressure(bme280, &pressure) != ESP_OK) {
        ESP_LOGW(TAG, "Sensor read failed");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Sensor read error");
        return ESP_FAIL;
    }

    // Convert °C to °F
    float temperature_f = (temperature * 9.0f / 5.0f) + 32.0f;

    // Prepare HTML response
    char html[2048];
    int len = snprintf(html, sizeof(html),
        "<!DOCTYPE html>"
        "<html lang=\"en\">"
        "<head>"
        "<meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<meta http-equiv=\"refresh\" content=\"5\">"
        "<title>ESP-IDF: ESP32 with BME280</title>"
        "<style>"
            "*{box-sizing:border-box;margin:0;padding:0;}"
            "body{font-family:Roboto,sans-serif;background:#f0f4f8;color:#1a202c;min-height:100vh;display:flex;flex-direction:column;align-items:center;justify-content:center;padding:20px;}"
            ".card{background:#ffffff;border-radius:16px;box-shadow:0 10px 25px rgba(0,0,0,0.08);padding:32px 28px;max-width:360px;width:100%%;text-align:center;}"
            "h1{font-size:1.4rem;font-weight:600;color:#2d3748;margin-bottom:28px;}"
            ".reading{margin:18px 0;padding:14px 0;border-bottom:1px solid #edf2f7;}"
            ".reading:last-of-type{border-bottom:none;}"
            ".label{font-size:0.85rem;color:#718096;text-transform:uppercase;letter-spacing:0.05em;margin-bottom:4px;}"
            ".value{font-size:2rem;font-weight:600;color:#2b6cb0;}"
            ".unit{font-size:1.1rem;font-weight:400;color:#4a5568;}"
            ".footer{margin-top:24px;font-size:0.8rem;color:#a0aec0;}"
        "</style>"
        "</head>"
        "<body>"
        "<div class=\"card\">"
        "  <h1>ESP-IDF: ESP32 with BME280</h1>"
        "  <div class=\"reading\">"
        "    <div class=\"label\">Temperature Celsius</div>"
        "    <div class=\"value\">%.1f<span class=\"unit\"> °C</span></div>"
        "  </div>"
        "  <div class=\"reading\">"
        "    <div class=\"label\">Temperature Fahrenheit</div>"
        "    <div class=\"value\">%.1f<span class=\"unit\"> °F</span></div>"
        "  </div>"
        "  <div class=\"reading\">"
        "    <div class=\"label\">Humidity</div>"
        "    <div class=\"value\">%.1f<span class=\"unit\"> %%</span></div>"
        "  </div>"
        "  <div class=\"reading\">"
        "    <div class=\"label\">Pressure</div>"
        "    <div class=\"value\">%.1f<span class=\"unit\"> hPa</span></div>"
        "  </div>"
        "  <div class=\"footer\">Auto refresh every 5 seconds</div>"
        "</div>"
        "</body>"
        "</html>",
        temperature, temperature_f, humidity, pressure);
    
    // Send the HTML response
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, len);
    return ESP_OK;
}

// Start the HTTP server
static httpd_handle_t start_web_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    // Start the HTTP server
    if (httpd_start(&server, &config) == ESP_OK) {
        ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
        // Register URI handler for root "/"
        httpd_uri_t uri_get = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = root_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &uri_get);

        return server;
    }
    ESP_LOGE(TAG, "Failed to start HTTP server");
    return NULL;
}

// Wi-Fi and IP event handler
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi STA started. Connecting to %s...", WIFI_SSID);
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Wi-Fi disconnected. Retrying connection...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP Address: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Web Server ready! Access at http://" IPSTR "/", IP2STR(&event->ip_info.ip));
    }
}

// Initialize the BME280 sensor
static esp_err_t bme280_sensor_init(void)
{
    // Configure I2C bus
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = I2C_SCL_PIN,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
    };
    // Create I2C bus
    i2c_bus = i2c_bus_create(I2C_PORT, &conf);
    if (i2c_bus == NULL) {
        ESP_LOGE(TAG, "Failed to create I2C bus");
        return ESP_FAIL;
    }
    // Create BME280 sensor handle
    bme280 = bme280_create(i2c_bus, BME280_I2C_ADDR);
    if (bme280 == NULL) {
        ESP_LOGE(TAG, "Failed to create BME280 handle (address 0x%02X)", BME280_I2C_ADDR);
        return ESP_FAIL;
    }
    // Initialize BME280 sensor with default settings
    ESP_ERROR_CHECK(bme280_default_init(bme280));
    ESP_LOGI(TAG, "BME280 initialized successfully (I2C addr: 0x%02X)", BME280_I2C_ADDR);

    return ESP_OK;
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

    // Initialize TCP/IP stack and event loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create default Wi-Fi STA interface
    esp_netif_create_default_wifi_sta();

    // Initialize Wi-Fi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler, NULL, NULL));
    
    // Configure Wi-Fi STA
    wifi_config_t wifi_config = {
        .sta = {
            .ssid     = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Initialize BME280 sensor
    ESP_ERROR_CHECK(bme280_sensor_init());

     // Start the web server (it will be ready once IP is assigned)
    httpd_handle_t server = start_web_server();
    if (server) {
        ESP_LOGI(TAG, "Web Server initialized. Waiting for Wi-Fi connection...");
    }
}
/*  
  Rui Santos & Sara Santos - Random Nerd Tutorials
  Learn more with our eBook » https://RandomNerdTutorials.com/esp-idf-esp32-ebook/
  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files.
  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
*/

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "i2c_bus.h"
#include "bme280.h"

// I2C Configuration
#define I2C_PORT        0          // I2C port number (0 or 1)
#define I2C_SDA_PIN     8          // SDA Pin number
#define I2C_SCL_PIN     9          // SCL Pin number
#define I2C_FREQ_HZ     100000     // I2C clock frequency (100kHz)

// REPLACE WITH YOUR BME280 I2C ADDRESS 
#define BME280_ADDR     0x76   // I2C address (usually 0x76 or 0x77)

// Logging tag for ESP_LOGx messages
static const char *TAG = "BME280";

// Initialize the BME280 sensor
static bme280_handle_t sensor_init(i2c_bus_handle_t bus, uint8_t addr)
{
    // Create BME280 handle
    bme280_handle_t sensor = bme280_create(bus, addr);
    // Check if the sensor handle was created successfully
    if (sensor == NULL) {
        ESP_LOGE(TAG, "Failed to create BME280 handle (addr 0x%02X)", addr);
        return NULL;
    }
    // Initialize the sensor with default settings and check for errors
    if (bme280_default_init(sensor) != ESP_OK) {
        ESP_LOGE(TAG, "BME280 not detected at address 0x%02X - check wiring", addr);
        return NULL;
    }

    return sensor;
}

void app_main(void)
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
    i2c_bus_handle_t i2c_bus = i2c_bus_create(I2C_PORT, &conf);
    if (i2c_bus == NULL) {
        ESP_LOGE(TAG, "Failed to create I2C bus");
        return;
    }
    // Initialize BME280 sensor and check for errors
    bme280_handle_t bme280 = sensor_init(i2c_bus, BME280_ADDR);
    if (bme280 == NULL) {
        ESP_LOGE(TAG, "Failed to initialize BME280 sensor");
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    float temperature_c = 0.0f, humidity = 0.0f, pressure = 0.0f;

    while (1) {
        // Read temperature, pressure, and humidity from the BME280 sensor
        esp_err_t temp_ok = bme280_read_temperature(bme280, &temperature_c);
        esp_err_t press_ok = bme280_read_pressure(bme280, &pressure);
        esp_err_t humi_ok = bme280_read_humidity(bme280, &humidity);

        // Log the readings if successful, otherwise print an error message
        if (temp_ok == ESP_OK && press_ok == ESP_OK && humi_ok == ESP_OK) {
            float temperature_f = temperature_c * 9.0f / 5.0f + 32.0f;
            ESP_LOGI(TAG, "Temperature: %.2f C / %.2f F  Pressure: %.2f hPa  Humidity: %.2f %%",
                     temperature_c, temperature_f, pressure, humidity);
        } else {
            ESP_LOGW(TAG, "Sensor read failed (t=%d p=%d h=%d)", temp_ok, press_ok, humi_ok);
        }
        // Delay for 30 seconds before the next reading
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

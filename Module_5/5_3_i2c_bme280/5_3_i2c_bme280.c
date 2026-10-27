/*  
  Rui Santos & Sara Santos - Random Nerd Tutorials
  Learn more with our eBook » https://RandomNerdTutorials.com/esp-idf-esp32-ebook/
  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files.
  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
*/

#include <stdio.h>
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Logging tag for ESP_LOGx messages
#define TAG "BME280"

// I2C Configuration
#define I2C_PORT        0        // I2C port number (0 or 1)
#define I2C_SDA_PIN     8        // SDA Pin number
#define I2C_SCL_PIN     9        // SCL Pin number
#define I2C_FREQ_HZ     100000   // I2C clock frequency (100kHz)

// REPLACE WITH YOUR BME280 I2C ADDRESS 
#define BME280_ADDR     0x76   // I2C address (usually 0x76 or 0x77)

#define REG_ID         0xD0    // Chip ID register (should return 0x60)
#define REG_RESET      0xE0    // Soft reset register
#define REG_CTRL_HUM   0xF2    // Humidity control (oversampling)
#define REG_CTRL_MEAS  0xF4    // Temperature/Pressure control + mode
#define REG_CONFIG     0xF5    // Configuration (standby time, filter)
#define REG_DATA       0xF7    // Raw sensor data start (pressure, temp, humidity)

static i2c_master_bus_handle_t bus;  // I2C bus handle
static i2c_master_dev_handle_t dev;  // BME280 device handle

// Calibration data - coefficients stored as raw integers and used in the compensation formulas
static uint16_t dig_T1;
static int16_t  dig_T2, dig_T3;
static uint16_t dig_P1;
static int16_t  dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
static uint8_t  dig_H1;
static int16_t  dig_H2;
static uint8_t  dig_H3;
static int16_t  dig_H4, dig_H5;
static int8_t   dig_H6;
static int32_t t_fine;

// Write a single register
esp_err_t bme_write(uint8_t reg, uint8_t data)
{
    uint8_t buf[2] = { reg, data };
    return i2c_master_transmit(dev, buf, sizeof(buf), -1);
}

// Read one or more bytes starting from a register
esp_err_t bme_read(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(dev, &reg, 1, data, len, -1);
}

// BME280 read calibration
void bme280_read_calibration(void)
{
    uint8_t buf[26];
    bme_read(0x88, buf, 26);

    dig_T1 = (buf[1] << 8) | buf[0];
    dig_T2 = (buf[3] << 8) | buf[2];
    dig_T3 = (buf[5] << 8) | buf[4];

    dig_P1 = (buf[7] << 8) | buf[6];
    dig_P2 = (buf[9] << 8) | buf[8];
    dig_P3 = (buf[11] << 8) | buf[10];
    dig_P4 = (buf[13] << 8) | buf[12];
    dig_P5 = (buf[15] << 8) | buf[14];
    dig_P6 = (buf[17] << 8) | buf[16];
    dig_P7 = (buf[19] << 8) | buf[18];
    dig_P8 = (buf[21] << 8) | buf[20];
    dig_P9 = (buf[23] << 8) | buf[22];

    dig_H1 = buf[25];

    uint8_t hbuf[7];
    bme_read(0xE1, hbuf, 7);

    dig_H2 = (hbuf[1] << 8) | hbuf[0];
    dig_H3 = hbuf[2];
    dig_H4 = (hbuf[3] << 4) | (hbuf[4] & 0x0F);
    dig_H5 = (hbuf[5] << 4) | (hbuf[4] >> 4);
    dig_H6 = (int8_t)hbuf[6];
}

// Temperature compensation (returns °C)
float compensate_temp(int32_t adc_T)
{
    int32_t var1, var2;
    var1 = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1))) * dig_T2) >> 11;
    var2 = (((((adc_T >> 4) - (int32_t)dig_T1) *
              ((adc_T >> 4) - (int32_t)dig_T1)) >> 12) *
            dig_T3) >> 14;

    t_fine = var1 + var2;
    return (t_fine * 5 + 128) / 256.0f / 100.0f;
}

// Humidity compensation (returns % relative humidity)
float compensate_humidity(int32_t adc_H)
{
    int32_t v_x1;
    v_x1 = t_fine - 76800;
    v_x1 = (((((adc_H << 14) - ((int32_t)dig_H4 << 20) -
              ((int32_t)dig_H5 * v_x1)) + 16384) >> 15) *
             (((((((v_x1 * dig_H6) >> 10) *
              (((v_x1 * dig_H3) >> 11) + 32768)) >> 10) + 2097152) *
              dig_H2 + 8192) >> 14));
    v_x1 -= (((((v_x1 >> 15) * (v_x1 >> 15)) >> 7) * dig_H1) >> 4);
    v_x1 = (v_x1 < 0 ? 0 : v_x1);
    v_x1 = (v_x1 > 419430400 ? 419430400 : v_x1);
    return (v_x1 >> 12) / 1024.0f;
}

// Pressure compensation (returns hPa)
float compensate_pressure(int32_t adc_P)
{
    int64_t var1, var2, p;
    var1 = (int64_t)t_fine - 128000;
    var2 = var1 * var1 * dig_P6;
    var2 += ((var1 * dig_P5) << 17);
    var2 += ((int64_t)dig_P4 << 35);
    var1 = ((var1 * var1 * dig_P3) >> 8) + ((var1 * dig_P2) << 12);
    var1 = (((int64_t)1 << 47) + var1) * dig_P1 >> 33;

    if (var1 == 0) return 0;

    p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = ((int64_t)dig_P9 * (p >> 13) * (p >> 13)) >> 25;
    var2 = ((int64_t)dig_P8 * p) >> 19;
    p = ((p + var1 + var2) >> 8) + ((int64_t)dig_P7 << 4);
    return p / 25600.0f;   // hPa
}

// BME280 Sensor Initilization
void bme280_init(void)
{
    // Create I2C master bus
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_PORT,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    // Add BME280 device to the bus
    i2c_device_config_t dev_cfg = {
        .device_address = BME280_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &dev));

    // Check sensor ID
    uint8_t id;
    bme_read(REG_ID, &id, 1);
    ESP_LOGI(TAG, "BME280 Chip ID: 0x%02X", id);

    // Soft reset the sensor
    bme_write(REG_RESET, 0xB6);
    vTaskDelay(pdMS_TO_TICKS(100));

    // Load factory calibration data
    bme280_read_calibration();

    // Configure sensor for continuous operation
    bme_write(REG_CTRL_HUM, 0x01);
    bme_write(REG_CTRL_MEAS, 0x27);
    bme_write(REG_CONFIG, 0xA0);

    ESP_LOGI(TAG, "BME280 initialized successfully");
}

void app_main(void)
{
    // Initialize BME280 sensor and I2C bus
    bme280_init();

    while (1) {
        uint8_t data[8];
        
        // Read all raw data in one burst (temperature + humidity + pressure)
        bme_read(REG_DATA, data, 8);
        // Prepare data for compensation formulas
        int32_t adc_T = (data[3] << 12) | (data[4] << 4) | (data[5] >> 4);
        int32_t adc_P = (data[0] << 12) | (data[1] << 4) | (data[2] >> 4);
        int32_t adc_H = (data[6] << 8) | data[7];

        // Apply sensor compensation
        float tempC = compensate_temp(adc_T);
        float tempF = tempC * 9.0 / 5.0 + 32.0;
        float hum = compensate_humidity(adc_H);
        float press = compensate_pressure(adc_P);

        // Print raw sensor values
        ESP_LOGI(TAG,
            "RAW  TEMP:%ld  HUM:%ld  PRESS:%ld", adc_T, adc_H, adc_P);
        // Print compensated values in the requested format
        ESP_LOGI(TAG,
            "TEMP: %.2f °C | %.2f °F  HUM: %.2f %%  PRESS: %.2f hPa",
            tempC, tempF, hum, press);

        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
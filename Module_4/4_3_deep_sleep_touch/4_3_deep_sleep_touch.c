/*  
  Rui Santos & Sara Santos - Random Nerd Tutorials
  Learn more with our eBook » https://RandomNerdTutorials.com/esp-idf-esp32-ebook/
  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files.
  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
*/

#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "driver/touch_sens.h"
#include "esp_sleep.h"

static const char *TAG = "touch_wakeup";

#define TOUCH_CH 8

// Touch sensor configuration
#if SOC_TOUCH_SENSOR_VERSION == 1       // ESP32
    #define SAMPLE_CONFIG       TOUCH_SENSOR_V1_DEFAULT_SAMPLE_CONFIG(5.0, TOUCH_VOLT_LIM_L_0V5, TOUCH_VOLT_LIM_H_1V7)
    #define ACTIVE_THRESH_RATIO 0.98

    #define CHAN_CFG_DEFAULT()  { \
        .abs_active_thresh = {1000}, \
        .charge_speed = TOUCH_CHARGE_SPEED_7, \
        .init_charge_volt = TOUCH_INIT_CHARGE_VOLT_DEFAULT, \
        .group = TOUCH_CHAN_TRIG_GROUP_BOTH, \
    }
#elif SOC_TOUCH_SENSOR_VERSION == 2     // ESP32-S2 / S3
    #define SAMPLE_CONFIG       TOUCH_SENSOR_V2_DEFAULT_SAMPLE_CONFIG(500, TOUCH_VOLT_LIM_L_0V5, TOUCH_VOLT_LIM_H_2V2)
    #define ACTIVE_THRESH_RATIO 0.02

    #define CHAN_CFG_DEFAULT()  { \
        .active_thresh = {2000}, \
        .charge_speed = TOUCH_CHARGE_SPEED_7, \
        .init_charge_volt = TOUCH_INIT_CHARGE_VOLT_DEFAULT, \
    }
#elif SOC_TOUCH_SENSOR_VERSION == 3     // ESP32-P4
    #define SAMPLE_CONFIG       TOUCH_SENSOR_V3_DEFAULT_SAMPLE_CONFIG2(3, 29, 8, 3), \
                                TOUCH_SENSOR_V3_DEFAULT_SAMPLE_CONFIG2(2, 88, 31, 7), \
                                TOUCH_SENSOR_V3_DEFAULT_SAMPLE_CONFIG2(3, 10, 31, 7)
    #define ACTIVE_THRESH_RATIO 0.02

    #define CHAN_CFG_DEFAULT()  { \
        .active_thresh = {1000, 2500, 5000}, \
    }
#else
    #error "Board target not supported"
#endif

// Function to do touch sensor calibration (benchmark + threshold setting)
static void calibrate_touch(touch_sensor_handle_t sens, touch_channel_handle_t chan)
{
    ESP_ERROR_CHECK(touch_sensor_enable(sens));
    for (int i = 0; i < 3; i++) {
        ESP_ERROR_CHECK(touch_sensor_trigger_oneshot_scanning(sens, 2000));
    }
    ESP_ERROR_CHECK(touch_sensor_disable(sens));

    uint32_t benchmark = 0;
#if SOC_TOUCH_SUPPORT_BENCHMARK
    touch_channel_read_data(chan, TOUCH_CHAN_DATA_TYPE_BENCHMARK, &benchmark);
#else
    touch_channel_read_data(chan, TOUCH_CHAN_DATA_TYPE_SMOOTH, &benchmark);
#endif

    touch_channel_config_t chan_cfg = CHAN_CFG_DEFAULT();

#if SOC_TOUCH_SENSOR_VERSION == 1
    chan_cfg.abs_active_thresh[0] = (uint32_t)(benchmark * ACTIVE_THRESH_RATIO);
#else
    chan_cfg.active_thresh[0]     = (uint32_t)(benchmark * ACTIVE_THRESH_RATIO);
#endif

    ESP_ERROR_CHECK(touch_sensor_reconfig_channel(chan, &chan_cfg));
    ESP_LOGI(TAG, "Benchmark: %lu - threshold set", benchmark);
}

void app_main(void)
{
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TOUCHPAD) {
        ESP_LOGI(TAG, "Woke up from touch sensor!");
    }

    touch_sensor_handle_t sens = NULL;
    touch_channel_handle_t chan = NULL;

    // Touch sensor controller
    touch_sensor_sample_config_t sample_cfg[TOUCH_SAMPLE_CFG_NUM] = { SAMPLE_CONFIG };
    touch_sensor_config_t sens_cfg = TOUCH_SENSOR_DEFAULT_BASIC_CONFIG(TOUCH_SAMPLE_CFG_NUM, sample_cfg);

    ESP_ERROR_CHECK(touch_sensor_new_controller(&sens_cfg, &sens));

    // Configure touch sensor channel
    touch_channel_config_t chan_cfg = CHAN_CFG_DEFAULT();
    ESP_ERROR_CHECK(touch_sensor_new_channel(sens, TOUCH_CH, &chan_cfg, &chan));

    // Filter
    touch_sensor_filter_config_t filter_cfg = TOUCH_SENSOR_DEFAULT_FILTER_CONFIG();
    ESP_ERROR_CHECK(touch_sensor_config_filter(sens, &filter_cfg));

    // Calibrate touch sensor
    calibrate_touch(sens, chan);

    // Deep sleep touch wakeup
    touch_sleep_config_t sleep_cfg = TOUCH_SENSOR_DEFAULT_DSLP_CONFIG();
    ESP_ERROR_CHECK(touch_sensor_config_sleep_wakeup(sens, &sleep_cfg));

    // Start continuous touch sensor scanning
    ESP_ERROR_CHECK(touch_sensor_enable(sens));
    ESP_ERROR_CHECK(touch_sensor_start_continuous_scanning(sens));

    ESP_LOGI(TAG, "Going to deep sleep. The ESP32 wakes up when you touch GPIO %d...", TOUCH_CH);
    // Start deep sleep mode
    esp_deep_sleep_start();
}
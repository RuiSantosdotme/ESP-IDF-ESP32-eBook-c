/*  
  Rui Santos & Sara Santos - Random Nerd Tutorials
  Learn more with our eBook » https://RandomNerdTutorials.com/esp-idf-esp32-ebook/
  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files.
  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
*/

#include <stdio.h>
#include "esp_sleep.h"
#include "esp_log.h"
#include "driver/rtc_io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define WAKEUP_GPIO    GPIO_NUM_4

void app_main(void)
{
    esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();

    if (wakeup_cause == ESP_SLEEP_WAKEUP_EXT0) {
        ESP_LOGI("DEEP", "» ESP32 Woke up from EXT0 on GPIO %d «", WAKEUP_GPIO);
        rtc_gpio_deinit(WAKEUP_GPIO);
    } else {
        ESP_LOGI("DEEP", "» FIRST BOOT «");
    }

    // Enable EXT0 wakeup on HIGH level (change the parameter to 0 for active low)
    ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(WAKEUP_GPIO, 1));

    ESP_LOGI("DEEP", "Going to deep sleep. The ESP32 wakes up when GPIO %d is HIGH...", WAKEUP_GPIO);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    esp_deep_sleep_start();
}
/*  
  Rui Santos & Sara Santos - Random Nerd Tutorials
  Learn more with our eBook » https://RandomNerdTutorials.com/esp-idf-esp32-ebook/
  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files.
  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
*/

#include <stdio.h>
#include "esp_sleep.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static RTC_DATA_ATTR int boot_count = 0;

void app_main(void)
{
    esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();

    if (wakeup_cause == ESP_SLEEP_WAKEUP_TIMER) {
        ESP_LOGI("DEEP", "» ESP32 woke up from timer «");
        boot_count++;
    } else {
        ESP_LOGI("DEEP", "» FIRST BOOT «");
    }

    ESP_LOGI("DEEP", "Boot count = %d", boot_count);

    // Wake up every 20 seconds
    esp_sleep_enable_timer_wakeup(20 * 1000000ULL);

    ESP_LOGI("DEEP", "Entering deep sleep mode for 20 seconds...\n");
    vTaskDelay(100 / portTICK_PERIOD_MS);
    esp_deep_sleep_start();
}
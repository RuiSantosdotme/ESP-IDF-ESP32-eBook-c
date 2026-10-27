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

#define GPIO_BTN1    GPIO_NUM_4
#define GPIO_BTN2    GPIO_NUM_5

void app_main(void)
{
    uint64_t wakeup_status = esp_sleep_get_ext1_wakeup_status();

    if (wakeup_status != 0) {
        ESP_LOGI("DEEP", "» ESP32 woke up from EXT1 «");

        if (wakeup_status & (1ULL << GPIO_BTN1)){
            ESP_LOGI("DEEP", "GPIO %d triggered", GPIO_BTN1);
        }
        if (wakeup_status & (1ULL << GPIO_BTN2)) {
            ESP_LOGI("DEEP", "GPIO %d triggered", GPIO_BTN2);
        }
    } else {
        ESP_LOGI("DEEP", "» FIRST BOOT «");
    }

    uint64_t mask = (1ULL << GPIO_BTN1) | (1ULL << GPIO_BTN2);

    ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup_io(mask, ESP_EXT1_WAKEUP_ANY_HIGH));

    ESP_LOGI("DEEP", "Going to deep sleep. The ESP32 wakes up when GPIO 4 or GPIO 5 is HIGH...");
    vTaskDelay(100 / portTICK_PERIOD_MS);
    esp_deep_sleep_start();
}
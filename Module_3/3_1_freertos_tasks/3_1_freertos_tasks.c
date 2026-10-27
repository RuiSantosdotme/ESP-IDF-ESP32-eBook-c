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
#include "esp_timer.h"
#include "esp_rom_sys.h"

#define LED_PIN     2
#define BUTTON_PIN  0

static const char *TAG = "TASK";

// Task handle for the BlinkTask
static TaskHandle_t BlinkTaskHandle = NULL;

// Flag to indicate if the BlinkTask is suspended
static volatile bool taskSuspended = false;

// Debounce variables
static volatile int64_t lastInterruptTime = 0;
static const int64_t debounceDelay_us = 100 * 1000; 

// Task delay for blinking the LED (1000ms = 1s)
#define BLINK_INTERVAL_MS 1000

// Interrupt Service Routine for the button
static void IRAM_ATTR button_isr_handler(void *arg)
{
    // Button debounce logic
    int64_t now = esp_timer_get_time();
    if (now - lastInterruptTime < debounceDelay_us) {
        return;
    }
    lastInterruptTime = now;

    // Toggle the taskSuspended flag and suspend/resume the BlinkTask
    taskSuspended = !taskSuspended;
    if (taskSuspended) {
        vTaskSuspend(BlinkTaskHandle);
        esp_rom_printf("BlinkTask Suspended!\n");
    } else {
        vTaskResume(BlinkTaskHandle);
        esp_rom_printf("BlinkTask Resumed!\n");
    }
}

// BlinkTask function
static void BlinkTask(void *parameter)
{
    ESP_LOGI(TAG, "BlinkTask: Runs on core %d", xPortGetCoreID());
    while (1) {
        // Turn the LED on and off with a delay
        gpio_set_level(LED_PIN, 1);
        ESP_LOGI(TAG, "BlinkTask: LED ON");
        vTaskDelay(pdMS_TO_TICKS(BLINK_INTERVAL_MS));

        gpio_set_level(LED_PIN, 0);
        ESP_LOGI(TAG, "BlinkTask: LED OFF");
        vTaskDelay(pdMS_TO_TICKS(BLINK_INTERVAL_MS));
    }
}

void app_main(void)
{
    // Configure GPIO
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << LED_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE
    };
    gpio_config(&led_conf);

    // Configure Button
    gpio_config_t btn_conf = {
        .pin_bit_mask = (1ULL << BUTTON_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE
    };
    gpio_config(&btn_conf);

    // Install ISR service and add the button ISR handler
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_PIN, button_isr_handler, NULL);

    // Create the BlinkTask pinned to core 1
    xTaskCreatePinnedToCore(BlinkTask, "BlinkTask", 4096, NULL, 1, &BlinkTaskHandle, 1);

    ESP_LOGI(TAG, "Ready. Press on-board BOOT button to suspend/resume the BlinkTask.");
}
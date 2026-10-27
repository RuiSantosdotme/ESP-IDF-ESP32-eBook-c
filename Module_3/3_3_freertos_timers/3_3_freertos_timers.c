/*  
  Rui Santos & Sara Santos - Random Nerd Tutorials
  Learn more with our eBook » https://RandomNerdTutorials.com/esp-idf-esp32-ebook/
  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files.
  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
*/

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define LED_PIN 2

// Define the blink period in milliseconds 1000ms = 1s
#define BLINK_PERIOD_MS 1000

static const char *TAG = "TIMER";

// Timer handle for the LED blink timer
static TimerHandle_t blinkTimer = NULL;

// Variable to keep track of the LED state
static bool ledState = false;

// Timer callback function to toggle the LED state
static void BlinkCallback(TimerHandle_t xTimer)
{
    ledState = !ledState;
    // Toggle the LED state based on the ledState variable
    if (ledState) {
        gpio_set_level(LED_PIN, 1);
        ESP_LOGI(TAG, "LED is ON");
    } else {
        gpio_set_level(LED_PIN, 0);
        ESP_LOGI(TAG, "LED is OFF");
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting a FreeRTOS Periodic Timer for LED Blink");

    // Configure GPIO
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << LED_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE
    };
    gpio_config(&led_conf);

    // Start with LED off
    gpio_set_level(LED_PIN, 0);

    // Create the software timer (periodic, 1 second)
    blinkTimer = xTimerCreate("BlinkTimer", pdMS_TO_TICKS(BLINK_PERIOD_MS), pdTRUE, NULL, BlinkCallback);

    // Check if timer was created successfully
    if (blinkTimer == NULL) {
        ESP_LOGE(TAG, "Failed to create timer!");
        return;
    }

    // Start the timer immediately
    if (xTimerStart(blinkTimer, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start timer!");
        return;
    }
}
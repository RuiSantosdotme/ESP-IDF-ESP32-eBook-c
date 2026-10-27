/*  
  Rui Santos & Sara Santos - Random Nerd Tutorials
  Learn more with our eBook » https://RandomNerdTutorials.com/esp-idf-esp32-ebook/
  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files.
  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
*/

#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include "sdkconfig.h"

#define LED_PIN_1 2
#define LED_PIN_2 4
#define LED_PIN_3 5

void app_main(void)
{
    // Configure multiple GPIOs
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_PIN_1) | (1ULL << LED_PIN_2) | (1ULL << LED_PIN_3),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    while (1) {
        // Turn all LEDs ON
        gpio_set_level(LED_PIN_1, 1);
        gpio_set_level(LED_PIN_2, 1);
        gpio_set_level(LED_PIN_3, 1);
        vTaskDelay(1000 / portTICK_PERIOD_MS);

        // Turn all LEDs OFF
        gpio_set_level(LED_PIN_1, 0);
        gpio_set_level(LED_PIN_2, 0);
        gpio_set_level(LED_PIN_3, 0);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
/*  
  Rui Santos & Sara Santos - Random Nerd Tutorials
  Learn more with our eBook » https://RandomNerdTutorials.com/esp-idf-esp32-ebook/
  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files.
  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
*/

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

static const char *TAG = "QUEUE";

// How many items the queue can hold
#define QUEUE_LENGTH 5

// The queue handle
QueueHandle_t number_queue;

// Sender task: sends numbers to the queue
void sender_task(void *pvParameters)
{
    int value = 0;

    while (1) {
        if (xQueueSend(number_queue, &value, portMAX_DELAY) == pdPASS) {
            ESP_LOGI(TAG, "Sent %d (spaces left: %u)",
                     value, uxQueueSpacesAvailable(number_queue));
        }
        value++;
        vTaskDelay(pdMS_TO_TICKS(1300));
    }
}

// Receiver task: receives numbers from the queue
void receiver_task(void *pvParameters)
{
    int received;

    while (1) {
        if (xQueueReceive(number_queue, &received, portMAX_DELAY) == pdPASS) {
            ESP_LOGI(TAG, "Received %d (numbers waiting: %u)",
                     received, uxQueueMessagesWaiting(number_queue));
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void app_main(void)
{
    // Create a queue capable of containing QUEUE_LENGTH
    number_queue = xQueueCreate(QUEUE_LENGTH, sizeof(int));
    
    // Check if the queue was created successfully
    if (number_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create queue");
        return;
    }

    // Create the sender and receiver tasks
    xTaskCreate(sender_task, "sender", 2048, NULL, 5, NULL);
    xTaskCreate(receiver_task, "receiver", 2048, NULL, 5, NULL);
}
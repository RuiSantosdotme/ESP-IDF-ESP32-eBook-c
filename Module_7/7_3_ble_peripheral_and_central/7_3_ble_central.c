/*  
  Rui Santos & Sara Santos - Random Nerd Tutorials
  Learn more with our eBook » https://RandomNerdTutorials.com/esp-idf-esp32-ebook/
  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files.
  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
*/
// BLE Central device: scans for GPIO control service, connects,
// and writes 0x00/0x01 on button press, it toggles the GPIO LED from the BLE Peripheral device

#include <assert.h>
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"

// You can replace these UUIDs with your own, but they must match the BLE Peripheral exactly for the BLE connection to work.
// GPIO Control Service UUID: must match the BLE Peripheral exactly
static const ble_uuid128_t gpio_svc_uuid =
    BLE_UUID128_INIT(0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                     0x88, 0x99, 0xaa, 0xbb, 0x00, 0x00, 0xa0, 0x1a);

// GPIO State Characteristic (write, with response): must match the BLE Peripheral exactly
static const ble_uuid128_t gpio_chr_uuid =
    BLE_UUID128_INIT(0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                     0x88, 0x99, 0xaa, 0xbb, 0x01, 0x00, 0xa0, 0x1a);

// ESP32 GPIO Central, it's the advertised device name
#define DEVICE_NAME "ESP32_GPIO_Central"

// The name of the BLE Peripheral device we want to connect to. It must match your peripheral's advertised name
#define TARGET_PERIPH_NAME "ESP32_GPIO_Peripheral"

// ESP32 on-board BOOT Pushbutton input pin (active LOW, internal pull-up)
#define BUTTON_GPIO 0
#define DEBOUNCE_MS 100

static const char *TAG = "BLE_CENTRAL";

// Connection and characteristic handles
static uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t gpio_chr_val_handle = 0;
static bool chr_discovered = false;

// Last value written to the GPIO State characteristic
static uint8_t last_written_val = 0;

// Own address type, set by the BLE host when it starts up
static uint8_t own_addr_type;

// Debounce timer and ISR flag for button press handling
static TimerHandle_t debounce_timer;
static volatile bool button_isr_flag = false;

static void ble_app_scan(void);

// GPIO button ISR: sets a flag to indicate the button was pressed
static void IRAM_ATTR button_isr_handler(void *arg)
{
    button_isr_flag = true;
}

static void write_gpio_state(uint8_t value);

// Debounce timer callback 
static void debounce_timer_cb(TimerHandle_t xTimer)
{
    // After debounce, re-check the button. If still pressed, toggle and write the new GPIO value
    if (gpio_get_level(BUTTON_GPIO) == 0) {
        uint8_t new_val = last_written_val ? 0 : 1;
        write_gpio_state(new_val);
    }
    xTimerStop(debounce_timer, 0);
}

// Polling task: checks the ISR flag and starts the debounce timer if set
static void button_task_poll(void)
{
    if (button_isr_flag) {
        button_isr_flag = false;
        xTimerStart(debounce_timer, 0);
    }
}

// Initializes the GPIO button input with pull-up and interrupt on falling edge
static void button_gpio_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(BUTTON_GPIO, button_isr_handler, NULL));

    debounce_timer = xTimerCreate("debounce",
                                   pdMS_TO_TICKS(DEBOUNCE_MS),
                                   pdFALSE, NULL, debounce_timer_cb);
    assert(debounce_timer != NULL);
}


// GATT write callback: called when a write response is received from the peripheral
static int on_write_cb(uint16_t conn_hdl, const struct ble_gatt_error *error, 
                       struct ble_gatt_attr *attr, void *arg)
{
    uint8_t sent_val = (uint8_t)(uintptr_t)arg;

    if (error->status == 0) {
        last_written_val = sent_val;
        ESP_LOGI(TAG, "GPIO state write OK -> %s",
                 sent_val ? "ON (1)" : "OFF (0)");
    } else {
        ESP_LOGW(TAG, "GPIO state write failed; status=%d", error->status);
    }
    return 0;
}

// Writes the given value (0 or 1) to the GPIO State characteristic on the BLE Peripheral
static void write_gpio_state(uint8_t value)
{
    if (conn_handle == BLE_HS_CONN_HANDLE_NONE || !chr_discovered) {
        ESP_LOGW(TAG, "Not connected / characteristic not ready; write skipped");
        return;
    }

    int rc = ble_gattc_write_flat(conn_handle, gpio_chr_val_handle, &value, sizeof(value), 
                                  on_write_cb, (void *)(uintptr_t)value);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gattc_write_flat failed; rc=%d", rc);
    }
}

// Characteristic discovery callback: looks for the GPIO State characteristic
static int on_chr_disc_cb(uint16_t conn_hdl, const struct ble_gatt_error *error,
                           const struct ble_gatt_chr *chr, void *arg)
{
    if (error->status == 0 && chr != NULL) {
        if (ble_uuid_cmp(&chr->uuid.u, &gpio_chr_uuid.u) == 0) {
            gpio_chr_val_handle = chr->val_handle;
            chr_discovered = true;
            ESP_LOGI(TAG, "Discovered GPIO State characteristic; handle=%d",
                     gpio_chr_val_handle);
        }
        return 0;
    }

    if (error->status != BLE_HS_EDONE) {
        ESP_LOGW(TAG, "Characteristic discovery error; status=%d", error->status);
    } else if (!chr_discovered) {
        ESP_LOGW(TAG, "GPIO State characteristic not found on peripheral");
    }
    return 0;
}

// Service discovery callback: looks for the GPIO Control service and starts characteristic discovery
static int on_svc_disc_cb(uint16_t conn_hdl, const struct ble_gatt_error *error,
                          const struct ble_gatt_svc *service, void *arg)
{
    if (error->status == 0 && service != NULL) {
        if (ble_uuid_cmp(&service->uuid.u, &gpio_svc_uuid.u) == 0) {
            ESP_LOGI(TAG, "Found GPIO Control service; discovering characteristics");
            int rc = ble_gattc_disc_all_chrs(conn_hdl,
                                             service->start_handle,
                                             service->end_handle,
                                             on_chr_disc_cb, NULL);
            if (rc != 0) {
                ESP_LOGE(TAG, "disc_all_chrs failed; rc=%d", rc);
            }
        }
        return 0;
    }

    if (error->status != BLE_HS_EDONE) {
        ESP_LOGW(TAG, "Service discovery error; status=%d", error->status);
    } else if (!chr_discovered) {
        ESP_LOGW(TAG, "GPIO Control service not found on peripheral");
    }
    return 0;
}

// Checks if the advertisement fields contain the target peripheral name
static bool adv_fields_have_target_name(const struct ble_hs_adv_fields *fields)
{
    if (TARGET_PERIPH_NAME == NULL) {
        return true;
    }
    if (fields->name == NULL || fields->name_len == 0) {
        return false;
    }
    size_t target_len = strlen(TARGET_PERIPH_NAME);
    if (fields->name_len != target_len) {
        return false;
    }
    return memcmp(fields->name, TARGET_PERIPH_NAME, target_len) == 0;
}

static int ble_gap_event(struct ble_gap_event *event, void *arg);

// Connects to the BLE Peripheral with the given address
static void connect_to_peripheral(const ble_addr_t *addr)
{
    int rc = ble_gap_disc_cancel();
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGW(TAG, "disc_cancel rc=%d (continuing)", rc);
    }

    rc = ble_gap_connect(own_addr_type, addr, 30000, NULL,
                          ble_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_connect failed; rc=%d", rc);
        ble_app_scan();
    }
}

// Starts scanning for BLE peripherals advertising the GPIO Control service
static void ble_app_scan(void)
{
    struct ble_gap_disc_params disc_params = {0};
    disc_params.filter_duplicates = 1;
    disc_params.passive = 0;
    disc_params.itvl = 0;
    disc_params.window = 0;
    disc_params.filter_policy = BLE_HCI_SCAN_FILT_NO_WL;
    disc_params.limited = 0;

    ESP_LOGI(TAG, "scanning for peripheral...");
    int rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &disc_params,
                           ble_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_disc failed; rc=%d", rc);
    }
}

// GAP event callback: handles discovery, connection, and disconnection events
static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_hs_adv_fields fields;
    int rc;

    switch (event->type) {

    case BLE_GAP_EVENT_DISC:
        rc = ble_hs_adv_parse_fields(&fields, event->disc.data,
                                      event->disc.length_data);
        if (rc != 0) {
            return 0;
        }

        bool name_match = adv_fields_have_target_name(&fields);
        bool uuid_match = false;
        for (int i = 0; i < fields.num_uuids128 && !uuid_match; i++) {
            if (ble_uuid_cmp(&fields.uuids128[i].u, &gpio_svc_uuid.u) == 0) {
                uuid_match = true;
            }
        }

        if (name_match || uuid_match) {
            ESP_LOGI(TAG, "target peripheral found; connecting");
            connect_to_peripheral(&event->disc.addr);
        }
        return 0;

    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            conn_handle = event->connect.conn_handle;
            chr_discovered = false;
            ESP_LOGI(TAG, "connected; conn_handle=%d", conn_handle);

            rc = ble_gattc_disc_svc_by_uuid(conn_handle, &gpio_svc_uuid.u,
                                             on_svc_disc_cb, NULL);
            if (rc != 0) {
                ESP_LOGE(TAG, "disc_svc_by_uuid failed; rc=%d", rc);
            }
        } else {
            ESP_LOGW(TAG, "connect failed; status=%d, resuming scan",
                     event->connect.status);
            conn_handle = BLE_HS_CONN_HANDLE_NONE;
            ble_app_scan();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected; reason=%d", event->disconnect.reason);
        conn_handle = BLE_HS_CONN_HANDLE_NONE;
        chr_discovered = false;
        gpio_chr_val_handle = 0;
        ble_app_scan();
        return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        ESP_LOGI(TAG, "scan complete; reason=%d", event->disc_complete.reason);
        return 0;

    default:
        return 0;
    }
}

// Called when the BLE host and controller are synchronized and ready to use
static void on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    assert(rc == 0);
    ble_app_scan();
}

// When the host resets, it logs the reason and restarts advertising
static void on_reset(int reason)
{
    ESP_LOGE(TAG, "BLE host reset; reason=%d", reason);
}

// NimBLE host task - runs in the background all BLE events
static void host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// Application task: polls the button ISR flag and starts the debounce timer
static void app_task(void *param)
{
    for (;;) {
        button_task_poll();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize the GPIO button input and debounce timer
    button_gpio_init();

    // Initialize NimBLE host
    ESP_ERROR_CHECK(nimble_port_init());
    // Configure the host
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    // Initialize the standard GAP (Generic Access Profile) service
    ble_svc_gap_init();

    // Initialize the standard GAP (Generic Access Profile) service
    int rc = ble_svc_gap_device_name_set(DEVICE_NAME);
    assert(rc == 0);

    // Start the host task
    nimble_port_freertos_init(host_task);
    // Start the application task that checks the button ISR flag
    xTaskCreate(app_task, "app_task", 2048, NULL, 5, NULL);
    ESP_LOGI(TAG, "BLE Central started; waiting for BLE Peripheral...");
}
/*  
  Rui Santos & Sara Santos - Random Nerd Tutorials
  Learn more with our eBook » https://RandomNerdTutorials.com/esp-idf-esp32-ebook/
  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files.
  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
*/

#include <assert.h>
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOSConfig.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#define ESS_UUID            0x181A   // Environmental Sensing Service
#define ESS_TEMP_UUID       0x2A6E   // Temperature Characteristic

#define DEVICE_NAME         "ESP32_Temp"  // BLE device name advertised

// Temperature simulation parameters
#define TEMP_INIT           2000     // 20.00 °C in 0.01 °C units
#define TEMP_MAX            3000     // 30.00 °C      
#define TEMP_STEP           10       // 0.10 °C per tick 
#define NOTIFY_INTERVAL_MS  1000     // 1 second between notifications

// Tag for logging
static const char *TAG = "ESS_TEMP";

// Used to identify the temperature characteristic in GATT operations
static uint16_t temp_val_handle;

// Simulated temperature value
static int16_t temp_val = TEMP_INIT;

// Connection state for the current client
static uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;
// Notification state for the current client
static bool notify_state = false;

// Bluetooth address type
static uint8_t own_addr_type;

// FreeRTOS timer for periodic notifications
static TimerHandle_t temp_timer;

// GATT access callback for the temperature characteristic
static int temp_access_cb(uint16_t conn_hdl, uint16_t attr_hdl,
               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int rc = os_mbuf_append(ctxt->om, &temp_val, sizeof(temp_val));
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}   

// GATT service definition for the Environmental Sensing Service (ESS)
static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(ESS_UUID),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                // Temperature characteristics: READ and NOTIFY
                .uuid       = BLE_UUID16_DECLARE(ESS_TEMP_UUID),
                .access_cb  = temp_access_cb,
                .val_handle = &temp_val_handle,
                .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 },
        },
    },
    { 0 },
};

// FreeRTOS timer callback for periodic temperature notifications
static void temp_timer_cb(TimerHandle_t ev)
{
    temp_val += TEMP_STEP;
    if (temp_val > TEMP_MAX) {
        temp_val = TEMP_INIT;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(&temp_val, sizeof(temp_val));
    int rc = ble_gatts_notify_custom(conn_handle, temp_val_handle, om);
    assert(rc == 0);

    ESP_LOGI(TAG, "notify: %d (%.2f C)", temp_val, temp_val / 100.0f);
}

static int ble_gap_event(struct ble_gap_event *event, void *arg);

// Called when the BLE host and controller are synchronized and ready to use
static void ess_advertise(void)
{
    struct ble_hs_adv_fields fields = {0};
    struct ble_gap_adv_params params = {0};

    fields.flags            = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name             = (uint8_t *)DEVICE_NAME;
    fields.name_len         = strlen(DEVICE_NAME);
    fields.name_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    assert(rc == 0);

    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &params, ble_gap_event, NULL);
    assert(rc == 0);
}

// Called when a GAP event occurs (connect, disconnect, subscribe, etc..)
static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {

    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "connect; status=%d", event->connect.status);
        if (event->connect.status != 0) {
            ess_advertise();
        } else {
            conn_handle = event->connect.conn_handle;
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnect; reason=%d", event->disconnect.reason);
        notify_state = false;
        xTimerStop(temp_timer, portMAX_DELAY);
        conn_handle = BLE_HS_CONN_HANDLE_NONE;
        ess_advertise();
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ess_advertise();
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == temp_val_handle) {
            notify_state = event->subscribe.cur_notify;
            ESP_LOGI(TAG, "temperature notify %s",
                     notify_state ? "enabled" : "disabled");
            if (notify_state) {
                xTimerStart(temp_timer, portMAX_DELAY);
            } else {
                xTimerStop(temp_timer, portMAX_DELAY);
            }
        }
        break;
    }

    return 0;
}

// Called when the BLE host and controller are synchronized and ready to use
static void on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    assert(rc == 0);
    ess_advertise();
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

void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize NimBLE host
    ESP_ERROR_CHECK(nimble_port_init());
    // Configure the host
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    // Initialize the standard GAP (Generic Access Profile) service
    ble_svc_gap_init();
    // Initialize the standard GATT (Generic Attribute Profile) service
    ble_svc_gatt_init();

    // Count and add register the GATT services
    int rc = ble_gatts_count_cfg(gatt_svcs);
    assert(rc == 0);
    rc = ble_gatts_add_svcs(gatt_svcs);
    assert(rc == 0);

    // Set the device name that will be advertised
    rc = ble_svc_gap_device_name_set(DEVICE_NAME);
    assert(rc == 0);

    // 1 second periodic timer for BLE temperature notifications
    temp_timer = xTimerCreate("temp_timer",
                              pdMS_TO_TICKS(NOTIFY_INTERVAL_MS),
                              pdTRUE, NULL, temp_timer_cb);
    assert(temp_timer != NULL);
    
    // Start the host task
    nimble_port_freertos_init(host_task);
    ESP_LOGI(TAG, "waiting for BLE client to connect...");
}
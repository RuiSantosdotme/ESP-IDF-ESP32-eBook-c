/*  
  Rui Santos & Sara Santos - Random Nerd Tutorials
  Learn more with our eBook » https://RandomNerdTutorials.com/esp-idf-esp32-ebook/
  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files.
  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
*/
// BLE Peripheral device: advertises GPIO Control service
// the other ESP32 (BLE Central device) writes 0x01/0x00 to toggle GPIO LED

#include <assert.h>
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

// You can replace these UUIDs with your own, but they must match the BLE Central exactly for the BLE connection to work.
// GPIO Control Service UUID: must match the BLE Central exactly
static const ble_uuid128_t gpio_svc_uuid =
    BLE_UUID128_INIT(0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                     0x88, 0x99, 0xaa, 0xbb, 0x00, 0x00, 0xa0, 0x1a);

// GPIO State Characteristic (write, with response): must match the BLE Central exactly
static const ble_uuid128_t gpio_chr_uuid =
    BLE_UUID128_INIT(0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                     0x88, 0x99, 0xaa, 0xbb, 0x01, 0x00, 0xa0, 0x1a);

// ESP32 GPIO Peripheral, it's the advertised device name
#define DEVICE_NAME "ESP32_GPIO_Peripheral"

// LED GPIO
#define LED_GPIO 2

static const char *TAG = "BLE_PERIPHERAL";

// Handle for the GPIO State characteristic
static uint16_t gpio_chr_val_handle;

// Current logical GPIO state (0/1), mirrors what's driven on LED_GPIO
static uint8_t gpio_state = 0;

// Connection characteristic handle, set when a connection is established
static uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;
// Own address type, set by the BLE host when it starts up
static uint8_t own_addr_type;

static void gpio_advertise(void);

// Initialize the GPIO for the LED
static void led_gpio_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << LED_GPIO,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(LED_GPIO, gpio_state);
}

// GATT access callback for the GPIO State characteristic
static int gpio_state_access_cb(uint16_t conn_hdl, uint16_t attr_hdl,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    switch (ctxt->op) {
    
    // Handle read operation for the GPIO State characteristic
    case BLE_GATT_ACCESS_OP_READ_CHR: {
        int rc = os_mbuf_append(ctxt->om, &gpio_state, sizeof(gpio_state));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    // Handle write operation for the GPIO State characteristic
    case BLE_GATT_ACCESS_OP_WRITE_CHR: {
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        if (len != sizeof(uint8_t)) {
            ESP_LOGW(TAG, "write rejected: expected 1 byte, got %d", len);
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }

        uint8_t new_val = 0;
        int rc = ble_hs_mbuf_to_flat(ctxt->om, &new_val, sizeof(new_val), NULL);
        if (rc != 0) {
            return BLE_ATT_ERR_UNLIKELY;
        }

        gpio_state = new_val ? 1 : 0;
        gpio_set_level(LED_GPIO, gpio_state);

        ESP_LOGI(TAG, "GPIO write received -> %s", gpio_state ? "ON (1)" : "OFF (0)");

        return 0;
    }

    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}

// GATT service definition for the custom GPIO Control service
static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &gpio_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                // GPIO State Characteristic
                .uuid       = &gpio_chr_uuid.u,
                .access_cb  = gpio_state_access_cb,
                .val_handle = &gpio_chr_val_handle,
                .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
            },
            { 0 },
        },
    },
    { 0 },
};

// GAP: advertising and connection events
static int ble_gap_event(struct ble_gap_event *event, void *arg);

// Called when the BLE host and controller are synchronized and ready to use
static void gpio_advertise(void)
{
    struct ble_hs_adv_fields fields = {0};
    struct ble_hs_adv_fields rsp_fields = {0};
    struct ble_gap_adv_params params = {0};

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    fields.uuids128 = (ble_uuid128_t *)&gpio_svc_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    assert(rc == 0);

    rsp_fields.name = (uint8_t *)DEVICE_NAME;
    rsp_fields.name_len = strlen(DEVICE_NAME);
    rsp_fields.name_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    assert(rc == 0);

    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &params, ble_gap_event, NULL);
    assert(rc == 0);
}

// Called when a GAP event occurs (connect, disconnect, etc..)
static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {

    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "connect; status=%d", event->connect.status);
        if (event->connect.status != 0) {
            gpio_advertise();
        } else {
            conn_handle = event->connect.conn_handle;
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnect; reason=%d", event->disconnect.reason);
        conn_handle = BLE_HS_CONN_HANDLE_NONE;
        gpio_advertise();
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        gpio_advertise();
        break;

    default:
        break;
    }

    return 0;
}

// Called when the BLE host and controller are synchronized and ready to use
static void on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    assert(rc == 0);
    gpio_advertise();
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

    // Initialize the GPIO for the LED
    led_gpio_init();

    // Initialize NimBLE host
    ESP_ERROR_CHECK(nimble_port_init());
    // Configure the host
    ble_hs_cfg.sync_cb  = on_sync;
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

    // Start the host task
    nimble_port_freertos_init(host_task);
    ESP_LOGI(TAG, "BLE Peripheral started (LED=%s); waiting for BLE Central...",
                   gpio_state ? "ON" : "OFF");
}
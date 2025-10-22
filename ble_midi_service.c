#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/sys/printk.h>
#include "ble_midi_service.h"

// BLE MIDI Service UUID: 03B80E5A-EDE8-4B33-A751-6CE34EC4C700
#define BT_UUID_MIDI_SERVICE_VAL \
    BT_UUID_128_ENCODE(0x03B80E5A, 0xEDE8, 0x4B33, 0xA751, 0x6CE34EC4C700)

// MIDI I/O Characteristic UUID: 7772E5DB-3868-4112-A1A9-F2669D106BF3
#define BT_UUID_MIDI_IO_VAL \
    BT_UUID_128_ENCODE(0x7772E5DB, 0x3868, 0x4112, 0xA1A9, 0xF2669D106BF3)

#define BT_UUID_MIDI_SERVICE  BT_UUID_DECLARE_128(BT_UUID_MIDI_SERVICE_VAL)
#define BT_UUID_MIDI_IO       BT_UUID_DECLARE_128(BT_UUID_MIDI_IO_VAL)

// Connection tracking
static struct bt_conn *current_conn = NULL;
static bool notify_enabled = false;

// BLE Status LED
static const struct gpio_dt_spec *ble_status_led_ptr = NULL;

// MIDI data buffer (for read operations - optional)
static uint8_t midi_data_buf[20] = {0};
static uint8_t midi_data_len = 0;

// Forward declarations
static ssize_t read_midi_io(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                            void *buf, uint16_t len, uint16_t offset);
static ssize_t write_midi_io(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                             const void *buf, uint16_t len, uint16_t offset, uint8_t flags);
static void midi_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value);

// Define BLE MIDI GATT Service
BT_GATT_SERVICE_DEFINE(midi_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_MIDI_SERVICE),
    
    // MIDI I/O Characteristic
    BT_GATT_CHARACTERISTIC(BT_UUID_MIDI_IO,
                          BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE_WITHOUT_RESP | BT_GATT_CHRC_NOTIFY,
                          BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                          read_midi_io, write_midi_io, NULL),
    BT_GATT_CCC(midi_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

// Read MIDI I/O characteristic
static ssize_t read_midi_io(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                            void *buf, uint16_t len, uint16_t offset)
{
    // Return last MIDI data sent (optional)
    return bt_gatt_attr_read(conn, attr, buf, len, offset, midi_data_buf, midi_data_len);
}

// Write MIDI I/O characteristic (for receiving MIDI from central)
static ssize_t write_midi_io(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                             const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    // Handle incoming MIDI data (optional - for bidirectional MIDI)
    printk("Received MIDI data: %d bytes\n", len);
    return len;
}

// CCC (Client Characteristic Configuration) changed callback
static void midi_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    notify_enabled = (value == BT_GATT_CCC_NOTIFY);
    printk("MIDI notifications %s\n", notify_enabled ? "enabled" : "disabled");
}

// Connection callbacks
static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        printk("BLE Connection failed (err 0x%02x)\n", err);
    } else {
        current_conn = bt_conn_ref(conn);
        printk("BLE MIDI Connected\n");
        
        // Turn on BLE status LED
        if (ble_status_led_ptr && gpio_is_ready_dt(ble_status_led_ptr)) {
            gpio_pin_set_dt(ble_status_led_ptr, 1);
        }
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    printk("BLE MIDI Disconnected (reason 0x%02x)\n", reason);
    
    if (current_conn) {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }
    
    notify_enabled = false;
    
    // Turn off BLE status LED
    if (ble_status_led_ptr && gpio_is_ready_dt(ble_status_led_ptr)) {
        gpio_pin_set_dt(ble_status_led_ptr, 0);
    }
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

// Advertising parameters (new API - no deprecated options)
static struct bt_le_adv_param adv_param = BT_LE_ADV_PARAM_INIT(
    BT_LE_ADV_OPT_CONN,
    BT_GAP_ADV_FAST_INT_MIN_2,
    BT_GAP_ADV_FAST_INT_MAX_2,
    NULL
);

// Advertising data
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

// Scan response data
static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_MIDI_SERVICE_VAL),
};

// Initialize BLE MIDI
int ble_midi_init(const struct gpio_dt_spec *status_led)
{
    int err;

    printk("Initializing BLE MIDI...\n");
    
    // Store status LED pointer
    ble_status_led_ptr = status_led;

    // Enable Bluetooth
    err = bt_enable(NULL);
    if (err) {
        printk("Bluetooth init failed (err %d)\n", err);
        return err;
    }

    printk("Bluetooth initialized\n");

    // Start advertising (using new API)
    err = bt_le_adv_start(&adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err) {
        printk("Advertising failed to start (err %d)\n", err);
        return err;
    }

    printk("BLE MIDI advertising as '%s'\n", CONFIG_BT_DEVICE_NAME);
    
    return 0;
}

// Send MIDI data
int ble_midi_send(const uint8_t *data, uint8_t len)
{
    if (!data || len == 0 || len > sizeof(midi_data_buf)) {
        return -EINVAL;
    }

    // Store for read operations
    memcpy(midi_data_buf, data, len);
    midi_data_len = len;
    
    // Send notification if connected and enabled
    if (current_conn && notify_enabled) {
        int err = bt_gatt_notify(current_conn, &midi_svc.attrs[1], data, len);
        if (err) {
            printk("MIDI notify failed (err %d)\n", err);
            return err;
        }
    }
    
    return 0;
}

// Check if connected
bool ble_midi_is_connected(void)
{
    return (current_conn != NULL && notify_enabled);
}


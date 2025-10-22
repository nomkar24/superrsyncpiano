#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/sys/printk.h>
#include "ble_service.h"

// Custom Service UUID: 12340000-1234-5678-1234-56789abcdef0
#define BT_UUID_SWITCH_SERVICE_VAL \
    BT_UUID_128_ENCODE(0x12340000, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)

// SW1 Characteristic UUID: 12340001-1234-5678-1234-56789abcdef0
#define BT_UUID_SW1_CHAR_VAL \
    BT_UUID_128_ENCODE(0x12340001, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)

// SW2 Characteristic UUID: 12340002-1234-5678-1234-56789abcdef0
#define BT_UUID_SW2_CHAR_VAL \
    BT_UUID_128_ENCODE(0x12340002, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)

#define BT_UUID_SWITCH_SERVICE  BT_UUID_DECLARE_128(BT_UUID_SWITCH_SERVICE_VAL)
#define BT_UUID_SW1_CHAR        BT_UUID_DECLARE_128(BT_UUID_SW1_CHAR_VAL)
#define BT_UUID_SW2_CHAR        BT_UUID_DECLARE_128(BT_UUID_SW2_CHAR_VAL)

// Switch states
static uint8_t sw1_state = 0;  // 0 = OFF, 1 = ON
static uint8_t sw2_state = 0;  // 0 = OFF, 1 = ON

// Connection tracking
static struct bt_conn *current_conn = NULL;
static bool notify_enabled_sw1 = false;
static bool notify_enabled_sw2 = false;

// Forward declarations
static ssize_t read_sw1(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                        void *buf, uint16_t len, uint16_t offset);
static ssize_t read_sw2(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                        void *buf, uint16_t len, uint16_t offset);
static void sw1_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value);
static void sw2_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value);

// Define GATT Service
BT_GATT_SERVICE_DEFINE(switch_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_SWITCH_SERVICE),
    
    // SW1 Characteristic
    BT_GATT_CHARACTERISTIC(BT_UUID_SW1_CHAR,
                          BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                          BT_GATT_PERM_READ,
                          read_sw1, NULL, NULL),
    BT_GATT_CCC(sw1_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    
    // SW2 Characteristic
    BT_GATT_CHARACTERISTIC(BT_UUID_SW2_CHAR,
                          BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                          BT_GATT_PERM_READ,
                          read_sw2, NULL, NULL),
    BT_GATT_CCC(sw2_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

// Read SW1 characteristic
static ssize_t read_sw1(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                        void *buf, uint16_t len, uint16_t offset)
{
    const uint8_t *value = &sw1_state;
    return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(sw1_state));
}

// Read SW2 characteristic
static ssize_t read_sw2(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                        void *buf, uint16_t len, uint16_t offset)
{
    const uint8_t *value = &sw2_state;
    return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(sw2_state));
}

// SW1 CCC (Client Characteristic Configuration) changed callback
static void sw1_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    notify_enabled_sw1 = (value == BT_GATT_CCC_NOTIFY);
    printk("SW1 notifications %s\n", notify_enabled_sw1 ? "enabled" : "disabled");
}

// SW2 CCC changed callback
static void sw2_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    notify_enabled_sw2 = (value == BT_GATT_CCC_NOTIFY);
    printk("SW2 notifications %s\n", notify_enabled_sw2 ? "enabled" : "disabled");
}

// Connection callbacks
static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        printk("Connection failed (err 0x%02x)\n", err);
    } else {
        current_conn = bt_conn_ref(conn);
        printk("BLE Connected\n");
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    printk("BLE Disconnected (reason 0x%02x)\n", reason);
    
    if (current_conn) {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }
    
    notify_enabled_sw1 = false;
    notify_enabled_sw2 = false;
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

// Advertising data
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

// Scan response data
static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_SWITCH_SERVICE_VAL),
};

// Initialize BLE
int ble_init(void)
{
    int err;

    printk("Initializing BLE...\n");

    // Enable Bluetooth
    err = bt_enable(NULL);
    if (err) {
        printk("Bluetooth init failed (err %d)\n", err);
        return err;
    }

    printk("Bluetooth initialized\n");

    // Start advertising
    err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err) {
        printk("Advertising failed to start (err %d)\n", err);
        return err;
    }

    printk("Advertising started as '%s'\n", CONFIG_BT_DEVICE_NAME);
    
    return 0;
}

// Update SW1 state and notify
void ble_update_sw1(uint8_t state)
{
    sw1_state = state;
    
    if (current_conn && notify_enabled_sw1) {
        int err = bt_gatt_notify(current_conn, &switch_svc.attrs[1], 
                                 &sw1_state, sizeof(sw1_state));
        if (err) {
            printk("SW1 notify failed (err %d)\n", err);
        }
    }
}

// Update SW2 state and notify
void ble_update_sw2(uint8_t state)
{
    sw2_state = state;
    
    if (current_conn && notify_enabled_sw2) {
        int err = bt_gatt_notify(current_conn, &switch_svc.attrs[4], 
                                 &sw2_state, sizeof(sw2_state));
        if (err) {
            printk("SW2 notify failed (err %d)\n", err);
        }
    }
}

// Check if connected
bool ble_is_connected(void)
{
    return (current_conn != NULL);
}


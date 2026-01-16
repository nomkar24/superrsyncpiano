#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/logging/log.h>
#include "ble_config_service.h"

LOG_MODULE_REGISTER(ble_conf, LOG_LEVEL_INF);

// Global Settings (Defaults)
uint8_t g_sensitivity = 50;
uint8_t g_led_theme = 0;
int8_t  g_transpose = 0;

// ========== UUID DEFINITIONS ==========
// Base UUID: 12345678-1234-5678-1234-56789abc0000
#define BT_UUID_SUPERR_VAL \
    BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abc0000)

// Characteristics
#define BT_UUID_SENSITIVITY_VAL \
    BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abc0001)

#define BT_UUID_THEME_VAL \
    BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abc0002)

#define BT_UUID_TRANSPOSE_VAL \
    BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abc0003)

#define BT_UUID_SUPERR_SERVICE  BT_UUID_DECLARE_128(BT_UUID_SUPERR_VAL)
#define BT_UUID_SENSITIVITY     BT_UUID_DECLARE_128(BT_UUID_SENSITIVITY_VAL)
#define BT_UUID_THEME           BT_UUID_DECLARE_128(BT_UUID_THEME_VAL)
#define BT_UUID_TRANSPOSE       BT_UUID_DECLARE_128(BT_UUID_TRANSPOSE_VAL)

// ========== CALLBACKS ==========

// 1. Sensitivity Write Callback (0-100)
static ssize_t write_sensitivity(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                 const void *buf, uint16_t len, uint16_t offset,
                                 uint8_t flags)
{
    if (len != 1) return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    
    uint8_t val = *((uint8_t *)buf);
    if (val > 100) val = 100; // Clamp
    
    g_sensitivity = val;
    LOG_INF("Sensitivity updated to: %d", val);
    
    return len;
}

// 2. Theme Write Callback (0-2)
static ssize_t write_theme(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                           const void *buf, uint16_t len, uint16_t offset,
                           uint8_t flags)
{
    if (len != 1) return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    
    uint8_t val = *((uint8_t *)buf);
    // 0=Aurora, 1=Fire, 2=Matrix
    
    g_led_theme = val;
    LOG_INF("LED Theme updated to: %d", val);
    
    return len;
}

// 3. Transpose Write Callback (-12 to +12)
static ssize_t write_transpose(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                               const void *buf, uint16_t len, uint16_t offset,
                               uint8_t flags)
{
    if (len != 1) return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    
    int8_t val = *((int8_t *)buf);
    
    if (val < -12) val = -12;
    if (val > 12) val = 12;
    
    g_transpose = val;
    LOG_INF("Transpose updated to: %d", val);
    
    return len;
}

// ========== SERVICE DEFINITION ==========
BT_GATT_SERVICE_DEFINE(superr_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_SUPERR_SERVICE),
    
    // Characteristic: Sensitivity (Read/Write)
    BT_GATT_CHARACTERISTIC(BT_UUID_SENSITIVITY,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                           NULL, write_sensitivity, &g_sensitivity),
                           
    // Characteristic: LED Theme (Read/Write)
    BT_GATT_CHARACTERISTIC(BT_UUID_THEME,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                           NULL, write_theme, &g_led_theme),
                           
    // Characteristic: Transpose (Read/Write)
    BT_GATT_CHARACTERISTIC(BT_UUID_TRANSPOSE,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                           NULL, write_transpose, &g_transpose)
);

int ble_config_init(void)
{
    // Zephyr's BT_GATT_SERVICE_DEFINE automatically registers it at boot time.
    // We just return success.
    LOG_INF("Superr Configuration Service Initialized");
    return 0;
}

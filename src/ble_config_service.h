#ifndef BLE_CONFIG_SERVICE_H
#define BLE_CONFIG_SERVICE_H

#include <zephyr/types.h>

// ========== GLOBAL SETTINGS ==========
// These are modified by the Phone App via Bluetooth
// and read by the Main Loop to change behavior.

extern uint8_t g_sensitivity; // 0 (Hard) to 100 (Sensitive). Default: 50
extern uint8_t g_led_theme;   // 0=Aurora, 1=Fire, 2=Matrix. Default: 0
extern int8_t  g_transpose;   // -12 to +12 semitones. Default: 0

// ========== API ==========
/** @brief Initialize the Configuration Service */
int ble_config_init(void);

#endif

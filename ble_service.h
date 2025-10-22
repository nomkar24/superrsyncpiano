#ifndef BLE_SERVICE_H
#define BLE_SERVICE_H

#include <zephyr/types.h>

/**
 * @brief Initialize BLE stack and start advertising
 * 
 * @return 0 on success, negative error code on failure
 */
int ble_init(void);

/**
 * @brief Update SW1 state and notify connected clients
 * 
 * @param state 0 = OFF, 1 = ON
 */
void ble_update_sw1(uint8_t state);

/**
 * @brief Update SW2 state and notify connected clients
 * 
 * @param state 0 = OFF, 1 = ON
 */
void ble_update_sw2(uint8_t state);

/**
 * @brief Check if any BLE client is connected
 * 
 * @return true if connected, false otherwise
 */
bool ble_is_connected(void);

#endif // BLE_SERVICE_H


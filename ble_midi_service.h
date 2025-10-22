#ifndef BLE_MIDI_SERVICE_H
#define BLE_MIDI_SERVICE_H

#include <zephyr/types.h>
#include <zephyr/drivers/gpio.h>

/**
 * @brief Initialize BLE MIDI service and start advertising
 * 
 * @param status_led Optional BLE status LED (NULL if not used)
 * @return 0 on success, negative error code on failure
 */
int ble_midi_init(const struct gpio_dt_spec *status_led);

/**
 * @brief Send MIDI data over BLE
 * 
 * @param data BLE MIDI packet data
 * @param len Length of data
 * @return 0 on success, negative on error
 */
int ble_midi_send(const uint8_t *data, uint8_t len);

/**
 * @brief Check if a BLE MIDI client is connected
 * 
 * @return true if connected, false otherwise
 */
bool ble_midi_is_connected(void);

#endif // BLE_MIDI_SERVICE_H


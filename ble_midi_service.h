#ifndef BLE_MIDI_SERVICE_H
#define BLE_MIDI_SERVICE_H

#include <zephyr/types.h>

/**
 * @brief Initialize BLE MIDI service and start advertising
 * 
 * @return 0 on success, negative error code on failure
 */
int ble_midi_init(void);

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


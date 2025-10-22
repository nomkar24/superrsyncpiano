#ifndef MIDI_BLE_H
#define MIDI_BLE_H

#include <zephyr/audio/midi.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @brief Convert Zephyr UMP MIDI1 message to BLE MIDI format
 * 
 * @param ump Zephyr Universal MIDI Packet
 * @param buf Output buffer for BLE MIDI packet
 * @param buf_len Size of output buffer
 * @return Length of BLE MIDI packet, or negative on error
 */
int midi_ble_encode(const struct midi_ump *ump, uint8_t *buf, size_t buf_len);

/**
 * @brief Create BLE MIDI packet for Note On
 * 
 * @param note MIDI note number (0-127, 60=Middle C)
 * @param velocity Note velocity (0-127)
 * @param channel MIDI channel (0-15)
 * @param buf Output buffer
 * @param buf_len Buffer size
 * @return Length of packet
 */
int midi_ble_note_on(uint8_t note, uint8_t velocity, uint8_t channel, 
                     uint8_t *buf, size_t buf_len);

/**
 * @brief Create BLE MIDI packet for Note Off
 * 
 * @param note MIDI note number (0-127)
 * @param velocity Release velocity (0-127)
 * @param channel MIDI channel (0-15)
 * @param buf Output buffer
 * @param buf_len Buffer size
 * @return Length of packet
 */
int midi_ble_note_off(uint8_t note, uint8_t velocity, uint8_t channel,
                      uint8_t *buf, size_t buf_len);

/**
 * @brief Create BLE MIDI packet for Control Change
 * 
 * @param cc_num Control Change number (0-127)
 * @param value CC value (0-127)
 * @param channel MIDI channel (0-15)
 * @param buf Output buffer
 * @param buf_len Buffer size
 * @return Length of packet
 */
int midi_ble_control_change(uint8_t cc_num, uint8_t value, uint8_t channel,
                             uint8_t *buf, size_t buf_len);

#endif // MIDI_BLE_H


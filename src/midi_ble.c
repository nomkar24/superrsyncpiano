#include "midi_ble.h"
#include <zephyr/kernel.h>
#include <string.h>

// BLE MIDI timestamp helpers
// For simplicity, we use a running timestamp based on system uptime
static uint8_t get_timestamp_high(void)
{
    // BLE MIDI timestamp high 6 bits (bit 7 = 1, bits 6-0 = timestamp bits 12-6)
    uint32_t ms = k_uptime_get_32();
    return 0x80 | ((ms >> 6) & 0x3F);
}

static uint8_t get_timestamp_low(void)
{
    // BLE MIDI timestamp low 7 bits (bit 7 = 1, bits 6-0 = timestamp bits 5-0)
    uint32_t ms = k_uptime_get_32();
    return 0x80 | (ms & 0x3F);
}

int midi_ble_encode(const struct midi_ump *ump, uint8_t *buf, size_t buf_len)
{
    if (!ump || !buf || buf_len < 5) {
        return -EINVAL;
    }

    uint8_t mt = UMP_MT(*ump);
    
    // We only handle MIDI1 channel voice messages for now
    if (mt != UMP_MT_MIDI1_CHANNEL_VOICE) {
        return -ENOTSUP;
    }

    // BLE MIDI packet format:
    // [Header] [Timestamp] [Status] [Data1] [Data2...]
    buf[0] = get_timestamp_high();
    buf[1] = get_timestamp_low();
    buf[2] = UMP_MIDI_STATUS(*ump);      // Status byte
    buf[3] = UMP_MIDI1_P1(*ump);          // First parameter
    buf[4] = UMP_MIDI1_P2(*ump);          // Second parameter

    return 5;
}

int midi_ble_note_on(uint8_t note, uint8_t velocity, uint8_t channel,
                     uint8_t *buf, size_t buf_len)
{
    if (!buf || buf_len < 5) {
        return -EINVAL;
    }

    // Create Zephyr MIDI1 UMP for Note On
    struct midi_ump ump = UMP_MIDI1_CHANNEL_VOICE(
        0,                      // Group 0
        UMP_MIDI_NOTE_ON,       // Note On command
        channel & 0x0F,         // Channel (0-15)
        note & 0x7F,            // Note number (0-127)
        velocity & 0x7F         // Velocity (0-127)
    );

    return midi_ble_encode(&ump, buf, buf_len);
}

int midi_ble_note_off(uint8_t note, uint8_t velocity, uint8_t channel,
                      uint8_t *buf, size_t buf_len)
{
    if (!buf || buf_len < 5) {
        return -EINVAL;
    }

    // Create Zephyr MIDI1 UMP for Note Off
    struct midi_ump ump = UMP_MIDI1_CHANNEL_VOICE(
        0,                      // Group 0
        UMP_MIDI_NOTE_OFF,      // Note Off command
        channel & 0x0F,         // Channel (0-15)
        note & 0x7F,            // Note number (0-127)
        velocity & 0x7F         // Release velocity (0-127)
    );

    return midi_ble_encode(&ump, buf, buf_len);
}

int midi_ble_control_change(uint8_t cc_num, uint8_t value, uint8_t channel,
                             uint8_t *buf, size_t buf_len)
{
    if (!buf || buf_len < 5) {
        return -EINVAL;
    }

    // Create Zephyr MIDI1 UMP for Control Change
    struct midi_ump ump = UMP_MIDI1_CHANNEL_VOICE(
        0,                          // Group 0
        UMP_MIDI_CONTROL_CHANGE,    // Control Change command
        channel & 0x0F,             // Channel (0-15)
        cc_num & 0x7F,              // CC number (0-127)
        value & 0x7F                // CC value (0-127)
    );

    return midi_ble_encode(&ump, buf, buf_len);
}


# Superr MIDI Keyboard - BLE Interface Documentation

This document provides the Bluetooth Low Energy (BLE) specifications for connecting the custom mobile app to the Superr MIDI Keyboard.

## 1. Connection Details

- **Device Name:** `Superr_MIDI`
- **Advertising:** The device advertises the **MIDI Service** (UUID below).
- **Connection Strategy:** Scan for devices with the name "Superr_MIDI" or the MIDI Service UUID.

## 2. Services & Characteristics

### A. MIDI Service (Standard BLE MIDI)
Used for sending/receiving MIDI notes.

- **Service UUID:** `03B80E5A-EDE8-4B33-A751-6CE34EC4C700`
- **MIDI I/O Characteristic:** `7772E5DB-3868-4112-A1A9-F2669D106BF3`
  - **Properties:** Read, Write Without Response, Notify
  - **Function:** Standard BLE MIDI packet exchange.

---

### B. Superr Configuration Service (Custom)
Used to configure device settings (Sensitivity, LED Theme, Transpose). This service is available via GATT discovery after connection.

- **Service UUID:** `12345678-1234-5678-1234-56789abc0000`

#### Characteristics

| Name | UUID | Type | Range / Values | Description |
|------|------|------|----------------|-------------|
| **Sensitivity** | `...0001` * | `uint8_t` (1 byte) | `0` - `100` | Keyboard velocity sensitivity. <br>0 = Off, 50 = Normal, 100 = High. |
| **LED Theme** | `...0002` * | `uint8_t` (1 byte) | `0`, `1`, `2` | Visual effect pattern. <br>`0`: Aurora (Blue/Purple) <br>`1`: Fire (Red/Orange) <br>`2`: Matrix (Green) |
| **Transpose** | `...0003` * | `int8_t` (1 byte) | `-12` to `+12` | Pitch shift in semitones. <br>Signed integer (e.g., 0xFF = -1). |

*\* calculate full UUID by replacing the last 2 bytes of the Base UUID: `12345678-1234-5678-1234-56789abcXXXX`*

**Full UUIDs:**
- **Sensitivity:** `12345678-1234-5678-1234-56789abc0001`
- **LED Theme:** `12345678-1234-5678-1234-56789abc0002`
- **Transpose:** `12345678-1234-5678-1234-56789abc0003`

### Properties for Config Characteristics
All configuration characteristics support:
- **Read:** To get the current value.
- **Write:** To set a new value (response is sent).

## 3. Interaction Flow

1. **Scan** for `Superr_MIDI`.
2. **Connect** to the device.
3. **Discover Services**.
4. **Read** current settings from the **Configuration Service** to update the App UI.
5. **Write** changes to the **Configuration Service** when the user modifies settings in the App.
   - Example: User slides "Sensitivity" to 80 -> Write `0x50` (80) to Sensitivity Characteristic.
6. **Subscribe** to **MIDI I/O** notifications if the app needs to receive key presses (optional).

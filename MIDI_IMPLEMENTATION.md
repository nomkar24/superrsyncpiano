# BLE MIDI Implementation Guide

## ✅ **What Was Implemented**

### **Using Zephyr's Official MIDI Library!**
Instead of writing custom MIDI code, we're using:
- ✅ **`zephyr/audio/midi.h`** - Zephyr's official MIDI library
- ✅ **UMP (Universal MIDI Packet)** - Standard MIDI1/MIDI2 format
- ✅ **BLE MIDI Standard** - Apple/MIDI.org specification
- ✅ **Tested macros** - `UMP_MIDI1_CHANNEL_VOICE`, `UMP_MIDI_NOTE_ON`, etc.

---

## 📁 **Files Created**

1. **`src/midi_ble.h`** - BLE MIDI encoding API
2. **`src/midi_ble.c`** - Converts Zephyr UMP → BLE MIDI packets
3. **`src/ble_midi_service.h`** - BLE MIDI GATT service API
4. **`src/ble_midi_service.c`** - Standard BLE MIDI service (Apple compatible)
5. **`src/main.c`** - Updated to send MIDI notes

---

## 🎹 **How It Works**

```
┌──────────────┐
│ Press SW1    │
└──────┬───────┘
       ↓
┌─────────────────────────────────────┐
│ Zephyr MIDI Library                 │
│ UMP_MIDI1_CHANNEL_VOICE(...)        │
│ → Creates MIDI1 Note On message     │
└──────┬──────────────────────────────┘
       ↓
┌─────────────────────────────────────┐
│ midi_ble.c                          │
│ midi_ble_note_on()                  │
│ → Converts UMP to BLE MIDI format   │
│ → [Header][Timestamp][MIDI Data]   │
└──────┬──────────────────────────────┘
       ↓
┌─────────────────────────────────────┐
│ BLE MIDI Service                    │
│ ble_midi_send()                     │
│ → Sends BLE notification            │
└──────┬──────────────────────────────┘
       ↓
┌─────────────────────────────────────┐
│ iPhone/iPad/Mac/Android             │
│ GarageBand plays note C4            │
└─────────────────────────────────────┘
```

---

## 🎵 **MIDI Note Mapping**

| Switch | MIDI Note | Note Name | Use Case |
|--------|-----------|-----------|----------|
| SW1 | 60 | C4 (Middle C) | Kick drum, Piano key |
| SW2 | 62 | D4 | Snare, Next key |

**To change notes**, edit in `main.c`:
```c
#define MIDI_NOTE_SW1  60  // Change to any MIDI note (0-127)
#define MIDI_NOTE_SW2  62  // Change to any MIDI note (0-127)
```

**Common MIDI Notes:**
- 36 = C1 (Kick drum)
- 38 = D1 (Snare)
- 42 = F#1 (Closed Hi-Hat)
- 60 = C4 (Middle C)
- 64 = E4
- 67 = G4

---

## 📱 **BLE MIDI Service Details**

### **Service UUID (Standard):**
```
03B80E5A-EDE8-4B33-A751-6CE34EC4C700
```

### **Characteristic UUID (Standard):**
```
7772E5DB-3868-4112-A1A9-F2669D106BF3
```

### **Properties:**
- READ
- WRITE WITHOUT RESPONSE  
- NOTIFY

### **Device Name:**
```
Switch_Monitor
```

---

## 🔨 **Build & Flash**

```bash
cd c:\Users\omkar\superr
west build -b nrf5340dk/nrf5340/cpuapp --pristine
west flash
```

---

## 📊 **Expected Serial Output**

```
*** Booting nRF Connect SDK ***
Dual Switch LED Demo Started
Initializing BLE MIDI...
Bluetooth initialized
BLE MIDI advertising as 'Switch_Monitor'
Ready! Press switches to control LED

[When device connects]
BLE MIDI Connected
MIDI notifications enabled

[When you press SW1]
SW1: ON - MIDI Note 60 ON

[When you release SW1]
SW1: OFF - MIDI Note 60 OFF

[When you press SW2]
SW2: ON - MIDI Note 62 ON

[When you release SW2]
SW2: OFF - MIDI Note 62 OFF
```

---

## 📱 **Testing with iOS/iPadOS (Easiest)**

### **1. GarageBand (Free, Built-in)**

#### **Setup:**
1. Open **GarageBand** on iPhone/iPad
2. Tap **+** → **Keyboard** (or any instrument)
3. Tap **🎚️** (Settings icon)
4. Enable **Bluetooth MIDI Devices**
5. Your nRF5340 will appear as **"Switch_Monitor"**
6. Tap to connect

#### **Test:**
- Press **SW1** → Hear note **C4** play
- Press **SW2** → Hear note **D4** play
- Works with ANY GarageBand instrument!

---

### **2. MIDI Wrench (MIDI Monitor)**

#### **Download:**
[MIDI Wrench on App Store](https://apps.apple.com/app/midi-wrench/id588095376)

#### **Test:**
1. Open MIDI Wrench
2. Go to **Input Monitor**
3. Connect to **Switch_Monitor**
4. Press switches → See MIDI messages in real-time:
   ```
   90 3C 64  (Note On, Channel 1, Note 60, Velocity 100)
   80 3C 00  (Note Off, Channel 1, Note 60)
   ```

---

## 🎹 **Testing with macOS**

### **1. Audio MIDI Setup**

```bash
# Open MIDI Studio
open "/Applications/Utilities/Audio MIDI Setup.app"
```

1. Go to **Window** → **Show MIDI Studio**
2. Your nRF5340 appears automatically as **Switch_Monitor**
3. Double-click to test connection

### **2. GarageBand (same as iOS)**

### **3. Ableton Live / Logic Pro**
1. Open DAW
2. Go to **MIDI Settings**
3. **Switch_Monitor** appears in MIDI devices
4. Enable it
5. Assign to a track → Press switches to trigger sounds

---

## 📱 **Testing with Android**

### **1. MIDI BLE Connect (Required)**

#### **Download:**
[MIDI BLE Connect](https://play.google.com/store/apps/details?id=com.mobileer.example.midibtlepairing)

#### **Setup:**
1. Open **MIDI BLE Connect**
2. Scan for **Switch_Monitor**
3. Tap **Connect**
4. Leave app running in background

### **2. FL Studio Mobile**
1. Open **FL Studio Mobile**
2. Go to **Settings** → **MIDI**
3. **Switch_Monitor** should appear
4. Enable it
5. Assign to instrument
6. Press switches → Trigger sounds

### **3. MIDI Monitor App**
Any Android MIDI monitor app will show the messages.

---

## 🖥️ **Testing with Windows**

### **1. Enable BLE MIDI Support**

Windows 10/11 has built-in BLE MIDI support, but you need to pair first:

```
Settings → Bluetooth & Devices → Add Device
→ Select "Switch_Monitor"
```

### **2. Use with DAW**
- **FL Studio**
- **Ableton Live**  
- **Reaper**

The device will appear in MIDI settings.

### **3. MIDI Monitor**
Download **MIDI-OX** or **MIDIberry** to monitor MIDI messages.

---

## 🎛️ **Customization Examples**

### **Example 1: Drum Pads**

```c
// In main.c
#define MIDI_NOTE_SW1  36  // Kick drum (C1)
#define MIDI_NOTE_SW2  38  // Snare (D1)
```

Use with GarageBand **Drums** instrument.

---

### **Example 2: Control Changes Instead of Notes**

Modify `main.c` to use `midi_ble_control_change()`:

```c
if (sw1_state == 0) {
    // CC #20, Value 127 (ON)
    len = midi_ble_control_change(20, 127, MIDI_CHANNEL,
                                   midi_packet, sizeof(midi_packet));
} else {
    // CC #20, Value 0 (OFF)
    len = midi_ble_control_change(20, 0, MIDI_CHANNEL,
                                   midi_packet, sizeof(midi_packet));
}
```

Use for:
- Effect toggles
- Filter control
- Volume faders

---

### **Example 3: Transport Controls**

```c
// Play/Stop button
if (sw1_state == 0) {
    len = midi_ble_control_change(85, 127, MIDI_CHANNEL, ...);  // Play
}

// Record button
if (sw2_state == 1) {
    len = midi_ble_control_change(86, 127, MIDI_CHANNEL, ...);  // Record
}
```

---

## 🔍 **Troubleshooting**

### **Device not appearing:**
- ✅ Check serial: "BLE MIDI advertising"
- ✅ Enable Bluetooth on phone
- ✅ On iOS: Go to **Settings** → **Bluetooth** (should see Switch_Monitor)
- ✅ Restart board

### **No sound in GarageBand:**
- ✅ Make sure you're on an **Instrument track** (not audio)
- ✅ Enable BLE MIDI in GarageBand settings
- ✅ Tap **Switch_Monitor** to connect
- ✅ Turn up volume

### **MIDI messages not showing:**
- ✅ Check serial output when pressing switches
- ✅ Verify connection: "BLE MIDI Connected"
- ✅ Enable notifications: "MIDI notifications enabled"
- ✅ Try disconnecting and reconnecting

---

## 🎯 **Advantages of Using Zephyr MIDI Library**

✅ **Standards-compliant** - Uses MIDI.org specifications  
✅ **Future-proof** - Supports MIDI 2.0 (UMP)  
✅ **Well-tested** - Part of Zephyr RTOS  
✅ **Easy to extend** - Add more MIDI message types  
✅ **Cross-platform** - Works on all BLE MIDI compatible devices  
✅ **No dependencies** - Built into Zephyr  

---

## 📚 **MIDI Messages Supported**

Currently implemented:
- ✅ Note On
- ✅ Note Off  
- ✅ Control Change

**Easy to add** (using Zephyr macros):
- Program Change: `UMP_MIDI_PROGRAM_CHANGE`
- Pitch Bend: `UMP_MIDI_PITCH_BEND`
- Aftertouch: `UMP_MIDI_AFTERTOUCH`
- System messages: `UMP_SYS_RT_COMMON`

---

## 🚀 **Next Steps**

1. ✅ **Test with GarageBand** (easiest)
2. ✅ **Try different MIDI notes** (edit main.c)
3. ✅ **Add more switches** (expand to 4, 8, 16 keys)
4. ✅ **Add velocity sensitivity** (if using analog sensors)
5. ✅ **Add CC controls** (for filters, effects)
6. ✅ **Build a custom MIDI controller** 🎹

---

**You now have a professional BLE MIDI controller using Zephyr's official MIDI library!** 🎉


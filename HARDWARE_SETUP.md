# Hardware Setup Guide - Superr MIDI Controller

## ✅ **Complete Implementation**

### **What's New:**
1. ✅ **2x1 Matrix Keyboard** - P0.25 (column) drives P0.24 & P0.26 (rows)
2. ✅ **3 LEDs** - BLE Status + 2 Switch indicators
3. ✅ **Device Name** - "Superr_MIDI"
4. ✅ **MIDI Notes** - C4 (60) and C#4 (61) - First two notes of octave

---

## 🔌 **Pin Configuration**

### **Matrix Keyboard (2x1):**
```
P0.25 (Column 0) → OUTPUT (drives HIGH)
P0.24 (Row 0)    → INPUT with PULL_DOWN (Switch 0)
P0.26 (Row 1)    → INPUT with PULL_DOWN (Switch 1)
```

### **LEDs:**
```
P0.28 → BLE Status LED (ON when connected)
P0.29 → Switch 0 LED (ON when SW0 pressed)
P0.30 → Switch 1 LED (ON when SW1 pressed)
```

---

## 🔧 **Hardware Connections**

### **Components Needed:**
- 2x Tactile push buttons (or momentary switches)
- 3x LEDs (any color, recommend: Blue for BLE, Green/Red for switches)
- 3x 220Ω-330Ω resistors (for LEDs)
- Jumper wires
- Breadboard

---

## 📐 **Connection Diagram**

```
┌─────────────────────────────────────────────────────────┐
│                  nRF5340 DK                             │
├─────────────────────────────────────────────────────────┤
│                                                         │
│  Matrix Column (Drive):                                │
│  P0.25 ●────────────────┐                              │
│                          │                              │
│  Matrix Rows (Sense):    │                              │
│  P0.24 ●──[SW0]──────────┤  (Switch 0 connects to col) │
│  P0.26 ●──[SW1]──────────┘  (Switch 1 connects to col) │
│                                                         │
│  LEDs:                                                  │
│  P0.28 ●──[220Ω]──[BLE LED]──GND  (Blue - Status)      │
│  P0.29 ●──[220Ω]──[SW0 LED]──GND  (Green - Switch 0)   │
│  P0.30 ●──[220Ω]──[SW1 LED]──GND  (Red - Switch 1)     │
│                                                         │
│  GND   ●                                                │
└─────────────────────────────────────────────────────────┘
```

---

## 🔨 **Step-by-Step Wiring**

### **1. Matrix Switches:**

**Switch 0 (SW0):**
```
Pin 1 → P0.25 (Column)
Pin 2 → P0.24 (Row 0)
```

**Switch 1 (SW1):**
```
Pin 1 → P0.25 (Column)
Pin 2 → P0.26 (Row 1)
```

### **2. BLE Status LED (Blue):**
```
P0.28 → [220Ω Resistor] → LED Anode (+)
LED Cathode (-) → GND
```

### **3. Switch 0 LED (Green):**
```
P0.29 → [220Ω Resistor] → LED Anode (+)
LED Cathode (-) → GND
```

### **4. Switch 1 LED (Red):**
```
P0.30 → [220Ω Resistor] → LED Anode (+)
LED Cathode (-) → GND
```

---

## 🎹 **MIDI Note Mapping**

| Switch | Row Pin | MIDI Note | Note Name | Use Case |
|--------|---------|-----------|-----------|----------|
| SW0 | P0.24 | 60 | C4 (Middle C) | First note of octave |
| SW1 | P0.26 | 61 | C#4 / Db4 | Second note |

**Perfect for apps!** Starting from C4 makes it easy to use with any MIDI app or DAW.

---

## 📱 **Visual Behavior**

### **Power On:**
```
BLE LED: OFF (not connected)
SW0 LED: OFF
SW1 LED: OFF
Serial: "Superr MIDI Controller v1.0"
```

### **BLE Connected:**
```
BLE LED: ON (blue glow) ✨
SW0 LED: OFF
SW1 LED: OFF
Serial: "BLE MIDI Connected"
```

### **Press SW0:**
```
BLE LED: ON
SW0 LED: ON (green glow) ✅
SW1 LED: OFF
Serial: "SW0: PRESSED → MIDI Note 60 (C4) ON"
```

### **Press Both:**
```
BLE LED: ON
SW0 LED: ON ✅
SW1 LED: ON ✅
Serial: Both switch press messages
MIDI: C4 + C#4 playing together (chord!)
```

---

## 🎯 **Matrix Scanning Logic**

```
P0.25 (Column) drives HIGH
     ↓
   [SW0] → P0.24 reads HIGH when pressed
     ↓
   [SW1] → P0.26 reads HIGH when pressed
```

**How it works:**
1. Column P0.25 continuously outputs HIGH
2. When SW0 is pressed, it connects P0.25 → P0.24
3. P0.24 reads HIGH (switch pressed)
4. Same for SW1 with P0.26

**Advantages:**
- ✅ Scalable to larger matrices (2x2, 4x4, etc.)
- ✅ Low pin count (1 column + N rows = N switches)
- ✅ Standard keyboard matrix technique
- ✅ Can detect multiple simultaneous presses

---

## 🔍 **Testing Checklist**

### **Power-Up Test:**
- [ ] All LEDs are OFF initially
- [ ] Serial shows "Superr MIDI Controller v1.0"
- [ ] "BLE MIDI advertising as 'Superr_MIDI'" appears

### **BLE Connection Test:**
- [ ] Open GarageBand or BLE MIDI app
- [ ] Device appears as "Superr_MIDI"
- [ ] Connect to device
- [ ] **BLE LED turns ON** ← Important!
- [ ] Serial shows "BLE MIDI Connected"

### **Switch Test (Without BLE):**
- [ ] Press SW0 → SW0 LED turns ON
- [ ] Release SW0 → SW0 LED turns OFF
- [ ] Press SW1 → SW1 LED turns ON
- [ ] Release SW1 → SW1 LED turns OFF
- [ ] Serial shows press/release messages

### **MIDI Test (With BLE Connected):**
- [ ] Press SW0 → Hear C4 note in GarageBand
- [ ] Release SW0 → Note stops
- [ ] Press SW1 → Hear C#4 note
- [ ] Press both → Hear chord (both notes together)

### **LED Indicator Test:**
- [ ] BLE LED glows when connected (stays ON)
- [ ] SW0 LED glows only when SW0 is pressed
- [ ] SW1 LED glows only when SW1 is pressed
- [ ] Disconnect BLE → BLE LED turns OFF

---

## ⚡ **Serial Output Examples**

### **Startup:**
```
╔════════════════════════════════════╗
║   Superr MIDI Controller v1.0     ║
║   2x1 Matrix BLE MIDI Device      ║
╚════════════════════════════════════╝

✓ BLE Status LED configured (P0.28)
✓ Switch 0 LED configured (P0.29)
✓ Switch 1 LED configured (P0.30)
✓ Matrix Column 0 configured (P0.25 - OUTPUT HIGH)
✓ Matrix Row 0 configured (P0.24 - INPUT PULL_DOWN)
✓ Matrix Row 1 configured (P0.26 - INPUT PULL_DOWN)

Initializing BLE MIDI...
Bluetooth initialized
BLE MIDI advertising as 'Superr_MIDI'

╔════════════════════════════════════╗
║  MIDI Mapping:                    ║
║  SW0 (Row0) → C4  (Note 60)       ║
║  SW1 (Row1) → C#4 (Note 61)       ║
╚════════════════════════════════════╝

Ready! Press switches to send MIDI notes.
BLE Status LED will glow when connected.
```

### **During Use:**
```
BLE MIDI Connected
MIDI notifications enabled
SW0: PRESSED → MIDI Note 60 (C4) ON
SW0: RELEASED → MIDI Note 60 (C4) OFF
SW1: PRESSED → MIDI Note 61 (C#4) ON
SW1: RELEASED → MIDI Note 61 (C#4) OFF
```

---

## 🎵 **Using with GarageBand (iOS/iPad)**

1. **Open GarageBand**
2. **Create Keyboard/Piano Track**
3. **Settings** → **Advanced** → **Bluetooth MIDI Devices**
4. **Connect to "Superr_MIDI"**
5. **BLE LED turns ON** ✅
6. **Press switches** → Hear C4 and C#4 notes!

**Tips:**
- Works great with Piano, Synth, or Drums
- Try "Alchemy" synth for cool sounds
- Use "Smart Piano" for auto-accompaniment
- Record multi-track by pressing both switches

---

## 🚀 **Expansion Ideas**

### **Easy Upgrades:**
1. **4x1 Matrix** (4 switches, 4 notes - C, C#, D, D#)
   - Add P0.27, P0.31 as Row 2, Row 3
   - Completes the first half of octave

2. **2x2 Matrix** (4 switches with 3 pins!)
   - Add P0.31 as Column 1
   - 2 columns × 2 rows = 4 switches
   - Still only 1 LED per switch

3. **8-Key Octave** (Full octave C-C)
   - 2 columns × 4 rows = 8 switches
   - Notes 60-67 (C4 to B4)

4. **Velocity Sensitivity**
   - Add analog input for pressure sensing
   - Variable MIDI velocity based on press force

5. **Battery Power**
   - Add CR2032 battery holder
   - Enable low-power mode
   - Hours of wireless MIDI!

---

## 🎨 **Recommended LED Colors**

- **BLE Status**: Blue (classic Bluetooth color)
- **SW0**: Green (GO! Press me!)
- **SW1**: Red or Yellow
- **All White**: Clean, modern look
- **RGB LEDs**: Ultimate customization!

---

## ✅ **Build & Flash**

```bash
cd c:\Users\omkar\superr
west build -b nrf5340dk/nrf5340/cpuapp --pristine
west flash
```

---

**You now have a professional 2-key MIDI controller with visual feedback!** 🎹✨


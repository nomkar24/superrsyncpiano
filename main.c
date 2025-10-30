#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include "ble_midi_service.h"
#include "midi_ble.h"
#include "ws2812_driver.h"  // Simple Zephyr WS2812B driver
#include "color_utils.h"    // RGB/HSV utilities

// MIDI note assignments - First octave starting from C4
#define MIDI_NOTE_SW0  60  // C4 (Middle C) - First note of octave
#define MIDI_NOTE_SW1  83  // B5 - Second note
#define MIDI_CHANNEL   0   // MIDI Channel 1 (0-indexed)

// WS2812B LED mapping
#define WS2812_LED_SW0  0  // LED index 0 for switch 0 (blue)
#define WS2812_LED_SW1  1  // LED index 1 for switch 1 (green)

// Device tree aliases
#define BLE_STATUS_LED_NODE   DT_ALIAS(ble_status_led)
#define SW0_LED_NODE          DT_ALIAS(sw0_led)
#define SW1_LED_NODE          DT_ALIAS(sw1_led)
#define MATRIX_COL0_NODE      DT_ALIAS(matrix_col0)
#define MATRIX_ROW0_NODE      DT_ALIAS(matrix_row0)
#define MATRIX_ROW1_NODE      DT_ALIAS(matrix_row1)

// GPIO device specs
static const struct gpio_dt_spec ble_status_led = GPIO_DT_SPEC_GET(BLE_STATUS_LED_NODE, gpios);
static const struct gpio_dt_spec sw0_led = GPIO_DT_SPEC_GET(SW0_LED_NODE, gpios);
static const struct gpio_dt_spec sw1_led = GPIO_DT_SPEC_GET(SW1_LED_NODE, gpios);
static const struct gpio_dt_spec matrix_col0 = GPIO_DT_SPEC_GET(MATRIX_COL0_NODE, gpios);
static const struct gpio_dt_spec matrix_row0 = GPIO_DT_SPEC_GET(MATRIX_ROW0_NODE, gpios);
static const struct gpio_dt_spec matrix_row1 = GPIO_DT_SPEC_GET(MATRIX_ROW1_NODE, gpios);

int main(void)
{
    int ret;
    int sw0_state, sw1_state;
    int prev_sw0_state = 0, prev_sw1_state = 0; // Previous states (0 = released)

    printk("\n");
    printk("╔════════════════════════════════════╗\n");
    printk("║   Superr MIDI Controller v1.0     ║\n");
    printk("║   2x1 Matrix BLE MIDI Device      ║\n");
    printk("║   + WS2812B LED Strip (24 LEDs)   ║\n");
    printk("╚════════════════════════════════════╝\n");
    printk("\n");

    // ========== Configure BLE Status LED ==========
    if (!gpio_is_ready_dt(&ble_status_led)) {
        printk("ERROR: BLE Status LED device not ready\n");
        return 0;
    }
    ret = gpio_pin_configure_dt(&ble_status_led, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        printk("ERROR: Failed to configure BLE Status LED\n");
        return 0;
    }
    printk("✓ BLE Status LED configured (P0.28)\n");

    // ========== Configure Switch LEDs ==========
    if (!gpio_is_ready_dt(&sw0_led)) {
        printk("ERROR: SW0 LED device not ready\n");
        return 0;
    }
    ret = gpio_pin_configure_dt(&sw0_led, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        printk("ERROR: Failed to configure SW0 LED\n");
        return 0;
    }
    printk("✓ Switch 0 LED configured (P0.29)\n");

    if (!gpio_is_ready_dt(&sw1_led)) {
        printk("ERROR: SW1 LED device not ready\n");
        return 0;
    }
    ret = gpio_pin_configure_dt(&sw1_led, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        printk("ERROR: Failed to configure SW1 LED\n");
        return 0;
    }
    printk("✓ Switch 1 LED configured (P0.30)\n");

    // ========== Configure Matrix Column (Drive) ==========
    if (!gpio_is_ready_dt(&matrix_col0)) {
        printk("ERROR: Matrix Col0 device not ready\n");
        return 0;
    }
    ret = gpio_pin_configure_dt(&matrix_col0, GPIO_OUTPUT_ACTIVE);
    if (ret < 0) {
        printk("ERROR: Failed to configure Matrix Col0\n");
        return 0;
    }
    // Drive column HIGH for matrix scanning
    gpio_pin_set_dt(&matrix_col0, 1);
    printk("✓ Matrix Column 0 configured (P0.25 - OUTPUT HIGH)\n");

    // ========== Configure Matrix Rows (Sense) ==========
    if (!gpio_is_ready_dt(&matrix_row0)) {
        printk("ERROR: Matrix Row0 device not ready\n");
        return 0;
    }
    ret = gpio_pin_configure_dt(&matrix_row0, GPIO_INPUT | GPIO_PULL_DOWN);
    if (ret < 0) {
        printk("ERROR: Failed to configure Matrix Row0\n");
        return 0;
    }
    printk("✓ Matrix Row 0 configured (P0.24 - INPUT PULL_DOWN)\n");

    if (!gpio_is_ready_dt(&matrix_row1)) {
        printk("ERROR: Matrix Row1 device not ready\n");
        return 0;
    }
    ret = gpio_pin_configure_dt(&matrix_row1, GPIO_INPUT | GPIO_PULL_DOWN);
    if (ret < 0) {
        printk("ERROR: Failed to configure Matrix Row1\n");
        return 0;
    }
    printk("✓ Matrix Row 1 configured (P0.26 - INPUT PULL_DOWN)\n");

    printk("\n");

        // ========== Initialize WS2812B LED Strip ==========
        ret = ws2812_init();
        if (ret) {
            printk("ERROR: WS2812B initialization failed (err %d)\n", ret);
            return 0;
        }

    // ========== Initialize BLE MIDI ==========
    ret = ble_midi_init(&ble_status_led);
    if (ret) {
        printk("ERROR: BLE MIDI initialization failed (err %d)\n", ret);
        return 0;
    }

        printk("\n");
        printk("╔════════════════════════════════════╗\n");
        printk("║  OPERATION MODE                   ║\n");
        printk("╠════════════════════════════════════╣\n");
        printk("║  MIDI Mapping:                    ║\n");
        printk("║  SW0 (Row0) → Note 60  (C4)       ║\n");
        printk("║  SW1 (Row1) → Note 83  (B5)       ║\n");
        printk("║                                   ║\n");
        printk("║  LED Strip Sync:                  ║\n");
        printk("║  WS2812 LED 0 → SW0 (Blue)        ║\n");
        printk("║  WS2812 LED 1 → SW1 (Green)       ║\n");
        printk("║                                   ║\n");
        printk("║  ⚠️  IMPORTANT:                    ║\n");
        printk("║  LEDs work WITHOUT BLE!           ║\n");
        printk("║  Just press switches to test      ║\n");
        printk("╚════════════════════════════════════╝\n");
        printk("\n");
        printk("✅ System Ready!\n");
        printk("→ Press SW0 or SW1 to control LEDs\n");
        printk("→ Connect BLE for MIDI output (optional)\n");
        printk("\n");

    // ========== Main Loop ==========
    while (1) {
        // Read matrix row states
        // Column is driven HIGH, so pressed switch will pull row HIGH
        sw0_state = gpio_pin_get_dt(&matrix_row0);
        sw1_state = gpio_pin_get_dt(&matrix_row1);

        // ===== Handle Switch 0 (Row 0) =====
        if (sw0_state != prev_sw0_state) {
            uint8_t midi_packet[5];
            int len;
            
            if (sw0_state == 1) {
                // Switch pressed
                printk("\n🔵 SW0: PRESSED → LED 0 BLUE + MIDI Note %d (C4) ON\n", MIDI_NOTE_SW0);
                gpio_pin_set_dt(&sw0_led, 1);  // Turn on indicator LED
                
                    // Turn on WS2812 LED 0 (Blue) - BRIGHT
                    ws2812_set_led(WS2812_LED_SW0, 0, 0, 255);  // Full blue
                    ws2812_update();
                    printk("   ✓ WS2812 LED 0 set to BLUE\n");
                
                len = midi_ble_note_on(MIDI_NOTE_SW0, 100, MIDI_CHANNEL, 
                                       midi_packet, sizeof(midi_packet));
            } else {
                // Switch released
                printk("○  SW0: RELEASED → LED 0 OFF + MIDI Note %d (C4) OFF\n\n", MIDI_NOTE_SW0);
                gpio_pin_set_dt(&sw0_led, 0);  // Turn off indicator LED
                
                    // Turn off WS2812 LED 0
                    ws2812_led_off(WS2812_LED_SW0);
                    ws2812_update();
                    printk("   ✓ WS2812 LED 0 turned OFF\n");
                
                len = midi_ble_note_off(MIDI_NOTE_SW0, 0, MIDI_CHANNEL,
                                        midi_packet, sizeof(midi_packet));
            }
            
            if (len > 0) {
                ble_midi_send(midi_packet, len);
            }
            
            prev_sw0_state = sw0_state;
        }

        // ===== Handle Switch 1 (Row 1) =====
        if (sw1_state != prev_sw1_state) {
            uint8_t midi_packet[5];
            int len;
            
            if (sw1_state == 1) {
                // Switch pressed
                printk("\n🟢 SW1: PRESSED → LED 1 GREEN + MIDI Note %d (B5) ON\n", MIDI_NOTE_SW1);
                gpio_pin_set_dt(&sw1_led, 1);  // Turn on indicator LED
                
                    // Turn on WS2812 LED 1 (Green) - BRIGHT
                    ws2812_set_led(WS2812_LED_SW1, 0, 255, 0);  // Full green
                    ws2812_update();
                    printk("   ✓ WS2812 LED 1 set to GREEN\n");
                
                len = midi_ble_note_on(MIDI_NOTE_SW1, 100, MIDI_CHANNEL,
                                       midi_packet, sizeof(midi_packet));
            } else {
                // Switch released
                printk("○  SW1: RELEASED → LED 1 OFF + MIDI Note %d (B5) OFF\n\n", MIDI_NOTE_SW1);
                gpio_pin_set_dt(&sw1_led, 0);  // Turn off indicator LED
                
                    // Turn off WS2812 LED 1
                    ws2812_led_off(WS2812_LED_SW1);
                    ws2812_update();
                    printk("   ✓ WS2812 LED 1 turned OFF\n");
                
                len = midi_ble_note_off(MIDI_NOTE_SW1, 0, MIDI_CHANNEL,
                                        midi_packet, sizeof(midi_packet));
            }
            
            if (len > 0) {
                ble_midi_send(midi_packet, len);
            }
            
            prev_sw1_state = sw1_state;
        }

        // Small delay to prevent excessive CPU usage
        k_msleep(5);
    }

    return 0;
}

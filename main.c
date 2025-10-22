#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include "ble_midi_service.h"
#include "midi_ble.h"

// MIDI note assignments
#define MIDI_NOTE_SW1  60  // Middle C (C4)
#define MIDI_NOTE_SW2  62  // D4
#define MIDI_CHANNEL   0   // MIDI Channel 1 (0-indexed)

#define LED0_NODE DT_ALIAS(led0)
#define SW1_NODE DT_ALIAS(sw1)
#define SW2_DRIVE_NODE DT_ALIAS(sw2_drive)
#define SW2_SENSE_NODE DT_ALIAS(sw2_sense)

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec button1 = GPIO_DT_SPEC_GET(SW1_NODE, gpios);
static const struct gpio_dt_spec sw2_drive = GPIO_DT_SPEC_GET(SW2_DRIVE_NODE, gpios);
static const struct gpio_dt_spec sw2_sense = GPIO_DT_SPEC_GET(SW2_SENSE_NODE, gpios);

int main(void)
{
    int ret;
    int sw1_state, sw2_state;
    int prev_sw1_state = 1, prev_sw2_state = 0; // Previous states (start with released)

    printk("Dual Switch LED Demo Started\n");

    // Check if LED device is ready
    if (!gpio_is_ready_dt(&led)) {
        printk("Error: LED device not ready\n");
        return 0;
    }

    // Check if button devices are ready
    if (!gpio_is_ready_dt(&button1)) {
        printk("Error: Button 1 device not ready\n");
        return 0;
    }

    if (!gpio_is_ready_dt(&sw2_drive)) {
        printk("Error: SW2 Drive Pin device not ready\n");
        return 0;
    }

    if (!gpio_is_ready_dt(&sw2_sense)) {
        printk("Error: SW2 Sense Pin device not ready\n");
        return 0;
    }

    // Configure LED pin as output
    ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
    if (ret < 0) {
        printk("Error: Failed to configure LED pin\n");
        return 0;
    }

    // Configure button 1 pin as input with pull-up
    ret = gpio_pin_configure_dt(&button1, GPIO_INPUT | GPIO_PULL_UP);
    if (ret < 0) {
        printk("Error: Failed to configure Button 1 pin\n");
        return 0;
    }

    // Configure SW2 drive pin as OUTPUT (set HIGH to drive the line)
    ret = gpio_pin_configure_dt(&sw2_drive, GPIO_OUTPUT_ACTIVE);
    if (ret < 0) {
        printk("Error: Failed to configure SW2 Drive Pin\n");
        return 0;
    }

    // Configure SW2 sense pin as INPUT with pull-down
    ret = gpio_pin_configure_dt(&sw2_sense, GPIO_INPUT | GPIO_PULL_DOWN);
    if (ret < 0) {
        printk("Error: Failed to configure SW2 Sense Pin\n");
        return 0;
    }

    // Set SW2 drive pin HIGH (matrix scanning: drive the column)
    gpio_pin_set_dt(&sw2_drive, 1);

    // Initialize BLE MIDI
    ret = ble_midi_init();
    if (ret) {
        printk("BLE MIDI initialization failed (err %d)\n", ret);
        return 0;
    }

    printk("Ready! Press switches to control LED\n");

    while (1) {
        // Read button states
        sw1_state = gpio_pin_get_dt(&button1);
        
        // Matrix scanning for SW2:
        // P0.24 is driving HIGH (already set)
        // Read P0.25: If switch is pressed, it reads HIGH (receives signal from P0.24)
        //             If switch is open, it reads LOW (pulled down)
        sw2_state = gpio_pin_get_dt(&sw2_sense);

        // Check for SW1 state change
        if (sw1_state != prev_sw1_state) {
            uint8_t midi_packet[5];
            int len;
            
            if (sw1_state == 0) {
                printk("SW1: ON - MIDI Note %d ON\n", MIDI_NOTE_SW1);
                len = midi_ble_note_on(MIDI_NOTE_SW1, 100, MIDI_CHANNEL, 
                                       midi_packet, sizeof(midi_packet));
            } else {
                printk("SW1: OFF - MIDI Note %d OFF\n", MIDI_NOTE_SW1);
                len = midi_ble_note_off(MIDI_NOTE_SW1, 0, MIDI_CHANNEL,
                                        midi_packet, sizeof(midi_packet));
            }
            
            if (len > 0) {
                ble_midi_send(midi_packet, len);
            }
            
            prev_sw1_state = sw1_state;
        }

        // Check for SW2 state change
        if (sw2_state != prev_sw2_state) {
            uint8_t midi_packet[5];
            int len;
            
            if (sw2_state == 1) {
                printk("SW2: ON - MIDI Note %d ON\n", MIDI_NOTE_SW2);
                len = midi_ble_note_on(MIDI_NOTE_SW2, 100, MIDI_CHANNEL,
                                       midi_packet, sizeof(midi_packet));
            } else {
                printk("SW2: OFF - MIDI Note %d OFF\n", MIDI_NOTE_SW2);
                len = midi_ble_note_off(MIDI_NOTE_SW2, 0, MIDI_CHANNEL,
                                        midi_packet, sizeof(midi_packet));
            }
            
            if (len > 0) {
                ble_midi_send(midi_packet, len);
            }
            
            prev_sw2_state = sw2_state;
        }

        // Control LED based on switch states
        // LED turns ON if either switch is pressed
        // SW1: active-low (0=pressed), SW2: active-high (1=pressed)
        if (sw1_state == 0 || sw2_state == 1) {
            gpio_pin_set_dt(&led, 1);
        } else {
            gpio_pin_set_dt(&led, 0);
        }

        // Small delay to prevent excessive CPU usage
        k_msleep(10);
    }

    return 0;
}
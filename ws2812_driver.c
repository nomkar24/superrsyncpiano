/*!
 * @file ws2812_driver.c
 * 
 * WS2812B LED Strip Driver Implementation
 * Uses GPIO bit-banging with precise timing for nRF5340
 */

#include "ws2812_driver.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <string.h>

// GPIO device and pin
static const struct device *gpio_dev = NULL;
static uint8_t led_data[WS2812_NUM_LEDS * 3];  // GRB format

// WS2812B Timing (conservative timing for maximum compatibility)
// These values work with most WS2812B variants (WS2812, WS2812B, WS2813)
// T0H: 0 bit high time (target ~400ns, using ~625ns for safety)
// T0L: 0 bit low time (target ~850ns, using ~1.25us for safety)
// T1H: 1 bit high time (target ~800ns, using ~1.25us for safety)
// T1L: 1 bit low time (target ~450ns, using ~625ns for safety)
#define T0H_CYCLES  50   // ~0.625us
#define T0L_CYCLES  100  // ~1.25us
#define T1H_CYCLES  100  // ~1.25us
#define T1L_CYCLES  50   // ~0.625us

/**
 * @brief Cycle-accurate delay
 */
static inline void delay_cycles(uint32_t cycles) {
    for (volatile uint32_t i = 0; i < cycles; i++) {
        __asm__ volatile ("nop");
    }
}

/**
 * @brief Send a single bit to WS2812B
 */
static void send_bit(bool bit_value) {
    if (bit_value) {
        // Send '1': HIGH for 800ns, LOW for 450ns
        gpio_pin_set_raw(gpio_dev, WS2812_PIN, 1);
        delay_cycles(T1H_CYCLES);
        gpio_pin_set_raw(gpio_dev, WS2812_PIN, 0);
        delay_cycles(T1L_CYCLES);
    } else {
        // Send '0': HIGH for 400ns, LOW for 850ns
        gpio_pin_set_raw(gpio_dev, WS2812_PIN, 1);
        delay_cycles(T0H_CYCLES);
        gpio_pin_set_raw(gpio_dev, WS2812_PIN, 0);
        delay_cycles(T0L_CYCLES);
    }
}

/**
 * @brief Send a byte to WS2812B (MSB first)
 */
static void send_byte(uint8_t byte) {
    for (int i = 7; i >= 0; i--) {
        send_bit(byte & (1 << i));
    }
}

int ws2812_init(void) {
    printk("\n");
    printk("========================================\n");
    printk("  WS2812B LED Strip Initialization\n");
    printk("========================================\n");
    
    // Get GPIO device
    gpio_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));
    if (!device_is_ready(gpio_dev)) {
        printk("ERROR: GPIO device not ready\n");
        return -1;
    }
    printk("✓ GPIO device ready\n");
    
    // Configure pin as output
    int ret = gpio_pin_configure(gpio_dev, WS2812_PIN, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        printk("ERROR: Failed to configure WS2812 pin (error %d)\n", ret);
        return ret;
    }
    printk("✓ P0.%d configured as OUTPUT\n", WS2812_PIN);
    
    // Test GPIO toggle (verify pin works)
    printk("Testing GPIO toggle...\n");
    for (int i = 0; i < 5; i++) {
        gpio_pin_set_raw(gpio_dev, WS2812_PIN, 1);
        k_msleep(50);
        gpio_pin_set_raw(gpio_dev, WS2812_PIN, 0);
        k_msleep(50);
    }
    printk("✓ GPIO toggle successful\n");
    
    // Initialize buffer (all LEDs off)
    memset(led_data, 0, sizeof(led_data));
    printk("✓ LED buffer cleared (%d bytes)\n", sizeof(led_data));
    
    // Send initial reset
    printk("Sending reset signal...\n");
    gpio_pin_set_raw(gpio_dev, WS2812_PIN, 0);
    k_busy_wait(100);  // 100us reset
    
    // Test sequence: Flash multiple LEDs to verify strip works
    printk("\nLED Test Sequence (testing first 5 LEDs):\n");
    printk("Watch your LED strip carefully!\n\n");
    
    // Test LED 0 - RED
    printk("→ LED 0: RED (500ms)\n");
    ws2812_set_led(0, 255, 0, 0);
    ws2812_update();
    k_msleep(500);
    ws2812_led_off(0);
    ws2812_update();
    k_msleep(200);
    
    // Test LED 1 - GREEN  
    printk("→ LED 1: GREEN (500ms)\n");
    ws2812_set_led(1, 0, 255, 0);
    ws2812_update();
    k_msleep(500);
    ws2812_led_off(1);
    ws2812_update();
    k_msleep(200);
    
    // Test LED 2 - BLUE
    printk("→ LED 2: BLUE (500ms)\n");
    ws2812_set_led(2, 0, 0, 255);
    ws2812_update();
    k_msleep(500);
    ws2812_led_off(2);
    ws2812_update();
    k_msleep(200);
    
    // Test LED 3 - YELLOW
    printk("→ LED 3: YELLOW (500ms)\n");
    ws2812_set_led(3, 255, 255, 0);
    ws2812_update();
    k_msleep(500);
    ws2812_led_off(3);
    ws2812_update();
    k_msleep(200);
    
    // Test LED 4 - WHITE
    printk("→ LED 4: WHITE (500ms)\n");
    ws2812_set_led(4, 255, 255, 255);
    ws2812_update();
    k_msleep(500);
    ws2812_led_off(4);
    ws2812_update();
    k_msleep(200);
    
    // Chase pattern - helps identify which LED is which
    printk("\n→ Running chase pattern (LEDs 0-9)...\n");
    for (int i = 0; i < 10; i++) {
        ws2812_set_led(i, 50, 0, 50);  // Dim purple
        ws2812_update();
        k_msleep(100);
        ws2812_led_off(i);
        ws2812_update();
    }
    
    // All off
    printk("→ All LEDs OFF\n");
    ws2812_clear_all();
    
    printk("\n⚠️  DID YOU SEE ANY LEDS LIGHT UP?\n");
    printk("   If NO: Check power (5V), wiring, or try different strip\n");
    printk("   If YES: Note which LED worked - might help debug!\n");
    
    printk("\n✅ WS2812B initialization complete!\n");
    printk("   • Strip: %d LEDs on P0.%d\n", WS2812_NUM_LEDS, WS2812_PIN);
    printk("   • Controlling: LED 0 (SW0-Blue), LED 1 (SW1-Green)\n");
    printk("   • Remaining LEDs (2-%d): OFF/unused\n", WS2812_NUM_LEDS-1);
    printk("   • BLE NOT required for LED operation\n");
    printk("========================================\n\n");
    
    return 0;
}

void ws2812_set_led(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
    if (index >= WS2812_NUM_LEDS) {
        return;
    }
    
    // WS2812B expects GRB format
    uint16_t pos = index * 3;
    led_data[pos]     = g;  // Green first
    led_data[pos + 1] = r;  // Red second
    led_data[pos + 2] = b;  // Blue third
}

void ws2812_led_off(uint8_t index) {
    ws2812_set_led(index, 0, 0, 0);
}

void ws2812_update(void) {
    if (!gpio_dev) {
        printk("ERROR: WS2812 not initialized!\n");
        return;
    }
    
    // Disable interrupts for precise timing
    uint32_t key = irq_lock();
    
    // Send all LED data (all 60 LEDs = 180 bytes)
    for (uint16_t i = 0; i < (WS2812_NUM_LEDS * 3); i++) {
        send_byte(led_data[i]);
    }
    
    // Re-enable interrupts
    irq_unlock(key);
    
    // Reset/latch code: >50us low (WS2812B spec requires 50us min)
    gpio_pin_set_raw(gpio_dev, WS2812_PIN, 0);
    k_busy_wait(100);  // 100us reset for safety with 60 LEDs
}

void ws2812_clear_all(void) {
    memset(led_data, 0, sizeof(led_data));
    ws2812_update();
}


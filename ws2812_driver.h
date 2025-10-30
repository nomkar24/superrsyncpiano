/*!
 * @file ws2812_driver.h
 * 
 * Simple WS2812B LED Strip Driver for Zephyr RTOS
 * Optimized for nRF5340 - Pure C implementation
 */

#ifndef WS2812_DRIVER_H
#define WS2812_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

#define WS2812_NUM_LEDS 24  // Total LEDs in strip
#define WS2812_PIN 7        // GPIO P0.07

/**
 * @brief Initialize WS2812B LED strip
 * @return 0 on success, negative on error
 */
int ws2812_init(void);

/**
 * @brief Set a single LED color
 * @param index LED index (0-23)
 * @param r Red value (0-255)
 * @param g Green value (0-255)
 * @param b Blue value (0-255)
 */
void ws2812_set_led(uint8_t index, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Turn off a single LED
 * @param index LED index (0-23)
 */
void ws2812_led_off(uint8_t index);

/**
 * @brief Update the LED strip (send data)
 */
void ws2812_update(void);

/**
 * @brief Clear all LEDs (turn off)
 */
void ws2812_clear_all(void);

#endif // WS2812_DRIVER_H


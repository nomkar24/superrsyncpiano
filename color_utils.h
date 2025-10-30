/*!
 * @file color_utils.h
 * 
 * Color Conversion Utilities for RGB LED Effects
 */

#ifndef COLOR_UTILS_H
#define COLOR_UTILS_H

#include <stdint.h>

/**
 * @brief Convert HSV color to RGB
 * @param h Hue (0-255)
 * @param s Saturation (0-255)
 * @param v Value/Brightness (0-255)
 * @param r Output: Red (0-255)
 * @param g Output: Green (0-255)
 * @param b Output: Blue (0-255)
 */
void hsv_to_rgb(uint8_t h, uint8_t s, uint8_t v, 
                uint8_t *r, uint8_t *g, uint8_t *b);

/**
 * @brief Apply brightness scaling to RGB color
 * @param r Red value (modified in place)
 * @param g Green value (modified in place)
 * @param b Blue value (modified in place)
 * @param brightness Brightness 0-255
 */
void apply_brightness(uint8_t *r, uint8_t *g, uint8_t *b, uint8_t brightness);

#endif // COLOR_UTILS_H


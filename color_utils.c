/*!
 * @file color_utils.c
 * 
 * Color Conversion Utilities Implementation
 */

#include "color_utils.h"

void hsv_to_rgb(uint8_t h, uint8_t s, uint8_t v, 
                uint8_t *r, uint8_t *g, uint8_t *b) {
    // Handle grayscale
    if (s == 0) {
        *r = *g = *b = v;
        return;
    }
    
    // Convert hue to region (0-5)
    uint8_t region = h / 43;
    uint8_t remainder = (h - (region * 43)) * 6;
    
    uint8_t p = (v * (255 - s)) >> 8;
    uint8_t q = (v * (255 - ((s * remainder) >> 8))) >> 8;
    uint8_t t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;
    
    switch (region) {
        case 0:
            *r = v; *g = t; *b = p;
            break;
        case 1:
            *r = q; *g = v; *b = p;
            break;
        case 2:
            *r = p; *g = v; *b = t;
            break;
        case 3:
            *r = p; *g = q; *b = v;
            break;
        case 4:
            *r = t; *g = p; *b = v;
            break;
        default:
            *r = v; *g = p; *b = q;
            break;
    }
}

void apply_brightness(uint8_t *r, uint8_t *g, uint8_t *b, uint8_t brightness) {
    *r = (*r * brightness) >> 8;
    *g = (*g * brightness) >> 8;
    *b = (*b * brightness) >> 8;
}


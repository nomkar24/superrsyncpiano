#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include "ble_midi_service.h"
#include "midi_ble.h"
#include <zephyr/drivers/led_strip.h>
#include <math.h>
#include <zephyr/drivers/led_strip.h>
#include <math.h>
#include <math.h>
#include <math.h>
#include <zephyr/drivers/watchdog.h>
#include <soc.h>
#include <hal/nrf_regulators.h>
#include "ble_config_service.h"

// ========== RTOS CONFIGURATION ==========
#define SCAN_STACK_SIZE 1024
#define SCAN_PRIORITY   1
#define LED_STACK_SIZE  2048
#define LED_PRIORITY    5

K_THREAD_STACK_DEFINE(scan_stack, SCAN_STACK_SIZE);
K_THREAD_STACK_DEFINE(led_stack, LED_STACK_SIZE);

struct k_thread scan_thread_data;
struct k_thread led_thread_data;

// ========== WATCHDOG GLOBALS ==========
static const struct device *wdt;
static int wdt_chan_scan;
static int wdt_chan_led;

// Event Queue for communicating Key Presses to LED Thread
struct led_event {
    uint8_t key_index;
    uint8_t velocity;
    bool is_on;
};

// Queue can hold 50 events (buffer for rapid playing)
K_MSGQ_DEFINE(led_msgq, sizeof(struct led_event), 50, 4);

// ========== 24-KEY CONFIGURATION ==========
#define NUM_COLS 4    // 4 columns (all active)
#define NUM_ROWS 6    // 6 rows per matrix
#define NUM_KEYS (NUM_COLS * NUM_ROWS)  // 24 keys total

// ========== GPIO PIN ASSIGNMENTS (17 pins) ==========
// STANDARD KEYBOARD MATRIX LOGIC:
// HARDWARE: Diodes with cathode at switch, anode at row
// Current flow when key pressed: Column (pull-up HIGH) → Switch → Diode → Row (scanning LOW)
// LOGIC: Rows OUTPUT (default HIGH, scan LOW), Columns INPUT (pull-up, read LOW when pressed)

// COLUMNS (INPUT with PULL-UP) - Moved to P0 (Safe Analog Inputs)
#define COL1_PIN  4   // P0.04 (AIN0)
#define COL2_PIN  5   // P0.05 (AIN1)
#define COL3_PIN  6   // P0.06 (AIN2)
#define COL4_PIN  7   // P0.07 (AIN3)

// MATRIX 1 ROWS (OUTPUT) - Moved to P0 (Safe GPIOs & NFC pins)
#define M1_ROW1_PIN  25  // P0.25 (Safe)
#define M1_ROW2_PIN  26  // P0.26 (Safe)
#define M1_ROW3_PIN  2   // P0.02 (NFC1 -> GPIO)
#define M1_ROW4_PIN  3   // P0.03 (NFC2 -> GPIO)
#define M1_ROW5_PIN  10  // P0.10 (Safe)
#define M1_ROW6_PIN  11  // P0.11 (Safe)

// MATRIX 2 ROWS (OUTPUT) - Use P1 pins
#define M2_ROWa_PIN  10  // P1.10
#define M2_ROWb_PIN  11  // P1.11
#define M2_ROWc_PIN  12  // P1.12
#define M2_ROWd_PIN  13  // P1.13
#define M2_ROWe_PIN  14  // P1.14
#define M2_ROWf_PIN  15  // P1.15



// ========== MIDI CONFIGURATION ==========
#define MIDI_CHANNEL 0         // MIDI Channel 1 (0-indexed)
#define BASE_MIDI_NOTE 60      // C4 (Middle C) - Starting note

// ========== VELOCITY SENSING PARAMETERS ==========
#define MAX_VELOCITY_TIME_MS 100   // Max time for velocity calculation
#define MIN_VELOCITY 20            // Minimum MIDI velocity (soft)
#define MAX_VELOCITY 127           // Maximum MIDI velocity (hard)

// ========== KEY STATE STRUCTURE ==========
typedef struct {
    bool matrix1_active;      // First contact made (key starts)
    bool matrix2_active;      // Second contact made (key fully pressed)
    bool note_playing;        // Note currently playing
    uint32_t matrix1_time;    // Timestamp of first contact (milliseconds)
    uint32_t matrix2_time;    // Timestamp of second contact (milliseconds)
    uint8_t velocity;         // Calculated MIDI velocity
    uint32_t m1_latch_timer;  // Debounce latch
    uint32_t m2_latch_timer;  // Debounce latch M2
} key_state_t;

// ========== GLOBAL VARIABLES ==========
static struct gpio_dt_spec cols[NUM_COLS];           // 4 columns
static struct gpio_dt_spec matrix1_rows[NUM_ROWS];   // 6 rows for Matrix 1
static struct gpio_dt_spec matrix2_rows[NUM_ROWS];   // 6 rows for Matrix 2

static struct gpio_dt_spec ble_status_led = GPIO_DT_SPEC_GET(DT_ALIAS(ble_status_led), gpios);
static key_state_t keys[NUM_KEYS];                   // 24 keys

// ========== POWER MANAGEMENT ==========
static int64_t last_activity_time = 0;
#define SLEEP_TIMEOUT_MS  (5 * 60 * 1000) // 5 Minutes
#define DIM_TIMEOUT_MS    (1 * 60 * 1000) // 1 Minute

// ========== LED STRIP CONFIGURATION ==========
#define STRIP_NODE DT_ALIAS(led_strip)
#define SUB_STRIP_NUM_PIXELS 25

static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);
// VISUAL ENGINE STATE
static struct led_rgb pixels[SUB_STRIP_NUM_PIXELS];         // Current Displayed Color
static struct led_rgb target_pixels[SUB_STRIP_NUM_PIXELS];  // Target Color (Smoothing)

// LED Colors (R, G, B) - scaled down for brightness safety
// static const struct led_rgb color_on = { .r = 255, .g = 0, .b = 0 }; // Red
// static const struct led_rgb color_off = { .r = 0, .g = 0, .b = 0 };  // Off

// ========== GPIO INITIALIZATION ==========
static int init_gpio(void)
{
    int ret;
    const struct device *gpio0 = DEVICE_DT_GET(DT_NODELABEL(gpio0));
    const struct device *gpio1 = DEVICE_DT_GET(DT_NODELABEL(gpio1));
    
    if (!device_is_ready(gpio0)) {
        printk("ERROR: GPIO0 device not ready\n");
        return -1;
    }
    
    if (!device_is_ready(gpio1)) {
        printk("ERROR: GPIO1 device not ready\n");
        return -1;
    }
    
    printk("\n[GPIO] Initializing GPIO Pins (24 Keys, Diode-Protected Matrix):\n");
    printk("==============================================\n");
    printk("[INFO] Using P1 (GPIO1) port to avoid conflicts with board features\n\n");
    
    // ===== Initialize COLUMN pins (INPUT with PULL-UP - Standard Matrix Logic) =====
    printk("[COLUMNS] INPUT with PULL-UP - Standard keyboard matrix:\n");
    const uint8_t col_pins[] = {COL1_PIN, COL2_PIN, COL3_PIN, COL4_PIN};
    for (int i = 0; i < NUM_COLS; i++) {
        cols[i].port = gpio0;  // MOVED TO GPIO0
        cols[i].pin = col_pins[i];
        cols[i].dt_flags = GPIO_ACTIVE_HIGH;
        
        ret = gpio_pin_configure(gpio0, col_pins[i], GPIO_INPUT | GPIO_PULL_UP);
        if (ret < 0) {
            printk("[ERROR] Failed to configure Column %d (P0.%02d)\n", i + 1, col_pins[i]);
            return ret;
        }
        printk("[OK] Column %d: P0.%02d (INPUT with pull-up, default HIGH)\n", i + 1, col_pins[i]);
    }
    

    
    // ===== Initialize MATRIX 1 row pins (OUTPUT - Drive for scanning) =====
    printk("\n[MATRIX 1 ROWS] OUTPUT - Scanned LOW one at a time:\n");
    const uint8_t m1_row_pins[] = {M1_ROW1_PIN, M1_ROW2_PIN, M1_ROW3_PIN, 
                                     M1_ROW4_PIN, M1_ROW5_PIN, M1_ROW6_PIN};
    for (int i = 0; i < NUM_ROWS; i++) {
        matrix1_rows[i].port = gpio0;  // MOVED TO GPIO0
        matrix1_rows[i].pin = m1_row_pins[i];
        matrix1_rows[i].dt_flags = GPIO_ACTIVE_HIGH;
        
        ret = gpio_pin_configure(gpio0, m1_row_pins[i], GPIO_OUTPUT);
        if (ret < 0) {
            printk("[ERROR] Failed to configure Matrix 1 Row %d (P0.%02d)\n", i + 1, m1_row_pins[i]);
            return ret;
        }
        // Set row HIGH by default (not scanning)
        gpio_pin_set_dt(&matrix1_rows[i], 1);
        printk("[OK] Matrix 1, Row %d: P0.%02d (OUTPUT -> set HIGH)\n", i + 1, m1_row_pins[i]);
    }
    
    // ===== Initialize MATRIX 2 row pins (OUTPUT - Drive for scanning) =====
    printk("\n[MATRIX 2 ROWS] OUTPUT - Scanned LOW one at a time:\n");
    const uint8_t m2_row_pins[] = {M2_ROWa_PIN, M2_ROWb_PIN, M2_ROWc_PIN, 
                                     M2_ROWd_PIN, M2_ROWe_PIN, M2_ROWf_PIN};
    for (int i = 0; i < NUM_ROWS; i++) {
        matrix2_rows[i].port = gpio1;  // Using GPIO1 port
        matrix2_rows[i].pin = m2_row_pins[i];
        matrix2_rows[i].dt_flags = GPIO_ACTIVE_HIGH;
        
        ret = gpio_pin_configure(gpio1, m2_row_pins[i], GPIO_OUTPUT);
        if (ret < 0) {
            printk("[ERROR] Failed to configure Matrix 2 Row %c (P1.%02d)\n", 'a' + i, m2_row_pins[i]);
            return ret;
        }
        // Set row HIGH by default (not scanning)
        gpio_pin_set_dt(&matrix2_rows[i], 1);
        printk("[OK] Matrix 2, Row %c: P1.%02d (OUTPUT -> set HIGH)\n", 'a' + i, m2_row_pins[i]);
    }
    
    printk("==============================================\n");
    
    // Wait for pins to stabilize - longer delay for P1 port
    printk("\n[WAIT] Waiting 50ms for GPIO pins to stabilize...\n");
    k_msleep(50);
    
    // Verify all columns are HIGH and rows are LOW (no keys pressed)
    printk("\n[VERIFY] Pin states after stabilization:\n");
    printk("   Columns (should be HIGH):\n");
    for (int i = 0; i < NUM_COLS; i++) {
        int col_state = gpio_pin_get_dt(&cols[i]);
        printk("     Col %d P0.%02d: %s\n", i + 1, cols[i].pin, 
               col_state ? "HIGH [OK]" : "LOW [ERROR]");
    }
    
    printk("   Rows (should be HIGH when no key pressed):\n");
    for (int i = 0; i < NUM_ROWS; i++) {
        int m1_state = gpio_pin_get_dt(&matrix1_rows[i]);
        int m2_state = gpio_pin_get_dt(&matrix2_rows[i]);
        printk("     Row %d: M1=P0.%02d %s, M2=P1.%02d %s\n", 
               i + 1, 
               matrix1_rows[i].pin, m1_state ? "HIGH [OK]" : "LOW [ERROR]",
               matrix2_rows[i].pin, m2_state ? "HIGH [OK]" : "LOW [ERROR]");
    }
    printk("\n");
    
    // ===== GPIO TEST: Blink Row 1 to verify GPIO is working =====
    printk("[TEST] Blinking Matrix 1 Row 1 (P0.25) 5 times...\n");
    printk("   Use multimeter to verify pin toggles HIGH/LOW\n");
    for (int i = 0; i < 5; i++) {
        gpio_pin_set_dt(&matrix1_rows[0], 0);  // Set LOW
        printk("   -> LOW\n");
        k_msleep(500);
        
        gpio_pin_set_dt(&matrix1_rows[0], 1);  // Set HIGH
        printk("   -> HIGH\n");
        k_msleep(500);
    }
    printk("[OK] GPIO test complete!\n\n");
    
    return 0;
}

// ========== POWER MANAGEMENT HELPER ==========
void enter_deep_sleep(void) {
    printk("[POWER] Entering Deep Sleep (System OFF)...\n");
    
    // 1. Turn off LEDs (Black)
    memset(pixels, 0, sizeof(pixels));
    led_strip_update_rgb(strip, pixels, SUB_STRIP_NUM_PIXELS);
    k_busy_wait(100); // Wait for data to send

    // 2. Configure Wake-Up Source (Any Key Press)
    // To wake up, we need a HIGH -> LOW transition (or just LOW level).
    // Mechanics:
    // - Drive ALL Rows LOW.
    // - Configure ALL Columns as INPUT with Pull-Up and SENSE_LOW interrupt.
    // - When key pressed, Col connects to Low Row -> Col goes Low -> Wake Up.
    
    // Set all Rows to Output LOW
    for (int i=0; i<NUM_ROWS; i++) {
        gpio_pin_configure_dt(&matrix1_rows[i], GPIO_OUTPUT_INACTIVE); // Low
        gpio_pin_configure_dt(&matrix2_rows[i], GPIO_OUTPUT_INACTIVE); // Low
    }
    
    // Configure Columns as Interrupts
    for (int i=0; i<NUM_COLS; i++) {
        gpio_pin_interrupt_configure_dt(&cols[i], GPIO_INT_LEVEL_LOW);
    }
    
    // 3. Goodbye
    printk("[POWER] Goodnight. Press any key to wake.\n");
    k_sleep(K_MSEC(100)); // Compose output
    
    // Force System OFF (Deep Sleep) - Manual Register Write
    // nRF5340 Application Core
    #if defined(NRF_REGULATORS)
        nrf_regulators_system_off(NRF_REGULATORS);
    #elif defined(NRF_REGULATORS_NS)
        nrf_regulators_system_off(NRF_REGULATORS_NS);
    #else
        // Fallback for older headers
        NRF_REGULATORS_S->SYSTEMOFF = 1;
    #endif
    
    // Safety barrier
    while(1) { __WFE(); }
}

// Calculate velocity from time difference (inverse relationship)
static uint8_t calculate_velocity(uint32_t time_diff_ms) {
    if (time_diff_ms == 0) return MAX_VELOCITY;
    if (time_diff_ms > MAX_VELOCITY_TIME_MS) return MIN_VELOCITY;
    
    // Linear interpolation
    // Fast (small time) -> High Velocity
    // Slow (large time) -> Low Velocity
    uint8_t raw_vel = MAX_VELOCITY - ((time_diff_ms * (MAX_VELOCITY - MIN_VELOCITY)) / MAX_VELOCITY_TIME_MS);

    // Apply Sensitivity Scaling (Global Setting)
    // g_sensitivity: 50 = 1.0x (Normal)
    // 100 = 2.0x (Super Sensitive)
    // 0 = 0.0x (Off)
    float scale = (float)g_sensitivity / 50.0f;
    int scaled_vel = (int)((float)raw_vel * scale);
    
    if (scaled_vel > 127) scaled_vel = 127;
    if (scaled_vel < 0) scaled_vel = 0;
    
    return (uint8_t)scaled_vel;
}

// ========== FORCE RESET ALL KEYS (Debug Helper) ==========
static void force_reset_all_keys(void)
{
    printk("\n[WARN] FORCE RESET: Clearing all stuck keys!\n");
    for (int i = 0; i < NUM_KEYS; i++) {
        if (keys[i].note_playing) {
            uint8_t midi_note = BASE_MIDI_NOTE + i;
            uint8_t midi_packet[5];
            int len = midi_ble_note_off(midi_note, 0, MIDI_CHANNEL,
                                       midi_packet, sizeof(midi_packet));
            if (len > 0) {
                ble_midi_send(midi_packet, len);
            }
            printk("   Reset Key %d (Note %d)\n", i, midi_note);
        }
        keys[i].matrix1_active = false;
        keys[i].matrix2_active = false;
        keys[i].note_playing = false;
    }
    printk("[OK] All keys reset!\n\n");
}

// ========== MATRIX SCANNING WITH VELOCITY SENSING ==========
// STANDARD KEYBOARD MATRIX LOGIC:
// - Rows are OUTPUT (default HIGH, scan by setting LOW one at a time)
// - Columns are INPUT with PULL-UP (default HIGH)
// - When key pressed and row is LOW: Column reads LOW (pulled down through switch and diode)

// Helper: Apply brightness to color


// Helper: Map Velocity (0-127) to Thermal Color Gradient (Blue -> Purple -> Red)
static struct led_rgb get_velocity_color(uint8_t velocity) {
    struct led_rgb color = {0};
    
    // Clamp velocity
    if (velocity < MIN_VELOCITY) velocity = MIN_VELOCITY;
    if (velocity > MAX_VELOCITY) velocity = MAX_VELOCITY;
    
    // Normalize to 0.0 - 1.0 range based on min/max
    float t = (float)(velocity - MIN_VELOCITY) / (float)(MAX_VELOCITY - MIN_VELOCITY);
    
    // THEME 0: AURORA (Blue -> Purple -> Pink)
    if (g_led_theme == 0) {
        color.r = (uint8_t)(t * 255.0f);
        color.b = (uint8_t)((1.0f - t) * 255.0f);
        if (t > 0.8f) color.g = (uint8_t)((t - 0.8f) * 150.0f); // Hot Pop
    }
    // THEME 1: FIRE (Red -> Orange -> White)
    else if (g_led_theme == 1) {
        color.r = 255;
        color.g = (uint8_t)(t * 200.0f); // Add Green to make Orange/Yellow
        color.b = (uint8_t)(t > 0.8f ? (t - 0.8f) * 255.0f : 0); // White hot tip
    }
    // THEME 2: MATRIX (Dim Green -> Bright Green -> White)
    else {
        color.r = (uint8_t)(t > 0.9f ? (t - 0.9f) * 2550.0f : 0); // Flash white at max (clamped)
        color.g = (uint8_t)(50 + t * 205.0f);
        color.b = 0;
    }
    
    return color;
}

// Helper: Linear Interpolation for Smooth Fades
static uint8_t lerp_uint8(uint8_t current, uint8_t target, float factor) {
    if (current == target) return current;
    float diff = (float)target - (float)current;
    
    // Snap if very close
    if (diff > -1.0f && diff < 1.0f) return target;
    
    return (uint8_t)(current + diff * factor);
}

// RTOS: LED Thread (Handles Animation & Events)
void led_thread_entry(void *p1, void *p2, void *p3) {
    printk("[RTOS] LED Thread Started\n");
    
    // 1. Run Startup Animation
    printk("[Start] Running Premium Aurora Effect...\n");
    int steps = 1500; // 15 seconds
    
    for (int t = 0; t < steps; t++) {
        float time_val = (float)t * 0.05f;
        for (int i = 0; i < SUB_STRIP_NUM_PIXELS; i++) {
            float pos_val = (float)i * 0.3f;
            float wave1 = 0.5f + 0.5f * sinf(time_val + pos_val);
            float wave2 = 0.5f + 0.5f * sinf(time_val * 0.7f - pos_val);
            float wave3 = 0.5f + 0.5f * sinf(time_val * 1.3f + pos_val);
            
            int r = (int)(wave1 * 60.0f);
            int g = (int)(wave2 * 40.0f);
            int b = (int)(wave3 * 80.0f + 20.0f); 

            float brightness = 1.0f;
            if (t < 200) brightness = (float)t / 200.0f;
            if (t > steps - 200) brightness = (float)(steps - t) / 200.0f;

            pixels[i].r = (uint8_t)(r * brightness);
            pixels[i].g = (uint8_t)(g * brightness);
            pixels[i].b = (uint8_t)(b * brightness);
        }
        led_strip_update_rgb(strip, pixels, SUB_STRIP_NUM_PIXELS);
        
        // Feed Watchdog during long animation
        if (wdt) wdt_feed(wdt, wdt_chan_led);
        
        k_msleep(10);
    }
    
    // Clear Strip
    memset(pixels, 0, sizeof(pixels));
    memset(target_pixels, 0, sizeof(target_pixels));
    led_strip_update_rgb(strip, pixels, SUB_STRIP_NUM_PIXELS);
    printk("[App] Ready. Entering LED Loop.\n");

    // 2. Main LED Loop (60 FPS Game Loop)
    struct led_event evt;
    bool led_is_off = false;
    
    while(1) {
        // A. Input Phase (Drain Queue)
        // Check for new notes (non-blocking)
        while (k_msgq_get(&led_msgq, &evt, K_NO_WAIT) == 0) {
            led_is_off = false; // Wake up on event
            int led_idx = evt.key_index + 1; // +1 for sacrificial
            
            if (led_idx < SUB_STRIP_NUM_PIXELS) {
                if (evt.is_on) {
                     // Set TARGET to the new color
                     target_pixels[led_idx] = get_velocity_color(evt.velocity);
                } else {
                     // Set TARGET to Black (OFF)
                     memset(&target_pixels[led_idx], 0, sizeof(struct led_rgb));
                }
            }
        }
        
        // B. Update Phase (Smoothing / Physics)
        bool needs_update = false;
        
        // Only run physics if not in "Dim Mode"
        if (!led_is_off) {
            float smooth_factor = 0.25f; // Adjust for speed (0.1=Slow, 0.5=Fast)
            
            for (int i=0; i<SUB_STRIP_NUM_PIXELS; i++) {
                struct led_rgb *curr = &pixels[i];
                struct led_rgb *targ = &target_pixels[i];
                
                // Interpolate R, G, B
                uint8_t new_r = lerp_uint8(curr->r, targ->r, smooth_factor);
                uint8_t new_g = lerp_uint8(curr->g, targ->g, smooth_factor);
                uint8_t new_b = lerp_uint8(curr->b, targ->b, smooth_factor);
                
                // Check if changed
                if (new_r != curr->r || new_g != curr->g || new_b != curr->b) {
                    curr->r = new_r;
                    curr->g = new_g;
                    curr->b = new_b;
                    needs_update = true;
                }
            }
            
            // C. Render Phase
            if (needs_update) {
                led_strip_update_rgb(strip, pixels, SUB_STRIP_NUM_PIXELS);
            }
        }
        
        // D. Housekeeping
        // Feed Watchdog (Every 1s at least) - Logic simplified for loop
        if (wdt) {
             wdt_feed(wdt, wdt_chan_led);
        }
        
        // Check Dim Mode (turn off LEDs if idle for 1 minute)
        if (!led_is_off && (k_uptime_get() - last_activity_time > DIM_TIMEOUT_MS)) {
             printk("[POWER] Auto-Dim: Turning off LEDs\n");
             memset(pixels, 0, sizeof(pixels));
             memset(target_pixels, 0, sizeof(target_pixels)); // Ensure target also off
             led_strip_update_rgb(strip, pixels, SUB_STRIP_NUM_PIXELS);
             led_is_off = true;
        }
        
        // E. Frame Limiter (60 FPS = ~16ms)
        k_msleep(16);
    }
}

// #if 0
static void scan_matrix(void)
{
    static uint32_t debug_counter = 0;
    static uint32_t stuck_counter = 0;
    uint32_t current_time = k_uptime_get_32();
    
    // OPTIMIZED KEYBOARD MATRIX SCANNING WITH TWO MATRICES:
    // Both matrices share the same 4 columns but have separate row sets
    // Matrix 1 (rows 1-6): First contact detection
    // Matrix 2 (rows a-f): Second contact detection
    
    for (int row = 0; row < NUM_ROWS; row++) {
        // ===== SCAN MATRIX 1 (First Contact) =====
        // Set current Matrix 1 row LOW (active scan)
        gpio_pin_set_dt(&matrix1_rows[row], 0);
        k_busy_wait(100);  // Increased to 100us for better signal settling

        // Debug: Show we're scanning (every 1000 scans = ~5 seconds)
        // if (debug_counter % 1000 == 0 && row == 0) {
        //    printk("[SCAN] Scanning active (Row set LOW, should read columns)\n");
        // }
        
        // Read all columns for Matrix 1
        for (int col = 0; col < NUM_COLS; col++) {
            int key_idx = row * NUM_COLS + col;
            key_state_t *key = &keys[key_idx];
            // uint8_t midi_note = BASE_MIDI_NOTE + key_idx; // Removed unused var
            
            // Read column: LOW = key pressed (pulled down through switch and diode to LOW row)
            int col_state = gpio_pin_get_dt(&cols[col]);
            bool m1_pressed = (col_state == 0);  // LOW = pressed
            
            // Activity Detected?
            if (m1_pressed) {
               last_activity_time = k_uptime_get();
            }

            // ===== Handle            // First contact made
            if (m1_pressed && !key->matrix1_active) {
                key->matrix1_active = true;
                key->matrix1_time = current_time;
                key->m1_latch_timer = current_time; // Start latch timer
                // printk dropped for performance
            } else if (!m1_pressed && key->matrix1_active) {
                // First contact released - SMART DEBOUNCE
                // CASE A: Note NOT playing yet? We are in the "Press" phase.
                //    - User might be pressing slowly, or switch is bouncing.
                //    - We MUST hold M1 active for a long window (250ms) to wait for M2.
                //    - This preserves the "Start Time" so we can get TRUE VELOCITY.
                // CASE B: Note IS playing? We are in the "Release" phase.
                //    - User is letting go. We want snappy release.
                //    - Use short debounce (50ms) just to filter noise.
                
                uint32_t hold_time = key->note_playing ? 50 : 250;

                if (k_uptime_get_32() - key->m1_latch_timer > hold_time && !key->matrix2_active) {
                     key->matrix1_active = false;
                }
            }
        }
        
        // Set Matrix 1 row back to HIGH
        gpio_pin_set_dt(&matrix1_rows[row], 1);
        
        // Add settling time between Matrix 1 and Matrix 2 scans to prevent interference
        k_busy_wait(50); // Safe guard delay between matrix scans
        
        // ===== SCAN MATRIX 2 (Second Contact) =====
        // Set current Matrix 2 row LOW (active scan)
        gpio_pin_set_dt(&matrix2_rows[row], 0);
        k_busy_wait(100);  // Increased to 100us for better signal settling
        
        // Read all columns for Matrix 2
        for (int col = 0; col < NUM_COLS; col++) {
            int key_idx = row * NUM_COLS + col;
            key_state_t *key = &keys[key_idx];
            uint8_t midi_note = BASE_MIDI_NOTE + key_idx;
            
            // Read column: LOW = key pressed (pulled down through switch and diode to LOW row)
            int col_state = gpio_pin_get_dt(&cols[col]);
            bool m2_pressed = (col_state == 0);  // LOW = pressed
            
            // ===== Handle Matrix 2 (Second Contact) =====
            if (m2_pressed && !key->matrix2_active) {
                // Second contact made
                key->matrix2_active = true;
                key->matrix2_time = current_time;
                key->m2_latch_timer = current_time; // Start latch
                
                printk("[M2] Key[R%d,C%d]: Matrix 2 SECOND contact detected (Note %d)\n",  
                       row + 1, col + 1, midi_note);
                
                // Calculate velocity and send Note ON
                if (key->matrix1_active && !key->note_playing) {
                    uint32_t time_diff = key->matrix2_time - key->matrix1_time;
                    key->velocity = calculate_velocity(time_diff);
                    // Send MIDI Note ON
                    // Re-calculate note with current transpose (in case it changed mid-press)
                    uint8_t midi_note = BASE_MIDI_NOTE + key_idx + g_transpose;
                    
                    uint8_t midi_packet[5];
                    int len = midi_ble_note_on(midi_note, key->velocity, MIDI_CHANNEL, 
                                              midi_packet, sizeof(midi_packet));
                    if (len > 0) {
                        ble_midi_send(midi_packet, len);
                    }

                    key->note_playing = true;
                    
                    // Send Event to LED Thread
                    struct led_event e = {
                        .key_index = key_idx,
                        .velocity = key->velocity,
                        .is_on = true
                    };
                    k_msgq_put(&led_msgq, &e, K_NO_WAIT);
                    
                } else if (!key->matrix1_active) {
                    printk("[WARN] Key[R%d,C%d]: M2 contact but M1 not active!\n", 
                           row + 1, col + 1);
                }
            } else if (!m2_pressed && key->matrix2_active) {
                // Second contact released - DEBOUNCE
                if (k_uptime_get_32() - key->m2_latch_timer > 50) {
                     key->matrix2_active = false;
                }
            }
        }
        
        // Set Matrix 2 row back to HIGH
        gpio_pin_set_dt(&matrix2_rows[row], 1);
    }
    
    // ===== Handle Note OFF for ALL keys after scanning =====
    // Check all keys to see if both contacts are released
    for (int i = 0; i < NUM_KEYS; i++) {
        key_state_t *key = &keys[i];
        if (!key->matrix1_active && !key->matrix2_active && key->note_playing) {
            // Both contacts released, send Note OFF
            uint8_t midi_note = BASE_MIDI_NOTE + i + g_transpose;
            uint8_t midi_packet[5];
            int len = midi_ble_note_off(midi_note, 0, MIDI_CHANNEL, 
                                       midi_packet, sizeof(midi_packet));
            if (len > 0) {
                ble_midi_send(midi_packet, len);
            }
            
            key->note_playing = false;
            int row = i / NUM_COLS;
            int col = i % NUM_COLS;
            
            // Send Event to LED Thread
            struct led_event e = {
                .key_index = i,
                .velocity = 0,
                .is_on = false
            };
            k_msgq_put(&led_msgq, &e, K_NO_WAIT);

            printk("[NOTE OFF] Key[R%d,C%d]\n", row + 1, col + 1);
        }
    }
    
    // Debug output every 200 scans (~1 time per second at 200Hz)
    if (debug_counter++ % 200 == 0) {

        
        int active_keys = 0;
        for (int i = 0; i < NUM_KEYS; i++) {
            if (keys[i].note_playing) {
                active_keys++;
            }
        }
        if (active_keys > 0) {
            stuck_counter++;
            printk("\n[DEBUG] %d keys stuck! (Count: %u) Details:\n", active_keys, stuck_counter);
            for (int i = 0; i < NUM_KEYS; i++) {
                if (keys[i].note_playing) {
                    int r = i / NUM_COLS;
                    int c = i % NUM_COLS;
                    printk("   Key[R%d,C%d] Note %d: M1=%d M2=%d Playing=%d\n",
                           r + 1, c + 1, BASE_MIDI_NOTE + i,
                           keys[i].matrix1_active, keys[i].matrix2_active, 
                           keys[i].note_playing);
                }
            }
            
            // Auto-reset if stuck for more than 5 seconds (10 consecutive checks)
            if (stuck_counter > 10) {
                printk("[WARN] Keys stuck for >5 seconds, forcing reset...\n");
                force_reset_all_keys();
                stuck_counter = 0;
            }
        } else {
            stuck_counter = 0;  // Reset stuck counter when all keys are OK
            if (debug_counter % 1000 == 0) {
                printk("[DEBUG] All keys OFF (OK), Time: %u ms\n", current_time);
                
                // Verify columns are HIGH (check all columns)
                int c1 = gpio_pin_get_dt(&cols[0]);
                int c2 = gpio_pin_get_dt(&cols[1]);
                int c3 = gpio_pin_get_dt(&cols[2]);
                int c4 = gpio_pin_get_dt(&cols[3]);
                printk("   Columns check: C1=%s C2=%s C3=%s C4=%s (should all be HIGH)\n",
                       c1 ? "HIGH" : "LOW", c2 ? "HIGH" : "LOW",
                       c3 ? "HIGH" : "LOW", c4 ? "HIGH" : "LOW");
            }
        }
    }
}





void scan_thread_entry(void *p1, void *p2, void *p3) {
    printk("[RTOS] Scan Thread Started\n");
    last_activity_time = k_uptime_get(); // Init timer
    
    int loop_count = 0;
    while (1) {
        scan_matrix();
        
        // Power Management Check
        int64_t now = k_uptime_get();
        if ((now - last_activity_time) > SLEEP_TIMEOUT_MS) {
            enter_deep_sleep();
        }

        k_usleep(100); // Sleep 100us (0.1ms) to release CPU to lower priority threads (LEDs)

        // Feed Watchdog every 1000 loops (~1 sec)
        if (++loop_count >= 1000) {
            if (wdt) wdt_feed(wdt, wdt_chan_scan);
            loop_count = 0;
        }
    }
}
// #endif

// ========== LED TEST PATTERN ==========
#if 0
static void test_led_pattern(void)
{
    printk("[TEST] Running LED startup sequence (R -> G -> B)...\n");

    struct led_rgb color_red = { .r = 20, .g = 0, .b = 0 };
    struct led_rgb color_green = { .r = 0, .g = 20, .b = 0 };
    struct led_rgb color_blue = { .r = 0, .g = 0, .b = 20 };
    
    // RED
    for (int i = 0; i < SUB_STRIP_NUM_PIXELS; i++) {
        memcpy(&pixels[i], &color_red, sizeof(struct led_rgb));
    }
    led_strip_update_rgb(strip, pixels, SUB_STRIP_NUM_PIXELS);
    k_msleep(500);

    // GREEN
    for (int i = 0; i < SUB_STRIP_NUM_PIXELS; i++) {
        memcpy(&pixels[i], &color_green, sizeof(struct led_rgb));
    }
    led_strip_update_rgb(strip, pixels, SUB_STRIP_NUM_PIXELS);
    k_msleep(500);

    // BLUE
    for (int i = 0; i < SUB_STRIP_NUM_PIXELS; i++) {
        memcpy(&pixels[i], &color_blue, sizeof(struct led_rgb));
    }
    led_strip_update_rgb(strip, pixels, SUB_STRIP_NUM_PIXELS);
    k_msleep(500);

    // OFF
    memset(pixels, 0, sizeof(pixels));
    led_strip_update_rgb(strip, pixels, SUB_STRIP_NUM_PIXELS);
    printk("[TEST] LED sequence complete\n");
}
#endif
// #endif
// #endif

// #endif

// ========== MAIN FUNCTION ==========
int main(void)
{
    int ret;

    printk("\n");
    printk("==============================================\n");
    printk("   Superr Velocity MIDI Keyboard v3.0\n");
    printk("   6x4 Diode-Protected Dual Matrix\n");
    printk("   24 Velocity-Sensitive Keys\n");
    printk("   BLE MIDI Controller with Test Pin\n");
    printk("==============================================\n");
    printk("\n");
    
    // Initialize all key states to zero
    memset(keys, 0, sizeof(keys));

    // ========== Configure BLE Status LED ==========
    if (!gpio_is_ready_dt(&ble_status_led)) {
        printk("[ERROR] BLE Status LED device not ready\n");
        return 0;
    }
    ret = gpio_pin_configure_dt(&ble_status_led, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        printk("[ERROR] Failed to configure BLE Status LED\n");
        return 0;
    }
    printk("[OK] BLE Status LED configured\n");
    
    // ========== Initialize GPIO Pins ==========
    ret = init_gpio();
    if (ret) {
        printk("\n[ERROR] GPIO initialization failed!\n");
        return 0;
    }

    // ========== Initialize LED Strip ==========
    if (device_is_ready(strip)) {
        printk("[OK] Found LED strip device %s\n", strip->name);
        memset(pixels, 0, sizeof(pixels)); // Force clear all LEDs
        led_strip_update_rgb(strip, pixels, SUB_STRIP_NUM_PIXELS);
        printk("[OK] Cleared LED strip to OFF\n");
    } else {
        printk("[ERROR] LED strip device not ready!\n");
    }

    // ========== Run LED Test Pattern ==========
    /*
    if (device_is_ready(strip)) {
        test_led_pattern();
    }
    */

    // ========== Initialize BLE MIDI ==========
    printk("\n[BLE] Initializing BLE MIDI...\n");
    ret = ble_midi_init(&ble_status_led);
    if (ret) {
        printk("[ERROR] BLE MIDI initialization failed (err %d)\n", ret);
        return 0;
    }
    
    // ========== Initialize Config Service ==========
    ret = ble_config_init();
    if (ret) {
        printk("[WARN] BLE Config initialization failed\n");
    }

    // ========== Initialize Watchdog ==========
    wdt = DEVICE_DT_GET(DT_ALIAS(watchdog0));
    if (!device_is_ready(wdt)) {
        printk("[CRITICAL] Watchdog not ready! System unsafe.\n");
        return 0; // Don't run without safety
    }

    struct wdt_timeout_cfg wdt_config = {
        .window.min = 0,
        .window.max = 5000, // 5 seconds max before reset
        .callback = NULL,   // No callback, just reset
        .flags = WDT_FLAG_RESET_SOC,
    };

    wdt_chan_scan = wdt_install_timeout(wdt, &wdt_config);
    wdt_chan_led = wdt_install_timeout(wdt, &wdt_config);
    
    if (wdt_chan_scan < 0 || wdt_chan_led < 0) {
        printk("[ERROR] Failed to install WDT timeouts\n");
        return 0;
    }

    ret = wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG);
    if (ret < 0) {
        printk("[ERROR] WDT setup failed\n");
        return 0;
    }
    printk("[OK] Watchdog Armed! (5s timeout)\n");

    printk("\n");
    printk("==============================================\n");
    printk("   SYSTEM READY - 24 KEYS + TEST PIN\n");
    printk("==============================================\n");
    printk("   Hardware: 17 GPIO Pins (P1 port)\n");
    printk("   - 4 Columns (P1.00-P1.03) -> INPUT\n");
    printk("   - 6 Matrix 1 Rows (P1.04-P1.09) -> OUT\n");
    printk("   - 6 Matrix 2 Rows (P1.10-P1.15) -> OUT\n");
    printk("   - 1 Test Pin (P0.27) -> 3.3V Detect\n");
    printk("\n");
    printk("   Matrix Configuration:\n");
    printk("   - 24 velocity-sensitive keys (6x4)\n");
    printk("   - Diode-protected matrix\n");
    printk("   - Standard keyboard matrix logic\n");
    printk("   - No conflicts with board features\n");
    printk("\n");
    printk("   MIDI Configuration:\n");
    printk("   - Notes: %d - %d\n", 
           BASE_MIDI_NOTE, BASE_MIDI_NOTE + NUM_KEYS - 1);
    printk("   - Velocity: %d-%d (dynamic)\n", 
           MIN_VELOCITY, MAX_VELOCITY);
    printk("   - Channel: %d\n", MIDI_CHANNEL + 1);
    printk("==============================================\n");
    printk("\n");
    printk("[READY] Ready to play!\n");
    printk("[INFO] HARDWARE: Column -> Switch -> Diode -> Row\n");
    printk("   Columns (P1.00-03) INPUT with PULL-UP -> default HIGH\n");
    printk("   Rows (P1.04-15) OUTPUT -> scan by setting LOW\n");
    printk("   When key pressed -> Column reads LOW\n");
    printk("[TEST] Touch P0.27 to 3.3V to test connectivity\n");
    printk("[SCAN] Scanning 24 keys for velocity sensitivity\n\n");
    
    // ========== Start RTOS Threads ==========
    k_thread_create(&scan_thread_data, scan_stack,
                    K_THREAD_STACK_SIZEOF(scan_stack),
                    scan_thread_entry, NULL, NULL, NULL,
                    SCAN_PRIORITY, 0, K_NO_WAIT);
                    
    k_thread_create(&led_thread_data, led_stack,
                    K_THREAD_STACK_SIZEOF(led_stack),
                    led_thread_entry, NULL, NULL, NULL,
                    LED_PRIORITY, 0, K_NO_WAIT);



    // Main thread becomes idle or handles BLE management
    while (1) {
        k_msleep(10000);
    }

    return 0;
}

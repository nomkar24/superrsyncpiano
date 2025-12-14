#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include "ble_midi_service.h"
#include "midi_ble.h"
#include <zephyr/drivers/led_strip.h>

// ========== 24-KEY CONFIGURATION ==========
#define NUM_COLS 4    // 4 columns (all active)
#define NUM_ROWS 6    // 6 rows per matrix
#define NUM_KEYS (NUM_COLS * NUM_ROWS)  // 24 keys total

// ========== GPIO PIN ASSIGNMENTS (17 pins) ==========
// STANDARD KEYBOARD MATRIX LOGIC:
// HARDWARE: Diodes with cathode at switch, anode at row
// Current flow when key pressed: Column (pull-up HIGH) → Switch → Diode → Row (scanning LOW)
// LOGIC: Rows OUTPUT (default HIGH, scan LOW), Columns INPUT (pull-up, read LOW when pressed)

// COLUMNS (INPUT with PULL-UP) - Use P1 (GPIO1) pins to avoid conflicts
#define COL1_PIN  0   // P1.00
#define COL2_PIN  1   // P1.01
#define COL3_PIN  2   // P1.02
#define COL4_PIN  3   // P1.03

// MATRIX 1 ROWS (OUTPUT) - Use P1 pins
#define M1_ROW1_PIN  4   // P1.04
#define M1_ROW2_PIN  5   // P1.05
#define M1_ROW3_PIN  6   // P1.06
#define M1_ROW4_PIN  7   // P1.07
#define M1_ROW5_PIN  8   // P1.08
#define M1_ROW6_PIN  9   // P1.09

// MATRIX 2 ROWS (OUTPUT) - Use P1 pins
#define M2_ROWa_PIN  10  // P1.10
#define M2_ROWb_PIN  11  // P1.11
#define M2_ROWc_PIN  12  // P1.12
#define M2_ROWd_PIN  13  // P1.13
#define M2_ROWe_PIN  14  // P1.14
#define M2_ROWf_PIN  15  // P1.15

// TEST PIN - Use free P0 pin
#define TEST_PIN  4  // P0.04 (moved from P0.27)

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
} key_state_t;

// ========== GLOBAL VARIABLES ==========
static struct gpio_dt_spec cols[NUM_COLS];           // 4 columns
static struct gpio_dt_spec matrix1_rows[NUM_ROWS];   // 6 rows for Matrix 1
static struct gpio_dt_spec matrix2_rows[NUM_ROWS];   // 6 rows for Matrix 2
static struct gpio_dt_spec test_pin;                 // Test pin for 3.3V detection
static struct gpio_dt_spec ble_status_led = GPIO_DT_SPEC_GET(DT_ALIAS(ble_status_led), gpios);
static key_state_t keys[NUM_KEYS];                   // 24 keys

// ========== LED STRIP CONFIGURATION ==========
#define STRIP_NODE DT_ALIAS(led_strip)
#define SUB_STRIP_NUM_PIXELS 24

static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);
static struct led_rgb pixels[SUB_STRIP_NUM_PIXELS];

// LED Colors (R, G, B) - scaled down for brightness safety
static const struct led_rgb color_on = { .r = 0, .g = 10, .b = 30 }; // Cyan/Blueish
static const struct led_rgb color_off = { .r = 0, .g = 0, .b = 0 };  // Off

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
        cols[i].port = gpio1;  // Using GPIO1 port
        cols[i].pin = col_pins[i];
        cols[i].dt_flags = GPIO_ACTIVE_HIGH;
        
        ret = gpio_pin_configure(gpio1, col_pins[i], GPIO_INPUT | GPIO_PULL_UP);
        if (ret < 0) {
            printk("[ERROR] Failed to configure Column %d (P1.%02d)\n", i + 1, col_pins[i]);
            return ret;
        }
        printk("[OK] Column %d: P1.%02d (INPUT with pull-up, default HIGH)\n", i + 1, col_pins[i]);
    }
    
    // ===== Initialize TEST PIN (for 3.3V detection) =====
    printk("\n[TEST PIN] For 3.3V detection:\n");
    test_pin.port = gpio0;
    test_pin.pin = TEST_PIN;
    test_pin.dt_flags = GPIO_ACTIVE_HIGH;
    
    ret = gpio_pin_configure(gpio0, TEST_PIN, GPIO_INPUT | GPIO_PULL_DOWN);
    if (ret < 0) {
        printk("[ERROR] Failed to configure Test Pin (P0.%02d)\n", TEST_PIN);
        return ret;
    }
    printk("[OK] Test Pin: P0.%02d (INPUT with pull-down)\n", TEST_PIN);
    printk("     Touch to 3.3V to test - should show HIGH\n");
    
    // ===== Initialize MATRIX 1 row pins (OUTPUT - Drive for scanning) =====
    printk("\n[MATRIX 1 ROWS] OUTPUT - Scanned LOW one at a time:\n");
    const uint8_t m1_row_pins[] = {M1_ROW1_PIN, M1_ROW2_PIN, M1_ROW3_PIN, 
                                     M1_ROW4_PIN, M1_ROW5_PIN, M1_ROW6_PIN};
    for (int i = 0; i < NUM_ROWS; i++) {
        matrix1_rows[i].port = gpio1;  // Using GPIO1 port
        matrix1_rows[i].pin = m1_row_pins[i];
        matrix1_rows[i].dt_flags = GPIO_ACTIVE_HIGH;
        
        ret = gpio_pin_configure(gpio1, m1_row_pins[i], GPIO_OUTPUT);
        if (ret < 0) {
            printk("[ERROR] Failed to configure Matrix 1 Row %d (P1.%02d)\n", i + 1, m1_row_pins[i]);
            return ret;
        }
        // Set row HIGH by default (not scanning)
        gpio_pin_set_dt(&matrix1_rows[i], 1);
        printk("[OK] Matrix 1, Row %d: P1.%02d (OUTPUT -> set HIGH)\n", i + 1, m1_row_pins[i]);
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
        printk("     Col %d P1.%02d: %s\n", i + 1, cols[i].pin, 
               col_state ? "HIGH [OK]" : "LOW [ERROR]");
    }
    
    printk("   Rows (should be HIGH when no key pressed):\n");
    for (int i = 0; i < NUM_ROWS; i++) {
        int m1_state = gpio_pin_get_dt(&matrix1_rows[i]);
        int m2_state = gpio_pin_get_dt(&matrix2_rows[i]);
        printk("     Row %d: M1=P1.%02d %s, M2=P1.%02d %s\n", 
               i + 1, 
               matrix1_rows[i].pin, m1_state ? "HIGH [OK]" : "LOW [ERROR]",
               matrix2_rows[i].pin, m2_state ? "HIGH [OK]" : "LOW [ERROR]");
    }
    printk("\n");
    
    // ===== GPIO TEST: Blink Row 1 to verify GPIO is working =====
    printk("[TEST] Blinking Matrix 1 Row 1 (P1.04) 5 times...\n");
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

// ========== VELOCITY CALCULATION ==========
static uint8_t calculate_velocity(uint32_t time_diff_ms)
{
    if (time_diff_ms == 0) {
        return MAX_VELOCITY;  // Instantaneous = maximum velocity
    }
    
    if (time_diff_ms > MAX_VELOCITY_TIME_MS) {
        return MIN_VELOCITY;  // Too slow = minimum velocity
    }
    
    // Linear mapping: faster press = higher velocity
    // velocity = MAX - (time_diff * range / MAX_TIME)
    uint8_t velocity = MAX_VELOCITY - 
        ((time_diff_ms * (MAX_VELOCITY - MIN_VELOCITY)) / MAX_VELOCITY_TIME_MS);
    
    // Clamp to valid MIDI velocity range
    if (velocity < MIN_VELOCITY) velocity = MIN_VELOCITY;
    if (velocity > MAX_VELOCITY) velocity = MAX_VELOCITY;
    
    return velocity;
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
#if 0
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
        k_busy_wait(100);  // Increased delay to make scanning visible with multimeter
        
        // Debug: Show we're scanning (every 1000 scans = ~5 seconds)
        if (debug_counter % 1000 == 0 && row == 0) {
            printk("[SCAN] Scanning active (Row set LOW, should read columns)\n");
        }
        
        // Read all columns for Matrix 1
        for (int col = 0; col < NUM_COLS; col++) {
            int key_idx = row * NUM_COLS + col;
            key_state_t *key = &keys[key_idx];
            uint8_t midi_note = BASE_MIDI_NOTE + key_idx;
            
            // Read column: LOW = key pressed (pulled down through switch and diode to LOW row)
            int col_state = gpio_pin_get_dt(&cols[col]);
            bool m1_pressed = (col_state == 0);  // LOW = pressed
            
            // ===== Handle Matrix 1 (First Contact) =====
            if (m1_pressed && !key->matrix1_active) {
                // First contact made
                key->matrix1_active = true;
                key->matrix1_time = current_time;
                printk("\n[M1] Key[R%d,C%d]: Matrix 1 FIRST contact (Note %d)\n", 
                       row + 1, col + 1, midi_note);
            } else if (!m1_pressed && key->matrix1_active) {
                // First contact released
                key->matrix1_active = false;
            }
        }
        
        // Set Matrix 1 row back to HIGH
        gpio_pin_set_dt(&matrix1_rows[row], 1);
        
        // Add settling time between Matrix 1 and Matrix 2 scans to prevent interference
        k_busy_wait(50);
        
        // ===== SCAN MATRIX 2 (Second Contact) =====
        // Set current Matrix 2 row LOW (active scan)
        gpio_pin_set_dt(&matrix2_rows[row], 0);
        k_busy_wait(150);  // Longer delay for Matrix 2 to ensure stable reading
        
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
                
                printk("[M2] Key[R%d,C%d]: Matrix 2 SECOND contact detected (Note %d)\n", 
                       row + 1, col + 1, midi_note);
                
                // Calculate velocity and send Note ON
                if (key->matrix1_active && !key->note_playing) {
                    uint32_t time_diff = key->matrix2_time - key->matrix1_time;
                    key->velocity = calculate_velocity(time_diff);
                    
                    // Send MIDI Note ON
                    uint8_t midi_packet[5];
                    int len = midi_ble_note_on(midi_note, key->velocity, MIDI_CHANNEL,
                                              midi_packet, sizeof(midi_packet));
                    if (len > 0) {
                        ble_midi_send(midi_packet, len);
                    }
                    
                    key->note_playing = true;
                    
                    printk("[NOTE ON] Key[R%d,C%d]: Note %d, Velocity %d (time=%ums)\n",
                           row + 1, col + 1, midi_note, key->velocity, time_diff);

                    // LED ON
                    if (key_idx < SUB_STRIP_NUM_PIXELS) {
                        memcpy(&pixels[key_idx], &color_on, sizeof(struct led_rgb));
                        led_strip_update_rgb(strip, pixels, SUB_STRIP_NUM_PIXELS);
                    }
                } else if (!key->matrix1_active) {
                    printk("[WARN] Key[R%d,C%d]: M2 contact but M1 not active!\n", 
                           row + 1, col + 1);
                }
            } else if (!m2_pressed && key->matrix2_active) {
                // Second contact released
                key->matrix2_active = false;
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
            uint8_t midi_note = BASE_MIDI_NOTE + i;
            uint8_t midi_packet[5];
            int len = midi_ble_note_off(midi_note, 0, MIDI_CHANNEL,
                                       midi_packet, sizeof(midi_packet));
            if (len > 0) {
                ble_midi_send(midi_packet, len);
            }
            
            key->note_playing = false;
            int row = i / NUM_COLS;
            int col = i % NUM_COLS;

            // LED OFF
            if (i < SUB_STRIP_NUM_PIXELS) {
                memcpy(&pixels[i], &color_off, sizeof(struct led_rgb));
                led_strip_update_rgb(strip, pixels, SUB_STRIP_NUM_PIXELS);
            }

            printk("[NOTE OFF] Key[R%d,C%d]: Note %d\n", row + 1, col + 1, midi_note);
        }
    }
    
    // Debug output every 200 scans (~1 time per second at 200Hz)
    if (debug_counter++ % 200 == 0) {
        // Check test pin status
        static int last_test_state = -1;
        int test_state = gpio_pin_get_dt(&test_pin);
        if (test_state != last_test_state) {
            if (test_state) {
                printk("[TEST PIN] P0.28: HIGH [OK] (Connected to 3.3V)\n");
            } else {
                printk("[TEST PIN] P0.28: LOW (Pull-down active)\n");
            }
            last_test_state = test_state;
        }
        
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

// ========== LED TEST PATTERN ==========
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
        // memset(pixels, 0, sizeof(pixels)); // Clear all LEDs
        // led_strip_update_rgb(strip, pixels, SUB_STRIP_NUM_PIXELS);
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
    
    // ========== Main Scanning Loop ==========
    while (1) {
        for (int pos = 0; pos < SUB_STRIP_NUM_PIXELS; pos++) {
            memset(pixels, 0, sizeof(pixels));
            pixels[pos].g = 255;   // bright green dot
            led_strip_update_rgb(strip, pixels, SUB_STRIP_NUM_PIXELS);
            k_sleep(K_MSEC(100));
        }
    }

    return 0;
}

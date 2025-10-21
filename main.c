#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

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

    printk("Dual Switch LED Demo Started (Matrix Scanning Mode)\n");
    printk("SW1: P0.23, SW2: P0.24 (drive) -> P0.25 (sense), LED: P0.28\n");

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

    printk("Setup complete! Monitoring switches...\n");
    printk("Press SW1 or SW2 to control LED and see serial output\n");
    printk("Matrix Scanning: P0.24 drives HIGH, P0.25 senses (ACTIVE_HIGH with PULL_DOWN)\n");

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
            if (sw1_state == 0) {
                printk("SW1: PRESSED (ON)\n");
            } else {
                printk("SW1: RELEASED (OFF)\n");
            }
            prev_sw1_state = sw1_state;
        }

        // Check for SW2 state change
        if (sw2_state != prev_sw2_state) {
            if (sw2_state == 1) {
                printk("SW2: PRESSED (ON) - P0.24 driving P0.25 HIGH through switch\n");
            } else {
                printk("SW2: RELEASED (OFF) - P0.25 pulled down (no connection)\n");
            }
            prev_sw2_state = sw2_state;
        }

        // Debug output for SW2 pin states
        static int debug_counter = 0;
        if (debug_counter++ % 100 == 0) { // Print every 1 second
            printk("SW2 Matrix State - Drive (P0.24): HIGH, Sense (P0.25): %d, Switch: %s\n", 
                   sw2_state, sw2_state ? "PRESSED" : "RELEASED");
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
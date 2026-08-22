#include <stdint.h>

// Direct volatile access to PRU Output Register (R30)
volatile register uint32_t __R30 __asm__("r30");

// P9_29 in Mode 5 maps directly to PRU0 output bit 1 (r30_1)
#define SERVO_PIN_BIT (1U << 1)

// Timing definitions for 200MHz PRU Clock (1 cycle = 5ns)
// 200 cycles = 1 microsecond (1us)
#define DELAY_10US() __delay_cycles(2000)

static void busy_wait_us(uint32_t us)
{
    // 200 cycles per microsecond at 200MHz
    while (us--) {
        __delay_cycles(200);
    }
}

static void send_pulse(uint32_t width_us)
{
    // Pin HIGH
    __R30 |= SERVO_PIN_BIT;
    busy_wait_us(width_us);

    // Pin LOW
    __R30 &= ~SERVO_PIN_BIT;
    busy_wait_us(20000u - width_us);
}

int main(void)
{
    uint32_t width;

    // Set pin low initially
    __R30 &= ~SERVO_PIN_BIT;

    while (1) {
        // Continuous sweep between 1000us (0 deg) and 2000us (180 deg)
        for (width = 1000; width <= 2000; width += 10) {
            send_pulse(width);
        }
        for (width = 2000; width >= 1000; width -= 10) {
            send_pulse(width);
        }
    }
    return 0;
}
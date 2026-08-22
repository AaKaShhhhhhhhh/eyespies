#include <stdint.h>

volatile register uint32_t __R30 __asm__("r30");

#define SERVO_BIT_PIN (1U << 1)  /* P9_29 on PRU1 */
#define PERIOD_US     20000u      /* 20 ms -> 50 Hz */

static void busy_wait_us(uint32_t us)
{
    /* 200 cycles per microsecond at 200MHz PRU core clock */
    while (us--) {
        __delay_cycles(200);
    }
}

static void send_pulse(uint32_t width_us)
{
    __R30 |= SERVO_BIT_PIN;             /* Pin HIGH */
    busy_wait_us(width_us);
    __R30 &= ~SERVO_BIT_PIN;            /* Pin LOW */
    busy_wait_us(PERIOD_US - width_us);
}

int main(void)
{
    uint32_t width;

    while (1) {
        /* Smooth sweep 1000us (0 deg) -> 2000us (180 deg) */
        for (width = 1000; width <= 2000; width += 10)
            send_pulse(width);
        for (width = 2000; width >= 1000; width -= 10)
            send_pulse(width);
    }
    return 0;
}
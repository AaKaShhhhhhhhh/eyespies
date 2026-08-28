#include <stdint.h>

/* Direct assembly binding for PRU R30 Output Register */
register uint32_t __R30 __asm__("r30");

#define SERVO_BIT (1U << 1) /* P9_29 on PRU1 */
#define PERIOD_US 20000u    /* 20 ms / 50 Hz */

static inline void delay_us(uint32_t us)
{
    while (us--) {
        __delay_cycles(200); /* 200 cycles = 1 us at 200MHz */
    }
}

static void send_pulse(uint32_t width_us)
{
    __R30 |= SERVO_BIT;
    delay_us(width_us);
    __R30 &= ~SERVO_BIT;
    delay_us(PERIOD_US - width_us);
}

int main(void)
{
    uint32_t width;

    /* CRITICAL FIX (board #31): clear STANDBY_INIT so r30 drives the pad.
       While STANDBY_INIT (bit 0 of PRU CFG SYSCFG @ local 0x22004) is set, the
       PRU tri-states its GPO -- P9_29 floats and only moves when you touch the
       bare wire. ARM CANNOT clear this (proven 2026-08-26), but the PRU can and
       must. The earlier "read-only / r30 is live" note was wrong (ARM-only). */
    (*(volatile uint32_t *)0x22004) &= ~(1u << 0);

    while (1) {
        /* Continuous sweep 1ms (-90 deg) -> 2ms (+90 deg) */
        for (width = 1000; width <= 2000; width += 10) {
            send_pulse(width);
        }
        for (width = 2000; width >= 1000; width -= 10) {
            send_pulse(width);
        }
    }
    return 0;
}
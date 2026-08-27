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

    /* NOTE: STANDBY_INIT (CFG SYSCFG bit 0 @ 0x26004) is a READ-ONLY status bit
       (PRCM standby handshake). Proven 2026-08-26 via syscfg_probe: writing 0 OR
       1 leaves it at 0x25. It does NOT tri-state r30 -- r30 is live whenever the
       PRU runs and P9_29 is muxed to a PRU mode. The old clear line was a no-op
       and has been removed. */

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
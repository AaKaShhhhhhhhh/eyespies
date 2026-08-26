#include <stdint.h>

/* Direct assembly binding for PRU R30 Output Register */
register uint32_t __R30 __asm__("r30");

#define SERVO_BIT (1U << 1) /* P9_29 on PRU1 */
#define PERIOD_US 20000u    /* 20 ms / 50 Hz */

#define PRU_CFG_SYSCFG (*(volatile uint32_t *)0x00026004)

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

    /* Clear STANDBY_INIT (CFG SYSCFG bit 0 -- NOT bit 4, which is SUB_MWAIT)
       so the PRU's r30 outputs are NOT tri-stated and actually reach the pad.
       Leaving STANDBY_INIT set is what makes P9_29 float (servo dead until you
       touch the bare wire). */
    PRU_CFG_SYSCFG &= ~1U;

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
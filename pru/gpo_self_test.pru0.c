#include <stdint.h>

/* Direct assembly binding for PRU R30 Output Register */
register uint32_t __R30 __asm__("r30");

#define SERVO_BIT (1U << 1) /* P9_29 on PRU0 r30.1 */

static inline void delay_us(uint32_t us)
{
    while (us--) {
        __delay_cycles(200); /* 200 cycles = 1 us at 200MHz */
    }
}

/* LOOPBACK TEST: blink P9_29 (r30.1) at 5 Hz (100 ms HIGH / 100 ms LOW).
   Jumper P9_29 -> P8_13 and read P8_13 from Linux with loopback_probe.
   If P8_13 toggles -> the PRU GPO reaches the pad (mux + r30 path OK).
   If P8_13 stays flat -> r30.1 is not driving the pad. */
int main(void)
{
    (void)(*(volatile uint32_t *)0x22004); /* SYSCFG touch; no clear needed */

    while (1) {
        __R30 |=  SERVO_BIT; delay_us(100000); /* 100 ms HIGH  */
        __R30 &= ~SERVO_BIT; delay_us(100000); /* 100 ms LOW   */
    }
    return 0;
}

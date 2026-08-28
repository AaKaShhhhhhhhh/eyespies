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

    /* NOTE: PRU CFG SYSCFG at local 0x22004. At reset SYSCFG == 0x25, so
       STANDBY_INIT (bit 4) is already 0 and the GPOs (r30) drive the pad
       directly -- no PRU-side un-tri-state is needed. (An earlier "STANDBY_INIT
       bit 0 must be cleared by the PRU" theory was WRONG: bit 0 is the read-only
       IDLE_MODE status bit, and bit 4 is already 0 at reset.) The thing that
       actually made P9_29 move was (1) the U-Boot uenvcmd mux to mode 4 (0x24)
       and (2) fixing pru1_servo's shared-RAM address. This firmware just sweeps
       r30.1 with a valid 50 Hz / 1-2 ms servo PWM. */
    (void)(*(volatile uint32_t *)0x22004); /* touch to keep ref; no clear needed */

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
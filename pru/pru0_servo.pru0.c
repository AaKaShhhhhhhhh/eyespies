/*
 * pru0_servo.pru0.c
 * ==================
 * MONSTER PURPOSE:
 *   This program runs on the PRU0 co-processor (200 MHz, NO operating system,
 *   NO scheduler). Its ONLY job: generate a PERFECT, jitter-free 50 Hz servo
 *   pulse on P9_16 (tilt, wired) and P9_14 (pan, not wired yet).
 *
 *   It reads the desired angle — as a pulse width in microseconds, 500..2500
 *   (500 = 0°, 1500 = 90°, 2500 = 180°) — from a tiny block of SHARED RAM
 *   that the main Linux program (turret, or the test arm_write) writes.
 *
 *   Why this kills the buzz: gpiod makes the pulse from Linux, where a syscall
 *   + nanosleep slip by ~20µs. The PRU flips the pin with __delay_cycles()
 *   accurate to 5 ns and never talks to Linux in the hot loop. Zero jitter.
 *
 * HOW TO BUILD (on the BeagleBone, see pru/Makefile):
 *   Blink test (Phase 1/2):  pru-gcc -O2 -DBLINK_TEST -I<inc> -mmcu=am335x.pru0 -o blink.out pru0_servo.pru0.c
 *   Servo (Phase 4+):        pru-gcc -O2 -I<inc> -mmcu=am335x.pru0 -o pru0_servo.out pru0_servo.pru0.c
 *
 * <inc> is where rsc_types.h lives, e.g. /usr/share/ti/cgt-pru/include
 * (find it with:  find / -name rsc_types.h )
 */
#include <stdint.h>
#include "resource_table_empty.h"

/* ---------- HARDWARE ADDRESSES (AM335x chip on the BeagleBone Black) ----------
 * On this chip, controlling a pin means writing a number to a specific memory
 * address. GPIO bank 0 lives at 0x44E07000. Inside it:                 */
#define GPIO0_BASE  0x44E07000u
#define GPIO_OE     0x134u / 4   /* Output-Enable: writing 0 to a bit = that pin is OUTPUT */
#define GPIO_SET    0x194u / 4   /* Set-data:     writing 1 to a bit = pin goes HIGH       */
#define GPIO_CLR    0x190u / 4   /* Clear-data:   writing 1 to a bit = pin goes LOW        */

/* Pins (bit position inside the GPIO register): */
#define BIT_TILT   (1u << 19)    /* P9_16 = GPIO0.19  (tilt, WIRED)   */
#define BIT_PAN    (1u << 18)    /* P9_14 = GPIO0.18  (pan, NOT wired)*/

/* ---------- SHARED RAM: how ARM (Linux) talks to the PRU ----------
 * The PRU has 8 KB of its own RAM. It sees it starting at address 0.
 * ARM (the main Linux CPU) sees that same RAM at physical address 0x4A300000.
 * We reserve the block at offset 0x1000 (4 KB in) so we don't clash with the
 * program code. ARM writes the angle here; the PRU reads it. No lock needed
 * because a single 32-bit write is atomic on this chip.                */
#define SHM_ADDR  0x00001000u
struct cmd { volatile uint32_t tilt_us; volatile uint32_t pan_us; };
#define SHM  ((volatile struct cmd *)SHM_ADDR)

/* PRU runs at 200 MHz -> 200 clock cycles = 1 microsecond. */
#define CYCLES_PER_US  200u

/* Burn CPU cycles to wait. 190 per us leaves slack for the loop's own cost.
 * (We avoid __delay_cycles(variable) because the compiler handles a constant
 *  best; a loop of fixed delays is reliable.)                            */
static void delay_us(uint32_t us){
    while (us--) {
        __delay_cycles(190u);
    }
}

/* Clamp + one servo pulse on ONE pin:
 *   pin HIGH for 'us' microseconds, then LOW for the rest of the 20 ms frame. */
static inline void pulse_pin(uint32_t bit, uint32_t us){
    volatile uint32_t *gpio = (volatile uint32_t *)GPIO0_BASE;
    if (us < 500) us = 500;
    if (us > 2500) us = 2500;
    gpio[GPIO_SET] = bit;     /* pin -> HIGH  */
    delay_us(us);             /* width = angle */
    gpio[GPIO_CLR] = bit;     /* pin -> LOW   */
}

void main(void){
    volatile uint32_t *gpio = (volatile uint32_t *)GPIO0_BASE;

    /* 1) Make both pins OUTPUT (clear their bit in the OE register). */
    gpio[GPIO_OE] &= ~BIT_TILT;
    gpio[GPIO_OE] &= ~BIT_PAN;

    /* 2) Safe default: start centered (1500 us = 90°). */
    SHM->tilt_us = 1500u;
    SHM->pan_us  = 1500u;

#ifdef BLINK_TEST
    /* ============ PHASE 1 / 2 : BLINK (prove the PRU works) ============
     * Just toggle P9_16 on/off every 100 ms = 1 Hz blink.
     * Check it WITHOUT extra hardware: connect your tilt servo here and it
     * will click between two positions once per second. Or use a multimeter
     * (DC volts will jump between ~0 V and ~3.3 V).                 */
    while (1) {
        gpio[GPIO_SET] = BIT_TILT;
        delay_us(100000u);          /* 100 ms on  */
        gpio[GPIO_CLR] = BIT_TILT;
        delay_us(100000u);          /* 100 ms off */
    }
#else
    /* ============ PHASE 4+ : REAL SERVO LOOP ============
     * Drive BOTH servos inside one 20 ms (50 Hz) frame:
     *   - raise both pins
     *   - wait the shorter pulse, drop that pin
     *   - wait the difference, drop the other
     *   - wait out the rest of 20 ms
     * This keeps each servo at a true 50 Hz even with two of them.    */
    while (1) {
        uint32_t t = SHM->tilt_us;
        uint32_t p = SHM->pan_us;
        if (t < 500) t = 500; if (t > 2500) t = 2500;
        if (p < 500) p = 500; if (p > 2500) p = 2500;

        uint32_t hi = (t < p) ? t : p;          /* shorter high time  */
        uint32_t lo = (t < p) ? p : t;          /* longer high time   */

        gpio[GPIO_SET] = BIT_TILT | BIT_PAN;    /* both HIGH          */
        delay_us(hi);                           /* wait shorter pulse */
        gpio[GPIO_CLR] = (t < p) ? BIT_TILT : BIT_PAN;
        delay_us(lo - hi);                      /* wait the difference*/
        gpio[GPIO_CLR] = BIT_TILT | BIT_PAN;    /* both LOW           */
        delay_us(20000u - lo);                  /* finish 20 ms frame */
    }
#endif
}

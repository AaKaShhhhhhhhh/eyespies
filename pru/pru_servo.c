/*
 * pru_servo.c - Drive an MG90S servo on P9_31 using PRU0 direct GPO (__R30).
 *
 * WHY __R30 (and not OCP-to-GPIO):
 *   The PRU's OCP master cannot reach GPIO0 (0x44E07000, L4_WKUP domain) on
 *   this board/image - proven earlier: with STANDBY cleared (0x20) and GPIO0
 *   OE cleared the running firmware's SET/CLEAR writes never moved the pin.
 *   TI PRU GPIO examples always use GPIO1/2/3 (L4_PER), never GPIO0. So we use
 *   __R30 instead: a direct output of the PRU core that needs no OCP, no
 *   SYSCFG, and no STANDBY_INIT.
 *
 * PIN SETUP (verified against the official BeagleBoard PRU cape DTS,
 * AM335X-PRU-RPROC-4-19-TI-PRUCAPE-00A0.dts):
 *   P9_31 = mcasp0_aclkx = pad 0x44E10990.
 *   Cape mux: 0x190 0x05  -> PRU CAPE Blue LED  (mode 5 = PRU0 R30 output).
 *   => P9_31 is PRU0 __R30 bit 0  (confirmed: cape lists P9_31 among PRU R30
 *      outputs; P9_16/P9_29 are NOT reliable PRU R30 pins on this board).
 *
 *   The padconf write is applied at BOOT by U-Boot (mw.l 0x44E10990 0x05 in
 *   /boot/firmware/uEnv.txt). On kernel 6.x an ARM devmem2 write to the
 *   padconf is dropped, so the U-Boot mux is the only thing that sticks.
 *
 * SERVO TIMING (PRU0 core = 200 MHz, __delay_cycles(1) = 1 cycle):
 *   50 Hz period = 20 ms = 4,000,000 cycles.
 *   Pulse width 1.0 ms .. 2.0 ms = 200,000 .. 400,000 cycles (sweep).
 *
 * NOTE: __delay_cycles() only accepts a COMPILE-TIME constant, so we wrap it
 * in a runtime helper that counts fixed 1000-cycle blocks (loop overhead is
 * ~0.5%, fine for a demo sweep).
 */

#include <stdint.h>
#include "resource_table_empty.h"

/* __R30 is the PRU direct GPO register (magic name, understood by pru-gcc). */
volatile register uint32_t __R30 asm("r30");

#define P9_31_R30_BIT  (1u << 0)   /* P9_31 -> PRU0 R30_0 (mcasp0_aclkx, pad 0x44E10990) */

#define PERIOD_CYCLES  4000000u    /* 20 ms @ 200 MHz = 50 Hz */
#define PULSE_MIN      200000u     /* 1.0 ms */
#define PULSE_MAX      400000u     /* 2.0 ms */
#define PULSE_STEP     20000u      /* 10 positions across the sweep */
#define DELAY_UNIT     1000u       /* __delay_cycles() needs a constant; count blocks */

/* Runtime delay: total cycles = units * DELAY_UNIT (+ tiny loop overhead). */
static inline void delay_cycles(uint32_t cycles)
{
    uint32_t units = cycles / DELAY_UNIT;
    while (units--)
        __delay_cycles(DELAY_UNIT);
}

int main(void)
{
    __R30 &= ~P9_31_R30_BIT;   /* start the line low */

    while (1) {
        for (uint32_t pos = 0; pos <= 10; pos++) {
            uint32_t on  = PULSE_MIN + pos * PULSE_STEP;   /* high time */
            uint32_t off = PERIOD_CYCLES - on;             /* low time  */

            __R30 |=  P9_31_R30_BIT;   /* raise pulse */
            delay_cycles(on);
            __R30 &= ~P9_31_R30_BIT;   /* drop pulse */
            delay_cycles(off);
        }
    }
}

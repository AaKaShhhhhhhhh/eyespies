/*
 * pru_servo.c - Drive an MG90S servo on P9_16 using PRU0 direct GPO (__R30).
 *
 * WHY __R30 (and not OCP-to-GPIO0):
 *   The PRU's OCP master cannot reach GPIO0 (0x44E07000, L4_WKUP domain) on
 *   this board/image - proven: with STANDBY cleared (0x20) and GPIO0 OE for
 *   P9_16 cleared (0xFFF7FFFF) the running firmware's SET/CLEAR writes never
 *   moved the pin. TI PRU GPIO examples always use GPIO1/2/3 (L4_PER), never
 *   GPIO0, for exactly this reason. So we use __R30 instead: a direct output
 *   of the PRU core that needs no OCP, no SYSCFG, and no STANDBY_INIT.
 *
 * PIN SETUP (from ARM, before loading this firmware):
 *   P9_16 = conf_gpmc_be1n = pad 0x44E10984.
 *   In mode 5 the ball is "pr1_pru0_pru_r30_5" -> PRU0 __R30 bit 5.
 *   Mux at runtime (no reboot):
 *       sudo devmem2 0x44E10984 w 0x05
 *   Or make it permanent in /boot/firmware/uEnv.txt:
 *       uenvcmd=mw.l 0x44E10984 0x05
 *
 * SERVO TIMING (PRU0 core = 200 MHz, __delay_cycles(1) = 1 cycle):
 *   50 Hz period = 20 ms = 4,000,000 cycles.
 *   Pulse width 1.0 ms .. 2.0 ms = 200,000 .. 400,000 cycles (sweep).
 */

#include <stdint.h>
#include "resource_table_empty.h"

/* __R30 is the PRU direct GPO register (magic name, understood by pru-gcc). */
volatile register uint32_t __R30;

#define P9_16_R30_BIT  (1u << 5)   /* P9_16 -> PRU0 R30_5 */

#define PERIOD_CYCLES  4000000u    /* 20 ms @ 200 MHz = 50 Hz */
#define PULSE_MIN      200000u     /* 1.0 ms */
#define PULSE_MAX      400000u     /* 2.0 ms */
#define PULSE_STEP      (20000u)   /* 10 positions across the sweep */

int main(void)
{
    __R30 &= ~P9_16_R30_BIT;   /* start the line low */

    while (1) {
        for (uint32_t pos = 0; pos <= 10; pos++) {
            uint32_t on  = PULSE_MIN + pos * PULSE_STEP;   /* high time */
            uint32_t off = PERIOD_CYCLES - on;             /* low time  */

            __R30 |=  P9_16_R30_BIT;   /* raise pulse */
            __delay_cycles(on);
            __R30 &= ~P9_16_R30_BIT;   /* drop pulse */
            __delay_cycles(off);
        }
    }
}

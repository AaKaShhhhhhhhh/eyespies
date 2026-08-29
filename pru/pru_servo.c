#include <stdint.h>
#include "resource_table_empty.h"

/* ============================================================================
 *  R30 — THE ONE THING THAT CONFUSES EVERYONE (read this slowly)
 * ============================================================================
 *  - The PRU is a tiny separate processor INSIDE the AM3358 chip, next to the
 *    main Linux CPU. It has its own tiny registers, and R30 is one of them.
 *  - R30 is a 32-bit number. The PRU hardware hard-wires certain BITS of R30
 *    directly to physical pins:
 *        bit 0 of R30  ->  physical pin P9_31   (we call this PAN)
 *        bit 1 of R30  ->  physical pin P9_30   (we will call this TILT)
 *    (other bits go to other pins; we only use two right now.)
 *  - So:  "__R30 |= (1u<<0)"  means "set bit 0"  -> P9_31 goes HIGH (3.3V).
 *         "__R30 &= ~(1u<<0)" means "clear bit 0" -> P9_31 goes LOW (0V).
 *  - WHY use R30 instead of a normal Linux GPIO?
 *    Linux is a multitasking OS — it might pause your program for milliseconds
 *    to do something else, which would wreck the precise 20 ms servo pulse.
 *    The PRU runs ALONE at 200 MHz and is never interrupted, so its pulse is
 *    perfectly steady. That's the WHOLE reason we bother with the PRU.
 *
 *  Think of R30 as a light-switch panel built into the PRU. Flip bit 0 = one
 *  light (pin) turns on. Flip bit 1 = another light turns on. Nothing more.
 * ========================================================================== */
volatile register uint32_t __R30 asm("r30");   /* the PRU's direct output register */
#define P9_31_R30_BIT  (1u << 0)   /* pan  */
#define P9_30_R30_BIT  (1u << 1)   /* tilt */

#define PERIOD_US 20000u           /* 20 ms = 50 Hz */
#define DELAY_UNIT 1000u

/* ============================================================================
 *  SHARED RAM "WHITEBOARD" STRUCTURE (the contract between Linux and PRU)
 * ============================================================================
 *  This struct is the layout of the shared memory block. BOTH the Linux side
 *  (pru_comms.c) and this PRU firmware agree on this exact shape.
 *
 *  - magic : a fixed "password" the Linux side writes (0x50524F55 = "PROU").
 *            The PRU only trusts the whiteboard once it sees this password,
 *            so it knows the layout matches and Linux is alive.
 *  - pan_us  : pulse width for the PAN servo, in microseconds (500..2500).
 *  - tilt_us : pulse width for the TILT servo, in microseconds (500..2500).
 *  - seq     : a counter. Linux bumps it by 1 every time it writes new angles.
 *              The PRU remembers the last seq it saw; if seq is unchanged, no
 *              new command arrived, so it just repeats the last pulse.
 *  - flags   : spare bits for future use (e.g. a STOP flag). Unused for now.
 *
 *  WHY 'volatile': this memory is written by ANOTHER processor (Linux). The
 *  compiler must NOT cache its value in a register, so 'volatile' forces the
 *  PRU to re-read it from RAM every loop. (Without volatile, the PRU might
 *  optimise away the read and never see Linux's update.)
 * ========================================================================== */
typedef struct {
    volatile uint32_t magic, pan_us, tilt_us, seq, flags;
} pru_servo_cmd_t;

/* A tiny helper: __delay_cycles() (built into pru-gcc) only accepts a CONSTANT
   number, so we wrap it in a loop that counts fixed 1000-cycle blocks. This lets
   us delay for any runtime value. The 'static inline' just means "paste this
   code in where called" (no real function call overhead on the tiny PRU). */
static inline void delay_cycles(uint32_t cycles) {
    uint32_t units = cycles / DELAY_UNIT;
    while (units--) __delay_cycles(DELAY_UNIT);
}
/* ----------------------------------------------------------------------------
 *  pulse()  --  emit ONE servo pulse on ONE pin
 * ----------------------------------------------------------------------------
 *  A servo needs a single short HIGH pulse every 20 ms. The pulse WIDTH (how
 *  long HIGH lasts) decides the angle. So:
 *    'bit' = which pin (P9_31 or P9_30, i.e. which R30 bit)
 *    'us'  = how many microseconds to hold it HIGH (500 = one end, 2500 = other)
 *  Step-by-step inside the function:
 *    1. on  = us * 200   -> PRU runs at 200 MHz, so 1 us = 200 clock cycles.
 *                            Convert the desired pulse width into cycle count.
 *    2. off = (20000 - us) * 200  -> the rest of the 20 ms period stays LOW.
 *    3. __R30 |= bit   -> raise that pin HIGH (start the pulse).
 *    4. delay_cycles(on) -> hold HIGH for the right time.
 *    5. __R30 &= ~bit  -> drop the pin LOW (end the pulse).
 *    6. delay_cycles(off) -> wait out the rest of the 20 ms.
 *  Do this 50 times a second = 50 Hz = what the servo wants.
 * -------------------------------------------------------------------------- */
static inline void pulse(uint32_t bit, uint32_t us) {
    uint32_t on  = us * 200u;
    uint32_t off = (PERIOD_US - us) * 200u;
    __R30 |=  bit;  delay_cycles(on);
    __R30 &= ~bit;  delay_cycles(off);
}

int main(void) {
    /* Point 'cmd' at the shared whiteboard. 0x00010000 is the PRU's own view of
       the shared RAM. (On the Linux/ARM side the SAME memory is at 0x4A310000 —
       two addresses, one physical block, like two doors into one room.) */
    pru_servo_cmd_t *cmd = (pru_servo_cmd_t *)0x00010000;

    /* Start both pins LOW so no pulse is emitted before Linux is ready. */
    __R30 &= ~(P9_31_R30_BIT | P9_30_R30_BIT);

    uint32_t last_seq = 0;   /* remember the last command number we acted on */

    /* FOREVER loop: the PRU never exits while firmware is loaded. */
    while (1) {
        /* Only act if Linux has written the magic password ("PROU").
           If magic is wrong, Linux hasn't initialised the board yet — do nothing. */
        if (cmd->magic == 0x50524F55u) {          /* "PROU" -> ARM has written */
            if (cmd->seq != last_seq) {            /* new command since last loop */
                last_seq = cmd->seq;               /* remember we handled this one */
                pulse(P9_31_R30_BIT, cmd->pan_us); /* pan  */
                pulse(P9_30_R30_BIT, cmd->tilt_us);/* tilt */
            } else {
                /* no new command: re-emit last pulse so servo holds position */
                pulse(P9_31_R30_BIT, cmd->pan_us);
                pulse(P9_30_R30_BIT, cmd->tilt_us);
            }
        }
    }
}
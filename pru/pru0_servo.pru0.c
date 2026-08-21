#include <stdint.h>
#include "resource_table_empty.h"

/* ============================================================
   WHY we poke the GPIO peripheral (and NOT __R30):
   P9_16 = GPIO1_19. On a stock BeagleBone the pin is muxed to
   plain GPIO mode, NOT to a PRU "direct output" (R30) line.
   So writing __R30 does nothing on P9_16 -- that is why you
   heard no click earlier. The fix: the PRU reaches out over the
   OCP bus and writes straight into the GPIO1 hardware registers.
   No device-tree / pinmux change needed; the pin stays in GPIO
   mode (the same mode gpiod used to move the servo before).

   THE TRAP THAT BIT US: the GPIO1 block has its OWN clock gate
   inside the PRCM (power/clock manager). When Linux's gpiod ran,
   it switched that clock ON. The moment the program exited, Linux
   gated the clock OFF again to save power. With the clock off,
   the PRU's register writes are SILENTLY DROPPED -- no error, the
   pin just never moves. So the PRU MUST switch the clock on itself
   before poking the pin. That is the fix added below.
   ============================================================ */

/* AM335x physical addresses -- from the TRM memory map.
   Think of these as "house numbers" on the chip's address bus. */
#define GPIO1_BASE        0x4804C000u   /* GPIO1 block start            */
#define GPIO_OE           0x134u        /* Output Enable: 0=out, 1=in   */
#define GPIO_SETDATAOUT   0x194u        /* write 1 => that pin goes HIGH */
#define GPIO_CLEARDATAOUT 0x190u        /* write 1 => that pin goes LOW  */
#define P9_16_BIT        (1u << 19)    /* GPIO1_19 is bit 19            */

/* PRU-ICSS config block: we must wake up the OCP bus so the PRU
   is allowed to touch hardware outside its own little RAM. */
#define CFG_SYSCFG        0x4A300004u
#define STANDBY_INIT_BIT  (1u << 4)

/* PRCM (Power/Clock manager), CM_PER region. GPIO1 has its own
   clock gate here. 0x44E00000 = CM_PER base, 0xAC = GPIO1_CLKCTRL.
   Without enabling this, GPIO1 register writes are dropped. */
#define CM_PER_GPIO1_CLKCTRL  0x44E000ACu
#define CM_PER_GPIO1_ENABLE   0x2u       /* MODULEMODE = 2 (enable)      */
/* IDLEST field is bits [17:16]; 0 means the clock is actually running. */

/* PRU core runs at 200 MHz => 200,000 cycles per millisecond. */
#define CYCLES_PER_MS     200000u
#define DELAY_MS(ms)      __delay_cycles((uint32_t)(CYCLES_PER_MS * (ms)))

/* ------------------------------------------------------------
   prcm_ocp_enable(): do the two "wake up the hardware" steps
   that Linux normally does for us:
     1) enable the PRU's OCP master port (so it can reach GPIO1)
     2) switch ON the GPIO1 functional clock in the PRCM
   Call this BEFORE touching any GPIO1 register.
   ------------------------------------------------------------ */
static inline void prcm_ocp_enable(void) {
    /* 1) let the PRU talk to the rest of the chip (OCP bus). */
    (*(volatile uint32_t*)CFG_SYSCFG) &= ~STANDBY_INIT_BIT;

    /* 2) switch ON the GPIO1 clock via the PRCM power manager. */
    volatile uint32_t *clk = (volatile uint32_t*)CM_PER_GPIO1_CLKCTRL;
    *clk = CM_PER_GPIO1_ENABLE;                 /* MODULEMODE = enable */
    /* Wait (bounded) until the clock reports "running" (IDLEST == 0).
       Bounded so the PRU can't hang forever if something is off. */
    for (uint32_t t = 0; t < 100000u; t++) {
        if (((*clk) & (3u << 16)) == 0u) break;
    }
}

/* helper: make P9_16 an OUTPUT (clear bit 19 in the OE register). */
static inline void p9_16_output(void) {
    (*(volatile uint32_t*)(GPIO1_BASE + GPIO_OE)) &= ~P9_16_BIT;
}

/* helper: one delay chunk -- CONSTANT cycles so __delay_cycles
   expands to a reliable inline loop on this compiler. */
#define UNIT_CYCLES   2000u    /* 10 us per chunk (constant -> safe) */

void main(void){
#ifdef BLINK_TEST
    /* ---- 5 Hz toggle, only to prove the pin can flip --------------- */
    prcm_ocp_enable();
    p9_16_output();
    int i;
    for (i = 0; i < 10; i++){
        (*(volatile uint32_t*)(GPIO1_BASE + GPIO_SETDATAOUT))   = P9_16_BIT;
        DELAY_MS(100);
        (*(volatile uint32_t*)(GPIO1_BASE + GPIO_CLEARDATAOUT)) = P9_16_BIT;
        DELAY_MS(100);
    }
#elif defined(SERVO_STEADY)
    /* ============================================================
       DIAGNOSTIC: hold a STEADY 1.5 ms center pulse forever.
       Constant delays only -> timing is rock solid.
       WHY: if THIS moves the servo but the sweep doesn't, the
       bug is in the sweep's variable-count delay logic. If THIS
       ALSO does nothing, the PRU's writes aren't reaching the
       pin at all (clock/pinmux) and we move the diagnostic to
       the Linux side.
       ============================================================ */
    prcm_ocp_enable();
    p9_16_output();
    for (;;) {
        (*(volatile uint32_t*)(GPIO1_BASE + GPIO_SETDATAOUT))   = P9_16_BIT;  /* HIGH */
        for (uint32_t k = 0; k < 150u; k++) __delay_cycles(UNIT_CYCLES);       /* 1.5 ms */
        (*(volatile uint32_t*)(GPIO1_BASE + GPIO_CLEARDATAOUT)) = P9_16_BIT;   /* LOW  */
        for (uint32_t k = 0; k < 1850u; k++) __delay_cycles(UNIT_CYCLES);      /* 18.5 ms -> 50 Hz */
    }
#else
    /* ============================================================
       REAL PRU SERVO CONTROL  (this IS the project goal!)
       ------------------------------------------------------------
       Make the PRU emit the exact 50 Hz PWM the gpiod test used to
       move your servo. No Linux, no gpiod -- the PRU flips P9_16
       all by itself, with rock-steady timing.

       Hold two facts in your head:
         * PRU core clock = 200 MHz  =>  200,000 cycles = 1 ms.
         * Servo wants 50 Hz => one period = 20 ms. Inside each
           20 ms we hold HIGH for 1.0..2.0 ms (the "pulse") and
           LOW for the rest. Pulse width = servo position.

       IMPORTANT: on this compiler __delay_cycles(N) needs a
       CONSTANT N. So we build every delay from many identical
       10 us (2000-cycle) chunks and vary only HOW MANY chunks
       we loop -- never the chunk size.
       ============================================================ */

    #define PERIOD_UNITS  2000u    /* 2000 chunks = 20 ms => 50 Hz       */
    /* pulse spans 100..200 chunks == 1.0 ms .. 2.0 ms */

    prcm_ocp_enable();
    p9_16_output();

    for (;;) {
        /* sweep UP: pulse 1.0ms -> 2.0ms */
        for (uint32_t s = 0; s <= 100u; s++) {
            uint32_t pulse = 100u + s;             /* 100..200 chunks */
            uint32_t gap   = PERIOD_UNITS - pulse; /* 1900..1800 */
            (*(volatile uint32_t*)(GPIO1_BASE + GPIO_SETDATAOUT))   = P9_16_BIT;
            for (uint32_t k = 0; k < pulse; k++) __delay_cycles(UNIT_CYCLES);
            (*(volatile uint32_t*)(GPIO1_BASE + GPIO_CLEARDATAOUT)) = P9_16_BIT;
            for (uint32_t k = 0; k < gap;   k++) __delay_cycles(UNIT_CYCLES);
        }
        /* sweep DOWN: pulse 2.0ms -> 1.0ms */
        for (uint32_t s = 0; s <= 100u; s++) {
            uint32_t pulse = 200u - s;             /* 200..100 chunks */
            uint32_t gap   = PERIOD_UNITS - pulse;
            (*(volatile uint32_t*)(GPIO1_BASE + GPIO_SETDATAOUT))   = P9_16_BIT;
            for (uint32_t k = 0; k < pulse; k++) __delay_cycles(UNIT_CYCLES);
            (*(volatile uint32_t*)(GPIO1_BASE + GPIO_CLEARDATAOUT)) = P9_16_BIT;
            for (uint32_t k = 0; k < gap;   k++) __delay_cycles(UNIT_CYCLES);
        }
    }
#endif
    __halt();
}

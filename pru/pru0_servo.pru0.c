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

/* PRU core runs at 200 MHz => 200,000 cycles per millisecond. */
#define CYCLES_PER_MS     200000u
#define DELAY_MS(ms)      __delay_cycles((uint32_t)(CYCLES_PER_MS * (ms)))

void main(void){
#ifdef BLINK_TEST
    /* 1) let the PRU talk to the rest of the chip (enable OCP). */
    (*(volatile uint32_t*)CFG_SYSCFG) &= ~STANDBY_INIT_BIT;

    /* 2) make P9_16 an OUTPUT (clear bit 19 in the OE register). */
    (*(volatile uint32_t*)(GPIO1_BASE + GPIO_OE)) &= ~P9_16_BIT;

    /* 3) toggle 10 times: HIGH 100 ms, LOW 100 ms. */
    int i;
    for (i = 0; i < 10; i++){
        (*(volatile uint32_t*)(GPIO1_BASE + GPIO_SETDATAOUT))   = P9_16_BIT;
        DELAY_MS(100);
        (*(volatile uint32_t*)(GPIO1_BASE + GPIO_CLEARDATAOUT)) = P9_16_BIT;
        DELAY_MS(100);
    }
#else
    /* PHASE 2: real servo sweep. We will write this together next.
       For now just halt so the non-blink build still compiles. */
    __halt();
#endif
    __halt();
}

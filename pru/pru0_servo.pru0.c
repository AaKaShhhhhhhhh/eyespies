#include <stdint.h>
#include "resource_table_empty.h"

/* ============================================================
   THE ONE LESSON BEHIND ALL THE SILENCE (read this first):
   ------------------------------------------------------------
   P9_16 is NOT GPIO1_19. On this BeagleBone:
       gpiodetect -> "gpiochip0 [gpio-0-31]"  = GPIO0  (base 0x44E07000)
       gpiodetect -> "gpiochip1 [gpio-32-63]" = GPIO1  (base 0x4804C000)
   Your WORKING userspace test used gpiochip0 line 19 = GPIO0_19.
   So the servo lives on GPIO0, and the PRU must poke GPIO0.
   (An earlier version poked GPIO1_19 = an empty pin = servo never moved.)

   WHY poke the GPIO peripheral (and NOT __R30):
   P9_16 is just a normal GPIO pad. The PRU's private fast pins (__R30)
   are on other headers (P9_41 etc), not P9_16. To drive P9_16 from the
   PRU we write the GPIO0 registers directly over the OCP bus.

   GPIO0 register map (base 0x44E07000), same layout as all GPIO banks:
       OE           0x134   (bit=1 -> input;  bit=0 -> output)
       CLEARDATAOUT 0x190   (write 1 -> pin LOW)
       SETDATAOUT   0x194   (write 1 -> pin HIGH)
   Pin bit for P9_16 / GPIO0_19 = (1u << 19).

   GPIO0 clock lives in the WKUP power domain:
       CM_WKUP_GPIO0_CLKCTRL = 0x44E00408
       MODULEMODE bits 0-1 = 2 (enable); IDLEST bits 16-17 = 0 when on.
   ============================================================ */

#define GPIO0_BASE           0x44E07000u
#define GPIO_OE              0x134u   /* 1=input, 0=output */
#define GPIO_CLEARDATAOUT    0x190u   /* write 1 -> LOW  */
#define GPIO_SETDATAOUT      0x194u   /* write 1 -> HIGH */
#define SERVO_BIT            (1u << 19)   /* GPIO0_19 = P9_16 */

/* Clock enable register for GPIO0 (WKUP domain). */
#define CM_WKUP_GPIO0_CLKCTRL 0x44E00408u

/* Turn the GPIO0 module clock ON (it can be gated by Linux when idle).
   Without this, writes to the GPIO0 registers are silently dropped. */
static void prcm_gpio0_enable(void) {
    volatile uint32_t *clk = (volatile uint32_t *)CM_WKUP_GPIO0_CLKCTRL;
    *clk = (2u << 0);                       /* MODULEMODE = 2 (enable) */
    while (((*clk) & (3u << 16)) != 0u) {   /* wait until IDLEST == 0 */
        /* bounded by hardware; safe to spin */
    }
}

/* PRU's __delay_cycles() REQUIRES a compile-time CONSTANT argument - you cannot
   pass a variable. So we wait in fixed 10us chunks (constant __delay_cycles(2000)),
   looping a variable number of times. Sub-10us remainder is dropped (max 0.9% error). */
#define US_PER_CHUNK      10u
#define CYCLES_PER_CHUNK  (US_PER_CHUNK * 200u)   /* 2000 cycles = 10us @ 200 MHz */

/* busy-wait for roughly `us` microseconds using the 200 MHz PRU cycle counter. */
static void busy_wait_us(uint32_t us) {
    uint32_t chunks = us / US_PER_CHUNK;
    while (chunks--) {
        __delay_cycles(CYCLES_PER_CHUNK);
    }
}

/* ---------- mode switch (picked at build time via -D on the command line) ---------- */
#if defined(SERVO_STEADY)
    /* DIAGNOSTIC: hold a constant 1.5 ms center pulse forever.
       If THIS moves the servo but the sweep doesn't, the sweep delay-logic
       is wrong. If this also does nothing, the PRU still isn't reaching the
       pin (clock / pinmux). */
    #define PULSE_ON_US   1500u
    #define PULSE_OFF_US  18500u
    #define DO_SWEEP     0
#elif defined(BLINK_TEST)
    /* DIAGNOSTIC: toggle the pin at ~5 Hz so you can scope/measure it.
       NOT a servo signal (too slow) — only for pin-reach checks. */
    #define PULSE_ON_US   100000u
    #define PULSE_OFF_US  100000u
    #define DO_SWEEP     0
#else
    /* PROJECT MODE: real 50 Hz servo sweep, 1.0 ms -> 2.0 ms over ~6 s. */
    #define PERIOD_US    20000u   /* 20 ms = 50 Hz */
    #define PW_MIN_US    1000u    /* 1.0 ms -> one end */
    #define PW_MAX_US    2000u    /* 2.0 ms -> other end */
    #define STEP_COUNT   300u     /* 300 cycles * 20 ms = 6 s sweep */
    #define DO_SWEEP     1
#endif

void main(void) {
    /* 1) Make sure the GPIO0 module clock is running. */
    prcm_gpio0_enable();

    /* 2) Configure P9_16 (GPIO0_19) as an OUTPUT.
       We CLEAR the OE bit. Any 1s already set elsewhere are untouched. */
    volatile uint32_t *oe = (volatile uint32_t *)(GPIO0_BASE + GPIO_OE);
    *oe = (*oe) & ~SERVO_BIT;

    volatile uint32_t *set = (volatile uint32_t *)(GPIO0_BASE + GPIO_SETDATAOUT);
    volatile uint32_t *clr = (volatile uint32_t *)(GPIO0_BASE + GPIO_CLEARDATAOUT);

#if DO_SWEEP
    /* ---- real servo sweep ---- */
    for (;;) {
        for (uint32_t i = 0; i < STEP_COUNT; i++) {
            uint32_t pw = PW_MIN_US
                        + ((PW_MAX_US - PW_MIN_US) * i) / (STEP_COUNT - 1u);
            /* pulse HIGH for pw microseconds */
            *set = SERVO_BIT;
            busy_wait_us(pw);
            /* pulse LOW for the rest of the 20 ms period */
            *clr = SERVO_BIT;
            busy_wait_us(PERIOD_US - pw);
        }
        /* (optional) reverse direction could go here; sweep end-to-end is fine */
    }
#else
    /* ---- diagnostic steady / blink (constant pulse) ---- */
    for (;;) {
        *set = SERVO_BIT;
        busy_wait_us(PULSE_ON_US);
        *clr = SERVO_BIT;
        busy_wait_us(PULSE_OFF_US);
    }
#endif
}

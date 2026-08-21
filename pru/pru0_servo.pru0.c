/*
 * pru0_servo.pru0.c
 * ---------------------------------------------------------------------------
 * PRU0 firmware that drives a servo signal on the BeagleBone pin the servo is
 * actually wired to. We drive BOTH candidate banks (GPIO0_19 and GPIO1_19) so
 * it works no matter which one the device tree routed the pin to.
 *
 * WHY THIS IS HARD (the 3 Linux gates the PRU must open itself):
 *   1) SYSCFG.STANDBY_INIT (bit 4) gates the PRU's OCP bus. Clear it or the
 *      PRU "runs" but every write to 0x44E00000+ is silently dropped.
 *   2) The clock DOMAIN (CLKSTCTRL) must be forced awake (SW_WKUP=2), or the
 *      module clock gate stays shut even after we enable the module.
 *   3) The module clock (CLKCTRL, MODULEMODE=2) must be enabled, or the GPIO
 *      peripheral is dead.
 * Only after all three does writing GPIO OE/SET/CLEAR reach the physical pin.
 *
 * NOTE: we deliberately do NOT use __R30 (PRU direct output) because that needs
 * the pinmux to be in "mode 5" (pruout), which the device tree isn't setting
 * here (the pin is in GPIO mode, which is why Linux gpiod could move it).
 * GPIO register-poke works in GPIO mode with no pinmux change.
 * ---------------------------------------------------------------------------
 */

#include "resource_table_empty.h"

/* ---- PRU config (LOCAL PRU addresses, NOT 0x44E global) ---- */
#define PRU_CFG_SYSCFG  (*(volatile uint32_t *)0x00026004u)  /* STANDBY_INIT = bit 4 */

/* ---- Clock manager (global L4 addresses) ---- */
#define CM_WKUP_CLKSTCTRL      0x44E00400u   /* WKUP domain sleep ctl (GPIO0) */
#define CM_WKUP_GPIO0_CLKCTRL  0x44E00408u   /* GPIO0 module clock           */
#define CM_PER_CLKSTCTRL       0x44E00000u   /* PER domain sleep ctl (GPIO1) */
#define CM_PER_GPIO1_CLKCTRL   0x44E000ACu   /* GPIO1 module clock           */

/* ---- GPIO banks ---- */
#define GPIO0_BASE 0x44E07000u
#define GPIO1_BASE 0x4804C000u
#define GPIO_OE           0x134u   /* 0 = output, 1 = input */
#define GPIO_SETDATAOUT   0x194u
#define GPIO_CLEARDATAOUT 0x190u

#define SERVO_BIT (1u << 19)      /* bit 19 in both GPIO0 and GPIO1 */

/* Delay: PRU runs at 200 MHz -> 5 ns per cycle.
   pru-gcc's __delay_cycles() REQUIRES a compile-time constant, so we loop a
   fixed 2000-cycle (10 us) chunk. */
#define CYCLES_PER_CHUNK 2000u

static void busy_wait_us(uint32_t us) {
    uint32_t chunks = (us * 100u) / (CYCLES_PER_CHUNK / 10u);  /* us*100 -> 10us units */
    for (uint32_t i = 0; i < chunks; i++) {
        __delay_cycles(CYCLES_PER_CHUNK);
    }
}

/* Force a clock DOMAIN awake, then enable a MODULE's clock. */
static void prcm_enable(volatile uint32_t *clkstctrl, volatile uint32_t *clk) {
    *clkstctrl = 0x2u;                     /* SW_WKUP: force domain clocks on */
    *clk = (2u << 0);                      /* MODULEMODE = 2 (enable) */
    for (volatile uint32_t t = 0u; t < 2000000u; t++) {
        if (((*clk) & (3u << 16)) == 0u) break;  /* IDLEST == 0 => clock on */
    }
}

void main(void) {
    /* 0) Wake the PRU's OCP bus. */
    PRU_CFG_SYSCFG &= ~(1u << 4);

    /* 1) Bring up the clocks for BOTH banks (covers bank confusion). */
    prcm_enable((volatile uint32_t *)CM_WKUP_CLKSTCTRL,
                (volatile uint32_t *)CM_WKUP_GPIO0_CLKCTRL);
    prcm_enable((volatile uint32_t *)CM_PER_CLKSTCTRL,
                (volatile uint32_t *)CM_PER_GPIO1_CLKCTRL);

    /* 2) Configure GPIO0_19 and GPIO1_19 as OUTPUTS. */
    volatile uint32_t *oe0 = (volatile uint32_t *)(GPIO0_BASE + GPIO_OE);
    volatile uint32_t *oe1 = (volatile uint32_t *)(GPIO1_BASE + GPIO_OE);
    *oe0 = (*oe0) & ~SERVO_BIT;
    *oe1 = (*oe1) & ~SERVO_BIT;

    volatile uint32_t *set0 = (volatile uint32_t *)(GPIO0_BASE + GPIO_SETDATAOUT);
    volatile uint32_t *clr0 = (volatile uint32_t *)(GPIO0_BASE + GPIO_CLEARDATAOUT);
    volatile uint32_t *set1 = (volatile uint32_t *)(GPIO1_BASE + GPIO_SETDATAOUT);
    volatile uint32_t *clr1 = (volatile uint32_t *)(GPIO1_BASE + GPIO_CLEARDATAOUT);

#if DO_SWEEP
    /* real servo sweep (50 Hz, 1.0..2.0 ms) */
    #define PERIOD_US 20000u
    #define PW_MIN_US 1000u
    #define PW_MAX_US 2000u
    #define STEP_COUNT 40u
    for (;;) {
        for (uint32_t i = 0; i < STEP_COUNT; i++) {
            uint32_t pw = PW_MIN_US + ((PW_MAX_US - PW_MIN_US) * i) / (STEP_COUNT - 1u);
            *set0 = SERVO_BIT; *set1 = SERVO_BIT;
            busy_wait_us(pw);
            *clr0 = SERVO_BIT; *clr1 = SERVO_BIT;
            busy_wait_us(PERIOD_US - pw);
        }
    }
#else
    /* diagnostic: steady 50 Hz, 1.5 ms pulse (center) */
    for (;;) {
        *set0 = SERVO_BIT; *set1 = SERVO_BIT;
        busy_wait_us(1500u);
        *clr0 = SERVO_BIT; *clr1 = SERVO_BIT;
        busy_wait_us(18500u);
    }
#endif
}

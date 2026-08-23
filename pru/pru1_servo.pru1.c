#include <stdint.h>

/* =========================================================================
   AM335x PRU-ICSS SERVO FIRMWARE  —  drives P9_29 via PRU0 r30 DIRECT OUTPUT
   -------------------------------------------------------------------------
   IMPORTANT CORRECTION (this is the real root cause of "mode 5 / no move"):

   The PRU's r30 output does NOT magically bypass the pinmux. The pad only
   reaches the PRU when its pinmux is set to a PRU mode. For P9_29 (ball R28)
   the modes are:
       mode 4 -> pr1_pru0_pru_r30_1   (PRU0 direct output)   <-- what we use
       mode 5 -> pr1_pru1_pru_r30_1   (PRU1 direct output)
       mode 7 -> GPIO3_21             (the DEFAULT boot mode)
   So at boot the pad is GPIO (mode 7) and PRU r30.1 reaches NOTHING. This is
   why your original PRU1 firmware "worked briefly then stopped": the pad was
   momentarily in mode 5 (routed to PRU1) and then reset to mode 7 at reboot.

   FIX: this firmware sets its OWN pinmux. The PRU has an OCP master port that
   can write the AM335x Control Module (pinmux) registers, so we reprogram
   P9_29 to mode 4 ourselves — no config-pin, no devmem, no DT overlay, no
   Linux cooperation. Then we clear STANDBY_INIT (so r30 isn't tri-stated) and
   drive r30.1. Fully self-contained and survives reboots.

   (If you ever want the pin back as GPIO, just stop the PRU; the pinmux stays
   at mode 4 until the next reboot, when it returns to mode 7.)

   ARM<->PRU: we read the desired pulse width (us, 1000..2000) from PRU SHARED
   RAM @ 0x4A310000 word 0. ARM writes it with `arm_write_p929` (mmap). No
   syscalls in the hot loop, so the 50 Hz pulse is jitter-free.
   ========================================================================= */

/* ---- AM335x memory map (from TRM) ---------------------------------------- */
#define PRU0_CFG_BASE    0x4A322000u   /* PRU0 control/status registers      */
#define PRU_SHARED_RAM   0x4A310000u   /* 12KB shared with ARM -> mailbox    */

/* Control Module (pinmux) base + P9_29 (ball R28) conf register offset.
   From the BeagleBone SRM / Derek Molloy pin table for the PRU cape pins:
     P9_28 = gpmc_ben1  -> conf 0x99C
     P9_29 = gpmc_csn3  -> conf 0x9A0   <-- P9_29
     P9_30 = gpmc_csn2  -> conf 0x9A4
     P9_31 = gpmc_csn1  -> conf 0x9A8
   So P9_29 conf register = 0x44E109A0.  *** IF P9_29 still does not move
   after loading, this single number is the most likely culprit (off-by-one
   with P9_30 @ 0x9A4) — change it and rebuild. Verify on the board with:
     sudo cat /sys/kernel/debug/pinctrl/44e10800.pinmux-pinctrl-single/pins | grep -i '9a0\|9a4'
   and confirm which offset currently shows P9_29's GPIO-mode default. */
#define CONTROL_MODULE_BASE 0x44E10000u
#define CONF_P9_29_OFF      0x9A0u
#define CONF_P9_29          (CONTROL_MODULE_BASE + CONF_P9_29_OFF)

#define SERVO_BIT  (1u << 1)   /* r30.1 -> P9_29 on PRU0 (once muxed to mode 4) */

/* Minimal remoteproc resource table, self-contained (no rsc_types.h needed).
   Linux's remoteproc accepts this and boots the firmware. */
struct resource_table {
    uint32_t ver;
    uint32_t num;
    uint32_t reserved[2];
};
struct my_resource_table {
    struct resource_table base;
    uint32_t offset[1];
} __attribute__((packed));
__attribute__((section(".resource_table"), used))
struct my_resource_table pru_remoteproc_ResourceTable = {
    .base = { .ver = 1, .num = 0, .reserved = {0, 0} },
    .offset = { 0 }
};

/* The PRU core runs at 200 MHz -> 200 cycles = 1 us.
   pru-gcc wants __delay_cycles() argument to be a COMPILE-TIME constant, so we
   loop a fixed 10 us chunk instead of one big variable delay. */
#define CHUNK_CYCLES 2000u   /* 2000 cycles = 10 us at 200 MHz */
static void delay_us(unsigned us) {
    unsigned chunks = (us * 200u) / CHUNK_CYCLES;  /* us / 10 */
    unsigned i;
    for (i = 0; i < chunks; i++) __delay_cycles(CHUNK_CYCLES);
}

/* r30 is the PRU's OWN direct-output register. After we mux P9_29 to mode 4,
   writing bit 1 drives P9_29 straight to the pad. */
volatile register uint32_t __R30 __asm__("r30");

#define PERIOD_US 20000u     /* 20 ms / 50 Hz servo frame */

int main(void)
{
    /* ARM writes the desired pulse width (microseconds) into shared[0].
       1000 us = one extreme, 1500 us = center, 2000 us = other extreme. */
    volatile uint32_t *shared = (volatile uint32_t *)PRU_SHARED_RAM;
    volatile uint32_t *conf_p929 = (volatile uint32_t *)CONF_P9_29;
    uint32_t pulse_us;

    /* 1) Reprogram P9_29 to PRU0 direct-output mode (mode 4), pull-up/down
       disabled, fast slew. bits[2:0]=4, bit5=1 (pulldisable), bit6=1 (fast).
       Value 0x24. This is what makes r30.1 actually reach the pad. */
    *conf_p929 = 0x24u;

    /* 2) Clear STANDBY_INIT in SYSCFG so the PRU's r30 outputs are NOT
       tri-stated and actually reach the pad. (PRU0 CFG base + 0x4;
       STANDBY_INIT = bit 4.) */
    (*(volatile uint32_t *)(PRU0_CFG_BASE + 0x4)) &= ~(1u << 4);

    /* Start with the pin LOW. */
    __R30 &= ~SERVO_BIT;

    while (1) {
        pulse_us = shared[0];
        if (pulse_us < 1000u) pulse_us = 1000u;   /* clamp to safe servo range */
        if (pulse_us > 2000u) pulse_us = 2000u;

        __R30 |=  SERVO_BIT;                       /* P9_29 HIGH */
        delay_us(pulse_us);                        /* pulse length = position   */
        __R30 &= ~SERVO_BIT;                       /* P9_29 LOW  */
        delay_us(PERIOD_US - pulse_us);            /* fill -> 20 ms total @50Hz */
    }
    return 0;
}

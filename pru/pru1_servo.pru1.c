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
   IMPORTANT — learned from the board's own pinctrl dump on 2026-08-23:
     pin 104  18:gpio-64-95  44e109a0  00000027   <- GPIO2_18 (NOT P9_29)
     pin 105  19:gpio-64-95  44e109a4  00000027   <- GPIO2_19 (NOT P9_29)
   So 0x9A0/0x9A4 are GPIO2 pins. P9_29 is GPIO3, so its conf offset is
   elsewhere. Leading candidate: ball R28's primary signal is gpmc_csn3, whose
   conf register is 0x44E1086C. To be safe, the firmware reads the offset from
   PRU shared RAM word 1 (see arm_write_p929's optional 2nd arg); if zero it
   uses DEFAULT_CONF_OFF below. This lets you try offsets WITHOUT recompiling:
     sudo ./arm_write_p929 1500 0x86c   # then reload the firmware
   Confirm the true offset on the board with:
     LINE=$(gpioinfo gpiochip3 | awk '/[Pp]9_29/{print $2}' | tr -d ':')
     grep "${LINE}:gpio-96-127" /sys/kernel/debug/pinctrl/44e10800.pinmux-pinctrl-single/pins
   The printed 44e108xx is P9_29's conf register. */
#define CONTROL_MODULE_BASE 0x44E10000u
#define DEFAULT_CONF_OFF    0x9BCu   /* CONF_GPMC_CSN3 (ball R28 / P9_29, GPIO3_21).
                                       CONFIRMED from board pinctrl dump 2026-08-23:
                                       gpio-96-127 line 21 = 0x44E109BC (offset 0x9BC).
                                       Override at runtime via arm_write_p929 <us> <offset>. */
#define CONF_P9_29          (CONTROL_MODULE_BASE + DEFAULT_CONF_OFF)

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
    uint32_t pulse_us;

    /* 1) Reprogram P9_29 to PRU0 direct-output mode (mode 4), keep the
       pull-up like the default (0x27) but switch mode bits to 4 -> 0x24.
       The conf offset comes from shared RAM word 1 if set, else the default.
       This is what makes r30.1 actually reach the pad. */
    uint32_t conf_off = shared[1] ? shared[1] : DEFAULT_CONF_OFF;
    volatile uint32_t *conf_p929 = (volatile uint32_t *)(CONTROL_MODULE_BASE + conf_off);
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

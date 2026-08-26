#include <stdint.h>

/* =========================================================================
   AM335x PRU-ICSS SERVO FIRMWARE  --  drives P9_29 via PRU0 r30 DIRECT OUTPUT
   -------------------------------------------------------------------------
   ARCHITECTURE (set 2026-08-23 after proving the old approach fails):

   The PRU's r30 output does NOT bypass the pinmux. The pad reaches the PRU
   only when the pinmux is set to a PRU mode. For P9_29 (ball R28):
       mode 4 -> pr1_pru0_pru_r30_1   (PRU0 direct output)   <-- we use this
       mode 5 -> pr1_pru1_pru_r30_1   (PRU1 direct output)
       mode 7 -> GPIO3_21             (DEFAULT boot mode)

   We ORIGINALLY tried to let the PRU set its own pinmux over its OCP master
   (write 0x24 to conf 0x44E109BC). PROVEN FAIL: on the 6.x kernel the PRU
   OCP master cannot write the Control Module -- pinctrl stayed at 0x28 (mode
   0, GPIO) after the firmware ran. So the mux is now set from ARM/Linux
   (arm_write_p929 writes 0x24 via /dev/mem, which DOES reach 0x44E10000),
   and this firmware ONLY:
     - clears STANDBY_INIT so r30 is not tri-stated, and
     - drives r30.1 (P9_29) with a 50 Hz servo pulse from shared RAM.

   The PRU is now a "dumb PWM" -- no pinmux access, no kernel cooperation
   beyond the loader. Fully self-contained for the PWM part.

   ARM<->PRU: pulse width (us, 1000..2000) lives in PRU SHARED RAM
   @ 0x4A310000 word 0. ARM writes it with `arm_write_p929`. No syscalls in
   the hot loop => jitter-free 50 Hz.
   ========================================================================= */

/* ---- AM335x memory map (from TRM) ---------------------------------------- */
#define PRU0_CFG_BASE    0x4A322000u   /* PRU0 control/status registers      */
#define PRU_SHARED_RAM   0x4A310000u   /* 12KB shared with ARM -> mailbox    */

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

/* r30 is the PRU's OWN direct-output register. Once ARM muxed P9_29 to mode 4,
   writing bit 1 drives P9_29 straight to the pad. */
volatile register uint32_t __R30 __asm__("r30");

#define PERIOD_US 20000u     /* 20 ms / 50 Hz servo frame */

int main(void)
{
    /* ARM writes the desired pulse width (microseconds) into shared[0].
       1000 us = one extreme, 1500 us = center, 2000 us = other extreme. */
    volatile uint32_t *shared = (volatile uint32_t *)PRU_SHARED_RAM;
    uint32_t pulse_us;

    /* Clear STANDBY_INIT (CFG SYSCFG bit 0 -- NOT bit 4, which is SUB_MWAIT)
       so the PRU's r30 outputs are NOT tri-stated and actually reach the pad.
       From PRU core code this uses the PRU-LOCAL CFG address 0x26004. If this
       bit is left set, r30 is high-Z and P9_29 floats (servo dead until you
       touch the bare wire). Arm-side arm_write_p929 also clears it via /dev/mem
       as a belt-and-suspenders measure. */
    (*(volatile uint32_t *)0x00026004) &= ~1u;

    /* Start with the pin LOW. */
    __R30 &= ~SERVO_BIT;

    while (1) {
        uint32_t mode = shared[0];

        if (mode == 0u) {            /* TEST: force constant LOW  */
            __R30 &= ~SERVO_BIT;
        } else if (mode == 1u) {     /* TEST: force constant HIGH */
            __R30 |=  SERVO_BIT;
        } else {                     /* NORMAL: 50 Hz PWM, pulse = position */
            pulse_us = mode;
            if (pulse_us < 1000u) pulse_us = 1000u;   /* clamp safe range */
            if (pulse_us > 2000u) pulse_us = 2000u;

            __R30 |=  SERVO_BIT;                       /* P9_29 HIGH */
            delay_us(pulse_us);                        /* pulse length */
            __R30 &= ~SERVO_BIT;                       /* P9_29 LOW  */
            delay_us(PERIOD_US - pulse_us);            /* fill 20 ms */
        }
    }
    return 0;
}

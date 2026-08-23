#include <stdint.h>

/* =========================================================================
   AM335x PRU-ICSS MEMORY MAP  (why these addresses are here, explicitly)
   -------------------------------------------------------------------------
   The PRU is a SEPARATE tiny core inside the AM335x chip. The main ARM core
   (Linux) and the PRU cannot just "share a variable" by magic - they must
   agree on the SAME physical RAM address. We use the PRU-ICSS "Shared RAM"
   as a mailbox: ARM writes a number there, the PRU reads it every loop.

   These are the PRU1 addresses straight from the AM335x Technical Reference
   Manual (TRM). You already met 0x4A338000 - that is the address you read
   with `devmem2 0x4A338000` to see PRU1's control register.

     PRU1 CFG base    0x4A338000  <- PRU1 control/status registers
                                    (this is the devmem address you used)
     PRU1 Data RAM    0x4A302000  <- PRU1's own 8KB scratch memory
     PRU1 Shared RAM  0x4A310000  <- 12KB shared with ARM  *** OUR MAILBOX ***
     PRU1 Instr RAM   0x4A334000  <- where THIS firmware is loaded (read-only)
   ========================================================================= */
#define PRU1_CFG_BASE    0x4A338000u   /* PRU1 control registers (devmem 0x4A338000) */
#define PRU1_DATA_RAM    0x4A302000u   /* PRU1 local 8KB data RAM */
#define PRU1_SHARED_RAM  0x4A310000u   /* shared with ARM -> control channel */

/* Minimal remoteproc resource table, inlined.
   This removes the need for the external rsc_types.h header (pru-gcc on the
   board does not ship it). Linux's remoteproc driver accepts this as a
   "header-less resource table" and boots the firmware. */
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

/* __R30 is the PRU's OWN output register. Bit 1 is hard-wired to pin P9_29.
   Writing here drives the pin DIRECTLY - no Linux, no GPIO mux, no clock gate.
   This is exactly why P9_29 moved when P9_16 never did. */
volatile register uint32_t __R30 __asm__("r30");

/* The PRU core runs at 200 MHz -> 200 cycles = 1 microsecond.
   pru-gcc requires __delay_cycles()'s argument to be a COMPILE-TIME constant,
   so we loop a fixed 10us chunk instead of one big variable delay. */
#define CHUNK_CYCLES 2000u   /* 2000 cycles = 10 us at 200 MHz */
static void delay_us(unsigned us) {
    unsigned chunks = (us * 200u) / CHUNK_CYCLES;  /* us / 10 */
    unsigned i;
    for (i = 0; i < chunks; i++) __delay_cycles(CHUNK_CYCLES);
}

void main(void) {
    /* ARM writes the desired pulse width (microseconds) into shared[0].
       1000 us = one extreme, 1500 us = center, 2000 us = other extreme.
       We read it every loop so the camera/motion code can steer the tilt. */
    volatile uint32_t *shared = (volatile uint32_t *)PRU1_SHARED_RAM;
    uint32_t pulse_us;

    while (1) {
        pulse_us = shared[0];
        if (pulse_us < 1000u) pulse_us = 1000u;   /* clamp to safe servo range */
        if (pulse_us > 2000u) pulse_us = 2000u;

        __R30 |=  (1u << 1);            /* P9_29 HIGH */
        delay_us(pulse_us);             /* pulse length = commanded position */
        __R30 &= ~(1u << 1);            /* P9_29 LOW  */
        delay_us(20000u - pulse_us);    /* fill remainder -> 20 ms total = 50 Hz */
    }
}

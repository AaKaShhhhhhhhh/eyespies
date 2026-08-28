#include <stdint.h>

/* Minimal, fault-PROOF PRU0 firmware: drives P9_29 (r30.1) as a slow ~0.5 Hz
   square wave (1 s HIGH / 1 s LOW) forever. It does NOT read any memory
   (no shared RAM, no OCP), so a bus fault cannot halt it. Isolation test:
   if the servo swings once per ~2 s, the PRU GPO reaches P9_29 (standby/mux
   are fine) and the only prior bug was firmware logic (bad shared-RAM address);
   if it stays dead, r30 is not reaching the pad at all. */

volatile register uint32_t __R30 __asm__("r30");

#define ONE_SEC_CYCLES  40000000u   /* PRU = 200 MHz -> 5 ns/cycle */

/* Self-contained empty resource table (same shape as pru1_servo.pru1.c,
   which compiled cleanly). NOT including resource_table_empty.h here because
   that header already defines struct resource_table and would clash. */
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

void main(void) {
    /* CRITICAL FIX (board #31): clear STANDBY_INIT so r30 drives the pad.
       While STANDBY_INIT (bit 0 of PRU CFG SYSCFG @ local 0x22004) is set, the
       PRU tri-states its GPO and P9_29 floats. remoteproc does NOT clear it for
       us (proven: pin floated and only moved on touch). The PRU must clear it. */
    (*(volatile uint32_t *)0x22004) &= ~(1u << 0);

    for (;;) {
        __R30 |=  (1u << 1);            /* r30.1 = 1 -> P9_29 HIGH ~1 s */
        __delay_cycles(ONE_SEC_CYCLES);
        __R30 &= ~(1u << 1);            /* r30.1 = 0 -> P9_29 LOW  ~1 s */
        __delay_cycles(ONE_SEC_CYCLES);
    }
}

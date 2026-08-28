#include <stdint.h>
#include "resource_table_empty.h"

/* Minimal, fault-PROOF PRU0 firmware: drives P9_29 (r30.1) as a slow ~0.5 Hz
   square wave (1 s HIGH / 1 s LOW) forever. It does NOT read any memory
   (no shared RAM, no OCP), so a bus fault cannot halt it. Isolation test:
   if the servo swings once per ~2 s, the PRU GPO reaches P9_29 (standby/mux
   are fine) and the only prior bug was firmware logic (bad shared-RAM adddress);
   if it stays dead, r30 is not reaching the pad at all. */

volatile register uint32_t __R30 __asm__("r30");

#define ONE_SEC_CYCLES  40000000u   /* PRU = 200 MHz -> 5 ns/cycle */

struct resource_table {
    uint32_t reserved[2];
    uint8_t  num;
    uint8_t  type;
    uint16_t resv;
    uint32_t offset;
} __attribute__((packed)) resource_table = {
    {0, 0},
    0, 0, 0, 0
};

void main(void) {
    /* STANDBY_INIT already cleared by remoteproc; do NOT touch SYSCFG. */
    for (;;) {
        __R30 |=  (1u << 1);            /* r30.1 = 1 -> P9_29 HIGH ~1 s */
        __delay_cycles(ONE_SEC_CYCLES);
        __R30 &= ~(1u << 1);            /* r30.1 = 0 -> P9_29 LOW  ~1 s */
        __delay_cycles(ONE_SEC_CYCLES);
    }
}

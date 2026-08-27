#include <stdint.h>

/* Minimal, fault-PROOF PRU0 firmware: drives P9_29 (r30.1) as a slow 0.5 Hz
   square wave (1 s HIGH / 1 s LOW) forever. It does NOT read any memory
   (no shared RAM, no OCP), so there is zero chance of a bus fault halting
   the core. Purpose: isolate whether the PRU's r30.1 actually reaches the
   P9_29 pad when muxed to PRU mode 4.
     - If the servo SWINGS to extremes once per second -> GPO reaches the pad
       (standby/mux are fine); the real bug was elsewhere (e.g. the shared-RAM
       address fault in pru1_servo).
     - If the servo stays dead -> the PRU output is not reaching the pad at all
       (standby/tri-state or mux), independent of firmware logic. */

register uint32_t __R30 __asm__("r30");
#define SERVO_BIT (1u << 1)   /* r30.1 -> P9_29 on PRU0 (mux mode 4) */

/* Minimal remoteproc resource table (num=0 -> no carveouts needed). */
struct resource_table { uint32_t ver; uint32_t num; uint32_t reserved[2]; };
struct my_resource_table {
    struct resource_table base;
    uint32_t offset[1];
} __attribute__((packed, section(".resource_table"), used)) =
    { .base = { 1, 0, {0, 0} }, .offset = { 0 } };

/* PRU runs at 200 MHz -> 200 cycles = 1 us. pru-gcc wants a compile-time
   constant for __delay_cycles, so loop fixed 10 us chunks. */
#define CHUNK_CYCLES 2000u   /* 2000 cycles = 10 us @ 200 MHz */
static void delay_us(unsigned us) {
    unsigned chunks = (us * 200u) / CHUNK_CYCLES;   /* us / 10 */
    unsigned i;
    for (i = 0; i < chunks; i++) __delay_cycles(CHUNK_CYCLES);
}

int main(void)
{
    __R30 &= ~SERVO_BIT;          /* start LOW */
    while (1) {
        __R30 |=  SERVO_BIT;      /* P9_29 HIGH for 1 s */
        delay_us(1000000u);
        __R30 &= ~SERVO_BIT;      /* P9_29 LOW for 1 s */
        delay_us(1000000u);
    }
    return 0;
}

/*
 * pru_servo.c — PRU servo firmware, driving P9_16 (GPIO0_19).
 *
 * APPROACH (why this is the right path):
 *   The user already PROVED P9_16 works: `gpioset gpiochip0 19=1` physically
 *   moved the servo. That means P9_16 in its DEFAULT boot mode (mode 7 = GPIO)
 *   is fully functional — the wiring, servo, and pad are all good. So we do
 *   NOT touch the pinmux. Instead the PRU writes the GPIO0 SET/CLEAR
 *   registers over its OCP master (the PRU is a 200 MHz bus master that can
 *   reach the GPIO peripheral at 0x44E07000). The pin stays in GPIO mode,
 *   and the PRU alone generates the 50 Hz servo PWM.
 *
 *   This avoids EVERY pitfall we hit:
 *     - no pinmux mode change (P9_29 was stuck because uBoot/6.x wouldn't mux)
 *     - no r30 bit-number guessing
 *     - no STANDBY_INIT tri-state issue (we don't use r30 at all)
 *
 * PIN:
 *   P9_16 = GPIO0_19. SET=0x44E07194, CLEAR=0x44E07190, OE=0x44E07134 (bit19).
 *
 * BUILD (on BBB, needs pru-gcc):
 *   pru-gcc -O2 -mmcu=am335x.pru0 -o pru_servo.out pru_servo.c
 *
 * LOAD:
 *   sudo ./load_pru.sh pru0 pru_servo.out
 *
 * Stop:
 *   echo stop | sudo tee /sys/class/remoteproc/<pru0-node>/state
 */
#include <stdint.h>

#define GPIO0_BASE      0x44E07000u
#define GPIO_OE         (GPIO0_BASE + 0x134u)   /* 0=output, 1=input       */
#define GPIO_CLEARDATA  (GPIO0_BASE + 0x190u)   /* write 1 to clear bit    */
#define GPIO_SETDATA    (GPIO0_BASE + 0x194u)   /* write 1 to set bit      */
#define PIN_BIT         (1u << 19)              /* GPIO0_19 = P9_16        */

/* PRU0 CFG block (SYSCFG register) — global L3 address 0x4A322004.
 * (Local PRU-view equivalent is 0x00022004; the earlier 0x4A326004 was a
 *  DIFFERENT ICSS sub-block and did NOT clear our STANDBY_INIT — that was
 *  the bug behind board #44/#45 "running but silent".)
 * SYSCFG bit 4 = STANDBY_INIT: when 1 the PRU OCP master port is held in
 *   standby so writes to 0x44E07000 (GPIO0) are silently dropped -> servo
 *   never moves. Clear to 0 to bring the OCP master live. We also clear bit 0
 *   (the repo's earlier r30-era "fix") although it is a read-only IDLE_MODE
 *   status bit; the write is ignored but removes any doubt. MUST be cleared
 *   BEFORE any GPIO write. */
#define PRU0_CFG_SYSCFG 0x4A322004u
#define CFG_STANDBY_INIT (1u << 4)

#define PERIOD_US       20000u                   /* 20 ms frame @ 50 Hz    */

/* Minimal remoteproc resource table (empty; loader requires it). */
struct resource_table {
    uint32_t ver;
    uint32_t num;
    uint32_t resv[2];
};
struct my_resource_table {
    struct resource_table base;
} __attribute__((section(".resource_table"), used, aligned(4)))
resourceTable = { { 1, 0, 0, 0 } };

/* 200 MHz PRU -> 200 cycles = 1 us. */
static void delay_us(unsigned us) {
    while (us--) __delay_cycles(200);
}

int main(void) {
    /* CRITICAL: bring the PRU OCP master out of standby so writes to the
     * GPIO0 registers actually leave the PRU and reach the peripheral. */
    *(volatile uint32_t *)PRU0_CFG_SYSCFG &= ~(CFG_STANDBY_INIT | (1u << 0));

    /* Make sure P9_16 is an OUTPUT (bit 19 of GPIO_OE = 0). */
    *(volatile uint32_t *)GPIO_OE &= ~PIN_BIT;

    while (1) {
        /* Sweep 1.0 ms (one extreme) -> 2.0 ms (other extreme). */
        for (uint32_t w = 1000; w <= 2000; w += 10) {
            *(volatile uint32_t *)GPIO_SETDATA   = PIN_BIT; delay_us(w);
            *(volatile uint32_t *)GPIO_CLEARDATA = PIN_BIT; delay_us(PERIOD_US - w);
        }
        for (uint32_t w = 2000; w >= 1000; w -= 10) {
            *(volatile uint32_t *)GPIO_SETDATA   = PIN_BIT; delay_us(w);
            *(volatile uint32_t *)GPIO_CLEARDATA = PIN_BIT; delay_us(PERIOD_US - w);
        }
    }
    return 0;
}

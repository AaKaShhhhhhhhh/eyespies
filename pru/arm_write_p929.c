/*
 * arm_write_p929.c
 * ---------------------------------------------------------------------------
 * Linux-side helper for the P9_29 PRU servo firmware (pru1_servo.pru1.c).
 *
 * Does TWO things from ARM (root, via /dev/mem):
 *   1) Sets P9_29's PINMUX to PRU0 direct-output mode (mode 4 -> 0x24) by
 *      writing the Control Module conf register at 0x44E109BC.
 *      WHY FROM ARM, NOT THE PRU: on this 6.x kernel the PRU's OCP master
 *      CANNOT write the Control Module (proven 2026-08-23: firmware wrote
 *      0x24 but pinctrl stayed 0x28). So the mux must be set from Linux,
 *      which CAN reach 0x44E10000 through /dev/mem.
 *   2) Writes the commanded pulse width (us, 1000..2000) into PRU SHARED RAM
 *      @ 0x4A310000 word 0. The firmware reads it every 20 ms loop and drives
 *      r30.1 (P9_29) — no syscalls in the hot loop, jitter-free 50 Hz PWM.
 *
 * This tool replaces the dead config-pin / devmem2 / DT-overlay paths.
 *
 * usage:
 *   sudo ./arm_write_p929 <pulse_us 1000..2000> [conf_offset_hex]
 *     sudo ./arm_write_p929 1500            # default mux offset 0x9BC
 *     sudo ./arm_write_p929 1500 0x9bc      # same, explicit
 *     sudo ./arm_write_p929 1500 0x44e109bc # full phys addr also accepted
 *
 * Build:  gcc -O2 -Wall -o arm_write_p929 arm_write_p929.c
 * Requires root (opens /dev/mem).
 * ---------------------------------------------------------------------------
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#define PRU_SHARED_PHYS    0x4A310000u   /* 12KB PRU<->ARM shared RAM      */
#define CONTROL_MODULE_PHYS 0x44E10000u  /* AM335x pinmux / conf registers */
#define DEFAULT_CONF_OFF   0x9BCu        /* P9_29 (ball R28) conf offset    */
#define MAP_SIZE           0x1000u

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <pulse_us 1000..2000> [conf_offset_hex]\n", argv[0]);
        return 1;
    }
    unsigned long us = strtoul(argv[1], NULL, 10);
    if (us < 1000) us = 1000;
    if (us > 2000) us = 2000;

    unsigned long conf_in = DEFAULT_CONF_OFF;
    if (argc >= 3)
        conf_in = strtoul(argv[2], NULL, 16);   /* 0x9bc OR 0x44e109bc */
    if (conf_in >= CONTROL_MODULE_PHYS)
        conf_in -= CONTROL_MODULE_PHYS;          /* accept full phys addr */
    uint32_t conf_off = (uint32_t)conf_in;
    uint32_t conf_phys = CONTROL_MODULE_PHYS + conf_off;

    int fd = open("/dev/mem", O_RDWR);
    if (fd < 0) { perror("open /dev/mem"); return 1; }

    /* (1) set the mux: mode 4 (PRU0 r30.1), input buffer off, pull disabled */
    void *cmap = mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
                      fd, CONTROL_MODULE_PHYS);
    if (cmap == MAP_FAILED) { perror("mmap control module"); close(fd); return 1; }
    volatile uint32_t *conf = (volatile uint32_t *)((char *)cmap + conf_off);
    uint32_t mux_before = *conf;          /* read current mux state first */
    *conf = 0x24u;                         /* attempt: PRU0 mode, rx off, pull off */
    uint32_t mux_rb = *conf;              /* immediate read-back (did it stick?) */
    munmap(cmap, MAP_SIZE);

    /* (2) write the pulse width into PRU shared RAM word 0 */
    void *smap = mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
                      fd, PRU_SHARED_PHYS);
    if (smap == MAP_FAILED) { perror("mmap shared ram"); close(fd); return 1; }
    volatile uint32_t *shared = (volatile uint32_t *)smap;
    shared[0] = (uint32_t)us;
    uint32_t pulse_rb = shared[0];
    munmap(smap, MAP_SIZE);
    close(fd);

    printf("MUX   : before 0x%08X -> wrote 0x24 to 0x%08X (offset 0x%03X) -> readback 0x%08X\n",
           mux_before, conf_phys, conf_off, mux_rb);
    printf("PULSE : wrote %lu us to PRU shared RAM @ 0x%08X -> readback %u\n",
           us, PRU_SHARED_PHYS, pulse_rb);
    printf("Verify mux with: sudo grep %03X /sys/kernel/debug/pinctrl/44e10800.pinmux-pinctrl-single/pins\n",
           conf_off);
    if (mux_rb == 0x24u) {
        printf("RESULT: MUX OK (0x24 = PRU0 mode). The escape hatch works.\n");
        printf("        Now load the firmware and the PRU will drive P9_29.\n");
    } else {
        printf("RESULT: MUX BLOCKED (readback 0x%08X != 0x24).\n", mux_rb);
        printf("        A kernel driver owns P9_29 and re-asserts GPIO mode.\n");
        printf("        Find the owner:  sudo cat /sys/kernel/debug/gpio | grep -i 117\n");
        printf("        (GPIO3_21 = gpio-117). Unbind that driver, then re-run.\n");
    }
    return 0;
}

/*
 * arm_write_p929.c
 * ---------------------------------------------------------------------------
 * Linux-side helper for the P9_29 PRU servo firmware (pru1_servo.pru1.c).
 *
 * The firmware reads its commanded pulse width (in microseconds, 1000..2000)
 * from PRU SHARED RAM at physical address 0x4A310000 (word 0 = 32-bit word).
 * ARM writes there with an mmap of /dev/mem; the PRU reads it every 20 ms loop.
 *
 * OPTIONAL 2nd arg = the pinmux conf-register OFFSET (in bytes from the
 * Control Module base 0x44E10000) for P9_29. The firmware reprograms that
 * conf register to PRU mode 4. If you omit it, the firmware uses a built-in
 * default. Pass a different offset to probe without recompiling the firmware:
 *   sudo ./arm_write_p929 1500          # default offset
 *   sudo ./arm_write_p929 1500 0x86c    # try offset 0x86C
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

#define PRU_SHARED_PHYS 0x4A310000u
#define MAP_SIZE        0x1000u

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <pulse_us 1000..2000> [conf_offset_hex]\n", argv[0]);
        return 1;
    }
    unsigned long us = strtoul(argv[1], NULL, 10);
    if (us < 1000) us = 1000;
    if (us > 2000) us = 2000;

    unsigned long conf_in = 0;
    if (argc >= 3)
        conf_in = strtoul(argv[2], NULL, 16);   /* 0x9bc OR 0x44e109bc */

    int fd = open("/dev/mem", O_RDWR);
    if (fd < 0) { perror("open /dev/mem"); return 1; }

    void *map = mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, PRU_SHARED_PHYS);
    if (map == MAP_FAILED) { perror("mmap"); close(fd); return 1; }

    volatile uint32_t *shared = (volatile uint32_t *)map;
    shared[0] = (uint32_t)us;

    /* Accept EITHER the offset (0x9bc) OR the full physical address
       (0x44e109bc). If it's >= the Control Module base 0x44E10000, treat it
       as a physical address and subtract the base to get the offset the
       firmware expects. */
    unsigned long conf_off = conf_in;
    if (conf_off >= 0x44E10000UL)
        conf_off -= 0x44E10000UL;
    shared[1] = (uint32_t)conf_off;   /* 0 = firmware uses its default */

    printf("Wrote pulse_us=%lu into PRU shared RAM @ 0x%08X\n", us, PRU_SHARED_PHYS);
    if (conf_in)
        printf("Set P9_29 conf offset = 0x%03lX (phys 0x%08lX). Reload firmware to apply.\n",
               conf_off, 0x44E10000UL + conf_off);
    else
        printf("(PRU firmware uses its built-in default conf offset.)\n");

    munmap(map, MAP_SIZE);
    close(fd);
    return 0;
}

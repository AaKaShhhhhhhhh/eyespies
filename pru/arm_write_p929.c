/*
 * arm_write_p929.c
 * ---------------------------------------------------------------------------
 * Linux-side helper for the P9_29 PRU servo firmware (pru1_servo.pru1.c).
 *
 * The firmware reads its commanded pulse width (in microseconds, 1000..2000)
 * from PRU SHARED RAM at physical address 0x4A310000 (offset 0 = 32-bit word).
 * ARM writes there with an mmap of /dev/mem; the PRU reads it every 20 ms loop.
 *
 * Usage:
 *   sudo ./arm_write_p929 1500      # center
 *   sudo ./arm_write_p929 1000      # one extreme
 *   sudo ./arm_write_p929 2000      # other extreme
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
    if (argc != 2) {
        fprintf(stderr, "usage: %s <pulse_us 1000..2000>\n", argv[0]);
        return 1;
    }
    unsigned long us = strtoul(argv[1], NULL, 10);
    if (us < 1000) us = 1000;
    if (us > 2000) us = 2000;

    int fd = open("/dev/mem", O_RDWR);
    if (fd < 0) { perror("open /dev/mem"); return 1; }

    void *map = mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, PRU_SHARED_PHYS);
    if (map == MAP_FAILED) { perror("mmap"); close(fd); return 1; }

    volatile uint32_t *shared = (volatile uint32_t *)map;
    shared[0] = (uint32_t)us;

    printf("Wrote pulse_us=%lu into PRU shared RAM @ 0x%08X\n", us, PRU_SHARED_PHYS);
    printf("(PRU firmware reads this every loop and drives P9_29.)\n", us);

    munmap(map, MAP_SIZE);
    close(fd);
    return 0;
}

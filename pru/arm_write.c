/*
 * arm_write.c
 * ===========
 * PURPOSE: A normal Linux program (runs on the ARM CPU, NOT the PRU) that
 * opens /dev/mem, maps the PRU0 RAM, and writes a servo pulse-width (in
 * microseconds) into the shared block. This lets you TEST the PRU servo
 * WITHOUT building the whole turret program.
 *
 * Build on the BeagleBone:   gcc -O2 -o arm_write arm_write.c
 * Run:                       sudo ./arm_write 1500   (center, silent)
 *                             sudo ./arm_write 1000   (~45 deg)
 *                             sudo ./arm_write 2000   (~135 deg)
 *
 * The PRU must already be loaded (see PRU_GUIDE.md "Load the firmware").
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#define PRU0_DRAM_PHYS  0x4A300000u   /* ARM physical address of PRU0's RAM */
#define SHM_OFFSET      0x1000u       /* MUST match SHM_ADDR in pru0_servo.pru0.c */
#define MAP_SIZE        0x2000u

struct cmd { uint32_t tilt_us; uint32_t pan_us; };

int main(int argc, char **argv){
    if (argc < 2){
        printf("usage: %s <tilt_us 500..2500>\n", argv[0]);
        return 1;
    }
    uint32_t tilt = (uint32_t)atoi(argv[1]);

    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0){ perror("open /dev/mem"); return 1; }

    void *map = mmap(0, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, PRU0_DRAM_PHYS);
    if (map == MAP_FAILED){ perror("mmap"); return 1; }

    /* The shared struct lives at offset 0x1000 inside the mapped region. */
    volatile struct cmd *c = (volatile struct cmd *)((char *)map + SHM_OFFSET);

    c->tilt_us = tilt;     /* PRU reads this on its very next loop iteration */
    c->pan_us  = 1500;

    printf("wrote tilt_us=%u to PRU shared RAM\n", tilt);
    printf("servo should move and be SILENT (no buzz). Press Enter to exit.\n");
    getchar();

    munmap(map, MAP_SIZE);
    close(fd);
    return 0;
}

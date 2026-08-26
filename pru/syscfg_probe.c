/*
 * syscfg_probe.c  --  empirically determine SYSCFG[STANDBY_INIT] (bit 0) write
 * semantics on THIS board, because the bit will not clear with a write-0.
 *
 * Run (as root, PRU0 already loaded or not):
 *   gcc -O2 -o syscfg_probe syscfg_probe.c
 *   sudo ./syscfg_probe
 *
 * It prints the raw register, then tries:
 *   (a) write (val & ~1)   -- "write 0 to clear" hypothesis (W0C)
 *   (b) write (val | 1)    -- "write 1 to clear" hypothesis (W1C)
 *   (c) write (val & ~1) again
 *   (d) re-read after 2 ms settle
 * The polarity that drops bit 0 to 0 is the one to bake into the firmware.
 *
 * AM335x PRU-ICSS CFG base 0x4A322000; SYSCFG is at offset 0x4 (0x4A322004).
 */
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>

#define PRU0_CFG_PHYS 0x4A322000u
#define SYSCFG_OFF    0x4u
#define MAP_SIZE      0x1000u

static void dump(const char *label, uint32_t v)
{
    printf("%-40s 0x%08X  bit0(STANDBY_INIT)=%u  bit1(SUB_MWAIT)=%u  "
           "bit2..3(IDLE)=0x%x  bit4(STANDBY_MODE)=%u\n",
           label, v, v & 1u, (v >> 1) & 1u, (v >> 2) & 0x3u, (v >> 4) & 1u);
}

int main(void)
{
    int fd = open("/dev/mem", O_RDWR);
    if (fd < 0) { perror("open /dev/mem"); return 1; }

    void *m = mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, PRU0_CFG_PHYS);
    if (m == MAP_FAILED) { perror("mmap"); close(fd); return 1; }

    volatile uint32_t *scfg = (volatile uint32_t *)((char *)m + SYSCFG_OFF);

    uint32_t v0 = *scfg;
    dump("SYSCFG initial", v0);

    /* (a) W0C: write 0 to bit 0 */
    *scfg = v0 & ~1u;
    uint32_t v1 = *scfg;
    dump("after write (val & ~1)  [W0C]", v1);

    /* (b) W1C: write 1 to bit 0 */
    *scfg = v1 | 1u;
    uint32_t v2 = *scfg;
    dump("after write (val | 1)   [W1C]", v2);

    /* (c) W0C again */
    *scfg = v2 & ~1u;
    uint32_t v3 = *scfg;
    dump("after write (val & ~1)  [W0C again]", v3);

    /* (d) settle */
    usleep(2000);
    uint32_t v4 = *scfg;
    dump("after 2 ms settle", v4);

    printf("\n=== INTERPRETATION ===\n");
    /* Track WHICH write cleared bit0, to distinguish W1C from W0C.
       v1 = after write-0 (W0C attempt)
       v2 = after write-1 (W1C attempt)
       v3 = after write-0 again */
    if ((v1 & 1u) == 0)
        printf("W0C works: bit0 cleared by writing 0. FIX = write 0 to bit0 (&= ~1u).\n");
    else if ((v2 & 1u) == 0)
        printf("W1C works: bit0 cleared ONLY by writing 1. FIX = write 1 to bit0 (|= 1u).\n");
    else
        printf("NEITHER write clears bit0 -> it is a status-only / PRCM-gated bit; "
               "need to wake the PRU-ICSS power domain, not write the register.\n");

    munmap(m, MAP_SIZE);
    close(fd);
    return 0;
}

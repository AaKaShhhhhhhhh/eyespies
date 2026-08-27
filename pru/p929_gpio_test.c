/*
 * p929_gpio_test.c  --  bisect diagnostic for the P9_29 servo silence.
 *
 * Does NOT use the PRU. It drives P9_29 (ball R28 = GPIO3_21) directly from
 * the ARM GPIO controller at ~50 Hz for a few seconds, so we can tell whether
 * the WIRE + SERVO + PIN itself respond to a 3.3V square wave on P9_29.
 *
 *   If the servo MOVES  -> P9_29 wiring/servo are fine; the problem is in the
 *                          PRU output path (pin ownership, mux-mode, firmware).
 *   If the servo does NOT move -> the signal wire is not actually on P9_29 (or
 *                          the servo / power is dead); fix wiring first.
 *
 * Build:  gcc -O2 -o p929_gpio_test p929_gpio_test.c
 * Run:    sudo ./p929_gpio_test [seconds]   (default 4)
 *
 * Note: leaves P9_29 in GPIO mode (0x47). To go back to PRU mode, re-run
 *       arm_write_p929 (or reboot -- uEnv sets mode 4 at boot).
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#define CONTROL_MODULE_PHYS 0x44E10000u
#define CONF_OFF_P929       0x9BCu      /* P9_29 conf register offset        */
#define GPIO3_PHYS          0x481AE000u /* GPIO3 bank base (AM335x)          */
#define GPIO_OE             0x134u      /* output enable: 0 = output         */
#define GPIO_SETDATAOUT     0x194u
#define GPIO_CLEARDATAOUT   0x190u
#define GPIO3_21_BIT        (1u << 21)  /* GPIO3_21 = P9_29                  */
#define MAP_SIZE            0x1000u

int main(int argc, char **argv)
{
    int seconds = (argc >= 2) ? atoi(argv[1]) : 4;
    if (seconds < 1) seconds = 4;

    int fd = open("/dev/mem", O_RDWR);
    if (fd < 0) { perror("open /dev/mem"); return 1; }

    /* (1) Mux P9_29 to GPIO mode 7, pull disabled, output buffer enabled. */
    void *cm = mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
                    fd, CONTROL_MODULE_PHYS);
    if (cm == MAP_FAILED) { perror("mmap control module"); close(fd); return 1; }
    volatile uint32_t *conf = (volatile uint32_t *)((char *)cm + CONF_OFF_P929);
    uint32_t before = *conf;
    *conf = 0x47u;                       /* mode7, pull-disable, output */
    uint32_t after = *conf;
    munmap(cm, MAP_SIZE);
    printf("P9_29 mux: before 0x%08X -> wrote 0x47 (GPIO mode7) -> readback 0x%08X\n",
           before, after);

    /* (2) Toggle GPIO3_21 at ~50 Hz from the ARM GPIO controller. */
    void *g = mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
                   fd, GPIO3_PHYS);
    if (g == MAP_FAILED) { perror("mmap gpio3"); close(fd); return 1; }
    volatile uint32_t *oe  = (volatile uint32_t *)((char *)g + GPIO_OE);
    volatile uint32_t *set = (volatile uint32_t *)((char *)g + GPIO_SETDATAOUT);
    volatile uint32_t *clr = (volatile uint32_t *)((char *)g + GPIO_CLEARDATAOUT);
    *oe &= ~GPIO3_21_BIT;                /* make GPIO3_21 an output */

    printf("Toggling GPIO3_21 (P9_29) at ~50 Hz for %d s...\n", seconds);
    for (int i = 0; i < seconds * 50; i++) {
        *set = GPIO3_21_BIT;             /* HIGH ~1 ms */
        usleep(1000);
        *clr = GPIO3_21_BIT;             /* LOW  ~9 ms (loop overhead ~50 Hz) */
        usleep(9000);
    }
    *clr = GPIO3_21_BIT;                 /* leave LOW */
    printf("Done. servo MOVED  -> P9_29 wired correctly; PRU path is the issue.\n");
    printf("      servo STILL  -> signal wire not on P9_29 (or servo/power dead).\n");

    munmap(g, MAP_SIZE);
    close(fd);
    return 0;
}

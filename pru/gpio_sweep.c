/*
 * gpio_sweep.c — eliminate jumper-placement ambiguity.
 *
 * Drives ONE gpio line and discovers which OTHER line(s) on the same chip
 * follow it. Instead of us guessing how the user seated the jumper, the tool
 * REPORTS which pin the wire connects to.
 *
 * Build:  gcc -O2 -Wall -o gpio_sweep gpio_sweep.c -lgpiod
 * Usage:  sudo ./gpio_sweep <chip> <drive_line> <seconds>
 *   e.g.  sudo ./gpio_sweep gpiochip1 28 4     # drive P8_13, find its partner
 *         sudo ./gpio_sweep gpiochip1 15 4     # drive P8_15, find its partner
 *
 * Output: list of lines that read 1 when driven high and 0 when driven low
 *         (i.e. physically connected through the jumper).
 *
 * NOTE: AM335x GPIO banks have 32 lines; we scan 0..31. Lines claimed by the
 * kernel (eMMC, LEDs, buttons) can't be requested as input and are skipped.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <gpiod.h>

#define NLINES 32
#define REPS 2

static int claim_input(struct gpiod_chip *c, unsigned off, struct gpiod_line **lp)
{
    struct gpiod_line *l = gpiod_chip_get_line(c, off);
    if (!l)
        return -1;
    if (gpiod_line_request_input(l, "gpio_sweep")) {
        gpiod_line_release(l);
        return -1;
    }
    *lp = l;
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: %s <chip> <drive_line> <seconds>\n", argv[0]);
        return 2;
    }
    const char *chipname = argv[1];
    unsigned drive = (unsigned)atoi(argv[2]);
    int seconds = atoi(argv[3]);
    (void)seconds; /* scan cost is fixed (~REPS*NLINES); arg kept for parity */

    struct gpiod_chip *chip = gpiod_chip_open_by_name(chipname);
    if (!chip) {
        perror("gpiod_chip_open_by_name");
        return 1;
    }

    struct gpiod_line *d = gpiod_chip_get_line(chip, drive);
    if (!d) {
        fprintf(stderr, "bad drive line %u\n", drive);
        gpiod_chip_close(chip);
        return 1;
    }
    if (gpiod_line_request_output(d, "gpio_sweep", 0)) {
        fprintf(stderr, "FAIL request OUTPUT %s:%u (muxed away?)\n", chipname, drive);
        gpiod_line_release(d);
        gpiod_chip_close(chip);
        return 1;
    }

    int *follow = calloc(NLINES, sizeof(int));

    for (int r = 0; r < REPS; r++) {
        for (unsigned c = 0; c < NLINES; c++) {
            if (c == drive)
                continue;
            struct gpiod_line *l = NULL;
            if (claim_input(chip, c, &l))
                continue; /* unavailable (kernel-claimed) -> skip */
            gpiod_line_set_value(d, 1);
            usleep(150000);
            int hi = gpiod_line_get_value(l);
            gpiod_line_set_value(d, 0);
            usleep(150000);
            int lo = gpiod_line_get_value(l);
            gpiod_line_release(l);
            if (hi == 1 && lo == 0)
                follow[c]++;
        }
    }

    printf("Drove %s:%u. Lines that followed (hi=1, lo=0) over %d reps:\n",
           chipname, drive, REPS);
    int found = 0;
    for (unsigned c = 0; c < NLINES; c++) {
        if (follow[c] == REPS) {
            printf("  %s:%u  (gpio1 line %u)\n", chipname, c, c);
            found++;
        }
    }
    if (!found)
        printf("  NONE. This pin is not connected through the jumper to any "
               "other GPIO1 line.\n");

    free(follow);
    gpiod_line_release(d);
    gpiod_chip_close(chip);
    return 0;
}

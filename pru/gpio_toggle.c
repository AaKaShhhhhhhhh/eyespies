/*
 * gpio_toggle.c
 * ---------------------------------------------------------------------------
 * Drives a KNOWN-GOOD GPIO line HIGH/LOW at 5 Hz for a few seconds.
 *
 * PURPOSE: self-test the P9_29 -> P8_13 loopback rig.
 *   The loopback_probe read on P8_13 (gpiochip1 line 14) returned all zeros
 *   when PRU drove P9_29. That could mean (a) the jumper wasn't connected,
 *   or (b) P9_29 really isn't driving. To tell them apart, drive a pin we
 *   KNOW Linux can toggle (P8_15 = gpiochip1 line 15, default GPIO mode),
 *   jumper it to P8_13, and run loopback_probe in another shell.
 *     - if loopback_probe sees transitions -> rig (jumper + P8_13 input + tool)
 *       is proven good, so the earlier P9_29=0 is a REAL result.
 *     - if loopback_probe stays 0 -> the rig itself is broken (bad wire, wrong
 *       mapping); fix the rig before trusting any loopback result.
 *
 * Build:  gcc -O2 -Wall -o gpio_toggle gpio_toggle.c -lgpiod
 * Run:    sudo ./gpio_toggle <chip> <line> <seconds>
 *   e.g.  sudo ./gpio_toggle gpiochip1 15 6     (drive P8_15 for 6 s)
 *   then in another shell:  sudo ./loopback_probe 6
 * ---------------------------------------------------------------------------
 */

#include <gpiod.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s <chip> <line> <seconds>\n", argv[0]);
        fprintf(stderr, "  e.g. sudo %s gpiochip1 15 6   (drive P8_15 for 6 s)\n",
                argv[0]);
        return 1;
    }

    const char *chip_name = argv[1];
    unsigned int line_off = (unsigned int)strtoul(argv[2], NULL, 10);
    int seconds = atoi(argv[3]);
    if (seconds < 1) seconds = 1;

    struct gpiod_chip *chip = gpiod_chip_open_by_name(chip_name);
    if (!chip) { perror("gpiod_chip_open_by_name"); return 1; }

    struct gpiod_line *line = gpiod_chip_get_line(chip, line_off);
    if (!line) { perror("gpiod_chip_get_line"); gpiod_chip_close(chip); return 1; }

    if (gpiod_line_request_output(line, "gpio-toggle", 0) < 0) {
        perror("gpiod_line_request_output");
        gpiod_chip_close(chip);
        return 1;
    }

    printf("Toggling %s line %u at 5 Hz for %d s. Jumper this pin to P8_13 "
           "and run 'sudo ./loopback_probe %d' in another shell.\n",
           chip_name, line_off, seconds, seconds);
    fflush(stdout);

    const double step = 0.1;                 /* 100 ms -> 5 Hz square */
    int n = (int)(seconds / step);
    int v = 0;
    for (int i = 0; i < n; i++) {
        v ^= 1;
        gpiod_line_set_value(line, v);
        usleep((useconds_t)(step * 1e6));
    }

    gpiod_line_release(line);
    gpiod_chip_close(chip);
    printf("done.\n");
    return 0;
}

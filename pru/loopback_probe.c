#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <gpiod.h>

/* Reads a GPIO input pin for N seconds and counts level transitions.
   Usage:  sudo ./loopback_probe [seconds]
   Default: 6 s. Targets P8_13 = GPIO1_28 (gpiochip1 line 28) by default.
   Run AFTER jumpering P9_29 (PRU blink) -> P8_13, with P8_13 muxed to
   GPIO mode 7 (input). A 5 Hz PRU blink should produce ~30 transitions. */

int main(int argc, char **argv)
{
    const char *chip = "gpiochip1"; /* P8_13 = GPIO1_28 */
    int line = 28;
    int secs = 6;
    if (argc > 1) secs = atoi(argv[1]);
    if (secs < 1) secs = 6;

    struct gpiod_chip *c = gpiod_chip_open_by_name(chip);
    if (!c) { fprintf(stderr, "open %s failed\n", chip); return 1; }
    struct gpiod_line *l = gpiod_chip_get_line(c, line);
    if (!l) { fprintf(stderr, "get line %d failed\n", line); gpiod_chip_close(c); return 1; }
    if (gpiod_line_request_input(l, "loopback")) {
        fprintf(stderr, "request input failed (is P8_13 muxed to GPIO mode 7?)\n");
        gpiod_line_release(l); gpiod_chip_close(c); return 1;
    }

    int prev = -1, changes = 0, t = 0;
    while (t < secs * 10) {
        int v = gpiod_line_get_value(l);
        if (v < 0) { fprintf(stderr, "read error\n"); break; }
        printf("t=%.1fs value=%d\n", t / 10.0, v);
        if (prev >= 0 && v != prev) changes++;
        prev = v;
        usleep(100000);
        t++;
    }

    printf("total transitions: %d\n", changes);
    if (changes > 3)
        printf("LOOPBACK OK: P9_29 GPO reaches the pad (r30.1 drives P9_29).\n");
    else
        printf("LOOPBACK DEAD: P9_29 pad never toggled -> r30.1 not reaching the pad.\n");

    gpiod_line_release(l);
    gpiod_chip_close(c);
    return 0;
}

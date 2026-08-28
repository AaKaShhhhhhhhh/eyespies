#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <gpiod.h>

/* Reads a GPIO input pin for N seconds and counts level transitions.
 *
 * This is the READ side of a loopback test. It is pin-generic: pass the chip
 * and line to read. When the PRU (or gpio_toggle / loopback_self) drives the
 * OTHER end of a jumpered wire, this should see transitions.
 *
 * USAGE:
 *   sudo ./loopback_probe [seconds] [chip] [line]
 *     seconds : how long to sample (default 6)
 *     chip    : gpiochip name (default gpiochip1 = GPIO1, the P8 header bank)
 *     line    : line offset within the chip (default 28 = GPIO1_28 = P8_13)
 *
 * Sampling is at ~500 Hz (2 ms) so a 5 Hz transmitter cannot be aliased away.
 * Run the transmitter (PRU firmware / loopback_self) WHILE this runs -- ideally
 * start this in the background and load the PRU in another shell.
 */

int main(int argc, char **argv)
{
    int secs = 6;
    const char *chip = "gpiochip1";
    int line = 28; /* GPIO1_28 = P8_13 */

    if (argc > 1) secs = atoi(argv[1]);
    if (secs < 1) secs = 6;
    if (argc > 2) chip = argv[2];
    if (argc > 3) line = atoi(argv[3]);

    struct gpiod_chip *c = gpiod_chip_open_by_name(chip);
    if (!c) { fprintf(stderr, "open %s failed\n", chip); return 1; }
    struct gpiod_line *l = gpiod_chip_get_line(c, line);
    if (!l) { fprintf(stderr, "get line %d failed\n", line); gpiod_chip_close(c); return 1; }
    if (gpiod_line_request_input(l, "loopback") < 0) {
        fprintf(stderr, "request input failed (is %s:%d muxed to GPIO mode 7?)\n",
                chip, line);
        gpiod_line_release(l); gpiod_chip_close(c); return 1;
    }

    printf("Reading %s:%d for %d s at ~500 Hz...\n", chip, line, secs);
    fflush(stdout);

    int prev = -1, changes = 0;
    const int samples_per_sec = 500;
    const int total = secs * samples_per_sec;
    for (int i = 0; i < total; i++) {
        int v = gpiod_line_get_value(l);
        if (v < 0) { fprintf(stderr, "read error\n"); break; }
        if (prev >= 0 && v != prev) changes++;
        prev = v;
        usleep(2000);
    }

    printf("total transitions on %s:%d: %d\n", chip, line, changes);
    if (changes > 3)
        printf("LOOPBACK OK: signal reaches this pin through the jumper/wire.\n");
    else
        printf("LOOPBACK DEAD: %s:%d never toggled -> nothing is driving that line.\n",
               chip, line);

    gpiod_line_release(l);
    gpiod_chip_close(c);
    return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <gpiod.h>

/* Read-only loopback read side.
 * ---------------------------------------------------------------------------
 * Samples an input pin and counts transitions. Pin-generic.
 *
 * USAGE:
 *   sudo ./loopback_probe [secs] [chip] [line]
 *     secs : how long to sample (default 6)
 *     chip : gpiochip name (default gpiochip1 = GPIO1, the P8 header bank)
 *     line : line offset (default 28 = GPIO1_28 = P8_13)
 *
 * Samples at ~500 Hz (2 ms) so a 5 Hz transmitter cannot be aliased away.
 * Prints raw value range + any read errors.
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
    if (!c) { fprintf(stderr, "FAIL open %s\n", chip); return 1; }
    struct gpiod_line *l = gpiod_chip_get_line(c, line);
    if (!l) { fprintf(stderr, "FAIL get line %d\n", line);
              gpiod_chip_close(c); return 1; }
    if (gpiod_line_request_input(l, "loopback") < 0) {
        fprintf(stderr,
                "FAIL request INPUT %s:%d (muxed away / already claimed?)\n",
                chip, line);
        gpiod_line_release(l); gpiod_chip_close(c); return 1;
    }

    printf("Reading %s:%d for %d s at ~500 Hz...\n", chip, line, secs);
    fflush(stdout);

    int prev = -1, changes = 0, reads = 0, errs = 0, vmin = 1, vmax = 0;
    const int sps = 500;
    int total = secs * sps;
    for (int i = 0; i < total; i++) {
        int v = gpiod_line_get_value(l);
        if (v < 0) {
            errs++;
            if (errs <= 3) fprintf(stderr, "  read error\n");
        } else {
            reads++;
            if (v < vmin) vmin = v;
            if (v > vmax) vmax = v;
            if (prev >= 0 && v != prev) changes++;
            prev = v;
            if (i < 10) printf("  t=%.2fs value=%d\n", (double)i / sps, v);
        }
        usleep(2000);
    }
    printf("reads=%d errors=%d range=[%d..%d] transitions=%d\n",
           reads, errs, vmin, vmax, changes);
    if (errs > 0)
        printf("READ ERRORS on %s:%d.\n", chip, line);
    else if (changes > 3)
        printf("LOOPBACK OK: signal reaches this pin through the jumper/wire.\n");
    else
        printf("LOOPBACK DEAD: %s:%d never toggled (range %d..%d).\n",
               chip, line, vmin, vmax);

    gpiod_line_release(l);
    gpiod_chip_close(c);
    return 0;
}

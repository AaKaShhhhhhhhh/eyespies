#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <gpiod.h>

/* Self-contained loopback / control test.
 * ---------------------------------------------------------------------------
 * Drives OUT (chip:line) at ~5 Hz and reads IN (chip:line) in the SAME loop,
 * on the same process. No two-shell timing race, no sampler aliasing.
 *
 * USAGE:
 *   sudo ./loopback_self <out_chip> <out_line> <in_chip> <in_line> <secs>
 *     e.g. sudo ./loopback_self gpiochip1 15 gpiochip1 28 6
 *          (drive P8_15, read P8_13)
 *
 * It now prints raw sampled values + a value RANGE + any read errors, so a
 * real pin fault (mux not GPIO / permission) is distinguishable from a merely
 * open wire.
 */

int main(int argc, char **argv)
{
    if (argc < 6) {
        fprintf(stderr,
                "USAGE: %s <out_chip> <out_line> <in_chip> <in_line> <secs>\n",
                argv[0]);
        return 2;
    }
    const char *ochip = argv[1];
    int oline = atoi(argv[2]);
    const char *ichip = argv[3];
    int iline = atoi(argv[4]);
    int secs = atoi(argv[5]);
    if (secs < 1) secs = 6;

    /* --- output side --- */
    struct gpiod_chip *oc = gpiod_chip_open_by_name(ochip);
    if (!oc) { fprintf(stderr, "FAIL open output chip %s\n", ochip); return 1; }
    struct gpiod_line *ol = gpiod_chip_get_line(oc, oline);
    if (!ol) { fprintf(stderr, "FAIL get output line %d\n", oline);
               gpiod_chip_close(oc); return 1; }
    if (gpiod_line_request_output(ol, "loopback_drv", 0) < 0) {
        fprintf(stderr,
                "FAIL request OUTPUT on %s:%d (pin muxed away from GPIO?)\n",
                ochip, oline);
        gpiod_line_release(ol); gpiod_chip_close(oc); return 1;
    }

    /* --- input side --- */
    struct gpiod_chip *ic = gpiod_chip_open_by_name(ichip);
    if (!ic) { fprintf(stderr, "FAIL open input chip %s\n", ichip);
               gpiod_line_release(ol); gpiod_chip_close(oc); return 1; }
    struct gpiod_line *il = gpiod_chip_get_line(ic, iline);
    if (!il) { fprintf(stderr, "FAIL get input line %d\n", iline);
               gpiod_line_release(ol); gpiod_chip_close(ic);
               gpiod_chip_close(oc); return 1; }
    if (gpiod_line_request_input(il, "loopback_rd") < 0) {
        fprintf(stderr,
                "FAIL request INPUT on %s:%d (muxed away / already claimed?)\n",
                ichip, iline);
        gpiod_line_release(ol); gpiod_line_release(il);
        gpiod_chip_close(ic); gpiod_chip_close(oc); return 1;
    }

    printf("Self-loopback: drive %s:%d  read %s:%d  for %d s @ 5 Hz\n",
           ochip, oline, ichip, iline, secs);
    fflush(stdout);

    int prev = -1, changes = 0, reads = 0, errs = 0, vmin = 1, vmax = 0;
    for (int i = 0; i < secs * 5; i++) {
        int out = (i % 2) ? 1 : 0;
        gpiod_line_set_value(ol, out);
        for (int k = 0; k < 5; k++) {
            int v = gpiod_line_get_value(il);
            if (v < 0) {
                errs++;
                if (errs <= 3)
                    fprintf(stderr, "  read error on %s:%d\n", ichip, iline);
            } else {
                reads++;
                if (v < vmin) vmin = v;
                if (v > vmax) vmax = v;
                if (prev >= 0 && v != prev) changes++;
                prev = v;
                if (i < 2) printf("  sample out=%d in=%d\n", out, v);
            }
            usleep(20000);
        }
    }
    printf("reads=%d errors=%d  raw-range=[%d..%d]  transitions=%d\n",
           reads, errs, vmin, vmax, changes);
    if (errs > 0)
        printf("READ ERRORS: libgpiod could not sample the input pin "
               "(check mux / permissions).\n");
    else if (changes > 3)
        printf("RIG OK: drive pin reaches read pin through the jumper.\n");
    else
        printf("RIG DEAD: read pin never toggled (range %d..%d). "
               "Open wire / bad seat / mux.\n", vmin, vmax);

    gpiod_line_release(ol); gpiod_line_release(il);
    gpiod_chip_close(oc); gpiod_chip_close(ic);
    return 0;
}

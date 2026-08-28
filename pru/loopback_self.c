#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <gpiod.h>

/* Self-contained loopback / control test.
 * Drives OUT at ~5 Hz and reads IN in the SAME loop, same process.
 * Uses ONE chip handle (the pattern gpio_sweep proved works on this board),
 * not two separate opens of the same chip. Settles 30 ms after each drive
 * before sampling.
 * USAGE: sudo ./loopback_self <out_chip> <out_line> <in_chip> <in_line> <secs>
 *   e.g. sudo ./loopback_self gpiochip1 15 gpiochip1 28 6
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

    struct gpiod_chip *oc = gpiod_chip_open_by_name(ochip);
    if (!oc) { fprintf(stderr, "FAIL open chip %s\n", ochip); return 1; }
    struct gpiod_line *ol = gpiod_chip_get_line(oc, oline);
    if (!ol) { fprintf(stderr, "FAIL get output line %d\n", oline);
               gpiod_chip_close(oc); return 1; }
    if (gpiod_line_request_output(ol, "lb_drv", 0) < 0) {
        fprintf(stderr, "FAIL request OUTPUT %s:%d (muxed away?)\n", ochip, oline);
        gpiod_line_release(ol); gpiod_chip_close(oc); return 1;
    }

    /* If in == out chip, reuse the same handle (proven-good pattern). */
    struct gpiod_chip *ic = oc;
    if (strcmp(ichip, ochip)) {
        ic = gpiod_chip_open_by_name(ichip);
        if (!ic) { fprintf(stderr, "FAIL open chip %s\n", ichip);
                   gpiod_line_release(ol); gpiod_chip_close(oc); return 1; }
    }
    struct gpiod_line *il = gpiod_chip_get_line(ic, iline);
    if (!il) { fprintf(stderr, "FAIL get input line %d\n", iline);
               gpiod_line_release(ol); gpiod_chip_close(oc);
               if (ic != oc) { gpiod_chip_close(ic); } return 1; }
    if (gpiod_line_request_input(il, "lb_rd") < 0) {
        fprintf(stderr, "FAIL request INPUT %s:%d (muxed/claimed?)\n", ichip, iline);
        gpiod_line_release(ol); gpiod_line_release(il);
        gpiod_chip_close(oc); if (ic != oc) gpiod_chip_close(ic); return 1;
    }

    printf("Self-loopback: drive %s:%d  read %s:%d  for %d s @ 5 Hz\n",
           ochip, oline, ichip, iline, secs);
    fflush(stdout);

    int prev = -1, changes = 0, reads = 0, errs = 0, vmin = 1, vmax = 0;
    for (int i = 0; i < secs * 5; i++) {
        int out = (i % 2) ? 1 : 0;
        gpiod_line_set_value(ol, out);
        usleep(30000); /* settle so the driven level reaches the read pin */
        for (int k = 0; k < 5; k++) {
            int v = gpiod_line_get_value(il);
            if (v < 0) {
                errs++;
                if (errs <= 3) fprintf(stderr, "  read error %s:%d\n", ichip, iline);
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
        printf("READ ERRORS: libgpiod could not sample the input pin.\n");
    else if (changes > 3)
        printf("RIG OK: drive pin reaches read pin through the jumper.\n");
    else
        printf("RIG DEAD: drive pin toggled 0..1 but read pin stayed %d..%d.\n"
               "  -> jumper not conducting / mis-seated, OR a real read-pin fault.\n"
               "     To distinguish, run: sudo ./gpio_sweep %s %d 4\n"
               "     (if sweep finds a partner, the wiring is fine and this is a\n"
               "      tool bug; if sweep finds NONE, the read pin is at fault.)\n",
               vmin, vmax, ochip, oline);

    gpiod_line_release(ol); gpiod_line_release(il);
    gpiod_chip_close(oc); if (ic != oc) gpiod_chip_close(ic);
    return 0;
}

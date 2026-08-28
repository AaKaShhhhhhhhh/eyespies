/*
 * loopback_self.c  --  self-contained loopback / control test
 * ---------------------------------------------------------------------------
 * Drives an OUTPUT pin and reads it back on a separate INPUT pin in the SAME
 * process and the SAME loop. This removes the two-shell timing race and the
 * sampler/transmitter aliasing that plagued the earlier gpio_toggle +
 * loopback_probe split test.
 *
 *   - It drives at 5 Hz (100 ms period).
 *   - Within each 100 ms step it reads the input 5 times (~18 ms apart),
 *     so edges can never be missed (no aliasing).
 *   - Counts transitions seen on the READ pin.
 *
 * CONNECT A JUMPER BETWEEN THE TWO PINS, then run ONE command:
 *
 *   Control (known-good Linux pins):
 *       sudo ./loopback_self gpiochip1 15 gpiochip1 28 6
 *       (drive P8_15, read back P8_13)
 *         -> ~30 transitions  => RIG OK (wire + P8_13 input + tool all good)
 *         -> 0 transitions    => RIG DEAD (bad wire / wrong pins / P8_13 fault)
 *
 *   Once the rig is proven, the REAL test is the PRU driving P9_29 while this
 *   tool is NOT used; instead use loopback_probe to watch P8_13 while the PRU
 *   firmware (gpo_self_test / pru0_servo) is loaded.
 *
 * Build:  gcc -O2 -Wall -o loopback_self loopback_self.c -lgpiod
 * ---------------------------------------------------------------------------
 */

#include <gpiod.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 6) {
        fprintf(stderr,
                "usage: %s <drive_chip> <drive_line> <read_chip> <read_line> <seconds>\n",
                argv[0]);
        fprintf(stderr, "  e.g. sudo %s gpiochip1 15 gpiochip1 28 6\n", argv[0]);
        return 1;
    }

    const char *dchip = argv[1];
    unsigned int dline = (unsigned int)strtoul(argv[2], NULL, 10);
    const char *rchip = argv[3];
    unsigned int rline = (unsigned int)strtoul(argv[4], NULL, 10);
    int seconds = atoi(argv[5]);
    if (seconds < 1) seconds = 1;

    struct gpiod_chip *dc = gpiod_chip_open_by_name(dchip);
    if (!dc) { perror("drive chip open"); return 1; }
    struct gpiod_line *dl = gpiod_chip_get_line(dc, dline);
    if (!dl) { perror("drive line"); gpiod_chip_close(dc); return 1; }
    if (gpiod_line_request_output(dl, "loopback-self", 0) < 0) {
        perror("drive request_output");
        gpiod_chip_close(dc);
        return 1;
    }

    struct gpiod_chip *rc = gpiod_chip_open_by_name(rchip);
    if (!rc) { perror("read chip open"); gpiod_line_release(dl); gpiod_chip_close(dc); return 1; }
    struct gpiod_line *rl = gpiod_chip_get_line(rc, rline);
    if (!rl) { perror("read line"); gpiod_chip_close(rc); gpiod_line_release(dl); gpiod_chip_close(dc); return 1; }
    if (gpiod_line_request_input(rl, "loopback-self") < 0) {
        fprintf(stderr, "read request_input failed (is %s:%u muxed to GPIO mode 7?)\n",
                rchip, rline);
        gpiod_chip_close(rc); gpiod_line_release(dl); gpiod_chip_close(dc);
        return 1;
    }

    printf("Self-loopback: drive %s:%u  read %s:%u  for %d s @ 5 Hz\n",
           dchip, dline, rchip, rline, seconds);
    fflush(stdout);

    int v = 0, prev = -1, changes = 0;
    const int steps = seconds * 10;        /* 10 steps/s -> 100 ms drive period */
    for (int i = 0; i < steps; i++) {
        v ^= 1;
        gpiod_line_set_value(dl, v);
        for (int j = 0; j < 5; j++) {      /* sample read side 5x/step (~18 ms) */
            int rv = gpiod_line_get_value(rl);
            if (rv < 0) { fprintf(stderr, "read error\n"); i = steps; break; }
            if (prev >= 0 && rv != prev) changes++;
            prev = rv;
            usleep(18000);
        }
    }

    printf("total transitions on read pin: %d\n", changes);
    if (changes > 3)
        printf("RIG OK: drive pin reaches read pin through the jumper.\n");
    else
        printf("RIG DEAD: read pin never toggled -> jumper/wire or read-pin fault.\n");

    gpiod_line_release(rl); gpiod_chip_close(rc);
    gpiod_line_release(dl); gpiod_chip_close(dc);
    return 0;
}

/*
 * gpio_hold.c
 * ---------------------------------------------------------------------------
 * Keeps a GPIO line REQUESTED from Linux and sleeps forever.
 *
 * WHY THIS EXISTS (P9_16 / GPIO0_19 case):
 *   The userspace servo path (servo_pwm_test.c, pmw_servo.c) drives P9_16 via
 *   libgpiod. When the PRU drives a pin over its GPIO-block OCP writes (e.g.
 *   the pru0_servo firmware on P9_16), the PRU alone cannot keep the pad
 *   "alive": Linux owns the pad mux and the GPIO0 clock, and if no Linux
 *   process holds the line the kernel may power-gate the bank, so the PRU's
 *   writes land in a dead module. So:
 *     1. run THIS program (holds gpiochip0 line 19 = P9_16 as output) -> clock+mux on
 *     2. THEN load the PRU firmware (it pokes the same DATAOUT reg) -> reaches wire
 *
 * NOTE (P9_29): the pru1_servo.pru1.c firmware does NOT need this helper. It
 * drives P9_29 via the PRU's r30 direct output and reprograms its OWN pinmux
 * to PRU mode 4 over OCP, so it is fully self-contained. gpio_hold is only
 * relevant for the OCP-GPIO-block driven pins (P9_16/GPIO0) or for testing.
 *
 * Pin mapping (BeagleBone Black, from the SRM header table):
 *   P9_29 = GPIO3_21 -> gpiochip3 line 23   (driven by PRU r30, no holder)
 *   P9_16 = GPIO0_19 -> gpiochip0 line 19   (driven by PRU OCP / gpiod)
 *   P9_14 = GPIO0_18 -> gpiochip0 line 18
 *
 * Build:  gcc -O2 -Wall -o gpio_hold gpio_hold.c -lgpiod
 * Run:    sudo ./gpio_hold            # defaults to P9_16 (gpiochip0 line 19)
 *   or:    sudo ./gpio_hold gpiochip3 23   # hold P9_29 instead (if ever needed)
 * Stop:   kill the gpio_hold process; it releases the line on exit.
 * ---------------------------------------------------------------------------
 */

#include <gpiod.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

static struct gpiod_line *line;

static void on_signal(int sig)
{
    (void)sig;
    if (line)
        gpiod_line_release(line);   /* give the pin back to Linux cleanly */
    _exit(0);
}

int main(int argc, char **argv)
{
    const char *chip_name = "gpiochip0";   /* default: P9_16 = GPIO0_19 */
    unsigned int line_off = 19;            /* default: GPIO0_19 */

    if (argc >= 3) {
        chip_name = argv[1];
        line_off  = (unsigned int)strtoul(argv[2], NULL, 10);
    } else if (argc != 1) {
        fprintf(stderr, "usage: %s [<chip> <line>]\n", argv[0]);
        fprintf(stderr, "  default (no args): gpiochip0 line 19  (P9_16 / GPIO0_19)\n");
        return 1;
    }

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    struct gpiod_chip *chip = gpiod_chip_open_by_name(chip_name);
    if (!chip) {
        perror("gpiod_chip_open_by_name");
        return 1;
    }

    line = gpiod_chip_get_line(chip, line_off);
    if (!line) {
        perror("gpiod_chip_get_line");
        gpiod_chip_close(chip);
        return 1;
    }

    if (gpiod_line_request_output(line, "pru-servo-hold", 0) < 0) {
        perror("gpiod_line_request_output");
        gpiod_chip_close(chip);
        return 1;
    }

    printf("Holding %s line %u as output. Load the PRU firmware in another "
           "shell, then Ctrl-C to release.\n", chip_name, line_off);
    fflush(stdout);

    /* Sleep forever; the requested line keeps the clock + mux alive. */
    while (1)
        pause();

    return 0;   /* unreachable */
}

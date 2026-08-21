/*
 * gpio_hold.c
 * ---------------------------------------------------------------------------
 * Keeps a GPIO line REQUESTED from Linux and sleeps forever.
 *
 * WHY THIS MATTERS:
 *   Our PRU servo firmware writes to the GPIO0 registers, but the PRU alone
 *   cannot keep the pin "alive": Linux owns the pad mux + the GPIO0 clock, and
 *   turns both OFF the moment no Linux process is using the line. So:
 *     1. run THIS program (it holds gpiochip0 line 19 as output)   -> clock+mux on
 *     2. THEN load the PRU firmware (it pokes the same DATAOUT reg) -> reaches wire
 *   Without step 1 the PRU writes go into a dead module and nothing moves.
 *
 * Simplest alternative is just:  gpioset -b gpiochip0 19=0
 * (libgpiod's "background" mode holds the line open and sleeps forever too.)
 *
 * Build:  gcc -O2 -Wall -o gpio_hold gpio_hold.c -lgpiod
 * Run:    sudo ./gpio_hold
 *         (then, in another shell: sudo ./load_pru0.sh pru0_servo.out)
 * Stop:   kill the gpio_hold process; it releases the line on exit.
 * ---------------------------------------------------------------------------
 */

#include <gpiod.h>
#include <stdio.h>
#include <stdlib.h>
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

int main(void)
{
    struct gpiod_chip *chip;
    int ret;

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    chip = gpiod_chip_open_by_name("gpiochip0");
    if (!chip) {
        perror("gpiod_chip_open_by_name gpiochip0");
        return 1;
    }

    line = gpiod_chip_get_line(chip, 19);   /* GPIO0_19 -- the servo */
    if (!line) {
        perror("gpiod_chip_get_line 19");
        gpiod_chip_close(chip);
        return 1;
    }

    ret = gpiod_line_request_output(line, "pru-servo-hold", 0);
    if (ret < 0) {
        perror("gpiod_line_request_output");
        gpiod_chip_close(chip);
        return 1;
    }

    printf("Holding gpiochip0 line 19 (GPIO0_19) as output. "
           "Load the PRU firmware in another shell, then Ctrl-C to release.\n");
    fflush(stdout);

    /* Sleep forever; the requested line keeps the clock + mux alive. */
    while (1)
        pause();

    return 0;   /* unreachable */
}

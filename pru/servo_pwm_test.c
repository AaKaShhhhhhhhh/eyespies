/*
 * servo_pwm_test.c — userspace (Linux libgpiod) servo test for P9_16.
 *
 * WHY THIS EXISTS:
 *   We need to prove the PIN + SERVO + WIRING work WITHOUT involving the PRU.
 *   P9_16 = GPIO1_19 = gpiochip0 line 19 on the BeagleBone Black.
 *
 *   If this makes the servo sweep/whir, then pin+servo+wiring are GOOD and the
 *   only remaining problem was the PRU firmware/loader. If it does NOTHING,
 *   then P9_16 is not muxed as GPIO (device-tree issue) or the wiring is wrong.
 *
 * IMPORTANT: stop the PRU first so it isn't also driving the pin:
 *   echo stop | sudo tee /sys/class/remoteproc/remoteproc1/state   # only if running
 *
 * BUILD on BBB:   gcc servo_pwm_test.c -o servo_pwm_test -lgpiod
 * RUN:            sudo ./servo_pwm_test
 *
 * It sends 50 Hz PWM (20 ms period). The pulse width sweeps 1.0 ms -> 2.0 ms
 * over ~6 seconds, so a healthy servo should visibly sweep end-to-end and
 * whir/click as it moves.
 */
#include <gpiod.h>
#include <stdio.h>
#include <time.h>

/* Busy-wait spin for `us` microseconds (good enough for servo timing). */
static void busy_sleep_us(unsigned long us) {
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);
    do {
        clock_gettime(CLOCK_MONOTONIC, &now);
    } while ((now.tv_sec - start.tv_sec) * 1000000UL
             + (now.tv_nsec - start.tv_nsec) / 1000UL < us);
}

int main(void) {
    struct gpiod_chip *chip = gpiod_chip_open_by_name("gpiochip0");
    if (!chip) { perror("gpiod_chip_open_by_name"); return 1; }

    struct gpiod_line *line = gpiod_chip_get_line(chip, 19);
    if (!line) { perror("gpiod_chip_get_line"); gpiod_chip_close(chip); return 1; }

    if (gpiod_line_request_output(line, "servo_test", 0) < 0) {
        perror("gpiod_line_request_output (is P9_16 muxed as GPIO? is PRU stopped?)");
        gpiod_line_release(line);
        gpiod_chip_close(chip);
        return 1;
    }

    printf("Sending 50Hz PWM sweep on gpiochip0 line 19 (P9_16) for ~6s...\n");
    printf("Watch the servo: it should sweep from one end to the other.\n");

    /* 300 cycles * 20 ms = 6 s. Pulse width sweeps 1000us -> 2000us. */
    for (int i = 0; i < 300; i++) {
        unsigned long pw = 1000UL + (1000UL * (unsigned long)i) / 299UL; /* 1.0ms..2.0ms */
        gpiod_line_set_value(line, 1);
        busy_sleep_us(pw);
        gpiod_line_set_value(line, 0);
        busy_sleep_us(20000UL - pw);
    }

    gpiod_line_set_value(line, 0);
    gpiod_line_release(line);
    gpiod_chip_close(chip);
    printf("Done. Servo moved? -> pin+servo+wiring good. Servo silent? -> pinmux/wiring problem.\n");
    return 0;
}

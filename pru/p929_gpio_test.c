/*
 * p929_gpio_test.c  --  bisect diagnostic for the P9_29 servo silence.
 *
 * Drives P9_29 (GPIO3_21) directly from the ARM GPIO controller at ~50 Hz for
 * a few seconds, so we can tell whether the WIRE + SERVO + PIN itself respond
 * to a 3.3V square wave (with NO PRU involved).
 *
 *   If the servo MOVES  -> P9_29 wiring/servo are fine; the problem is in the
 *                          PRU output path (pin ownership, mux-mode, firmware).
 *   If the servo does NOT move -> the signal wire is not actually on P9_29 (or
 *                          the servo / power is dead).
 *
 * IMPORTANT (learned 2026-08-26, board #23):
 *   - Raw /dev/mem access to GPIO3 (0x481AE000) BUS-FAULTS ("external abort on
 *     non-linefetch") because the GPIO3 module clock is gated and userspace
 *     mmap cannot enable it. So this tool uses libgpiod, which goes through the
 *     kernel GPIO driver (it enables the clock and owns the pad).
 *   - /dev/mem writes to the Control Module are DROPPED on this kernel, so we
 *     CANNOT flip P9_29 to GPIO mode from Linux. For this test the pin MUST be
 *     in GPIO mode 7, set at boot via U-Boot:
 *         printf 'uenvcmd=mw.l 0x44E109BC 0x47\n' | sudo tee -a /boot/firmware/uEnv.txt
 *     then reboot, run this test, then restore uenvcmd to 0x24 and reboot.
 *   - If P9_29 is still in PRU mode 4 (0x24), requesting the GPIO line as output
 *     will succeed but the PAD will NOT follow (it is routed to the PRU), so the
 *     servo will not move even though the test "ran". The mux warning below
 *     catches this.
 *
 * Build:  gcc -O2 -Wall -o p929_gpio_test p929_gpio_test.c -lgpiod
 * Run:    sudo ./p929_gpio_test [seconds]   (default 4)
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <gpiod.h>

#define CHIP  "gpiochip3"
#define LINE  21u   /* GPIO3_21 = P9_29 */

int main(int argc, char **argv)
{
    int seconds = (argc >= 2) ? atoi(argv[1]) : 4;
    if (seconds < 1) seconds = 4;

    /* Warn if the pin is still in PRU mode (0x24). Read the live pinctrl via
       the kernel debugfs (this read is allowed; only /dev/mem writes are blocked). */
    FILE *pf = popen("grep -i 9bc /sys/kernel/debug/pinctrl/44e10800.pinmux-pinctrl-single/pins 2>/dev/null", "r");
    if (pf) {
        char buf[256];
        if (fgets(buf, sizeof(buf), pf)) {
            if (strstr(buf, "00000024"))
                printf("[WARN] P9_29 mux = 0x24 (PRU mode 4). GPIO test cannot drive the pad.\n"
                       "       Set U-Boot uenvcmd to 0x47, reboot, run this, then restore 0x24.\n");
        }
        pclose(pf);
    }

    struct gpiod_chip *chip = gpiod_chip_open("/dev/gpiochip3");
    if (!chip) { perror("gpiod_chip_open"); return 1; }
    struct gpiod_line *line = gpiod_chip_get_line(chip, (unsigned int)LINE);
    if (!line) { perror("gpiod_chip_get_line"); gpiod_chip_close(chip); return 1; }
    if (gpiod_line_request_output(line, "p929_gpio_test", 0) < 0) {
        perror("gpiod_line_request_output");
        gpiod_chip_close(chip); return 1;
    }

    printf("Toggling %s line %u (P9_29) at ~50 Hz for %d s...\n", CHIP, LINE, seconds);
    for (int i = 0; i < seconds * 50; i++) {
        gpiod_line_set_value(line, 1);   /* HIGH ~1 ms */
        usleep(1000);
        gpiod_line_set_value(line, 0);   /* LOW  ~9 ms */
        usleep(9000);
    }
    gpiod_line_set_value(line, 0);
    printf("Done. servo MOVED  -> wire/pin/servo OK (assuming mux was GPIO mode).\n");
    printf("      servo STILL  -> signal wire not on P9_29 (or servo/power dead).\n");

    gpiod_line_release(line);
    gpiod_chip_close(chip);
    return 0;
}

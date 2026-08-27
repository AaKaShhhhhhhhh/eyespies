/*
 * p929_gpio_test.c  --  bisect diagnostic for the servo silence.
 *
 * Drives a header pin from the ARM GPIO controller at ~50 Hz for a few seconds,
 * to tell whether the WIRE + SERVO + PIN itself respond to a 3.3V square wave
 * (with NO PRU involved). Accepts a header pin NAME so we can test P9_29, P9_30,
 * P9_31, P8_45, P8_46, etc. without recompiling.
 *
 *   If the servo MOVES  -> that pin's wiring/servo are fine; the problem is in
 *                          the PRU output path (mux-mode, firmware, r30 bit).
 *   If the servo does NOT move -> the signal wire is not on that pin (or servo/
 *                          power dead). Try another pin / check wiring.
 *
 * IMPORTANT (learned 2026-08-26, board #23/#24):
 *   - Raw /dev/mem access to GPIO3 (0x481AE000) BUS-FAULTS ("external abort on
 *     non-linefetch") because the GPIO module clock is gated and userspace mmap
 *     cannot enable it. So this tool uses libgpiod, which goes through the kernel
 *     GPIO driver (it enables the clock and owns the pad).
 *   - /dev/mem writes to the Control Module are DROPPED on this kernel, so we
 *     CANNOT flip the mux from Linux. For a GPIO test the pin MUST be in GPIO mode
 *     (mode 7), set at boot via U-Boot:
 *         printf 'uenvcmd=mw.l 0x44E109BC 0x47\n' | sudo tee -a /boot/firmware/uEnv.txt
 *     then reboot, run this test, then restore uenvcmd to 0x24 and reboot.
 *   - If the pin is still in PRU mode (mux != 0x37), the GPIO write will NOT reach
 *     the pad even though libgpiod "succeeds". The tool reads the live mux and
 *     REFUSES to run (with the exact fix) when the pad is not in GPIO mode.
 *
 * Build:  gcc -O2 -Wall -o p929_gpio_test p929_gpio_test.c -lgpiod
 * Run:    sudo ./p929_gpio_test [seconds] [PIN]      (PIN default P9_29)
 *   e.g.  sudo ./p929_gpio_test 4 P9_29
 *         sudo ./p929_gpio_test 4 P8_46
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <gpiod.h>

/* Header pin -> (gpiochip, line). These are the standard BeagleBone Black
   assignments. P9_29/30/31 are PRU0 r30 outputs (mode 4); P8_45/46 are PRU1
   r30 outputs (mode 5). Verify a P8 pin with `gpioinfo gpiochip2` if unsure. */
struct pinmap { const char *name; const char *chip; unsigned int line; };
static const struct pinmap PINS[] = {
    { "P9_29", "gpiochip3", 21 },   /* GPIO3_21, PRU0 r30.1 (mode 4) */
    { "P9_30", "gpiochip3", 28 },   /* GPIO3_28, PRU0 r30.2 (mode 4) */
    { "P9_31", "gpiochip3", 14 },   /* GPIO3_14, PRU0 r30.0 (mode 4) */
    { "P9_27", "gpiochip3", 19 },   /* GPIO3_19, PRU0 r30.5 (mode 4) */
    { "P9_28", "gpiochip3", 17 },   /* GPIO3_17, PRU0 r30.3 (mode 4) */
    { "P8_45", "gpiochip2", 2  },   /* GPIO2_2,  PRU1 r30.0 (mode 5) */
    { "P8_46", "gpiochip2", 3  },   /* GPIO2_3,  PRU1 r30.1 (mode 5) */
    { NULL,    NULL,       0  }
};

/* Control-Module conf offset for each PRU-capable pin, so we can read its mux.
   P9_29=0x9bc, P9_30=0x9b8, P9_31=0x9b4, P9_27=0x9a8, P9_28=0x9a4,
   P8_45=0x9b0 (gpmc_a1), P8_46=0x9ac (gpmc_a0). */
static unsigned long conf_of(const char *name)
{
    if (!strcmp(name,"P9_29")) return 0x9bc;
    if (!strcmp(name,"P9_30")) return 0x9b8;
    if (!strcmp(name,"P9_31")) return 0x9b4;
    if (!strcmp(name,"P9_27")) return 0x9a8;
    if (!strcmp(name,"P9_28")) return 0x9a4;
    if (!strcmp(name,"P8_45")) return 0x9b0;
    if (!strcmp(name,"P8_46")) return 0x9ac;
    return 0;
}

int main(int argc, char **argv)
{
    int seconds = (argc >= 2) ? atoi(argv[1]) : 4;
    if (seconds < 1) seconds = 4;
    const char *pin = (argc >= 3) ? argv[2] : "P9_29";

    const struct pinmap *p = NULL;
    for (int i = 0; PINS[i].name; i++)
        if (!strcmp(PINS[i].name, pin)) { p = &PINS[i]; break; }
    if (!p) { fprintf(stderr, "unknown pin '%s' (known: P9_29 P9_30 P9_31 P9_27 P9_28 P8_45 P8_46)\n", pin); return 1; }

    /* Read the live mux for this pin from pinctrl debugfs (read allowed). */
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "grep -i %lx /sys/kernel/debug/pinctrl/44e10800.pinmux-pinctrl-single/pins 2>/dev/null",
             conf_of(pin));
    FILE *pf = popen(cmd, "r");
    char buf[256];
    int gpio_mode = 0;   /* 1 if mux byte looks like GPIO (0x07/0x27/0x37/0x47) */
    if (pf && fgets(buf, sizeof(buf), pf)) {
        /* value is the last 8 hex digits before "pinctrl-single" */
        unsigned int val = 0;
        char *sp = strrchr(buf, ' ');
        if (sp && sscanf(sp+1, "%x", &val) == 1) {
            int mode = val & 0x7;
            printf("mux for %s (conf 0x%03lx): 0x%08X (mode %d)\n", pin, conf_of(pin), val, mode);
            if (mode == 7) gpio_mode = 1;
        }
        pclose(pf);
    } else {
        printf("mux read failed (debugfs?) for %s; assuming NOT gpio mode.\n", pin);
    }

    if (!gpio_mode) {
        printf("[ABORT] %s is NOT in GPIO mode 7. A GPIO toggle cannot reach the pad.\n", pin);
        printf("        Fix: set U-Boot uenvcmd to write 0x47 to conf 0x%03lx, then reboot:\n", conf_of(pin));
        printf("          sudo sed -i '/^uenvcmd=/d' /boot/firmware/uEnv.txt\n");
        printf("          printf 'uenvcmd=mw.l 0x44E1%03lX 0x47\\n' | sudo tee -a /boot/firmware/uEnv.txt\n", conf_of(pin));
        printf("          sudo reboot   ;  then: sudo ./p929_gpio_test %d %s\n", seconds, pin);
        printf("        After the test, restore: uenvcmd=mw.l 0x44E1%03lX 0x24 ; reboot\n", conf_of(pin));
        return 2;
    }

    struct gpiod_chip *chip = gpiod_chip_open_by_name(p->chip);
    if (!chip) { perror("gpiod_chip_open"); return 1; }
    struct gpiod_line *line = gpiod_chip_get_line(chip, p->line);
    if (!line) { perror("gpiod_chip_get_line"); gpiod_chip_close(chip); return 1; }
    if (gpiod_line_request_output(line, "p929_gpio_test", 0) < 0) {
        perror("gpiod_line_request_output");
        gpiod_chip_close(chip); return 1;
    }

    printf("Toggling %s (%s line %u) at ~50 Hz for %d s...\n", pin, p->chip, p->line, seconds);
    for (int i = 0; i < seconds * 50; i++) {
        gpiod_line_set_value(line, 1);   /* HIGH ~1 ms */
        usleep(1000);
        gpiod_line_set_value(line, 0);   /* LOW  ~9 ms */
        usleep(9000);
    }
    gpiod_line_set_value(line, 0);
    printf("Done. servo MOVED  -> %s wire/pin/servo OK (assuming mux was GPIO mode).\n", pin);
    printf("      servo STILL  -> signal wire not on %s (or servo/power dead).\n", pin);

    gpiod_line_release(line);
    gpiod_chip_close(chip);
    return 0;
}

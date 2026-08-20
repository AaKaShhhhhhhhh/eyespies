/*
 * pmw/pmw_servo.c  —  USERSPACE servo driver using libgpiod (bit-banged 50 Hz PWM)
 *
 * WHY THIS FILE LOOKS DIFFERENT FROM BEFORE:
 * ------------------------------------------
 * The BeagleBone's *hardware* PWM pinmux is currently unreachable on the
 * stock Debian kernel we are using:
 *   - no CONFIG_OF_CONFIGFS in the kernel  -> can't load pinmux overlays
 *   - `config-pin` is dead on kernel 6.x
 *   - `devmem` register writes are blocked by the kernel
 * So /sys/class/pwm never actually drives the pin (you saw the pin stuck in
 * GPIO mode 0x27). BUT the servo works perfectly if WE toggle the GPIO pin
 * in software at 50 Hz. That technique is called "bit-banging".
 *
 * This file does exactly that, behind the SAME function names/arguments the
 * rest of the project already calls (pwm_set_period_ns / pwm_enable /
 * servo_set_angle / ...). Because the interface is identical, main.c,
 * v4l2.c and control_loop.c need ZERO changes.
 *
 * IN THE YOCTO IMAGE YOU CAN EITHER:
 *   (A) keep this gpiod version  -> just add `libgpiod` to the image. Works
 *       guaranteed, no device-tree magic needed.
 *   (B) switch back to the /sys/class/pwm (hardware PWM) version once the
 *       device tree pinmux is fixed -> the interface is identical, so again
 *       nothing else changes. (The old sysfs version is preserved in git
 *       history if you want it.)
 */

#include "pmw_servo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <gpiod.h>

/* We bit-bang a 50 Hz servo pulse from userspace. If the kernel ever preempts
 * this thread mid-pulse, the servo gets a wrong-width pulse and buzzes/jitters.
 * So we give the pulse thread the HIGHEST scheduling priority available to an
 * unprivileged-ish process. (Run the binary as root — sudo — and this works.) */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sched.h>

/* ------------------------------------------------------------------ */
/* Servo axis registry                                                 */
/* We map the OLD /sys/class/pwm path strings (used by main.c) to the   */
/* REAL gpiochip/line numbers on the BeagleBone:                        */
/*   P9_14 = gpiochip0 line 18  -> PAN   (not wired yet)                */
/*   P9_16 = gpiochip0 line 19  -> TILT  (verified working)             */
/* ------------------------------------------------------------------ */
typedef struct {
    const char      *key;        /* matches the PWM_PATH macro in main.c  */
    const char      *chip;       /* gpiod chip name                       */
    int              line;       /* GPIO line number on that chip         */
    unsigned long    period_ns;  /* PWM period (20 ms = 50 Hz for servo)  */
    unsigned long    duty_ns;    /* current target pulse width            */
    int              running;    /* internal: background thread alive     */
    pthread_t        thread;     /* background pulse thread               */
    pthread_mutex_t  lock;       /* protects period_ns / duty_ns          */
    struct gpiod_chip *chip_h;   /* open gpiod handle                     */
    struct gpiod_line *line_h;   /* requested output line                 */
} servo_axis_t;

#define NUM_AXES 2
static servo_axis_t axes[NUM_AXES] = {
    [0] = {  /* PAN  (P9_14, gpiochip0 line 18) */
        .key       = "/sys/class/pwm/pwmchip1/pwm0",
        .chip      = "gpiochip0",
        .line      = 18,
        .period_ns = 20000000,   /* 20 ms */
        .duty_ns   = 1500000,    /* 1.5 ms = 90 deg (safe centre) */
        .lock      = PTHREAD_MUTEX_INITIALIZER,
    },
    [1] = {  /* TILT (P9_16, gpiochip0 line 19) */
        .key       = "/sys/class/pwm/pwmchip1/pwm1",
        .chip      = "gpiochip0",
        .line      = 19,
        .period_ns = 20000000,
        .duty_ns   = 1500000,
        .lock      = PTHREAD_MUTEX_INITIALIZER,
    },
};

static servo_axis_t *find_axis(const char *pwm_path) {
    for (int i = 0; i < NUM_AXES; i++)
        if (strcmp(axes[i].key, pwm_path) == 0)
            return &axes[i];
    fprintf(stderr, "pmw_servo: unknown pwm_path '%s'\n", pwm_path);
    return NULL;
}

/* Sleep for a number of nanoseconds using nanosleep. */
static void sleep_ns(unsigned long ns) {
    struct timespec ts;
    ts.tv_sec  = (time_t)(ns / 1000000000UL);
    ts.tv_nsec = (long)(ns % 1000000000UL);
    nanosleep(&ts, NULL);
}

/* Background thread: continuously emit 50 Hz pulses at the current duty.
 * A servo MUST keep receiving pulses or it loses position, so this loop
 * never stops while the axis is enabled. */
static void *pwm_thread(void *arg) {
    servo_axis_t *a = (servo_axis_t *)arg;
    unsigned long period, duty;

    while (a->running) {
        pthread_mutex_lock(&a->lock);
        period = a->period_ns;
        duty   = a->duty_ns;
        pthread_mutex_unlock(&a->lock);

        if (duty > period) duty = period;

        /* One pulse: HIGH for duty_ns, then LOW for the rest of the period. */
        gpiod_line_set_value(a->line_h, 1);
        sleep_ns(duty);
        gpiod_line_set_value(a->line_h, 0);
        sleep_ns(period - duty);
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Public API  (same names / arguments as the old sysfs version)        */
/* ------------------------------------------------------------------ */

unsigned long angle_to_duty_ns(float angle_degrees) {
    /* Standard servo: 0 deg -> 0.5 ms, 180 deg -> 2.5 ms. */
    float duty_ms = 0.5f + (angle_degrees / 180.0f) * 2.0f;
    return (unsigned long)(duty_ms * 1000000.0f);
}

void pwm_set_period_ns(const char *pwm_path, unsigned long period_ns) {
    servo_axis_t *a = find_axis(pwm_path);
    if (!a) return;
    pthread_mutex_lock(&a->lock);
    a->period_ns = period_ns;
    pthread_mutex_unlock(&a->lock);
}

void pwm_set_duty_cycle_ns(const char *pwm_path, unsigned long duty_cycle_ns) {
    servo_axis_t *a = find_axis(pwm_path);
    if (!a) return;
    pthread_mutex_lock(&a->lock);
    a->duty_ns = duty_cycle_ns;
    pthread_mutex_unlock(&a->lock);
}

void pwm_enable(const char *pwm_path, int enable) {
    servo_axis_t *a = find_axis(pwm_path);
    if (!a) return;

    if (enable) {
        if (a->running) return;            /* already running, ignore */

        a->chip_h = gpiod_chip_open_by_name(a->chip);
        if (!a->chip_h) {
            perror("pmw_servo: gpiod_chip_open_by_name");
            return;
        }
        a->line_h = gpiod_chip_get_line(a->chip_h, a->line);
        if (!a->line_h) {
            perror("pmw_servo: gpiod_chip_get_line");
            gpiod_chip_close(a->chip_h);
            a->chip_h = NULL;
            return;
        }
        if (gpiod_line_request_output(a->line_h, "eyespies_servo", 0) < 0) {
            perror("pmw_servo: gpiod_line_request_output");
            gpiod_chip_close(a->chip_h);
            a->chip_h = NULL;
            a->line_h = NULL;
            return;
        }
        a->running = 1;
        pthread_create(&a->thread, NULL, pwm_thread, a);
        /* Boost the pulse thread to realtime so the 50 Hz signal stays rock
         * steady even while the camera loop is busy. Prevents servo buzz. */
        struct sched_param sp;
        sp.sched_priority = sched_get_priority_max(SCHED_FIFO);
        if (sp.sched_priority > 0)
            pthread_setschedparam(a->thread, SCHED_FIFO, &sp);
    } else {
        if (!a->running) return;
        a->running = 0;                    /* tell thread to stop */
        pthread_join(a->thread, NULL);
        gpiod_line_release(a->line_h);
        gpiod_chip_close(a->chip_h);
        a->line_h = NULL;
        a->chip_h = NULL;
    }
}

void servo_set_angle(const char *pwm_path, float angle_degrees) {
    unsigned long duty = angle_to_duty_ns(angle_degrees);
    pwm_set_duty_cycle_ns(pwm_path, duty);
}

/* ------------------------------------------------------------------ */
/* Optional standalone self-test:  gcc -DSTANDALONE_TEST -o test pmw_servo.c -lgpiod */
/* ------------------------------------------------------------------ */
#ifdef STANDALONE_TEST
int main(void) {
    const char *tilt = "/sys/class/pwm/pwmchip1/pwm1"; /* P9_16, line 19 */
    const char *pan  = "/sys/class/pwm/pwmchip1/pwm0"; /* P9_14, line 18 */

    printf("=== eyespies gpiod servo self-test ===\n");
    pwm_set_period_ns(tilt, 20000000);
    pwm_enable(tilt, 1);
    pwm_set_period_ns(pan, 20000000);
    pwm_enable(pan, 1);

    servo_set_angle(tilt, 90);  sleep(1);   /* centre */
    servo_set_angle(tilt, 0);   sleep(1);   /* one extreme */
    servo_set_angle(tilt, 180); sleep(1);   /* other extreme */
    servo_set_angle(tilt, 90);  sleep(1);   /* back to centre */

    pwm_enable(tilt, 0);
    pwm_enable(pan, 0);
    printf("done\n");
    return 0;
}
#endif /* STANDALONE_TEST */

/*
 * pru0_servo.pru0.c
 * ---------------------------------------------------------------------------
 * PRU0 firmware: generates a 50 Hz servo PWM on the pin the servo is actually
 * wired to. We PROVED (via servo_pwm_test.c) the servo sits on
 *   gpiochip0 line 19  ==  GPIO0_19   (base 0x44E07000)
 * so this firmware drives GPIO0_19 ONLY.
 *
 * IMPORTANT (the bug we finally found):
 *   The PRU can poke the GPIO registers, but only if Linux has (a) muxed the
 *   pad to GPIO mode, (b) turned the GPIO0 clock ON, and (c) set OE=0 (output).
 *   Linux does all three *while a process holds the line* -- and undoes them
 *   the instant that process exits. So: run `gpio_hold` (or `gpioset -b`) from
 *   Linux to keep the line alive, THEN load this firmware.
 *
 * Build:  pru-gcc -O2 -Wall -mmcu=am335x.pru0 -o pru0_servo.out pru0_servo.pru0.c
 *         pru-gcc -O2 -Wall -DSERVO_STEADY -mmcu=am335x.pru0 -o servo_steady.out pru0_servo.pru0.c
 * Load:   sudo ./load_pru0.sh pru0_servo.out   (or servo_steady.out)
 * ---------------------------------------------------------------------------
 */
#include <stdint.h>

/* REMOVED: #include <pru_cfg.h> because it is built for TI clpru, not pru-gcc */
/* Instead, we explicitly define the structure your code needs locally */
typedef struct {
    volatile uint32_t REVID;
    volatile uint32_t SYSCFG;
    volatile uint32_t GPCFG0;
    volatile uint32_t GPCFG1;
} pruCfg;

/* Map CT_CFG safely to its absolute physical register space for pru-gcc */
#define CT_CFG (*(volatile pruCfg *)0x00026000)

#include "resource_table_empty.h"


/* --- GPIO0 register map (AM335x TRM) --- */


/* --- GPIO0 register map (AM335x TRM) --- */
#define GPIO0_BASE     0x44E07000u
#define GPIO_OE        0x134u   /* output enable: 0=output, 1=input        */
#define GPIO_DATAOUT   0x13Cu   /* read back current output level          */
#define GPIO_CLEARDATA 0x190u   /* write 1 to clear a bit (no read-modify) */
#define GPIO_SETDATA   0x194u   /* write 1 to set   a bit (no read-modify) */
#define SERVO_BIT      (1u << 19)   /* GPIO0_19 */

/* --- PRCM: keep the GPIO0 module clock on (belt & suspenders) --- */
#define CM_WKUP_BASE           0x44E00400u
#define CM_WKUP_GPIO0_CLKCTRL  0x08u   /* MODULEMODE field, bits 0-1 */

/* --- PRU local config: clear STANDBY_INIT so OCP writes reach the bus --- */
#define PRU_CFG_SYSCFG  0x00026004u   /* SYSCFG; bit 4 = STANDBY_INIT */

/* --- timing --- */
#define PRU_FREQ_MHZ      200u
#define CYCLES_PER_CHUNK  2000u        /* 2000 cycles @200MHz = 10 us */
#define PERIOD_US         20000u       /* 20 ms -> 50 Hz              */
#define PULSE_MIN_US      1000u        /* 1.0 ms = -90 deg            */
#define PULSE_MAX_US      2000u        /* 2.0 ms = +90 deg            */
#define PULSE_CENTER_US   1500u        /* 1.5 ms = center             */

/* HWREG: PRU has a flat 32-bit physical address space, so a cast is enough. */
#define HWREG(x) (*(volatile unsigned *)(x))

/* pru-gcc provides __delay_cycles(), but ONLY with a compile-time constant. */
static void busy_wait_us(unsigned us)
{
    unsigned chunks = (us * 100u) / CYCLES_PER_CHUNK;   /* 10 us per chunk */
    unsigned i;
    for (i = 0; i < chunks; i++)
        __delay_cycles(CYCLES_PER_CHUNK);
}

/* Turn the GPIO0 module clock on (Linux usually already did this if the
 * line is held, but we re-assert it so the firmware is self-sufficient). */
static void prcm_enable_gpio0(void)
{
    HWREG(CM_WKUP_BASE + CM_WKUP_GPIO0_CLKCTRL) = (2u << 0);  /* MODULEMODE=ENABLE */
    /* small settle delay so the module comes out of idle before we touch it */
    busy_wait_us(100);
}

/* Let the PRU act as an OCP bus master so its writes leave the PRU. */
static void pru_ocp_enable(void)
{
    HWREG(PRU_CFG_SYSCFG) &= ~(1u << 4);   /* clear STANDBY_INIT */
}

/* Drive ONE pulse of the given width, then the rest of the 20 ms period low. */
static void send_pulse(unsigned width_us)
{
    HWREG(GPIO0_BASE + GPIO_SETDATA)   = SERVO_BIT;   /* pin HIGH */
    busy_wait_us(width_us);
    HWREG(GPIO0_BASE + GPIO_CLEARDATA) = SERVO_BIT;   /* pin LOW  */
    busy_wait_us(PERIOD_US - width_us);
}

void main(void)
{
    unsigned width;

    prcm_enable_gpio0();
    pru_ocp_enable();

    /* Make GPIO0_19 an OUTPUT (Linux already did this if the line is held,
     * but we repeat it so the PRU is not dependent on that). */
    HWREG(GPIO0_BASE + GPIO_OE) &= ~SERVO_BIT;

#ifdef HOLD_HIGH
    /* DECISIVE TEST: drive the pin HIGH and park it. Then read the live
     * register from Linux with `sudo ./gpio_read` (mmap of GPIO0 DATAOUT).
     *   -> bit19=1 : the PRU's OCP write REACHED the register (bus works)
     *   -> bit19=0 : the PRU's write is NOT reaching the register (bus/mux bug)
     * We keep spinning so the core never halts (belt & suspenders). */
    HWREG(GPIO0_BASE + GPIO_SETDATA) = SERVO_BIT;
    while (1) __delay_cycles(CYCLES_PER_CHUNK);
#elif defined(SERVO_STEADY)
    /* Constant 1.5 ms center pulse forever -- simplest "is it alive?" test. */
    while (1)
        send_pulse(PULSE_CENTER_US);
#else
    /* Sweep 1.0 ms -> 2.0 ms -> 1.0 ms, 10 ms per step. Easy to see motion. */
    while (1) {
        for (width = PULSE_MIN_US; width <= PULSE_MAX_US; width += 20)
            send_pulse(width);
        for (width = PULSE_MAX_US; width >= PULSE_MIN_US; width -= 20)
            send_pulse(width);
    }
#endif
}

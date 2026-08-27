# pru/Makefile
# ===========================================================================
# Build the PRU firmware + small Linux helpers on the BeagleBone Black.
# Requires on the board:
#   sudo apt install gcc-pru          # for the .out PRU firmware
#   sudo apt install libgpiod-dev     # for gpio_hold.c / arm_write / tests
#
# IMPORTANT — which PRU drives which pin:
#   P9_29 is reached by PRU0's r30.1 — but ONLY when its pinmux is in a PRU
#   mode (4 = PRU0, 5 = PRU1); at the default boot mode (7 = GPIO) r30 reaches
#   nothing. The firmware pru1_servo.pru1.c runs on PRU0 (DO NOT change
#   -mmcu) and simply drives r30.1; the PAD MUX to mode 4 is set by the
#   device-tree overlay pru_p9_29.dts (see below). r30 does NOT bypass the mux.
#   (Despite the "pru1" filename it targets PRU0 — PRU1 r30.1 is a P8 ball,
#   not P9_29, so PRU1 alone cannot move P9_29.)
#
#   P9_16 (GPIO0_19) is driven by pru0_servo.pru0.c via GPIO0 OCP writes
#   (built for PRU0), or by the userspace servo_pwm_test.c.
#
# Pinmux note (your kernel 6.x image) — HARD-WON, SOLVED:
#   config-pin is dead, devmem is blocked (CONFIG_STRICT_DEVMEM), PRU OCP
#   writes to the Control Module are dropped, ARM /dev/mem writes to the
#   Control Module read back unchanged (0x28 instead of 0x24), and NO DT
#   overlay shape works on 6.12 (bone-pinmux-helper no-op, pinctrl-hog no-op,
#   &ocp self-cycle, &pruss "no pins entries"). The ONLY working way to mux
#   P9_29 to PRU mode 4 is a U-Boot `uenvcmd` register write set at every boot:
#       printf 'uenvcmd=mw.l 0x44E109BC 0x24\n' | sudo tee -a /boot/firmware/uEnv.txt
#   Confirmed: post-boot `grep 9bc .../pins` -> `pin 111 ... 44e109bc 00000024`.
#   The pru_p9_29.dts overlay is RETIRED (kept only as a record). See
#   PRU_SERVO_DEBUG_LOG.md §2/§5/§9a.
# ===========================================================================

CFLAGS += -I/usr/lib/ti/pru-software-support-package-v6.3/include/am335x

all: pru0_servo.out servo_steady.out hold_high.out blink.out \
     gpio_hold gpio_read servo_pwm_test arm_write_p929 pru1_servo.pru1.out \
     pru_const_high.pru0.out \
     p929_gpio_test syscfg_probe

# Real servo firmware -- PRU0 drives P9_29 via r30.1. The pad is muxed to
# PRU mode 4 by the device-tree overlay pru_p9_29.dts (load it FIRST; see the
# pinmux note at the top). The firmware itself only drives r30.1 and reads the
# commanded pulse width from PRU shared RAM (written by arm_write_p929).
# Built FOR PRU0 (-mmcu=am335x.pru0). (Despite the "pru1" filename it targets
# PRU0 — PRU1 r30.1 is a P8 ball, not P9_29.) Load with:
#   sudo ./load_pru0.sh pru1_servo.pru1.out      # (or load_pru.sh, if present)
#   sudo ./arm_write_p929 1500                   # center pulse
pru1_servo.pru1.out: pru1_servo.pru1.c
	pru-gcc -O2 -Wall -mmcu=am335x.pru0 -o $@ $<

# Isolation firmware: drives P9_29 (r30.1) as a 0.5 Hz square wave with NO
# memory reads (no shared RAM / OCP), so a bus fault cannot halt it. Used to
# prove the PRU pad reaches P9_29 when muxed to mode 4 (see PRU_SERVO_DEBUG_LOG
# board #29). If the servo swings once per second -> GPO reaches the pad.
pru_const_high.pru0.out: pru_const_high.pru0.c resource_table_empty.h
	pru-gcc -I/usr/lib/ti/pru-software-support-package-v6.3/include/am335x -O2 -Wall -mmcu=am335x.pru0 -o $@ $<

# Real servo firmware -- PRU emits 50 Hz PWM sweep on GPIO0_19 (P9_16).
pru0_servo.out: pru0_servo.pru0.c resource_table_empty.h
	pru-gcc -I/usr/lib/ti/pru-software-support-package-v6.3/include/am335x -O2 -Wall -mmcu=am335x.pru0 -o $@ $<

# Diagnostic: hold a STEADY 1.5 ms pulse (servo center).
servo_steady.out: pru0_servo.pru0.c resource_table_empty.h
	pru-gcc -I/usr/lib/ti/pru-software-support-package-v6.3/include/am335x -O2 -Wall -DSERVO_STEADY -mmcu=am335x.pru0 -o $@ $<

# DECISIVE TEST: drive the pin HIGH and park it. Read the live register from
# Linux with `sudo ./gpio_read` (mmap of GPIO0 DATAOUT). 1 = PRU bus works;
# 0 = bus/mux bug.
hold_high.out: pru0_servo.pru0.c resource_table_empty.h
	pru-gcc -I/usr/lib/ti/pru-software-support-package-v6.3/include/am335x -O2 -Wall -DHOLD_HIGH -mmcu=am335x.pru0 -o $@ $<

# Blink test -- 5 Hz GPIO toggle (define BLINK_TEST) for isolating the pin.
blink.out: pru0_servo.pru0.c resource_table_empty.h
	pru-gcc -I/usr/lib/ti/pru-software-support-package-v6.3/include/am335x -O2 -Wall -DBLINK_TEST -mmcu=am335x.pru0 -o $@ $<

# Linux helper: hold a GPIO line (default P9_29 = gpiochip3 line 23) as output
# forever so the PRU's OCP writes reach the wire.  gpioset -b <chip> <line>=0
# is an equivalent one-liner.
gpio_hold: gpio_hold.c
	gcc -O2 -Wall -o $@ $< -lgpiod

# Linux helper: read the ACTUAL GPIO0 DATAOUT register via /dev/mem mmap.
gpio_read: gpio_read.c
	gcc -O2 -Wall -o $@ $<

# Userspace (libgpiod) servo sweep on P9_16 / GPIO0_19 — used to prove
# pin+servo+wiring work without the PRU.
servo_pwm_test: servo_pwm_test.c
	gcc -O2 -Wall -o $@ $< -lgpiod

# Linux helper: write the commanded pulse width (us) into PRU shared RAM
# (0x4A310000) for pru1_servo.pru1.out. The pad mux (mode 4) must already be
# set by the pru_p9_29.dtbo overlay -- this helper just VERIFIES the mux reads
# 0x24 and writes the pulse. (On 6.x the CM write is blocked, so the overlay is
# the only thing that can change the mux; this tool reports MUX OK/BLOCKED.)
arm_write_p929: arm_write_p929.c
	gcc -O2 -Wall -o $@ $<

# Linux helper: bisect tool. Muxes P9_29 to GPIO mode 7 and toggles GPIO3_21
# (P9_29) at ~50 Hz for N seconds to confirm the wire+servo+pin respond to a
# 3.3V square wave with NO PRU involved. Uses libgpiod (NOT raw /dev/mem, which
# bus-faults on the gated GPIO3 clock). If the servo moves, P9_29 is good and
# the problem is the PRU output path. If it does not, the signal wire is not on
# P9_29 (or servo/power dead). NOTE: needs P9_29 muxed to GPIO mode 7 first
# (set U-Boot uenvcmd to 0x47, reboot). Build+run:
#   gcc -O2 -Wall -o p929_gpio_test p929_gpio_test.c -lgpiod ; sudo ./p929_gpio_test 4
p929_gpio_test: p929_gpio_test.c
	gcc -O2 -Wall -o $@ $< -lgpiod

# Linux helper: probe SYSCFG STANDBY_INIT writability (proved it is read-only
# PRCM status bit). gcc -O2 -o syscfg_probe syscfg_probe.c ; sudo ./syscfg_probe
syscfg_probe: syscfg_probe.c
	gcc -O2 -Wall -o $@ $<

# Device-tree overlay: mux P9_29 -> PRU0 (mode 4). Build with dtc, then load
# (see pinmux note / PRU_SERVO_DEBUG_LOG.md). Requires: sudo apt install device-tree-compiler
pru_p9_29.dtbo: pru_p9_29.dts
	dtc -O dtb -o $@ -b 0 -@ $<

clean:
	rm -f *.out gpio_hold gpio_read servo_pwm_test arm_write_p929 pru_p9_29.dtbo p929_gpio_test syscfg_probe

.PHONY: all clean

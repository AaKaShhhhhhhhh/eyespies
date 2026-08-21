# pru/Makefile
# ===========================================================================
# Build the PRU firmware + a tiny Linux helper on the BeagleBone Black.
# Requires on the board:
#   sudo apt install gcc-pru          # for the .out PRU firmware
#   sudo apt install libgpiod-dev     # for gpio_hold.c
#
# WHY TWO BINARIES:
#   The PRU can write the GPIO0 registers, but it CANNOT keep the pin "alive"
#   on its own -- Linux owns the pad mux + the GPIO0 clock and turns both off
#   when no Linux process holds the line. So we run `gpio_hold` (or
#   `gpioset -b gpiochip0 19=0`) to keep the line requested, THEN load the PRU
#   firmware which pokes the same DATAOUT register and reaches the wire.
#
# NOTE: We do NOT pass -I to TI's CGT include dir. GNU gcc-pru ships its own
# standard C headers; the resource table header is self-contained.
# ===========================================================================

# Default target: PRU sweep + steady diagnostic + blink, plus the Linux holder.
#   pru0_servo.out   -> real 50 Hz servo SWEEP on GPIO0_19 (the servo pin)
#   servo_steady.out -> DIAGNOSTIC: constant 1.5 ms center pulse
#   blink.out        -> 5 Hz toggle, for isolating the pin
#   gpio_hold        -> Linux helper that holds gpiochip0 line 19 so the PRU
#                       writes actually reach the pad
CFLAGS += -I/usr/lib/ti/pru-software-support-package-v6.3/include/am335x

all: pru0_servo.out servo_steady.out hold_high.out blink.out gpio_hold gpio_read

# Real servo firmware -- PRU emits 50 Hz PWM sweep on GPIO0_19.
# Real servo firmware -- PRU emits 50 Hz PWM sweep on GPIO0_19.	
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

# Linux helper: hold gpiochip0 line 19 (GPIO0_19) as output forever so the
# PRU's writes reach the wire. Built with the normal ARM gcc + libgpiod.
gpio_hold: gpio_hold.c
	gcc -O2 -Wall -o $@ $< -lgpiod

# Linux helper: read the ACTUAL GPIO0 DATAOUT register via /dev/mem mmap.
# Does NOT request the line, so it works while gpio_hold owns it (no "busy").
# Use with hold_high.out to prove whether the PRU's write reached the register.
gpio_read: gpio_read.c
	gcc -O2 -Wall -o $@ $<

clean:
	rm -f *.out gpio_hold gpio_read

.PHONY: all clean



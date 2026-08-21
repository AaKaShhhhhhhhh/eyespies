# pru/Makefile
# ===========================================================================
# Build the PRU firmware on the BeagleBone Black.
# Requires:  sudo apt install gcc-pru
#
# NOTE: We intentionally do NOT pass -I to TI's Code Generation Tools include
# dir. GNU gcc-pru ships its own standard C headers; pointing it at TI's CGT
# headers (which target TI's proprietary compiler) breaks <stddef.h>. The
# resource table header is self-contained, so no -I is needed.
# ===========================================================================

# Default target: build the sweep + steady diagnostic + blink.
#   pru0_servo.out  -> real 50 Hz servo SWEEP on P9_16 (project goal)
#   servo_steady.out-> DIAGNOSTIC: hold a constant 1.5 ms center pulse
#                       (proves the PRU can drive the pin at all)
#   blink.out       -> 5 Hz GPIO toggle, for isolating the pin
all: pru0_servo.out servo_steady.out blink.out

# Real servo firmware -- PRU emits 50 Hz PWM sweep on P9_16 all by itself.
pru0_servo.out: pru0_servo.pru0.c resource_table_empty.h
	pru-gcc -O2 -Wall -mmcu=am335x.pru0 -o $@ $<

# Diagnostic: hold a STEADY 1.5 ms pulse (servo center). If this moves
# the servo but the sweep doesn't, the bug is in the sweep delay logic.
servo_steady.out: pru0_servo.pru0.c resource_table_empty.h
	pru-gcc -O2 -Wall -DSERVO_STEADY -mmcu=am335x.pru0 -o $@ $<

# Blink test -- define BLINK_TEST (5 Hz toggle; use only to prove pin toggles).
blink.out: pru0_servo.pru0.c resource_table_empty.h
	pru-gcc -O2 -Wall -DBLINK_TEST -mmcu=am335x.pru0 -o $@ $<

clean:
	rm -f *.out

.PHONY: all clean

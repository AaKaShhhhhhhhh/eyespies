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

# Default target: build BOTH firmwares.
#   pru0_servo.out  -> real 50 Hz servo sweep on P9_16 (project goal)
#   blink.out       -> 5 Hz GPIO toggle, for isolating the pin
all: pru0_servo.out blink.out

# Real servo firmware -- PRU emits 50 Hz PWM on P9_16 all by itself.
pru0_servo.out: pru0_servo.pru0.c resource_table_empty.h
	pru-gcc -O2 -Wall -mmcu=am335x.pru0 -o $@ $<

# Blink test -- define BLINK_TEST (5 Hz toggle; use only to prove pin toggles).
blink.out: pru0_servo.pru0.c resource_table_empty.h
	pru-gcc -O2 -Wall -DBLINK_TEST -mmcu=am335x.pru0 -o $@ $<

clean:
	rm -f *.out

.PHONY: all clean

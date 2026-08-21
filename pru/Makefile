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

# Real servo firmware (Phase 4+)
pru0_servo.out: pru0_servo.pru0.c resource_table_empty.h
	pru-gcc -O2 -Wall -mmcu=am335x.pru0 -o $@ $<

# Blink test (Phase 1/2) -- define BLINK_TEST
blink.out: pru0_servo.pru0.c resource_table_empty.h
	pru-gcc -O2 -Wall -DBLINK_TEST -mmcu=am335x.pru0 -o $@ $<

clean:
	rm -f *.out

.PHONY: clean

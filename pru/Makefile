# pru/Makefile
# ===========================================================================
# Build the PRU firmware on the BeagleBone Black.
# Requires:  sudo apt install gcc-pru pru-software-support-package
#
# Find the include path for rsc_types.h (needed by resource_table_empty.h):
#     find / -name rsc_types.h
# Common result:  /usr/share/ti/cgt-pru/include
# If yours differs, override:  make PRU_INC=/your/path
# ===========================================================================
PRU_INC ?= /usr/share/ti/cgt-pru/include

# Real servo firmware (Phase 4+)
pru0_servo.out: pru0_servo.pru0.c resource_table_empty.h
	pru-gcc -O2 -Wall -I$(PRU_INC) -mmcu=am335x.pru0 -o $@ $<

# Blink test (Phase 1/2) — define BLINK_TEST
blink.out: pru0_servo.pru0.c resource_table_empty.h
	pru-gcc -O2 -Wall -DBLINK_TEST -I$(PRU_INC) -mmcu=am335x.pru0 -o $@ $<

clean:
	rm -f *.out

.PHONY: clean

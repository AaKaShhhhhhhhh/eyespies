# pru/Makefile
# ===========================================================================
# Build the PRU firmware on the BeagleBone Black.
# Requires:  sudo apt install gcc-pru pru-software-support-package
#
# This Makefile auto-detects where rsc_types.h lives on your board, so you
# normally just run `make blink.out` or `make pru0_servo.out` with no args.
# If auto-detect ever fails, override:  make PRU_INC=/your/path
# ===========================================================================

# Auto-detect the TI include dir (where rsc_types.h lives). Common spots:
#   /usr/share/ti/cgt-pru/include        (TI package)
#   /usr/include                         (some Debian packages)
RSC_H   := $(shell find /usr/share/ti /usr/include /opt -name rsc_types.h 2>/dev/null | head -n1)
PRU_INC ?= $(if $(RSC_H),$(shell dirname $(RSC_H)),/usr/share/ti/cgt-pru/include)

# Real servo firmware (Phase 4+)
pru0_servo.out: pru0_servo.pru0.c resource_table_empty.h
	pru-gcc -O2 -Wall -I$(PRU_INC) -mmcu=am335x.pru0 -o $@ $<

# Blink test (Phase 1/2) -- define BLINK_TEST
blink.out: pru0_servo.pru0.c resource_table_empty.h
	pru-gcc -O2 -Wall -DBLINK_TEST -I$(PRU_INC) -mmcu=am335x.pru0 -o $@ $<

clean:
	rm -f *.out

.PHONY: clean

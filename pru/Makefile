# pru/Makefile
# ===========================================================================
# Build the PRU servo firmware on the BeagleBone Black.
# Requires on the board:  sudo apt install gcc-pru
#
# SOLVED state (2026-08-28): PRU0 drives an MG90S servo on P9_31 via
# direct GPO (__R30 bit 0). The pad is muxed to PRU mode 5 by a U-Boot
# uenvcmd line in /boot/firmware/uEnv.txt (kernel 6.x blocks ARM padconf
# writes, so the U-Boot mux is the only thing that sticks). See
# PRU_SERVO_DEBUG_LOG.md and docs/TURRET_PLAN.md.
#
# Load/stop the firmware with the helper script:
#   sudo bash load_pru.sh          # start (muxes + loads pru_servo.out)
#   sudo bash load_pru.sh stop      # halt PRU0 (signal holds last level)
# ===========================================================================

PRU_INC = -I/usr/lib/ti/pru-software-support-package-v6.3/include/am335x
PRU_MCU = -mmcu=am335x.pru0

CFLAGS += $(PRU_INC)

.PHONY: all clean

# The only shipping firmware: 50 Hz PWM sweep on P9_31 (R30 bit 0).
all: pru_servo.out

pru_servo.out: pru_servo.c resource_table_empty.h
	pru-gcc -O2 -Wall $(PRU_INC) $(PRU_MCU) -o $@ pru_servo.c

clean:
	rm -f *.out

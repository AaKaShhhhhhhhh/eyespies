# Top-level Makefile — Camera Turret (single-board, BeagleBone Black)
# Camera + detection + control + PWM all run in ONE process on the board.
# The old two-machine network bridge (main_debian.c / main_bbb.c / websocket/)
# has been removed — see INTEGRATION_GUIDE.md.

CC      = gcc
CFLAGS  = -Wall -Wextra -g -O2
INCLUDES = -I. -I./capture -I./control -I./detection -I./pmw
LIBS    = -lpthread -lm -lrt

CAPTURE_OBJS   = capture/v4l2.o
CONTROL_OBJS   = control/control_loop.o
DETECTION_OBJS = detection/color_threshold.o
PWM_OBJS       = pmw/pmw_servo.o
OBJS = $(CAPTURE_OBJS) $(CONTROL_OBJS) $(DETECTION_OBJS) $(PWM_OBJS)

.PHONY: all turret clean

all: $(OBJS)
	@echo "Objects built. Write main.c (see INTEGRATION_GUIDE.md), then: make turret"

# Final on-board binary: camera + detection + control + pwm, no network
turret: main.c $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ main.c $(OBJS) $(LIBS)

clean:
	rm -f turret main.o
	rm -f capture/*.o
	rm -f control/*.o control/*.a
	rm -f detection/*.o detection/*.a
	rm -f pmw/*.o pmw/*.a

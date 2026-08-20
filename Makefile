# Top-level Makefile — Camera Turret (single-board, BeagleBone Black)
# Camera + detection + control + PWM all run in ONE process on the board.
# The old two-machine network bridge (main_debian.c / main_bbb.c / websocket/)
# has been removed — see INTEGRATION_GUIDE.md.

CC      = gcc
CFLAGS  = -Wall -Wextra -g -O2
INCLUDES = -I. -I./capture -I./control -I./detection -I./pmw
LIBS    = -lpthread -lm -lrt -lgpiod

CAPTURE_OBJS   = capture/v4l2.o
CONTROL_OBJS   = control/control_loop.o
DETECTION_OBJS = detection/color_threshold.o detection/motion_detect.o
PWM_OBJS       = pmw/pmw_servo.o
OBJS = $(CAPTURE_OBJS) $(CONTROL_OBJS) $(DETECTION_OBJS) $(PWM_OBJS)

.PHONY: all turret clean

all: $(OBJS)
	@echo "Objects built. Run: make turret   (to link the final binary)"

# Final on-board binary: camera + detection + control + pwm, no network.
# We compile each module with the shared include paths so headers resolve.
capture/v4l2.o: capture/v4l2.c capture/capture.h control/control_loop.h \
                pmw/pmw_servo.h detection/color_threshold.h
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ capture/v4l2.c

control/control_loop.o: control/control_loop.c control/control_loop.h
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ control/control_loop.c

detection/color_threshold.o: detection/color_threshold.c detection/color_threshold.h \
                              capture/capture.h
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ detection/color_threshold.c

detection/motion_detect.o: detection/motion_detect.c detection/motion_detect.h \
                           capture/capture.h
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ detection/motion_detect.c

pmw/pmw_servo.o: pmw/pmw_servo.c pmw/pmw_servo.h
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ pmw/pmw_servo.c

turret: main.c $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ main.c $(OBJS) $(LIBS)

clean:
	rm -f turret main.o
	rm -f capture/*.o
	rm -f control/*.o control/*.a
	rm -f detection/*.o detection/*.a
	rm -f pmw/*.o pmw/*.a

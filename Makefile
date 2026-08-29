# Top-level Makefile — Camera Turret (single-board, BeagleBone Black)
# Camera + detection + control + PWM all run in ONE process on the board.
# (See INTEGRATION_GUIDE.md for the future Yocto image / dashboard packaging.)

CC      = gcc
CFLAGS  = -Wall -Wextra -g -O2
INCLUDES = -I. -I./capture -I./control -I./detection -I./pmw -I./pru
LIBS    = -lpthread -lm -lrt -lgpiod

CAPTURE_OBJS   = capture/v4l2.o
CONTROL_OBJS   = control/control_loop.o
DETECTION_OBJS = detection/motion_detect.o
PWM_OBJS       = pmw/pmw_servo.o          # still linked for now (unused after PRU switch)
PRU_COMMS_OBJS = pru/pru_comms.o          # ARM/Linux side: writes the whiteboard
OBJS = $(CAPTURE_OBJS) $(CONTROL_OBJS) $(DETECTION_OBJS) $(PWM_OBJS) $(PRU_COMMS_OBJS)

# Live ASCII camera viewer (headless, no extra libs). Grabs frames exactly like
# the turret and renders a greyscale preview to the SSH terminal with '+' = centre
# and 'X' = motion centroid. Does NOT touch the PRU/servo. Great for sanity-checking
# the camera + detection before blaming the servo.
#   make cam_view && ./cam_view
capture/cam_view.o: capture/cam_view.c capture/capture.h detection/motion_detect.h
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ capture/cam_view.c

cam_view: capture/cam_view.o $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ capture/cam_view.o $(OBJS) $(LIBS)

.PHONY: all turret cam_view clean

all: $(OBJS)
	@echo "Objects built. Run: make turret   (to link the final binary)"

# Final on-board binary: camera + detection + control + pwm, no network.
# We compile each module with the shared include paths so headers resolve.
capture/v4l2.o: capture/v4l2.c capture/capture.h control/control_loop.h \
                pmw/pmw_servo.h detection/motion_detect.h
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ capture/v4l2.c

control/control_loop.o: control/control_loop.c control/control_loop.h
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ control/control_loop.c

detection/motion_detect.o: detection/motion_detect.c detection/motion_detect.h \
                           capture/capture.h
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ detection/motion_detect.c

pmw/pmw_servo.o: pmw/pmw_servo.c pmw/pmw_servo.h
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ pmw/pmw_servo.c

# ARM/Linux side of the PRU whiteboard. NOTE: this is NOT the PRU firmware
# (pru/pru_servo.c is built by pru-gcc, not here). This is normal ARM code that
# runs in the turret process and mmaps /dev/mem.
pru/pru_comms.o: pru/pru_comms.c pru/pru_comms.h
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ pru/pru_comms.c

turret: main.c $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ main.c $(OBJS) $(LIBS)

clean:
	rm -f turret main.o
	rm -f capture/*.o
	rm -f control/*.o control/*.a
	rm -f detection/*.o detection/*.a
	rm -f pmw/*.o pmw/*.a

# Top-level Makefile for Camera Turret Project
# Builds executables for both Debian laptop and BBB

CC = gcc
CFLAGS = -Wall -Wextra -g -O2

# Include paths
INCLUDES = -I. -I./capture -I./control -I./detection -I./pmw -I./websocket

# Libraries
LIBS = -lpthread -lm -lrt

# Paths
CAPTURE_DIR = capture
CONTROL_DIR = control
DETECTION_DIR = detection
PWM_DIR = pmw
WEBSOCKET_DIR = websocket

# Object files
CAPTURE_OBJS = $(CAPTURE_DIR)/v4l2.o
CONTROL_OBJS = $(CONTROL_DIR)/control_loop.o
DETECTION_OBJS = $(DETECTION_DIR)/color_threshold.o
PWM_OBJS = $(PWM_DIR)/pmw_servo.o
WEBSOCKET_SENDER_OBJS = $(WEBSOCKET_DIR)/position_sender.o
WEBSOCKET_RECEIVER_OBJS = $(WEBSOCKET_DIR)/position_reciever.o

# Default target
all: main_debian main_bbb

# Main Debian executable (capture + detection + sender)
main_debian: $(CAPTURE_OBJS) $(DETECTION_OBJS) $(WEBSOCKET_SENDER_OBJS)
	$(CC) $(CFLAGS) -o $@ $(CAPTURE_OBJS) $(DETECTION_OBJS) $(WEBSOCKET_SENDER_OBJS) $(LIBS)

# Main BBB executable (receiver + control + pwm)
main_bbb: $(WEBSOCKET_RECEIVER_OBJS) $(CONTROL_OBJS) $(PWM_OBJS)
	$(CC) $(CFLAGS) -o $@ $(WEBSOCKET_RECEIVER_OBJS) $(CONTROL_OBJS) $(PWM_OBJS)

# Compile rules for capture
$(CAPTURE_DIR)/v4l2.o: $(CAPTURE_DIR)/v4l2.c $(CAPTURE_DIR)/capture.h
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

# Compile rules for control
$(CONTROL_DIR)/control_loop.o: $(CONTROL_DIR)/control_loop.c $(CONTROL_DIR)/control_loop.h
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

# Compile rules for detection
$(DETECTION_DIR)/color_threshold.o: $(DETECTION_DIR)/color_threshold.c $(DETECTION_DIR)/color_threshold.h
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

# Compile rules for pwm
$(PWM_DIR)/pmw_servo.o: $(PWM_DIR)/pmw_servo.c $(PWM_DIR)/pmw_servo.h
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

# Compile rules for websocket sender
$(WEBSOCKET_DIR)/position_sender.o: $(WEBSOCKET_DIR)/position_sender.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

# Compile rules for websocket receiver
$(WEBSOCKET_DIR)/position_reciever.o: $(WEBSOCKET_DIR)/position_reciever.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

# Clean all built files
clean:
	rm -f main_debian main_bbb
	rm -f $(CAPTURE_DIR)/*.o
	rm -f $(CONTROL_DIR)/*.o
	rm -f $(DETECTION_DIR)/*.o
	rm -f $(PWM_DIR)/*.o
	rm -f $(WEBSOCKET_DIR)/*.o

# Deep clean - also remove libraries
deep-clean: clean
	rm -f $(CONTROL_DIR)/*.a
	rm -f $(DETECTION_DIR)/*.a
	rm -f $(PWM_DIR)/*.a

.PHONY: all clean deep-clean main_debian main_bbb
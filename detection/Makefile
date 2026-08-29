CC = gcc
CFLAGS = -Wall -Wextra -g
INCLUDES = -I..

# Object files (the detection module the top-level Makefile actually links).
OBJS = motion_detect.o

all: libdetection.a

libdetection.a: $(OBJS)
	ar rcs $@ $^

motion_detect.o: motion_detect.c motion_detect.h ../capture/capture.h
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ motion_detect.c

clean:
	rm -f *.o *.a

.PHONY: all clean

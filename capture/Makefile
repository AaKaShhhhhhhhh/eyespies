CC      = gcc
CFLAGS  = -Wall -Wextra -g

# Build just the capture module (no X11 / libv4l2all — V4L2 is used via kernel
# ioctl(), so no extra library is needed; those flags were for an old idea).
all: v4l2.o

v4l2.o: v4l2.c capture.h
	$(CC) $(CFLAGS) -I. -c -o $@ v4l2.c

clean:
	rm -f *.o v4l2

.PHONY: all clean

CC = gcc
CFLAGS = -Wall -Wextra -g

# Object files
OBJS = v4l2.o

all: v4l2

v4l2: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) -lpthread -lm -lrt -lX11 -lXext -lXfixes -lXrender -lXrandr -lXinerama -lXi -lXcursor -lv4l2all

v4l2.o: v4l2.c capture.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o v4l2

.PHONY: all clean
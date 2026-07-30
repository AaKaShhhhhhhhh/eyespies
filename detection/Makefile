CC = gcc
CFLAGS = -Wall -Wextra -g

# Object files
OBJS = color_threshold.o

all: libdetection.a

libdetection.a: $(OBJS)
	ar rcs $@ $^

color_threshold.o: color_threshold.c color_threshold.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o *.a

.PHONY: all clean
CC = gcc
CFLAGS = -Wall -Wextra -g

# Object files
OBJS = pmw_servo.o

all: libpwm.a

libpwm.a: $(OBJS)
	ar rcs $@ $^

pmw_servo.o: pmw_servo.c pmw_servo.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o *.a

.PHONY: all clean
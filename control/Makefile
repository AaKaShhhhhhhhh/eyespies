CC = gcc
CFLAGS = -Wall -Wextra -g

# Object files
OBJS = control_loop.o

all: libcontrol.a

libcontrol.a: $(OBJS)
	ar rcs $@ $^

control_loop.o: control_loop.c control_loop.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o *.a

.PHONY: all clean
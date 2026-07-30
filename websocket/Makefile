CC = gcc
CFLAGS = -Wall -Wextra -g
INCLUDES = -I../control -I../pmw

# Object files
OBJS = position_reciever.o

all: position_receiver

position_receiver: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

position_reciever.o: position_reciever.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

clean:
	rm -f *.o position_receiver

.PHONY: all clean
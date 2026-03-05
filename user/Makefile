CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread

all: arduino_bridge

arduino_bridge: arduino_bridge.c
	$(CC) $(CFLAGS) -o arduino_bridge arduino_bridge.c

clean:
	rm -f arduino_bridge

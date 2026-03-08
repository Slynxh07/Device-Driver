CC=gcc
CFLAGS=-Wall -O2 -pthread

all: keylogger_app

keylogger_app: keylogger_app.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f keylogger_app

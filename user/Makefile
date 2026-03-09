CC=gcc
# CC: the C compiler to use. Here we use the default GNU C compiler "gcc".

CFLAGS=-Wall -O2 -pthread
# CFLAGS: options passed to the compiler.
#  -Wall   : enable most compiler warnings (helps catch bugs).
#  -O2     : optimize the code for speed.
#  -pthread: enable POSIX threads support (needed for pthread_create, etc.).

all: keylogger_app
# "all" is the default target when you run "make".
# It says: to build "all", you need to build "keylogger_app".

keylogger_app: keylogger_app.c
	$(CC) $(CFLAGS) -o $@ $<
# Rule to build the "keylogger_app" program from keylogger_app.c.
#  - $(CC)      : use the compiler defined above (gcc).
#  - $(CFLAGS)  : use the flags defined above.
#  - -o $@      : output file name is the target ("keylogger_app").
#  - $<         : the first prerequisite (here, "keylogger_app.c").

clean:
	rm -f keylogger_app
# "clean" target removes the compiled program.
# We force remove the keylogger_app file to clean.
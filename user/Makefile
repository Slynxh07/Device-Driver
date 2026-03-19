CC = gcc
CFLAGS = -Wall -Wextra

USER_SRC = src/user.c src/key_handler.c
IOCTL_SRC = src/ioctl_controller.c

USER_OUT = build/user
IOCTL_OUT = build/ioctl_controller

all: $(USER_OUT) $(IOCTL_OUT)

$(USER_OUT): $(USER_SRC)
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude $(USER_SRC) -o $(USER_OUT)

$(IOCTL_OUT): $(IOCTL_SRC)
	mkdir -p build
	$(CC) $(CFLAGS) $(IOCTL_SRC) -o $(IOCTL_OUT)

clean:
	rm -rf build
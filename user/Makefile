CC = gcc

USER_SRC = src/user.c src/key_handler.c
USER_OUT = build/user

all: $(USER_OUT)

$(USER_OUT): $(USER_SRC)
	mkdir -p build
	$(CC) -Iinclude $(USER_SRC) -o $(USER_OUT) -lraylib -lm -lpthread -lGL -ldl -lrt -lX11

clean:
	rm -rf build
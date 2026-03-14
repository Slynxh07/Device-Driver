CC = gcc

SRC = src/user.c src/key_handler.c
OUT = build/user

all: $(OUT)

$(OUT): $(SRC)
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude $(SRC) -o $(OUT)

clean:
	rm -rf build/*
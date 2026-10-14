CC ?= gcc
CFLAGS ?= -std=c99 -O2 -Wall -Isrc $(shell pkg-config --cflags sdl2)
LDLIBS ?= $(shell pkg-config --libs sdl2) -lm

SRC = src/gba.c src/mem.c src/arm.c src/ppu.c src/apu.c src/bios.c src/main.c
OBJ = $(SRC:.c=.o)

all: boycottadvance-reborn

boycottadvance-reborn: $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) $(LDLIBS) -o $@

clean:
	rm -f $(OBJ) boycottadvance-reborn

.PHONY: all clean

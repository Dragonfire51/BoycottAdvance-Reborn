CC ?= gcc
CFLAGS ?= -std=c99 -O2 -Wall -Isrc $(shell pkg-config --cflags sdl2)
LDLIBS ?= $(shell pkg-config --libs sdl2) -lm

SRC = src/gba.c src/mem.c src/arm.c src/ppu.c src/apu.c src/bios.c src/main.c
OBJ = $(SRC:.c=.o)

# On Windows, embed assets/icon.ico into the executable via windres.
ifeq ($(OS),Windows_NT)
  RC ?= windres
  ICON_OBJ = src/icon.res
  OBJ += $(ICON_OBJ)
endif

all: boycottadvance-reborn

boycottadvance-reborn: $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) $(LDLIBS) -o $@

src/icon.res: src/icon.rc assets/icon.ico
	$(RC) -i $< -o $@

clean:
	rm -f $(OBJ) boycottadvance-reborn

.PHONY: all clean

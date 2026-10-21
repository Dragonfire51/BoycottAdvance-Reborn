CC ?= gcc
CFLAGS ?= -std=c99 -O2 -Wall -Isrc
LDLIBS ?= -lm

SRC = src/gba.c src/mem.c src/arm.c src/ppu.c src/apu.c src/bios.c src/fb.c src/video.c src/gui.c src/eeprom.c src/flash.c

ifneq ($(OS),Windows_NT)
  CFLAGS += $(shell pkg-config --cflags sdl2)
  LDLIBS += $(shell pkg-config --libs sdl2)
  SRC += src/main.c
  VK_LIBS = $(shell pkg-config --libs vulkan 2>/dev/null)
  ifneq ($(VK_LIBS),)
    CFLAGS += -DHAVE_VULKAN $(shell pkg-config --cflags vulkan 2>/dev/null)
    LDLIBS += $(VK_LIBS)
    SRC += src/vk.c
  endif
else
  RC ?= windres
  ICON_OBJ = src/icon.res
  SRC += src/win32.c
  LDLIBS += -lwinmm -ld3d9 -lgdi32 -luser32 -lcomdlg32
endif
OBJ = $(SRC:.c=.o) $(ICON_OBJ)

all: boycottadvance-reborn

boycottadvance-reborn: $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) $(LDLIBS) -o $@

src/icon.res: src/icon.rc assets/icon.ico
	$(RC) -O coff -i $< -o $@

clean:
	rm -f $(OBJ) boycottadvance-reborn

.PHONY: all clean

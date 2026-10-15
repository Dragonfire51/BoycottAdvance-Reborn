CC ?= gcc
CFLAGS ?= -std=c99 -O2 -Wall -Isrc $(shell pkg-config --cflags sdl2)
LDLIBS ?= $(shell pkg-config --libs sdl2) -lm

# Vulkan is used for the "Vulkan" frame-buffer backend when the SDK is present.
VK_LIBS = $(shell pkg-config --libs vulkan 2>/dev/null)
ifneq ($(VK_LIBS),)
  CFLAGS += -DHAVE_VULKAN $(shell pkg-config --cflags vulkan 2>/dev/null)
  LDLIBS += $(VK_LIBS)
endif

SRC = src/gba.c src/mem.c src/arm.c src/ppu.c src/apu.c src/bios.c src/fb.c src/video.c src/main.c
ifneq ($(VK_LIBS),)
  SRC += src/vk.c
endif
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

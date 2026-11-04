# OpenTBBlue: make (the opentbblue program), make test, make amiga.
# Needs a C compiler and SDL2 (on AmigaOS, DalsinAI/gfopensdl).

CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=gnu99 -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Isrc -include src/compat.h
LDLIBS  ?=
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null || sdl2-config --cflags)
SDL_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null || sdl2-config --libs)
PREFIX  ?= /usr/local

# The Next, from ZxNextEmu's core (DalsinAI/ZxNextEmu): the Z80N, the Next
# itself, its SD card and devices. zx.c also carries the classic Spectrums
# the Next's machine code is shared with.
CORE = src/z80/z80.c \
       src/zx/zx.c src/zx/beta.c src/zx/expansion.c src/zx/files.c src/zx/keys.c \
       src/zx/ay.c src/zx/tape.c src/zx/snap.c src/spec256/spec256.c src/spec256/palette.c \
       src/next/next.c src/next/uart.c src/next/video.c src/next/dma.c src/next/sd.c src/next/bootrom.c \
       src/storage/blockdev.c src/storage/vfat.c \
       src/devices/esp8266.c src/devices/nextpi.c src/devices/saa1099.c src/devices/sid.c \
       src/devices/gs.c src/devices/ata.c
NET  = src/frontend/net_posix.c
ifeq ($(OS),Windows_NT)
NET  = src/frontend/net_win32.c
NET_LIBS = -lws2_32
endif
HDRS = $(wildcard src/*.h src/*/*.h)
OWN  = src/opentbblue.c src/video.c src/setup.c src/fatimg.c

all: opentbblue

opentbblue: $(OWN) $(CORE) $(NET) $(HDRS)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(OWN) $(CORE) $(NET) $(SDL_LIBS) $(NET_LIBS) $(LDLIBS) -o $@

tests/setup_test: tests/setup_test.c src/setup.c src/setup.h src/fatimg.c src/fatimg.h $(CORE)
	$(CC) $(CFLAGS) tests/setup_test.c src/setup.c src/fatimg.c $(CORE) $(LDLIBS) -o $@

# Frame-for-frame hashes, for comparing two builds (tests/compare.sh).
tests/frames: tests/frames.c $(CORE) $(HDRS)
	$(CC) $(CFLAGS) tests/frames.c $(CORE) $(LDLIBS) -o $@

test: tests/setup_test
	./tests/setup_test

# AmigaOS 3.2, 68040: see docs/Amiga.md. Built with OpenAmigaGCC 16.2.0-open3
# (OpenNDK); -m68040 picks the soft-float libraries by itself. The core has
# no floating point at all. -fomit-frame-pointer frees a register for the
# CPU loop. SDL2 is the OpenGPU SDL2 stub (SDL2.module) from the stove, and
# the network is bsdsocket.library.
AMIGA_CC      ?= m68k-amigaos-gcc
AMIGA_CFLAGS  ?= -O3 -m68040 -mtune=68040 -fomit-frame-pointer -fno-strict-aliasing -fno-tree-loop-distribute-patterns -DNDEBUG
AMIGA_SDL     ?= $(dir $(shell which $(AMIGA_CC) 2>/dev/null))../m68k-amigaos
AMIGA_LDLIBS  ?= -noixemul -lm
amiga: opentbblue.68k

opentbblue.68k: $(OWN) $(CORE) src/frontend/net_amiga.c $(HDRS)
	$(AMIGA_CC) -noixemul $(AMIGA_CFLAGS) -std=gnu99 -Isrc -include src/compat.h -I$(AMIGA_SDL)/include/SDL2 \
	    $(OWN) $(CORE) src/frontend/net_amiga.c -L$(AMIGA_SDL)/lib -lSDL2 $(AMIGA_LDLIBS) -o $@

install: opentbblue
	install -D -m 755 opentbblue $(DESTDIR)$(PREFIX)/bin/opentbblue

clean:
	rm -f opentbblue opentbblue.exe opentbblue.68k tests/setup_test tests/frames

.PHONY: all test amiga install clean

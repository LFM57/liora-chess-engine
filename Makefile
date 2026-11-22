CC ?= gcc
WIN_CC ?= $(shell if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then echo x86_64-w64-mingw32-gcc; elif command -v zig >/dev/null 2>&1; then echo "zig cc -target x86_64-windows-gnu"; else echo x86_64-w64-mingw32-gcc; fi)

CFLAGS ?= -O3 -march=native -Wall -Wextra
WIN_CFLAGS ?= -O3 -march=native -Wall -Wextra
LDFLAGS = -lm
WIN_LDFLAGS = -lm

EMBED_NNUE ?= 0
NNUE_FILE ?= ../NNUE Weights/Gen 5/liora.nnue

ifeq ($(EMBED_NNUE),1)
    CFLAGS += -DEMBED_NNUE -DNNUE_EMBEDDED_FILE='"$(NNUE_FILE)"'
    WIN_CFLAGS += -DEMBED_NNUE -DNNUE_EMBEDDED_FILE='"$(NNUE_FILE)"'
endif

SRC = board.c main.c nnue.c nnue_embed.c polyglot.c search.c
OBJS = $(SRC:.c=.o)
WIN_OBJS = $(SRC:.c=.win.o)

TARGET = liora
TARGET_WIN = liora.exe

ifeq ($(OS),Windows_NT)
    TARGET := $(TARGET).exe
endif

DIST_DIR ?= ../dist
VERSION ?= $(shell grep '#define NAME' defs.h | sed -E 's/.*"Liora (v[0-9]+\.[0-9]+) ([CF])".*/\1_\2/g')

ifeq ($(EMBED_NNUE),1)
    DIST_SUFFIX = _NNUE
else
    DIST_SUFFIX =
endif

DIST_LINUX = $(DIST_DIR)/liora_$(VERSION)$(DIST_SUFFIX)
DIST_WINDOWS = $(DIST_DIR)/liora_$(VERSION)$(DIST_SUFFIX).exe

all: $(TARGET)

linux: $(TARGET)

windows: $(TARGET_WIN)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(TARGET_WIN): $(WIN_OBJS)
	$(WIN_CC) $(WIN_CFLAGS) $^ -o $@ $(WIN_LDFLAGS)
	@rm -f liora.pdb

dist: $(DIST_LINUX) $(DIST_WINDOWS)
	@echo "Compiled Linux and Windows binaries at $(DIST_DIR)"

$(DIST_LINUX): $(OBJS)
	@mkdir -p $(DIST_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@chmod +x $@ 2>/dev/null || true

$(DIST_WINDOWS): $(WIN_OBJS)
	@mkdir -p $(DIST_DIR)
	$(WIN_CC) $(WIN_CFLAGS) $^ -o $@ $(WIN_LDFLAGS)
	@rm -f $(DIST_DIR)/*.pdb

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

nnue_embed.o: nnue_embed.c incbin.h
	$(CC) $(CFLAGS) -c $< -o $@

%.win.o: %.c
	$(WIN_CC) $(WIN_CFLAGS) -c $< -o $@

nnue_embed.win.o: nnue_embed.c incbin.h
	$(WIN_CC) $(WIN_CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(TARGET_WIN) $(OBJS) $(WIN_OBJS) *.pdb

.PHONY: all linux windows dist clean

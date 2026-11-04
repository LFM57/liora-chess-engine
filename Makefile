CC ?= gcc
CFLAGS ?= -O3 -march=native -Wall -Wextra
LDFLAGS = -lm

EMBED_NNUE ?= 1
NNUE_FILE ?= ../NNUE Weights/Gen 4/liora_20.nnue

ifeq ($(EMBED_NNUE),1)
    CFLAGS += -DEMBED_NNUE -DNNUE_EMBEDDED_FILE='"$(NNUE_FILE)"'
endif

SRC = board.c main.c nnue.c nnue_embed.c polyglot.c search.c
OBJS = $(SRC:.c=.o)
TARGET = liora

ifeq ($(OS),Windows_NT)
    TARGET := $(TARGET).exe
endif

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

nnue_embed.o: nnue_embed.c incbin.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(TARGET).exe $(OBJS)

.PHONY: all clean

CC ?= gcc
CFLAGS = -O3 -Wall -Wextra
LDFLAGS = -lm

SRC = board.c main.c nnue.c polyglot.c search.c
TARGET = liora

ifeq ($(OS),Windows_NT)
    TARGET := $(TARGET).exe
endif

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGET) $(TARGET).exe *.o

.PHONY: all clean

# Makefile for the "writer" application (finder-app)
#
# Usage:
#   Native build:
#       make
#     or
#       make all
#
#   Cross-compile build (e.g. for aarch64 target):
#       make CROSS_COMPILE=aarch64-none-linux-gnu-
#
#   Clean build artifacts:
#       make clean
#
# CROSS_COMPILE is empty by default, which causes CC to resolve to the
# native "gcc". When CROSS_COMPILE is set on the command line, CC
# becomes e.g. "aarch64-none-linux-gnu-gcc", selecting the cross
# compiler installed in the toolchain.

CROSS_COMPILE ?=
CC := $(CROSS_COMPILE)gcc

CFLAGS ?= -Wall -Wextra -g -O0
LDFLAGS ?=

TARGET := writer
SRCS := writer.c
OBJS := $(SRCS:.c=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS)

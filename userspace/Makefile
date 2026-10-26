# SPDX-License-Identifier: GPL-2.0-only
# Makefile for userspace test binaries
#
# Build all test suites and the main runner.
# Usage:
#   make              — build everything
#   make clean        — remove binaries and objects
#   make CROSS=arm-linux-gnueabihf-  — cross-compile for BBB

CC       = $(CROSS)gcc
CFLAGS   = -Wall -Wextra -Wpedantic -std=c11 -g \
           -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L \
           -I.
LDFLAGS  = -lpthread

SRCS_FRAMEWORK = test_framework.c
SRCS_TESTS     = tests/test_basic_rw.c   \
                 tests/test_ioctl.c      \
                 tests/test_concurrent.c \
                 tests/test_i2c_eeprom.c

SRCS_MAIN = main_test.c

OBJS_FRAMEWORK = $(SRCS_FRAMEWORK:.c=.o)
OBJS_TESTS     = $(SRCS_TESTS:.c=.o)
OBJS_MAIN      = $(SRCS_MAIN:.c=.o)

TARGET = main_test

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS_FRAMEWORK) $(OBJS_TESTS) $(OBJS_MAIN)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Header dependencies
$(OBJS_FRAMEWORK) $(OBJS_TESTS) $(OBJS_MAIN): test_framework.h

clean:
	rm -f $(OBJS_FRAMEWORK) $(OBJS_TESTS) $(OBJS_MAIN) $(TARGET)

# Host tests for software/calibration.c.
#
# These tests are not part of the firmware build: they compile the real
# module with the host compiler and run it against a simulated EEPROM.
#
#   make -C software/test        build and run the tests
#   make -C software/test clean  remove the test binary
#
CC ?= cc
CFLAGS = -std=c11 -Wall -Wextra -O2 -I..
SRC = ../calibration.c test_calibration.c

.PHONY: all test clean

all: test_calibration

test_calibration: $(SRC) ../calibration.h ../config.h ../load.h ../systick.h
	$(CC) $(CFLAGS) $(SRC) -o $@

test: test_calibration
	./test_calibration

clean:
	rm -f test_calibration

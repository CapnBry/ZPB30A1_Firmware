# Host tests for the runtime calibration feature.
#
# These tests are not part of the firmware build: they compile the real
# modules with the host compiler and run them against a simulated EEPROM
# and a recorded display model.
#
#   make -C software/test        build and run the tests
#   make -C software/test clean  remove the test binaries
#
# test_wizard compiles the real ui.c on the host. A copy of ui.c is placed
# in gen/ so that its pin-mapping includes ("inc/stm8s_*.h") and adc.h
# resolve to the mocks in mock/ instead of the STM8 headers (the copy is
# needed because a quoted include always searches the source's own
# directory first). The interrupt attribute is compiled away with
# -D'__interrupt(x)='.
CC ?= cc
CFLAGS = -std=c11 -Wall -Wextra -O2 -I..
WIZ_CFLAGS = -Imock -std=c11 -Wall -Wextra -O2 -I.. -D'__interrupt(x)='
SRC = ../calibration.c test_calibration.c
WIZ_SRC = gen/ui_host.c ../menu_items.c ../calibration.c test_wizard.c

.PHONY: all test clean

all: test_calibration test_wizard

test_calibration: $(SRC) ../calibration.h ../config.h ../load.h ../systick.h
	$(CC) $(CFLAGS) $(SRC) -o $@

gen/ui_host.c: ../ui.c
	@mkdir -p gen
	cp $< $@

test_wizard: $(WIZ_SRC) ../ui.h ../menu_items.h ../calibration.h ../config.h \
             ../settings.h ../load.h ../systick.h \
             mock/adc.h mock/inc/stm8s_gpio.h mock/inc/stm8s_itc.h
	$(CC) $(WIZ_CFLAGS) $(WIZ_SRC) -o $@

test: test_calibration test_wizard
	./test_calibration
	./test_wizard

clean:
	rm -f test_calibration test_wizard
	rm -rf gen

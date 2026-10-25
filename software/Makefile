CC=sdcc
STM8FLASH=stm8flash
CFLAGS=-mstm8 --std-sdcc11 --opt-code-speed
DEFINES=STM8S005
PROCESSOR=STM8S005K6
STLINK_VERSION=2
FLASH_SIZE=32768
RAM_SIZE=2048

MAIN=electronic_load.c
SRC=tm1650.c uart.c utils.c fan.c ui.c systick.c load.c settings.c \
 	adc.c beeper.c menu_items.c calibration.c
BUILDDIR=build

SRC:=$(MAIN) $(SRC)
REL=$(patsubst %.c,$(BUILDDIR)/%.rel, $(SRC))
IHX=$(MAIN:%.c=$(BUILDDIR)/%.ihx)
HEX=$(IHX:.ihx=.hex)
DEP=$(REL:%.rel=%.d)

.PHONY: all bin size mkdir clean flash unlock clear_eeprom_unix clear_eeprom_windows

all: bin size

# SDCC includes working wrong on Windows with "/" slahes
ifeq ($(OS),Windows_NT)
ifeq ($(STLINK_VERSION),2)
PROGRAMMER=ST-LINK-V2
else
PROGRAMMER=ST-LINK
endif
flash: flash_windows
unlock: unlock_windows
clear_eeprom: clear_eeprom_windows
else
PROGRAMMER=stlinkv$(STLINK_VERSION)
flash: flash_unix
unlock: unlock_unix
clear_eeprom: clear_eeprom_unix
endif
CFLAGS+=$(INCLUDES) -D $(DEFINES)

mkdir:
	@mkdir -p $(BUILDDIR)

bin: mkdir $(IHX)

$(IHX): $(REL)
	$(CC) $(CFLAGS) $^ -o $@

#-MMD = automatically create dependency files
$(BUILDDIR)/%.rel: %.c
	$(CC) -c $(CFLAGS) -MMD $< -o $@

$(HEX): $(IHX)
	packihx $< > $@

# bin is a prerequisite to ensure the map file exists before it is read.
size: bin
	@echo "Memory usage:"
	@LC_ALL=C awk -v flash_size=$(FLASH_SIZE) -v ram_size=$(RAM_SIZE) \
		-f memusage.awk $(IHX:.ihx=.map)

clean:
	rm -rf $(BUILDDIR)

flash_windows: $(HEX)
	stvp_cmdline -BoardName=$(PROGRAMMER) -Device=$(PROCESSOR) -Port=USB -ProgMode=SWIM \
	-verbose -no_loop -no_log -FileProg=$<

flash_unix: $(IHX)
	$(STM8FLASH) -c $(PROGRAMMER) -p $(PROCESSOR) -w $<

unlock_windows:
	@echo "Unlocking is currently unsupported on windows"
	false

unlock_unix:
	$(STM8FLASH) -c $(PROGRAMMER) -p $(PROCESSOR) -u # Works in stm8flash versions after git commit f3f547b4

clear_eeprom_unix: empty_eeprom
	$(STM8FLASH) -c $(PROGRAMMER) -p $(PROCESSOR) -s eeprom -w $<

-include $(DEP)

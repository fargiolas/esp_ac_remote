TARGET=irdump

MCU=atmega328p
ARCH=-mmcu=$(MCU)
F_CPU=16000000UL
BAUD=115200
PORT=/dev/ttyACM0

PROGRAMMER=arduino

CC=avr-gcc
OBJCOPY=avr-objcopy
AVRSIZE=avr-size
AVRDUDE=avrdude

# -Os: optimize for size
CFLAGS=-Os -Wall -std=gnu99 -DF_CPU=$(F_CPU) $(ARCH)

SOURCES=$(TARGET).c
OBJECTS=$(SOURCES:.c=.o)

.PHONY: all clean flash upload debug monitor kill-monitor

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -R .eeprom -O ihex $< $@

$(TARGET).elf: $(OBJECTS)
	$(CC) $(ARCH) $^ -o $@

%.o: %.c Makefile
	$(CC) $(CFLAGS) $(ARCH) -c -o $@ $<

size: $(TARGET).elf
	$(AVRSIZE) --mcu=$(MCU) --format=avr $(TARGET).elf

flash: $(TARGET).hex
	$(AVRDUDE) -V -c $(PROGRAMMER) -p $(MCU) -D -P $(PORT) -b $(BAUD) -U flash:w:$<:i

kill-monitor:
	screen -ls | \
	grep avr >/dev/null 2>&1; \
	if [ $$? -eq 0 ] ; then screen -X -S avrmonitor quit; fi

monitor:
	gnome-terminal -e "screen -S avrmonitor $(PORT) 19200"

upload: kill-monitor flash size

all: kill-monitor $(TARGET).hex

debug:
	@echo
	@echo "Source files:"   $(SOURCES)
	@echo "MCU, F_CPU, BAUD:"  $(MCU), $(F_CPU), $(BAUD)
	@echo

clean:
	rm -f $(TARGET).elf $(TARGET).hex $(TARGET).obj $(TARGET).o

CC = gcc
CFLAGS = -Wall -DUSE_DEV_LIB -DUSE_LGPIO_LIB -g
LIBS = -lgpiod -llgpio -ludev

# Remove libvterm dependency
OBJS = main.o hwconfig.o EPD_7in5_V2.o lgpio_gpio.o editor.o epaper.o input_handler.o scren.o keymap.o font8x16.o

all: epd_test

epd_test: $(OBJS)
	$(CC) -o $@ $^ $(LIBS)

clean:
	rm -f *.o epd_test

.PHONY: all clean

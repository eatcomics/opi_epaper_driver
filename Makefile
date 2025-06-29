CC = gcc
CFLAGS = -Wall -DUSE_DEV_LIB -DUSE_LGPIO_LIB -g `pkg-config --cflags freetype2`
LIBS = -lgpiod -llgpio -ludev -lvterm `pkg-config --libs freetype2`

OBJS = main.o hwconfig.o EPD_7in5_V2.o lgpio_gpio.o pty.o vterm.o keyboard.o keymap.o font_loader.o
TEST_OBJS = test_system_fonts.o hwconfig.o EPD_7in5_V2.o lgpio_gpio.o font_loader.o

all: epd_test test_system_fonts

epd_test: $(OBJS)
	$(CC) -o $@ $^ $(LIBS)

test_system_fonts: $(TEST_OBJS)
	$(CC) -o $@ $^ -lgpiod -llgpio `pkg-config --libs freetype2`

clean:
	rm -f *.o epd_test test_system_fonts

.PHONY: all clean
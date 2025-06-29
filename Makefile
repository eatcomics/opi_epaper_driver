CC = gcc
CFLAGS = -Wall -DUSE_DEV_LIB -DUSE_LGPIO_LIB -g
LIBS = -lgpiod -llgpio -ludev -lvterm

OBJS = main.o hwconfig.o EPD_7in5_V2.o lgpio_gpio.o pty.o vterm.o keyboard.o keymap.o font8x16.o unicode_font.o
TEST_OBJS = test_japanese.o hwconfig.o EPD_7in5_V2.o lgpio_gpio.o unicode_font.o font8x16.o

all: epd_test test_japanese

epd_test: $(OBJS)
	$(CC) -o $@ $^ $(LIBS)

test_japanese: $(TEST_OBJS)
	$(CC) -o $@ $^ -lgpiod -llgpio

clean:
	rm -f *.o epd_test test_japanese

.PHONY: all clean
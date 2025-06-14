CC = gcc
CFLAGS = -Wall -DUSE_DEV_LIB -DUSE_LGPIO_LIB
LIBS = -lgpiod -llgpio

OBJS = main.o hwconfig.o EPD_7in5_V2.o lgpio_gpio.o

all: epd_test

epd_test: $(OBJS)
	$(CC) -o $@ $^ $(LIBS)

clean:
	rm -f *.o epd_test

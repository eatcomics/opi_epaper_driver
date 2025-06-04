CC = gcc
CFLAGS = -Wall -DUSE_DEV_LIB
LIBS = -lgpiod -llgpio

OBJS = main.o hwconfig.o EPD_7in5_V2.o lgpio_gpio.o dev_hardware_SPI.o

all: epd_test

epd_test: $(OBJS)
	$(CC) -o $@ $^ $(LIBS)

clean:
	rm -f *.o epd_test

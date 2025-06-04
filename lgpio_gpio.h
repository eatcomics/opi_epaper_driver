#ifndef __LGPIO_GPIO_H__
#define __LGPIO_GPIO_H__

#define GPIOD_IN  0
#define GPIOD_OUT 1

int GPIOD_Export_GPIO(void);
void GPIOD_Unexport_GPIO(void);

int GPIOD_Direction(int pin, int mode);
int GPIOD_Write(int pin, int value);
int GPIOD_Read(int pin);

void GPIOD_Export(int pin);   // No-op for compatibility
void GPIOD_Unexport(int pin); // No-op for compatibility

#endif


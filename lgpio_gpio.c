#include <lgpio.h>
#include <stdio.h>

static int gpio_handle = -1;

int GPIOD_Export_GPIO(void) {
    if (gpio_handle >= 0)
        return 0;

    // Try opening chip0 (usually right for Orange Pi)
    gpio_handle = lgGpiochipOpen(0);
    if (gpio_handle < 0) {
        perror("lgGpiochipOpen failed");
        return -1;
    }
    return 0;
}

void GPIOD_Unexport_GPIO(void) {
    if (gpio_handle >= 0) {
        lgGpiochipClose(gpio_handle);
        gpio_handle = -1;
    }
}

#define GPIOD_IN  0
#define GPIOD_OUT 1

int GPIOD_Direction(int pin, int mode) {
    if (gpio_handle < 0) return -1;

    if (mode == GPIOD_IN)
        return lgGpioClaimInput(gpio_handle, 0, pin);
    else
        return lgGpioClaimOutput(gpio_handle, 0, pin, 0);
}

int GPIOD_Write(int pin, int value) {
    if (gpio_handle < 0) return -1;
    return lgGpioWrite(gpio_handle, pin, value);
}

int GPIOD_Read(int pin) {
    if (gpio_handle < 0) return -1;
    return lgGpioRead(gpio_handle, pin);
}

void GPIOD_Export(int pin) {
    // Not needed with lgpio — no-op
}

void GPIOD_Unexport(int pin) {
    // Not needed with lgpio — no-op
}


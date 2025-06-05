#include "EPD_7in5_V2.h"
#include "hwconfig.h"
#include "lgpio_gpio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int screenwidth = 800;
int screenheight = 480;

int main (void) {
    if (DEV_Module_Init() != 0) {
        printf("Hardware init failed.\n");
    }

    // Init the e-ink display
    EPD_7IN5_V2_Init();
    
    // Clear the display
    EPD_7IN5_V2_Clear();

    // Creating an image buffer
    size_t buffer_size = screenwidth * screenheight / 8;
    UBYTE *image = (UBYTE *)malloc(buffer_size);
    if (!image) {
        printf("Failed to allocate memory\n");
        DEV_Module_Exit();
        return -1;
    }

    // Send image buffer to display and then free the image
    memset(image, 0xFF, buffer_size);
    EPD_7IN5_V2_Display(image);
    free(image);

    // Sleep the Display
    EPD_7IN5_V2_Sleep();

    DEV_Module_Exit();

    return 0;
}

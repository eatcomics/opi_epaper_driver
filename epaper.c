#include "epaper.h"
#include "settings.h"
#include "EPD_7in5_V2.h"
#include "hwconfig.h"

// Constructor/Destructor
int epaper_init(){
    if (DEV_Module_init() != 0) {
        printf("Hardware init failed.\n");
        return -1;
    }

    if (EPD_7IN5_V2_Init() != 0) {
        printf("E-ink display init fialed.\n");
        DEV_Module_Exit();
        return -1;
    }

    epaper_clear();
    
    return 0;
}

void epaper_destroy() {
    epaper_sleep();
    DEV_Module_Exit();
}

void epaper_clear() {
    EPD_7IN5_V2_Clear(); 
}

void epaper_sleep() {
    printf("Sleeping Screen...\n");
    EPD_7IN5_V2_Sleep();
}

void epaper_redraw(uint8_t *buffer) {
    
}

void epaper_draw_char() {
    
}

void epaper_draw_rect(int cur_x, int cur_y) {
    
}


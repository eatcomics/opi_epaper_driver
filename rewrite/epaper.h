#ifndef __EPAPER_H
#define __EPAPER_H
#include <stdint.h>

int epaper_init();
void epaper_destroy();

// Display functions
void epaper_clear();
void epaper_redraw(uint8_t *frambeuffer);
void epaper_sleep();
void epaper_draw_char();
void epaper_draw_cursor(int cur_x, int cur_y); // Cursor

#endif

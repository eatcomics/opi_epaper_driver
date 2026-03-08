#ifndef __SCREEN_H
#define __SCREEN_H

#include <stdlib.h>
#include <stdint.h>

int screen_init();
void screen_destroy();

int handle_screen(uint8_t *doc, size_t doc_len, size_t cursor_pos);

#endif 

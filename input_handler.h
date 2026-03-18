#ifndef __INPUT_HANDLER_H
#define __INPUT_HANDLER_H

#include <stdint.h>
#include <stdlib.h>

int keyboard_init();
void keyboard_close();

int read_key_event(uint32_t *keycode, int *modifiers);
uint8_t check_keys();

#endif

#ifndef __INPUT_HANDLER_H
#define __INPUT_HANDLER_H

#include <stdint.h>
#include <stdlib.h>

int keyboard_init(void);
void keyboard_close(void);

int read_key_event(uint32_t *keycode, int *modifiers);
size_t check_keys(uint8_t *buf);

#endif

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

int keyboard_init(void);
void keyboard_close(void);

// Blocks or polls for key event (depending on implementation)
int read_key_event(uint32_t *keycode, int *modifiers);

#endif // KEYBOARD_H

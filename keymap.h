#ifndef KEYMAP_H
#define KEYMAP_H

#include <stdint.h>
#include "vterm.h"

VTermKey convert_keycode_to_vtermkey(uint32_t evdev_code);

#endif // KEYMAP_H

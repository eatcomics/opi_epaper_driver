#include "keymap.h"
#include <linux/input-event-codes.h>

VTermKey convert_keycode_to_vtermkey(uint32_t code) {
    switch (code) {
        case KEY_ENTER: return VTERM_KEY_ENTER;
        case KEY_BACKSPACE: return VTERM_KEY_BACKSPACE;
        case KEY_TAB: return VTERM_KEY_TAB;
        case KEY_ESC: return VTERM_KEY_ESCAPE;

        case KEY_UP: return VTERM_KEY_UP;
        case KEY_DOWN: return VTERM_KEY_DOWN;
        case KEY_LEFT: return VTERM_KEY_LEFT;
        case KEY_RIGHT: return VTERM_KEY_RIGHT;

        case KEY_HOME: return VTERM_KEY_HOME;
        case KEY_END: return VTERM_KEY_END;
        case KEY_PAGEUP: return VTERM_KEY_PAGEUP;
        case KEY_PAGEDOWN: return VTERM_KEY_PAGEDOWN;
        case KEY_INSERT: return VTERM_KEY_INSERT;
        case KEY_DELETE: return VTERM_KEY_DEL;

        case KEY_F1: return VTERM_KEY_FUNCTION(1);
        case KEY_F2: return VTERM_KEY_FUNCTION(2);
        case KEY_F3: return VTERM_KEY_FUNCTION(3);
        case KEY_F4: return VTERM_KEY_FUNCTION(4);
        case KEY_F5: return VTERM_KEY_FUNCTION(5);
        case KEY_F6: return VTERM_KEY_FUNCTION(6);
        case KEY_F7: return VTERM_KEY_FUNCTION(7);
        case KEY_F8: return VTERM_KEY_FUNCTION(8);
        case KEY_F9: return VTERM_KEY_FUNCTION(9);
        case KEY_F10: return VTERM_KEY_FUNCTION(10);
        case KEY_F11: return VTERM_KEY_FUNCTION(11);
        case KEY_F12: return VTERM_KEY_FUNCTION(12);

        default:
            return VTERM_KEY_NONE;
    }
}

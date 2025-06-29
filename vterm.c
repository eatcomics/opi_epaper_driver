#include "vterm.h"
#include "EPD_7in5_V2.h"
#include "font8x16.h"
#include "keymap.h"
#include <vterm.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <linux/input-event-codes.h>

#define CELL_WIDTH 8
#define CELL_HEIGHT 16

#define COLOR_WHITE 0
#define COLOR_BLACK 1

// Internal buffer
static uint8_t *vterm_buffer = NULL;
static size_t buffer_size = 0;

// Vterm variables
static VTerm *vterm = NULL;
static VTermScreen *screen = NULL;
static int term_rows, term_cols;
static int pty_fd = -1;
static int damage_pending = 0;

// Forward declarations
static void render_cell(int col, int row, const VTermScreenCell *cell);
static int damage_callback(VTermRect rect, void *user);

// Complete key mapping table for Linux input event codes to ASCII
static char keycode_to_ascii(uint32_t keycode, int shift_pressed) {
    // Handle letters (KEY_Q=16, KEY_W=17, KEY_E=18, etc.)
    switch (keycode) {
        // QWERTY row 1
        case KEY_Q: return shift_pressed ? 'Q' : 'q';           // 16
        case KEY_W: return shift_pressed ? 'W' : 'w';           // 17
        case KEY_E: return shift_pressed ? 'E' : 'e';           // 18
        case KEY_R: return shift_pressed ? 'R' : 'r';           // 19
        case KEY_T: return shift_pressed ? 'T' : 't';           // 20
        case KEY_Y: return shift_pressed ? 'Y' : 'y';           // 21
        case KEY_U: return shift_pressed ? 'U' : 'u';           // 22
        case KEY_I: return shift_pressed ? 'I' : 'i';           // 23
        case KEY_O: return shift_pressed ? 'O' : 'o';           // 24
        case KEY_P: return shift_pressed ? 'P' : 'p';           // 25
        
        // QWERTY row 2
        case KEY_A: return shift_pressed ? 'A' : 'a';           // 30
        case KEY_S: return shift_pressed ? 'S' : 's';           // 31
        case KEY_D: return shift_pressed ? 'D' : 'd';           // 32
        case KEY_F: return shift_pressed ? 'F' : 'f';           // 33
        case KEY_G: return shift_pressed ? 'G' : 'g';           // 34
        case KEY_H: return shift_pressed ? 'H' : 'h';           // 35
        case KEY_J: return shift_pressed ? 'J' : 'j';           // 36
        case KEY_K: return shift_pressed ? 'K' : 'k';           // 37
        case KEY_L: return shift_pressed ? 'L' : 'l';           // 38
        
        // QWERTY row 3
        case KEY_Z: return shift_pressed ? 'Z' : 'z';           // 44
        case KEY_X: return shift_pressed ? 'X' : 'x';           // 45
        case KEY_C: return shift_pressed ? 'C' : 'c';           // 46
        case KEY_V: return shift_pressed ? 'V' : 'v';           // 47
        case KEY_B: return shift_pressed ? 'B' : 'b';           // 48
        case KEY_N: return shift_pressed ? 'N' : 'n';           // 49
        case KEY_M: return shift_pressed ? 'M' : 'm';           // 50
    }
    
    // Handle numbers and their shifted symbols
    switch (keycode) {
        case KEY_1: return shift_pressed ? '!' : '1';           // 2
        case KEY_2: return shift_pressed ? '@' : '2';           // 3
        case KEY_3: return shift_pressed ? '#' : '3';           // 4
        case KEY_4: return shift_pressed ? '$' : '4';           // 5
        case KEY_5: return shift_pressed ? '%' : '5';           // 6
        case KEY_6: return shift_pressed ? '^' : '6';           // 7
        case KEY_7: return shift_pressed ? '&' : '7';           // 8
        case KEY_8: return shift_pressed ? '*' : '8';           // 9
        case KEY_9: return shift_pressed ? '(' : '9';           // 10
        case KEY_0: return shift_pressed ? ')' : '0';           // 11
    }
    
    // Handle special characters and punctuation
    switch (keycode) {
        case KEY_SPACE: return ' ';                             // 57
        case KEY_MINUS: return shift_pressed ? '_' : '-';       // 12
        case KEY_EQUAL: return shift_pressed ? '+' : '=';       // 13
        case KEY_LEFTBRACE: return shift_pressed ? '{' : '[';   // 26
        case KEY_RIGHTBRACE: return shift_pressed ? '}' : ']';  // 27
        case KEY_BACKSLASH: return shift_pressed ? '|' : '\\';  // 43
        case KEY_SEMICOLON: return shift_pressed ? ':' : ';';   // 39
        case KEY_APOSTROPHE: return shift_pressed ? '"' : '\''; // 40
        case KEY_GRAVE: return shift_pressed ? '~' : '`';       // 41
        case KEY_COMMA: return shift_pressed ? '<' : ',';       // 51
        case KEY_DOT: return shift_pressed ? '>' : '.';         // 52
        case KEY_SLASH: return shift_pressed ? '?' : '/';       // 53
        default: return 0;
    }
}

// Utility function for libvterm compatibility
int vterm_unicode_to_utf8(uint32_t codepoint, char *buffer) {
    printf("LOG: vterm_unicode_to_utf8 called with codepoint=0x%x\n", codepoint);
    if (codepoint < 0x80) {
        buffer[0] = codepoint;
        return 1;
    } else if (codepoint < 0x800) {
        buffer[0] = 0xC0 | (codepoint >> 6);
        buffer[1] = 0x80 | (codepoint & 0x3F);
        return 2;
    } else if (codepoint < 0x10000) {
        buffer[0] = 0xE0 | (codepoint >> 12);
        buffer[1] = 0x80 | ((codepoint >> 6) & 0x3F);
        buffer[2] = 0x80 | (codepoint & 0x3F);
        return 3;
    } else if (codepoint < 0x110000) {
        buffer[0] = 0xF0 | (codepoint >> 18);
        buffer[1] = 0x80 | ((codepoint >> 12) & 0x3F);
        buffer[2] = 0x80 | ((codepoint >> 6) & 0x3F);
        buffer[3] = 0x80 | (codepoint & 0x3F);
        return 4;
    } else {
        return 0;
    }
}

int vterm_init(int rows, int cols, int pty, uint8_t *buffer) {
    printf("LOG: vterm_init START - rows=%d, cols=%d, pty=%d, buffer=%p\n", rows, cols, pty, buffer);
    
    if (!buffer) {
        printf("Error: vterm_init called with NULL buffer\n");
        return -1;
    }

    if (rows <= 0 || cols <= 0) {
        printf("Error: invalid terminal dimensions %dx%d\n", cols, rows);
        return -1;
    }

    // Clean up any existing state
    if (vterm) {
        printf("LOG: Cleaning up existing vterm\n");
        vterm_destroy();
    }

    term_rows = rows;
    term_cols = cols;
    pty_fd = pty;
    vterm_buffer = buffer;
    buffer_size = (EPD_7IN5_V2_WIDTH * EPD_7IN5_V2_HEIGHT) / 8;

    printf("Terminal dimensions: %dx%d\n", cols, rows);
    printf("Buffer size: %zu bytes\n", buffer_size);

    // Create libvterm instance
    printf("LOG: Creating libvterm instance\n");
    vterm = vterm_new(rows, cols);
    if (!vterm) {
        printf("Error: Failed to create libvterm instance\n");
        return -1;
    }
    printf("LOG: libvterm instance created successfully\n");

    // Configure libvterm
    printf("LOG: Configuring libvterm\n");
    vterm_set_utf8(vterm, 1);
    printf("LOG: UTF-8 mode set\n");
    
    // Get screen interface
    printf("LOG: Getting screen interface\n");
    screen = vterm_obtain_screen(vterm);
    if (!screen) {
        printf("Error: Failed to obtain libvterm screen\n");
        vterm_free(vterm);
        vterm = NULL;
        return -1;
    }
    printf("LOG: Screen interface obtained\n");

    // Set up callbacks - CRITICAL: Initialize all fields to NULL first
    printf("LOG: Setting up callbacks\n");
    VTermScreenCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.damage = damage_callback;
    
    vterm_screen_set_callbacks(screen, &callbacks, screen);
    printf("LOG: Callbacks set\n");

    // Reset and initialize
    printf("LOG: Resetting screen\n");
    vterm_screen_reset(screen, 1);
    printf("LOG: Screen reset complete\n");

    // Clear the framebuffer
    printf("LOG: Clearing framebuffer\n");
    memset(buffer, 0xFF, buffer_size);
    printf("LOG: Framebuffer cleared\n");

    damage_pending = 0;

    printf("libvterm terminal initialized successfully\n");
    return 0;
}

void vterm_destroy(void) {
    printf("LOG: vterm_destroy START\n");
    if (vterm) {
        printf("Destroying libvterm terminal\n");
        vterm_free(vterm);
        vterm = NULL;
        screen = NULL;
    }
    
    vterm_buffer = NULL;
    buffer_size = 0;
    damage_pending = 0;
    printf("LOG: vterm_destroy COMPLETE\n");
}

void vterm_feed_output(const char *data, size_t len, uint8_t *buffer) {
    printf("LOG: vterm_feed_output START - len=%zu, buffer=%p\n", len, buffer);
    
    if (!data || len == 0 || !buffer || !vterm) {
        printf("LOG: vterm_feed_output - invalid parameters\n");
        return;
    }
    
    vterm_buffer = buffer;
    
    // Feed data to libvterm
    printf("LOG: Feeding %zu bytes to libvterm\n", len);
    vterm_input_write(vterm, data, len);
    printf("LOG: Data fed to libvterm successfully\n");
    
    damage_pending = 1;
    printf("LOG: vterm_feed_output COMPLETE\n");
}

void vterm_process_input(uint32_t keycode, int modifiers) {
    printf("LOG: vterm_process_input START - keycode=%u, modifiers=%d\n", keycode, modifiers);
    
    if (pty_fd < 0 || !vterm) {
        printf("LOG: vterm_process_input - invalid state (pty_fd=%d, vterm=%p)\n", pty_fd, vterm);
        return;
    }

    printf("Processing key: %u, modifiers: %d\n", keycode, modifiers);

    // Check for modifier keys
    int ctrl_pressed = (modifiers & 0x04) != 0;   // Ctrl
    int shift_pressed = (modifiers & 0x01) != 0;  // Shift
    int alt_pressed = (modifiers & 0x08) != 0;    // Alt
    
    // Convert modifiers to VTerm format
    VTermModifier vterm_mods = VTERM_MOD_NONE;
    if (shift_pressed) vterm_mods |= VTERM_MOD_SHIFT;
    if (ctrl_pressed) vterm_mods |= VTERM_MOD_CTRL;
    if (alt_pressed) vterm_mods |= VTERM_MOD_ALT;

    // Handle special keys first
    printf("LOG: Converting keycode to VTerm key\n");
    VTermKey vterm_key = convert_keycode_to_vtermkey(keycode);
    if (vterm_key != VTERM_KEY_NONE) {
        printf("Sending special key via libvterm\n");
        vterm_keyboard_key(vterm, vterm_key, vterm_mods);
        
        // REMOVED: vterm_output_read() calls that were causing segfault
        printf("LOG: Special key processed, skipping output read\n");
        return;
    }

    // Handle printable characters
    printf("LOG: Converting keycode to ASCII\n");
    char ascii_char = keycode_to_ascii(keycode, shift_pressed);
    if (ascii_char != 0) {
        if (ctrl_pressed && ascii_char >= 'a' && ascii_char <= 'z') {
            // Convert to control character
            char ctrl_char = ascii_char - 'a' + 1;
            printf("Sending Ctrl+%c (0x%02x)\n", ascii_char, ctrl_char);
            write(pty_fd, &ctrl_char, 1);
        } else if (ctrl_pressed && ascii_char >= 'A' && ascii_char <= 'Z') {
            // Convert to control character
            char ctrl_char = ascii_char - 'A' + 1;
            printf("Sending Ctrl+%c (0x%02x)\n", ascii_char, ctrl_char);
            write(pty_fd, &ctrl_char, 1);
        } else {
            // Send directly to PTY instead of through libvterm
            printf("Sending ASCII directly: '%c' (0x%02x)\n", ascii_char, ascii_char);
            write(pty_fd, &ascii_char, 1);
        }
    } else {
        printf("Unhandled keycode: %u\n", keycode);
    }
    
    printf("LOG: vterm_process_input COMPLETE\n");
}

void vterm_redraw(uint8_t *buffer) {
    printf("LOG: vterm_redraw START - buffer=%p\n", buffer);
    
    if (!buffer || !screen) {
        printf("vterm_redraw: invalid parameters (buffer=%p, screen=%p)\n", buffer, screen);
        return;
    }
    
    vterm_buffer = buffer;
    
    // Clear the framebuffer to white
    printf("LOG: Clearing framebuffer\n");
    memset(buffer, 0xFF, buffer_size);
    
    VTermScreenCell cell;
    int rendered_chars = 0;
    
    printf("LOG: Starting cell rendering loop\n");
    for (int row = 0; row < term_rows; row++) {
        for (int col = 0; col < term_cols; col++) {
            VTermPos pos = {.row = row, .col = col};
            
            // Initialize cell to safe defaults
            memset(&cell, 0, sizeof(cell));
            
            printf("LOG: Getting cell at row=%d, col=%d\n", row, col);
            if (vterm_screen_get_cell(screen, pos, &cell)) {
                if (cell.chars[0] != 0) {
                    printf("LOG: Rendering cell with char=0x%x\n", cell.chars[0]);
                    render_cell(col, row, &cell);
                    rendered_chars++;
                }
            } else {
                printf("LOG: Failed to get cell at row=%d, col=%d\n", row, col);
            }
        }
    }
    
    printf("Rendered %d non-empty cells\n", rendered_chars);
    printf("LOG: Calling flush_display\n");
    flush_display();
    damage_pending = 0;
    printf("LOG: vterm_redraw COMPLETE\n");
}

void flush_display(void) {
    printf("LOG: flush_display START\n");
    if (vterm_buffer) {
        printf("LOG: Calling EPD_7IN5_V2_Display\n");
        EPD_7IN5_V2_Display(vterm_buffer);
        printf("LOG: EPD_7IN5_V2_Display returned\n");
    } else {
        printf("LOG: flush_display - no buffer\n");
    }
    printf("LOG: flush_display COMPLETE\n");
}

int vterm_has_pending_damage(void) {
    return damage_pending;
}

void set_pixel(int x, int y, int color) {
    if (!vterm_buffer || x < 0 || x >= EPD_7IN5_V2_WIDTH || y < 0 || y >= EPD_7IN5_V2_HEIGHT) {
        return;
    }
    
    int byte_index = (y * EPD_7IN5_V2_WIDTH + x) / 8;
    int bit_index = 7 - (x % 8);
    
    if (byte_index < 0 || byte_index >= buffer_size) {
        return;
    }
    
    if (color == COLOR_BLACK) {
        vterm_buffer[byte_index] &= ~(1 << bit_index);
    } else {
        vterm_buffer[byte_index] |= (1 << bit_index);
    }
}

void draw_rect(int x, int y, int w, int h, int color) {
    for (int dy = 0; dy < h; dy++) {
        for (int dx = 0; dx < w; dx++) {
            set_pixel(x + dx, y + dy, color);
        }
    }
}

void draw_char_fallback(int x, int y, char ch, int color) {
    extern const uint8_t font8x16[96][16];
    
    if (ch < 0x20 || ch > 0x7F) {
        ch = '?';  // Replace unprintable characters with '?'
    }
    
    const uint8_t *glyph = font8x16[ch - 0x20];

    for (int row = 0; row < CELL_HEIGHT; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < CELL_WIDTH; col++) {
            if (bits & (1 << (7 - col))) {
                set_pixel(x + col, y + row, color);
            }
        }
    }
}

// --- Internal Functions ---

static int damage_callback(VTermRect rect, void *user) {
    (void)user; // Suppress unused parameter warning
    
    printf("libvterm damage callback: rows %d-%d, cols %d-%d\n", 
           rect.start_row, rect.end_row, rect.start_col, rect.end_col);
    
    damage_pending = 1;
    return 1;
}

static void render_cell(int col, int row, const VTermScreenCell *cell) {
    printf("LOG: render_cell START - col=%d, row=%d, cell=%p\n", col, row, cell);
    
    if (!cell || !vterm_buffer) {
        printf("LOG: render_cell - invalid parameters\n");
        return;
    }
    
    int x = col * CELL_WIDTH;
    int y = row * CELL_HEIGHT;

    // Bounds checking
    if (x < 0 || y < 0 || x >= EPD_7IN5_V2_WIDTH || y >= EPD_7IN5_V2_HEIGHT) {
        printf("LOG: render_cell - out of bounds x=%d, y=%d\n", x, y);
        return;
    }

    // Determine colors
    int fg_color = COLOR_BLACK;
    int bg_color = COLOR_WHITE;
    
    // Handle reverse video
    if (cell->attrs.reverse) {
        fg_color = COLOR_WHITE;
        bg_color = COLOR_BLACK;
    }

    // Draw background
    if (bg_color == COLOR_BLACK) {
        printf("LOG: Drawing black background\n");
        draw_rect(x, y, CELL_WIDTH, CELL_HEIGHT, COLOR_BLACK);
    }

    // Draw character if present
    if (cell->chars[0] != 0) {
        printf("LOG: Converting unicode to UTF-8\n");
        char ch[5] = {0};
        int len = vterm_unicode_to_utf8(cell->chars[0], ch);
        if (len > 0 && ch[0] >= 0x20 && ch[0] <= 0x7F) {
            printf("LOG: Drawing character '%c'\n", ch[0]);
            draw_char_fallback(x, y, ch[0], fg_color);
        }
    }
    
    // Draw underline if needed
    if (cell->attrs.underline) {
        printf("LOG: Drawing underline\n");
        draw_rect(x, y + CELL_HEIGHT - 2, CELL_WIDTH, 1, fg_color);
    }
    
    printf("LOG: render_cell COMPLETE\n");
}
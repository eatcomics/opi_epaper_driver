#include "vterm.h"
#include "EPD_7in5_V2.h"
#include "font_loader.h"
#include "keymap.h"
#include <vterm.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#define CELL_WIDTH 8
#define CELL_HEIGHT 16

#define COLOR_WHITE 0
#define COLOR_BLACK 1

// Internal buffer
static uint8_t *vterm_buffer = NULL;

// Vterm variables
static VTerm *vterm = NULL;
static VTermScreen *screen = NULL;
static int term_rows, term_cols;
static int pty_fd = -1;

// Forward declarations
static void render_cell(int col, int row, const VTermScreenCell *cell);
static int damage_callback(VTermRect rect, void *user);

// Utility function that's optional in libvterm. Putting it here so you don't
// have to recompile it with the option
int vterm_unicode_to_utf8(uint32_t codepoint, char *buffer) {
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

// UTF-8 to Unicode conversion
uint32_t utf8_to_unicode(const char *utf8, int *bytes_consumed) {
    const unsigned char *s = (const unsigned char *)utf8;
    uint32_t codepoint = 0;
    
    if (s[0] < 0x80) {
        // 1-byte sequence (ASCII)
        codepoint = s[0];
        *bytes_consumed = 1;
    } else if ((s[0] & 0xE0) == 0xC0) {
        // 2-byte sequence
        codepoint = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
        *bytes_consumed = 2;
    } else if ((s[0] & 0xF0) == 0xE0) {
        // 3-byte sequence
        codepoint = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        *bytes_consumed = 3;
    } else if ((s[0] & 0xF8) == 0xF0) {
        // 4-byte sequence
        codepoint = ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        *bytes_consumed = 4;
    } else {
        // Invalid UTF-8
        codepoint = 0xFFFD; // Replacement character
        *bytes_consumed = 1;
    }
    
    return codepoint;
}

int vterm_init(int rows, int cols, int pty, uint8_t *buffer) {
    term_rows = rows;
    term_cols = cols;
    pty_fd = pty;
    vterm_buffer = buffer;

    // Initialize font loader
    if (font_loader_init() != 0) {
        printf("Warning: Font loader initialization failed, using fallback\n");
    }

    // Clear the buffer initially
    memset(buffer, 0xFF, (EPD_7IN5_V2_WIDTH * EPD_7IN5_V2_HEIGHT) / 8);

    vterm = vterm_new(rows, cols);
    if (!vterm) return -1;

    vterm_set_utf8(vterm, 1);
    screen = vterm_obtain_screen(vterm);

    VTermScreenCallbacks callbacks = {
        .damage = damage_callback,
    };
    vterm_screen_set_callbacks(screen, &callbacks, screen);
    vterm_screen_reset(screen, 1);
    return 0;
}

void vterm_destroy(void) {
    if (vterm)
        vterm_free(vterm);
    font_loader_cleanup();
}

void vterm_feed_output(const char *data, size_t len, uint8_t *buffer) {
    vterm_buffer = buffer;
    vterm_input_write(vterm, data, len);
}

void vterm_process_input(uint32_t keycode, int modifiers) {
    if (keycode >= 32 && keycode < 127) {
        // Printable ASCII character
        char ch = (char)keycode;
        write(pty_fd, &ch, 1);
    } else {
        // Special keys
        VTermKey key = convert_keycode_to_vtermkey(keycode);
        if (key != VTERM_KEY_NONE) {
            // Convert VTerm key to escape sequence and send to PTY
            char seq[16];
            int len = 0;
            
            switch (key) {
                case VTERM_KEY_ENTER:
                    seq[0] = '\r';
                    len = 1;
                    break;
                case VTERM_KEY_BACKSPACE:
                    seq[0] = '\b';
                    len = 1;
                    break;
                case VTERM_KEY_TAB:
                    seq[0] = '\t';
                    len = 1;
                    break;
                case VTERM_KEY_ESCAPE:
                    seq[0] = '\x1b';
                    len = 1;
                    break;
                case VTERM_KEY_UP:
                    strcpy(seq, "\x1b[A");
                    len = 3;
                    break;
                case VTERM_KEY_DOWN:
                    strcpy(seq, "\x1b[B");
                    len = 3;
                    break;
                case VTERM_KEY_RIGHT:
                    strcpy(seq, "\x1b[C");
                    len = 3;
                    break;
                case VTERM_KEY_LEFT:
                    strcpy(seq, "\x1b[D");
                    len = 3;
                    break;
                default:
                    break;
            }
            
            if (len > 0) {
                write(pty_fd, seq, len);
            }
        }
    }
}

void vterm_redraw(uint8_t *buffer) {
    vterm_buffer = buffer;
    VTermScreenCell cell;
    for (int row = 0; row < term_rows; row++) {
        for (int col = 0; col < term_cols; col++) {
            VTermPos pos = {.row = row, .col = col};
            if (vterm_screen_get_cell(screen, pos, &cell)) {
                render_cell(col, row, &cell);
            }
        }
    }
    flush_display();
}

void flush_display(void) {
    if (vterm_buffer) {
        EPD_7IN5_V2_Display(vterm_buffer);
    }
}

void set_pixel(int x, int y, int color) {
    if (x < 0 || x >= EPD_7IN5_V2_WIDTH || y < 0 || y >= EPD_7IN5_V2_HEIGHT) return;
    
    int byte_index = (y * EPD_7IN5_V2_WIDTH + x) / 8;
    int bit_index = 7 - (x % 8);
    
    if (color == COLOR_BLACK) {
        vterm_buffer[byte_index] &= ~(1 << bit_index); // Set bit to 0 for black
    } else {
        vterm_buffer[byte_index] |= (1 << bit_index);  // Set bit to 1 for white
    }
}

void draw_rect(int x, int y, int w, int h, int color) {
    for (int dy = 0; dy < h; dy++) {
        for (int dx = 0; dx < w; dx++) {
            set_pixel(x + dx, y + dy, color);
        }
    }
}

void draw_unicode_char(int x, int y, uint32_t codepoint, int color) {
    int width, height, advance;
    const uint8_t *bitmap = get_char_bitmap(codepoint, &width, &height, &advance);
    
    if (!bitmap) {
        // Fallback: draw a simple box
        draw_rect(x, y, CELL_WIDTH, CELL_HEIGHT, COLOR_WHITE);
        draw_rect(x, y, CELL_WIDTH, 1, COLOR_BLACK);
        draw_rect(x, y + CELL_HEIGHT - 1, CELL_WIDTH, 1, COLOR_BLACK);
        draw_rect(x, y, 1, CELL_HEIGHT, COLOR_BLACK);
        draw_rect(x + CELL_WIDTH - 1, y, 1, CELL_HEIGHT, COLOR_BLACK);
        return;
    }

    // Clear background first
    draw_rect(x, y, CELL_WIDTH, CELL_HEIGHT, COLOR_WHITE);

    // Draw character bitmap
    for (int row = 0; row < height && row < CELL_HEIGHT; row++) {
        for (int col = 0; col < width && col < CELL_WIDTH; col++) {
            if (bitmap[row * width + col]) {
                set_pixel(x + col, y + row, color);
            }
        }
    }
}

// --- Internal Functions ---

static int damage_callback(VTermRect rect, void *user) {
    VTermScreen *screen = (VTermScreen *)user;

    for (int row = rect.start_row; row < rect.end_row; row++) {
        for (int col = rect.start_col; col < rect.end_col; col++) {
            VTermPos pos = {.row = row, .col = col};
            VTermScreenCell cell;
            if (vterm_screen_get_cell(screen, pos, &cell)) {
                render_cell(col, row, &cell);
            }
        }
    }

    // Don't flush display here - let the main loop handle it
    return 1;
}

static void render_cell(int col, int row, const VTermScreenCell *cell) {
    int x = col * CELL_WIDTH;
    int y = row * CELL_HEIGHT;

    // Clear the cell background first
    draw_rect(x, y, CELL_WIDTH, CELL_HEIGHT, COLOR_WHITE);

    // If there's no character, just leave it blank
    if (cell->chars[0] == 0) {
        return;
    }

    // Render the Unicode character using system fonts
    draw_unicode_char(x, y, cell->chars[0], COLOR_BLACK);
}
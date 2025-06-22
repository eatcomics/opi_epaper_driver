#include "vterm.h"
#include <vterm.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

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

int vterm_init(int rows, int cols, int pty, uint8_t *buffer) {
    term_rows = rows;
    term_cols = cols;
    pty_fd = pty;
    vterm_buffer = buffer;

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
}

void vterm_feed_output(const char *data, size_t len, uint8_t *buffer) {
    vterm_buffer = buffer;
    vterm_input_write(vterm, data, len);
}

void vterm_process_input(uint32_t keycode, int modifiers) {
    char buf[16];
    int len;

    if (keycode < 128 && isprint(keycode)) {
        len = vterm_keyboard_unichar(vterm, keycode, modifiers);
    } else {
        VTermKey key = convert_keycode_to_vtermkey(keycode); 
        len = vterm_keyboard_key(vterm, key, modifiers);
    }

    if (len > 0) {
        write(pty_fd, buf, len);
    }
}

void vterm_redraw(uint8_t *buffer) {
    vterm_buffer = buffer;
    VTermScreenCell cell;
    for (int row = 0; row < term_rows; row++) {
        for (int col = 0; col < term_cols; col++) {
            if (vterm_screen_get_cell(screen, row, col, &cell)) {
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

// --- Internal Functions ---

static int damage_callback(VTermRect rect, void *user) {
    VTermScreen *screen = (VTermScreen *)user;
    VTermScreenCell cell;

    for (int row = rect.start_row; row < rect.end_row; row++) {
        for (int col = rect.start_col; col < rect.end_col; col++) {
            if (vterm_screen_get_cell(screen, row, col, &cell)) {
                render_cell(col, row, &cell);
            }
        }
    }

    flush_display();
    return 1;
}

static void render_cell(int col, int row, const VTermScreenCell *cell) {
    int x = col * CELL_WIDTH;
    int y = row * CELL_HEIGHT;

    draw_rect(x, y, CELL_WIDTH, CELL_HEIGHT, COLOR_WHITE);

    if (cell->chars[0] == 0)
        return;

    char ch[5] = {0};
    vterm_unicode_to_utf8(cell->chars[0], ch);
    draw_char(x, y, ch[0], COLOR_BLACK);
}


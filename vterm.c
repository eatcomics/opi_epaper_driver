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

// Track initialization state
static int vterm_initialized = 0;
static int damage_pending = 0;

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

int vterm_init(int rows, int cols, int pty, uint8_t *buffer) {
    if (!buffer) {
        printf("Error: vterm_init called with NULL buffer\n");
        return -1;
    }

    if (rows <= 0 || cols <= 0) {
        printf("Error: invalid terminal dimensions %dx%d\n", cols, rows);
        return -1;
    }

    // Clean up any existing vterm instance
    if (vterm_initialized) {
        printf("Warning: vterm already initialized, cleaning up first\n");
        vterm_destroy();
    }

    term_rows = rows;
    term_cols = cols;
    pty_fd = pty;
    vterm_buffer = buffer;
    buffer_size = (EPD_7IN5_V2_WIDTH * EPD_7IN5_V2_HEIGHT) / 8;

    printf("Initializing vterm: %dx%d\n", cols, rows);
    printf("Buffer size: %zu bytes\n", buffer_size);

    // Clear the buffer initially
    memset(buffer, 0xFF, buffer_size);

    // Initialize libvterm with error checking
    vterm = vterm_new(rows, cols);
    if (!vterm) {
        printf("Error: Failed to create vterm instance\n");
        return -1;
    }

    printf("Created vterm instance\n");

    // Configure vterm
    vterm_set_utf8(vterm, 1);
    
    // Get the screen
    screen = vterm_obtain_screen(vterm);
    if (!screen) {
        printf("Error: Failed to obtain vterm screen\n");
        vterm_free(vterm);
        vterm = NULL;
        return -1;
    }

    printf("Obtained vterm screen\n");

    // Set up callbacks - be very careful here
    VTermScreenCallbacks callbacks = {0}; // Initialize all to NULL/0
    callbacks.damage = damage_callback;
    
    vterm_screen_set_callbacks(screen, &callbacks, screen);
    
    // Reset the screen
    vterm_screen_reset(screen, 1);
    
    // Force a flush to make sure everything is set up
    vterm_screen_flush_damage(screen);

    vterm_initialized = 1;
    damage_pending = 0;
    printf("vterm initialization complete\n");
    return 0;
}

void vterm_destroy(void) {
    if (vterm) {
        printf("Destroying vterm instance\n");
        vterm_free(vterm);
        vterm = NULL;
    }
    screen = NULL;
    vterm_buffer = NULL;
    buffer_size = 0;
    vterm_initialized = 0;
    damage_pending = 0;
}

void vterm_feed_output(const char *data, size_t len, uint8_t *buffer) {
    if (!vterm_initialized || !vterm || !screen || !data || len == 0) {
        printf("vterm_feed_output: invalid state or parameters\n");
        return;
    }
    
    if (!buffer) {
        printf("vterm_feed_output: NULL buffer\n");
        return;
    }
    
    // Limit the amount of data we process at once
    if (len > 256) {
        printf("vterm_feed_output: truncating large input from %zu to 256 bytes\n", len);
        len = 256;
    }
    
    printf("Feeding %zu bytes to vterm: ", len);
    for (size_t i = 0; i < len && i < 20; i++) {
        if (isprint(data[i])) {
            printf("%c", data[i]);
        } else {
            printf("\\x%02x", (unsigned char)data[i]);
        }
    }
    if (len > 20) printf("...");
    printf("\n");
    
    vterm_buffer = buffer;
    
    // Process data in small chunks to be safe
    const size_t chunk_size = 16;
    for (size_t offset = 0; offset < len; offset += chunk_size) {
        size_t this_chunk = (offset + chunk_size > len) ? (len - offset) : chunk_size;
        
        printf("Processing chunk at offset %zu, size %zu\n", offset, this_chunk);
        
        // Feed the chunk
        vterm_input_write(vterm, &data[offset], this_chunk);
        
        // Flush damage after each chunk
        vterm_screen_flush_damage(screen);
        
        // Small delay to prevent overwhelming the system
        usleep(1000); // 1ms
    }
    
    printf("Finished feeding data to vterm\n");
}

void vterm_process_input(uint32_t keycode, int modifiers) {
    if (pty_fd < 0) {
        return;
    }

    printf("Processing key: code=%u, mods=%d\n", keycode, modifiers);

    if (keycode >= 32 && keycode < 127) {
        // Printable ASCII character
        char ch = (char)keycode;
        ssize_t written = write(pty_fd, &ch, 1);
        printf("Wrote character '%c' to PTY: %zd bytes\n", ch, written);
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
                    printf("Unhandled special key: %d\n", key);
                    break;
            }
            
            if (len > 0) {
                ssize_t written = write(pty_fd, seq, len);
                printf("Wrote escape sequence to PTY: %zd bytes\n", written);
            }
        } else {
            printf("Unknown keycode: %u\n", keycode);
        }
    }
}

void vterm_redraw(uint8_t *buffer) {
    if (!vterm_initialized || !vterm || !screen || !buffer) {
        printf("vterm_redraw: invalid state\n");
        return;
    }
    
    printf("Redrawing terminal %dx%d\n", term_cols, term_rows);
    
    vterm_buffer = buffer;
    
    // Clear the buffer first
    memset(buffer, 0xFF, buffer_size);
    
    VTermScreenCell cell;
    int cells_rendered = 0;
    
    for (int row = 0; row < term_rows; row++) {
        for (int col = 0; col < term_cols; col++) {
            VTermPos pos = {.row = row, .col = col};
            if (vterm_screen_get_cell(screen, pos, &cell)) {
                render_cell(col, row, &cell);
                if (cell.chars[0] != 0) {
                    cells_rendered++;
                }
            }
        }
    }
    
    printf("Rendered %d non-empty cells\n", cells_rendered);
    flush_display();
    damage_pending = 0;
}

void flush_display(void) {
    if (vterm_buffer) {
        printf("Flushing display to E-ink\n");
        EPD_7IN5_V2_Display(vterm_buffer);
    }
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
    
    // Bounds check for buffer access
    if (byte_index < 0 || byte_index >= buffer_size) {
        return;
    }
    
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

// Fallback character rendering using built-in font
void draw_char_fallback(int x, int y, char ch, int color) {
    extern const uint8_t font8x16[96][16];
    
    if (ch < 0x20 || ch > 0x7F) {
        ch = '?'; // Replace non-printable with question mark
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
    if (!vterm_initialized || !screen || !vterm_buffer) {
        printf("damage_callback: invalid state\n");
        return 0;
    }

    printf("Damage callback: rows %d-%d, cols %d-%d\n", 
           rect.start_row, rect.end_row, rect.start_col, rect.end_col);

    // Validate rectangle bounds
    if (rect.start_row < 0 || rect.end_row > term_rows ||
        rect.start_col < 0 || rect.end_col > term_cols) {
        printf("damage_callback: invalid rectangle bounds\n");
        return 0;
    }

    // Mark that we have pending damage
    damage_pending = 1;
    
    // Don't render immediately in the damage callback - just mark as damaged
    // The main loop will handle the actual rendering
    printf("Damage callback completed, marked pending\n");
    return 1;
}

static void render_cell(int col, int row, const VTermScreenCell *cell) {
    if (!cell || !vterm_buffer) {
        return;
    }

    // Validate cell position
    if (col < 0 || col >= term_cols || row < 0 || row >= term_rows) {
        return;
    }

    int x = col * CELL_WIDTH;
    int y = row * CELL_HEIGHT;

    // Bounds check for screen coordinates
    if (x < 0 || x >= EPD_7IN5_V2_WIDTH || y < 0 || y >= EPD_7IN5_V2_HEIGHT) {
        return;
    }

    // Clear the cell background first
    draw_rect(x, y, CELL_WIDTH, CELL_HEIGHT, COLOR_WHITE);

    // If there's no character, just leave it blank
    if (cell->chars[0] == 0) {
        return;
    }

    // For now, just render ASCII characters using the fallback font
    if (cell->chars[0] < 128) {
        draw_char_fallback(x, y, (char)cell->chars[0], COLOR_BLACK);
    } else {
        // For non-ASCII, draw a placeholder box
        draw_rect(x + 1, y + 1, CELL_WIDTH - 2, CELL_HEIGHT - 2, COLOR_BLACK);
    }
}
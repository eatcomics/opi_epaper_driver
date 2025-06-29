#include "vterm.h"
#include "EPD_7in5_V2.h"
#include "font8x16.h"
#include "keymap.h"
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

// Terminal state
static int term_rows, term_cols;
static int pty_fd = -1;

// Simple terminal emulator state
static char **screen_buffer = NULL;  // 2D array of characters
static int cursor_row = 0;
static int cursor_col = 0;
static int damage_pending = 0;

// ANSI escape sequence parser state
typedef enum {
    PARSE_NORMAL,
    PARSE_ESCAPE,
    PARSE_CSI,
    PARSE_CSI_PARAM
} parse_state_t;

static parse_state_t parse_state = PARSE_NORMAL;
static char escape_buffer[64];
static int escape_pos = 0;

// Forward declarations
static void clear_screen_buffer(void);
static void scroll_up(void);
static void process_escape_sequence(void);
static void move_cursor(int row, int col);
static void put_char_at(int row, int col, char ch);
static void render_screen_buffer(void);

int vterm_init(int rows, int cols, int pty, uint8_t *buffer) {
    printf("vterm_init: Starting fallback terminal emulator...\n");
    
    if (!buffer) {
        printf("Error: vterm_init called with NULL buffer\n");
        return -1;
    }

    if (rows <= 0 || cols <= 0) {
        printf("Error: invalid terminal dimensions %dx%d\n", cols, rows);
        return -1;
    }

    // Clean up any existing state
    if (screen_buffer) {
        vterm_destroy();
    }

    term_rows = rows;
    term_cols = cols;
    pty_fd = pty;
    vterm_buffer = buffer;
    buffer_size = (EPD_7IN5_V2_WIDTH * EPD_7IN5_V2_HEIGHT) / 8;

    printf("Initializing fallback terminal: %dx%d\n", cols, rows);
    printf("Buffer size: %zu bytes\n", buffer_size);

    // Allocate screen buffer
    screen_buffer = malloc(term_rows * sizeof(char*));
    if (!screen_buffer) {
        printf("Error: Failed to allocate screen buffer rows\n");
        return -1;
    }

    for (int i = 0; i < term_rows; i++) {
        screen_buffer[i] = malloc(term_cols + 1); // +1 for null terminator
        if (!screen_buffer[i]) {
            printf("Error: Failed to allocate screen buffer row %d\n", i);
            // Clean up already allocated rows
            for (int j = 0; j < i; j++) {
                free(screen_buffer[j]);
            }
            free(screen_buffer);
            screen_buffer = NULL;
            return -1;
        }
        memset(screen_buffer[i], ' ', term_cols);
        screen_buffer[i][term_cols] = '\0';
    }

    // Clear the framebuffer
    memset(buffer, 0xFF, buffer_size);

    // Initialize terminal state
    cursor_row = 0;
    cursor_col = 0;
    parse_state = PARSE_NORMAL;
    escape_pos = 0;
    damage_pending = 0;

    printf("Fallback terminal initialized successfully\n");
    return 0;
}

void vterm_destroy(void) {
    if (screen_buffer) {
        printf("Destroying fallback terminal\n");
        for (int i = 0; i < term_rows; i++) {
            if (screen_buffer[i]) {
                free(screen_buffer[i]);
            }
        }
        free(screen_buffer);
        screen_buffer = NULL;
    }
    vterm_buffer = NULL;
    buffer_size = 0;
    damage_pending = 0;
}

void vterm_feed_output(const char *data, size_t len, uint8_t *buffer) {
    if (!data || len == 0 || !buffer || !screen_buffer) {
        return;
    }
    
    vterm_buffer = buffer;
    
    printf("Processing %zu bytes of terminal output\n", len);
    
    for (size_t i = 0; i < len; i++) {
        char ch = data[i];
        
        switch (parse_state) {
            case PARSE_NORMAL:
                if (ch == '\x1b') {  // ESC
                    parse_state = PARSE_ESCAPE;
                    escape_pos = 0;
                } else if (ch == '\r') {
                    cursor_col = 0;
                } else if (ch == '\n') {
                    cursor_row++;
                    if (cursor_row >= term_rows) {
                        scroll_up();
                        cursor_row = term_rows - 1;
                    }
                } else if (ch == '\b') {
                    if (cursor_col > 0) {
                        cursor_col--;
                    }
                } else if (ch == '\t') {
                    // Simple tab handling - move to next 8-column boundary
                    cursor_col = ((cursor_col + 8) / 8) * 8;
                    if (cursor_col >= term_cols) {
                        cursor_col = 0;
                        cursor_row++;
                        if (cursor_row >= term_rows) {
                            scroll_up();
                            cursor_row = term_rows - 1;
                        }
                    }
                } else if (isprint(ch) || ch == ' ') {
                    // Handle line wrapping
                    if (cursor_col >= term_cols) {
                        cursor_col = 0;
                        cursor_row++;
                        if (cursor_row >= term_rows) {
                            scroll_up();
                            cursor_row = term_rows - 1;
                        }
                    }
                    
                    put_char_at(cursor_row, cursor_col, ch);
                    cursor_col++;
                }
                break;
                
            case PARSE_ESCAPE:
                if (ch == '[') {
                    parse_state = PARSE_CSI;
                    escape_pos = 0;
                } else {
                    // Other escape sequences - ignore for now
                    parse_state = PARSE_NORMAL;
                }
                break;
                
            case PARSE_CSI:
                if (escape_pos < sizeof(escape_buffer) - 1) {
                    escape_buffer[escape_pos++] = ch;
                }
                
                if (isalpha(ch)) {
                    // End of CSI sequence
                    escape_buffer[escape_pos] = '\0';
                    process_escape_sequence();
                    parse_state = PARSE_NORMAL;
                    escape_pos = 0;
                } else if (escape_pos >= sizeof(escape_buffer) - 1) {
                    // Buffer overflow - abort sequence
                    parse_state = PARSE_NORMAL;
                    escape_pos = 0;
                }
                break;
                
            default:
                parse_state = PARSE_NORMAL;
                break;
        }
    }
    
    damage_pending = 1;
}

void vterm_process_input(uint32_t keycode, int modifiers) {
    if (pty_fd < 0) {
        return;
    }

    printf("Processing key: code=%u, mods=%d\n", keycode, modifiers);

    if (keycode >= 32 && keycode < 127) {
        // Printable ASCII character
        char ch = (char)keycode;
        
        // Handle Ctrl combinations
        if (modifiers & 0x04) { // Ctrl modifier
            if (keycode >= 'a' && keycode <= 'z') {
                ch = keycode - 'a' + 1; // Ctrl+A = 0x01, etc.
            } else if (keycode >= 'A' && keycode <= 'Z') {
                ch = keycode - 'A' + 1;
            }
        }
        
        ssize_t written = write(pty_fd, &ch, 1);
        printf("Wrote character '%c' (0x%02x) to PTY: %zd bytes\n", 
               isprint(ch) ? ch : '?', (unsigned char)ch, written);
    } else {
        // Special keys
        VTermKey key = convert_keycode_to_vtermkey(keycode);
        if (key != VTERM_KEY_NONE) {
            char seq[16];
            int len = 0;
            
            switch (key) {
                case VTERM_KEY_ENTER:
                    seq[0] = '\r';
                    len = 1;
                    break;
                case VTERM_KEY_BACKSPACE:
                    seq[0] = '\x7f'; // DEL character
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
                case VTERM_KEY_HOME:
                    strcpy(seq, "\x1b[H");
                    len = 3;
                    break;
                case VTERM_KEY_END:
                    strcpy(seq, "\x1b[F");
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
    if (!buffer || !screen_buffer) {
        return;
    }
    
    vterm_buffer = buffer;
    render_screen_buffer();
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
        ch = '?';
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

static void clear_screen_buffer(void) {
    if (!screen_buffer) return;
    
    for (int i = 0; i < term_rows; i++) {
        memset(screen_buffer[i], ' ', term_cols);
        screen_buffer[i][term_cols] = '\0';
    }
    cursor_row = 0;
    cursor_col = 0;
}

static void scroll_up(void) {
    if (!screen_buffer) return;
    
    // Move all lines up by one
    char *first_line = screen_buffer[0];
    for (int i = 0; i < term_rows - 1; i++) {
        screen_buffer[i] = screen_buffer[i + 1];
    }
    
    // Clear the last line
    screen_buffer[term_rows - 1] = first_line;
    memset(screen_buffer[term_rows - 1], ' ', term_cols);
    screen_buffer[term_rows - 1][term_cols] = '\0';
}

static void process_escape_sequence(void) {
    if (escape_pos == 0) return;
    
    char cmd = escape_buffer[escape_pos - 1];
    
    printf("Processing ANSI sequence: [%s\n", escape_buffer);
    
    switch (cmd) {
        case 'H': // Cursor position
        case 'f': {
            int row = 1, col = 1;
            if (escape_pos > 1) {
                sscanf(escape_buffer, "%d;%d", &row, &col);
            }
            move_cursor(row - 1, col - 1); // Convert to 0-based
            break;
        }
        case 'A': // Cursor up
            if (cursor_row > 0) cursor_row--;
            break;
        case 'B': // Cursor down
            if (cursor_row < term_rows - 1) cursor_row++;
            break;
        case 'C': // Cursor right
            if (cursor_col < term_cols - 1) cursor_col++;
            break;
        case 'D': // Cursor left
            if (cursor_col > 0) cursor_col--;
            break;
        case 'J': // Clear screen
            if (escape_buffer[0] == '2') {
                clear_screen_buffer();
            }
            break;
        case 'K': // Clear line
            if (screen_buffer && cursor_row >= 0 && cursor_row < term_rows) {
                memset(&screen_buffer[cursor_row][cursor_col], ' ', 
                       term_cols - cursor_col);
            }
            break;
        case 'm': // Set graphics mode (colors, etc.) - ignore for now
            break;
        default:
            printf("Unhandled ANSI sequence: %c\n", cmd);
            break;
    }
}

static void move_cursor(int row, int col) {
    cursor_row = (row < 0) ? 0 : (row >= term_rows) ? term_rows - 1 : row;
    cursor_col = (col < 0) ? 0 : (col >= term_cols) ? term_cols - 1 : col;
}

static void put_char_at(int row, int col, char ch) {
    if (!screen_buffer || row < 0 || row >= term_rows || col < 0 || col >= term_cols) {
        return;
    }
    
    screen_buffer[row][col] = ch;
}

static void render_screen_buffer(void) {
    if (!screen_buffer || !vterm_buffer) {
        return;
    }
    
    // Clear the framebuffer
    memset(vterm_buffer, 0xFF, buffer_size);
    
    // Render each character
    for (int row = 0; row < term_rows; row++) {
        for (int col = 0; col < term_cols; col++) {
            char ch = screen_buffer[row][col];
            if (ch != ' ') {
                int x = col * CELL_WIDTH;
                int y = row * CELL_HEIGHT;
                draw_char_fallback(x, y, ch, COLOR_BLACK);
            }
        }
    }
    
    // Draw cursor (simple block cursor)
    if (cursor_row >= 0 && cursor_row < term_rows && 
        cursor_col >= 0 && cursor_col < term_cols) {
        int x = cursor_col * CELL_WIDTH;
        int y = cursor_row * CELL_HEIGHT;
        
        // Draw cursor as inverted block
        for (int dy = 0; dy < CELL_HEIGHT; dy++) {
            for (int dx = 0; dx < CELL_WIDTH; dx++) {
                // Get current pixel
                int px = x + dx;
                int py = y + dy;
                if (px >= 0 && px < EPD_7IN5_V2_WIDTH && py >= 0 && py < EPD_7IN5_V2_HEIGHT) {
                    int byte_index = (py * EPD_7IN5_V2_WIDTH + px) / 8;
                    int bit_index = 7 - (px % 8);
                    if (byte_index >= 0 && byte_index < buffer_size) {
                        // Invert the pixel
                        vterm_buffer[byte_index] ^= (1 << bit_index);
                    }
                }
            }
        }
    }
}
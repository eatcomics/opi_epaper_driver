#include "tsm_term.h"
#include "EPD_7in5_V2.h"
#include "font8x16.h"
#include "keymap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <linux/input-event-codes.h>

#define CELL_WIDTH 8
#define CELL_HEIGHT 16
#define COLOR_WHITE 0
#define COLOR_BLACK 1

// Output buffering - much larger buffer
#define OUTPUT_BUFFER_SIZE 8192
static char output_buffer[OUTPUT_BUFFER_SIZE];
static size_t output_buffer_pos = 0;
static int output_buffer_dirty = 0;

// Terminal state
static int term_rows = 24;
static int term_cols = 80;
static int cursor_row = 0;
static int cursor_col = 0;
static uint8_t *framebuffer = NULL;
static size_t buffer_size = 0;
static int pty_fd = -1;
static int damage_pending = 0;

// Screen buffer - stores characters and attributes
static struct {
    char ch;
    uint8_t fg_color;
    uint8_t bg_color;
    uint8_t attrs; // bold, underline, etc.
} screen_buffer[30][100]; // Max size

// ANSI escape sequence parser state
static enum {
    STATE_NORMAL,
    STATE_ESCAPE,
    STATE_CSI,
    STATE_OSC
} parser_state = STATE_NORMAL;

static char escape_buffer[256];
static int escape_pos = 0;

// Forward declarations
static void set_pixel(int x, int y, int color);
static void draw_char(int x, int y, char ch, int fg_color, int bg_color, uint8_t attrs);
static void scroll_up(void);
static void clear_screen(void);
static void move_cursor(int row, int col);
static void process_csi_sequence(const char *seq, int len);
static void render_screen(void);
static char keycode_to_ascii(uint32_t keycode, int shift_pressed);
static void flush_output_buffer(void);
static void process_buffered_output(void);

int tsm_term_init(int rows, int cols, int pty, uint8_t *buffer) {
    printf("tsm_term_init: %dx%d\n", cols, rows);
    
    if (rows <= 0 || cols <= 0 || !buffer) {
        return -1;
    }
    
    // Limit to safe sizes
    term_rows = (rows > 24) ? 24 : rows;
    term_cols = (cols > 80) ? 80 : cols;
    
    framebuffer = buffer;
    buffer_size = (EPD_7IN5_V2_WIDTH * EPD_7IN5_V2_HEIGHT) / 8;
    pty_fd = pty;
    
    cursor_row = 0;
    cursor_col = 0;
    parser_state = STATE_NORMAL;
    escape_pos = 0;
    
    // Initialize output buffer
    output_buffer_pos = 0;
    output_buffer_dirty = 0;
    
    // Clear screen buffer
    for (int r = 0; r < term_rows; r++) {
        for (int c = 0; c < term_cols; c++) {
            screen_buffer[r][c].ch = ' ';
            screen_buffer[r][c].fg_color = COLOR_BLACK;
            screen_buffer[r][c].bg_color = COLOR_WHITE;
            screen_buffer[r][c].attrs = 0;
        }
    }
    
    // Clear framebuffer to white
    memset(framebuffer, 0xFF, buffer_size);
    
    damage_pending = 1;
    
    printf("TSM terminal initialized: %dx%d\n", term_cols, term_rows);
    return 0;
}

void tsm_term_destroy(void) {
    // Flush any remaining output
    flush_output_buffer();
    
    framebuffer = NULL;
    buffer_size = 0;
    pty_fd = -1;
    output_buffer_pos = 0;
    output_buffer_dirty = 0;
}

void tsm_term_feed_output(const char *data, size_t len, uint8_t *buffer) {
    if (!data || len == 0 || !buffer) {
        return;
    }
    
    framebuffer = buffer;
    
    printf("tsm_term_feed_output: %zu bytes: ", len);
    // Print first 40 chars for debugging
    for (size_t i = 0; i < len && i < 40; i++) {
        if (data[i] >= 32 && data[i] <= 126) {
            printf("%c", data[i]);
        } else {
            printf("\\x%02x", (unsigned char)data[i]);
        }
    }
    printf("\n");
    
    // Add to output buffer instead of processing immediately
    for (size_t i = 0; i < len; i++) {
        if (output_buffer_pos < OUTPUT_BUFFER_SIZE - 1) {
            output_buffer[output_buffer_pos++] = data[i];
            output_buffer_dirty = 1;
        } else {
            // Buffer full, process what we have
            printf("Output buffer full, processing...\n");
            process_buffered_output();
            output_buffer[0] = data[i];
            output_buffer_pos = 1;
            output_buffer_dirty = 1;
        }
    }
    
    printf("Output buffered: %zu chars total\n", output_buffer_pos);
}

static void process_buffered_output(void) {
    if (!output_buffer_dirty || output_buffer_pos == 0) {
        return;
    }
    
    printf("Processing %zu buffered characters\n", output_buffer_pos);
    
    for (size_t i = 0; i < output_buffer_pos; i++) {
        char ch = output_buffer[i];
        
        switch (parser_state) {
            case STATE_NORMAL:
                switch (ch) {
                    case '\r':  // Carriage return
                        cursor_col = 0;
                        break;
                        
                    case '\n':  // Line feed
                        cursor_row++;
                        if (cursor_row >= term_rows) {
                            scroll_up();
                            cursor_row = term_rows - 1;
                        }
                        break;
                        
                    case '\t':  // Tab
                        cursor_col = ((cursor_col + 8) / 8) * 8;
                        if (cursor_col >= term_cols) {
                            cursor_col = 0;
                            cursor_row++;
                            if (cursor_row >= term_rows) {
                                scroll_up();
                                cursor_row = term_rows - 1;
                            }
                        }
                        break;
                        
                    case '\b':  // Backspace
                        if (cursor_col > 0) {
                            cursor_col--;
                            screen_buffer[cursor_row][cursor_col].ch = ' ';
                            damage_pending = 1;
                        }
                        break;
                        
                    case 0x1B:  // Escape
                        parser_state = STATE_ESCAPE;
                        escape_pos = 0;
                        break;
                        
                    case 0x07:  // Bell - ignore
                        break;
                        
                    default:
                        if (ch >= 0x20 && ch <= 0x7E) {  // Printable ASCII
                            if (cursor_row < term_rows && cursor_col < term_cols) {
                                screen_buffer[cursor_row][cursor_col].ch = ch;
                                screen_buffer[cursor_row][cursor_col].fg_color = COLOR_BLACK;
                                screen_buffer[cursor_row][cursor_col].bg_color = COLOR_WHITE;
                                screen_buffer[cursor_row][cursor_col].attrs = 0;
                                damage_pending = 1;
                                cursor_col++;
                                
                                if (cursor_col >= term_cols) {
                                    cursor_col = 0;
                                    cursor_row++;
                                    if (cursor_row >= term_rows) {
                                        scroll_up();
                                        cursor_row = term_rows - 1;
                                    }
                                }
                            }
                        }
                        break;
                }
                break;
                
            case STATE_ESCAPE:
                if (ch == '[') {
                    parser_state = STATE_CSI;
                    escape_pos = 0;
                } else if (ch == ']') {
                    parser_state = STATE_OSC;
                    escape_pos = 0;
                } else {
                    // Single character escape sequence, ignore for now
                    parser_state = STATE_NORMAL;
                }
                break;
                
            case STATE_CSI:
                if (escape_pos < sizeof(escape_buffer) - 1) {
                    escape_buffer[escape_pos++] = ch;
                }
                
                // CSI sequence ends with a letter
                if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')) {
                    escape_buffer[escape_pos] = '\0';
                    process_csi_sequence(escape_buffer, escape_pos);
                    parser_state = STATE_NORMAL;
                }
                break;
                
            case STATE_OSC:
                // OSC sequences end with BEL (0x07) or ESC backslash
                if (ch == 0x07) {
                    parser_state = STATE_NORMAL;
                } else if (ch == 0x1B) {
                    // Might be ESC backslash sequence, but for simplicity just reset
                    parser_state = STATE_NORMAL;
                }
                break;
        }
    }
    
    // Clear the buffer
    output_buffer_pos = 0;
    output_buffer_dirty = 0;
    
    printf("Cursor position: %d,%d\n", cursor_row, cursor_col);
}

static void flush_output_buffer(void) {
    if (output_buffer_dirty) {
        process_buffered_output();
    }
}

void tsm_term_process_input(uint32_t keycode, int modifiers) {
    if (pty_fd < 0) {
        printf("ERROR: PTY not available (fd=%d)\n", pty_fd);
        return;
    }

    printf("Processing key: %u, modifiers: %d\n", keycode, modifiers);

    // Check for modifier keys
    int ctrl_pressed = (modifiers & 0x04) != 0;
    int shift_pressed = (modifiers & 0x01) != 0;

    // Handle special keys first
    switch (keycode) {
        case KEY_ENTER:
            printf("Sending ENTER (\\r) to PTY\n");
            if (write(pty_fd, "\r", 1) < 0) {
                perror("write ENTER failed");
            }
            return;
        case KEY_BACKSPACE:
            printf("Sending BACKSPACE (\\b) to PTY\n");
            if (write(pty_fd, "\b", 1) < 0) {
                perror("write BACKSPACE failed");
            }
            return;
        case KEY_TAB:
            printf("Sending TAB (\\t) to PTY\n");
            if (write(pty_fd, "\t", 1) < 0) {
                perror("write TAB failed");
            }
            return;
        case KEY_ESC:
            printf("Sending ESC to PTY\n");
            if (write(pty_fd, "\x1b", 1) < 0) {
                perror("write ESC failed");
            }
            return;
        case KEY_UP:
            printf("Sending UP arrow to PTY\n");
            if (write(pty_fd, "\x1b[A", 3) < 0) {
                perror("write UP failed");
            }
            return;
        case KEY_DOWN:
            printf("Sending DOWN arrow to PTY\n");
            if (write(pty_fd, "\x1b[B", 3) < 0) {
                perror("write DOWN failed");
            }
            return;
        case KEY_RIGHT:
            printf("Sending RIGHT arrow to PTY\n");
            if (write(pty_fd, "\x1b[C", 3) < 0) {
                perror("write RIGHT failed");
            }
            return;
        case KEY_LEFT:
            printf("Sending LEFT arrow to PTY\n");
            if (write(pty_fd, "\x1b[D", 3) < 0) {
                perror("write LEFT failed");
            }
            return;
        case KEY_HOME:
            if (write(pty_fd, "\x1b[H", 3) < 0) {
                perror("write HOME failed");
            }
            return;
        case KEY_END:
            if (write(pty_fd, "\x1b[F", 3) < 0) {
                perror("write END failed");
            }
            return;
        case KEY_PAGEUP:
            if (write(pty_fd, "\x1b[5~", 4) < 0) {
                perror("write PAGEUP failed");
            }
            return;
        case KEY_PAGEDOWN:
            if (write(pty_fd, "\x1b[6~", 4) < 0) {
                perror("write PAGEDOWN failed");
            }
            return;
        case KEY_DELETE:
            if (write(pty_fd, "\x1b[3~", 4) < 0) {
                perror("write DELETE failed");
            }
            return;
    }

    // Handle printable characters
    char ascii_char = keycode_to_ascii(keycode, shift_pressed);
    if (ascii_char != 0) {
        if (ctrl_pressed && ascii_char >= 'a' && ascii_char <= 'z') {
            // Convert to control character
            char ctrl_char = ascii_char - 'a' + 1;
            printf("Sending Ctrl+%c (0x%02x) to PTY\n", ascii_char, (unsigned char)ctrl_char);
            if (write(pty_fd, &ctrl_char, 1) < 0) {
                perror("write ctrl char failed");
            }
        } else if (ctrl_pressed && ascii_char >= 'A' && ascii_char <= 'Z') {
            // Convert to control character
            char ctrl_char = ascii_char - 'A' + 1;
            printf("Sending Ctrl+%c (0x%02x) to PTY\n", ascii_char, (unsigned char)ctrl_char);
            if (write(pty_fd, &ctrl_char, 1) < 0) {
                perror("write ctrl char failed");
            }
        } else {
            printf("Sending ASCII '%c' (0x%02x) to PTY\n", ascii_char, (unsigned char)ascii_char);
            if (write(pty_fd, &ascii_char, 1) < 0) {
                perror("write ascii char failed");
            }
        }
    } else {
        printf("Unhandled keycode: %u\n", keycode);
    }
}

void tsm_term_redraw(uint8_t *buffer) {
    if (!buffer) {
        return;
    }
    
    framebuffer = buffer;
    
    // Process any buffered output first
    flush_output_buffer();
    
    if (!damage_pending) {
        printf("No damage pending, skipping redraw\n");
        return;
    }
    
    printf("Redrawing terminal screen\n");
    render_screen();
    tsm_flush_display();
    damage_pending = 0;
}

int tsm_term_has_pending_damage(void) {
    return damage_pending || output_buffer_dirty;
}

void tsm_flush_display(void) {
    if (framebuffer) {
        printf("Flushing display to E-ink\n");
        EPD_7IN5_V2_Display(framebuffer);
    }
}

// --- Internal Functions ---

static void set_pixel(int x, int y, int color) {
    if (!framebuffer || x < 0 || x >= EPD_7IN5_V2_WIDTH || y < 0 || y >= EPD_7IN5_V2_HEIGHT) {
        return;
    }
    
    int byte_index = (y * EPD_7IN5_V2_WIDTH + x) / 8;
    int bit_index = 7 - (x % 8);
    
    if (byte_index < 0 || byte_index >= (int)buffer_size) {
        return;
    }
    
    if (color == COLOR_BLACK) {
        framebuffer[byte_index] &= ~(1 << bit_index);
    } else {
        framebuffer[byte_index] |= (1 << bit_index);
    }
}

static void draw_char(int x, int y, char ch, int fg_color, int bg_color, uint8_t attrs) {
    extern const uint8_t font8x16[96][16];
    
    if (ch < 0x20 || ch > 0x7F) {
        ch = '?';  // Replace unprintable characters with '?'
    }
    
    const uint8_t *glyph = font8x16[ch - 0x20];

    // Draw background
    if (bg_color == COLOR_BLACK) {
        for (int row = 0; row < CELL_HEIGHT; row++) {
            for (int col = 0; col < CELL_WIDTH; col++) {
                set_pixel(x + col, y + row, COLOR_BLACK);
            }
        }
    }

    // Draw character
    for (int row = 0; row < CELL_HEIGHT; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < CELL_WIDTH; col++) {
            if (bits & (1 << (7 - col))) {
                set_pixel(x + col, y + row, fg_color);
            }
        }
    }
    
    // Draw underline if needed
    if (attrs & 1) { // underline
        for (int col = 0; col < CELL_WIDTH; col++) {
            set_pixel(x + col, y + CELL_HEIGHT - 2, fg_color);
        }
    }
}

static void scroll_up(void) {
    // Move all lines up by one
    for (int r = 0; r < term_rows - 1; r++) {
        for (int c = 0; c < term_cols; c++) {
            screen_buffer[r][c] = screen_buffer[r + 1][c];
        }
    }
    
    // Clear the last line
    for (int c = 0; c < term_cols; c++) {
        screen_buffer[term_rows - 1][c].ch = ' ';
        screen_buffer[term_rows - 1][c].fg_color = COLOR_BLACK;
        screen_buffer[term_rows - 1][c].bg_color = COLOR_WHITE;
        screen_buffer[term_rows - 1][c].attrs = 0;
    }
    
    damage_pending = 1;
}

static void clear_screen(void) {
    for (int r = 0; r < term_rows; r++) {
        for (int c = 0; c < term_cols; c++) {
            screen_buffer[r][c].ch = ' ';
            screen_buffer[r][c].fg_color = COLOR_BLACK;
            screen_buffer[r][c].bg_color = COLOR_WHITE;
            screen_buffer[r][c].attrs = 0;
        }
    }
    cursor_row = 0;
    cursor_col = 0;
    damage_pending = 1;
}

static void move_cursor(int row, int col) {
    if (row >= 0 && row < term_rows) {
        cursor_row = row;
    }
    if (col >= 0 && col < term_cols) {
        cursor_col = col;
    }
}

static void process_csi_sequence(const char *seq, int len) {
    if (len == 0) return;
    
    char cmd = seq[len - 1];
    
    printf("CSI sequence: %.*s%c\n", len - 1, seq, cmd);
    
    switch (cmd) {
        case 'H': // Cursor position
        case 'f': // Horizontal and vertical position
            {
                int row = 1, col = 1;
                sscanf(seq, "%d;%d", &row, &col);
                move_cursor(row - 1, col - 1); // Convert to 0-based
            }
            break;
            
        case 'A': // Cursor up
            {
                int n = 1;
                sscanf(seq, "%d", &n);
                cursor_row = (cursor_row - n < 0) ? 0 : cursor_row - n;
            }
            break;
            
        case 'B': // Cursor down
            {
                int n = 1;
                sscanf(seq, "%d", &n);
                cursor_row = (cursor_row + n >= term_rows) ? term_rows - 1 : cursor_row + n;
            }
            break;
            
        case 'C': // Cursor right
            {
                int n = 1;
                sscanf(seq, "%d", &n);
                cursor_col = (cursor_col + n >= term_cols) ? term_cols - 1 : cursor_col + n;
            }
            break;
            
        case 'D': // Cursor left
            {
                int n = 1;
                sscanf(seq, "%d", &n);
                cursor_col = (cursor_col - n < 0) ? 0 : cursor_col - n;
            }
            break;
            
        case 'J': // Erase display
            {
                int n = 0;
                sscanf(seq, "%d", &n);
                if (n == 2) { // Clear entire screen
                    clear_screen();
                }
            }
            break;
            
        case 'K': // Erase line
            {
                int n = 0;
                sscanf(seq, "%d", &n);
                if (n == 0) { // Clear from cursor to end of line
                    for (int c = cursor_col; c < term_cols; c++) {
                        screen_buffer[cursor_row][c].ch = ' ';
                    }
                    damage_pending = 1;
                }
            }
            break;
            
        case 'm': // Set graphics mode (colors, etc.)
            // For now, ignore color commands
            break;
            
        default:
            printf("Unhandled CSI command: %c\n", cmd);
            break;
    }
}

static void render_screen(void) {
    if (!framebuffer) return;
    
    // Clear framebuffer to white
    memset(framebuffer, 0xFF, buffer_size);
    
    int rendered_chars = 0;
    
    for (int r = 0; r < term_rows; r++) {
        for (int c = 0; c < term_cols; c++) {
            if (screen_buffer[r][c].ch != ' ') {
                int x = c * CELL_WIDTH;
                int y = r * CELL_HEIGHT;
                
                if (x < EPD_7IN5_V2_WIDTH && y < EPD_7IN5_V2_HEIGHT) {
                    draw_char(x, y, screen_buffer[r][c].ch,
                             screen_buffer[r][c].fg_color,
                             screen_buffer[r][c].bg_color,
                             screen_buffer[r][c].attrs);
                    rendered_chars++;
                }
            }
        }
    }
    
    printf("Rendered %d characters\n", rendered_chars);
}

// Complete key mapping table for Linux input event codes to ASCII
static char keycode_to_ascii(uint32_t keycode, int shift_pressed) {
    // Handle letters (KEY_Q=16, KEY_W=17, KEY_E=18, etc.)
    switch (keycode) {
        // QWERTY row 1
        case KEY_Q: return shift_pressed ? 'Q' : 'q';
        case KEY_W: return shift_pressed ? 'W' : 'w';
        case KEY_E: return shift_pressed ? 'E' : 'e';
        case KEY_R: return shift_pressed ? 'R' : 'r';
        case KEY_T: return shift_pressed ? 'T' : 't';
        case KEY_Y: return shift_pressed ? 'Y' : 'y';
        case KEY_U: return shift_pressed ? 'U' : 'u';
        case KEY_I: return shift_pressed ? 'I' : 'i';
        case KEY_O: return shift_pressed ? 'O' : 'o';
        case KEY_P: return shift_pressed ? 'P' : 'p';
        
        // QWERTY row 2
        case KEY_A: return shift_pressed ? 'A' : 'a';
        case KEY_S: return shift_pressed ? 'S' : 's';
        case KEY_D: return shift_pressed ? 'D' : 'd';
        case KEY_F: return shift_pressed ? 'F' : 'f';
        case KEY_G: return shift_pressed ? 'G' : 'g';
        case KEY_H: return shift_pressed ? 'H' : 'h';
        case KEY_J: return shift_pressed ? 'J' : 'j';
        case KEY_K: return shift_pressed ? 'K' : 'k';
        case KEY_L: return shift_pressed ? 'L' : 'l';
        
        // QWERTY row 3
        case KEY_Z: return shift_pressed ? 'Z' : 'z';
        case KEY_X: return shift_pressed ? 'X' : 'x';
        case KEY_C: return shift_pressed ? 'C' : 'c';
        case KEY_V: return shift_pressed ? 'V' : 'v';
        case KEY_B: return shift_pressed ? 'B' : 'b';
        case KEY_N: return shift_pressed ? 'N' : 'n';
        case KEY_M: return shift_pressed ? 'M' : 'm';
    }
    
    // Handle numbers and their shifted symbols
    switch (keycode) {
        case KEY_1: return shift_pressed ? '!' : '1';
        case KEY_2: return shift_pressed ? '@' : '2';
        case KEY_3: return shift_pressed ? '#' : '3';
        case KEY_4: return shift_pressed ? '$' : '4';
        case KEY_5: return shift_pressed ? '%' : '5';
        case KEY_6: return shift_pressed ? '^' : '6';
        case KEY_7: return shift_pressed ? '&' : '7';
        case KEY_8: return shift_pressed ? '*' : '8';
        case KEY_9: return shift_pressed ? '(' : '9';
        case KEY_0: return shift_pressed ? ')' : '0';
    }
    
    // Handle special characters and punctuation
    switch (keycode) {
        case KEY_SPACE: return ' ';
        case KEY_MINUS: return shift_pressed ? '_' : '-';
        case KEY_EQUAL: return shift_pressed ? '+' : '=';
        case KEY_LEFTBRACE: return shift_pressed ? '{' : '[';
        case KEY_RIGHTBRACE: return shift_pressed ? '}' : ']';
        case KEY_BACKSLASH: return shift_pressed ? '|' : '\\';
        case KEY_SEMICOLON: return shift_pressed ? ':' : ';';
        case KEY_APOSTROPHE: return shift_pressed ? '"' : '\'';
        case KEY_GRAVE: return shift_pressed ? '~' : '`';
        case KEY_COMMA: return shift_pressed ? '<' : ',';
        case KEY_DOT: return shift_pressed ? '>' : '.';
        case KEY_SLASH: return shift_pressed ? '?' : '/';
        default: return 0;
    }
}
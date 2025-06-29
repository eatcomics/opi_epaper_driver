#include "vterm.h"
#include "EPD_7in5_V2.h"
#include "font8x16.h"
#include "keymap.h"
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

// Terminal state
static int term_rows, term_cols;
static int pty_fd = -1;

// Simple terminal emulator state
static char **screen_buffer = NULL;  // 2D array of characters
static uint8_t **attr_buffer = NULL; // 2D array of attributes (colors, etc.)
static int cursor_row = 0;
static int cursor_col = 0;
static int damage_pending = 0;
static int cursor_visible = 1;

// Scrollback buffer
#define SCROLLBACK_LINES 100
static char **scrollback_buffer = NULL;
static int scrollback_pos = 0;
static int scrollback_count = 0;

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

// Current text attributes
static uint8_t current_attr = 0;
#define ATTR_BOLD     0x01
#define ATTR_REVERSE  0x02
#define ATTR_UNDERLINE 0x04

// Forward declarations
static void clear_screen_buffer(void);
static void scroll_up(void);
static void process_escape_sequence(void);
static void move_cursor(int row, int col);
static void put_char_at(int row, int col, char ch);
static void render_screen_buffer(void);
static void save_line_to_scrollback(int row);

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
    
    // Handle numbers
    if (keycode >= KEY_1 && keycode <= KEY_9) {
        if (shift_pressed) {
            // Shifted number keys
            const char shifted[] = "!@#$%^&*()";
            return shifted[keycode - KEY_1];
        } else {
            return '1' + (keycode - KEY_1);
        }
    }
    
    if (keycode == KEY_0) {
        return shift_pressed ? ')' : '0';
    }
    
    // Handle special characters
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

int vterm_init(int rows, int cols, int pty, uint8_t *buffer) {
    printf("Initializing enhanced terminal emulator...\n");
    
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

    printf("Terminal dimensions: %dx%d\n", cols, rows);
    printf("Buffer size: %zu bytes\n", buffer_size);

    // Allocate screen buffer
    screen_buffer = malloc(term_rows * sizeof(char*));
    attr_buffer = malloc(term_rows * sizeof(uint8_t*));
    if (!screen_buffer || !attr_buffer) {
        printf("Error: Failed to allocate screen buffers\n");
        return -1;
    }

    for (int i = 0; i < term_rows; i++) {
        screen_buffer[i] = malloc(term_cols + 1);
        attr_buffer[i] = malloc(term_cols);
        if (!screen_buffer[i] || !attr_buffer[i]) {
            printf("Error: Failed to allocate screen buffer row %d\n", i);
            // Clean up
            for (int j = 0; j < i; j++) {
                free(screen_buffer[j]);
                free(attr_buffer[j]);
            }
            free(screen_buffer);
            free(attr_buffer);
            screen_buffer = NULL;
            attr_buffer = NULL;
            return -1;
        }
        memset(screen_buffer[i], ' ', term_cols);
        screen_buffer[i][term_cols] = '\0';
        memset(attr_buffer[i], 0, term_cols);
    }

    // Allocate scrollback buffer
    scrollback_buffer = malloc(SCROLLBACK_LINES * sizeof(char*));
    if (scrollback_buffer) {
        for (int i = 0; i < SCROLLBACK_LINES; i++) {
            scrollback_buffer[i] = malloc(term_cols + 1);
            if (scrollback_buffer[i]) {
                memset(scrollback_buffer[i], ' ', term_cols);
                scrollback_buffer[i][term_cols] = '\0';
            }
        }
        scrollback_pos = 0;
        scrollback_count = 0;
    }

    // Clear the framebuffer
    memset(buffer, 0xFF, buffer_size);

    // Initialize terminal state
    cursor_row = 0;
    cursor_col = 0;
    parse_state = PARSE_NORMAL;
    escape_pos = 0;
    damage_pending = 0;
    current_attr = 0;
    cursor_visible = 1;

    printf("Enhanced terminal initialized successfully\n");
    return 0;
}

void vterm_destroy(void) {
    if (screen_buffer) {
        printf("Destroying terminal\n");
        for (int i = 0; i < term_rows; i++) {
            if (screen_buffer[i]) free(screen_buffer[i]);
            if (attr_buffer[i]) free(attr_buffer[i]);
        }
        free(screen_buffer);
        free(attr_buffer);
        screen_buffer = NULL;
        attr_buffer = NULL;
    }
    
    if (scrollback_buffer) {
        for (int i = 0; i < SCROLLBACK_LINES; i++) {
            if (scrollback_buffer[i]) free(scrollback_buffer[i]);
        }
        free(scrollback_buffer);
        scrollback_buffer = NULL;
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
                    // Tab to next 8-column boundary
                    int next_tab = ((cursor_col + 8) / 8) * 8;
                    if (next_tab >= term_cols) {
                        cursor_col = 0;
                        cursor_row++;
                        if (cursor_row >= term_rows) {
                            scroll_up();
                            cursor_row = term_rows - 1;
                        }
                    } else {
                        cursor_col = next_tab;
                    }
                } else if (ch == '\a') {
                    // Bell - ignore for now
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
                } else if (ch == 'c') {
                    // Reset terminal
                    clear_screen_buffer();
                    current_attr = 0;
                    parse_state = PARSE_NORMAL;
                } else {
                    // Other escape sequences - ignore
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

    printf("Processing key: %u, modifiers: %d\n", keycode, modifiers);

    // Check for Ctrl combinations first
    int ctrl_pressed = (modifiers & 0x04) != 0;
    int shift_pressed = (modifiers & 0x01) != 0;
    
    if (ctrl_pressed) {
        // Handle Ctrl+letter combinations
        if ((keycode >= KEY_A && keycode <= KEY_Z) || 
            (keycode >= KEY_Q && keycode <= KEY_P)) {
            char ctrl_char;
            
            // Map keycode to letter
            switch (keycode) {
                case KEY_Q: ctrl_char = 17; break;  // Ctrl+Q
                case KEY_W: ctrl_char = 23; break;  // Ctrl+W
                case KEY_E: ctrl_char = 5; break;   // Ctrl+E
                case KEY_R: ctrl_char = 18; break;  // Ctrl+R
                case KEY_T: ctrl_char = 20; break;  // Ctrl+T
                case KEY_Y: ctrl_char = 25; break;  // Ctrl+Y
                case KEY_U: ctrl_char = 21; break;  // Ctrl+U
                case KEY_I: ctrl_char = 9; break;   // Ctrl+I
                case KEY_O: ctrl_char = 15; break;  // Ctrl+O
                case KEY_P: ctrl_char = 16; break;  // Ctrl+P
                case KEY_A: ctrl_char = 1; break;   // Ctrl+A
                case KEY_S: ctrl_char = 19; break;  // Ctrl+S
                case KEY_D: ctrl_char = 4; break;   // Ctrl+D
                case KEY_F: ctrl_char = 6; break;   // Ctrl+F
                case KEY_G: ctrl_char = 7; break;   // Ctrl+G
                case KEY_H: ctrl_char = 8; break;   // Ctrl+H
                case KEY_J: ctrl_char = 10; break;  // Ctrl+J
                case KEY_K: ctrl_char = 11; break;  // Ctrl+K
                case KEY_L: ctrl_char = 12; break;  // Ctrl+L
                case KEY_Z: ctrl_char = 26; break;  // Ctrl+Z
                case KEY_X: ctrl_char = 24; break;  // Ctrl+X
                case KEY_C: ctrl_char = 3; break;   // Ctrl+C
                case KEY_V: ctrl_char = 22; break;  // Ctrl+V
                case KEY_B: ctrl_char = 2; break;   // Ctrl+B
                case KEY_N: ctrl_char = 14; break;  // Ctrl+N
                case KEY_M: ctrl_char = 13; break;  // Ctrl+M
                default: return;
            }
            
            printf("Sending Ctrl+%c (0x%02x)\n", 'A' + ctrl_char - 1, ctrl_char);
            write(pty_fd, &ctrl_char, 1);
            return;
        }
        
        // Handle other Ctrl combinations
        switch (keycode) {
            case KEY_SPACE:
                {
                    char null_char = 0;
                    write(pty_fd, &null_char, 1);
                    return;
                }
        }
    }

    // Handle special keys (non-printable)
    switch (keycode) {
        case KEY_ENTER:
            printf("Sending Enter\n");
            write(pty_fd, "\r", 1);
            return;
        case KEY_BACKSPACE:
            printf("Sending Backspace\n");
            write(pty_fd, "\x7f", 1);
            return;
        case KEY_TAB:
            printf("Sending Tab\n");
            write(pty_fd, "\t", 1);
            return;
        case KEY_ESC:
            printf("Sending Escape\n");
            write(pty_fd, "\x1b", 1);
            return;
        case KEY_UP:
            printf("Sending Up Arrow\n");
            write(pty_fd, "\x1b[A", 3);
            return;
        case KEY_DOWN:
            printf("Sending Down Arrow\n");
            write(pty_fd, "\x1b[B", 3);
            return;
        case KEY_RIGHT:
            printf("Sending Right Arrow\n");
            write(pty_fd, "\x1b[C", 3);
            return;
        case KEY_LEFT:
            printf("Sending Left Arrow\n");
            write(pty_fd, "\x1b[D", 3);
            return;
        case KEY_HOME:
            printf("Sending Home\n");
            write(pty_fd, "\x1b[H", 3);
            return;
        case KEY_END:
            printf("Sending End\n");
            write(pty_fd, "\x1b[F", 3);
            return;
        case KEY_PAGEUP:
            printf("Sending Page Up\n");
            write(pty_fd, "\x1b[5~", 4);
            return;
        case KEY_PAGEDOWN:
            printf("Sending Page Down\n");
            write(pty_fd, "\x1b[6~", 4);
            return;
        case KEY_DELETE:
            printf("Sending Delete\n");
            write(pty_fd, "\x1b[3~", 4);
            return;
        case KEY_INSERT:
            printf("Sending Insert\n");
            write(pty_fd, "\x1b[2~", 4);
            return;
    }

    // Handle printable characters
    char ascii_char = keycode_to_ascii(keycode, shift_pressed);
    if (ascii_char != 0) {
        printf("Sending ASCII: '%c' (0x%02x)\n", ascii_char, ascii_char);
        write(pty_fd, &ascii_char, 1);
    } else {
        printf("Unhandled keycode: %u\n", keycode);
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
        memset(attr_buffer[i], 0, term_cols);
    }
    cursor_row = 0;
    cursor_col = 0;
}

static void save_line_to_scrollback(int row) {
    if (!scrollback_buffer || row < 0 || row >= term_rows) return;
    
    // Copy line to scrollback
    if (scrollback_buffer[scrollback_pos]) {
        strcpy(scrollback_buffer[scrollback_pos], screen_buffer[row]);
    }
    
    scrollback_pos = (scrollback_pos + 1) % SCROLLBACK_LINES;
    if (scrollback_count < SCROLLBACK_LINES) {
        scrollback_count++;
    }
}

static void scroll_up(void) {
    if (!screen_buffer) return;
    
    // Save the top line to scrollback
    save_line_to_scrollback(0);
    
    // Move all lines up by one
    char *first_line = screen_buffer[0];
    uint8_t *first_attr = attr_buffer[0];
    
    for (int i = 0; i < term_rows - 1; i++) {
        screen_buffer[i] = screen_buffer[i + 1];
        attr_buffer[i] = attr_buffer[i + 1];
    }
    
    // Clear the last line
    screen_buffer[term_rows - 1] = first_line;
    attr_buffer[term_rows - 1] = first_attr;
    memset(screen_buffer[term_rows - 1], ' ', term_cols);
    screen_buffer[term_rows - 1][term_cols] = '\0';
    memset(attr_buffer[term_rows - 1], 0, term_cols);
}

static void process_escape_sequence(void) {
    if (escape_pos == 0) return;
    
    char cmd = escape_buffer[escape_pos - 1];
    
    switch (cmd) {
        case 'H': // Cursor position
        case 'f': {
            int row = 1, col = 1;
            if (escape_pos > 1) {
                sscanf(escape_buffer, "%d;%d", &row, &col);
            }
            move_cursor(row - 1, col - 1);
            break;
        }
        case 'A': { // Cursor up
            int n = 1;
            if (escape_pos > 1) {
                sscanf(escape_buffer, "%d", &n);
            }
            cursor_row = (cursor_row - n < 0) ? 0 : cursor_row - n;
            break;
        }
        case 'B': { // Cursor down
            int n = 1;
            if (escape_pos > 1) {
                sscanf(escape_buffer, "%d", &n);
            }
            cursor_row = (cursor_row + n >= term_rows) ? term_rows - 1 : cursor_row + n;
            break;
        }
        case 'C': { // Cursor right
            int n = 1;
            if (escape_pos > 1) {
                sscanf(escape_buffer, "%d", &n);
            }
            cursor_col = (cursor_col + n >= term_cols) ? term_cols - 1 : cursor_col + n;
            break;
        }
        case 'D': { // Cursor left
            int n = 1;
            if (escape_pos > 1) {
                sscanf(escape_buffer, "%d", &n);
            }
            cursor_col = (cursor_col - n < 0) ? 0 : cursor_col - n;
            break;
        }
        case 'J': { // Clear screen
            int mode = 0;
            if (escape_pos > 1) {
                sscanf(escape_buffer, "%d", &mode);
            }
            if (mode == 2) {
                clear_screen_buffer();
            }
            break;
        }
        case 'K': { // Clear line
            int mode = 0;
            if (escape_pos > 1) {
                sscanf(escape_buffer, "%d", &mode);
            }
            if (screen_buffer && cursor_row >= 0 && cursor_row < term_rows) {
                if (mode == 0) {
                    // Clear from cursor to end of line
                    memset(&screen_buffer[cursor_row][cursor_col], ' ', 
                           term_cols - cursor_col);
                    memset(&attr_buffer[cursor_row][cursor_col], 0,
                           term_cols - cursor_col);
                } else if (mode == 1) {
                    // Clear from start of line to cursor
                    memset(screen_buffer[cursor_row], ' ', cursor_col + 1);
                    memset(attr_buffer[cursor_row], 0, cursor_col + 1);
                } else if (mode == 2) {
                    // Clear entire line
                    memset(screen_buffer[cursor_row], ' ', term_cols);
                    memset(attr_buffer[cursor_row], 0, term_cols);
                }
            }
            break;
        }
        case 'm': { // Set graphics mode
            // Parse multiple parameters
            char *param = escape_buffer;
            char *end;
            
            do {
                int code = strtol(param, &end, 10);
                
                switch (code) {
                    case 0:  // Reset
                        current_attr = 0;
                        break;
                    case 1:  // Bold
                        current_attr |= ATTR_BOLD;
                        break;
                    case 4:  // Underline
                        current_attr |= ATTR_UNDERLINE;
                        break;
                    case 7:  // Reverse
                        current_attr |= ATTR_REVERSE;
                        break;
                    case 22: // Normal intensity
                        current_attr &= ~ATTR_BOLD;
                        break;
                    case 24: // No underline
                        current_attr &= ~ATTR_UNDERLINE;
                        break;
                    case 27: // No reverse
                        current_attr &= ~ATTR_REVERSE;
                        break;
                    // Color codes 30-37, 40-47 - ignore for now
                }
                
                param = end;
                if (*param == ';') param++;
            } while (*param && param < escape_buffer + escape_pos);
            break;
        }
        case 'l':
        case 'h': { // Set/reset mode
            if (strstr(escape_buffer, "?25") == escape_buffer) {
                // Cursor visibility
                cursor_visible = (cmd == 'h');
            }
            break;
        }
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
    attr_buffer[row][col] = current_attr;
}

static void render_screen_buffer(void) {
    if (!screen_buffer || !vterm_buffer) {
        return;
    }
    
    // Clear the framebuffer
    memset(vterm_buffer, 0xFF, buffer_size);
    
    int rendered_chars = 0;
    
    // Render each character
    for (int row = 0; row < term_rows; row++) {
        for (int col = 0; col < term_cols; col++) {
            char ch = screen_buffer[row][col];
            uint8_t attr = attr_buffer[row][col];
            
            if (ch != ' ' || attr != 0) {
                int x = col * CELL_WIDTH;
                int y = row * CELL_HEIGHT;
                
                // Determine colors based on attributes
                int fg_color = COLOR_BLACK;
                int bg_color = COLOR_WHITE;
                
                if (attr & ATTR_REVERSE) {
                    fg_color = COLOR_WHITE;
                    bg_color = COLOR_BLACK;
                }
                
                // Draw background if needed
                if (bg_color == COLOR_BLACK) {
                    draw_rect(x, y, CELL_WIDTH, CELL_HEIGHT, COLOR_BLACK);
                }
                
                // Draw character
                if (ch != ' ') {
                    draw_char_fallback(x, y, ch, fg_color);
                    rendered_chars++;
                }
                
                // Draw underline if needed
                if (attr & ATTR_UNDERLINE) {
                    draw_rect(x, y + CELL_HEIGHT - 2, CELL_WIDTH, 1, fg_color);
                }
            }
        }
    }
    
    printf("Rendered %d non-empty cells\n", rendered_chars);
    
    // Draw cursor if visible
    if (cursor_visible && cursor_row >= 0 && cursor_row < term_rows && 
        cursor_col >= 0 && cursor_col < term_cols) {
        int x = cursor_col * CELL_WIDTH;
        int y = cursor_row * CELL_HEIGHT;
        
        // Draw cursor as a block that inverts the cell
        for (int dy = 0; dy < CELL_HEIGHT; dy++) {
            for (int dx = 0; dx < CELL_WIDTH; dx++) {
                int px = x + dx;
                int py = y + dy;
                if (px >= 0 && px < EPD_7IN5_V2_WIDTH && py >= 0 && py < EPD_7IN5_V2_HEIGHT) {
                    int byte_index = (py * EPD_7IN5_V2_WIDTH + px) / 8;
                    int bit_index = 7 - (px % 8);
                    if (byte_index >= 0 && byte_index < buffer_size) {
                        vterm_buffer[byte_index] ^= (1 << bit_index);
                    }
                }
            }
        }
    }
}
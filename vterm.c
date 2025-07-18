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
#include <signal.h>
#include <setjmp.h>

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
static int vterm_initialized = 0;

// Safety mechanism for crash detection
static jmp_buf crash_recovery;
static volatile int in_vterm_call = 0;

// Forward declarations
static void render_cell(int col, int row, const VTermScreenCell *cell);
static int damage_callback(VTermRect rect, void *user);
static void output_callback(const char *s, size_t len, void *user);
static void crash_handler(int sig);

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

static void crash_handler(int sig) {
    printf("CRASH: Signal %d caught during libvterm operation!\n", sig);
    if (in_vterm_call) {
        printf("CRASH: Jumping back to safety...\n");
        longjmp(crash_recovery, sig);
    }
    exit(1);
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

    // Set up crash handler
    signal(SIGSEGV, crash_handler);
    signal(SIGBUS, crash_handler);
    signal(SIGFPE, crash_handler);

    // Clean up any existing state
    if (vterm) {
        printf("LOG: Cleaning up existing vterm\n");
        vterm_destroy();
    }

    // IMPORTANT: Use much smaller terminal size to avoid crashes
    // The original 100x30 might be too large for libvterm
    int safe_rows = (rows > 24) ? 24 : rows;    // Max 24 rows
    int safe_cols = (cols > 80) ? 80 : cols;    // Max 80 cols
    
    printf("LOG: Adjusting terminal size from %dx%d to %dx%d for safety\n", 
           cols, rows, safe_cols, safe_rows);

    term_rows = safe_rows;
    term_cols = safe_cols;
    pty_fd = pty;
    vterm_buffer = buffer;
    buffer_size = (EPD_7IN5_V2_WIDTH * EPD_7IN5_V2_HEIGHT) / 8;
    vterm_initialized = 0;

    printf("Terminal dimensions: %dx%d\n", safe_cols, safe_rows);
    printf("Buffer size: %zu bytes\n", buffer_size);

    // Try creating libvterm with the safe size
    printf("LOG: Creating libvterm instance (%dx%d)\n", safe_rows, safe_cols);
    
    // Use setjmp/longjmp for crash recovery
    int crash_signal = setjmp(crash_recovery);
    if (crash_signal != 0) {
        printf("CRASH RECOVERY: libvterm crashed with signal %d\n", crash_signal);
        if (vterm) {
            vterm_free(vterm);
            vterm = NULL;
        }
        return -1;
    }
    
    in_vterm_call = 1;
    vterm = vterm_new(safe_rows, safe_cols);
    in_vterm_call = 0;
    
    if (!vterm) {
        printf("Error: Failed to create libvterm instance\n");
        return -1;
    }
    printf("LOG: libvterm instance created successfully\n");

    // Configure libvterm with crash protection
    printf("LOG: Configuring libvterm\n");
    
    in_vterm_call = 1;
    vterm_set_utf8(vterm, 1);
    in_vterm_call = 0;
    printf("LOG: UTF-8 mode set\n");
    
    // Set up output callback
    printf("LOG: Setting up output callback\n");
    in_vterm_call = 1;
    vterm_output_set_callback(vterm, output_callback, NULL);
    in_vterm_call = 0;
    printf("LOG: Output callback set\n");
    
    // Get screen interface
    printf("LOG: Getting screen interface\n");
    in_vterm_call = 1;
    screen = vterm_obtain_screen(vterm);
    in_vterm_call = 0;
    
    if (!screen) {
        printf("Error: Failed to obtain libvterm screen\n");
        vterm_free(vterm);
        vterm = NULL;
        return -1;
    }
    printf("LOG: Screen interface obtained\n");

    // Set up screen callbacks - KEEP DISABLED FOR NOW
    printf("LOG: Setting up screen callbacks\n");
    VTermScreenCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    // callbacks.damage = damage_callback;  // STILL DISABLED
    
    in_vterm_call = 1;
    vterm_screen_set_callbacks(screen, &callbacks, screen);
    in_vterm_call = 0;
    printf("LOG: Screen callbacks set (damage callback disabled)\n");

    // Reset and initialize
    printf("LOG: Resetting screen\n");
    in_vterm_call = 1;
    vterm_screen_reset(screen, 1);
    in_vterm_call = 0;
    printf("LOG: Screen reset complete\n");

    // Clear the framebuffer
    printf("LOG: Clearing framebuffer\n");
    memset(buffer, 0xFF, buffer_size);
    printf("LOG: Framebuffer cleared\n");

    damage_pending = 0;
    vterm_initialized = 1;

    printf("libvterm terminal initialized successfully\n");
    return 0;
}

void vterm_destroy(void) {
    printf("LOG: vterm_destroy START\n");
    vterm_initialized = 0;
    
    if (vterm) {
        printf("Destroying libvterm terminal\n");
        in_vterm_call = 1;
        vterm_free(vterm);
        in_vterm_call = 0;
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
    
    if (!data || len == 0 || !buffer) {
        printf("LOG: vterm_feed_output - invalid parameters (data=%p, len=%zu, buffer=%p)\n", data, len, buffer);
        return;
    }
    
    if (!vterm || !vterm_initialized) {
        printf("LOG: vterm_feed_output - vterm not initialized (vterm=%p, initialized=%d)\n", vterm, vterm_initialized);
        return;
    }
    
    vterm_buffer = buffer;
    
    // SAFETY: Print first few bytes of data to debug
    printf("LOG: Data preview (first 20 bytes): ");
    for (size_t i = 0; i < len && i < 20; i++) {
        printf("%02x ", (unsigned char)data[i]);
    }
    printf("\n");
    
    // Use crash recovery for vterm_input_write
    int crash_signal = setjmp(crash_recovery);
    if (crash_signal != 0) {
        printf("CRASH RECOVERY: vterm_input_write crashed with signal %d\n", crash_signal);
        return;
    }
    
    printf("LOG: About to call vterm_input_write with crash protection...\n");
    fflush(stdout);
    
    // Process in very small chunks with crash protection
    const size_t chunk_size = 1;
    for (size_t offset = 0; offset < len; offset += chunk_size) {
        size_t remaining = len - offset;
        size_t current_chunk = (remaining < chunk_size) ? remaining : chunk_size;
        
        printf("LOG: Processing byte %zu: 0x%02x ('%c')\n", 
               offset, (unsigned char)data[offset], 
               isprint(data[offset]) ? data[offset] : '?');
        fflush(stdout);
        
        // Validate vterm is still valid
        if (!vterm) {
            printf("ERROR: vterm became NULL during processing!\n");
            return;
        }
        
        // Try the actual call with crash protection
        in_vterm_call = 1;
        vterm_input_write(vterm, data + offset, current_chunk);
        in_vterm_call = 0;
        
        printf("LOG: Byte %zu processed successfully\n", offset);
    }
    
    printf("LOG: All bytes processed successfully\n");
    damage_pending = 1;
    printf("LOG: vterm_feed_output COMPLETE\n");
}

void vterm_process_input(uint32_t keycode, int modifiers) {
    printf("LOG: vterm_process_input START - keycode=%u, modifiers=%d\n", keycode, modifiers);
    
    if (pty_fd < 0 || !vterm || !vterm_initialized) {
        printf("LOG: vterm_process_input - invalid state (pty_fd=%d, vterm=%p, initialized=%d)\n", pty_fd, vterm, vterm_initialized);
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
        printf("Sending special key via libvterm: %d\n", vterm_key);
        
        // Use crash protection for keyboard input
        int crash_signal = setjmp(crash_recovery);
        if (crash_signal != 0) {
            printf("CRASH RECOVERY: vterm_keyboard_key crashed with signal %d\n", crash_signal);
            return;
        }
        
        in_vterm_call = 1;
        vterm_keyboard_key(vterm, vterm_key, vterm_mods);
        in_vterm_call = 0;
        printf("LOG: Special key processed successfully\n");
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
    
    if (!buffer || !screen || !vterm_initialized) {
        printf("vterm_redraw: invalid parameters (buffer=%p, screen=%p, initialized=%d)\n", buffer, screen, vterm_initialized);
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
            
            // Use crash protection for screen access
            int crash_signal = setjmp(crash_recovery);
            if (crash_signal != 0) {
                printf("CRASH RECOVERY: vterm_screen_get_cell crashed at %d,%d\n", row, col);
                continue;
            }
            
            in_vterm_call = 1;
            int cell_valid = vterm_screen_get_cell(screen, pos, &cell);
            in_vterm_call = 0;
            
            if (cell_valid && cell.chars[0] != 0) {
                render_cell(col, row, &cell);
                rendered_chars++;
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

static void output_callback(const char *s, size_t len, void *user) {
    (void)user; // Suppress unused parameter warning
    
    printf("LOG: output_callback called with %zu bytes\n", len);
    
    if (pty_fd >= 0 && s && len > 0) {
        ssize_t written = write(pty_fd, s, len);
        if (written < 0) {
            printf("LOG: output_callback - write failed\n");
        } else {
            printf("LOG: output_callback - wrote %zd bytes to PTY\n", written);
        }
    }
}

static int damage_callback(VTermRect rect, void *user) {
    (void)user; // Suppress unused parameter warning
    
    printf("LOG: damage_callback called - rows %d-%d, cols %d-%d\n", 
           rect.start_row, rect.end_row, rect.start_col, rect.end_col);
    
    // SAFETY: Add bounds checking
    if (rect.start_row < 0 || rect.end_row > term_rows || 
        rect.start_col < 0 || rect.end_col > term_cols) {
        printf("LOG: damage_callback - invalid rect bounds, ignoring\n");
        return 1;
    }
    
    if (!vterm_buffer || !vterm_initialized) {
        printf("LOG: damage_callback - not ready (buffer=%p, initialized=%d)\n", vterm_buffer, vterm_initialized);
        return 1;
    }
    
    printf("LOG: damage_callback - bounds check passed\n");
    
    // SAFETY: Only render the damaged area instead of full screen
    VTermScreenCell cell;
    int rendered_chars = 0;
    
    printf("LOG: Rendering damaged area: rows %d-%d, cols %d-%d\n", 
           rect.start_row, rect.end_row, rect.start_col, rect.end_col);
    
    for (int row = rect.start_row; row < rect.end_row && row < term_rows; row++) {
        for (int col = rect.start_col; col < rect.end_col && col < term_cols; col++) {
            VTermPos pos = {.row = row, .col = col};
            
            // Initialize cell to safe defaults
            memset(&cell, 0, sizeof(cell));
            
            if (vterm_screen_get_cell(screen, pos, &cell)) {
                render_cell(col, row, &cell);
                if (cell.chars[0] != 0) {
                    rendered_chars++;
                }
            }
        }
    }
    
    printf("LOG: damage_callback rendered %d chars\n", rendered_chars);
    damage_pending = 1;
    printf("LOG: damage_callback returning\n");
    return 1;
}

static void render_cell(int col, int row, const VTermScreenCell *cell) {
    if (!cell || !vterm_buffer) {
        return;
    }
    
    int x = col * CELL_WIDTH;
    int y = row * CELL_HEIGHT;

    // Bounds checking
    if (x < 0 || y < 0 || x >= EPD_7IN5_V2_WIDTH || y >= EPD_7IN5_V2_HEIGHT) {
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
        draw_rect(x, y, CELL_WIDTH, CELL_HEIGHT, COLOR_BLACK);
    }

    // Draw character if present
    if (cell->chars[0] != 0) {
        char ch[5] = {0};
        int len = vterm_unicode_to_utf8(cell->chars[0], ch);
        if (len > 0 && ch[0] >= 0x20 && ch[0] <= 0x7F) {
            draw_char_fallback(x, y, ch[0], fg_color);
        }
    }
    
    // Draw underline if needed
    if (cell->attrs.underline) {
        draw_rect(x, y + CELL_HEIGHT - 2, CELL_WIDTH, 1, fg_color);
    }
}
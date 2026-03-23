#include "screen.h"
#include "EPD_7in5_V2.h"
#include "font8x16.h"
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include "settings.h"

#define CELL_WIDTH 8
#define CELL_HEIGHT 16
#define COLOR_WHITE 0
#define COLOR_BLACK 1

#define OUTPUT_BUFFER_SIZE 8192

static char output_buffer[OUTPUT_BUFFER_SIZE];
static size_t output_buffer_pos = 0;
static int output_buffer_dirty = 0;
static size_t buffer_size = (SCREEN_WIDTH * SCREEN_HEIGHT / 8);
static uint8_t *framebuffer = NULL;
static int damage_pending = 1;
static uint8_t *last_doc;
static size_t last_doc_len = 0;
static uint8_t *current_doc;
static size_t current_doc_len = 0;

static struct {
    char ch;
    int fg_color;
    int bg_color;
    uint8_t attrs; // bold, underline, etc.
} screen_buffer[ED_ROWS][ED_COLS]; // Max size

// Forward Declarations
static void set_pixel(int x, int y, int color);
static void draw_char(int x, int y, char ch, int fg_color, int bg_color, uint8_t attrs);
static void screen_render();
void map_doc_coords(uint8_t *doc, size_t len, size_t cursor);

void screen_full_draw();
void screen_partial_draw(uint8_t *buffer, int start_x, int start_y, int end_x, int end_y);

int screen_init() {
    framebuffer = (uint8_t *)malloc(buffer_size);
    if (!framebuffer) {
        printf("Failed to allocate memory for freambuffer\n");
        screen_destroy();
        return -1;
    }

    // Initialize buffer to white (all bits set to 1)
    memset(framebuffer, 0xFF, buffer_size);
    printf("Framebuffer allocated white: %zu bytes\n", buffer_size);

    printf("Attempting inital draw (all white)\n");

    if (framebuffer) {
        EPD_7IN5_V2_Display(framebuffer);
        //screen_full_draw(); 
    } else {
        printf("Failed to draw screen\n");
        return -1;
    }

    // Initialize the screen buffer
    for (int r = 0; r < ED_ROWS; r++) {
        for (int c = 0; c < ED_COLS; c++) {
            screen_buffer[r][c].ch = ' ';
            screen_buffer[r][c].fg_color = COLOR_BLACK;
            screen_buffer[r][c].bg_color = COLOR_WHITE;
            screen_buffer[r][c].attrs = 0;
        }
    }
    
    return 0;
}

int handle_screen(uint8_t *doc, size_t len, size_t cursor_pos) { 
    current_doc_len = len;
    
    // if we need to draw, do it
    if (damage_pending != 0) {
        printf("Screen has pending damage, adding to framebuffer\n");

        printf("Screen doc = %s\n", doc);
        printf("Screen doc len = %zu\n", len);

        // take doc and process it into something the screen functions can use
        map_doc_coords(doc, len, cursor_pos);
        screen_full_draw(); 
        damage_pending = 0; // reset the damange pending, no need to draw now
    }

    return 0;
}

void screen_destroy() {
    free(framebuffer);
}

void set_screen_damage() {
    damage_pending = 1;
}

// Internal Functions
static void set_pixel(int x, int y, int color) {
    if (!framebuffer || x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) {
        return;
    }
    
    int byte_index = (y * SCREEN_WIDTH + x) / 8;
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

static void screen_render() {
    if (!framebuffer) return;
    
    if (current_doc_len != 0) {
        // Clear framebuffer to white
        memset(framebuffer, 0xFF, buffer_size);
    
        int rendered_chars = 0;
    
        for (int r = 0; r < ED_ROWS; r++) {
            for (int c = 0; c < ED_COLS; c++) {
                if (screen_buffer[r][c].ch != ' ') {
                    int x = c * CELL_WIDTH;
                    int y = r * CELL_HEIGHT;
                
                    if (x < SCREEN_WIDTH && y < SCREEN_HEIGHT) {
                        draw_char(x, y, screen_buffer[r][c].ch,
                                  screen_buffer[r][c].fg_color,
                                  screen_buffer[r][c].bg_color,
                                  screen_buffer[r][c].attrs);
                        rendered_chars++;
                    }
                }
            }
        }
        printf("\n");
    
        printf("Rendered %d characters\n", rendered_chars);
    }
}

// I think something is wrong with row++ and col++, that doesn't seem right
void map_doc_coords(uint8_t *doc, size_t doc_len, size_t cursor) {
    int row, col = 0;
    
    for (size_t i = 0; i < doc_len; i++) {
        if (i < 0) {
            printf("i is > 0 - Breaking\n");
            break;
        } else if (i > 1920) {
            printf("i is < 1920 - Breaking]n");
            break
        } else {
            printf("is i mod 80 == 0? ");
            if (i % 80 == 0) {
                printf("yes\n");
                //new line
                row++;
                col = 0;
            }
        }

        printf("We are setting screen_buffer %c", screen_buffer[row][col].ch);
        screen_buffer[row][col].ch = doc[i];
        screen_buffer[row][col].fg_color = COLOR_BLACK;
        screen_buffer[row][col].bg_color = COLOR_WHITE;
        col++;
    } 
    printf("\n");
}

void screen_full_draw() {
    if (framebuffer) {
        printf("Flushing display...\n");
        screen_render();
        printf("Screen rendered. Sending to display...\n");
        EPD_7IN5_V2_Display(framebuffer);
    }
}


void screen_partial_draw(uint8_t *buffer, int start_x, int start_y, int end_x, int end_y) {
    // Uh, yeah, use the EPD function for this
}

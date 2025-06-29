#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "unicode_font.h"
#include "EPD_7in5_V2.h"
#include "hwconfig.h"

// Test program to display Japanese characters
int main(void) {
    // Initialize hardware
    if (DEV_Module_Init() != 0) {
        printf("Hardware init failed.\n");
        return -1;
    }

    if (EPD_7IN5_V2_Init() != 0) {
        printf("E-ink display init failed.\n");
        DEV_Module_Exit();
        return -1;
    }

    // Initialize Unicode font system
    if (unicode_font_init() != 0) {
        printf("Unicode font init failed.\n");
        DEV_Module_Exit();
        return -1;
    }

    // Clear display
    EPD_7IN5_V2_Clear();

    // Create framebuffer
    size_t buffer_size = (800 * 480) / 8;
    uint8_t *image = malloc(buffer_size);
    if (!image) {
        printf("Failed to allocate memory\n");
        unicode_font_cleanup();
        DEV_Module_Exit();
        return -1;
    }
    memset(image, 0xFF, buffer_size); // White background

    // Test characters to display
    uint32_t test_chars[] = {
        // ASCII
        0x0048, 0x0065, 0x006C, 0x006C, 0x006F, 0x0020, // "Hello "
        // Hiragana
        0x3053, 0x3093, 0x306B, 0x3061, 0x306F, 0x0020, // "こんにちは " (konnichiwa)
        // Katakana  
        0x30B3, 0x30F3, 0x30CB, 0x30C1, 0x30EF, 0x0020, // "コンニチワ " (konnichiwa)
        // Kanji
        0x65E5, 0x672C, 0x8A9E, 0x0000 // "日本語" (nihongo - Japanese language)
    };

    // Draw test characters
    int x = 10, y = 50;
    for (int i = 0; test_chars[i] != 0; i++) {
        const uint8_t *glyph = get_glyph_bitmap(test_chars[i]);
        
        // Draw character
        for (int row = 0; row < 16; row++) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < 8; col++) {
                if (bits & (1 << (7 - col))) {
                    // Set pixel black
                    int px = x + col;
                    int py = y + row;
                    if (px >= 0 && px < 800 && py >= 0 && py < 480) {
                        int byte_index = (py * 800 + px) / 8;
                        int bit_index = 7 - (px % 8);
                        image[byte_index] &= ~(1 << bit_index);
                    }
                }
            }
        }
        
        x += 8; // Move to next character position
        if (x > 750) { // Wrap to next line
            x = 10;
            y += 20;
        }
    }

    // Display the image
    EPD_7IN5_V2_Display(image);

    printf("Japanese characters displayed. Press Enter to continue...\n");
    getchar();

    // Cleanup
    free(image);
    unicode_font_cleanup();
    EPD_7IN5_V2_Sleep();
    DEV_Module_Exit();

    return 0;
}
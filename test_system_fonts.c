#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "font_loader.h"
#include "EPD_7in5_V2.h"
#include "hwconfig.h"

// Test program to display characters using system fonts
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

    // Initialize font loader
    if (font_loader_init() != 0) {
        printf("Font loader init failed.\n");
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
        font_loader_cleanup();
        DEV_Module_Exit();
        return -1;
    }
    memset(image, 0xFF, buffer_size); // White background

    // Test text with various scripts
    const char *test_texts[] = {
        "Hello World! 123",
        "こんにちは世界",  // Japanese: Hello World
        "안녕하세요 세계",   // Korean: Hello World  
        "你好世界",         // Chinese: Hello World
        "Здравствуй мир",   // Russian: Hello World
        "مرحبا بالعالم",    // Arabic: Hello World
        "Γεια σου κόσμε",   // Greek: Hello World
        NULL
    };

    // Helper function to set pixel
    auto void set_pixel_helper(int x, int y, int color) {
        if (x < 0 || x >= 800 || y < 0 || y >= 480) return;
        int byte_index = (y * 800 + x) / 8;
        int bit_index = 7 - (x % 8);
        if (color) {
            image[byte_index] &= ~(1 << bit_index); // Black
        } else {
            image[byte_index] |= (1 << bit_index);  // White
        }
    }

    // Helper function to draw character
    auto void draw_char_helper(int x, int y, uint32_t codepoint) {
        int width, height, advance;
        const uint8_t *bitmap = get_char_bitmap(codepoint, &width, &height, &advance);
        
        if (bitmap) {
            for (int row = 0; row < height && row < 16; row++) {
                for (int col = 0; col < width && col < 8; col++) {
                    if (bitmap[row * width + col]) {
                        set_pixel_helper(x + col, y + row, 1);
                    }
                }
            }
        } else {
            // Draw missing character box
            for (int i = 0; i < 16; i++) {
                for (int j = 0; j < 8; j++) {
                    if (i == 0 || i == 15 || j == 0 || j == 7) {
                        set_pixel_helper(x + j, y + i, 1);
                    }
                }
            }
        }
    }

    // UTF-8 to Unicode conversion helper
    auto uint32_t utf8_to_unicode_helper(const char *utf8, int *bytes_consumed) {
        const unsigned char *s = (const unsigned char *)utf8;
        uint32_t codepoint = 0;
        
        if (s[0] < 0x80) {
            codepoint = s[0];
            *bytes_consumed = 1;
        } else if ((s[0] & 0xE0) == 0xC0) {
            codepoint = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
            *bytes_consumed = 2;
        } else if ((s[0] & 0xF0) == 0xE0) {
            codepoint = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
            *bytes_consumed = 3;
        } else if ((s[0] & 0xF8) == 0xF0) {
            codepoint = ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
            *bytes_consumed = 4;
        } else {
            codepoint = 0xFFFD; // Replacement character
            *bytes_consumed = 1;
        }
        
        return codepoint;
    }

    // Draw test texts
    int y = 50;
    for (int text_idx = 0; test_texts[text_idx] != NULL; text_idx++) {
        const char *text = test_texts[text_idx];
        int x = 10;
        
        printf("Rendering: %s\n", text);
        
        // Process UTF-8 string
        const char *ptr = text;
        while (*ptr) {
            int bytes_consumed;
            uint32_t codepoint = utf8_to_unicode_helper(ptr, &bytes_consumed);
            
            if (codepoint != 0) {
                draw_char_helper(x, y, codepoint);
                x += 8; // Fixed-width spacing
                
                if (x > 750) { // Wrap to next line
                    x = 10;
                    y += 20;
                }
            }
            
            ptr += bytes_consumed;
        }
        
        y += 25; // Space between different texts
        if (y > 400) break; // Don't go off screen
    }

    // Display the image
    printf("Displaying rendered text...\n");
    EPD_7IN5_V2_Display(image);

    printf("System fonts test complete. Press Enter to continue...\n");
    getchar();

    // Cleanup
    free(image);
    font_loader_cleanup();
    EPD_7IN5_V2_Sleep();
    DEV_Module_Exit();

    return 0;
}
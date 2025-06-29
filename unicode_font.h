#ifndef UNICODE_FONT_H
#define UNICODE_FONT_H

#include <stdint.h>

// Font metrics
#define FONT_WIDTH 8
#define FONT_HEIGHT 16
#define FONT_BYTES_PER_CHAR 16

// Character ranges
#define ASCII_START 0x0020
#define ASCII_END 0x007F
#define HIRAGANA_START 0x3040
#define HIRAGANA_END 0x309F
#define KATAKANA_START 0x30A0
#define KATAKANA_END 0x30FF
#define KANJI_START 0x4E00
#define KANJI_END 0x9FAF

// Font structures
typedef struct {
    uint32_t codepoint;
    uint8_t bitmap[FONT_BYTES_PER_CHAR];
} FontGlyph;

typedef struct {
    const FontGlyph *glyphs;
    uint32_t count;
    uint32_t start_codepoint;
    uint32_t end_codepoint;
} FontRange;

// Function declarations
int unicode_font_init(void);
void unicode_font_cleanup(void);
const uint8_t* get_glyph_bitmap(uint32_t codepoint);
int is_fullwidth_char(uint32_t codepoint);

#endif // UNICODE_FONT_H
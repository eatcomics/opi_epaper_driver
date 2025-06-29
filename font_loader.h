#ifndef FONT_LOADER_H
#define FONT_LOADER_H

#include <stdint.h>
#include <ft2build.h>
#include FT_FREETYPE_H

// Font metrics
#define FONT_SIZE 16
#define FONT_WIDTH 8
#define FONT_HEIGHT 16

// Font cache entry
typedef struct {
    uint32_t codepoint;
    uint8_t bitmap[FONT_HEIGHT * FONT_WIDTH];
    int width;
    int height;
    int advance;
    int bearing_x;
    int bearing_y;
} FontCacheEntry;

// Font cache
#define FONT_CACHE_SIZE 1024
typedef struct {
    FontCacheEntry entries[FONT_CACHE_SIZE];
    int count;
} FontCache;

// Font manager structure
typedef struct {
    FT_Library library;
    FT_Face face_regular;
    FT_Face face_cjk;
    FontCache cache;
    char *font_path_regular;
    char *font_path_cjk;
} FontManager;

// Function declarations
int font_loader_init(void);
void font_loader_cleanup(void);
const uint8_t* get_char_bitmap(uint32_t codepoint, int *width, int *height, int *advance);
int render_char_to_bitmap(uint32_t codepoint, uint8_t *bitmap, int max_width, int max_height);
int find_system_font(const char *font_name, char *path_buffer, size_t buffer_size);
int load_font_face(const char *font_path, FT_Face *face);

#endif // FONT_LOADER_H
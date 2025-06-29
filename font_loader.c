#include "font_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

static FontManager font_manager = {0};

// Common font search paths
static const char* font_search_paths[] = {
    "/usr/share/fonts/",
    "/usr/local/share/fonts/",
    "/system/fonts/",
    "~/.fonts/",
    "/usr/share/fonts/truetype/",
    "/usr/share/fonts/opentype/",
    NULL
};

// Preferred fonts for different scripts
static const char* latin_fonts[] = {
    "DejaVuSansMono.ttf",
    "LiberationMono-Regular.ttf", 
    "UbuntuMono-R.ttf",
    "Courier New.ttf",
    "consolas.ttf",
    "monaco.ttf",
    NULL
};

static const char* cjk_fonts[] = {
    "NotoSansCJK-Regular.ttc",
    "NotoSansJP-Regular.otf",
    "DroidSansFallback.ttf",
    "wqy-microhei.ttc",
    "fireflysung.ttf",
    "SimSun.ttf",
    "msyh.ttf",
    "YuGothic.ttf",
    NULL
};

// Search for a font file recursively
int search_font_recursive(const char *dir_path, const char *font_name, char *result_path, size_t result_size) {
    DIR *dir = opendir(dir_path);
    if (!dir) return 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            // Recursively search subdirectories
            if (search_font_recursive(full_path, font_name, result_path, result_size)) {
                closedir(dir);
                return 1;
            }
        } else if (S_ISREG(st.st_mode)) {
            // Check if this is the font we're looking for
            if (strcasestr(entry->d_name, font_name) != NULL) {
                strncpy(result_path, full_path, result_size - 1);
                result_path[result_size - 1] = '\0';
                closedir(dir);
                return 1;
            }
        }
    }

    closedir(dir);
    return 0;
}

int find_system_font(const char *font_name, char *path_buffer, size_t buffer_size) {
    for (int i = 0; font_search_paths[i] != NULL; i++) {
        const char *search_path = font_search_paths[i];
        
        // Handle home directory expansion
        char expanded_path[1024];
        if (search_path[0] == '~') {
            const char *home = getenv("HOME");
            if (home) {
                snprintf(expanded_path, sizeof(expanded_path), "%s%s", home, search_path + 1);
                search_path = expanded_path;
            }
        }

        if (search_font_recursive(search_path, font_name, path_buffer, buffer_size)) {
            return 1;
        }
    }
    return 0;
}

int load_font_face(const char *font_path, FT_Face *face) {
    FT_Error error = FT_New_Face(font_manager.library, font_path, 0, face);
    if (error) {
        printf("Failed to load font: %s (error: %d)\n", font_path, error);
        return -1;
    }

    // Set font size (16pt at 72 DPI)
    error = FT_Set_Char_Size(*face, 0, FONT_SIZE * 64, 72, 72);
    if (error) {
        printf("Failed to set font size (error: %d)\n", error);
        FT_Done_Face(*face);
        return -1;
    }

    return 0;
}

int font_loader_init(void) {
    // Initialize FreeType library
    FT_Error error = FT_Init_FreeType(&font_manager.library);
    if (error) {
        printf("Failed to initialize FreeType library (error: %d)\n", error);
        return -1;
    }

    // Find and load Latin font
    char font_path[1024];
    int found_latin = 0;
    for (int i = 0; latin_fonts[i] != NULL; i++) {
        if (find_system_font(latin_fonts[i], font_path, sizeof(font_path))) {
            if (load_font_face(font_path, &font_manager.face_regular) == 0) {
                font_manager.font_path_regular = strdup(font_path);
                printf("Loaded Latin font: %s\n", font_path);
                found_latin = 1;
                break;
            }
        }
    }

    if (!found_latin) {
        printf("Warning: No suitable Latin font found, using fallback\n");
    }

    // Find and load CJK font
    int found_cjk = 0;
    for (int i = 0; cjk_fonts[i] != NULL; i++) {
        if (find_system_font(cjk_fonts[i], font_path, sizeof(font_path))) {
            if (load_font_face(font_path, &font_manager.face_cjk) == 0) {
                font_manager.font_path_cjk = strdup(font_path);
                printf("Loaded CJK font: %s\n", font_path);
                found_cjk = 1;
                break;
            }
        }
    }

    if (!found_cjk) {
        printf("Warning: No suitable CJK font found\n");
    }

    // Initialize cache
    font_manager.cache.count = 0;

    return (found_latin || found_cjk) ? 0 : -1;
}

void font_loader_cleanup(void) {
    if (font_manager.face_regular) {
        FT_Done_Face(font_manager.face_regular);
        font_manager.face_regular = NULL;
    }

    if (font_manager.face_cjk) {
        FT_Done_Face(font_manager.face_cjk);
        font_manager.face_cjk = NULL;
    }

    if (font_manager.library) {
        FT_Done_FreeType(font_manager.library);
        font_manager.library = NULL;
    }

    if (font_manager.font_path_regular) {
        free(font_manager.font_path_regular);
        font_manager.font_path_regular = NULL;
    }

    if (font_manager.font_path_cjk) {
        free(font_manager.font_path_cjk);
        font_manager.font_path_cjk = NULL;
    }

    font_manager.cache.count = 0;
}

// Check if character is CJK
int is_cjk_char(uint32_t codepoint) {
    return (codepoint >= 0x4E00 && codepoint <= 0x9FFF) ||  // CJK Unified Ideographs
           (codepoint >= 0x3400 && codepoint <= 0x4DBF) ||  // CJK Extension A
           (codepoint >= 0x3040 && codepoint <= 0x309F) ||  // Hiragana
           (codepoint >= 0x30A0 && codepoint <= 0x30FF) ||  // Katakana
           (codepoint >= 0xFF00 && codepoint <= 0xFFEF);    // Halfwidth/Fullwidth Forms
}

// Find character in cache
FontCacheEntry* find_in_cache(uint32_t codepoint) {
    for (int i = 0; i < font_manager.cache.count; i++) {
        if (font_manager.cache.entries[i].codepoint == codepoint) {
            return &font_manager.cache.entries[i];
        }
    }
    return NULL;
}

// Add character to cache
FontCacheEntry* add_to_cache(uint32_t codepoint) {
    if (font_manager.cache.count >= FONT_CACHE_SIZE) {
        // Simple replacement: overwrite oldest entry
        font_manager.cache.count = 0;
    }

    FontCacheEntry *entry = &font_manager.cache.entries[font_manager.cache.count];
    font_manager.cache.count++;
    
    entry->codepoint = codepoint;
    return entry;
}

int render_char_to_bitmap(uint32_t codepoint, uint8_t *bitmap, int max_width, int max_height) {
    // Choose appropriate font face
    FT_Face face = NULL;
    if (is_cjk_char(codepoint) && font_manager.face_cjk) {
        face = font_manager.face_cjk;
    } else if (font_manager.face_regular) {
        face = font_manager.face_regular;
    }

    if (!face) {
        // No suitable font available
        return -1;
    }

    // Load glyph
    FT_UInt glyph_index = FT_Get_Char_Index(face, codepoint);
    if (glyph_index == 0) {
        // Character not found in font
        return -1;
    }

    FT_Error error = FT_Load_Glyph(face, glyph_index, FT_LOAD_DEFAULT);
    if (error) {
        return -1;
    }

    // Render glyph to bitmap
    error = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_MONO);
    if (error) {
        return -1;
    }

    FT_Bitmap *ft_bitmap = &face->glyph->bitmap;
    
    // Clear output bitmap
    memset(bitmap, 0, max_width * max_height);

    // Copy FreeType bitmap to our format
    int copy_width = (ft_bitmap->width < max_width) ? ft_bitmap->width : max_width;
    int copy_height = (ft_bitmap->rows < max_height) ? ft_bitmap->rows : max_height;

    for (int y = 0; y < copy_height; y++) {
        for (int x = 0; x < copy_width; x++) {
            // FreeType mono bitmap: 1 bit per pixel, packed
            int byte_index = y * ft_bitmap->pitch + (x / 8);
            int bit_index = 7 - (x % 8);
            
            if (ft_bitmap->buffer[byte_index] & (1 << bit_index)) {
                // Set pixel in our bitmap (row-major, 1 byte per pixel)
                bitmap[y * max_width + x] = 1;
            }
        }
    }

    return 0;
}

const uint8_t* get_char_bitmap(uint32_t codepoint, int *width, int *height, int *advance) {
    // Check cache first
    FontCacheEntry *cached = find_in_cache(codepoint);
    if (cached) {
        if (width) *width = cached->width;
        if (height) *height = cached->height;
        if (advance) *advance = cached->advance;
        return cached->bitmap;
    }

    // Not in cache, render it
    FontCacheEntry *entry = add_to_cache(codepoint);
    if (!entry) {
        return NULL;
    }

    // Render character
    uint8_t temp_bitmap[FONT_HEIGHT * FONT_WIDTH];
    if (render_char_to_bitmap(codepoint, temp_bitmap, FONT_WIDTH, FONT_HEIGHT) != 0) {
        // Fallback: return missing character bitmap
        memset(entry->bitmap, 0, sizeof(entry->bitmap));
        // Draw a simple box for missing characters
        for (int i = 0; i < FONT_HEIGHT; i++) {
            for (int j = 0; j < FONT_WIDTH; j++) {
                if (i == 0 || i == FONT_HEIGHT-1 || j == 0 || j == FONT_WIDTH-1) {
                    entry->bitmap[i * FONT_WIDTH + j] = 1;
                }
            }
        }
        entry->width = FONT_WIDTH;
        entry->height = FONT_HEIGHT;
        entry->advance = FONT_WIDTH;
    } else {
        // Copy rendered bitmap
        memcpy(entry->bitmap, temp_bitmap, sizeof(entry->bitmap));
        entry->width = FONT_WIDTH;
        entry->height = FONT_HEIGHT;
        entry->advance = FONT_WIDTH;
    }

    if (width) *width = entry->width;
    if (height) *height = entry->height;
    if (advance) *advance = entry->advance;
    
    return entry->bitmap;
}
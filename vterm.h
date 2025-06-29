#ifndef VTERM_H
#define VTERM_H

#include <stddef.h>
#include <stdint.h>

int vterm_init(int rows, int cols, int pty_fd, uint8_t *buffer);
void vterm_destroy(void);

void vterm_feed_output(const char *data, size_t len, uint8_t *buffer);
void vterm_process_input(uint32_t keycode, int modifiers);

// Call this to manually trigger redraw (optional)
void vterm_redraw(uint8_t *buffer);

// Check if there's pending damage that needs a redraw
int vterm_has_pending_damage(void);

// Display functions
void flush_display(void);
void set_pixel(int x, int y, int color);
void draw_rect(int x, int y, int w, int h, int color);
void draw_char_fallback(int x, int y, char ch, int color);

#endif // VTERM_H
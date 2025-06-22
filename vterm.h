#ifndef VTERM_H
#define VTERM_H

#include <stddef.h>
#include <stdint.h>

int vterm_init(int rows, int cols, int pty_fd);
void vterm_destroy(void);

void vterm_feed_output(const char *data, size_t len);
void vterm_process_input(uint32_t keycode, int modifiers);

// Call this to manually trigger redraw (optional)
void vterm_redraw(void);

#endif // VTERM_H

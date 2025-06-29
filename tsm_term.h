#ifndef TSM_TERM_H
#define TSM_TERM_H

#include <stddef.h>
#include <stdint.h>

// Initialize TSM-based terminal emulator
int tsm_term_init(int rows, int cols, int pty_fd, uint8_t *buffer);
void tsm_term_destroy(void);

// Feed output from PTY to terminal
void tsm_term_feed_output(const char *data, size_t len, uint8_t *buffer);

// Process keyboard input
void tsm_term_process_input(uint32_t keycode, int modifiers);

// Redraw terminal to framebuffer
void tsm_term_redraw(uint8_t *buffer);

// Check if redraw is needed
int tsm_term_has_pending_damage(void);

// Display functions
void tsm_flush_display(void);

#endif // TSM_TERM_H
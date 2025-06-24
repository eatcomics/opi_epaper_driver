#include "pty.h"
#include "vterm.h"
#include "keyboard.h"
#include "keymap.h"
#include "hwconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "EPD_7in5_V2.h"

unsigned long last_input_time = 0;
#define QUIET_TIMEOUT_MS 1200

unsigned long current_millis() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

// Globals
int screen_width = 800;
int screen_height = 480;

/*
extern void draw_char(int x, int y, char ch, int color); // from vterm.c or display.c

void draw_test_message(uint8_t *buffer) {
    const char *msg = "UNABLE TO OPEN PTY";
    int x = 10;
    int y = 10;

    for (int i = 0; msg[i] != '\0'; i++) {
        draw_char(x + i * 8, y, msg[i], 1);
    }

    EPD_7IN5_V2_Display(buffer); // force immediate screen update
}
*/

// MAIN!
int main (void) {
    // Set up the E-Ink Display
    if (DEV_Module_Init() != 0) {
        printf("Hardware init failed.\n");
    }

    // Init the e-ink display
    EPD_7IN5_V2_Init();
    
    // Clear the display
    EPD_7IN5_V2_Clear();

    // Only buffer for the moment, but I'll configure double buffering later 
    size_t buffer_size = (screen_width * screen_height / 8);
    uint8_t *image = (uint8_t *)malloc(buffer_size);
    if (!image) {
        printf("Failed to allocate memory\n");
        DEV_Module_Exit();
        return -1;
    }

    // Configure Keyboard input
    uint32_t keycode;
    int modifiers;

    keyboard_init();
    
    // Set up for PTY
    char *shell = getenv("SHELL");
    if (!shell){
      shell = "/bin/bash";
    }

    char *shell_argv[] = {shell, NULL};

    int term_cols = screen_width/8;
    int term_rows = screen_height/16;
    int pty_fd = setup_pty_and_spawn(shell, shell_argv, term_rows, term_cols); 

    // Init libvterm here
    vterm_init(term_cols, term_rows, pty_fd, image);
    
    if (pty_fd < 0) {
        fprintf(stderr, "Failed to open PTY!\n");
        //draw_test_message(image); // show failure message
        return 1;
    }


    // Handle PTY output
    char buf[4096];
    ssize_t n = read(pty_fd, buf, sizeof(buf));
    if (n > 0) {
        vterm_feed_output(buf, n, image);
        last_input_time = current_millis();
    }

    int run = 1;
    // Da main loop
    // Create main loop that handles reading keys (buffered) waits for a pause in typing,
    //     reads PTY, and updates the e-ink screen with either a partial, or full refresh
    /*while (run) {
        // Handle keyboard
        uint32_t *key = &keycode;
        int *mods = &modifiers;
        if (read_key_event(&key, &mods)) {
            vterm_process_input(keycode, modifiers);
            last_input_time = current_millis();
        }

        if (pty_fd < 0) {
            fprintf(stderr, "Failed to open PTY!\n");
            //draw_test_message(image); // show failure message
            return 1;
        }


        // Handle PTY output
        char buf[4096];
        ssize_t n = read(pty_fd, buf, sizeof(buf));
        if (n > 0) {
            vterm_feed_output(buf, n, image);
            last_input_time = current_millis();
        }

        // Refresh screen after a quiet period
        unsigned long now = current_millis();
        if (now - last_input_time > QUIET_TIMEOUT_MS) {
            vterm_redraw(image);         // redraws screen from vterm buffer to framebuffer
            last_input_time = now;  // don't double-refresh
        }

        usleep(10000); // 10ms idle 
    }
    
    */
    
    // Clean up
    free(image);
    EPD_7IN5_V2_Sleep(); // Sleep the Display
    DEV_Module_Exit();
    keyboard_close();
    vterm_destroy();
    
    return 0;
}

#include "EPD_7in5_V2.h"
#include "pty.h"
#include "vterm.h"
#include "keyboard.h"
#include "keymap.h"
#include "hwconfig.h"
#include "lgpio_gpio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Globals
int screen_width = 800;
int screen_height = 480;

// MAIN!
int main (void) {

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
    int term_rows = screen_height/8;
    int pty_fd = setup_pty_and_spawn(shell, shell_argv, term_rows, term_cols); 

    // Init libvterm here
    vterm_init(term_cols, term_rows, pty_fd);

    // Create main loop that handles reading keys (buffered) waits for a pause in typing, reads PTY, and updates the e-ink screen with either a partial, or full refresh

    /* leaving this here for reference on using the e-ink screen
    if (DEV_Module_Init() != 0) {
        printf("Hardware init failed.\n");
    }

    // Init the e-ink display
    EPD_7IN5_V2_Init();
    
    // Clear the display
    EPD_7IN5_V2_Clear();

    // Creating an image buffer
    size_t buffer_size = screenwidth * screenheight / 8;
    UBYTE *image = (UBYTE *)malloc(buffer_size);
    if (!image) {
        printf("Failed to allocate memory\n");
        DEV_Module_Exit();
        return -1;
    }

    // Send image buffer to display and then free the image
    memset(image, 0xFF, buffer_size);
    EPD_7IN5_V2_Display(image);
    free(image);

    // Sleep the Display
    EPD_7IN5_V2_Sleep();

    DEV_Module_Exit();
    */

    keyboard_close();
    vterm_destroy();
    
    return 0;
}

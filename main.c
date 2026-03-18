#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include "editor.h" // handles buffers, handles modified pieces, handles cursor
#include "epaper.h" // simplifies actual hardware functions
#include "input_handler.h" // handles keyboard

/* Gets viewable part of the current text file from editor, calls draw funcs in epaper.h. Will be in charge of screen clears and screen sleep logic maybe */
#include "screen.h"



// Global Cleanup/Exit
static volatile int cleanup_requested = 0;

// Global Time handling (this can probably go in a separate file later)
unsigned long current_millis() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

void signal_handler(int sig) {
    printf("\nReceived signal %d, cleaning up...\n", sig);
    cleanup_requested = 1;
}

void cleanup_and_exit(int status) {
    printf("Freeing editor resources\n");
    epaper_destroy();
    printf("Freeing editor resources\n");
    editor_destroy();
    printf("Closing keyboard handle\n");
    keyboard_close();
    printf("Freeing screen resources\n");
    screen_destroy();

    if (status != 0) printf("Exited with error: %d\n", status);
    exit(status);
}

int main (void) {
    // Set up signal handlers for clean exit
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Init e-ink display
    epaper_init();

    // Clear the display for use
    epaper_clear();
    
    // Menu
        // New File
        // Open File
        // (The below can wait)
        // Bluetooth (If no keyboard this should start automatically?)
        // Wifi 
        // System Settings

    // Load settings file (screen lock, idle time before draw, redraw settings)
    // Close settings file after settings set

    // If file opened - create a file handler and load the file contents, and set cursor 0,0
    // If new file, create a file handler and set cursor position to 0,0
    if (editor_init(1) != 0) {
        printf("Unable to initialize editor\n");
        cleanup_and_exit(1);
    }

    // Init input layer, grab keyboard, if no keyboard, we'll figure that out
    printf("Initializing keyboard...\n");
    if (keyboard_init() != 0) {
        printf("Keyboard init failed.\n");
        cleanup_and_exit(1);
    }

    // Create Screen
    printf("Initializing screen...\n");
    if (screen_init() != 0) {
        printf("Error initializing screen\n");
        cleanup_and_exit(1);
    }

    int run = 1;
    uint8_t key;
    size_t cursor = 0;
    uint8_t *doc;
    doc = malloc(sizeof(uint8_t)*1920);
    uint8_t doc_len = 0;
    
    // Main Editor Loop
    printf("Entering main loop...\n");
    while (run && !cleanup_requested) {
        // Check for input 
        key = check_keys(&key); 

        // Handle input in file
        if (key != 0)  {
            /*
            doc_insert_bytes(&key, 1);
            cursor = get_cursor_pos();
            */
            doc[doc_len] = key;
            doc_len++;
            printf("Doc = %s\n", doc);
        }

        cursor = doc_temp_cur();

        // Update Screen
        handle_screen(doc, doc_len, cursor);
    }

    // Clean Up
    cleanup_and_exit(0);
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
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
    if (!editor_init(1)) {
        printf("Unable to initialize editor\n");
    }

    // Init input layer, grab keyboard, if no keyboard, we'll figure that out

    // Create Screen

    // Main Loop

        // Check for input
        // Handle input in file
        // Update Screen
        // Draw Screen

    // Clean Up
    epaper_destroy();
}

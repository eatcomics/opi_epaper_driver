#include "pty.h"
#include "vterm.h"
#include "keyboard.h"
#include "keymap.h"
#include "hwconfig.h"
#include "EPD_7in5_V2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

unsigned long last_input_time = 0;
#define QUIET_TIMEOUT_MS 1200

// Global cleanup flag
static volatile int cleanup_requested = 0;

unsigned long current_millis() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

void signal_handler(int sig) {
    printf("\nReceived signal %d, cleaning up...\n", sig);
    cleanup_requested = 1;
}

// Globals
int screen_width = 800;
int screen_height = 480;

// MAIN!
int main (void) {
    // Set up signal handlers for clean exit
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("Starting E-ink Terminal...\n");

    // Set up the E-Ink Display
    printf("Initializing hardware...\n");
    if (DEV_Module_Init() != 0) {
        printf("Hardware init failed.\n");
        return -1;
    }

    // Init the e-ink display
    printf("Initializing E-ink display...\n");
    if (EPD_7IN5_V2_Init() != 0) {
        printf("E-ink display init failed.\n");
        DEV_Module_Exit();
        return -1;
    }
    
    // Clear the display
    printf("Clearing display...\n");
    EPD_7IN5_V2_Clear();

    // Allocate framebuffer
    printf("Allocating framebuffer...\n");
    size_t buffer_size = (screen_width * screen_height / 8);
    uint8_t *image = (uint8_t *)malloc(buffer_size);
    if (!image) {
        printf("Failed to allocate memory for framebuffer\n");
        DEV_Module_Exit();
        return -1;
    }

    // Initialize buffer to white (all bits set to 1)
    memset(image, 0xFF, buffer_size);
    printf("Framebuffer allocated: %zu bytes\n", buffer_size);

    // Configure Keyboard input
    printf("Initializing keyboard...\n");
    if (keyboard_init() != 0) {
        printf("Keyboard init failed.\n");
        free(image);
        DEV_Module_Exit();
        return -1;
    }
    
    // Set up for PTY
    printf("Setting up PTY...\n");
    char *shell = getenv("SHELL");
    if (!shell) {
        shell = "/bin/bash";
    }
    printf("Using shell: %s\n", shell);

    char *shell_argv[] = {shell, "-i", NULL}; // -i for interactive

    int term_cols = screen_width/8;
    int term_rows = screen_height/16;
    printf("Terminal size: %dx%d characters\n", term_cols, term_rows);
    
    int pty_fd = setup_pty_and_spawn(shell, shell_argv, term_rows, term_cols); 

    if (pty_fd < 0) {
        fprintf(stderr, "Failed to open PTY!\n");
        free(image);
        keyboard_close();
        DEV_Module_Exit();
        return -1;
    }
    printf("PTY created successfully, fd=%d\n", pty_fd);

    // Init libvterm here
    printf("Initializing terminal emulator...\n");
    if (vterm_init(term_rows, term_cols, pty_fd, image) != 0) {
        fprintf(stderr, "Failed to initialize vterm!\n");
        free(image);
        keyboard_close();
        close(pty_fd);
        DEV_Module_Exit();
        return -1;
    }

    printf("Terminal initialized successfully\n");

    // Send initial test message
    const char *msg = "Welcome to E-ink Terminal!\r\n";
    ssize_t written = write(pty_fd, msg, strlen(msg));
    if (written < 0) {
        printf("Warning: Failed to write initial message to PTY\n");
    } else {
        printf("Sent welcome message (%zd bytes)\n", written);
    }
    
    // Set PTY to non-blocking
    printf("Setting PTY to non-blocking mode...\n");
    int flags = fcntl(pty_fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
    } else {
        if (fcntl(pty_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
            perror("fcntl F_SETFL");
        }
    }
    
    printf("Entering main loop...\n");
    int run = 1;
    last_input_time = current_millis();
    
    // Give the shell a moment to start up and send initial output
    usleep(100000); // 100ms
    
    // Read any initial output from the shell
    char buf[4096];
    ssize_t n = read(pty_fd, buf, sizeof(buf));
    if (n > 0) {
        printf("Initial shell output: %zd bytes\n", n);
        vterm_feed_output(buf, n, image);
        vterm_redraw(image);
        last_input_time = current_millis();
    }
    
    // Da main loop
    while (run && !cleanup_requested) {
        int activity = 0;
        
        // Handle keyboard input
        uint32_t keycode;
        int modifiers;
        if (read_key_event(&keycode, &modifiers)) {
            vterm_process_input(keycode, modifiers);
            last_input_time = current_millis();
            activity = 1;
        }

        // Handle PTY output
        n = read(pty_fd, buf, sizeof(buf));
        if (n > 0) {
            vterm_feed_output(buf, n, image);
            last_input_time = current_millis();
            activity = 1;
        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            fprintf(stderr, "PTY read error: %s\n", strerror(errno));
            break;
        } else if (n == 0) {
            printf("PTY closed (EOF)\n");
            break;
        }

        // Refresh screen after a quiet period
        unsigned long now = current_millis();
        if (activity && (now - last_input_time > QUIET_TIMEOUT_MS)) {
            printf("Refreshing display...\n");
            vterm_redraw(image);
            last_input_time = now;
        }

        usleep(10000); // 10ms idle 
    }
    
    printf("Exiting main loop, cleaning up...\n");
    
    // Clean up
    free(image);
    EPD_7IN5_V2_Sleep(); // Sleep the Display
    DEV_Module_Exit();
    keyboard_close();
    vterm_destroy();
    close(pty_fd);
    
    printf("Cleanup complete\n");
    return 0;
}
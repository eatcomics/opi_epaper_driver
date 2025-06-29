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
unsigned long last_refresh_time = 0;
#define QUIET_TIMEOUT_MS 1200   // Wait 1.2 seconds after last input before refreshing
#define MIN_REFRESH_INTERVAL_MS 300  // Minimum time between full refreshes

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

    printf("Starting E-ink Terminal with libvterm...\n");

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

    // Use smaller terminal size to avoid libvterm crashes
    int term_cols = 80;  // Standard 80 columns instead of 100
    int term_rows = 24;  // Standard 24 rows instead of 30
    printf("Terminal size: %dx%d characters (reduced for stability)\n", term_cols, term_rows);
    
    int pty_fd = setup_pty_and_spawn(shell, shell_argv, term_rows, term_cols); 

    if (pty_fd < 0) {
        fprintf(stderr, "Failed to open PTY!\n");
        free(image);
        keyboard_close();
        DEV_Module_Exit();
        return -1;
    }
    printf("PTY created successfully, fd=%d\n", pty_fd);

    // Init terminal emulator with libvterm
    printf("Initializing libvterm terminal emulator...\n");
    if (vterm_init(term_rows, term_cols, pty_fd, image) != 0) {
        fprintf(stderr, "Failed to initialize libvterm terminal!\n");
        free(image);
        keyboard_close();
        close(pty_fd);
        DEV_Module_Exit();
        return -1;
    }

    printf("libvterm terminal initialized successfully\n");

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

    // Send initial welcome message
    const char *msg = "Welcome to E-ink Terminal with libvterm!\r\nType commands like 'ls', 'vim', or 'emacs'.\r\n";
    ssize_t written = write(pty_fd, msg, strlen(msg));
    if (written < 0) {
        printf("Warning: Failed to write initial message to PTY\n");
    } else {
        printf("Sent welcome message (%zd bytes)\n", written);
    }
    
    printf("Entering main loop...\n");
    int run = 1;
    last_input_time = current_millis();
    last_refresh_time = last_input_time;
    
    // Give the shell a moment to start up
    printf("Waiting for shell to initialize...\n");
    usleep(500000); // 500ms
    
    // Read any initial output from the shell
    char buf[2048]; // Larger buffer for better performance
    ssize_t n = read(pty_fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        printf("Initial shell output: %zd bytes\n", n);
        vterm_feed_output(buf, n, image);
        last_input_time = current_millis();
    } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        printf("Error reading initial output: %s\n", strerror(errno));
    }
    
    // Do an initial redraw - SAFELY
    printf("Performing initial redraw...\n");
    if (image) {
        vterm_redraw(image);
        last_refresh_time = current_millis();
    }
    
    // Main event loop with improved buffering
    while (run && !cleanup_requested) {
        int activity = 0;
        unsigned long now = current_millis();
        
        // Handle keyboard input (collect multiple keys if typed quickly)
        uint32_t keycode;
        int modifiers;
        int keys_processed = 0;
        
        // Process up to 5 keys in one batch to handle fast typing
        while (keys_processed < 5 && read_key_event(&keycode, &modifiers)) {
            printf("Key: %u (mods=%d)\n", keycode, modifiers);
            vterm_process_input(keycode, modifiers);
            last_input_time = now;
            activity = 1;
            keys_processed++;
        }

        // Handle PTY output (read larger chunks)
        n = read(pty_fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            printf("PTY: %zd bytes\n", n);
            
            vterm_feed_output(buf, n, image);
            last_input_time = now;
            activity = 1;
        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            fprintf(stderr, "PTY read error: %s\n", strerror(errno));
            break;
        } else if (n == 0) {
            printf("PTY closed (EOF)\n");
            break;
        }

        // Smart refresh logic - only refresh after user stops typing/activity
        int should_refresh = 0;
        
        if (vterm_has_pending_damage()) {
            // There's pending damage that needs to be displayed
            if (now - last_input_time > QUIET_TIMEOUT_MS) {
                // User has stopped typing for a while - safe to refresh
                should_refresh = 1;
                printf("Refreshing display...\n");
            } else if (now - last_refresh_time > MIN_REFRESH_INTERVAL_MS * 10) {
                // Force refresh if too much time has passed (3 seconds)
                should_refresh = 1;
                printf("Force refresh due to timeout...\n");
            }
        }
        
        if (should_refresh && image) {
            vterm_redraw(image);
            last_refresh_time = now;
        }

        // Shorter sleep for better responsiveness during typing
        if (activity) {
            usleep(2000);  // 2ms when there's activity
        } else {
            usleep(10000); // 10ms when idle
        }
    }
    
    printf("Exiting main loop, cleaning up...\n");
    
    // Clean up
    printf("Destroying terminal\n");
    vterm_destroy();
    free(image);
    EPD_7IN5_V2_Sleep();
    DEV_Module_Exit();
    keyboard_close();
    close(pty_fd);
    
    printf("Cleanup complete\n");
    return 0;
}
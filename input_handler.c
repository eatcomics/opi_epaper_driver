#include "input_handler.h"
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <libudev.h>
#include <linux/input-event-codes.h>
#include "settings.h"

static int kb_fd = -1;
unsigned long last_input_time; 
int inputs_since_draw = 0;
int modifiers = 0;

typedef struct {
    uint32_t *key_buf;
    size_t len;
} KeyBuffer;

typedef struct {
    uint8_t *key_buf;
    size_t len;
} ConvertedKeyBuffer;

KeyBuffer *input;
ConvertedKeyBuffer *conv_buf;

static int is_keyboard_device(struct udev_device *dev) {
    const char *kbd = udev_device_get_property_value(dev, "ID_INPUT_KEYBOARD");
    return (kbd && strcmp(kbd, "1") == 0);
}

static char keycode_to_ascii(uint32_t keycode, int shift_pressed);

int keyboard_init(void) {
    // Init the key input buffer
    input = malloc(sizeof(KeyBuffer));
    if (!input) {
        perror("malloc input");
        return -1;
    }

    input->key_buf = malloc(10 * sizeof(uint32_t));
    if (!input->key_buf) {
        perror("malloc input->key_buf");
        free(input);
        input = NULL;
        return -1;
    }
    
    input->len = 0;
    
    struct udev *udev = udev_new();
    if (!udev) {
        fprintf(stderr, "keyboard_init: failed to create udev\n");
        return -1;
    }

    struct udev_enumerate *enumerate = udev_enumerate_new(udev);
    udev_enumerate_add_match_subsystem(enumerate, "input");
    udev_enumerate_scan_devices(enumerate);

    struct udev_list_entry *devices = udev_enumerate_get_list_entry(enumerate);
    struct udev_list_entry *entry;

    udev_list_entry_foreach(entry, devices) {
        const char *path = udev_list_entry_get_name(entry);
        struct udev_device *dev = udev_device_new_from_syspath(udev, path);

        if (is_keyboard_device(dev)) {
            const char *devnode = udev_device_get_devnode(dev);
            if (devnode) {
                kb_fd = open(devnode, O_RDONLY | O_NONBLOCK);
                if (kb_fd >= 0) {
                    printf("keyboard_init: using %s\n", devnode);
                    udev_device_unref(dev);
                    udev_enumerate_unref(enumerate);
                    udev_unref(udev);
                    return 0;
                } else {
                    perror("keyboard_init: open failed");
                }
            }
        }

        udev_device_unref(dev);
    }

    udev_enumerate_unref(enumerate);
    udev_unref(udev);
    return -1;
}

void keyboard_close(void) {
    if (kb_fd >= 0) {
        close(kb_fd);
        kb_fd = -1;
    }

    free(conv_buf);
    free(input);
}

int read_key_event(uint32_t *keycode, int *modifiers) {
    struct input_event ev;
    *modifiers = 0;

    while (read(kb_fd, &ev, sizeof(ev)) > 0) {
        if (ev.type == EV_KEY && ev.value == 1) { // Key press
            *keycode = ev.code;

            switch (ev.code) {
                case KEY_LEFTSHIFT:
                case KEY_RIGHTSHIFT:
                case KEY_LEFTCTRL:
                case KEY_RIGHTCTRL:
                case KEY_LEFTALT:
                case KEY_RIGHTALT:
            }

            return 1;
        }
    }

    return 0;
}

size_t check_keys(uint8_t *buf) {
    uint32_t keycode = 0;

    printf("Checking keys...\n");

    if (input == NULL) {
        fprintf(stderr, "input or conv_buf is NULL\n");
        return 0;
    }

    if (input->len >= MAX_KEY_BUFFER) {
        input->len = 0;
    }

    printf("Reading keys...\n");

    for (int i = 0; i < 3; i++) {
        if (read_key_event(&keycode, &modifiers) == 0) {  // assuming 0 = success
            if (input->len < MAX_KEY_BUFFER) {
                input->key_buf[input->len] = keycode;
                input->len++;
            }
        }
    }

    if (input->len > 0) {
        free(conv_buf->key_buf);
        conv_buf->key_buf = malloc(input->len);
        if (conv_buf->key_buf == NULL) {
            fprintf(stderr, "malloc failed\n");
            conv_buf->len = 0;
            return 0;
        }

        conv_buf->len = 0;
        for (size_t i = 0; i < input->len; i++) {
            conv_buf->key_buf[i] = keycode_to_ascii(input->key_buf[i], 0);
            conv_buf->len++;
        }
    }

    printf("Returning buffer length: %zu...\n", conv_buf->len);
    return conv_buf->len;
}


// Complete key mapping table for Linux input event codes to ASCII
static char keycode_to_ascii(uint32_t keycode, int shift_pressed) {
    // Handle letters (KEY_Q=16, KEY_W=17, KEY_E=18, etc.)
    switch (keycode) {
        // QWERTY row 1
        case KEY_Q: return shift_pressed ? 'Q' : 'q';
        case KEY_W: return shift_pressed ? 'W' : 'w';
        case KEY_E: return shift_pressed ? 'E' : 'e';
        case KEY_R: return shift_pressed ? 'R' : 'r';
        case KEY_T: return shift_pressed ? 'T' : 't';
        case KEY_Y: return shift_pressed ? 'Y' : 'y';
        case KEY_U: return shift_pressed ? 'U' : 'u';
        case KEY_I: return shift_pressed ? 'I' : 'i';
        case KEY_O: return shift_pressed ? 'O' : 'o';
        case KEY_P: return shift_pressed ? 'P' : 'p';
        
        // QWERTY row 2
        case KEY_A: return shift_pressed ? 'A' : 'a';
        case KEY_S: return shift_pressed ? 'S' : 's';
        case KEY_D: return shift_pressed ? 'D' : 'd';
        case KEY_F: return shift_pressed ? 'F' : 'f';
        case KEY_G: return shift_pressed ? 'G' : 'g';
        case KEY_H: return shift_pressed ? 'H' : 'h';
        case KEY_J: return shift_pressed ? 'J' : 'j';
        case KEY_K: return shift_pressed ? 'K' : 'k';
        case KEY_L: return shift_pressed ? 'L' : 'l';
        
        // QWERTY row 3
        case KEY_Z: return shift_pressed ? 'Z' : 'z';
        case KEY_X: return shift_pressed ? 'X' : 'x';
        case KEY_C: return shift_pressed ? 'C' : 'c';
        case KEY_V: return shift_pressed ? 'V' : 'v';
        case KEY_B: return shift_pressed ? 'B' : 'b';
        case KEY_N: return shift_pressed ? 'N' : 'n';
        case KEY_M: return shift_pressed ? 'M' : 'm';
    }
    
    // Handle numbers and their shifted symbols
    switch (keycode) {
        case KEY_1: return shift_pressed ? '!' : '1';
        case KEY_2: return shift_pressed ? '@' : '2';
        case KEY_3: return shift_pressed ? '#' : '3';
        case KEY_4: return shift_pressed ? '$' : '4';
        case KEY_5: return shift_pressed ? '%' : '5';
        case KEY_6: return shift_pressed ? '^' : '6';
        case KEY_7: return shift_pressed ? '&' : '7';
        case KEY_8: return shift_pressed ? '*' : '8';
        case KEY_9: return shift_pressed ? '(' : '9';
        case KEY_0: return shift_pressed ? ')' : '0';
    }
    
    // Handle special characters and punctuation
    switch (keycode) {
        case KEY_SPACE: return ' ';
        case KEY_MINUS: return shift_pressed ? '_' : '-';
        case KEY_EQUAL: return shift_pressed ? '+' : '=';
        case KEY_LEFTBRACE: return shift_pressed ? '{' : '[';
        case KEY_RIGHTBRACE: return shift_pressed ? '}' : ']';
        case KEY_BACKSLASH: return shift_pressed ? '|' : '\\';
        case KEY_SEMICOLON: return shift_pressed ? ':' : ';';
        case KEY_APOSTROPHE: return shift_pressed ? '"' : '\'';
        case KEY_GRAVE: return shift_pressed ? '~' : '`';
        case KEY_COMMA: return shift_pressed ? '<' : ',';
        case KEY_DOT: return shift_pressed ? '>' : '.';
        case KEY_SLASH: return shift_pressed ? '?' : '/';
        default: return 0;
    }
}

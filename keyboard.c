#include "keyboard.h"
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <libudev.h>

static int kb_fd = -1;

static int is_keyboard_device(struct udev_device *dev) {
    const char *kbd = udev_device_get_property_value(dev, "ID_INPUT_KEYBOARD");
    return (kbd && strcmp(kbd, "1") == 0);
}

int keyboard_init(void) {
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
                    *modifiers |= VTERM_MOD_SHIFT; break;
                case KEY_LEFTCTRL:
                case KEY_RIGHTCTRL:
                    *modifiers |= VTERM_MOD_CTRL; break;
                case KEY_LEFTALT:
                case KEY_RIGHTALT:
                    *modifiers |= VTERM_MOD_ALT; break;
            }

            return 1;
        }
    }

    return 0; // No event or error
}

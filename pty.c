#include "pty.h"
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <termios.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <termios.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <errno.h>

int setup_pty_and_spawn(const char *program, char *const argv[], int rows, int cols) {
    int master_fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (master_fd < 0) {
        perror("posix_openpt");
        return -1;
    }

    if (grantpt(master_fd) < 0 || unlockpt(master_fd) < 0) {
        perror("grantpt/unlockpt");
        close(master_fd);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        close(master_fd);
        return -1;
    }

    if (pid == 0) {
        // CHILD PROCESS
        setsid(); // Become session leader

        char *slave_name = ptsname(master_fd);
        if (!slave_name) {
            perror("child ptsname");
            exit(1);
        }

        int slave_fd = open(slave_name, O_RDWR);
        if (slave_fd < 0) {
            perror("open slave pty");
            exit(1);
        }

        // Set terminal size
        struct winsize ws = {
            .ws_row = rows,
            .ws_col = cols,
            .ws_xpixel = 0,
            .ws_ypixel = 0
        };
        ioctl(slave_fd, TIOCSWINSZ, &ws);

        // Redirect stdio
        dup2(slave_fd, STDIN_FILENO);
        dup2(slave_fd, STDOUT_FILENO);
        dup2(slave_fd, STDERR_FILENO);
        if (slave_fd > STDERR_FILENO)
            close(slave_fd);

        setenv("TERM", "xterm-256color", 1);
        execvp(program, argv);
        perror("execvp failed");
        exit(1);
    }

    // PARENT returns master fd
    return master_fd;
}




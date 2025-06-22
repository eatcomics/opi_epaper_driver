#ifndef _PTY_H
#define _PTY_H

int setup_pty_and_spawn(const char *program, char *const argv[], int rows,
                        int cols);

#endif

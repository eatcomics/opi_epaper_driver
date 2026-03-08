#ifndef __FILEIO_H
#define __FILEIO_H

#include <stdint.h>

int file_open(char *filename, uint8_t *buffer);

int file_save();
int file_close();

#endif

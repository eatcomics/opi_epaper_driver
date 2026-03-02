#include "fileio.h"
#include "stdio.h"
#include "stdlib.h"
#include <stdint.h>

typedef struct {
    uint8_t *data;
    size_t size;
} file_buffer;


int file_open(char *filename, uint8_t *buffer, size_t len) {
    FILE* file_ptr;

    file_ptr = fopen(filename, "rb");
    if (file_ptr == NULL) {
        printf("Unable to open file: %s", filename);
        return -1;
    }
    fread(&buffer, sizeof(uint8_t), 1, file_ptr);

    fclose(file_ptr);

    return 0;
}

int file_save() {
    return 0;
}


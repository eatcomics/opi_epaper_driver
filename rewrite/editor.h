#ifndef __EDITOR_H
#define __EDITOR_H

#include <stdint.h>
#include <stdlib.h>

int editor_init(int new_file_flag);
void editor_destroy();

int doc_insert_bytes(const uint8_t *bytes, size_t len);

#endif

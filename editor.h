#ifndef __EDITOR_H
#define __EDITOR_H

#include <stdint.h>
#include <stdlib.h>

int editor_init(int new_file_flag);
void editor_destroy();

size_t doc_temp_insert(uint8_t *keypress, uint8_t *buf);
size_t doc_temp_cur();
int doc_insert_bytes(const uint8_t *bytes, size_t len);
size_t doc_build_slice(size_t start_pos, uint8_t *out, size_t out_cap);

size_t get_cursor_pos();

#endif

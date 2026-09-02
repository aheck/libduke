#ifndef LIBDUKE_INPUT_H
#define LIBDUKE_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct DukeInput DukeInput;

DukeInput* duke_input_new_filename(const char *filename);
DukeInput* duke_input_new_memory(const void *data, size_t size);
size_t duke_input_read(DukeInput *input, void *data, size_t size);
bool duke_input_seek(DukeInput *input, uint64_t position);
bool duke_input_size(DukeInput *input, uint64_t *size);
void duke_input_free(DukeInput *input);

#endif /* LIBDUKE_INPUT_H */

#include "input.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum DukeInputType {
    DUKE_INPUT_FILE,
    DUKE_INPUT_MEMORY
} DukeInputType;

struct DukeInput {
    DukeInputType type;
    FILE *file;
    uint8_t *data;
    size_t size;
    size_t position;
};

DukeInput* duke_input_new_filename(const char *filename)
{
    DukeInput *input;

    if (filename == NULL) {
        return NULL;
    }
    input = calloc(1, sizeof(*input));
    if (input == NULL) {
        return NULL;
    }
    input->type = DUKE_INPUT_FILE;
    input->file = fopen(filename, "rb");
    if (input->file == NULL) {
        free(input);
        return NULL;
    }
    return input;
}

DukeInput* duke_input_new_memory(const void *data, size_t size)
{
    DukeInput *input;

    if (data == NULL && size > 0) {
        return NULL;
    }
    input = calloc(1, sizeof(*input));
    if (input == NULL) {
        return NULL;
    }
    input->type = DUKE_INPUT_MEMORY;
    if (size > 0) {
        input->data = malloc(size);
        if (input->data == NULL) {
            free(input);
            return NULL;
        }
        memcpy(input->data, data, size);
    }
    input->size = size;
    return input;
}

size_t duke_input_read(DukeInput *input, void *data, size_t size)
{
    size_t available;

    if (input == NULL || (data == NULL && size > 0)) {
        return 0;
    }
    if (input->type == DUKE_INPUT_FILE) {
        return fread(data, 1, size, input->file);
    }
    available = input->size - input->position;
    if (size > available) {
        size = available;
    }
    if (size > 0) {
        memcpy(data, input->data + input->position, size);
        input->position += size;
    }
    return size;
}

bool duke_input_seek(DukeInput *input, uint64_t position)
{
    if (input == NULL) {
        return false;
    }
    if (input->type == DUKE_INPUT_FILE) {
        return position <= LONG_MAX
            && fseek(input->file, (long) position, SEEK_SET) == 0;
    }
    if (position > input->size) {
        return false;
    }
    input->position = (size_t) position;
    return true;
}

bool duke_input_size(DukeInput *input, uint64_t *size)
{
    long position;
    long end;

    if (input == NULL || size == NULL) {
        return false;
    }
    if (input->type == DUKE_INPUT_MEMORY) {
        *size = input->size;
        return true;
    }
    position = ftell(input->file);
    if (position < 0 || fseek(input->file, 0, SEEK_END) != 0
            || (end = ftell(input->file)) < 0
            || fseek(input->file, position, SEEK_SET) != 0) {
        return false;
    }
    *size = (uint64_t) end;
    return true;
}

void duke_input_free(DukeInput *input)
{
    if (input == NULL) {
        return;
    }
    if (input->file != NULL) {
        fclose(input->file);
    }
    free(input->data);
    free(input);
}

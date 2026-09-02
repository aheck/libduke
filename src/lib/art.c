#include "libduke/art.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "glist.h"

typedef enum DukeArtInputType {
    DUKE_ART_INPUT_FILE,
    DUKE_ART_INPUT_MEMORY
} DukeArtInputType;

typedef struct DukeArtInput {
    DukeArtInputType type;
    FILE *file;
    uint8_t *data;
    size_t size;
    size_t position;
} DukeArtInput;

static void input_free(DukeArtInput *input)
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

static size_t input_read(DukeArtInput *input, void *data, size_t size)
{
    if (input == NULL || data == NULL) {
        return 0;
    }
    if (input->type == DUKE_ART_INPUT_FILE) {
        return fread(data, 1, size, input->file);
    }
    if (input->position > input->size || size > input->size - input->position) {
        return 0;
    }
    memcpy(data, input->data + input->position, size);
    input->position += size;
    return size;
}

static bool input_seek(DukeArtInput *input, uint64_t position)
{
    if (input == NULL) {
        return false;
    }
    if (input->type == DUKE_ART_INPUT_FILE) {
        return position <= LONG_MAX
            && fseek(input->file, (long) position, SEEK_SET) == 0;
    }
    if (position > input->size) {
        return false;
    }
    input->position = (size_t) position;
    return true;
}

static bool input_size(DukeArtInput *input, uint64_t *size)
{
    long position;
    long end;

    if (input == NULL || size == NULL) {
        return false;
    }
    if (input->type == DUKE_ART_INPUT_MEMORY) {
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

static void set_error(DukeArtFile *file, const char *format, ...)
{
    va_list args;

    if (file == NULL) {
        return;
    }
    va_start(args, format);
    vsnprintf(file->last_error, sizeof(file->last_error), format, args);
    va_end(args);
}

static void clear_tiles(DukeArtFile *file)
{
    GList *node;

    if (file == NULL) {
        return;
    }
    for (node = file->tiles; node != NULL; node = node->next) {
        DukeArtTile *tile = node->data;
        free(tile->data);
        free(tile);
    }
    g_list_free(file->tiles);
    file->tiles = NULL;
}

static bool read_i16(DukeArtInput *input, int16_t *value)
{
    uint8_t bytes[2];
    if (input_read(input, bytes, sizeof(bytes)) != sizeof(bytes)) {
        return false;
    }
    *value = (int16_t) ((uint16_t) bytes[0] | ((uint16_t) bytes[1] << 8));
    return true;
}

static bool read_i32(DukeArtInput *input, int32_t *value)
{
    uint8_t bytes[4];
    uint32_t result;
    if (input_read(input, bytes, sizeof(bytes)) != sizeof(bytes)) {
        return false;
    }
    result = (uint32_t) bytes[0] | ((uint32_t) bytes[1] << 8)
        | ((uint32_t) bytes[2] << 16) | ((uint32_t) bytes[3] << 24);
    *value = (int32_t) result;
    return true;
}

static bool write_i16(FILE *fp, int16_t value)
{
    uint16_t raw = (uint16_t) value;
    uint8_t bytes[2] = { (uint8_t) raw, (uint8_t) (raw >> 8) };
    return fwrite(bytes, 1, sizeof(bytes), fp) == sizeof(bytes);
}

static bool write_i32(FILE *fp, int32_t value)
{
    uint32_t raw = (uint32_t) value;
    uint8_t bytes[4] = {
        (uint8_t) raw, (uint8_t) (raw >> 8), (uint8_t) (raw >> 16),
        (uint8_t) (raw >> 24)
    };
    return fwrite(bytes, 1, sizeof(bytes), fp) == sizeof(bytes);
}

static bool tile_size(const DukeArtTile *tile, uint32_t *size)
{
    uint64_t result;
    if (tile == NULL || tile->width < 0 || tile->height < 0) {
        return false;
    }
    result = (uint64_t) tile->width * (uint64_t) tile->height;
    if (result > UINT32_MAX) {
        return false;
    }
    *size = (uint32_t) result;
    return true;
}

DukeArtFile* duke_art_new(void)
{
    DukeArtFile *file = calloc(1, sizeof(*file));
    if (file == NULL) {
        return NULL;
    }
    file->header.artversion = 1;
    duke_art_reset_last_error(file);
    return file;
}

bool duke_art_open_filename(DukeArtFile *file, const char *filename)
{
    DukeArtInput *input;

    if (file == NULL || filename == NULL) {
        return false;
    }
    duke_art_close(file);
    input = calloc(1, sizeof(*input));
    if (input == NULL) {
        set_error(file, "failed to allocate ART input");
        return false;
    }
    input->type = DUKE_ART_INPUT_FILE;
    input->file = fopen(filename, "rb");
    if (input->file == NULL) {
        set_error(file, "failed to open ART file: %s", filename);
        input_free(input);
        return false;
    }
    file->input = input;
    if (!read_i32(file->input, &file->header.artversion)
            || !read_i32(file->input, &file->header.numtiles)
            || !read_i32(file->input, &file->header.localtilestart)
            || !read_i32(file->input, &file->header.localtileend)) {
        set_error(file, "failed to read ART header");
        input_free(file->input);
        file->input = NULL;
        return false;
    }
    return true;
}

bool duke_art_open_memory(DukeArtFile *file, const void *data, size_t size)
{
    DukeArtInput *input;

    if (file == NULL || data == NULL || size < 16) {
        return false;
    }
    duke_art_close(file);
    input = calloc(1, sizeof(*input));
    if (input == NULL) {
        set_error(file, "failed to allocate ART input");
        return false;
    }
    input->data = malloc(size);
    if (input->data == NULL) {
        set_error(file, "failed to copy ART data");
        input_free(input);
        return false;
    }
    input->type = DUKE_ART_INPUT_MEMORY;
    input->size = size;
    memcpy(input->data, data, size);
    file->input = input;
    if (!read_i32(file->input, &file->header.artversion)
            || !read_i32(file->input, &file->header.numtiles)
            || !read_i32(file->input, &file->header.localtilestart)
            || !read_i32(file->input, &file->header.localtileend)) {
        set_error(file, "failed to read ART header");
        input_free(file->input);
        file->input = NULL;
        return false;
    }
    return true;
}

bool duke_art_read_tiles_sparse(DukeArtFile *file)
{
    uint64_t count64;
    uint64_t metadata_size;
    uint64_t data_size = 0;
    uint64_t archive_size;

    if (file == NULL || file->input == NULL) {
        return false;
    }
    duke_art_reset_last_error(file);
    if (file->header.artversion != 1 || file->header.localtilestart < 0
            || file->header.localtileend < file->header.localtilestart) {
        set_error(file, "invalid ART header");
        return false;
    }
    count64 = (uint64_t) ((int64_t) file->header.localtileend
        - file->header.localtilestart) + 1;
    metadata_size = 16 + count64 * 8;
    if (count64 > UINT32_MAX || metadata_size > UINT32_MAX
            || !input_size(file->input, &archive_size)
            || metadata_size > archive_size
            || !input_seek(file->input, 16)) {
        set_error(file, "invalid ART tile range or truncated metadata");
        return false;
    }

    clear_tiles(file);
    for (uint32_t i = 0; i < (uint32_t) count64; i++) {
        DukeArtTile *tile = calloc(1, sizeof(*tile));
        if (tile == NULL || !read_i16(file->input, &tile->width)) {
            free(tile);
            set_error(file, "failed to read tile widths");
            clear_tiles(file);
            return false;
        }
        tile->tile_number = file->header.localtilestart + (int32_t) i;
        file->tiles = g_list_append(file->tiles, tile);
    }
    for (uint32_t i = 0; i < (uint32_t) count64; i++) {
        DukeArtTile *tile = duke_art_get_tile_by_index(file, i);
        if (!read_i16(file->input, &tile->height)) {
            set_error(file, "failed to read tile heights");
            clear_tiles(file);
            return false;
        }
    }
    for (uint32_t i = 0; i < (uint32_t) count64; i++) {
        DukeArtTile *tile = duke_art_get_tile_by_index(file, i);
        int32_t picanm;
        if (!read_i32(file->input, &picanm)) {
            set_error(file, "failed to read tile attributes");
            clear_tiles(file);
            return false;
        }
        tile->picanm = (uint32_t) picanm;
        tile->data_offset = (uint32_t) data_size;
        uint32_t size;
        if (!tile_size(tile, &size)) {
            set_error(file, "tile %d has invalid dimensions", tile->tile_number);
            clear_tiles(file);
            return false;
        }
        data_size += size;
        if (data_size > UINT32_MAX) {
            set_error(file, "ART pixel data is too large");
            clear_tiles(file);
            return false;
        }
    }
    file->data_section_offset = (uint32_t) metadata_size;
    if (metadata_size + data_size > (uint64_t) archive_size) {
        set_error(file, "truncated ART pixel data");
        clear_tiles(file);
        return false;
    }
    return true;
}

bool duke_art_read_tiles_full(DukeArtFile *file)
{
    if (!duke_art_read_tiles_sparse(file)) {
        return false;
    }
    for (uint32_t i = 0; i < g_list_length(file->tiles); i++) {
        void *data;
        if (duke_art_get_tile_data_by_index(file, i, &data) == (size_t) -1) {
            return false;
        }
    }
    return true;
}

bool duke_art_validate(DukeArtFile *file)
{
    uint64_t expected_size;
    uint32_t count;
    uint64_t actual_size;

    if (file == NULL || file->input == NULL || !duke_art_read_tiles_sparse(file)) {
        return false;
    }
    count = g_list_length(file->tiles);
    expected_size = file->data_section_offset;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t size;
        if (!tile_size(duke_art_get_tile_by_index(file, i), &size)) {
            return false;
        }
        expected_size += size;
    }
    if (!input_size(file->input, &actual_size) || expected_size != actual_size) {
        set_error(file, "ART file size does not match its tile metadata");
        return false;
    }
    return true;
}

DukeArtTile* duke_art_get_tile_by_index(DukeArtFile *file, uint32_t index)
{
    return file == NULL ? NULL : g_list_nth_data(file->tiles, index);
}

DukeArtTile* duke_art_get_tile_by_number(DukeArtFile *file, int32_t tile_number)
{
    if (file == NULL || file->tiles == NULL
            || tile_number < file->header.localtilestart
            || tile_number > file->header.localtileend) {
        return NULL;
    }
    return duke_art_get_tile_by_index(file,
        (uint32_t) (tile_number - file->header.localtilestart));
}

size_t duke_art_get_tile_data_by_index(DukeArtFile *file, uint32_t index,
    void **data)
{
    DukeArtTile *tile;
    uint32_t size;

    if (file == NULL || data == NULL
            || (tile = duke_art_get_tile_by_index(file, index)) == NULL
            || !tile_size(tile, &size)) {
        return -1;
    }
    if (tile->data != NULL) {
        *data = tile->data;
        return size;
    }
    if (size == 0) {
        *data = NULL;
        return 0;
    }
    if (file->input == NULL || !input_seek(file->input,
            file->data_section_offset + tile->data_offset)) {
        set_error(file, "failed to seek to tile %d", tile->tile_number);
        return -1;
    }
    tile->data = malloc(size == 0 ? 1 : size);
    if (tile->data == NULL
            || input_read(file->input, tile->data, size) != size) {
        free(tile->data);
        tile->data = NULL;
        set_error(file, "failed to read tile %d", tile->tile_number);
        return -1;
    }
    *data = tile->data;
    return size;
}

size_t duke_art_get_tile_data_by_number(DukeArtFile *file, int32_t tile_number,
    void **data)
{
    if (file == NULL || tile_number < file->header.localtilestart) {
        return -1;
    }
    return duke_art_get_tile_data_by_index(file,
        (uint32_t) (tile_number - file->header.localtilestart), data);
}

static DukeArtTile *empty_tile(int32_t tile_number)
{
    DukeArtTile *tile = calloc(1, sizeof(*tile));
    if (tile != NULL) {
        tile->tile_number = tile_number;
    }
    return tile;
}

bool duke_art_set_tile(DukeArtFile *file, int32_t tile_number, int16_t width,
    int16_t height, uint32_t picanm, const void *data, size_t data_size)
{
    uint64_t expected_size;
    DukeArtTile *tile;
    uint8_t *copy;

    if (file == NULL || tile_number < 0 || width < 0 || height < 0) {
        return false;
    }
    expected_size = (uint64_t) width * (uint64_t) height;
    if (expected_size != data_size || (data_size > 0 && data == NULL)) {
        set_error(file, "tile data size does not match its dimensions");
        return false;
    }
    copy = malloc(data_size == 0 ? 1 : data_size);
    if (copy == NULL) {
        set_error(file, "failed to allocate tile data");
        return false;
    }
    if (data_size > 0) {
        memcpy(copy, data, data_size);
    }

    if (file->tiles == NULL) {
        tile = empty_tile(tile_number);
        if (tile == NULL) {
            free(copy);
            return false;
        }
        file->tiles = g_list_append(NULL, tile);
        file->header.localtilestart = tile_number;
        file->header.localtileend = tile_number;
    } else {
        while (tile_number < file->header.localtilestart) {
            tile = empty_tile(file->header.localtilestart - 1);
            if (tile == NULL) {
                free(copy);
                return false;
            }
            file->tiles = g_list_prepend(file->tiles, tile);
            file->header.localtilestart--;
        }
        while (tile_number > file->header.localtileend) {
            tile = empty_tile(file->header.localtileend + 1);
            if (tile == NULL) {
                free(copy);
                return false;
            }
            file->tiles = g_list_append(file->tiles, tile);
            file->header.localtileend++;
        }
        tile = duke_art_get_tile_by_number(file, tile_number);
    }

    free(tile->data);
    tile->width = width;
    tile->height = height;
    tile->picanm = picanm;
    tile->data = copy;
    if (file->header.numtiles <= tile_number) {
        file->header.numtiles = tile_number == INT32_MAX
            ? INT32_MAX : tile_number + 1;
    }
    duke_art_reset_last_error(file);
    return true;
}

bool duke_art_clear_tile(DukeArtFile *file, int32_t tile_number)
{
    DukeArtTile *tile = duke_art_get_tile_by_number(file, tile_number);
    if (tile == NULL) {
        return false;
    }
    free(tile->data);
    tile->data = NULL;
    tile->width = 0;
    tile->height = 0;
    tile->picanm = 0;
    return true;
}

bool duke_art_write_filename(DukeArtFile *file, const char *filename)
{
    uint32_t count;
    FILE *output;
    bool ok = false;

    if (file == NULL || filename == NULL || file->tiles == NULL) {
        return false;
    }
    count = g_list_length(file->tiles);
    if ((uint64_t) file->header.localtileend - file->header.localtilestart + 1
            != count) {
        set_error(file, "tile list does not match the ART tile range");
        return false;
    }
    for (uint32_t i = 0; i < count; i++) {
        void *data;
        if (duke_art_get_tile_data_by_index(file, i, &data) == (size_t) -1) {
            return false;
        }
    }

    output = fopen(filename, "wb");
    if (output == NULL) {
        set_error(file, "failed to open ART output: %s", filename);
        return false;
    }
    if (!write_i32(output, 1) || !write_i32(output, file->header.numtiles)
            || !write_i32(output, file->header.localtilestart)
            || !write_i32(output, file->header.localtileend)) {
        goto cleanup;
    }
    for (uint32_t i = 0; i < count; i++) {
        if (!write_i16(output, duke_art_get_tile_by_index(file, i)->width)) {
            goto cleanup;
        }
    }
    for (uint32_t i = 0; i < count; i++) {
        if (!write_i16(output, duke_art_get_tile_by_index(file, i)->height)) {
            goto cleanup;
        }
    }
    for (uint32_t i = 0; i < count; i++) {
        if (!write_i32(output,
                (int32_t) duke_art_get_tile_by_index(file, i)->picanm)) {
            goto cleanup;
        }
    }
    for (uint32_t i = 0; i < count; i++) {
        DukeArtTile *tile = duke_art_get_tile_by_index(file, i);
        uint32_t size;
        tile_size(tile, &size);
        if (fwrite(tile->data, 1, size, output) != size) {
            goto cleanup;
        }
    }
    ok = true;

cleanup:
    if (fclose(output) != 0) {
        ok = false;
    }
    if (!ok) {
        set_error(file, "failed to write ART file: %s", filename);
    }
    return ok;
}

void duke_art_reset_last_error(DukeArtFile *file)
{
    if (file != NULL) {
        file->last_error[0] = '\0';
    }
}

bool duke_art_close(DukeArtFile *file)
{
    if (file == NULL) {
        return false;
    }
    input_free(file->input);
    file->input = NULL;
    clear_tiles(file);
    return true;
}

void duke_art_free(DukeArtFile *file)
{
    if (file != NULL) {
        duke_art_close(file);
        free(file);
    }
}

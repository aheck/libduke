#ifndef LIBDUKE_ART_H
#define LIBDUKE_ART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DukeArtHeader {
    int32_t artversion;
    int32_t numtiles;
    int32_t localtilestart;
    int32_t localtileend;
} DukeArtHeader;

typedef struct DukeArtTile {
    int32_t tile_number;
    int16_t width;
    int16_t height;
    uint32_t picanm;
    uint32_t data_offset;
    uint8_t *data;
} DukeArtTile;

struct GList;

typedef struct DukeArtFile {
    FILE *fp;
    uint32_t data_section_offset;
    DukeArtHeader header;
    struct GList *tiles;
    char last_error[256];
} DukeArtFile;

DukeArtFile* duke_art_new(void);
bool duke_art_open_filename(DukeArtFile *file, const char *filename);
bool duke_art_read_tiles_sparse(DukeArtFile *file);
bool duke_art_read_tiles_full(DukeArtFile *file);
bool duke_art_validate(DukeArtFile *file);
DukeArtTile* duke_art_get_tile_by_index(DukeArtFile *file, uint32_t index);
DukeArtTile* duke_art_get_tile_by_number(DukeArtFile *file, int32_t tile_number);
size_t duke_art_get_tile_data_by_index(DukeArtFile *file, uint32_t index,
    void **data);
size_t duke_art_get_tile_data_by_number(DukeArtFile *file, int32_t tile_number,
    void **data);
bool duke_art_set_tile(DukeArtFile *file, int32_t tile_number, int16_t width,
    int16_t height, uint32_t picanm, const void *data, size_t data_size);
bool duke_art_clear_tile(DukeArtFile *file, int32_t tile_number);
bool duke_art_write_filename(DukeArtFile *file, const char *filename);
void duke_art_reset_last_error(DukeArtFile *file);
bool duke_art_close(DukeArtFile *file);
void duke_art_free(DukeArtFile *file);

#ifdef __cplusplus
}
#endif

#endif /* LIBDUKE_ART_H */

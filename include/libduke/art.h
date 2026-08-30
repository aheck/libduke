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

/**
 * @brief Allocate an empty version 1 ART file object.
 *
 * @return A newly allocated object owned by the caller, or `NULL` if allocation
 * fails. Release the returned object with duke_art_free().
 */
DukeArtFile* duke_art_new(void);

/**
 * @brief Open an ART file and read its 16-byte header.
 *
 * Any file currently associated with @p file is closed first. Tile metadata and
 * pixel data are not read until one of the tile-reading functions is called.
 *
 * @param file Destination object created by duke_art_new().
 * @param filename Path to the ART file to open.
 * @return `true` if the file was opened and its complete header was read;
 * `false` otherwise, with details in `file->last_error` when available.
 */
bool duke_art_open_filename(DukeArtFile *file, const char *filename);

/**
 * @brief Read tile metadata without loading pixel data.
 *
 * Existing tile metadata and cached pixels are discarded once loading begins.
 * Each nonempty tile's `data` pointer remains `NULL` until its pixels are
 * requested.
 *
 * @param file Open ART file whose tile table should be read.
 * @return `true` if the header, tile range, metadata, and data extents are
 * valid; `false` otherwise, with details in `file->last_error`.
 */
bool duke_art_read_tiles_sparse(DukeArtFile *file);

/**
 * @brief Read tile metadata and cache every tile's pixel data.
 *
 * @param file Open ART file to load.
 * @return `true` if all metadata and pixel data were read successfully;
 * `false` otherwise, with details in `file->last_error`.
 */
bool duke_art_read_tiles_full(DukeArtFile *file);

/**
 * @brief Validate the ART metadata, pixel extents, and exact file size.
 *
 * Validation rereads the sparse tile table, discarding existing tile objects
 * and cached pixel data.
 *
 * @param file Open ART file to validate.
 * @return `true` if the complete file is structurally valid; `false` otherwise,
 * with details in `file->last_error` when available.
 */
bool duke_art_validate(DukeArtFile *file);

/**
 * @brief Retrieve tile metadata by its zero-based position in the local range.
 *
 * @param file ART object whose tile list should be searched.
 * @param index Zero-based tile-list index.
 * @return A library-owned tile pointer, or `NULL` if @p file is `NULL` or the
 * index does not exist. The pointer is invalidated when tiles are reloaded, the
 * file is closed, or the object is freed.
 */
DukeArtTile* duke_art_get_tile_by_index(DukeArtFile *file, uint32_t index);

/**
 * @brief Retrieve tile metadata by its absolute tile number.
 *
 * @param file ART object whose tile list should be searched.
 * @param tile_number Absolute tile number within the file's local tile range.
 * @return A library-owned tile pointer, or `NULL` if the tile is unavailable.
 * The pointer is invalidated when tiles are reloaded, the file is closed, or
 * the object is freed.
 */
DukeArtTile* duke_art_get_tile_by_number(DukeArtFile *file, int32_t tile_number);

/**
 * @brief Retrieve and, if necessary, load a tile's pixels by list index.
 *
 * @param file ART object with a parsed tile table.
 * @param index Zero-based tile-list index.
 * @param data Receives a library-owned pixel buffer, or `NULL` for an empty
 * tile. A non-`NULL` buffer remains valid until the tile is changed or cleared,
 * tiles are reloaded, the file is closed, or the object is freed.
 * @return The pixel-data size (`width * height`) in bytes, including zero for
 * an empty tile, or `(size_t)-1` if the tile is invalid or cannot be read.
 */
size_t duke_art_get_tile_data_by_index(DukeArtFile *file, uint32_t index,
    void **data);

/**
 * @brief Retrieve and, if necessary, load a tile's pixels by tile number.
 *
 * @param file ART object with a parsed tile table.
 * @param tile_number Absolute tile number to retrieve.
 * @param data Receives a library-owned pixel buffer, or `NULL` for an empty
 * tile. Its lifetime matches that described by
 * duke_art_get_tile_data_by_index().
 * @return The pixel-data size in bytes, including zero for an empty tile, or
 * `(size_t)-1` if the tile is unavailable or cannot be read.
 */
size_t duke_art_get_tile_data_by_number(DukeArtFile *file, int32_t tile_number,
    void **data);

/**
 * @brief Add or replace a tile, copying the supplied pixel data.
 *
 * The tile range is expanded as needed; gaps are represented by empty tiles.
 * The input buffer may be released or reused after this function returns.
 *
 * @param file ART object to modify.
 * @param tile_number Nonnegative absolute tile number to set.
 * @param width Nonnegative tile width in pixels.
 * @param height Nonnegative tile height in pixels.
 * @param picanm Raw Build tile animation and attribute bits.
 * @param data Pixel data to copy, or `NULL` when @p data_size is zero.
 * @param data_size Size of @p data; must equal `width * height`.
 * @return `true` if the tile was stored; `false` for invalid arguments or an
 * allocation failure, with details in `file->last_error` when available.
 */
bool duke_art_set_tile(DukeArtFile *file, int32_t tile_number, int16_t width,
    int16_t height, uint32_t picanm, const void *data, size_t data_size);

/**
 * @brief Replace a tile with an empty, zero-dimension tile.
 *
 * The tile remains in the local tile range, but its pixels and attributes are
 * discarded.
 *
 * @param file ART object to modify.
 * @param tile_number Absolute number of the tile to clear.
 * @return `true` if the tile exists and was cleared; `false` otherwise.
 */
bool duke_art_clear_tile(DukeArtFile *file, int32_t tile_number);

/**
 * @brief Write the current tile range as a version 1 ART file.
 *
 * Lazily loaded pixels are read from the source file before output begins. An
 * existing file at @p filename is replaced.
 *
 * @param file ART object containing at least one tile.
 * @param filename Destination path to create or overwrite.
 * @return `true` if the entire file was written and closed successfully;
 * `false` otherwise, with details in `file->last_error` when available.
 */
bool duke_art_write_filename(DukeArtFile *file, const char *filename);

/**
 * @brief Clear the ART object's most recent error message.
 *
 * @param file Object whose `last_error` buffer should be emptied. May be
 * `NULL`.
 */
void duke_art_reset_last_error(DukeArtFile *file);

/**
 * @brief Close the source file and discard all tiles and cached pixels.
 *
 * The DukeArtFile object remains allocated and may be reused by calling
 * duke_art_open_filename() or populated with duke_art_set_tile().
 *
 * @param file ART object to close.
 * @return `true` if @p file is non-`NULL`; `false` otherwise. Calling this on
 * an already closed object succeeds.
 */
bool duke_art_close(DukeArtFile *file);

/**
 * @brief Close and free an ART object and all resources it owns.
 *
 * @param file ART object to destroy. May be `NULL`.
 */
void duke_art_free(DukeArtFile *file);

#ifdef __cplusplus
}
#endif

#endif /* LIBDUKE_ART_H */

#ifndef LIBDUKE_PALETTE_H
#define LIBDUKE_PALETTE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DUKE_PALETTE_COLOR_COUNT 256
#define DUKE_PALETTE_TRANSLUCENCY_SIZE 65536
#define DUKE_PALETTE_MAX_CHANNEL 63

typedef struct DukePaletteColor {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} DukePaletteColor;

typedef struct DukePaletteFile {
    DukePaletteColor colors[DUKE_PALETTE_COLOR_COUNT];
    uint16_t num_shades;
    uint8_t *shade_tables;
    uint8_t *translucency_table;
    char last_error[256];
} DukePaletteFile;

/**
 * @brief Allocate an empty Build PALETTE.DAT object.
 *
 * The base colors and translucency table are initialized to zero and no shade
 * tables are present.
 *
 * @return A newly allocated palette owned by the caller, or `NULL` if
 * allocation fails. Release it with duke_palette_free().
 */
DukePaletteFile* duke_palette_new(void);

/**
 * @brief Read a complete Build PALETTE.DAT file from disk.
 *
 * The file must contain the 768-byte VGA palette, little-endian shade count,
 * every 256-byte shade lookup table, and the 65,536-byte translucency table,
 * with no trailing data. On failure, the existing palette contents remain
 * unchanged.
 *
 * @param palette Destination palette created by duke_palette_new().
 * @param filename Path to PALETTE.DAT.
 * @return `true` on success; `false` on malformed data, I/O failure, or an
 * allocation failure, with details in `palette->last_error`.
 */
bool duke_palette_read_from_filename(DukePaletteFile *palette,
    const char *filename);

/**
 * @brief Read a complete Build PALETTE.DAT file from memory.
 *
 * Parsing is completed during this call and the input is internally copied,
 * so the caller may release or reuse @p data after the function returns. On
 * failure, the existing palette contents remain unchanged.
 *
 * @param palette Destination palette created by duke_palette_new().
 * @param data Complete PALETTE.DAT contents.
 * @param size Size of @p data in bytes.
 * @return `true` on success; `false` on malformed data or allocation failure,
 * with details in `palette->last_error`.
 */
bool duke_palette_read_from_memory(DukePaletteFile *palette, const void *data,
    size_t size);

/**
 * @brief Write a complete Build PALETTE.DAT file.
 *
 * The palette is validated before the destination is opened.
 *
 * @param palette Palette to write.
 * @param filename Destination path to create or replace.
 * @return `true` if the complete file was written; `false` otherwise, with
 * details in `palette->last_error`.
 */
bool duke_palette_write_to_filename(DukePaletteFile *palette,
    const char *filename);

/**
 * @brief Validate the palette representation and VGA channel ranges.
 *
 * @param palette Palette to validate.
 * @return `true` when all required tables exist and every base color channel
 * is in the VGA range 0 through 63; `false` otherwise.
 */
bool duke_palette_validate(DukePaletteFile *palette);

/**
 * @brief Map a palette index through one shade table.
 *
 * @param palette Palette containing the shade tables.
 * @param shade Zero-based shade-table index.
 * @param color Original palette index.
 * @param result Receives the mapped palette index.
 * @return `true` if @p shade exists and @p result is non-`NULL`; `false`
 * otherwise.
 */
bool duke_palette_get_shaded_index(const DukePaletteFile *palette,
    uint16_t shade, uint8_t color, uint8_t *result);

/**
 * @brief Map two palette indices through the translucency table.
 *
 * @param palette Palette containing the translucency table.
 * @param first First palette index, used as the high table byte.
 * @param second Second palette index, used as the low table byte.
 * @param result Receives the blended palette index.
 * @return `true` when the table and @p result exist; `false` otherwise.
 */
bool duke_palette_get_translucent_index(const DukePaletteFile *palette,
    uint8_t first, uint8_t second, uint8_t *result);

/**
 * @brief Clear the palette's most recent error message.
 *
 * @param palette Palette whose error buffer should be cleared. May be `NULL`.
 */
void duke_palette_reset_last_error(DukePaletteFile *palette);

/**
 * @brief Free a palette and all lookup tables it owns.
 *
 * @param palette Palette to destroy. May be `NULL`.
 */
void duke_palette_free(DukePaletteFile *palette);

#ifdef __cplusplus
}
#endif

#endif /* LIBDUKE_PALETTE_H */

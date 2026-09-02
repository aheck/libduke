#include "libduke/palette.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "input.h"

static void set_error(DukePaletteFile *palette, const char *format, ...)
{
    va_list args;

    if (palette == NULL) {
        return;
    }
    va_start(args, format);
    vsnprintf(palette->last_error, sizeof(palette->last_error), format, args);
    va_end(args);
}

static bool read_palette(DukePaletteFile *palette, DukeInput *input)
{
    DukePaletteColor colors[DUKE_PALETTE_COLOR_COUNT];
    uint8_t color_data[DUKE_PALETTE_COLOR_COUNT * 3];
    uint8_t shade_count[2];
    uint8_t *shade_tables = NULL;
    uint8_t *translucency_table = NULL;
    uint16_t num_shades;
    size_t shade_size;
    uint64_t input_length;
    uint64_t expected_length;

    if (!duke_input_size(input, &input_length)
            || !duke_input_seek(input, 0)
            || duke_input_read(input, color_data, sizeof(color_data))
                != sizeof(color_data)
            || duke_input_read(input, shade_count, sizeof(shade_count))
                != sizeof(shade_count)) {
        set_error(palette, "failed to read palette header");
        return false;
    }
    num_shades = (uint16_t) shade_count[0]
        | ((uint16_t) shade_count[1] << 8);
    shade_size = (size_t) num_shades * DUKE_PALETTE_COLOR_COUNT;
    expected_length = sizeof(color_data) + sizeof(shade_count) + shade_size
        + DUKE_PALETTE_TRANSLUCENCY_SIZE;
    if (input_length != expected_length) {
        set_error(palette, "palette file size does not match its shade count");
        return false;
    }
    for (size_t index = 0; index < DUKE_PALETTE_COLOR_COUNT; ++index) {
        colors[index].red = color_data[index * 3];
        colors[index].green = color_data[index * 3 + 1];
        colors[index].blue = color_data[index * 3 + 2];
        if (colors[index].red > DUKE_PALETTE_MAX_CHANNEL
                || colors[index].green > DUKE_PALETTE_MAX_CHANNEL
                || colors[index].blue > DUKE_PALETTE_MAX_CHANNEL) {
            set_error(palette, "palette color %zu exceeds the VGA channel range",
                index);
            return false;
        }
    }
    if (shade_size > 0) {
        shade_tables = malloc(shade_size);
        if (shade_tables == NULL
                || duke_input_read(input, shade_tables, shade_size)
                    != shade_size) {
            set_error(palette, "failed to read shade tables");
            free(shade_tables);
            return false;
        }
    }
    translucency_table = malloc(DUKE_PALETTE_TRANSLUCENCY_SIZE);
    if (translucency_table == NULL
            || duke_input_read(input, translucency_table,
                DUKE_PALETTE_TRANSLUCENCY_SIZE)
                != DUKE_PALETTE_TRANSLUCENCY_SIZE) {
        set_error(palette, "failed to read translucency table");
        free(shade_tables);
        free(translucency_table);
        return false;
    }

    free(palette->shade_tables);
    free(palette->translucency_table);
    memcpy(palette->colors, colors, sizeof(colors));
    palette->num_shades = num_shades;
    palette->shade_tables = shade_tables;
    palette->translucency_table = translucency_table;
    duke_palette_reset_last_error(palette);
    return true;
}

DukePaletteFile* duke_palette_new(void)
{
    DukePaletteFile *palette = calloc(1, sizeof(*palette));

    if (palette == NULL) {
        return NULL;
    }
    palette->translucency_table = calloc(1, DUKE_PALETTE_TRANSLUCENCY_SIZE);
    if (palette->translucency_table == NULL) {
        free(palette);
        return NULL;
    }
    return palette;
}

bool duke_palette_read_from_filename(DukePaletteFile *palette,
    const char *filename)
{
    DukeInput *input;
    bool result;

    if (palette == NULL || filename == NULL) {
        return false;
    }
    input = duke_input_new_filename(filename);
    if (input == NULL) {
        set_error(palette, "failed to open palette file: %s", filename);
        return false;
    }
    result = read_palette(palette, input);
    duke_input_free(input);
    return result;
}

bool duke_palette_read_from_memory(DukePaletteFile *palette, const void *data,
    size_t size)
{
    DukeInput *input;
    bool result;

    if (palette == NULL || (data == NULL && size > 0)) {
        return false;
    }
    input = duke_input_new_memory(data, size);
    if (input == NULL) {
        set_error(palette, "failed to copy palette data");
        return false;
    }
    result = read_palette(palette, input);
    duke_input_free(input);
    return result;
}

bool duke_palette_validate(DukePaletteFile *palette)
{
    if (palette == NULL) {
        return false;
    }
    duke_palette_reset_last_error(palette);
    if (palette->num_shades > 0 && palette->shade_tables == NULL) {
        set_error(palette, "shade tables are missing");
        return false;
    }
    if (palette->translucency_table == NULL) {
        set_error(palette, "translucency table is missing");
        return false;
    }
    for (size_t index = 0; index < DUKE_PALETTE_COLOR_COUNT; ++index) {
        if (palette->colors[index].red > DUKE_PALETTE_MAX_CHANNEL
                || palette->colors[index].green > DUKE_PALETTE_MAX_CHANNEL
                || palette->colors[index].blue > DUKE_PALETTE_MAX_CHANNEL) {
            set_error(palette, "palette color %zu exceeds the VGA channel range",
                index);
            return false;
        }
    }
    return true;
}

bool duke_palette_write_to_filename(DukePaletteFile *palette,
    const char *filename)
{
    FILE *output;
    uint8_t color_data[DUKE_PALETTE_COLOR_COUNT * 3];
    size_t shade_size;
    uint8_t shade_count[2];
    bool result = false;

    if (palette == NULL || filename == NULL || !duke_palette_validate(palette)) {
        return false;
    }
    output = fopen(filename, "wb");
    if (output == NULL) {
        set_error(palette, "failed to open palette output: %s", filename);
        return false;
    }
    shade_size = (size_t) palette->num_shades * DUKE_PALETTE_COLOR_COUNT;
    shade_count[0] = (uint8_t) palette->num_shades;
    shade_count[1] = (uint8_t) (palette->num_shades >> 8);
    for (size_t index = 0; index < DUKE_PALETTE_COLOR_COUNT; ++index) {
        color_data[index * 3] = palette->colors[index].red;
        color_data[index * 3 + 1] = palette->colors[index].green;
        color_data[index * 3 + 2] = palette->colors[index].blue;
    }
    if (fwrite(color_data, 1, sizeof(color_data), output)
                == sizeof(color_data)
            && fwrite(shade_count, 1, sizeof(shade_count), output)
                == sizeof(shade_count)
            && (shade_size == 0
                || fwrite(palette->shade_tables, 1, shade_size, output)
                    == shade_size)
            && fwrite(palette->translucency_table, 1,
                DUKE_PALETTE_TRANSLUCENCY_SIZE, output)
                == DUKE_PALETTE_TRANSLUCENCY_SIZE) {
        result = true;
    }
    if (fclose(output) != 0) {
        result = false;
    }
    if (!result) {
        set_error(palette, "failed to write palette file: %s", filename);
    }
    return result;
}

bool duke_palette_get_shaded_index(const DukePaletteFile *palette,
    uint16_t shade, uint8_t color, uint8_t *result)
{
    if (palette == NULL || result == NULL || shade >= palette->num_shades
            || palette->shade_tables == NULL) {
        return false;
    }
    *result = palette->shade_tables[(size_t) shade
        * DUKE_PALETTE_COLOR_COUNT + color];
    return true;
}

bool duke_palette_get_translucent_index(const DukePaletteFile *palette,
    uint8_t first, uint8_t second, uint8_t *result)
{
    if (palette == NULL || result == NULL
            || palette->translucency_table == NULL) {
        return false;
    }
    *result = palette->translucency_table[(size_t) first * 256 + second];
    return true;
}

void duke_palette_reset_last_error(DukePaletteFile *palette)
{
    if (palette != NULL) {
        palette->last_error[0] = '\0';
    }
}

void duke_palette_free(DukePaletteFile *palette)
{
    if (palette == NULL) {
        return;
    }
    free(palette->shade_tables);
    free(palette->translucency_table);
    free(palette);
}

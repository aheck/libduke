#include "libduke/palette_lookup.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

DukePaletteLookupFile *duke_palette_lookup_new(void) { return calloc(1, sizeof(DukePaletteLookupFile)); }
static void clear(DukePaletteLookupFile *f) { free(f->palettes); free(f->alternate_palettes); f->palettes = NULL; f->alternate_palettes = NULL; f->count = 0; f->alternate_count = 0; }
bool duke_palette_lookup_read_from_memory(DukePaletteLookupFile *f, const void *data, size_t size)
{
    const uint8_t *p = data; uint8_t count; size_t records, rest, alts, off = 1;
    DukePaletteLookup *palettes = NULL; DukePaletteColor *alternate = NULL;
    if (!f || !p || size < 1) return false;
    count = p[0]; records = (size_t)count * 257;
    if (records > size - 1) return false;
    rest = size - 1 - records;
    if (rest % (256 * 3)) return false;
    alts = rest / (256 * 3);
    if (count && !(palettes = malloc((size_t)count * sizeof(*palettes)))) return false;
    for (size_t i = 0; i < count; i++) { palettes[i].id = p[off++]; memcpy(palettes[i].colors, p + off, 256); off += 256; }
    if (alts && !(alternate = malloc(alts * 256 * sizeof(*alternate)))) { free(palettes); return false; }
    for (size_t i = 0; i < alts * 256; i++) { alternate[i].red = p[off++]; alternate[i].green = p[off++]; alternate[i].blue = p[off++]; }
    clear(f); f->count = count; f->palettes = palettes; f->alternate_count = alts; f->alternate_palettes = alternate; return true;
}
bool duke_palette_lookup_read_from_filename(DukePaletteLookupFile *f, const char *name)
{
    FILE *in = NULL; long length; uint8_t *data; size_t size; bool ok;
    if (!f || !name || !(in = fopen(name, "rb")) || fseek(in, 0, SEEK_END) || (length = ftell(in)) < 1 || fseek(in, 0, SEEK_SET)) { if (in) fclose(in); return false; }
    size = (size_t)length; data = malloc(size); ok = data && fread(data, 1, size, in) == size && duke_palette_lookup_read_from_memory(f, data, size); free(data); fclose(in); return ok;
}
bool duke_palette_lookup_get_index(const DukePaletteLookupFile *f, uint8_t id, uint8_t color, uint8_t *result)
{
    if (!result) return false; if (!id) { *result = color; return true; } if (!f) return false;
    for (size_t i = 0; i < f->count; i++) if (f->palettes[i].id == id) { *result = f->palettes[i].colors[color]; return true; }
    return false;
}
void duke_palette_lookup_free(DukePaletteLookupFile *f) { if (f) { clear(f); free(f); } }

#ifndef LIBDUKE_PALETTE_LOOKUP_H
#define LIBDUKE_PALETTE_LOOKUP_H
#include "palette.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct DukePaletteLookup {
    uint8_t id;
    uint8_t colors[DUKE_PALETTE_COLOR_COUNT];
} DukePaletteLookup;
typedef struct DukePaletteLookupFile {
    uint8_t count;
    DukePaletteLookup *palettes;
    size_t alternate_count;
    DukePaletteColor *alternate_palettes;
} DukePaletteLookupFile;
DukePaletteLookupFile *duke_palette_lookup_new(void);
bool duke_palette_lookup_read_from_filename(DukePaletteLookupFile *, const char *);
bool duke_palette_lookup_read_from_memory(DukePaletteLookupFile *, const void *, size_t);
bool duke_palette_lookup_get_index(const DukePaletteLookupFile *, uint8_t, uint8_t, uint8_t *);
void duke_palette_lookup_free(DukePaletteLookupFile *);
#ifdef __cplusplus
}
#endif
#endif

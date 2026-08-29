#ifndef LIBDUKE_GRP_H
#define LIBDUKE_GRP_H

/*
 * 
 */

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DukeGrpHeader {
    char magic[12];      // Magic number. Should be "KenSilverman"
    uint32_t entry_count; // Number of files in this GRP file
} DukeGrpHeader;

typedef struct DukeGrpFileEntry {
    char filename[13]; // 12 bytes on disk and an additional byte for null-termination
    uint32_t filesize; // Size of this file in bytes
    uint32_t data_offset; // Offset from the data_section_offset where the data of this file begins
    uint8_t *data; // A pointer to the data of this file or NULL if it hasn't been fetched from disk, yet.
} DukeGrpFileEntry;

struct GList;

typedef struct DukeGrpFile {
    FILE *fp;
    uint32_t data_section_offset; // Offset from the start of the file where the data section begins
    DukeGrpHeader header;
    struct GList *entries;
    char last_error[256];
} DukeGrpFile;

DukeGrpFile* duke_grp_new(void);
bool duke_grp_open_filename(DukeGrpFile *file, const char *filename);
bool duke_grp_read_entries_sparse(DukeGrpFile *file);
bool duke_grp_read_entries_full(DukeGrpFile *file);
DukeGrpFileEntry* duke_grp_get_entry_by_index(DukeGrpFile *file, uint32_t index);
size_t duke_grp_get_file_data_by_index(DukeGrpFile *file, uint32_t index, void **data);
size_t duke_grp_get_file_data_by_filename(DukeGrpFile *file, const char *filename, void **data);
void duke_grp_reset_last_error(DukeGrpFile *file);
bool duke_grp_close(DukeGrpFile *file);
void duke_grp_free(DukeGrpFile *file);

#ifdef __cplusplus
}
#endif

#endif /* LIBDUKE_GRP_H */

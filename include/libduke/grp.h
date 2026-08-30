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

/**
 * @brief Allocate an empty GRP archive object.
 *
 * @return A newly allocated archive owned by the caller, or `NULL` if
 * allocation fails. Release the returned object with duke_grp_free().
 */
DukeGrpFile* duke_grp_new(void);

/**
 * @brief Open a GRP archive and read its 16-byte header.
 *
 * Any archive currently associated with @p file is closed first. Entry metadata
 * and file contents are not read until one of the entry-reading functions is
 * called.
 *
 * @param file Destination archive object created by duke_grp_new().
 * @param filename Path to the GRP archive to open.
 * @return `true` if the archive was opened and its complete header was read;
 * `false` otherwise. On failure, @p file is left closed.
 */
bool duke_grp_open_filename(DukeGrpFile *file, const char *filename);

/**
 * @brief Read the archive directory without loading entry contents.
 *
 * Existing entry metadata and cached contents are discarded once directory
 * loading begins. Each resulting DukeGrpFileEntry has a `NULL` `data` pointer
 * until its contents are requested.
 *
 * @param file Open archive whose directory should be read.
 * @return `true` if all directory entries were read and their offsets are
 * valid; `false` for an invalid archive, I/O error, or allocation failure.
 */
bool duke_grp_read_entries_sparse(DukeGrpFile *file);

/**
 * @brief Read the archive directory and cache every entry's contents.
 *
 * @param file Open archive to load.
 * @return `true` if all metadata and entry data were read successfully;
 * `false` otherwise.
 */
bool duke_grp_read_entries_full(DukeGrpFile *file);

/**
 * @brief Retrieve entry metadata by its zero-based directory index.
 *
 * @param file Archive whose parsed directory should be searched.
 * @param index Zero-based entry index.
 * @return A library-owned entry pointer, or `NULL` if @p file is `NULL` or the
 * index does not exist. The pointer is invalidated when entries are reloaded,
 * the archive is closed, or the archive object is freed.
 */
DukeGrpFileEntry* duke_grp_get_entry_by_index(DukeGrpFile *file, uint32_t index);

/**
 * @brief Retrieve and, if necessary, load an entry's contents by index.
 *
 * @param file Open archive with a parsed directory.
 * @param index Zero-based index of the entry to retrieve.
 * @param data Receives a library-owned pointer to the cached entry contents.
 * The pointer remains valid until entries are reloaded, the archive is closed,
 * or the archive object is freed.
 * @return The entry size in bytes, including zero for an empty entry, or
 * `(size_t)-1` if the arguments or index are invalid or the data cannot be read.
 */
size_t duke_grp_get_file_data_by_index(DukeGrpFile *file, uint32_t index,
    void **data);

/**
 * @brief Retrieve and, if necessary, load an entry's contents by filename.
 *
 * Filename matching is exact and case-sensitive.
 *
 * @param file Open archive with a parsed directory.
 * @param filename Null-terminated entry name to find.
 * @param data Receives a library-owned pointer to the cached entry contents.
 * The pointer remains valid until entries are reloaded, the archive is closed,
 * or the archive object is freed.
 * @return The entry size in bytes, including zero for an empty entry, or
 * `(size_t)-1` if no matching entry exists or the data cannot be read.
 */
size_t duke_grp_get_file_data_by_filename(DukeGrpFile *file,
    const char *filename, void **data);

/**
 * @brief Clear the archive's most recent error message.
 *
 * @param file Archive whose `last_error` buffer should be emptied. May be
 * `NULL`.
 */
void duke_grp_reset_last_error(DukeGrpFile *file);

/**
 * @brief Close the archive and discard all entry metadata and cached contents.
 *
 * The DukeGrpFile object remains allocated and may be reused by calling
 * duke_grp_open_filename().
 *
 * @param file Archive to close.
 * @return `true` if @p file is non-`NULL`; `false` otherwise. Calling this on
 * an already closed archive succeeds.
 */
bool duke_grp_close(DukeGrpFile *file);

/**
 * @brief Close and free an archive object and all resources it owns.
 *
 * @param file Archive to destroy. May be `NULL`.
 */
void duke_grp_free(DukeGrpFile *file);

#ifdef __cplusplus
}
#endif

#endif /* LIBDUKE_GRP_H */

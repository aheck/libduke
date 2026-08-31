#include "libduke/grp.h"

#include <stdlib.h>
#include <string.h>

#include "glist.h"

static uint32_t read_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0]
        | ((uint32_t)data[1] << 8)
        | ((uint32_t)data[2] << 16)
        | ((uint32_t)data[3] << 24);
}

static void duke_grp_clear_entries(DukeGrpFile *file)
{
    GList *node;

    if (file == NULL) {
        return;
    }

    for (node = file->entries; node != NULL; node = node->next) {
        DukeGrpFileEntry *entry = node->data;

        free(entry->data);
        free(entry);
    }

    g_list_free(file->entries);
    file->entries = NULL;
}

DukeGrpFile* duke_grp_new(void)
{
    DukeGrpFile *file;

    file = malloc(sizeof(DukeGrpFile));
    if (file == NULL) {
        return NULL;
    }

    file->fp = NULL;
    file->data_section_offset = 0;
    file->header.magic[0] = '\0';
    file->header.entry_count = 0;
    file->entries = NULL;
    duke_grp_reset_last_error(file);

    return file;
}

bool duke_grp_open_filename(DukeGrpFile *file, const char *filename)
{
    uint8_t count_data[4];
    size_t num_bytes;

    if (file == NULL) {
        return false;
    }

    duke_grp_close(file);
    file->fp = fopen(filename, "rb");
    if (file->fp == NULL) {
        return false;
    }

    // read header
    num_bytes = fread(&file->header.magic, 1, 12, file->fp);
    if (num_bytes != 12) {
        duke_grp_close(file);
        return false;
    }

    if (fread(count_data, 1, sizeof(count_data), file->fp)
            != sizeof(count_data)) {
        duke_grp_close(file);
        return false;
    }
    file->header.entry_count = read_u32_le(count_data);

    return true;
}

bool duke_grp_read_entries_sparse(DukeGrpFile *file)
{
    uint8_t size_data[4];
    size_t num_bytes;
    long archive_size;

    if (file == NULL || file->fp == NULL || fseek(file->fp, 0, SEEK_END) != 0
            || (archive_size = ftell(file->fp)) < 16
            || file->header.entry_count > ((uint64_t) archive_size - 16) / 16
            || fseek(file->fp, 16, SEEK_SET) != 0) {
        return false;
    }

    duke_grp_clear_entries(file);

    uint32_t file_offset = 0;

    for (uint32_t i = 0; i < file->header.entry_count; i++) {
        DukeGrpFileEntry *entry = calloc(1, sizeof(DukeGrpFileEntry));
        if (entry == NULL) {
            duke_grp_clear_entries(file);
            return false;
        }

        entry->data_offset = file_offset;
        num_bytes = fread(entry->filename, 1, 12, file->fp);
        if (num_bytes != 12) {
            free(entry);
            duke_grp_clear_entries(file);
            return false;
        }
        entry->filename[12] = '\0';

        if (fread(size_data, 1, sizeof(size_data), file->fp)
                != sizeof(size_data)) {
            free(entry);
            duke_grp_clear_entries(file);
            return false;
        }
        entry->filesize = read_u32_le(size_data);

        if ((uint64_t) file_offset + entry->filesize > UINT32_MAX) {
            free(entry);
            duke_grp_clear_entries(file);
            return false;
        }

        file->entries = g_list_append(file->entries, entry);
        file_offset += entry->filesize;
    }

    long data_section_offset = ftell(file->fp);
    if (data_section_offset < 0 || (uint64_t) data_section_offset > UINT32_MAX) {
        duke_grp_clear_entries(file);
        return false;
    }
    file->data_section_offset = (uint32_t) data_section_offset;
    return true;
}

bool duke_grp_read_entries_full(DukeGrpFile *file)
{
    if (file == NULL || !duke_grp_read_entries_sparse(file)) {
        return false;
    }

    for (uint32_t i = 0; i < file->header.entry_count; i++) {
        void *data;

        if (duke_grp_get_file_data_by_index(file, i, &data) == (size_t) -1) {
            return false;
        }
    }

    return true;
}

DukeGrpFileEntry* duke_grp_get_entry_by_index(DukeGrpFile *file, uint32_t index)
{
    if (file == NULL) {
        return NULL;
    }

    return g_list_nth_data(file->entries, index);
}

size_t duke_grp_get_file_data_by_index(DukeGrpFile *file, uint32_t index, void **data)
{
    DukeGrpFileEntry *entry;

    if (file == NULL || data == NULL) {
        return -1;
    }

    entry = duke_grp_get_entry_by_index(file, index);
    if (entry == NULL) {
        return -1;
    }

    if (entry->data != NULL) {
        *data = entry->data;
        return entry->filesize;
    }

    if (fseek(file->fp, file->data_section_offset + entry->data_offset,
            SEEK_SET) != 0) {
        return -1;
    }

    entry->data = malloc(entry->filesize == 0 ? 1 : entry->filesize);
    if (entry->data == NULL) {
        return -1;
    }

    size_t num_bytes = fread(entry->data, 1, entry->filesize, file->fp);

    if (num_bytes != entry->filesize) {
        free(entry->data);
        entry->data = NULL;
        return -1;
    }

    *data = entry->data;
    return num_bytes;
}

size_t duke_grp_get_file_data_by_filename(DukeGrpFile *file, const char *filename, void **data)
{
    GList *node;

    if (file == NULL || filename == NULL || data == NULL) {
        return -1;
    }

    for (node = file->entries; node != NULL; node = node->next) {
        DukeGrpFileEntry *entry = node->data;

        if (strcmp(entry->filename, filename) == 0) {
            int index = g_list_position(file->entries, node);
            return duke_grp_get_file_data_by_index(file, (uint32_t) index, data);
        }
    }

    return -1;
}

void duke_grp_reset_last_error(DukeGrpFile *file)
{
    if (file == NULL) {
        return;
    }

    file->last_error[0] = '\0';
}

bool duke_grp_close(DukeGrpFile *file)
{
    if (file == NULL) {
        return false;
    }

    if (file->fp != NULL) {
        fclose(file->fp);
        file->fp = NULL;
    }

    duke_grp_clear_entries(file);
    return true;
}

void duke_grp_free(DukeGrpFile *file)
{
    if (file == NULL) {
        return;
    }

    duke_grp_close(file);
    free(file);
}

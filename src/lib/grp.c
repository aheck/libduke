#include "libduke/grp.h"

#include <stdlib.h>

DukeGrpFile* duke_grp_new(void)
{
    DukeGrpFile *file;

    file = malloc(sizeof(DukeGrpFile));
    if (file == NULL) {
        return NULL;
    }

    file->header.magic[0] = '\0';
    file->header.entry_count = 0;
    file->entries = NULL;
    duke_grp_reset_last_error(file);

    return file;
}

bool duke_grp_open_filename(DukeGrpFile *file, const char *filename)
{
    size_t num_bytes;

    if (file == NULL) {
        return false;
    }

    file->fp = fopen(filename, "r");
    if (file->fp == NULL) {
        return false;
    }

    // read header
    num_bytes = fread(&file->header.magic, 1, 12, file->fp);
    if (num_bytes != 12) {
        duke_grp_close(file);
        return false;
    }

    num_bytes = fread(&file->header.entry_count, 1, 4, file->fp);
    if (num_bytes != 4) {
        duke_grp_close(file);
        return false;
    }

    return true;
}

bool duke_grp_read_entries_sparse(DukeGrpFile *file)
{
    size_t num_bytes;

    if (file == NULL) {
        return false;
    }

    file->entries = (DukeGrpFileEntry*) calloc(file->header.entry_count, sizeof(DukeGrpFileEntry));
    if (file->entries == NULL) {
        return false;
    }

    uint32_t file_offset = 0;

    for (uint32_t i = 0; i < file->header.entry_count; i++) {
        //FIXME: Error handling
        file->entries[i].data_offset = file_offset;
        file->entries[i].data = NULL;
        num_bytes = fread(&file->entries[i].filename, 1, 12, file->fp);
        if (num_bytes != 12) {
            free(file->entries);
            file->entries = NULL;
            return false;
        }
        file->entries[i].filename[12] = '\0';

        num_bytes = fread(&file->entries[i].filesize, 1, 4, file->fp);
        //fseek(file->fp, file->entries[i].filesize, SEEK_CUR);
        file_offset += file->entries[i].filesize;
    }

    file->data_section_offset = ftell(file->fp);
    fprintf(stderr, "exiting duke_grp_read_entries_sparse\n");
    return true;
}

bool duke_grp_read_entries_full(DukeGrpFile *file)
{
    if (file == NULL) {
        return false;
    }

    return true;
}

size_t duke_grp_get_file_data_by_index(DukeGrpFile *file, uint32_t index, void **data)
{
    if (file == NULL) {
        return -1;
    }

    DukeGrpFileEntry *entry = &file->entries[index];

    if (entry->data != NULL) {
        *data = entry->data;
        return entry->filesize;
    }

    fseek(file->fp, file->data_section_offset + entry->data_offset, SEEK_SET);
    entry->data = malloc(entry->filesize);

    size_t num_bytes = fread(entry->data, 1, entry->filesize, file->fp);

    if (num_bytes != entry->filesize) {
        free(entry->data);
        entry->data = NULL;
        return -1;
    }

    return num_bytes;
}

size_t duke_grp_get_file_data_by_filename(DukeGrpFile *file, const char *filename, void **data)
{
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

    return true;
}

void duke_grp_free(DukeGrpFile *file)
{
    if (file == NULL) {
        return;
    }

    duke_grp_close(file);
}







void duke_grp_init(struct duke_grp *grp)
{
    if (grp == NULL)
        return;

    grp->version = 1;
    grp->entry_count = 0;
}

int duke_grp_add_entry(struct duke_grp *grp)
{
    if (grp == NULL)
        return -1;

    ++grp->entry_count;

    return 0;
}

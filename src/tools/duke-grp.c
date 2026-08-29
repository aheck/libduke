#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "libduke/grp.h"

#define GRP_MAGIC "KenSilverman"
#define GRP_MAGIC_SIZE 12

static void usage(void)
{
    fprintf(stderr, "Usage: duke-grp ACTION GRPFILE [PARAMS]\n");
    fprintf(stderr, "Actions:\n");
    fprintf(stderr, "    info GRPFILE     - Print information about the GRP archive\n");
    fprintf(stderr, "    list GRPFILE     - List all files in the GRP archive\n");
    fprintf(stderr, "    validate GRPFILE - Validate the archive\n\n");
    fprintf(stderr, "    extract GRPFILE      - Extract all files to the current directory\n");
    fprintf(stderr, "    get GRPFILE FILENAME - Extract one file to the current directory\n\n");
    fprintf(stderr, "    append GRPFILE FILENAME              - Append a file to the archive\n");
    fprintf(stderr, "    replace GRPFILE FILE_IN_GRP FILENAME - Replace a file in the archive\n\n");
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "    duke-grp list DUKE3D.GRP\n");
    fprintf(stderr, "    duke-grp get DUKE3D.GRP E1L1.MAP\n");
    fprintf(stderr, "    duke-grp extract DUKE3D.GRP\n");
    fprintf(stderr, "    duke-grp append DUKE3D.GRP NEW.MAP\n");
    fprintf(stderr, "    duke-grp replace DUKE3D.GRP E1L1.MAP NEW-E1L1.MAP\n");
    exit(2);
}

static DukeGrpFile *open_archive(const char *filename, bool read_entries)
{
    DukeGrpFile *file = duke_grp_new();

    if (file == NULL) {
        fprintf(stderr, "ERROR: Failed to allocate GRP archive\n");
        return NULL;
    }
    if (!duke_grp_open_filename(file, filename)) {
        fprintf(stderr, "ERROR: Failed to open GRP archive: %s\n", filename);
        duke_grp_free(file);
        return NULL;
    }
    if (read_entries && !duke_grp_read_entries_sparse(file)) {
        fprintf(stderr, "ERROR: Failed to read entries from: %s\n", filename);
        duke_grp_free(file);
        return NULL;
    }
    return file;
}

static DukeGrpFileEntry *find_entry(DukeGrpFile *file, const char *filename,
    uint32_t *index)
{
    for (uint32_t i = 0; i < file->header.entry_count; i++) {
        DukeGrpFileEntry *entry = duke_grp_get_entry_by_index(file, i);
        if (entry != NULL && strcmp(entry->filename, filename) == 0) {
            if (index != NULL) {
                *index = i;
            }
            return entry;
        }
    }
    return NULL;
}

static bool safe_member_name(const char *filename)
{
    return filename[0] != '\0' && strcmp(filename, ".") != 0
        && strcmp(filename, "..") != 0 && strchr(filename, '/') == NULL
        && strchr(filename, '\\') == NULL;
}

static bool write_member(DukeGrpFile *file, uint32_t index)
{
    DukeGrpFileEntry *entry = duke_grp_get_entry_by_index(file, index);
    void *data = NULL;
    FILE *output;
    size_t size;
    bool ok;

    if (entry == NULL || !safe_member_name(entry->filename)) {
        fprintf(stderr, "ERROR: Unsafe filename in archive: %s\n",
            entry == NULL ? "(missing entry)" : entry->filename);
        return false;
    }
    size = duke_grp_get_file_data_by_index(file, index, &data);
    if (size == (size_t) -1) {
        fprintf(stderr, "ERROR: Failed to read %s from archive\n", entry->filename);
        return false;
    }
    output = fopen(entry->filename, "wb");
    if (output == NULL) {
        fprintf(stderr, "ERROR: Failed to create %s: %s\n", entry->filename,
            strerror(errno));
        return false;
    }
    ok = fwrite(data, 1, size, output) == size;
    if (fclose(output) != 0) {
        ok = false;
    }
    if (!ok) {
        fprintf(stderr, "ERROR: Failed to write %s\n", entry->filename);
    }
    return ok;
}

static bool validate_archive(DukeGrpFile *file, const char *filename)
{
    uint64_t expected_size = file->data_section_offset;
    long actual_size;

    if (memcmp(file->header.magic, GRP_MAGIC, GRP_MAGIC_SIZE) != 0) {
        fprintf(stderr, "Validation error: invalid GRP magic\n");
        return false;
    }
    for (uint32_t i = 0; i < file->header.entry_count; i++) {
        DukeGrpFileEntry *entry = duke_grp_get_entry_by_index(file, i);
        if (entry == NULL) {
            fprintf(stderr, "Validation error: missing entry %u\n", i);
            return false;
        }
        expected_size += entry->filesize;
    }
    if (fseek(file->fp, 0, SEEK_END) != 0 || (actual_size = ftell(file->fp)) < 0) {
        fprintf(stderr, "Validation error: cannot determine size of %s\n", filename);
        return false;
    }
    if ((uint64_t) actual_size != expected_size) {
        fprintf(stderr, "Validation error: expected %llu bytes, found %ld\n",
            (unsigned long long) expected_size, actual_size);
        return false;
    }
    return true;
}

static bool read_local_file(const char *filename, uint8_t **data, uint32_t *size)
{
    FILE *fp = fopen(filename, "rb");
    long length;
    uint8_t *buffer;

    if (fp == NULL) {
        fprintf(stderr, "ERROR: Failed to open %s: %s\n", filename, strerror(errno));
        return false;
    }
    if (fseek(fp, 0, SEEK_END) != 0 || (length = ftell(fp)) < 0
            || (uint64_t) length > UINT32_MAX || fseek(fp, 0, SEEK_SET) != 0) {
        fprintf(stderr, "ERROR: Cannot determine a valid size for %s\n", filename);
        fclose(fp);
        return false;
    }
    buffer = malloc(length == 0 ? 1 : (size_t) length);
    if (buffer == NULL || fread(buffer, 1, (size_t) length, fp) != (size_t) length) {
        fprintf(stderr, "ERROR: Failed to read %s\n", filename);
        free(buffer);
        fclose(fp);
        return false;
    }
    if (fclose(fp) != 0) {
        free(buffer);
        return false;
    }
    *data = buffer;
    *size = (uint32_t) length;
    return true;
}

static const char *base_name(const char *filename)
{
    const char *slash = strrchr(filename, '/');
    const char *backslash = strrchr(filename, '\\');

    if (backslash != NULL && (slash == NULL || backslash > slash)) {
        slash = backslash;
    }
    return slash == NULL ? filename : slash + 1;
}

static bool write_archive(DukeGrpFile *file, const char *archive_name,
    const char *new_name, const uint8_t *new_data, uint32_t new_size,
    bool append, uint32_t replace_index)
{
    size_t template_size = strlen(archive_name) + sizeof(".tmp.XXXXXX");
    char *template = malloc(template_size);
    uint32_t output_count;
    struct stat archive_stat;
    FILE *output = NULL;
    int fd = -1;
    bool ok = false;

    if (template == NULL || (append && file->header.entry_count == UINT32_MAX)) {
        fprintf(stderr, "ERROR: Cannot allocate archive output\n");
        goto cleanup;
    }
    output_count = file->header.entry_count + (append ? 1u : 0u);
    snprintf(template, template_size, "%s.tmp.XXXXXX", archive_name);
    fd = mkstemp(template);
    if (fd < 0) {
        fprintf(stderr, "ERROR: Failed to create temporary archive: %s\n",
            strerror(errno));
        goto cleanup;
    }
    if (stat(archive_name, &archive_stat) == 0) {
        (void) fchmod(fd, archive_stat.st_mode);
    }
    output = fdopen(fd, "wb");
    if (output == NULL) {
        fprintf(stderr, "ERROR: Failed to open temporary archive: %s\n",
            strerror(errno));
        goto cleanup;
    }
    fd = -1;
    if (fwrite(GRP_MAGIC, 1, GRP_MAGIC_SIZE, output) != GRP_MAGIC_SIZE
            || fwrite(&output_count, 1, 4, output) != 4) {
        goto write_error;
    }
    for (uint32_t i = 0; i < file->header.entry_count; i++) {
        DukeGrpFileEntry *entry = duke_grp_get_entry_by_index(file, i);
        uint32_t size = !append && i == replace_index ? new_size : entry->filesize;
        if (fwrite(entry->filename, 1, 12, output) != 12
                || fwrite(&size, 1, 4, output) != 4) {
            goto write_error;
        }
    }
    if (append) {
        char stored_name[12] = { 0 };
        memcpy(stored_name, new_name, strlen(new_name));
        if (fwrite(stored_name, 1, 12, output) != 12
                || fwrite(&new_size, 1, 4, output) != 4) {
            goto write_error;
        }
    }
    for (uint32_t i = 0; i < file->header.entry_count; i++) {
        DukeGrpFileEntry *entry = duke_grp_get_entry_by_index(file, i);
        void *data;
        size_t size;

        if (!append && i == replace_index) {
            data = (void *) new_data;
            size = new_size;
        } else {
            size = duke_grp_get_file_data_by_index(file, i, &data);
            if (size == (size_t) -1) {
                fprintf(stderr, "ERROR: Failed to read %s from archive\n",
                    entry->filename);
                goto cleanup;
            }
        }
        if (fwrite(data, 1, size, output) != size) {
            goto write_error;
        }
    }
    if (append && fwrite(new_data, 1, new_size, output) != new_size) {
        goto write_error;
    }
    if (fflush(output) != 0 || fsync(fileno(output)) != 0) {
        goto write_error;
    }
    if (fclose(output) != 0) {
        output = NULL;
        goto write_error;
    }
    output = NULL;
    if (rename(template, archive_name) != 0) {
        fprintf(stderr, "ERROR: Failed to replace %s: %s\n", archive_name,
            strerror(errno));
        goto cleanup;
    }
    ok = true;
    goto cleanup;

write_error:
    fprintf(stderr, "ERROR: Failed to write temporary archive\n");
cleanup:
    if (output != NULL) {
        fclose(output);
    } else if (fd >= 0) {
        close(fd);
    }
    if (!ok && template != NULL) {
        unlink(template);
    }
    free(template);
    return ok;
}

static bool modify_archive(DukeGrpFile *file, const char *archive_name,
    const char *member_name, const char *source_name, bool append)
{
    uint8_t *data = NULL;
    uint32_t size;
    uint32_t replace_index = 0;
    const char *stored_name = append ? base_name(source_name) : member_name;
    bool ok;

    if (stored_name[0] == '\0' || strlen(stored_name) > 12
            || !safe_member_name(stored_name)) {
        fprintf(stderr, "ERROR: GRP filenames must be 1 to 12 plain characters\n");
        return false;
    }
    if (append && find_entry(file, stored_name, NULL) != NULL) {
        fprintf(stderr, "ERROR: %s already exists in the archive\n", stored_name);
        return false;
    }
    if (!append && find_entry(file, member_name, &replace_index) == NULL) {
        fprintf(stderr, "ERROR: %s is not in the archive\n", member_name);
        return false;
    }
    if (!read_local_file(source_name, &data, &size)) {
        return false;
    }
    ok = write_archive(file, archive_name, stored_name, data, size, append,
        replace_index);
    free(data);
    return ok;
}

static bool valid_argument_count(const char *action, int argc)
{
    if (strcmp(action, "info") == 0 || strcmp(action, "list") == 0
            || strcmp(action, "validate") == 0 || strcmp(action, "extract") == 0) {
        return argc == 3;
    }
    if (strcmp(action, "get") == 0 || strcmp(action, "append") == 0) {
        return argc == 4;
    }
    if (strcmp(action, "replace") == 0) {
        return argc == 5;
    }
    return false;
}

int main(int argc, char **argv)
{
    const char *action;
    const char *archive_name;
    DukeGrpFile *file;
    bool ok = true;

    if (argc < 3) {
        usage();
    }
    action = argv[1];
    archive_name = argv[2];
    if (!valid_argument_count(action, argc)) {
        usage();
    }
    file = open_archive(archive_name, strcmp(action, "info") != 0);
    if (file == NULL) {
        return 1;
    }

    if (strcmp(action, "info") == 0) {
        printf("Magic: %.12s\n", file->header.magic);
        printf("Number of files: %u\n", file->header.entry_count);
    } else if (strcmp(action, "list") == 0) {
        for (uint32_t i = 0; i < file->header.entry_count; i++) {
            DukeGrpFileEntry *entry = duke_grp_get_entry_by_index(file, i);
            printf("%s %u\n", entry->filename, entry->filesize);
        }
    } else if (strcmp(action, "validate") == 0) {
        ok = validate_archive(file, archive_name);
        printf("Validation status: %s\n", ok ? "successful" : "failed");
    } else if (strcmp(action, "extract") == 0) {
        for (uint32_t i = 0; ok && i < file->header.entry_count; i++) {
            ok = write_member(file, i);
        }
    } else if (strcmp(action, "get") == 0) {
        uint32_t index;
        if (find_entry(file, argv[3], &index) == NULL) {
            fprintf(stderr, "ERROR: %s is not in the archive\n", argv[3]);
            ok = false;
        } else {
            ok = write_member(file, index);
        }
    } else if (strcmp(action, "append") == 0) {
        ok = modify_archive(file, archive_name, NULL, argv[3], true);
    } else {
        ok = modify_archive(file, archive_name, argv[3], argv[4], false);
    }

    duke_grp_free(file);
    return ok ? 0 : 1;
}

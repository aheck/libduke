#include <stdio.h>
#include <stdlib.h>

#include "libduke/grp.h"

void usage()
{
    fprintf(stderr, "Usage: duke-map GRPFILE ACTION [PARAMS]\n");
    fprintf(stderr, "Actions:\n");
    fprintf(stderr, "    info     - Print the number of files in the GRP archive\n");
    fprintf(stderr, "    list     - List all files in the GRP archive\n");
    fprintf(stderr, "    validate - Validate the archive\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "    extract      - Extract all files to the current working directory\n");
    fprintf(stderr, "    get FILENAME - Extract the given file to the current working directory\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "    append FILENAME              - Append the given file to the archive\n");
    fprintf(stderr, "    replace FILE_IN_GRP FILENAME - Replace the given file in the archive\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "    \n");
    exit(2);
}

int main(int argc, char **argv)
{
    DukeGrpFile *file;

    if (argc != 2) {
        return 2;
    }

    file = duke_grp_new();
    if (file == NULL) {
        fprintf(stderr, "ERROR: Failed to allocate DukeGrpFile\n");
        return 1;
    }

    bool res;
    res = duke_grp_open_filename(file, argv[1]);
    if (res == false) {
        fprintf(stderr, "\n");
        return 1;
    }

    printf("Magic: %.12s\n", &file->header.magic[0]);
    printf("Num of files: %d\n", file->header.entry_count);

    res = duke_grp_read_entries_sparse(file);
    if (res == false) {
        fprintf(stderr, "ERROR: Failed to read file entries\n");
        return 1;
    }

    if (file->entries != NULL) {
        printf("Files:\n\n");
        for (uint32_t i = 0; i < file->header.entry_count; i++) {
            printf("%s %d\n", &file->entries[i].filename[0], file->entries[i].filesize);
        }
    }

    return 0;
}

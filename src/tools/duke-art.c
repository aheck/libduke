#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "libduke/art.h"

static void usage(void)
{
    fprintf(stderr, "Usage: duke-art ACTION ARTFILE [PARAMS]\n");
    fprintf(stderr, "Actions:\n");
    fprintf(stderr, "    info ARTFILE     - Print ART header information\n");
    fprintf(stderr, "    list ARTFILE     - List tile metadata\n");
    fprintf(stderr, "    validate ARTFILE - Validate the archive\n\n");
    fprintf(stderr, "    extract ARTFILE        - Extract every tile as tile<NUMBER>.raw\n");
    fprintf(stderr, "    get ARTFILE TILE       - Extract one tile as tile<NUMBER>.raw\n\n");
    fprintf(stderr, "    append ARTFILE WIDTH HEIGHT FILE [PICANM]\n");
    fprintf(stderr, "        Append raw pixels as the next tile\n");
    fprintf(stderr, "    replace ARTFILE TILE WIDTH HEIGHT FILE [PICANM]\n");
    fprintf(stderr, "        Replace or add a tile\n");
    fprintf(stderr, "    create ARTFILE TILE WIDTH HEIGHT FILE [PICANM]\n");
    fprintf(stderr, "        Create a new one-tile ART archive\n\n");
    fprintf(stderr, "PICANM defaults to 0 and accepts decimal or 0x-prefixed hexadecimal.\n");
    fprintf(stderr, "Pixel files must contain exactly WIDTH * HEIGHT column-major bytes.\n\n");
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "    duke-art list TILES000.ART\n");
    fprintf(stderr, "    duke-art get TILES000.ART 42\n");
    fprintf(stderr, "    duke-art replace TILES000.ART 42 64 64 tile42.raw 0x00000103\n");
    fprintf(stderr, "    duke-art create TILES999.ART 4096 32 32 first.raw\n");
    exit(2);
}

static bool parse_i32(const char *text, int32_t minimum, int32_t maximum,
    int32_t *result)
{
    char *end;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || *text == '\0' || *end != '\0'
            || value < minimum || value > maximum) {
        fprintf(stderr, "ERROR: Invalid number: %s\n", text);
        return false;
    }
    *result = (int32_t) value;
    return true;
}

static bool parse_picanm(const char *text, uint32_t *result)
{
    char *end;
    unsigned long long value;

    errno = 0;
    value = strtoull(text, &end, 0);
    if (errno != 0 || *text == '\0' || *end != '\0' || value > UINT32_MAX) {
        fprintf(stderr, "ERROR: Invalid picanm value: %s\n", text);
        return false;
    }
    *result = (uint32_t) value;
    return true;
}

static DukeArtFile *open_art(const char *filename, bool read_tiles)
{
    DukeArtFile *art = duke_art_new();

    if (art == NULL) {
        fprintf(stderr, "ERROR: Failed to allocate ART object\n");
        return NULL;
    }
    if (!duke_art_open_filename(art, filename)) {
        fprintf(stderr, "ERROR: %s\n", art->last_error);
        duke_art_free(art);
        return NULL;
    }
    if (read_tiles && !duke_art_read_tiles_sparse(art)) {
        fprintf(stderr, "ERROR: %s\n", art->last_error);
        duke_art_free(art);
        return NULL;
    }
    return art;
}

static bool read_file(const char *filename, uint8_t **data, size_t *size)
{
    FILE *fp = fopen(filename, "rb");
    long length;
    uint8_t *buffer;

    if (fp == NULL) {
        fprintf(stderr, "ERROR: Failed to open %s: %s\n", filename, strerror(errno));
        return false;
    }
    if (fseek(fp, 0, SEEK_END) != 0 || (length = ftell(fp)) < 0
            || fseek(fp, 0, SEEK_SET) != 0) {
        fprintf(stderr, "ERROR: Failed to determine the size of %s\n", filename);
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
    *size = (size_t) length;
    return true;
}

static bool write_tile(DukeArtFile *art, int32_t tile_number)
{
    DukeArtTile *tile = duke_art_get_tile_by_number(art, tile_number);
    char filename[64];
    void *data;
    size_t size;
    FILE *output;
    bool ok;

    if (tile == NULL) {
        fprintf(stderr, "ERROR: Tile %d is not in this ART file\n", tile_number);
        return false;
    }
    size = duke_art_get_tile_data_by_number(art, tile_number, &data);
    if (size == (size_t) -1) {
        fprintf(stderr, "ERROR: %s\n", art->last_error);
        return false;
    }
    snprintf(filename, sizeof(filename), "tile%d.raw", tile_number);
    output = fopen(filename, "wb");
    if (output == NULL) {
        fprintf(stderr, "ERROR: Failed to create %s: %s\n", filename,
            strerror(errno));
        return false;
    }
    ok = fwrite(data, 1, size, output) == size;
    if (fclose(output) != 0) {
        ok = false;
    }
    if (!ok) {
        fprintf(stderr, "ERROR: Failed to write %s\n", filename);
    }
    return ok;
}

static bool write_art_atomic(DukeArtFile *art, const char *filename,
    bool must_not_exist)
{
    struct stat status;
    size_t template_size = strlen(filename) + sizeof(".tmp.XXXXXX");
    char *template = NULL;
    int fd = -1;
    bool ok = false;

    if (must_not_exist && lstat(filename, &status) == 0) {
        fprintf(stderr, "ERROR: ART file already exists: %s\n", filename);
        return false;
    }
    if (must_not_exist && errno != ENOENT) {
        fprintf(stderr, "ERROR: Cannot inspect %s: %s\n", filename,
            strerror(errno));
        return false;
    }
    template = malloc(template_size);
    if (template == NULL) {
        return false;
    }
    snprintf(template, template_size, "%s.tmp.XXXXXX", filename);
    fd = mkstemp(template);
    if (fd < 0) {
        fprintf(stderr, "ERROR: Failed to create temporary ART file: %s\n",
            strerror(errno));
        goto cleanup;
    }
    if (!must_not_exist && stat(filename, &status) == 0) {
        (void) fchmod(fd, status.st_mode);
    } else if (must_not_exist) {
        mode_t mask = umask(0);
        umask(mask);
        (void) fchmod(fd, 0666 & ~mask);
    }
    if (close(fd) != 0) {
        fd = -1;
        goto cleanup;
    }
    fd = -1;
    if (!duke_art_write_filename(art, template)) {
        fprintf(stderr, "ERROR: %s\n", art->last_error);
        goto cleanup;
    }
    if (must_not_exist) {
        if (link(template, filename) != 0) {
            fprintf(stderr, "ERROR: Failed to create %s: %s\n", filename,
                strerror(errno));
            goto cleanup;
        }
        if (unlink(template) != 0) {
            fprintf(stderr, "WARNING: Failed to remove temporary name %s\n",
                template);
        }
    } else if (rename(template, filename) != 0) {
        fprintf(stderr, "ERROR: Failed to replace %s: %s\n", filename,
            strerror(errno));
        goto cleanup;
    }
    ok = true;

cleanup:
    if (fd >= 0) {
        close(fd);
    }
    if (!ok && template != NULL) {
        unlink(template);
    }
    free(template);
    return ok;
}

static bool set_tile_from_arguments(DukeArtFile *art, int32_t tile_number,
    const char *width_text, const char *height_text, const char *filename,
    const char *picanm_text)
{
    int32_t width;
    int32_t height;
    uint32_t picanm = 0;
    uint8_t *data;
    size_t size;
    bool ok;

    if (!parse_i32(width_text, 0, INT16_MAX, &width)
            || !parse_i32(height_text, 0, INT16_MAX, &height)
            || (picanm_text != NULL && !parse_picanm(picanm_text, &picanm))
            || !read_file(filename, &data, &size)) {
        return false;
    }
    ok = duke_art_set_tile(art, tile_number, (int16_t) width, (int16_t) height,
        picanm, data, size);
    if (!ok) {
        fprintf(stderr, "ERROR: %s\n", art->last_error);
    }
    free(data);
    return ok;
}

static bool valid_arguments(const char *action, int argc)
{
    if (strcmp(action, "info") == 0 || strcmp(action, "list") == 0
            || strcmp(action, "validate") == 0 || strcmp(action, "extract") == 0) {
        return argc == 3;
    }
    if (strcmp(action, "get") == 0) {
        return argc == 4;
    }
    if (strcmp(action, "append") == 0) {
        return argc == 6 || argc == 7;
    }
    if (strcmp(action, "replace") == 0 || strcmp(action, "create") == 0) {
        return argc == 7 || argc == 8;
    }
    return false;
}

int main(int argc, char **argv)
{
    const char *action;
    const char *filename;
    DukeArtFile *art;
    int32_t tile_number;
    bool ok = true;

    if (argc < 3) {
        usage();
    }
    action = argv[1];
    filename = argv[2];
    if (!valid_arguments(action, argc)) {
        usage();
    }

    if (strcmp(action, "create") == 0) {
        if (!parse_i32(argv[3], 0, INT32_MAX, &tile_number)) {
            return 1;
        }
        art = duke_art_new();
        if (art == NULL) {
            return 1;
        }
        ok = set_tile_from_arguments(art, tile_number, argv[4], argv[5], argv[6],
            argc == 8 ? argv[7] : NULL);
        if (ok) {
            ok = write_art_atomic(art, filename, true);
        }
        duke_art_free(art);
        return ok ? 0 : 1;
    }

    art = open_art(filename, strcmp(action, "info") != 0);
    if (art == NULL) {
        return 1;
    }
    if (strcmp(action, "info") == 0) {
        printf("ART version: %d\n", art->header.artversion);
        printf("Number of tiles: %d\n", art->header.numtiles);
        printf("Local tile start: %d\n", art->header.localtilestart);
        printf("Local tile end: %d\n", art->header.localtileend);
    } else if (strcmp(action, "list") == 0) {
        int32_t number = art->header.localtilestart;
        for (;;) {
            DukeArtTile *tile = duke_art_get_tile_by_number(art, number);
            printf("%d %dx%d %u bytes picanm=0x%08x\n", number, tile->width,
                tile->height, (unsigned int) tile->width * tile->height,
                tile->picanm);
            if (number == art->header.localtileend) {
                break;
            }
            number++;
        }
    } else if (strcmp(action, "validate") == 0) {
        ok = duke_art_validate(art);
        printf("Validation status: %s\n", ok ? "successful" : "failed");
        if (!ok) {
            fprintf(stderr, "Validation error: %s\n", art->last_error);
        }
    } else if (strcmp(action, "extract") == 0) {
        int32_t number = art->header.localtilestart;
        while (ok) {
            ok = write_tile(art, number);
            if (number == art->header.localtileend) {
                break;
            }
            number++;
        }
    } else if (strcmp(action, "get") == 0) {
        ok = parse_i32(argv[3], 0, INT32_MAX, &tile_number)
            && write_tile(art, tile_number);
    } else if (strcmp(action, "append") == 0) {
        if (art->header.localtileend == INT32_MAX) {
            fprintf(stderr, "ERROR: Cannot append beyond tile %d\n", INT32_MAX);
            ok = false;
        } else {
            tile_number = art->header.localtileend + 1;
            ok = set_tile_from_arguments(art, tile_number, argv[3], argv[4],
                argv[5], argc == 7 ? argv[6] : NULL);
        }
        if (ok) {
            ok = write_art_atomic(art, filename, false);
        }
    } else {
        ok = parse_i32(argv[3], 0, INT32_MAX, &tile_number)
            && set_tile_from_arguments(art, tile_number, argv[4], argv[5],
                argv[6], argc == 8 ? argv[7] : NULL);
        if (ok) {
            ok = write_art_atomic(art, filename, false);
        }
    }

    duke_art_free(art);
    return ok ? 0 : 1;
}

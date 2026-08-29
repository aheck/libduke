#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libduke/map.h"

void usage(void)
{
    fprintf(stderr, "Usage: duke-map ACTION MAPFILE\n");
    fprintf(stderr, "Actions:\n");
    fprintf(stderr, "    info     - Print the number of files in the GRP archive\n");
    fprintf(stderr, "    dump     - List all files in the GRP archive\n");
    fprintf(stderr, "    validate - Validate the map\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "    duke-map info usermap.map\n");
    fprintf(stderr, "    duke-map dump usermap.map\n");
    fprintf(stderr, "    duke-map validate usermap.map\n");
    exit(2);
}

void print_map_info(DukeMapFile *map)
{
    printf("mapversion: %d\n", map->mapversion);
    printf("posx: %d\n", map->posx);
    printf("posy: %d\n", map->posy);
    printf("posz: %d\n", map->posz);
    printf("ang: %d\n", map->ang);
    printf("cursectnum: %d\n", map->cursectnum);
    printf("numsectors: %d\n", map->numsectors);
    printf("numwalls: %d\n", map->numwalls);
    printf("numsprites: %d\n", map->numsprites);
}

void print_map_dump(DukeMapFile *map)
{
    print_map_info(map);

    printf("\nSECTORS:\n\n");

    for (int i = 0; i < map->numsectors; i++) {
        DukeMapSector *sector = map->sectors[i];
        if (i > 0) {
            printf("\n");

        }
        printf("wallptr: %d\n", sector->wallptr);
        printf("wallnum: %d\n", sector->wallnum);
        printf("ceilingz: %d\n", sector->ceilingz);
        printf("floorz: %d\n", sector->floorz);
        printf("ceilingpicnum: %d\n", sector->ceilingpicnum);
        printf("floorpicnum: %d\n", sector->floorpicnum);
        printf("lotag: %d\n", sector->lotag);
        printf("hitag: %d\n", sector->hitag);
        printf("extra: %d\n", sector->extra);
    }
}

DukeMapFile* open_map_file(const char *filename)
{
    bool res;
    DukeMapFile *map = duke_map_file_new();

    if (map == NULL) {
        fprintf(stderr, "ERROR: Failed to allocate map object\n");
        exit(1);
    }

    res = duke_map_file_read_from_filename(map, filename);

    if (res == false) {
        fprintf(stderr, "ERROR: Failed to read map: %s\n", filename);
        duke_map_file_free(map);
        exit(1);
    }

    return map;
}

int main(int argc, const char **argv)
{
    DukeMapFile *map;
    bool res;

    if (argc != 3) {
        usage();
    }

    const char *action = argv[1];
    const char *filename = argv[2];

    if (strcmp(action, "info") == 0) {
        map = open_map_file(filename);
        print_map_info(map);
        duke_map_file_free(map);
    } else if (strcmp(action, "dump") == 0) {
        map = open_map_file(filename);
        print_map_dump(map);
        duke_map_file_free(map);
    } else if (strcmp(action, "validate") == 0) {
        map = open_map_file(filename);

        res = duke_map_file_validate(map);
        printf("Validation status: %s\n", res ? "successful" : "failed");
        if (res == false) {
            printf("Validation error: %s\n", &map->last_error[0]);
        }

        duke_map_file_free(map);
    } else {
        usage();
    }

    return 0;
}

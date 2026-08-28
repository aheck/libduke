#include <stdio.h>
#include <stdlib.h>

#include "libduke/map.h"

void usage(void)
{
    exit(2);
}

int main(int argc, const char **argv)
{
    DukeMapFile *map;
    bool res;

    if (argc != 2) {
        usage();
    }

    const char *filename = argv[1];

    map = duke_map_file_new();
    if (map == NULL) {
        fprintf(stderr, "ERROR: Failed to allocate map object\n");
        return 1;
    }

    res = duke_map_file_read_from_filename(map, filename);
    if (res == false) {
        fprintf(stderr, "ERROR: Failed to read map: %s\n", filename);
        return 1;
    }

    printf("mapversion: %d\n", map->mapversion);
    printf("posx: %d\n", map->posx);
    printf("posy: %d\n", map->posy);
    printf("posz: %d\n", map->posz);
    printf("ang: %d\n", map->ang);
    printf("cursectnum: %d\n", map->cursectnum);
    printf("numsectors: %d\n", map->numsectors);
    printf("numwalls: %d\n", map->numwalls);
    printf("numsprites: %d\n", map->numsprites);
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

    res = duke_map_file_validate(map);
    printf("\n\n");
    printf("Validation status: %s\n", res ? "successful" : "failed");
    if (res == false) {
        printf("Validation error: %s\n", &map->last_error[0]);
    }

    duke_map_file_free(map);

    return 0;
}

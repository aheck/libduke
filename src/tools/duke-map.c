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
        printf("Sector %d:\n", i);
        printf("wallptr: %d\n", sector->wallptr);
        printf("wallnum: %d\n", sector->wallnum);
        printf("ceilingz: %d\n", sector->ceilingz);
        printf("floorz: %d\n", sector->floorz);
        printf("ceilingstat: %d\n", sector->ceilingstat);
        printf("floorstat: %d\n", sector->floorstat);
        printf("ceilingpicnum: %d\n", sector->ceilingpicnum);
        printf("ceilingheinum: %d\n", sector->ceilingheinum);
        printf("ceilingshade: %d\n", sector->ceilingshade);
        printf("ceilingpal: %u\n", sector->ceilingpal);
        printf("ceilingxpanning: %u\n", sector->ceilingxpanning);
        printf("ceilingypanning: %u\n", sector->ceilingypanning);
        printf("floorpicnum: %d\n", sector->floorpicnum);
        printf("floorheinum: %d\n", sector->floorheinum);
        printf("floorshade: %d\n", sector->floorshade);
        printf("floorpal: %u\n", sector->floorpal);
        printf("floorxpanning: %u\n", sector->floorxpanning);
        printf("floorypanning: %u\n", sector->floorypanning);
        printf("visibility: %u\n", sector->visibility);
        printf("filler: %u\n", sector->filler);
        printf("lotag: %d\n", sector->lotag);
        printf("hitag: %d\n", sector->hitag);
        printf("extra: %d\n", sector->extra);
    }

    printf("\nWALLS:\n\n");

    for (int i = 0; i < map->numwalls; i++) {
        DukeMapWall *wall = map->walls[i];
        if (i > 0) {
            printf("\n");
        }
        printf("Wall %d:\n", i);
        printf("x: %d\n", wall->x);
        printf("y: %d\n", wall->y);
        printf("point2: %d\n", wall->point2);
        printf("nextwall: %d\n", wall->nextwall);
        printf("nextsector: %d\n", wall->nextsector);
        printf("cstat: %d\n", wall->cstat);
        printf("picnum: %d\n", wall->picnum);
        printf("overpicnum: %d\n", wall->overpicnum);
        printf("shade: %d\n", wall->shade);
        printf("pal: %u\n", wall->pal);
        printf("xrepeat: %u\n", wall->xrepeat);
        printf("yrepeat: %u\n", wall->yrepeat);
        printf("xpanning: %u\n", wall->xpanning);
        printf("ypanning: %u\n", wall->ypanning);
        printf("lotag: %d\n", wall->lotag);
        printf("hitag: %d\n", wall->hitag);
        printf("extra: %d\n", wall->extra);
    }

    printf("\nSPRITES:\n\n");

    for (int i = 0; i < map->numsprites; i++) {
        DukeMapSprite *sprite = map->sprites[i];
        if (i > 0) {
            printf("\n");
        }
        printf("Sprite %d:\n", i);
        printf("x: %d\n", sprite->x);
        printf("y: %d\n", sprite->y);
        printf("z: %d\n", sprite->z);
        printf("cstat: %d\n", sprite->cstat);
        printf("picnum: %d\n", sprite->picnum);
        printf("shade: %d\n", sprite->shade);
        printf("pal: %u\n", sprite->pal);
        printf("clipdist: %u\n", sprite->clipdist);
        printf("filler: %u\n", sprite->filler);
        printf("xrepeat: %u\n", sprite->xrepeat);
        printf("yrepeat: %u\n", sprite->yrepeat);
        printf("xoffset: %d\n", sprite->xoffset);
        printf("yoffset: %d\n", sprite->yoffset);
        printf("sectnum: %d\n", sprite->sectnum);
        printf("statnum: %d\n", sprite->statnum);
        printf("ang: %d\n", sprite->ang);
        printf("owner: %d\n", sprite->owner);
        printf("xvel: %d\n", sprite->xvel);
        printf("yvel: %d\n", sprite->yvel);
        printf("zvel: %d\n", sprite->zvel);
        printf("lotag: %d\n", sprite->lotag);
        printf("hitag: %d\n", sprite->hitag);
        printf("extra: %d\n", sprite->extra);
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

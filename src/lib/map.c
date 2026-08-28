#include "libduke/map.h"

#include <stdlib.h>
#include <string.h>

static int16_t read_i16_le(const uint8_t *data)
{
    uint16_t raw;
    int16_t value;

    raw = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
    memcpy(&value, &raw, sizeof(value));
    return value;
}

static uint16_t read_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static int32_t read_i32_le(const uint8_t *data)
{
    uint32_t raw;
    int32_t value;

    raw = (uint32_t)data[0]
        | ((uint32_t)data[1] << 8)
        | ((uint32_t)data[2] << 16)
        | ((uint32_t)data[3] << 24);
    memcpy(&value, &raw, sizeof(value));
    return value;
}

static void duke_map_file_clear(DukeMapFile *map)
{
    uint16_t i;

    if (map->sectors != NULL) {
        for (i = 0; i < (uint16_t)map->numsectors; i++) {
            duke_map_sector_free(map->sectors[i]);
        }
    }
    free(map->sectors);

    if (map->walls != NULL) {
        for (i = 0; i < map->numwalls; i++) {
            duke_map_wall_free(map->walls[i]);
        }
    }
    free(map->walls);

    if (map->sprites != NULL) {
        for (i = 0; i < map->numsprites; i++) {
            duke_map_sprite_free(map->sprites[i]);
        }
    }
    free(map->sprites);

    map->numsectors = 0;
    map->sectors = NULL;
    map->numwalls = 0;
    map->walls = NULL;
    map->numsprites = 0;
    map->sprites = NULL;
}

DukeMapFile* duke_map_file_new(void)
{
    DukeMapFile *map;

    map = malloc(sizeof(DukeMapFile));
    if (map == NULL) {
        return NULL;
    }

    map->mapversion = -1;
    map->posx = INT32_MIN;
    map->posy = INT32_MIN;
    map->posz = INT32_MIN;
    map->ang = INT16_MIN;
    map->cursectnum = -1;
    map->numsectors = 0;
    map->sectors = NULL;
    map->numwalls = 0;
    map->walls = NULL;
    map->numsprites = 0;
    map->sprites = NULL;
    map->last_error[0] = '\0';

    return map;
}

bool duke_map_file_read_from_filename(DukeMapFile *map, const char *filename)
{
    uint8_t header[22];
    uint8_t count_data[2];
    DukeMapFile *loaded;
    FILE *fp;
    uint16_t maxsectors;
    uint16_t maxwalls;
    uint16_t maxsprites;
    uint16_t i;

    if (map == NULL || filename == NULL) {
        return false;
    }

    duke_map_file_reset_last_error(map);

    fp = fopen(filename, "rb");
    if (fp == NULL) {
        snprintf(map->last_error, sizeof(map->last_error),
            "Unable to open map file: %s", filename);
        return false;
    }

    loaded = duke_map_file_new();
    if (loaded == NULL) {
        snprintf(map->last_error, sizeof(map->last_error),
            "Unable to allocate map data");
        fclose(fp);
        return false;
    }

    if (fread(header, sizeof(header), 1, fp) != 1) {
        snprintf(map->last_error, sizeof(map->last_error),
            "Unable to read map header");
        goto fail;
    }

    loaded->mapversion = read_i32_le(header + 0);
    loaded->posx = read_i32_le(header + 4);
    loaded->posy = read_i32_le(header + 8);
    loaded->posz = read_i32_le(header + 12);
    loaded->ang = read_i16_le(header + 16);
    loaded->cursectnum = read_i16_le(header + 18);
    loaded->numsectors = read_i16_le(header + 20);

    if (loaded->mapversion == 7) {
        maxsectors = MAPV7_MAXSECTORS;
        maxwalls = MAPV7_MAXWALLS;
        maxsprites = MAPV7_MAXSPRITES;
    } else if (loaded->mapversion == 8 || loaded->mapversion == 9) {
        maxsectors = MAPV8_MAXSECTORS;
        maxwalls = MAPV8_MAXWALLS;
        maxsprites = MAPV8_MAXSPRITES;
    } else {
        snprintf(map->last_error, sizeof(map->last_error),
            "Unsupported map version: %d", loaded->mapversion);
        goto fail;
    }

    if (loaded->numsectors < 0
        || (uint16_t)loaded->numsectors > maxsectors) {
        snprintf(map->last_error, sizeof(map->last_error),
            "Invalid sector count: %d", loaded->numsectors);
        goto fail;
    }

    if (loaded->numsectors > 0) {
        loaded->sectors = calloc((size_t)loaded->numsectors,
            sizeof(*loaded->sectors));
        if (loaded->sectors == NULL) {
            snprintf(map->last_error, sizeof(map->last_error),
                "Unable to allocate sectors");
            goto fail;
        }
    }

    for (i = 0; i < (uint16_t)loaded->numsectors; i++) {
        loaded->sectors[i] = duke_map_sector_new();
        if (loaded->sectors[i] == NULL
            || !duke_map_sector_read_from_file(loaded->sectors[i], fp)) {
            snprintf(map->last_error, sizeof(map->last_error),
                "Unable to read sector %u", i);
            goto fail;
        }
    }

    if (fread(count_data, sizeof(count_data), 1, fp) != 1) {
        snprintf(map->last_error, sizeof(map->last_error),
            "Unable to read wall count");
        goto fail;
    }
    loaded->numwalls = read_u16_le(count_data);
    if (loaded->numwalls > maxwalls) {
        snprintf(map->last_error, sizeof(map->last_error),
            "Invalid wall count: %u", loaded->numwalls);
        goto fail;
    }

    if (loaded->numwalls > 0) {
        loaded->walls = calloc(loaded->numwalls, sizeof(*loaded->walls));
        if (loaded->walls == NULL) {
            snprintf(map->last_error, sizeof(map->last_error),
                "Unable to allocate walls");
            goto fail;
        }
    }

    for (i = 0; i < loaded->numwalls; i++) {
        loaded->walls[i] = duke_map_wall_new();
        if (loaded->walls[i] == NULL
            || !duke_map_wall_read_from_file(loaded->walls[i], fp)) {
            snprintf(map->last_error, sizeof(map->last_error),
                "Unable to read wall %u", i);
            goto fail;
        }
    }

    if (fread(count_data, sizeof(count_data), 1, fp) != 1) {
        snprintf(map->last_error, sizeof(map->last_error),
            "Unable to read sprite count");
        goto fail;
    }
    loaded->numsprites = read_u16_le(count_data);
    if (loaded->numsprites > maxsprites) {
        snprintf(map->last_error, sizeof(map->last_error),
            "Invalid sprite count: %u", loaded->numsprites);
        goto fail;
    }

    if (loaded->numsprites > 0) {
        loaded->sprites = calloc(loaded->numsprites,
            sizeof(*loaded->sprites));
        if (loaded->sprites == NULL) {
            snprintf(map->last_error, sizeof(map->last_error),
                "Unable to allocate sprites");
            goto fail;
        }
    }

    for (i = 0; i < loaded->numsprites; i++) {
        loaded->sprites[i] = duke_map_sprite_new();
        if (loaded->sprites[i] == NULL
            || !duke_map_sprite_read_from_file(loaded->sprites[i], fp)) {
            snprintf(map->last_error, sizeof(map->last_error),
                "Unable to read sprite %u", i);
            goto fail;
        }
    }

    fclose(fp);
    duke_map_file_clear(map);
    *map = *loaded;
    free(loaded);
    return true;

fail:
    fclose(fp);
    duke_map_file_free(loaded);
    return false;
}

void duke_map_file_reset_last_error(DukeMapFile *map)
{
    if (map == NULL) {
        return;
    }

    map->last_error[0] = '\0';
}

void duke_map_file_free(DukeMapFile *map)
{
    if (map == NULL) {
        return;
    }

    duke_map_file_clear(map);
    free(map);
}

DukeMapSector* duke_map_sector_new(void)
{
    DukeMapSector *sector;

    sector = malloc(sizeof(DukeMapSector));
    if (sector == NULL) {
        return NULL;
    }

    sector->wallptr = -1;
    sector->wallnum = 0;
    sector->ceilingz = INT32_MIN;
    sector->floorz = INT32_MIN;
    sector->ceilingstat = 0;
    sector->floorstat = 0;
    sector->ceilingpicnum = -1;
    sector->ceilingheinum = 0;
    sector->ceilingshade = 0;
    sector->ceilingpal = 0;
    sector->ceilingxpanning = 0;
    sector->ceilingypanning = 0;
    sector->floorpicnum = -1;
    sector->floorheinum = 0;
    sector->floorshade = 0;
    sector->floorpal = 0;
    sector->floorxpanning = 0;
    sector->floorypanning = 0;
    sector->visibility = 0;
    sector->filler = 0;
    sector->lotag = 0;
    sector->hitag = 0;
    sector->extra = -1;

    return sector;
}

bool duke_map_sector_read_from_file(DukeMapSector *sector, FILE *fp)
{
    uint8_t data[40];

    if (sector == NULL || fp == NULL) {
        return false;
    }

    if (fread(data, sizeof(data), 1, fp) != 1) {
        return false;
    }

    sector->wallptr = read_i16_le(data + 0);
    sector->wallnum = read_i16_le(data + 2);
    sector->ceilingz = read_i32_le(data + 4);
    sector->floorz = read_i32_le(data + 8);
    sector->ceilingstat = read_i16_le(data + 12);
    sector->floorstat = read_i16_le(data + 14);
    sector->ceilingpicnum = read_i16_le(data + 16);
    sector->ceilingheinum = read_i16_le(data + 18);
    sector->ceilingshade = (int8_t)data[20];
    sector->ceilingpal = data[21];
    sector->ceilingxpanning = data[22];
    sector->ceilingypanning = data[23];
    sector->floorpicnum = read_i16_le(data + 24);
    sector->floorheinum = read_i16_le(data + 26);
    sector->floorshade = (int8_t)data[28];
    sector->floorpal = data[29];
    sector->floorxpanning = data[30];
    sector->floorypanning = data[31];
    sector->visibility = data[32];
    sector->filler = data[33];
    sector->lotag = read_i16_le(data + 34);
    sector->hitag = read_i16_le(data + 36);
    sector->extra = read_i16_le(data + 38);

    return true;
}

void duke_map_sector_free(DukeMapSector *sector)
{
    free(sector);
}

DukeMapWall* duke_map_wall_new(void)
{
    DukeMapWall *wall;

    wall = malloc(sizeof(DukeMapWall));
    if (wall == NULL) {
        return NULL;
    }

    wall->x = INT32_MIN;
    wall->y = INT32_MIN;
    wall->point2 = -1;
    wall->nextwall = -1;
    wall->nextsector = -1;
    wall->cstat = 0;
    wall->picnum = -1;
    wall->overpicnum = -1;
    wall->shade = 0;
    wall->pal = 0;
    wall->xrepeat = 0;
    wall->yrepeat = 0;
    wall->xpanning = 0;
    wall->ypanning = 0;
    wall->lotag = 0;
    wall->hitag = 0;
    wall->extra = -1;

    return wall;
}

bool duke_map_wall_read_from_file(DukeMapWall *wall, FILE *fp)
{
    uint8_t data[32];

    if (wall == NULL || fp == NULL) {
        return false;
    }

    if (fread(data, sizeof(data), 1, fp) != 1) {
        return false;
    }

    wall->x = read_i32_le(data + 0);
    wall->y = read_i32_le(data + 4);
    wall->point2 = read_i16_le(data + 8);
    wall->nextwall = read_i16_le(data + 10);
    wall->nextsector = read_i16_le(data + 12);
    wall->cstat = read_i16_le(data + 14);
    wall->picnum = read_i16_le(data + 16);
    wall->overpicnum = read_i16_le(data + 18);
    wall->shade = (int8_t)data[20];
    wall->pal = data[21];
    wall->xrepeat = data[22];
    wall->yrepeat = data[23];
    wall->xpanning = data[24];
    wall->ypanning = data[25];
    wall->lotag = read_i16_le(data + 26);
    wall->hitag = read_i16_le(data + 28);
    wall->extra = read_i16_le(data + 30);

    return true;
}

void duke_map_wall_free(DukeMapWall *wall)
{
    free(wall);
}

DukeMapSprite* duke_map_sprite_new(void)
{
    DukeMapSprite *sprite;

    sprite = malloc(sizeof(DukeMapSprite));
    if (sprite == NULL) {
        return NULL;
    }

    sprite->x = INT32_MIN;
    sprite->y = INT32_MIN;
    sprite->z = INT32_MIN;
    sprite->cstat = 0;
    sprite->picnum = -1;
    sprite->shade = 0;
    sprite->pal = 0;
    sprite->clipdist = 0;
    sprite->filler = 0;
    sprite->xrepeat = 0;
    sprite->yrepeat = 0;
    sprite->xoffset = 0;
    sprite->yoffset = 0;
    sprite->sectnum = -1;
    sprite->statnum = -1;
    sprite->ang = INT16_MIN;
    sprite->owner = -1;
    sprite->xvel = 0;
    sprite->yvel = 0;
    sprite->zvel = 0;
    sprite->lotag = 0;
    sprite->hitag = 0;
    sprite->extra = -1;

    return sprite;
}

bool duke_map_sprite_read_from_file(DukeMapSprite *sprite, FILE *fp)
{
    uint8_t data[44];

    if (sprite == NULL || fp == NULL) {
        return false;
    }

    if (fread(data, sizeof(data), 1, fp) != 1) {
        return false;
    }

    sprite->x = read_i32_le(data + 0);
    sprite->y = read_i32_le(data + 4);
    sprite->z = read_i32_le(data + 8);
    sprite->cstat = read_i16_le(data + 12);
    sprite->picnum = read_i16_le(data + 14);
    sprite->shade = (int8_t)data[16];
    sprite->pal = data[17];
    sprite->clipdist = data[18];
    sprite->filler = data[19];
    sprite->xrepeat = data[20];
    sprite->yrepeat = data[21];
    sprite->xoffset = (int8_t)data[22];
    sprite->yoffset = (int8_t)data[23];
    sprite->sectnum = read_i16_le(data + 24);
    sprite->statnum = read_i16_le(data + 26);
    sprite->ang = read_i16_le(data + 28);
    sprite->owner = read_i16_le(data + 30);
    sprite->xvel = read_i16_le(data + 32);
    sprite->yvel = read_i16_le(data + 34);
    sprite->zvel = read_i16_le(data + 36);
    sprite->lotag = read_i16_le(data + 38);
    sprite->hitag = read_i16_le(data + 40);
    sprite->extra = read_i16_le(data + 42);

    return true;
}

void duke_map_sprite_free(DukeMapSprite *sprite)
{
    free(sprite);
}

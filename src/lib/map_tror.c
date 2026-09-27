#include "libduke/map.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* Binary v9 compatibility encoding from EDuke32 build.h/engine.cpp.
 * Keep the raw records intact: loading and querying must be lossless. */
#define TROR_SURFACE_BIT 1024
#define TROR_WALL_BIT(surface) (1024 << (surface))

static bool invalid(DukeMapFile *map, const char *format, ...)
{
    if (map != NULL) {
        va_list args;
        va_start(args, format);
        vsnprintf(map->last_error, sizeof(map->last_error), format, args);
        va_end(args);
    }
    return false;
}

static bool valid_surface(DukeMapSurface surface)
{
    return surface == DUKE_MAP_CEILING || surface == DUKE_MAP_FLOOR;
}

static int raw_bunch(const DukeMapSector *sector, DukeMapSurface surface)
{
    int stat = surface == DUKE_MAP_CEILING ? sector->ceilingstat : sector->floorstat;
    return (stat & TROR_SURFACE_BIT)
        ? (surface == DUKE_MAP_CEILING ? sector->ceilingxpanning : sector->floorxpanning)
        : -1;
}

static int raw_link(const DukeMapWall *wall, DukeMapSurface surface)
{
    return (wall->cstat & TROR_WALL_BIT(surface))
        ? (surface == DUKE_MAP_CEILING ? wall->lotag : wall->extra) : -1;
}

int duke_map_sector_get_bunch(const DukeMapFile *map, int sector,
    DukeMapSurface surface)
{
    if (map == NULL || !valid_surface(surface) || sector < 0
        || sector >= map->numsectors || map->sectors == NULL
        || map->sectors[sector] == NULL
        || (map->mapversion != 7 && map->mapversion != 8 && map->mapversion != 9)) {
        return -2;
    }
    return map->mapversion == 9 ? raw_bunch(map->sectors[sector], surface) : -1;
}

int duke_map_wall_get_vertical_link(const DukeMapFile *map, int wall,
    DukeMapSurface surface)
{
    if (map == NULL || !valid_surface(surface) || wall < 0
        || wall >= map->numwalls || map->walls == NULL || map->walls[wall] == NULL
        || (map->mapversion != 7 && map->mapversion != 8 && map->mapversion != 9)) {
        return -2;
    }
    return map->mapversion == 9 ? raw_link(map->walls[wall], surface) : -1;
}

bool duke_map_sector_set_bunch(DukeMapFile *map, int sector,
    DukeMapSurface surface, int bunch)
{
    int16_t *stat;
    uint8_t *panning;
    if (duke_map_sector_get_bunch(map, sector, surface) == -2
        || map->mapversion != 9 || bunch < -1 || bunch >= MAPV9_MAXBUNCHES) {
        return invalid(map, "Invalid TROR bunch edit (requires version 9, valid sector/surface and bunch -1..255)");
    }
    stat = surface == DUKE_MAP_CEILING ? &map->sectors[sector]->ceilingstat : &map->sectors[sector]->floorstat;
    panning = surface == DUKE_MAP_CEILING ? &map->sectors[sector]->ceilingxpanning : &map->sectors[sector]->floorxpanning;
    if (bunch >= 0) {
        *stat |= TROR_SURFACE_BIT;
        *panning = (uint8_t)bunch;
    } else if (*stat & TROR_SURFACE_BIT) {
        *stat &= ~TROR_SURFACE_BIT;
        *panning = 0;
    }
    map->last_error[0] = '\0';
    return true;
}

bool duke_map_wall_set_vertical_link(DukeMapFile *map, int wall,
    DukeMapSurface surface, int target)
{
    DukeMapWall *record;
    int16_t *field;
    /* Do not use the returned raw link as an argument validity check: corrupt
     * on-disk values (including -2) must still be repairable with this API. */
    if (map == NULL || map->mapversion != 9 || !valid_surface(surface)
        || wall < 0 || wall >= map->numwalls || map->walls == NULL
        || map->walls[wall] == NULL || target < -1 || target >= map->numwalls
        || target > INT16_MAX || target == wall
        || (target >= 0 && map->walls[target] == NULL)) {
        return invalid(map, "Invalid TROR wall edit (requires version 9 and a valid, distinct target or -1)");
    }
    record = map->walls[wall];
    field = surface == DUKE_MAP_CEILING ? &record->lotag : &record->extra;
    if (target >= 0) {
        record->cstat |= TROR_WALL_BIT(surface);
        *field = (int16_t)target;
    } else if (record->cstat & TROR_WALL_BIT(surface)) {
        record->cstat &= ~TROR_WALL_BIT(surface);
        *field = surface == DUKE_MAP_CEILING ? 0 : -1;
    }
    map->last_error[0] = '\0';
    return true;
}

static bool has_markers(const DukeMapFile *map)
{
    for (int s = 0; s < map->numsectors; ++s) {
        if ((map->sectors[s]->ceilingstat | map->sectors[s]->floorstat) & TROR_SURFACE_BIT) {
            return true;
        }
    }
    for (int w = 0; w < map->numwalls; ++w) {
        if (map->walls[w]->cstat & (TROR_WALL_BIT(0) | TROR_WALL_BIT(1))) {
            return true;
        }
    }
    return false;
}

bool duke_map_file_validate_tror(DukeMapFile *map)
{
    unsigned members[2][MAPV9_MAXBUNCHES] = {{0}};
    int *owners;
    bool valid = false;

    if (!duke_map_file_validate_structure(map)) {
        return false;
    }
    if (!has_markers(map)) {
        return true;
    }
    if (map->mapversion != 9) {
        return invalid(map, "TROR connections require map version 9; explicitly clear TROR before downgrading");
    }
    /* This guarantees every point2 and owner lookup below is safe, and keeps
     * the permissive importer rules for effect-sector geometry. */
    if (!duke_map_file_validate_references(map)) {
        return false;
    }
    for (int s = 0; s < map->numsectors; ++s) {
        int ceiling = raw_bunch(map->sectors[s], DUKE_MAP_CEILING);
        int floor = raw_bunch(map->sectors[s], DUKE_MAP_FLOOR);
        if (ceiling >= 0 && ceiling == floor) {
            return invalid(map, "Sector %d belongs to both sides of TROR bunch %d", s, ceiling);
        }
        if (ceiling >= 0) {
            ++members[DUKE_MAP_CEILING][ceiling];
        }
        if (floor >= 0) {
            ++members[DUKE_MAP_FLOOR][floor];
        }
    }
    for (int b = 0; b < MAPV9_MAXBUNCHES; ++b) {
        if ((members[0][b] == 0) != (members[1][b] == 0)) {
            return invalid(map, "TROR bunch %d is missing ceiling or floor members", b);
        }
    }

    owners = malloc(((size_t)map->numwalls + 1) * sizeof(*owners));
    if (owners == NULL) {
        return invalid(map, "Unable to allocate TROR wall ownership data");
    }
    for (int s = 0; s < map->numsectors; ++s) {
        const DukeMapSector *sector = map->sectors[s];
        for (int w = sector->wallptr; w < sector->wallptr + sector->wallnum; ++w) {
            owners[w] = s;
        }
    }
    for (int w = 0; w < map->numwalls; ++w) {
        const DukeMapWall *wall = map->walls[w];
        for (int cf = 0; cf < 2; ++cf) {
            int target, bunch;
            const DukeMapWall *peer, *end, *peer_end;
            if (!(wall->cstat & TROR_WALL_BIT(cf))) {
                continue;
            }
            target = raw_link(wall, (DukeMapSurface)cf);
            if (target < 0 || target >= map->numwalls || target == w) {
                invalid(map, "Wall %d has invalid TROR %s link %d", w, cf == 0 ? "upper" : "lower", target);
                goto done;
            }
            peer = map->walls[target];
            if (raw_link(peer, (DukeMapSurface)(1 - cf)) != w) {
                invalid(map, "Wall %d TROR link to %d is not reciprocal", w, target);
                goto done;
            }
            bunch = raw_bunch(map->sectors[owners[w]], (DukeMapSurface)cf);
            if (owners[w] == owners[target] || bunch < 0
                || bunch != raw_bunch(map->sectors[owners[target]], (DukeMapSurface)(1 - cf))) {
                invalid(map, "Wall %d TROR link to %d has incompatible bunch membership", w, target);
                goto done;
            }
            /* Unlike horizontal portals, TROR peers have the same direction. */
            end = map->walls[wall->point2];
            peer_end = map->walls[peer->point2];
            if (wall->x != peer->x || wall->y != peer->y
                || end->x != peer_end->x || end->y != peer_end->y) {
                invalid(map, "Wall %d TROR link to %d has mismatched XY endpoints", w, target);
                goto done;
            }
        }
    }
    valid = true;
done:
    free(owners);
    return valid;
}

bool duke_map_file_set_version(DukeMapFile *map, int32_t version)
{
    DukeMapFile candidate;
    if (map == NULL) {
        return false;
    }
    candidate = *map;
    candidate.mapversion = version;
    if (!duke_map_file_validate_tror(&candidate)) {
        memcpy(map->last_error, candidate.last_error, sizeof(map->last_error));
        return false;
    }
    map->mapversion = version;
    map->last_error[0] = '\0';
    return true;
}

bool duke_map_file_clear_tror(DukeMapFile *map)
{
    if (!duke_map_file_validate_structure(map)) {
        return false;
    }
    for (int s = 0; s < map->numsectors; ++s) {
        DukeMapSector *sector = map->sectors[s];
        if (sector->ceilingstat & TROR_SURFACE_BIT) {
            sector->ceilingstat &= ~TROR_SURFACE_BIT;
            sector->ceilingxpanning = 0;
        }
        if (sector->floorstat & TROR_SURFACE_BIT) {
            sector->floorstat &= ~TROR_SURFACE_BIT;
            sector->floorxpanning = 0;
        }
    }
    for (int w = 0; w < map->numwalls; ++w) {
        DukeMapWall *wall = map->walls[w];
        if (wall->cstat & TROR_WALL_BIT(0)) {
            wall->cstat &= ~TROR_WALL_BIT(0);
            wall->lotag = 0;
        }
        if (wall->cstat & TROR_WALL_BIT(1)) {
            wall->cstat &= ~TROR_WALL_BIT(1);
            wall->extra = -1;
        }
    }
    return true;
}

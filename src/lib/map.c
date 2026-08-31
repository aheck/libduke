#include "libduke/map.h"

#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static int16_t read_i16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint16_t read_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static int32_t read_i32_le(const uint8_t *data)
{
    return (uint32_t)data[0]
        | ((uint32_t)data[1] << 8)
        | ((uint32_t)data[2] << 16)
        | ((uint32_t)data[3] << 24);
}

static void duke_map_file_clear(DukeMapFile *map)
{
    uint16_t i;

    // free all sectors
    if (map->sectors != NULL) {
        for (i = 0; i < (uint16_t)map->numsectors; i++) {
            duke_map_sector_free(map->sectors[i]);
        }
    }

    free(map->sectors);

    // free all walls
    if (map->walls != NULL) {
        for (i = 0; i < map->numwalls; i++) {
            duke_map_wall_free(map->walls[i]);
        }
    }

    free(map->walls);

    // free all sprites
    if (map->sprites != NULL) {
        for (i = 0; i < map->numsprites; i++) {
            duke_map_sprite_free(map->sprites[i]);
        }
    }

    free(map->sprites);

    // reset all primitive attributes
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

    //
    // Open the file
    //
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

    //
    // Read the map header from disk
    //
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

    //
    // Validate the map version and the limits for sectors, walls, and sprites
    //
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

    //
    // Validate the number of sectors
    //
    if (loaded->numsectors < 0
        || (uint16_t)loaded->numsectors > maxsectors) {
        snprintf(map->last_error, sizeof(map->last_error),
            "Invalid sector count: %d", loaded->numsectors);
        goto fail;
    }

    //
    //  Allocate memory for the sectors
    //
    if (loaded->numsectors > 0) {
        loaded->sectors = calloc((size_t)loaded->numsectors,
            sizeof(*loaded->sectors));
        if (loaded->sectors == NULL) {
            snprintf(map->last_error, sizeof(map->last_error),
                "Unable to allocate sectors");
            goto fail;
        }
    }

    //
    // Read the sectors form disk
    //
    for (i = 0; i < (uint16_t)loaded->numsectors; i++) {
        loaded->sectors[i] = duke_map_sector_new();
        if (loaded->sectors[i] == NULL
            || !duke_map_sector_read_from_file(loaded->sectors[i], fp)) {
            snprintf(map->last_error, sizeof(map->last_error),
                "Unable to read sector %u", i);
            goto fail;
        }
    }

    //
    // Read the number of walls from disk
    //
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

    //
    // Allocate memory for the walls
    //
    if (loaded->numwalls > 0) {
        loaded->walls = calloc(loaded->numwalls, sizeof(*loaded->walls));
        if (loaded->walls == NULL) {
            snprintf(map->last_error, sizeof(map->last_error),
                "Unable to allocate walls");
            goto fail;
        }
    }

    //
    // Read the walls from disk
    //
    for (i = 0; i < loaded->numwalls; i++) {
        loaded->walls[i] = duke_map_wall_new();
        if (loaded->walls[i] == NULL
            || !duke_map_wall_read_from_file(loaded->walls[i], fp)) {
            snprintf(map->last_error, sizeof(map->last_error),
                "Unable to read wall %u", i);
            goto fail;
        }
    }

    //
    // Read the number of sprites from disk
    //
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

    //
    // Read the number of sprites from disk
    //
    if (loaded->numsprites > 0) {
        loaded->sprites = calloc(loaded->numsprites,
            sizeof(*loaded->sprites));
        if (loaded->sprites == NULL) {
            snprintf(map->last_error, sizeof(map->last_error),
                "Unable to allocate sprites");
            goto fail;
        }
    }

    //
    // Read the sprites from disk
    //
    for (i = 0; i < loaded->numsprites; i++) {
        loaded->sprites[i] = duke_map_sprite_new();
        if (loaded->sprites[i] == NULL
            || !duke_map_sprite_read_from_file(loaded->sprites[i], fp)) {
            snprintf(map->last_error, sizeof(map->last_error),
                "Unable to read sprite %u", i);
            goto fail;
        }
    }

    //
    // Cleanup
    //
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

bool duke_map_file_validate(DukeMapFile *map)
{
    if (map == NULL) {
        return false;
    }

    duke_map_file_reset_last_error(map);

    /* Do cheaper prerequisite checks first. Each public validator repeats its
     * own prerequisites so that it is also safe to call independently. */
    return duke_map_file_validate_structure(map)
        && duke_map_file_validate_sector_wall_ownership(map)
        && duke_map_file_validate_wall_loops(map)
        && duke_map_file_validate_geometry(map)
        && duke_map_file_validate_portals(map)
        && duke_map_file_validate_vertical_sectors(map)
        && duke_map_file_validate_sprites(map)
        && duke_map_file_validate_start_position(map);
}

static bool map_invalid(DukeMapFile *map, const char *format, ...)
{
    va_list args;

    if (map != NULL) {
        va_start(args, format);
        vsnprintf(map->last_error, sizeof(map->last_error), format, args);
        va_end(args);
    }

    return false;
}

static bool map_limits(const DukeMapFile *map, uint16_t *maxsectors,
    uint16_t *maxwalls, uint16_t *maxsprites)
{
    if (map->mapversion == 7) {
        *maxsectors = MAPV7_MAXSECTORS;
        *maxwalls = MAPV7_MAXWALLS;
        *maxsprites = MAPV7_MAXSPRITES;
        return true;
    }

    if (map->mapversion == 8 || map->mapversion == 9) {
        *maxsectors = MAPV8_MAXSECTORS;
        *maxwalls = MAPV8_MAXWALLS;
        *maxsprites = MAPV8_MAXSPRITES;
        return true;
    }

    return false;
}

DukeMapSprite* duke_map_file_add_sprite(DukeMapFile *map)
{
    DukeMapSprite **sprites;
    DukeMapSprite *sprite;
    uint16_t maxsectors, maxwalls, maxsprites;

    if (map == NULL) {
        return NULL;
    }

    duke_map_file_reset_last_error(map);
    if (!map_limits(map, &maxsectors, &maxwalls, &maxsprites)) {
        map_invalid(map, "Unsupported map version: %d", map->mapversion);
        return NULL;
    }

    if (map->numsprites > 0 && map->sprites == NULL) {
        map_invalid(map, "Sprite array is NULL");
        return NULL;
    }

    if (map->numsprites >= maxsprites) {
        map_invalid(map, "Sprite limit reached: %u", maxsprites);
        return NULL;
    }

    sprite = duke_map_sprite_new();
    if (sprite == NULL) {
        map_invalid(map, "Unable to allocate sprite");
        return NULL;
    }

    sprites = realloc(map->sprites,
        ((size_t)map->numsprites + 1) * sizeof(*map->sprites));
    if (sprites == NULL) {
        duke_map_sprite_free(sprite);
        map_invalid(map, "Unable to grow sprite array");
        return NULL;
    }

    map->sprites = sprites;
    map->sprites[map->numsprites] = sprite;
    map->numsprites++;
    return sprite;
}

bool duke_map_file_remove_sprite(DukeMapFile *map, DukeMapSprite *sprite)
{
    DukeMapSprite **sprites;
    uint16_t i;

    if (map == NULL || sprite == NULL) {
        return false;
    }

    duke_map_file_reset_last_error(map);
    if (map->numsprites == 0 || map->sprites == NULL) {
        return map_invalid(map, "Sprite is not owned by map");
    }

    for (i = 0; i < map->numsprites; i++) {
        if (map->sprites[i] == sprite) {
            break;
        }
    }

    if (i == map->numsprites) {
        return map_invalid(map, "Sprite is not owned by map");
    }

    duke_map_sprite_free(sprite);
    if (i + 1 < map->numsprites) {
        memmove(&map->sprites[i], &map->sprites[i + 1],
            ((size_t)map->numsprites - i - 1) * sizeof(*map->sprites));
    }

    map->numsprites--;
    if (map->numsprites == 0) {
        free(map->sprites);
        map->sprites = NULL;
        return true;
    }

    /* Shrinking is optional: keep the valid original allocation if it fails. */
    sprites = realloc(map->sprites,
        (size_t)map->numsprites * sizeof(*map->sprites));
    if (sprites != NULL) {
        map->sprites = sprites;
    }

    return true;
}

bool duke_map_file_validate_structure(DukeMapFile *map)
{
    uint16_t maxsectors, maxwalls, maxsprites, i;

    if (map == NULL) {
        return false;
    }

    duke_map_file_reset_last_error(map);
    if (!map_limits(map, &maxsectors, &maxwalls, &maxsprites)) {
        return map_invalid(map, "Unsupported map version: %d", map->mapversion);
    }

    if (map->numsectors < 0 || (uint16_t)map->numsectors > maxsectors) {
        return map_invalid(map, "Invalid sector count: %d", map->numsectors);
    }

    if (map->numwalls > maxwalls) {
        return map_invalid(map, "Invalid wall count: %u", map->numwalls);
    }

    if (map->numsprites > maxsprites) {
        return map_invalid(map, "Invalid sprite count: %u", map->numsprites);
    }

    if (map->numsectors > 0 && map->sectors == NULL) {
        return map_invalid(map, "Sector array is NULL");
    }

    if (map->numwalls > 0 && map->walls == NULL) {
        return map_invalid(map, "Wall array is NULL");
    }

    if (map->numsprites > 0 && map->sprites == NULL) {
        return map_invalid(map, "Sprite array is NULL");
    }

    for (i = 0; i < (uint16_t)map->numsectors; i++) {
        if (map->sectors[i] == NULL) {
            return map_invalid(map, "Sector %u is NULL", i);
        }
    }

    for (i = 0; i < map->numwalls; i++) {
        if (map->walls[i] == NULL) {
            return map_invalid(map, "Wall %u is NULL", i);
        }
    }

    for (i = 0; i < map->numsprites; i++) {
        if (map->sprites[i] == NULL) {
            return map_invalid(map, "Sprite %u is NULL", i);
        }
    }

    return true;
}

static int map_wall_sector(const DukeMapFile *map, uint16_t wallnum)
{
    uint16_t i;

    for (i = 0; i < (uint16_t)map->numsectors; i++) {
        const DukeMapSector *sector = map->sectors[i];
        if (wallnum >= (uint16_t)sector->wallptr
            && wallnum < (uint16_t)(sector->wallptr + sector->wallnum)) {
            return (int)i;
        }
    }

    return -1;
}

bool duke_map_file_validate_sector_wall_ownership(DukeMapFile *map)
{
    uint16_t i;
    uint32_t expected = 0;

    if (!duke_map_file_validate_structure(map)) {
        return false;
    }

    /* Build stores each sector's walls in one contiguous slice. Requiring each
     * slice to begin where the previous one ended also rejects overlap, gaps,
     * and walls which are owned by more than one sector. */
    for (i = 0; i < (uint16_t)map->numsectors; i++) {
        DukeMapSector *sector = map->sectors[i];
        uint32_t end;

        if (sector->wallptr < 0 || sector->wallnum < 3) {
            return map_invalid(map,
                "Sector %u has invalid wall range (%d, %d)", i,
                sector->wallptr, sector->wallnum);
        }

        end = (uint32_t)(uint16_t)sector->wallptr
            + (uint32_t)(uint16_t)sector->wallnum;

        if ((uint32_t)(uint16_t)sector->wallptr != expected
            || end > map->numwalls) {
            return map_invalid(map,
                "Sector %u wall range is not contiguous or is out of bounds", i);
        }
        expected = end;
    }

    if (expected != map->numwalls) {
        return map_invalid(map, "%u wall(s) are not owned by a sector",
            (unsigned)(map->numwalls - expected));
    }

    return true;
}

bool duke_map_file_validate_wall_loops(DukeMapFile *map)
{
    uint16_t s;
    uint8_t *predecessors;

    if (!duke_map_file_validate_sector_wall_ownership(map)) {
        return false;
    }

    /* point2 must be a permutation within each sector. A valid in-range
     * permutation decomposes into one or more closed cycles without needing a
     * separate traversal or visited array. */
    predecessors = calloc(map->numwalls == 0 ? 1 : map->numwalls, 1);
    if (predecessors == NULL) {
        return map_invalid(map, "Unable to allocate wall validation data");
    }

    for (s = 0; s < (uint16_t)map->numsectors; s++) {
        DukeMapSector *sector = map->sectors[s];
        int32_t first = sector->wallptr;
        int32_t end = first + sector->wallnum;
        int32_t w;

        for (w = first; w < end; w++) {
            int32_t point2 = map->walls[w]->point2;
            if (point2 < first || point2 >= end) {
                free(predecessors);
                return map_invalid(map,
                    "Wall %d point2 %d is outside sector %u", w, point2, s);
            }

            if (++predecessors[point2] != 1) {
                free(predecessors);
                return map_invalid(map, "Wall %d has multiple predecessors",
                    point2);
            }
        }
    }

    for (s = 0; s < map->numwalls; s++) {
        if (predecessors[s] != 1) {
            free(predecessors);
            return map_invalid(map, "Wall %u has no predecessor", s);
        }
    }

    free(predecessors);
    return true;
}

static long double orient(const DukeMapWall *a, const DukeMapWall *b,
    const DukeMapWall *c)
{
    /* Coordinate differences can span the full int32_t range, so their cross
     * product does not fit safely in int64_t. */
    return ((long double)b->x - a->x) * ((long double)c->y - a->y)
        - ((long double)b->y - a->y) * ((long double)c->x - a->x);
}

static bool between(int32_t value, int32_t a, int32_t b)
{
    return value >= (a < b ? a : b) && value <= (a > b ? a : b);
}

static bool on_segment(const DukeMapWall *a, const DukeMapWall *b,
    const DukeMapWall *p)
{
    return orient(a, b, p) == 0.0L && between(p->x, a->x, b->x)
        && between(p->y, a->y, b->y);
}

static int sign_long_double(long double value)
{
    return (value > 0.0L) - (value < 0.0L);
}

static bool segments_intersect(const DukeMapWall *a, const DukeMapWall *b,
    const DukeMapWall *c, const DukeMapWall *d)
{
    long double o1 = orient(a, b, c), o2 = orient(a, b, d);
    long double o3 = orient(c, d, a), o4 = orient(c, d, b);

    /* Opposite orientations are a proper crossing. The remaining tests also
     * catch collinear overlap and one segment touching the middle of another. */
    if (sign_long_double(o1) != sign_long_double(o2)
        && sign_long_double(o3) != sign_long_double(o4)) {
        return true;
    }
    return (o1 == 0.0L && on_segment(a, b, c))
        || (o2 == 0.0L && on_segment(a, b, d))
        || (o3 == 0.0L && on_segment(c, d, a))
        || (o4 == 0.0L && on_segment(c, d, b));
}

static bool point_in_sector(const DukeMapFile *map, uint16_t sectnum,
    int32_t x, int32_t y, bool include_boundary)
{
    const DukeMapSector *sector = map->sectors[sectnum];
    int32_t first = sector->wallptr, end = first + sector->wallnum, w;
    bool inside = false;
    DukeMapWall point = { .x = x, .y = y };

    /* Odd-even ray casting works across every point2 cycle at once. Crossing a
     * hole boundary toggles the result back to outside. */
    for (w = first; w < end; w++) {
        const DukeMapWall *a = map->walls[w];
        const DukeMapWall *b = map->walls[a->point2];
        if (on_segment(a, b, &point)) {
            return include_boundary;
        }
        if ((a->y > y) != (b->y > y)) {
            long double at_x = a->x + ((long double)y - a->y)
                * ((long double)b->x - a->x) / ((long double)b->y - a->y);
            if (at_x > x) {
                inside = !inside;
            }
        }
    }
    return inside;
}

bool duke_map_file_validate_geometry(DukeMapFile *map)
{
    uint16_t s;

    if (!duke_map_file_validate_wall_loops(map)) {
        return false;
    }

    for (s = 0; s < (uint16_t)map->numsectors; s++) {
        DukeMapSector *sector = map->sectors[s];
        int32_t first = sector->wallptr, end = first + sector->wallnum;
        int32_t w, q;

        for (w = first; w < end; w++) {
            DukeMapWall *a = map->walls[w];
            DukeMapWall *b = map->walls[a->point2];

            if (a->x == b->x && a->y == b->y) {
                return map_invalid(map, "Wall %d has zero length", w);
            }

            for (q = w + 1; q < end; q++) {
                DukeMapWall *c = map->walls[q];
                DukeMapWall *d = map->walls[c->point2];
                /* Consecutive edges necessarily meet at their shared vertex. */
                if (a->point2 == q || c->point2 == w) {
                    continue;
                }
                if (segments_intersect(a, b, c, d)) {
                    return map_invalid(map,
                        "Walls %d and %d intersect in sector %u", w, q, s);
                }
            }
        }

        /* Every point2 cycle must have at least three vertices and nonzero area.
         * Starting from every wall avoids allocating another visited array;
         * only the lowest-numbered wall in a cycle performs the checks. */
        for (w = first; w < end; w++) {
            int32_t current = w, count = 0;
            long double area = 0.0L;
            bool lowest = true;
            do {
                DukeMapWall *a = map->walls[current];
                DukeMapWall *b = map->walls[a->point2];
                if (current < w) {
                    lowest = false;
                }
                area += (long double)a->x * b->y
                    - (long double)b->x * a->y;
                current = a->point2;
                count++;
            } while (current != w && count <= sector->wallnum);
            if (lowest) {
                if (count < 3 || area == 0.0L) {
                    return map_invalid(map,
                        "Wall loop beginning at %d is degenerate", w);
                }
                /* Build's Y axis points down on the editor map: the outer loop
                 * therefore has positive signed area and holes have negative
                 * signed area. wallptr identifies the outer loop. */
                if ((w == first && area < 0.0L)
                    || (w != first && area > 0.0L)) {
                    return map_invalid(map,
                        "Wall loop beginning at %d has invalid winding", w);
                }
            }
        }
    }
    return true;
}

bool duke_map_file_validate_portals(DukeMapFile *map)
{
    uint16_t w;

    if (!duke_map_file_validate_wall_loops(map)) {
        return false;
    }

    for (w = 0; w < map->numwalls; w++) {
        DukeMapWall *wall = map->walls[w];
        int owner = map_wall_sector(map, w);
        int32_t q = wall->nextwall, target = wall->nextsector;
        DukeMapWall *other;

        if (q == -1 && target == -1) {
            continue;
        }

        /* Any other combination must describe both halves of a portal. This
         * also rejects half-portals where just one field is -1. */
        if (q < 0 || q >= map->numwalls || target < 0
            || target >= map->numsectors) {
            return map_invalid(map, "Wall %u has an invalid portal reference", w);
        }

        other = map->walls[q];

        if (map_wall_sector(map, (uint16_t)q) != target
            || other->nextwall != (int16_t)w
            || other->nextsector != owner) {
            return map_invalid(map, "Wall %u portal is not reciprocal", w);
        }

        /* The two walls describe the same segment in opposite directions. */
        if (wall->x != map->walls[other->point2]->x
            || wall->y != map->walls[other->point2]->y
            || map->walls[wall->point2]->x != other->x
            || map->walls[wall->point2]->y != other->y) {
            return map_invalid(map,
                "Wall %u portal endpoints do not match wall %d", w, q);
        }
    }
    return true;
}

static long double sector_surface_z(const DukeMapFile *map, uint16_t sectnum,
    bool floor, int32_t x, int32_t y)
{
    DukeMapSector *sector = map->sectors[sectnum];
    DukeMapWall *a = map->walls[sector->wallptr];
    DukeMapWall *b = map->walls[a->point2];
    int16_t stat = floor ? sector->floorstat : sector->ceilingstat;
    int16_t heinum = floor ? sector->floorheinum : sector->ceilingheinum;
    long double z = floor ? sector->floorz : sector->ceilingz;
    long double dx, dy, length, cross;

    if ((stat & 2) == 0) {
        return z;
    }

    /* A Build slope is hinged on the sector's first wall. The signed cross
     * product is the perpendicular displacement from that hinge. */
    dx = (long double)b->x - a->x;
    dy = (long double)b->y - a->y;
    length = sqrtl(dx * dx + dy * dy);
    cross = dx * ((long double)y - a->y) - dy * ((long double)x - a->x);
    /* Build computes cross/8 divided by (wall length)*32. */
    return z + heinum * cross / (length * 256.0L);
}

bool duke_map_file_validate_vertical_sectors(DukeMapFile *map)
{
    uint16_t s;

    if (!duke_map_file_validate_geometry(map)) {
        return false;
    }
    for (s = 0; s < (uint16_t)map->numsectors; s++) {
        DukeMapSector *sector = map->sectors[s];
        int32_t w, end = sector->wallptr + sector->wallnum;
        /* Ceiling and floor slopes are affine planes. Their difference is also
         * affine, so checking every polygon vertex is sufficient to establish
         * the ordering throughout the sector. */
        for (w = sector->wallptr; w < end; w++) {
            DukeMapWall *point = map->walls[w];
            if (sector_surface_z(map, s, false, point->x, point->y)
                > sector_surface_z(map, s, true, point->x, point->y)) {
                return map_invalid(map,
                    "Sector %u ceiling is below its floor at wall %d", s, w);
            }
        }
    }
    return true;
}

bool duke_map_file_validate_sprites(DukeMapFile *map)
{
    uint16_t i;

    if (!duke_map_file_validate_geometry(map)) {
        return false;
    }

    for (i = 0; i < map->numsprites; i++) {
        DukeMapSprite *sprite = map->sprites[i];
        if (sprite->sectnum < 0 || sprite->sectnum >= map->numsectors) {
            return map_invalid(map, "Sprite %u has invalid sector %d", i,
                sprite->sectnum);
        }

        /* Points on a wall are accepted; Build may subsequently move the
         * sprite into either adjoining sector. */
        if (!point_in_sector(map, (uint16_t)sprite->sectnum,
                sprite->x, sprite->y, true)) {
            return map_invalid(map, "Sprite %u is outside sector %d", i,
                sprite->sectnum);
        }

        if (sprite->statnum < 0 || sprite->statnum >= MAP_MAXSTATUS) {
            return map_invalid(map, "Sprite %u has invalid status %d", i,
                sprite->statnum);
        }

        if (sprite->ang < 0 || sprite->ang >= 2048) {
            return map_invalid(map, "Sprite %u has invalid angle %d", i,
                sprite->ang);
        }
    }
    return true;
}

bool duke_map_file_validate_start_position(DukeMapFile *map)
{
    long double ceiling, floor;

    if (!duke_map_file_validate_vertical_sectors(map)) {
        return false;
    }

    if (map->cursectnum < 0 || map->cursectnum >= map->numsectors) {
        return map_invalid(map, "Invalid starting sector: %d", map->cursectnum);
    }

    if (!point_in_sector(map, (uint16_t)map->cursectnum,
            map->posx, map->posy, true)) {
        return map_invalid(map, "Starting position is outside sector %d",
            map->cursectnum);
    }

    ceiling = sector_surface_z(map, (uint16_t)map->cursectnum, false,
        map->posx, map->posy);
    floor = sector_surface_z(map, (uint16_t)map->cursectnum, true,
        map->posx, map->posy);

    if (map->posz < ceiling || map->posz > floor) {
        return map_invalid(map,
            "Starting Z coordinate is outside sector %d", map->cursectnum);
    }

    if (map->ang < 0 || map->ang >= 2048) {
        return map_invalid(map, "Invalid starting angle: %d", map->ang);
    }

    return true;
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

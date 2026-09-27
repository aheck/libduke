#include "libduke/map.h"
#include "test_platform.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static char first_path[128], second_path[128];

static void require_map(bool result, const DukeMapFile *map)
{
    if (!result) {
        fprintf(stderr, "Map operation failed: %s\n", map->last_error);
        abort();
    }
}

/* Three vertically connected rooms exercise both directions on one wall and
 * both ends of the binary bunch-ID range, without relying on the serializer
 * to establish the fixture's encoding. */
static DukeMapFile *stacked_map(void)
{
    static const int32_t xy[4][2] = {{0,0}, {1024,0}, {1024,1024}, {0,1024}};
    DukeMapFile *map = duke_map_file_new();
    assert(map != NULL);
    map->mapversion = 9;
    map->numsectors = 3;
    map->numwalls = 12;
    map->sectors = calloc(3, sizeof(*map->sectors));
    map->walls = calloc(12, sizeof(*map->walls));
    assert(map->sectors != NULL && map->walls != NULL);
    map->posx = map->posy = map->posz = 512;
    map->cursectnum = map->ang = 0;
    for (int s = 0; s < 3; ++s) {
        DukeMapSector *sector = map->sectors[s] = duke_map_sector_new();
        assert(sector != NULL);
        sector->wallptr = (int16_t)(s * 4);
        sector->wallnum = 4;
        sector->ceilingz = s * 1024;
        sector->floorz = (s + 1) * 1024;
        sector->ceilingstat = sector->floorstat = 16;
        sector->ceilingxpanning = sector->floorxpanning = 37;
        sector->ceilingypanning = sector->floorypanning = 43;
        if (s > 0) {
            sector->ceilingstat |= 1024;
            sector->ceilingxpanning = s == 1 ? 0 : 255;
        }
        if (s < 2) {
            sector->floorstat |= 1024;
            sector->floorxpanning = s == 0 ? 0 : 255;
        }
        for (int v = 0; v < 4; ++v) {
            int w = s * 4 + v;
            DukeMapWall *wall = map->walls[w] = duke_map_wall_new();
            assert(wall != NULL);
            wall->x = xy[v][0];
            wall->y = xy[v][1];
            wall->point2 = (int16_t)(s * 4 + (v + 1) % 4);
            wall->cstat = 8;
            wall->lotag = 123;
            wall->extra = 456;
            wall->hitag = 789;
            if (s > 0) {
                wall->cstat |= 1024;
                wall->lotag = (int16_t)(w - 4);
            }
            if (s < 2) {
                wall->cstat |= 2048;
                wall->extra = (int16_t)(w + 4);
            }
        }
    }
    return map;
}

static void same_file(const char *a, const char *b)
{
    FILE *left = fopen(a, "rb"), *right = fopen(b, "rb");
    int x, y;
    assert(left != NULL && right != NULL);
    do {
        x = fgetc(left);
        y = fgetc(right);
        assert(x == y);
    } while (x != EOF);
    assert(!ferror(left) && !ferror(right));
    fclose(left);
    fclose(right);
}

static void test_accessors(void)
{
    DukeMapFile *map = stacked_map();
    DukeMapSector before;
    DukeMapWall wall_before;
    require_map(duke_map_file_validate_tror(map), map);
    require_map(duke_map_file_validate(map), map);
    assert(duke_map_sector_get_bunch(map, 0, DUKE_MAP_CEILING) == -1);
    assert(duke_map_sector_get_bunch(map, 1, DUKE_MAP_CEILING) == 0);
    assert(duke_map_sector_get_bunch(map, 1, DUKE_MAP_FLOOR) == 255);
    assert(duke_map_wall_get_vertical_link(map, 4, DUKE_MAP_CEILING) == 0);
    assert(duke_map_wall_get_vertical_link(map, 4, DUKE_MAP_FLOOR) == 8);
    assert(duke_map_wall_get_vertical_link(map, 0, DUKE_MAP_CEILING) == -1);
    assert(duke_map_sector_get_bunch(NULL, 0, DUKE_MAP_CEILING) == -2);
    assert(duke_map_sector_get_bunch(map, -1, DUKE_MAP_CEILING) == -2);
    assert(duke_map_sector_get_bunch(map, 3, DUKE_MAP_CEILING) == -2);
    assert(duke_map_wall_get_vertical_link(map, 12, DUKE_MAP_FLOOR) == -2);
    assert(duke_map_wall_get_vertical_link(map, 0, (DukeMapSurface)2) == -2);
    assert(!duke_map_sector_set_bunch(NULL, 0, DUKE_MAP_CEILING, 0));
    assert(!duke_map_wall_set_vertical_link(NULL, 0, DUKE_MAP_CEILING, 0));

    before = *map->sectors[1];
    assert(!duke_map_sector_set_bunch(map, 1, DUKE_MAP_FLOOR, 256));
    assert(!duke_map_sector_set_bunch(map, 1, DUKE_MAP_FLOOR, -2));
    assert(memcmp(&before, map->sectors[1], sizeof(before)) == 0);
    wall_before = *map->walls[4];
    assert(!duke_map_wall_set_vertical_link(map, 4, DUKE_MAP_CEILING, 12));
    assert(!duke_map_wall_set_vertical_link(map, 4, DUKE_MAP_CEILING, 4));
    assert(memcmp(&wall_before, map->walls[4], sizeof(wall_before)) == 0);

    require_map(duke_map_sector_set_bunch(map, 1, DUKE_MAP_CEILING, -1), map);
    assert(map->sectors[1]->ceilingstat == 16 && map->sectors[1]->ceilingxpanning == 0);
    assert(map->sectors[1]->ceilingypanning == 43 && map->sectors[1]->floorxpanning == 255);
    require_map(duke_map_sector_set_bunch(map, 1, DUKE_MAP_CEILING, 0), map);
    assert(memcmp(&before, map->sectors[1], sizeof(before)) == 0);
    require_map(duke_map_wall_set_vertical_link(map, 4, DUKE_MAP_CEILING, -1), map);
    assert(map->walls[4]->cstat == (8 | 2048) && map->walls[4]->lotag == 0);
    assert(map->walls[4]->extra == 8 && map->walls[4]->hitag == 789);
    /* Repair a malformed raw value as well as a normally disconnected link. */
    map->walls[4]->cstat |= 1024;
    map->walls[4]->lotag = -2;
    require_map(duke_map_wall_set_vertical_link(map, 4, DUKE_MAP_CEILING, 0), map);
    assert(memcmp(&wall_before, map->walls[4], sizeof(wall_before)) == 0);
    require_map(duke_map_file_validate_tror(map), map);
    duke_map_file_free(map);
}

static void test_corruption(void)
{
    for (int fault = 0; fault < 10; ++fault) {
        DukeMapFile *map = stacked_map();
        switch (fault) {
        case 0: map->walls[0]->extra = 12; break;
        case 1: map->walls[0]->extra = -1; break;
        case 2: map->walls[4]->cstat &= ~1024; break;
        case 3: map->walls[0]->extra = 0; break;
        case 4: map->sectors[1]->ceilingxpanning = 23; break;
        case 5: map->sectors[1]->floorxpanning = 0; break;
        case 6: map->walls[4]->x += 1; break;
        case 7: map->walls[0]->point2 = 12; break;
        case 8: map->sectors[2]->wallptr = 4; break;
        case 9:
            map->sectors[0]->floorstat &= ~1024;
            map->sectors[1]->ceilingstat &= ~1024;
            break;
        }
        assert(!duke_map_file_validate_tror(map));
        assert(map->last_error[0] != '\0');
        assert(!duke_map_file_validate(map));
        duke_map_file_free(map);
    }
    assert(!duke_map_file_validate_tror(NULL));
}

static void test_one_to_many_bunch(void)
{
    /* One upper room connects to two lower rooms with an ordinary portal
     * between them. Their internal divider needs no vertical wall links. */
    static const int xy[14][2] = {
        {0,0}, {512,0}, {1024,0}, {1024,1024}, {512,1024}, {0,1024},
        {0,0}, {512,0}, {512,1024}, {0,1024},
        {512,0}, {1024,0}, {1024,1024}, {512,1024}
    };
    static const int peers[6] = {6,10,11,12,8,9};
    DukeMapFile *map = duke_map_file_new();
    assert(map != NULL);
    map->mapversion = 9;
    map->numsectors = 3;
    map->numwalls = 14;
    map->sectors = calloc(3, sizeof(*map->sectors));
    map->walls = calloc(14, sizeof(*map->walls));
    assert(map->sectors != NULL && map->walls != NULL);
    for (int s = 0; s < 3; ++s) {
        map->sectors[s] = duke_map_sector_new();
        assert(map->sectors[s] != NULL);
        map->sectors[s]->wallptr = (int16_t)(s == 0 ? 0 : 2 + 4 * s);
        map->sectors[s]->wallnum = (int16_t)(s == 0 ? 6 : 4);
        map->sectors[s]->ceilingz = s == 0 ? 0 : 1024;
        map->sectors[s]->floorz = s == 0 ? 1024 : 2048;
        require_map(duke_map_sector_set_bunch(map, s,
            s == 0 ? DUKE_MAP_FLOOR : DUKE_MAP_CEILING, 17), map);
    }
    for (int w = 0; w < 14; ++w) {
        map->walls[w] = duke_map_wall_new();
        assert(map->walls[w] != NULL);
        map->walls[w]->x = xy[w][0];
        map->walls[w]->y = xy[w][1];
        map->walls[w]->point2 = (int16_t)(w == 5 ? 0 : w == 9 ? 6 : w == 13 ? 10 : w + 1);
    }
    map->walls[7]->nextwall = 13;
    map->walls[7]->nextsector = 2;
    map->walls[13]->nextwall = 7;
    map->walls[13]->nextsector = 1;
    for (int w = 0; w < 6; ++w) {
        require_map(duke_map_wall_set_vertical_link(map, w, DUKE_MAP_FLOOR, peers[w]), map);
        require_map(duke_map_wall_set_vertical_link(map, peers[w], DUKE_MAP_CEILING, w), map);
    }
    require_map(duke_map_file_validate_tror(map), map);
    assert(duke_map_wall_get_vertical_link(map, 7, DUKE_MAP_CEILING) == -1);
    require_map(duke_map_file_write_to_filename(map, first_path), map);
    require_map(duke_map_file_read_from_filename(map, first_path), map);
    require_map(duke_map_file_write_to_filename(map, second_path), map);
    same_file(first_path, second_path);
    duke_map_file_free(map);
}

static void test_io_and_downgrade(void)
{
    DukeMapFile *map = stacked_map(), *loaded = duke_map_file_new();
    require_map(duke_map_file_write_to_filename(map, first_path), map);
    require_map(duke_map_file_read_from_filename(loaded, first_path), loaded);
    require_map(duke_map_file_write_to_filename(loaded, second_path), loaded);
    same_file(first_path, second_path);
    assert(loaded->mapversion == 9);
    assert(duke_map_sector_get_bunch(loaded, 2, DUKE_MAP_CEILING) == 255);
    assert(duke_map_wall_get_vertical_link(loaded, 4, DUKE_MAP_FLOOR) == 8);
    for (int version = 7; version <= 8; ++version) {
        assert(!duke_map_file_set_version(map, version));
        assert(map->mapversion == 9);
        map->mapversion = version; /* Direct field writes must not bypass safety. */
        assert(!duke_map_file_write_to_filename(map, first_path));
        same_file(first_path, second_path); /* No truncation on validation failure. */
        assert(duke_map_sector_get_bunch(map, 1, DUKE_MAP_CEILING) == -1);
        assert(duke_map_wall_get_vertical_link(map, 4, DUKE_MAP_CEILING) == -1);
        assert(!duke_map_sector_set_bunch(map, 1, DUKE_MAP_CEILING, 0));
        assert(!duke_map_wall_set_vertical_link(map, 4, DUKE_MAP_CEILING, 0));
        map->mapversion = 9;
    }
    map->walls[0]->extra = 12;
    assert(!duke_map_file_write_to_filename(map, first_path));
    same_file(first_path, second_path);
    map->walls[0]->extra = 4;

    /* Corrupt an actual on-disk link and verify that failed import preserves
     * the destination object, not just its counts. */
    {
        FILE *file = fopen(first_path, "r+b");
        const unsigned char bad_link[2] = {12, 0};
        DukeMapSector **sectors = loaded->sectors;
        DukeMapWall **walls = loaded->walls;
        assert(file != NULL);
        assert(fseek(file, 22 + 3 * 40 + 2 + 30, SEEK_SET) == 0);
        assert(fwrite(bad_link, 1, 2, file) == 2);
        assert(fclose(file) == 0);
        assert(!duke_map_file_read_from_filename(loaded, first_path));
        assert(strstr(loaded->last_error, "TROR") != NULL);
        assert(loaded->sectors == sectors && loaded->walls == walls);
        require_map(duke_map_file_write_to_filename(loaded, first_path), loaded);
        same_file(first_path, second_path);
    }

    map->mapversion = 8;
    require_map(duke_map_file_clear_tror(map), map);
    assert(map->mapversion == 8);
    assert(map->sectors[0]->ceilingxpanning == 37); /* Unmarked fields survive. */
    assert(map->sectors[0]->floorxpanning == 0);
    assert(map->walls[0]->lotag == 123 && map->walls[0]->extra == -1);
    assert(map->walls[8]->lotag == 0 && map->walls[8]->extra == 456);
    assert(map->walls[4]->cstat == 8 && map->walls[4]->hitag == 789);
    for (int version = 7; version <= 9; ++version) {
        require_map(duke_map_file_set_version(map, version), map);
        require_map(duke_map_file_write_to_filename(map, first_path), map);
        require_map(duke_map_file_read_from_filename(loaded, first_path), loaded);
        assert(loaded->mapversion == version);
        require_map(duke_map_file_write_to_filename(loaded, second_path), loaded);
        same_file(first_path, second_path);
    }
    assert(!duke_map_file_set_version(map, 10) && map->mapversion == 9);
    /* The limit check precedes pointer traversal. */
    map->numwalls = MAPV7_MAXWALLS + 1;
    assert(!duke_map_file_set_version(map, 7) && map->mapversion == 9);
    map->numwalls = 12;
    duke_map_file_free(loaded);
    duke_map_file_free(map);
}

/* Optional external Mapster32 fixtures are never modified or redistributed. */
static void test_external_fixture(const char *path)
{
    DukeMapFile *map = duke_map_file_new();
    DukeMapFile *loaded = duke_map_file_new();
    int connections = 0;
    require_map(duke_map_file_read_from_filename(map, path), map);
    assert(map->mapversion == 9);
    require_map(duke_map_file_validate_tror(map), map);
    for (int s = 0; s < map->numsectors; ++s) {
        if (duke_map_sector_get_bunch(map, s, DUKE_MAP_CEILING) >= 0) {
            ++connections;
        }
    }
    assert(connections > 0);
    require_map(duke_map_file_write_to_filename(map, first_path), map);
    same_file(path, first_path);
    require_map(duke_map_file_read_from_filename(loaded, first_path), loaded);
    require_map(duke_map_file_write_to_filename(loaded, second_path), loaded);
    same_file(path, second_path);
    printf("TROR fixture passed: %s (%d sectors, %u walls, %d extended ceilings)\n",
        path, map->numsectors, map->numwalls, connections);
    duke_map_file_free(loaded);
    duke_map_file_free(map);
}

int main(int argc, char **argv)
{
    snprintf(first_path, sizeof(first_path), "libduke-tror-%ld-a.map", (long)test_process_id());
    snprintf(second_path, sizeof(second_path), "libduke-tror-%ld-b.map", (long)test_process_id());
    test_accessors();
    test_corruption();
    test_one_to_many_bunch();
    test_io_and_downgrade();
    for (int i = 1; i < argc; ++i) {
        test_external_fixture(argv[i]);
    }
    assert(remove(first_path) == 0);
    assert(remove(second_path) == 0);
    return 0;
}

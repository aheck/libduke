#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libduke/map.h"

static DukeMapFile *square_map(void)
{
    static const int32_t coordinates[4][2] = {
        { 0, 0 }, { 1024, 0 }, { 1024, 1024 }, { 0, 1024 }
    };
    DukeMapFile *map = duke_map_file_new();
    unsigned i;

    ck_assert_ptr_nonnull(map);
    map->mapversion = 7;
    map->numsectors = 1;
    map->numwalls = 4;
    map->sectors = calloc(1, sizeof(*map->sectors));
    map->walls = calloc(4, sizeof(*map->walls));
    ck_assert_ptr_nonnull(map->sectors);
    ck_assert_ptr_nonnull(map->walls);

    map->sectors[0] = duke_map_sector_new();
    ck_assert_ptr_nonnull(map->sectors[0]);
    map->sectors[0]->wallptr = 0;
    map->sectors[0]->wallnum = 4;
    map->sectors[0]->ceilingz = 0;
    map->sectors[0]->floorz = 1024;

    for (i = 0; i < 4; i++) {
        map->walls[i] = duke_map_wall_new();
        ck_assert_ptr_nonnull(map->walls[i]);
        map->walls[i]->x = coordinates[i][0];
        map->walls[i]->y = coordinates[i][1];
        map->walls[i]->point2 = (int16_t)((i + 1) % 4);
    }

    map->posx = 512;
    map->posy = 512;
    map->posz = 512;
    map->ang = 0;
    map->cursectnum = 0;
    return map;
}

static DukeMapSprite *add_sprite(DukeMapFile *map, int32_t x, int32_t y)
{
    map->numsprites = 1;
    map->sprites = calloc(1, sizeof(*map->sprites));
    ck_assert_ptr_nonnull(map->sprites);
    map->sprites[0] = duke_map_sprite_new();
    ck_assert_ptr_nonnull(map->sprites[0]);
    map->sprites[0]->x = x;
    map->sprites[0]->y = y;
    map->sprites[0]->z = 512;
    map->sprites[0]->sectnum = 0;
    map->sprites[0]->statnum = 0;
    map->sprites[0]->ang = 0;
    return map->sprites[0];
}

static DukeMapFile *adjacent_sectors_map(void)
{
    static const int32_t coordinates[8][2] = {
        { 0, 0 }, { 100, 0 }, { 100, 100 }, { 0, 100 },
        { 100, 0 }, { 200, 0 }, { 200, 100 }, { 100, 100 }
    };
    DukeMapFile *map = square_map();
    unsigned i;

    duke_map_sector_free(map->sectors[0]);
    free(map->sectors);
    for (i = 0; i < map->numwalls; i++) {
        duke_map_wall_free(map->walls[i]);
    }
    free(map->walls);

    map->numsectors = 2;
    map->numwalls = 8;
    map->sectors = calloc(2, sizeof(*map->sectors));
    map->walls = calloc(8, sizeof(*map->walls));
    ck_assert_ptr_nonnull(map->sectors);
    ck_assert_ptr_nonnull(map->walls);
    for (i = 0; i < 2; i++) {
        map->sectors[i] = duke_map_sector_new();
        ck_assert_ptr_nonnull(map->sectors[i]);
        map->sectors[i]->wallptr = (int16_t)(i * 4);
        map->sectors[i]->wallnum = 4;
        map->sectors[i]->ceilingz = 0;
        map->sectors[i]->floorz = 1024;
    }
    for (i = 0; i < 8; i++) {
        unsigned base = (i / 4) * 4;
        map->walls[i] = duke_map_wall_new();
        ck_assert_ptr_nonnull(map->walls[i]);
        map->walls[i]->x = coordinates[i][0];
        map->walls[i]->y = coordinates[i][1];
        map->walls[i]->point2 = (int16_t)(base + ((i + 1) % 4));
    }
    map->walls[1]->nextwall = 7;
    map->walls[1]->nextsector = 1;
    map->walls[7]->nextwall = 1;
    map->walls[7]->nextsector = 0;
    map->posx = 50;
    map->posy = 50;
    return map;
}

START_TEST(test_valid_map)
{
    DukeMapFile *map = square_map();

    ck_assert(duke_map_file_validate(map));
    ck_assert_str_eq(map->last_error, "");
    duke_map_file_free(map);
}
END_TEST

START_TEST(test_map_write_and_read_round_trip)
{
    char filename[128];
    DukeMapFile *map = square_map();
    DukeMapFile *loaded = duke_map_file_new();
    DukeMapSprite *sprite = duke_map_file_add_sprite(map);

    snprintf(filename, sizeof(filename), "/tmp/libduke-map-%ld.map",
        (long)getpid());
    ck_assert_ptr_nonnull(loaded);
    ck_assert_ptr_nonnull(sprite);
    sprite->x = 256;
    sprite->y = 768;
    sprite->z = 512;
    sprite->picnum = 123;
    sprite->shade = -7;
    sprite->sectnum = 0;
    sprite->statnum = 0;
    sprite->ang = 1024;

    ck_assert(duke_map_file_write_to_filename(map, filename));
    ck_assert_str_eq(map->last_error, "");
    ck_assert(duke_map_file_read_from_filename(loaded, filename));
    ck_assert_int_eq(loaded->mapversion, map->mapversion);
    ck_assert_int_eq(loaded->numsectors, map->numsectors);
    ck_assert_uint_eq(loaded->numwalls, map->numwalls);
    ck_assert_uint_eq(loaded->numsprites, 1);
    ck_assert_int_eq(loaded->walls[2]->x, map->walls[2]->x);
    ck_assert_int_eq(loaded->sprites[0]->x, 256);
    ck_assert_int_eq(loaded->sprites[0]->y, 768);
    ck_assert_int_eq(loaded->sprites[0]->picnum, 123);
    ck_assert_int_eq(loaded->sprites[0]->shade, -7);
    ck_assert_int_eq(loaded->sprites[0]->ang, 1024);

    duke_map_file_free(loaded);
    duke_map_file_free(map);
    ck_assert_int_eq(remove(filename), 0);
}
END_TEST

START_TEST(test_add_sprite_appends_owned_sprite)
{
    DukeMapFile *map = square_map();
    DukeMapSprite *first;
    DukeMapSprite *second;

    first = duke_map_file_add_sprite(map);
    ck_assert_ptr_nonnull(first);
    ck_assert_uint_eq(map->numsprites, 1);
    ck_assert_ptr_eq(map->sprites[0], first);

    first->x = 512;
    second = duke_map_file_add_sprite(map);
    ck_assert_ptr_nonnull(second);
    ck_assert_uint_eq(map->numsprites, 2);
    ck_assert_ptr_eq(map->sprites[0], first);
    ck_assert_ptr_eq(map->sprites[1], second);
    ck_assert_int_eq(map->sprites[0]->x, 512);
    ck_assert_str_eq(map->last_error, "");
    duke_map_file_free(map);
}
END_TEST

START_TEST(test_add_sprite_rejects_format_limit)
{
    DukeMapFile *map = square_map();

    map->numsprites = MAPV7_MAXSPRITES;
    map->sprites = calloc(map->numsprites, sizeof(*map->sprites));
    ck_assert_ptr_nonnull(map->sprites);
    ck_assert_ptr_null(duke_map_file_add_sprite(map));
    ck_assert_uint_eq(map->numsprites, MAPV7_MAXSPRITES);
    ck_assert(strstr(map->last_error, "limit") != NULL);

    free(map->sprites);
    map->sprites = NULL;
    map->numsprites = 0;
    duke_map_file_free(map);
}
END_TEST

START_TEST(test_remove_sprite_frees_and_compacts_array)
{
    DukeMapFile *map = square_map();
    DukeMapSprite *first = duke_map_file_add_sprite(map);
    DukeMapSprite *second = duke_map_file_add_sprite(map);
    DukeMapSprite *third = duke_map_file_add_sprite(map);

    ck_assert_ptr_nonnull(first);
    ck_assert_ptr_nonnull(second);
    ck_assert_ptr_nonnull(third);
    ck_assert(duke_map_file_remove_sprite(map, second));
    ck_assert_uint_eq(map->numsprites, 2);
    ck_assert_ptr_eq(map->sprites[0], first);
    ck_assert_ptr_eq(map->sprites[1], third);
    ck_assert_str_eq(map->last_error, "");

    ck_assert(duke_map_file_remove_sprite(map, first));
    ck_assert(duke_map_file_remove_sprite(map, third));
    ck_assert_uint_eq(map->numsprites, 0);
    ck_assert_ptr_null(map->sprites);
    duke_map_file_free(map);
}
END_TEST

START_TEST(test_remove_sprite_rejects_unowned_sprite)
{
    DukeMapFile *map = square_map();
    DukeMapSprite *sprite = duke_map_sprite_new();

    ck_assert_ptr_nonnull(sprite);
    ck_assert(!duke_map_file_remove_sprite(map, sprite));
    ck_assert_uint_eq(map->numsprites, 0);
    ck_assert(strstr(map->last_error, "not owned") != NULL);
    duke_map_sprite_free(sprite);
    duke_map_file_free(map);
}
END_TEST

START_TEST(test_individual_validator_reports_error)
{
    DukeMapFile *map = square_map();

    map->walls[1]->point2 = 3;
    ck_assert(!duke_map_file_validate_wall_loops(map));
    ck_assert(strstr(map->last_error, "predecessor") != NULL);
    duke_map_file_free(map);
}
END_TEST

START_TEST(test_structure_rejects_unsupported_version)
{
    DukeMapFile *map = square_map();

    map->mapversion = 6;
    ck_assert(!duke_map_file_validate_structure(map));
    ck_assert_str_eq(map->last_error, "Unsupported map version: 6");
    duke_map_file_free(map);
}
END_TEST

START_TEST(test_structure_rejects_missing_array)
{
    DukeMapFile *map = square_map();
    DukeMapWall **walls = map->walls;

    map->walls = NULL;
    ck_assert(!duke_map_file_validate_structure(map));
    ck_assert_str_eq(map->last_error, "Wall array is NULL");
    map->walls = walls;
    duke_map_file_free(map);
}
END_TEST

START_TEST(test_ownership_rejects_noncontiguous_ranges)
{
    DukeMapFile *map = square_map();

    map->sectors[0]->wallptr = 1;
    ck_assert(!duke_map_file_validate_sector_wall_ownership(map));
    ck_assert(strstr(map->last_error, "not contiguous") != NULL);
    duke_map_file_free(map);
}
END_TEST

START_TEST(test_loop_rejects_cross_sector_point2)
{
    DukeMapFile *map = adjacent_sectors_map();

    map->walls[0]->point2 = 4;
    ck_assert(!duke_map_file_validate_wall_loops(map));
    ck_assert(strstr(map->last_error, "outside sector") != NULL);
    duke_map_file_free(map);
}
END_TEST

START_TEST(test_geometry_rejects_zero_length_wall)
{
    DukeMapFile *map = square_map();

    map->walls[1]->x = map->walls[0]->x;
    map->walls[1]->y = map->walls[0]->y;
    ck_assert(!duke_map_file_validate_geometry(map));
    ck_assert(strstr(map->last_error, "zero length") != NULL);
    duke_map_file_free(map);
}
END_TEST

START_TEST(test_geometry_rejects_self_intersection)
{
    DukeMapFile *map = square_map();

    map->walls[0]->point2 = 2;
    map->walls[1]->point2 = 3;
    map->walls[2]->point2 = 1;
    map->walls[3]->point2 = 0;
    ck_assert(!duke_map_file_validate_geometry(map));
    ck_assert(strstr(map->last_error, "intersect") != NULL);
    duke_map_file_free(map);
}
END_TEST

START_TEST(test_geometry_rejects_reversed_outer_loop)
{
    DukeMapFile *map = square_map();

    map->walls[0]->point2 = 3;
    map->walls[1]->point2 = 0;
    map->walls[2]->point2 = 1;
    map->walls[3]->point2 = 2;
    ck_assert(!duke_map_file_validate_geometry(map));
    ck_assert(strstr(map->last_error, "winding") != NULL);
    duke_map_file_free(map);
}
END_TEST

START_TEST(test_portal_must_be_complete)
{
    DukeMapFile *map = square_map();

    map->walls[0]->nextwall = 1;
    ck_assert(!duke_map_file_validate_portals(map));
    ck_assert(strstr(map->last_error, "portal") != NULL);
    duke_map_file_free(map);
}
END_TEST

START_TEST(test_reciprocal_portal_is_valid)
{
    DukeMapFile *map = adjacent_sectors_map();

    ck_assert(duke_map_file_validate_portals(map));
    duke_map_file_free(map);
}
END_TEST

START_TEST(test_portal_endpoints_must_match)
{
    DukeMapFile *map = adjacent_sectors_map();

    map->walls[7]->y = 99;
    ck_assert(!duke_map_file_validate_portals(map));
    ck_assert(strstr(map->last_error, "endpoints") != NULL);
    duke_map_file_free(map);
}
END_TEST

START_TEST(test_vertical_sector_rejects_inverted_flat_planes)
{
    DukeMapFile *map = square_map();

    map->sectors[0]->ceilingz = 1025;
    ck_assert(!duke_map_file_validate_vertical_sectors(map));
    ck_assert(strstr(map->last_error, "ceiling is below") != NULL);
    duke_map_file_free(map);
}
END_TEST

START_TEST(test_vertical_sector_checks_slopes)
{
    DukeMapFile *map = square_map();

    map->sectors[0]->ceilingstat = 2;
    map->sectors[0]->ceilingheinum = 4096;
    ck_assert(!duke_map_file_validate_vertical_sectors(map));
    ck_assert(strstr(map->last_error, "ceiling is below") != NULL);
    duke_map_file_free(map);
}
END_TEST

START_TEST(test_sprite_must_be_in_its_sector)
{
    DukeMapFile *map = square_map();

    add_sprite(map, 2048, 512);
    ck_assert(!duke_map_file_validate_sprites(map));
    ck_assert(strstr(map->last_error, "outside") != NULL);
    duke_map_file_free(map);
}
END_TEST

START_TEST(test_sprite_on_boundary_is_valid)
{
    DukeMapFile *map = square_map();

    add_sprite(map, 0, 512);
    ck_assert(duke_map_file_validate_sprites(map));
    duke_map_file_free(map);
}
END_TEST

START_TEST(test_sprite_references_are_bounded)
{
    DukeMapFile *map = square_map();
    DukeMapSprite *sprite = add_sprite(map, 512, 512);

    sprite->statnum = MAP_MAXSTATUS;
    ck_assert(!duke_map_file_validate_sprites(map));
    ck_assert(strstr(map->last_error, "status") != NULL);
    sprite->statnum = 0;
    sprite->ang = 2048;
    ck_assert(!duke_map_file_validate_sprites(map));
    ck_assert(strstr(map->last_error, "angle") != NULL);
    duke_map_file_free(map);
}
END_TEST

START_TEST(test_start_position_must_be_inside_sector)
{
    DukeMapFile *map = square_map();

    map->posx = 2048;
    ck_assert(!duke_map_file_validate_start_position(map));
    ck_assert(strstr(map->last_error, "outside sector") != NULL);
    duke_map_file_free(map);
}
END_TEST

START_TEST(test_start_position_checks_z_and_angle)
{
    DukeMapFile *map = square_map();

    map->posz = 2048;
    ck_assert(!duke_map_file_validate_start_position(map));
    ck_assert(strstr(map->last_error, "Z coordinate") != NULL);
    map->posz = 512;
    map->ang = 2048;
    ck_assert(!duke_map_file_validate_start_position(map));
    ck_assert(strstr(map->last_error, "angle") != NULL);
    duke_map_file_free(map);
}
END_TEST

START_TEST(test_success_clears_previous_error)
{
    DukeMapFile *map = square_map();

    strcpy(map->last_error, "old error");
    ck_assert(duke_map_file_validate_structure(map));
    ck_assert_str_eq(map->last_error, "");
    duke_map_file_free(map);
}
END_TEST

static Suite *map_suite(void)
{
    Suite *suite = suite_create("map");
    TCase *tc = tcase_create("validation");

    tcase_add_test(tc, test_valid_map);
    tcase_add_test(tc, test_map_write_and_read_round_trip);
    tcase_add_test(tc, test_add_sprite_appends_owned_sprite);
    tcase_add_test(tc, test_add_sprite_rejects_format_limit);
    tcase_add_test(tc, test_remove_sprite_frees_and_compacts_array);
    tcase_add_test(tc, test_remove_sprite_rejects_unowned_sprite);
    tcase_add_test(tc, test_individual_validator_reports_error);
    tcase_add_test(tc, test_structure_rejects_unsupported_version);
    tcase_add_test(tc, test_structure_rejects_missing_array);
    tcase_add_test(tc, test_ownership_rejects_noncontiguous_ranges);
    tcase_add_test(tc, test_loop_rejects_cross_sector_point2);
    tcase_add_test(tc, test_geometry_rejects_zero_length_wall);
    tcase_add_test(tc, test_geometry_rejects_self_intersection);
    tcase_add_test(tc, test_geometry_rejects_reversed_outer_loop);
    tcase_add_test(tc, test_portal_must_be_complete);
    tcase_add_test(tc, test_reciprocal_portal_is_valid);
    tcase_add_test(tc, test_portal_endpoints_must_match);
    tcase_add_test(tc, test_vertical_sector_rejects_inverted_flat_planes);
    tcase_add_test(tc, test_vertical_sector_checks_slopes);
    tcase_add_test(tc, test_sprite_must_be_in_its_sector);
    tcase_add_test(tc, test_sprite_on_boundary_is_valid);
    tcase_add_test(tc, test_sprite_references_are_bounded);
    tcase_add_test(tc, test_start_position_must_be_inside_sector);
    tcase_add_test(tc, test_start_position_checks_z_and_angle);
    tcase_add_test(tc, test_success_clears_previous_error);
    suite_add_tcase(suite, tc);
    return suite;
}

int main(void)
{
    Suite *suite = map_suite();
    SRunner *runner = srunner_create(suite);
    int failed;

    srunner_run_all(runner, CK_NORMAL);
    failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failed == 0 ? 0 : 1;
}

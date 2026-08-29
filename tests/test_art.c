#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "libduke/art.h"

START_TEST(test_art_round_trip_and_manipulation)
{
    const uint8_t tile_10[] = { 1, 2, 3, 4, 5, 6 };
    const uint8_t tile_12[] = { 9, 8, 7, 6 };
    char filename[128];
    void *data = NULL;

    snprintf(filename, sizeof(filename), "/tmp/libduke-art-%ld.art",
        (long) getpid());

    DukeArtFile *art = duke_art_new();
    ck_assert_ptr_nonnull(art);
    ck_assert(duke_art_set_tile(art, 10, 2, 3, 0x01234567, tile_10,
        sizeof(tile_10)));
    ck_assert(duke_art_set_tile(art, 12, 1, 4, 0x89abcdef, tile_12,
        sizeof(tile_12)));
    ck_assert_int_eq(art->header.localtilestart, 10);
    ck_assert_int_eq(art->header.localtileend, 12);
    ck_assert_ptr_nonnull(duke_art_get_tile_by_number(art, 11));
    ck_assert_int_eq(duke_art_get_tile_by_number(art, 11)->width, 0);
    ck_assert(duke_art_write_filename(art, filename));
    duke_art_free(art);

    art = duke_art_new();
    ck_assert_ptr_nonnull(art);
    ck_assert(duke_art_open_filename(art, filename));
    ck_assert(duke_art_validate(art));
    ck_assert(duke_art_read_tiles_full(art));
    ck_assert_int_eq(art->header.artversion, 1);
    ck_assert_int_eq(art->header.localtilestart, 10);
    ck_assert_int_eq(art->header.localtileend, 12);

    DukeArtTile *tile = duke_art_get_tile_by_number(art, 12);
    ck_assert_ptr_nonnull(tile);
    ck_assert_int_eq(tile->width, 1);
    ck_assert_int_eq(tile->height, 4);
    ck_assert_uint_eq(tile->picanm, 0x89abcdef);
    ck_assert_uint_eq(duke_art_get_tile_data_by_number(art, 12, &data), 4);
    ck_assert_int_eq(memcmp(data, tile_12, sizeof(tile_12)), 0);

    ck_assert(duke_art_clear_tile(art, 12));
    ck_assert_int_eq(tile->width, 0);
    ck_assert_int_eq(tile->height, 0);
    duke_art_free(art);
    ck_assert_int_eq(remove(filename), 0);
}
END_TEST

static Suite *art_suite(void)
{
    Suite *suite = suite_create("art");
    TCase *tc = tcase_create("core");

    tcase_add_test(tc, test_art_round_trip_and_manipulation);
    suite_add_tcase(suite, tc);
    return suite;
}

int main(void)
{
    Suite *suite = art_suite();
    SRunner *runner = srunner_create(suite);
    int failed;

    srunner_run_all(runner, CK_NORMAL);
    failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failed == 0 ? 0 : 1;
}

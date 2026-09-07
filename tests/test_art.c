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

START_TEST(test_art_open_memory_copies_buffer)
{
    uint8_t contents[] = {
        1, 0, 0, 0, 1, 0, 0, 0, 7, 0, 0, 0, 7, 0, 0, 0,
        2, 0, 2, 0, 0, 0, 0, 0, 1, 2, 3, 4
    };
    const uint8_t expected_pixels[] = { 1, 2, 3, 4 };
    void *pixels = NULL;
    DukeArtFile *art = duke_art_new();

    ck_assert_ptr_nonnull(art);
    ck_assert(duke_art_open_memory(art, contents, sizeof(contents)));
    memset(contents, 0, sizeof(contents));
    ck_assert(duke_art_read_tiles_sparse(art));
    ck_assert_int_eq(art->header.localtilestart, 7);
    ck_assert_uint_eq(duke_art_get_tile_data_by_number(art, 7, &pixels), 4);
    ck_assert_int_eq(memcmp(pixels, expected_pixels, sizeof(expected_pixels)), 0);

    duke_art_free(art);
}
END_TEST

START_TEST(test_tile_rgba_layout_palette_and_transparency)
{
    DukeArtTile tile = { .width = 2, .height = 3 };
    DukePaletteFile palette = {0};
    const uint8_t pixels[] = {0, 1, 2, 3, 4, 255};
    const uint8_t expected[] = {
        0,0,0,255, 130,0,0,255,
        255,0,0,255, 0,65,0,255,
        0,255,0,255, 4,8,12,0
    };
    uint8_t rgba[25];
    palette.colors[1].red = 63;
    palette.colors[2].green = 63;
    palette.colors[3].red = 32;
    palette.colors[4].green = 16;
    palette.colors[255] = (DukePaletteColor){1,2,3};
    memset(rgba, 42, sizeof(rgba));
    ck_assert(duke_art_tile_to_rgba(&tile, pixels, sizeof(pixels), &palette, rgba, sizeof(rgba)));
    ck_assert_int_eq(memcmp(rgba, expected, sizeof(expected)), 0);
    ck_assert_int_eq(rgba[24], 42);
}
END_TEST

START_TEST(test_tile_rgba_invalid_input)
{
    DukeArtTile tile = { .width = 1, .height = 1 };
    DukePaletteFile palette = {0};
    uint8_t pixel = 0;
    uint8_t rgba[] = {42,42,42,42};
    const uint8_t expected[] = {42,42,42,42};
    const DukeArtTile *tile_arg = &tile;
    const DukePaletteFile *palette_arg = &palette;
    const uint8_t *pixels_arg = &pixel;
    uint8_t *rgba_arg = rgba;
    size_t input_size = 1, output_size = 4;
    switch (_i) {
    case 0: tile_arg = NULL; break;
    case 1: palette_arg = NULL; break;
    case 2: pixels_arg = NULL; break;
    case 3: rgba_arg = NULL; break;
    case 4: tile.width = 0; break;
    case 5: tile.height = -1; break;
    case 6: input_size = 0; break;
    case 7: output_size = 3; break;
    case 8: palette.colors[0].blue = 64; break;
    case 9: tile.width = 32767; tile.height = 32767; break;
    }
    ck_assert(!duke_art_tile_to_rgba(tile_arg, pixels_arg, input_size, palette_arg, rgba_arg, output_size));
    ck_assert_int_eq(memcmp(rgba, expected, sizeof(expected)), 0);
}
END_TEST


static Suite *art_suite(void)
{
    Suite *suite = suite_create("art");
    TCase *tc = tcase_create("core");

    tcase_add_test(tc, test_tile_rgba_layout_palette_and_transparency);
    tcase_add_loop_test(tc, test_tile_rgba_invalid_input, 0, 10);
    tcase_add_test(tc, test_art_round_trip_and_manipulation);
    tcase_add_test(tc, test_art_open_memory_copies_buffer);
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

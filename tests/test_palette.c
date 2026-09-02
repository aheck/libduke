#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libduke/palette.h"

enum {
    TEST_SHADE_COUNT = 2,
    TEST_PALETTE_SIZE = 768 + 2 + TEST_SHADE_COUNT * 256 + 65536
};

static uint8_t *make_palette_data(void)
{
    uint8_t *data = calloc(1, TEST_PALETTE_SIZE);

    ck_assert_ptr_nonnull(data);
    for (size_t color = 0; color < 256; ++color) {
        data[color * 3] = (uint8_t) (color % 64);
        data[color * 3 + 1] = (uint8_t) ((color + 1) % 64);
        data[color * 3 + 2] = (uint8_t) ((color + 2) % 64);
    }
    data[768] = TEST_SHADE_COUNT;
    for (size_t color = 0; color < 256; ++color) {
        data[770 + color] = (uint8_t) color;
        data[770 + 256 + color] = (uint8_t) (255 - color);
    }
    for (size_t index = 0; index < 65536; ++index) {
        data[770 + TEST_SHADE_COUNT * 256 + index]
            = (uint8_t) ((index >> 8) ^ index);
    }
    return data;
}

START_TEST(test_palette_reads_memory_and_lookups)
{
    uint8_t *contents = make_palette_data();
    uint8_t result;
    DukePaletteFile *palette = duke_palette_new();

    ck_assert_ptr_nonnull(palette);
    ck_assert(duke_palette_read_from_memory(palette, contents,
        TEST_PALETTE_SIZE));
    memset(contents, 255, TEST_PALETTE_SIZE);
    ck_assert(duke_palette_validate(palette));
    ck_assert_uint_eq(palette->num_shades, TEST_SHADE_COUNT);
    ck_assert_uint_eq(palette->colors[10].red, 10);
    ck_assert(duke_palette_get_shaded_index(palette, 1, 10, &result));
    ck_assert_uint_eq(result, 245);
    ck_assert(duke_palette_get_translucent_index(palette, 12, 34, &result));
    ck_assert_uint_eq(result, 12 ^ 34);
    ck_assert(!duke_palette_get_shaded_index(palette, 2, 10, &result));

    duke_palette_free(palette);
    free(contents);
}
END_TEST

START_TEST(test_palette_file_round_trip)
{
    uint8_t *contents = make_palette_data();
    char filename[128];
    DukePaletteFile *written = duke_palette_new();
    DukePaletteFile *read = duke_palette_new();

    snprintf(filename, sizeof(filename), "/tmp/libduke-palette-%ld.dat",
        (long) getpid());
    ck_assert_ptr_nonnull(written);
    ck_assert_ptr_nonnull(read);
    ck_assert(duke_palette_read_from_memory(written, contents,
        TEST_PALETTE_SIZE));
    ck_assert(duke_palette_write_to_filename(written, filename));
    ck_assert(duke_palette_read_from_filename(read, filename));
    ck_assert_int_eq(memcmp(written->colors, read->colors,
        sizeof(written->colors)), 0);
    ck_assert_int_eq(memcmp(written->shade_tables, read->shade_tables,
        TEST_SHADE_COUNT * 256), 0);
    ck_assert_int_eq(memcmp(written->translucency_table,
        read->translucency_table, 65536), 0);

    duke_palette_free(written);
    duke_palette_free(read);
    free(contents);
    ck_assert_int_eq(remove(filename), 0);
}
END_TEST

START_TEST(test_palette_rejects_invalid_data_without_replacing_contents)
{
    uint8_t *contents = make_palette_data();
    DukePaletteFile *palette = duke_palette_new();

    ck_assert_ptr_nonnull(palette);
    ck_assert(duke_palette_read_from_memory(palette, contents,
        TEST_PALETTE_SIZE));
    contents[0] = 64;
    ck_assert(!duke_palette_read_from_memory(palette, contents,
        TEST_PALETTE_SIZE));
    ck_assert_uint_eq(palette->colors[0].red, 0);
    ck_assert(!duke_palette_read_from_memory(palette, contents,
        TEST_PALETTE_SIZE - 1));
    ck_assert_uint_eq(palette->num_shades, TEST_SHADE_COUNT);

    duke_palette_free(palette);
    free(contents);
}
END_TEST

static Suite *palette_suite(void)
{
    Suite *suite = suite_create("palette");
    TCase *tc = tcase_create("core");

    tcase_add_test(tc, test_palette_reads_memory_and_lookups);
    tcase_add_test(tc, test_palette_file_round_trip);
    tcase_add_test(tc,
        test_palette_rejects_invalid_data_without_replacing_contents);
    suite_add_tcase(suite, tc);
    return suite;
}

int main(void)
{
    Suite *suite = palette_suite();
    SRunner *runner = srunner_create(suite);
    int failed;

    srunner_run_all(runner, CK_NORMAL);
    failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failed == 0 ? 0 : 1;
}

#include "libduke/palette_lookup.h"
#include "test_platform.h"
#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

START_TEST(test_reads_sprite_palettes_and_alternate_palettes)
{
    const size_t size = 1 + 2 * 257 + 768;
    uint8_t *data = calloc(1, size), result;
    DukePaletteLookupFile *lookup = duke_palette_lookup_new();
    ck_assert_ptr_nonnull(data);
    ck_assert_ptr_nonnull(lookup);
    data[0] = 2;
    data[1] = 7;
    data[2 + 12] = 99;
    data[258] = 9;
    data[259 + 12] = 77;
    data[515] = 1;
    data[516] = 2;
    data[517] = 3;
    ck_assert(duke_palette_lookup_read_from_memory(lookup, data, size));
    ck_assert_uint_eq(lookup->count, 2);
    ck_assert(duke_palette_lookup_get_index(lookup, 7, 12, &result));
    ck_assert_uint_eq(result, 99);
    ck_assert(duke_palette_lookup_get_index(lookup, 0, 12, &result));
    ck_assert_uint_eq(result, 12);
    ck_assert_uint_eq(lookup->alternate_count, 1);
    ck_assert_uint_eq(lookup->alternate_palettes[0].red, 1);
    ck_assert_uint_eq(lookup->alternate_palettes[0].green, 2);
    ck_assert_uint_eq(lookup->alternate_palettes[0].blue, 3);
    free(data);
    duke_palette_lookup_free(lookup);
}
END_TEST

START_TEST(test_reads_lookup_from_filename)
{
    const size_t size = 1 + 25 * 257 + 5 * 768;
    uint8_t *data = calloc(1, size), result;
    char filename[128];
    FILE *file;
    DukePaletteLookupFile *lookup = duke_palette_lookup_new();
    ck_assert_ptr_nonnull(data);
    ck_assert_ptr_nonnull(lookup);

    /* Generate a standalone fixture instead of requiring local game assets. */
    data[0] = 25;
    for (size_t i = 0; i < 25; i++) {
        data[1 + i * 257] = (uint8_t)(i + 1);
        for (size_t color = 0; color < 256; color++) {
            data[2 + i * 257 + color] = (uint8_t)(color + i);
        }
    }
    data[size - 1] = 63;
    snprintf(filename, sizeof(filename), "test-lookup-%ld.dat",
        (long)test_process_id());
    file = fopen(filename, "wb");
    ck_assert_ptr_nonnull(file);
    ck_assert_uint_eq(fwrite(data, 1, size, file), size);
    ck_assert_int_eq(fclose(file), 0);
    free(data);

    bool loaded = duke_palette_lookup_read_from_filename(lookup, filename);
    ck_assert_int_eq(remove(filename), 0);
    ck_assert(loaded);
    ck_assert_uint_eq(lookup->count, 25);
    ck_assert_uint_eq(lookup->alternate_count, 5);
    ck_assert(duke_palette_lookup_get_index(lookup, 25, 12, &result));
    ck_assert_uint_eq(result, 36);
    ck_assert_uint_eq(lookup->alternate_palettes[5 * 256 - 1].blue, 63);
    duke_palette_lookup_free(lookup);
}
END_TEST

int main(void)
{
    Suite *suite = suite_create("palette-lookup");
    TCase *tc = tcase_create("core");
    SRunner *runner;
    int failed;
    tcase_add_test(tc, test_reads_sprite_palettes_and_alternate_palettes);
    tcase_add_test(tc, test_reads_lookup_from_filename);
    suite_add_tcase(suite, tc);
    runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

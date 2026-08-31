#include <check.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "libduke/grp.h"

START_TEST(test_grp_entries_are_read_and_accessed)
{
    const uint8_t entry_count_le[4] = { 2, 0, 0, 0 };
    const uint8_t first_size_le[4] = { 3, 0, 0, 0 };
    const uint8_t second_size_le[4] = { 5, 0, 0, 0 };
    char filename[128];
    const char first_name[12] = "FIRST.TXT";
    const char second_name[12] = "SECOND.BIN";
    const char first_data[] = "abc";
    const char second_data[] = "12345";
    uint32_t first_size = sizeof(first_data) - 1;
    uint32_t second_size = sizeof(second_data) - 1;
    void *data = NULL;

    snprintf(filename, sizeof(filename), "/tmp/libduke-grp-%ld.grp",
        (long) getpid());
    FILE *fp = fopen(filename, "wb");
    ck_assert_ptr_nonnull(fp);
    ck_assert_uint_eq(fwrite("KenSilverman", 1, 12, fp), 12);
    ck_assert_uint_eq(fwrite(entry_count_le, 1, 4, fp), 4);
    ck_assert_uint_eq(fwrite(first_name, 1, 12, fp), 12);
    ck_assert_uint_eq(fwrite(first_size_le, 1, 4, fp), 4);
    ck_assert_uint_eq(fwrite(second_name, 1, 12, fp), 12);
    ck_assert_uint_eq(fwrite(second_size_le, 1, 4, fp), 4);
    ck_assert_uint_eq(fwrite(first_data, 1, first_size, fp), first_size);
    ck_assert_uint_eq(fwrite(second_data, 1, second_size, fp), second_size);
    ck_assert_int_eq(fclose(fp), 0);

    DukeGrpFile *grp = duke_grp_new();
    ck_assert_ptr_nonnull(grp);
    ck_assert(duke_grp_open_filename(grp, filename));
    ck_assert(duke_grp_read_entries_sparse(grp));

    DukeGrpFileEntry *entry = duke_grp_get_entry_by_index(grp, 1);
    ck_assert_ptr_nonnull(entry);
    ck_assert_str_eq(entry->filename, "SECOND.BIN");
    ck_assert_uint_eq(entry->filesize, second_size);
    ck_assert_ptr_null(duke_grp_get_entry_by_index(grp, 2));

    ck_assert_uint_eq(
        duke_grp_get_file_data_by_filename(grp, "SECOND.BIN", &data),
        second_size);
    ck_assert_int_eq(memcmp(data, second_data, second_size), 0);

    duke_grp_free(grp);
    ck_assert_int_eq(remove(filename), 0);
}
END_TEST

static Suite *
grp_suite(void)
{
    Suite *suite;
    TCase *tc;

    suite = suite_create("grp");
    tc = tcase_create("core");

    tcase_add_test(tc, test_grp_entries_are_read_and_accessed);

    suite_add_tcase(suite, tc);

    return suite;
}

int
main(void)
{
    Suite *suite;
    SRunner *runner;
    int failed;

    suite = grp_suite();
    runner = srunner_create(suite);

    srunner_run_all(runner, CK_NORMAL);

    failed = srunner_ntests_failed(runner);

    srunner_free(runner);

    return failed == 0 ? 0 : 1;
}

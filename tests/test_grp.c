#include <check.h>

#include "libduke/grp.h"

START_TEST(test_grp_init)
{
    struct duke_grp grp;

    duke_grp_init(&grp);

    ck_assert_uint_eq(grp.version, 1);
    ck_assert_uint_eq(grp.entry_count, 0);
}
END_TEST

START_TEST(test_grp_add_entry)
{
    struct duke_grp grp;

    duke_grp_init(&grp);

    ck_assert_int_eq(duke_grp_add_entry(&grp), 0);
    ck_assert_uint_eq(grp.entry_count, 1);

    ck_assert_int_eq(duke_grp_add_entry(&grp), 0);
    ck_assert_uint_eq(grp.entry_count, 2);
}
END_TEST

static Suite *
grp_suite(void)
{
    Suite *suite;
    TCase *tc;

    suite = suite_create("grp");
    tc = tcase_create("core");

    tcase_add_test(tc, test_grp_init);
    tcase_add_test(tc, test_grp_add_entry);

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

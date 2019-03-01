/*
 * This file is part of lmapd.
 *
 * lmapd is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * lmapd is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with lmapd. If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <stdio.h>
#include <check.h>
#include <unistd.h>
#include <signal.h>

#include "lmap.h"
#include "lmapd.h"
#include "runner.h"
#include "utils.h"

static char last_error_msg[1024];

static void vlog(int level, const char *func, const char *format, va_list args)
{
    (void) level;
    (void) func;
    (void) vsnprintf(last_error_msg, sizeof(last_error_msg), format, args);
#if 0
    fprintf(stderr, "%s\n", last_error_msg);
#endif
}

static void alarm_handler(int signum)
{
    (void) signum;
    raise(SIGINT);
}

void setup(void)
{

}

void teardown(void)
{

}

START_TEST(test_lmapd)
{
    struct lmapd *lmapd;

    lmapd = lmapd_new();
    ck_assert_ptr_ne(lmapd, NULL);

    lmapd_free(lmapd);
}
END_TEST

START_TEST(test_lmapd_run)
{
    struct lmapd *lmapd;

    lmapd = lmapd_new();
    ck_assert_ptr_ne(lmapd, NULL);
    lmapd->lmap = lmap_new();
    (void) signal(SIGALRM, alarm_handler);
    (void) alarm(2);
    lmapd_run(lmapd);
    lmapd_free(lmapd);
}
END_TEST

Suite * lmap_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("lmapd");

    /* Core test case */
    tc_core = tcase_create("Core");

    tcase_add_checked_fixture(tc_core, setup, teardown);
    tcase_add_test(tc_core, test_lmapd);
    tcase_add_test(tc_core, test_lmapd_run);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    setenv("TZ", "GMT", 1);

    lmap_set_log_handler(vlog);

    s = lmap_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

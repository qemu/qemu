/*
 * Options Visitor unit-tests.
 *
 * Copyright (C) 2013 Red Hat, Inc.
 *
 * Authors:
 *   Laszlo Ersek <lersek@redhat.com> (based on test-string-output-visitor)
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <glib.h>

#include "qemu/config-file.h"     /* qemu_add_opts() */
#include "qemu/option.h"          /* qemu_opts_parse() */
#include "qapi/opts-visitor.h"    /* opts_visitor_new() */
#include "test-qapi-visit.h"      /* visit_type_UserDefOptions() */
#include "qapi/dealloc-visitor.h" /* qapi_dealloc_visitor_new() */

static QemuOptsList userdef_opts = {
    .name = "userdef",
    .head = QTAILQ_HEAD_INITIALIZER(userdef_opts.head),
    .desc = { { 0 } } /* validated with OptsVisitor */
};

/* fixture (= glib test case context) and test case manipulation */

typedef struct OptsVisitorFixture {
    UserDefOptions *userdef;
    Error *err;
} OptsVisitorFixture;


static void
setup_fixture(OptsVisitorFixture *f, gconstpointer test_data)
{
    const char *opts_string = test_data;
    QemuOpts *opts;
    OptsVisitor *ov;

    opts = qemu_opts_parse(qemu_find_opts("userdef"), opts_string, 0);
    g_assert(opts != NULL);

    ov = opts_visitor_new(opts);
    visit_type_UserDefOptions(opts_get_visitor(ov), &f->userdef, NULL,
                              &f->err);
    opts_visitor_cleanup(ov);
    qemu_opts_del(opts);
}


static void
teardown_fixture(OptsVisitorFixture *f, gconstpointer test_data)
{
    if (f->userdef != NULL) {
        QapiDeallocVisitor *dv;

        dv = qapi_dealloc_visitor_new();
        visit_type_UserDefOptions(qapi_dealloc_get_visitor(dv), &f->userdef,
                                  NULL, NULL);
        qapi_dealloc_visitor_cleanup(dv);
    }
    error_free(f->err);
}


static void
add_test(const char *testpath,
         void (*test_func)(OptsVisitorFixture *f, gconstpointer test_data),
         gconstpointer test_data)
{
    g_test_add(testpath, OptsVisitorFixture, test_data, setup_fixture,
               test_func, teardown_fixture);
}

/* test output evaluation */

static void
expect_ok(OptsVisitorFixture *f, gconstpointer test_data)
{
    g_assert(f->err == NULL);
    g_assert(f->userdef != NULL);
}


static void
expect_fail(OptsVisitorFixture *f, gconstpointer test_data)
{
    g_assert(f->err != NULL);

    /* The error message is printed when this test utility is invoked directly
     * (ie. without gtester) and the --verbose flag is passed:
     *
     * tests/test-opts-visitor --verbose
     */
    g_test_message("'%s': %s", (const char *)test_data,
                   error_get_pretty(f->err));
}


static void
test_value(OptsVisitorFixture *f, gconstpointer test_data)
{
    uint64_t magic, bitval;
    intList *i64;
    uint64List *u64;
    uint16List *u16;

    expect_ok(f, test_data);

    magic = 0;
    for (i64 = f->userdef->i64; i64 != NULL; i64 = i64->next) {
        g_assert(-16 <= i64->value && i64->value < 64-16);
        bitval = 1ull << (i64->value + 16);
        g_assert((magic & bitval) == 0);
        magic |= bitval;
    }
    g_assert(magic == 0xDEADBEEF);

    magic = 0;
    for (u64 = f->userdef->u64; u64 != NULL; u64 = u64->next) {
        g_assert(u64->value < 64);
        bitval = 1ull << u64->value;
        g_assert((magic & bitval) == 0);
        magic |= bitval;
    }
    g_assert(magic == 0xBADC0FFEE0DDF00DULL);

    magic = 0;
    for (u16 = f->userdef->u16; u16 != NULL; u16 = u16->next) {
        g_assert(u16->value < 64);
        bitval = 1ull << u16->value;
        g_assert((magic & bitval) == 0);
        magic |= bitval;
    }
    g_assert(magic == 0xD15EA5E);
}


static void
expect_i64_min(OptsVisitorFixture *f, gconstpointer test_data)
{
    expect_ok(f, test_data);
    g_assert(f->userdef->has_i64);
    g_assert(f->userdef->i64->next == NULL);
    g_assert(f->userdef->i64->value == INT64_MIN);
}


static void
expect_i64_max(OptsVisitorFixture *f, gconstpointer test_data)
{
    expect_ok(f, test_data);
    g_assert(f->userdef->has_i64);
    g_assert(f->userdef->i64->next == NULL);
    g_assert(f->userdef->i64->value == INT64_MAX);
}


static void
expect_zero(OptsVisitorFixture *f, gconstpointer test_data)
{
    expect_ok(f, test_data);
    g_assert(f->userdef->has_u64);
    g_assert(f->userdef->u64->next == NULL);
    g_assert(f->userdef->u64->value == 0);
}


static void
expect_u64_max(OptsVisitorFixture *f, gconstpointer test_data)
{
    expect_ok(f, test_data);
    g_assert(f->userdef->has_u64);
    g_assert(f->userdef->u64->next == NULL);
    g_assert(f->userdef->u64->value == UINT64_MAX);
}

/* test cases */

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qemu_add_opts(&userdef_opts);

    /* Three hexadecimal magic numbers, "dead beef", "bad coffee, odd food" and
     * "disease", from
     * <http://en.wikipedia.org/wiki/Magic_number_%28programming%29>, were
     * converted to binary and dissected into bit ranges. Each magic number is
     * going to be recomposed using the lists called "i64", "u64" and "u16",
     * respectively.
     *
     * (Note that these types pertain to the individual bit shift counts, not
     * the magic numbers themselves; the intent is to exercise opts_type_int()
     * and opts_type_uint64().)
     *
     * The "i64" shift counts have been decreased by 16 (decimal) in order to
     * test negative values as well. Finally, the full list of QemuOpt elements
     * has been permuted with "shuf".
     *
     * Both "i64" and "u64" have some (distinct) single-element ranges
     * represented as both "a" and "a-a". "u16" is a special case of "i64" (see
     * visit_type_uint16()), so it wouldn't add a separate test in this regard.
     */

    add_test("/visitor/opts/flatten/value", &test_value,
             "i64=-1-0,u64=12-16,u64=2-3,i64=-11--9,u64=57,u16=9,i64=5-5,"
             "u16=1-4,u16=20,u64=63-63,i64=-16--13,u64=50-52,i64=14-15,u16=11,"
             "i64=7,u16=18,i64=2-3,u16=6,u64=54-55,u64=0,u64=18-20,u64=33-43,"
             "i64=9-12,u16=26-27,u64=59-61,u16=13-16,u64=29-31,u64=22-23,"
             "u16=24,i64=-7--3");

    add_test("/visitor/opts/i64/val1/errno",    &expect_fail,
             "i64=0x8000000000000000");
    add_test("/visitor/opts/i64/val1/empty",    &expect_fail, "i64=");
    add_test("/visitor/opts/i64/val1/trailing", &expect_fail, "i64=5z");
    add_test("/visitor/opts/i64/nonlist",       &expect_fail, "i64x=5-6");
    add_test("/visitor/opts/i64/val2/errno",    &expect_fail,
             "i64=0x7fffffffffffffff-0x8000000000000000");
    add_test("/visitor/opts/i64/val2/empty",    &expect_fail, "i64=5-");
    add_test("/visitor/opts/i64/val2/trailing", &expect_fail, "i64=5-6z");
    add_test("/visitor/opts/i64/range/empty",   &expect_fail, "i64=6-5");
    add_test("/visitor/opts/i64/range/minval",  &expect_i64_min,
             "i64=-0x8000000000000000--0x8000000000000000");
    add_test("/visitor/opts/i64/range/maxval",  &expect_i64_max,
             "i64=0x7fffffffffffffff-0x7fffffffffffffff");

    add_test("/visitor/opts/u64/val1/errno",    &expect_fail, "u64=-1");
    add_test("/visitor/opts/u64/val1/empty",    &expect_fail, "u64=");
    add_test("/visitor/opts/u64/val1/trailing", &expect_fail, "u64=5z");
    add_test("/visitor/opts/u64/nonlist",       &expect_fail, "u64x=5-6");
    add_test("/visitor/opts/u64/val2/errno",    &expect_fail,
             "u64=0xffffffffffffffff-0x10000000000000000");
    add_test("/visitor/opts/u64/val2/empty",    &expect_fail, "u64=5-");
    add_test("/visitor/opts/u64/val2/trailing", &expect_fail, "u64=5-6z");
    add_test("/visitor/opts/u64/range/empty",   &expect_fail, "u64=6-5");
    add_test("/visitor/opts/u64/range/minval",  &expect_zero, "u64=0-0");
    add_test("/visitor/opts/u64/range/maxval",  &expect_u64_max,
             "u64=0xffffffffffffffff-0xffffffffffffffff");

    /* Test maximum range sizes. The macro value is open-coded here
     * *intentionally*; the test case must use concrete values by design. If
     * OPTS_VISITOR_RANGE_MAX is changed, the following values need to be
     * recalculated as well. The assert and this comment should help with it.
     */
    g_assert(OPTS_VISITOR_RANGE_MAX == 65536);

    /* The unsigned case is simple, a u64-u64 difference can always be
     * represented as a u64.
     */
    add_test("/visitor/opts/u64/range/max",  &expect_ok,   "u64=0-65535");
    add_test("/visitor/opts/u64/range/2big", &expect_fail, "u64=0-65536");

    /* The same cannot be said about an i64-i64 difference. */
    add_test("/visitor/opts/i64/range/max/pos/a", &expect_ok,
             "i64=0x7fffffffffff0000-0x7fffffffffffffff");
    add_test("/visitor/opts/i64/range/max/pos/b", &expect_ok,
             "i64=0x7ffffffffffeffff-0x7ffffffffffffffe");
    add_test("/visitor/opts/i64/range/2big/pos",  &expect_fail,
             "i64=0x7ffffffffffeffff-0x7fffffffffffffff");
    add_test("/visitor/opts/i64/range/max/neg/a", &expect_ok,
             "i64=-0x8000000000000000--0x7fffffffffff0001");
    add_test("/visitor/opts/i64/range/max/neg/b", &expect_ok,
             "i64=-0x7fffffffffffffff--0x7fffffffffff0000");
    add_test("/visitor/opts/i64/range/2big/neg",  &expect_fail,
             "i64=-0x8000000000000000--0x7fffffffffff0000");
    add_test("/visitor/opts/i64/range/2big/full", &expect_fail,
             "i64=-0x8000000000000000-0x7fffffffffffffff");

    g_test_run();
    return 0;
}

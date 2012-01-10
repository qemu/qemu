/*
 * QFloat unit-tests.
 *
 * Copyright IBM, Corp. 2009
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */
#include <glib.h>

#include "qfloat.h"
#include "qemu-common.h"

/*
 * Public Interface test-cases
 *
 * (with some violations to access 'private' data)
 */

static void qfloat_from_double_test(void)
{
    QFloat *qf;
    const double value = -42.23423;

    qf = qfloat_from_double(value);
    g_assert(qf != NULL);
    g_assert(qf->value == value);
    g_assert(qf->base.refcnt == 1);
    g_assert(qobject_type(QOBJECT(qf)) == QTYPE_QFLOAT);

    // destroy doesn't exit yet
    g_free(qf);
}

static void qfloat_destroy_test(void)
{
    QFloat *qf = qfloat_from_double(0.0);
    QDECREF(qf);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/public/from_double", qfloat_from_double_test);
    g_test_add_func("/public/destroy", qfloat_destroy_test);

    return g_test_run();
}

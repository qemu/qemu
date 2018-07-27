/*
 * QString unit-tests.
 *
 * Copyright (C) 2009 Red Hat Inc.
 *
 * Authors:
 *  Luiz Capitulino <lcapitulino@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */
#include "qemu/osdep.h"

#include "qapi/qmp/qstring.h"
#include "qemu-common.h"

/*
 * Public Interface test-cases
 *
 * (with some violations to access 'private' data)
 */

static void qstring_from_str_test(void)
{
    QString *qstring;
    const char *str = "QEMU";

    qstring = qstring_from_str(str);
    g_assert(qstring != NULL);
    g_assert(qstring->base.refcnt == 1);
    g_assert(strcmp(str, qstring->string) == 0);
    g_assert(qobject_type(QOBJECT(qstring)) == QTYPE_QSTRING);

    qobject_unref(qstring);
}

static void qstring_get_str_test(void)
{
    QString *qstring;
    const char *ret_str;
    const char *str = "QEMU/KVM";

    qstring = qstring_from_str(str);
    ret_str = qstring_get_str(qstring);
    g_assert(strcmp(ret_str, str) == 0);

    qobject_unref(qstring);
}

static void qstring_append_chr_test(void)
{
    int i;
    QString *qstring;
    const char *str = "qstring append char unit-test";

    qstring = qstring_new();

    for (i = 0; str[i]; i++)
        qstring_append_chr(qstring, str[i]);

    g_assert(strcmp(str, qstring_get_str(qstring)) == 0);
    qobject_unref(qstring);
}

static void qstring_from_substr_test(void)
{
    QString *qs;

    qs = qstring_from_substr("virtualization", 3, 10);
    g_assert(qs != NULL);
    g_assert(strcmp(qstring_get_str(qs), "tualiza") == 0);

    qobject_unref(qs);
}


static void qobject_to_qstring_test(void)
{
    QString *qstring;

    qstring = qstring_from_str("foo");
    g_assert(qobject_to(QString, QOBJECT(qstring)) == qstring);

    qobject_unref(qstring);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/public/from_str", qstring_from_str_test);
    g_test_add_func("/public/get_str", qstring_get_str_test);
    g_test_add_func("/public/append_chr", qstring_append_chr_test);
    g_test_add_func("/public/from_substr", qstring_from_substr_test);
    g_test_add_func("/public/to_qstring", qobject_to_qstring_test);

    return g_test_run();
}

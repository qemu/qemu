/*
 * QTest testcase for QOM
 *
 * Copyright (c) 2013 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "libqtest.h"

#include <glib.h>
#include <string.h>
#include "qemu/osdep.h"
#include "qapi/qmp/types.h"

static const char *blacklist_x86[] = {
    "xenfv", "xenpv", NULL
};

static const struct {
    const char *arch;
    const char **machine;
} blacklists[] = {
    { "i386", blacklist_x86 },
    { "x86_64", blacklist_x86 },
};

static bool is_blacklisted(const char *arch, const char *mach)
{
    int i;
    const char **p;

    for (i = 0; i < ARRAY_SIZE(blacklists); i++) {
        if (!strcmp(blacklists[i].arch, arch)) {
            for (p = blacklists[i].machine; *p; p++) {
                if (!strcmp(*p, mach)) {
                    return true;
                }
            }
        }
    }
    return false;
}

static void test_nop(gconstpointer data)
{
    QTestState *s;
    const char *machine = data;
    char *args;

    args = g_strdup_printf("-machine %s", machine);
    s = qtest_start(args);
    if (s) {
        qtest_quit(s);
    }
    g_free(args);
}

static void add_machine_test_cases(void)
{
    const char *arch = qtest_get_arch();
    QDict *response, *minfo;
    QList *list;
    const QListEntry *p;
    QObject *qobj;
    QString *qstr;
    const char *mname, *path;

    qtest_start("-machine none");
    response = qmp("{ 'execute': 'query-machines' }");
    g_assert(response);
    list = qdict_get_qlist(response, "return");
    g_assert(list);

    for (p = qlist_first(list); p; p = qlist_next(p)) {
        minfo = qobject_to_qdict(qlist_entry_obj(p));
        g_assert(minfo);
        qobj = qdict_get(minfo, "name");
        g_assert(qobj);
        qstr = qobject_to_qstring(qobj);
        g_assert(qstr);
        mname = qstring_get_str(qstr);
        if (!is_blacklisted(arch, mname)) {
            path = g_strdup_printf("/%s/qom/%s", arch, mname);
            g_test_add_data_func(path, mname, test_nop);
        }
    }
    qtest_end();
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    add_machine_test_cases();

    return g_test_run();
}

/*
 * QTest testcase for QOM
 *
 * Copyright (c) 2013 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "qemu-common.h"
#include "qemu/cutils.h"
#include "libqtest.h"
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

static void test_properties(const char *path, bool recurse)
{
    char *child_path;
    QDict *response, *tuple, *tmp;
    QList *list;
    QListEntry *entry;

    g_test_message("Obtaining properties of %s", path);
    response = qmp("{ 'execute': 'qom-list',"
                   "  'arguments': { 'path': %s } }", path);
    g_assert(response);

    if (!recurse) {
        QDECREF(response);
        return;
    }

    g_assert(qdict_haskey(response, "return"));
    list = qobject_to_qlist(qdict_get(response, "return"));
    QLIST_FOREACH_ENTRY(list, entry) {
        tuple = qobject_to_qdict(qlist_entry_obj(entry));
        bool is_child = strstart(qdict_get_str(tuple, "type"), "child<", NULL);
        bool is_link = strstart(qdict_get_str(tuple, "type"), "link<", NULL);

        if (is_child || is_link) {
            child_path = g_strdup_printf("%s/%s",
                                         path, qdict_get_str(tuple, "name"));
            test_properties(child_path, is_child);
            g_free(child_path);
        } else {
            const char *prop = qdict_get_str(tuple, "name");
            g_test_message("Testing property %s.%s", path, prop);
            tmp = qmp("{ 'execute': 'qom-get',"
                      "  'arguments': { 'path': %s,"
                      "                 'property': %s } }",
                      path, prop);
            /* qom-get may fail but should not, e.g., segfault. */
            g_assert(tmp);
            QDECREF(tmp);
        }
    }
    QDECREF(response);
}

static void test_machine(gconstpointer data)
{
    const char *machine = data;
    char *args;
    QDict *response;

    args = g_strdup_printf("-machine %s", machine);
    qtest_start(args);

    test_properties("/machine", true);

    response = qmp("{ 'execute': 'quit' }");
    g_assert(qdict_haskey(response, "return"));
    QDECREF(response);

    qtest_end();
    g_free(args);
    g_free((void *)machine);
}

static void add_machine_test_case(const char *mname)
{
    const char *arch = qtest_get_arch();

    if (!is_blacklisted(arch, mname)) {
        char *path = g_strdup_printf("qom/%s", mname);
        qtest_add_data_func(path, g_strdup(mname), test_machine);
        g_free(path);
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_cb_for_every_machine(add_machine_test_case);

    return g_test_run();
}

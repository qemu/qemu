/*
 * QTest testcase for PC CPUs
 *
 * Copyright (c) 2015 SUSE Linux GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <glib.h>

#include "qemu-common.h"
#include "libqtest.h"
#include "qapi/qmp/types.h"

struct PCTestData {
    const char *machine;
    const char *cpu_model;
    unsigned sockets;
    unsigned cores;
    unsigned threads;
    unsigned maxcpus;
};
typedef struct PCTestData PCTestData;

static void test_pc_with_cpu_add(gconstpointer data)
{
    const PCTestData *s = data;
    char *args;
    QDict *response;
    unsigned int i;

    args = g_strdup_printf("-machine %s -cpu %s "
                           "-smp sockets=%u,cores=%u,threads=%u,maxcpus=%u",
                           s->machine, s->cpu_model,
                           s->sockets, s->cores, s->threads, s->maxcpus);
    qtest_start(args);

    for (i = s->sockets * s->cores * s->threads; i < s->maxcpus; i++) {
        response = qmp("{ 'execute': 'cpu-add',"
                       "  'arguments': { 'id': %d } }", i);
        g_assert(response);
        g_assert(!qdict_haskey(response, "error"));
        QDECREF(response);
    }

    qtest_end();
    g_free(args);
}

static void test_pc_without_cpu_add(gconstpointer data)
{
    const PCTestData *s = data;
    char *args;
    QDict *response;

    args = g_strdup_printf("-machine %s -cpu %s "
                           "-smp sockets=%u,cores=%u,threads=%u,maxcpus=%u",
                           s->machine, s->cpu_model,
                           s->sockets, s->cores, s->threads, s->maxcpus);
    qtest_start(args);

    response = qmp("{ 'execute': 'cpu-add',"
                   "  'arguments': { 'id': %d } }",
                   s->sockets * s->cores * s->threads);
    g_assert(response);
    g_assert(qdict_haskey(response, "error"));
    QDECREF(response);

    qtest_end();
    g_free(args);
}

static void add_pc_test_cases(void)
{
    QDict *response, *minfo;
    QList *list;
    const QListEntry *p;
    QObject *qobj;
    QString *qstr;
    const char *mname, *path;
    PCTestData *data;

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
        if (!g_str_has_prefix(mname, "pc-")) {
            continue;
        }
        data = g_malloc(sizeof(PCTestData));
        data->machine = mname;
        data->cpu_model = "Haswell"; /* 1.3+ theoretically */
        data->sockets = 1;
        data->cores = 3;
        data->threads = 2;
        data->maxcpus = data->sockets * data->cores * data->threads * 2;
        if (g_str_has_suffix(mname, "-1.4") ||
            (strcmp(mname, "pc-1.3") == 0) ||
            (strcmp(mname, "pc-1.2") == 0) ||
            (strcmp(mname, "pc-1.1") == 0) ||
            (strcmp(mname, "pc-1.0") == 0) ||
            (strcmp(mname, "pc-0.15") == 0) ||
            (strcmp(mname, "pc-0.14") == 0) ||
            (strcmp(mname, "pc-0.13") == 0) ||
            (strcmp(mname, "pc-0.12") == 0) ||
            (strcmp(mname, "pc-0.11") == 0) ||
            (strcmp(mname, "pc-0.10") == 0)) {
            path = g_strdup_printf("cpu/%s/init/%ux%ux%u&maxcpus=%u",
                                   mname, data->sockets, data->cores,
                                   data->threads, data->maxcpus);
            qtest_add_data_func(path, data, test_pc_without_cpu_add);
        } else {
            path = g_strdup_printf("cpu/%s/add/%ux%ux%u&maxcpus=%u",
                                   mname, data->sockets, data->cores,
                                   data->threads, data->maxcpus);
            qtest_add_data_func(path, data, test_pc_with_cpu_add);
        }
    }
    qtest_end();
}

int main(int argc, char **argv)
{
    const char *arch = qtest_get_arch();

    g_test_init(&argc, &argv, NULL);

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        add_pc_test_cases();
    }

    return g_test_run();
}

/*
 * QTest testcase for PC CPUs
 *
 * Copyright (c) 2015 SUSE Linux GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "qemu-common.h"
#include "libqtest.h"
#include "qapi/qmp/types.h"

struct PCTestData {
    char *machine;
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

static void test_data_free(gpointer data)
{
    PCTestData *pc = data;

    g_free(pc->machine);
    g_free(pc);
}

static void add_pc_test_case(const char *mname)
{
    char *path;
    PCTestData *data;

    if (!g_str_has_prefix(mname, "pc-")) {
        return;
    }
    data = g_malloc(sizeof(PCTestData));
    data->machine = g_strdup(mname);
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
        qtest_add_data_func_full(path, data, test_pc_without_cpu_add,
                                 test_data_free);
        g_free(path);
    } else {
        path = g_strdup_printf("cpu/%s/add/%ux%ux%u&maxcpus=%u",
                               mname, data->sockets, data->cores,
                               data->threads, data->maxcpus);
        qtest_add_data_func_full(path, data, test_pc_with_cpu_add,
                                 test_data_free);
        g_free(path);
    }
}

int main(int argc, char **argv)
{
    const char *arch = qtest_get_arch();

    g_test_init(&argc, &argv, NULL);

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        qtest_cb_for_every_machine(add_pc_test_case);
    }

    return g_test_run();
}

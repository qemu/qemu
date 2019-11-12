/*
 * QTest testcase for CPU plugging
 *
 * Copyright (c) 2015 SUSE Linux GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "qemu-common.h"
#include "libqtest-single.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qlist.h"

struct PlugTestData {
    char *machine;
    const char *cpu_model;
    char *device_model;
    unsigned sockets;
    unsigned cores;
    unsigned threads;
    unsigned maxcpus;
};
typedef struct PlugTestData PlugTestData;

static void test_plug_with_cpu_add(gconstpointer data)
{
    const PlugTestData *s = data;
    char *args;
    QDict *response;
    unsigned int i;

    args = g_strdup_printf("-machine %s -cpu %s "
                           "-smp 1,sockets=%u,cores=%u,threads=%u,maxcpus=%u",
                           s->machine, s->cpu_model,
                           s->sockets, s->cores, s->threads, s->maxcpus);
    qtest_start(args);

    for (i = 1; i < s->maxcpus; i++) {
        response = qmp("{ 'execute': 'cpu-add',"
                       "  'arguments': { 'id': %d } }", i);
        g_assert(response);
        g_assert(!qdict_haskey(response, "error"));
        qobject_unref(response);
    }

    qtest_end();
    g_free(args);
}

static void test_plug_without_cpu_add(gconstpointer data)
{
    const PlugTestData *s = data;
    char *args;
    QDict *response;

    args = g_strdup_printf("-machine %s -cpu %s "
                           "-smp 1,sockets=%u,cores=%u,threads=%u,maxcpus=%u",
                           s->machine, s->cpu_model,
                           s->sockets, s->cores, s->threads, s->maxcpus);
    qtest_start(args);

    response = qmp("{ 'execute': 'cpu-add',"
                   "  'arguments': { 'id': %d } }",
                   s->sockets * s->cores * s->threads);
    g_assert(response);
    g_assert(qdict_haskey(response, "error"));
    qobject_unref(response);

    qtest_end();
    g_free(args);
}

static void test_plug_with_device_add(gconstpointer data)
{
    const PlugTestData *td = data;
    char *args;
    QTestState *qts;
    QDict *resp;
    QList *cpus;
    QObject *e;
    int hotplugged = 0;

    args = g_strdup_printf("-machine %s -cpu %s "
                           "-smp 1,sockets=%u,cores=%u,threads=%u,maxcpus=%u",
                           td->machine, td->cpu_model,
                           td->sockets, td->cores, td->threads, td->maxcpus);
    qts = qtest_init(args);

    resp = qtest_qmp(qts, "{ 'execute': 'query-hotpluggable-cpus'}");
    g_assert(qdict_haskey(resp, "return"));
    cpus = qdict_get_qlist(resp, "return");
    g_assert(cpus);

    while ((e = qlist_pop(cpus))) {
        const QDict *cpu, *props;

        cpu = qobject_to(QDict, e);
        if (qdict_haskey(cpu, "qom-path")) {
            qobject_unref(e);
            continue;
        }

        g_assert(qdict_haskey(cpu, "props"));
        props = qdict_get_qdict(cpu, "props");

        qtest_qmp_device_add_qdict(qts, td->device_model, props);
        hotplugged++;
        qobject_unref(e);
    }

    /* make sure that there were hotplugged CPUs */
    g_assert(hotplugged);
    qobject_unref(resp);
    qtest_quit(qts);
    g_free(args);
}

static void test_data_free(gpointer data)
{
    PlugTestData *pc = data;

    g_free(pc->machine);
    g_free(pc->device_model);
    g_free(pc);
}

static void add_pc_test_case(const char *mname)
{
    char *path;
    PlugTestData *data;

    if (!g_str_has_prefix(mname, "pc-")) {
        return;
    }
    data = g_new(PlugTestData, 1);
    data->machine = g_strdup(mname);
    data->cpu_model = "Haswell"; /* 1.3+ theoretically */
    data->device_model = g_strdup_printf("%s-%s-cpu", data->cpu_model,
                                         qtest_get_arch());
    data->sockets = 1;
    data->cores = 3;
    data->threads = 2;
    data->maxcpus = data->sockets * data->cores * data->threads;
    if (g_str_has_suffix(mname, "-1.4") ||
        (strcmp(mname, "pc-1.3") == 0) ||
        (strcmp(mname, "pc-1.2") == 0) ||
        (strcmp(mname, "pc-1.1") == 0) ||
        (strcmp(mname, "pc-1.0") == 0) ||
        (strcmp(mname, "pc-0.15") == 0) ||
        (strcmp(mname, "pc-0.14") == 0) ||
        (strcmp(mname, "pc-0.13") == 0) ||
        (strcmp(mname, "pc-0.12") == 0)) {
        path = g_strdup_printf("cpu-plug/%s/init/%ux%ux%u&maxcpus=%u",
                               mname, data->sockets, data->cores,
                               data->threads, data->maxcpus);
        qtest_add_data_func_full(path, data, test_plug_without_cpu_add,
                                 test_data_free);
        g_free(path);
    } else {
        PlugTestData *data2 = g_memdup(data, sizeof(PlugTestData));

        data2->machine = g_strdup(data->machine);
        data2->device_model = g_strdup(data->device_model);

        path = g_strdup_printf("cpu-plug/%s/cpu-add/%ux%ux%u&maxcpus=%u",
                               mname, data->sockets, data->cores,
                               data->threads, data->maxcpus);
        qtest_add_data_func_full(path, data, test_plug_with_cpu_add,
                                 test_data_free);
        g_free(path);
        path = g_strdup_printf("cpu-plug/%s/device-add/%ux%ux%u&maxcpus=%u",
                               mname, data2->sockets, data2->cores,
                               data2->threads, data2->maxcpus);
        qtest_add_data_func_full(path, data2, test_plug_with_device_add,
                                 test_data_free);
        g_free(path);
    }
}

static void add_pseries_test_case(const char *mname)
{
    char *path;
    PlugTestData *data;

    if (!g_str_has_prefix(mname, "pseries-") ||
        (g_str_has_prefix(mname, "pseries-2.") && atoi(&mname[10]) < 7)) {
        return;
    }
    data = g_new(PlugTestData, 1);
    data->machine = g_strdup(mname);
    data->cpu_model = "power8_v2.0";
    data->device_model = g_strdup("power8_v2.0-spapr-cpu-core");
    data->sockets = 2;
    data->cores = 3;
    data->threads = 1;
    data->maxcpus = data->sockets * data->cores * data->threads;

    path = g_strdup_printf("cpu-plug/%s/device-add/%ux%ux%u&maxcpus=%u",
                           mname, data->sockets, data->cores,
                           data->threads, data->maxcpus);
    qtest_add_data_func_full(path, data, test_plug_with_device_add,
                             test_data_free);
    g_free(path);
}

static void add_s390x_test_case(const char *mname)
{
    char *path;
    PlugTestData *data, *data2;

    if (!g_str_has_prefix(mname, "s390-ccw-virtio-")) {
        return;
    }

    data = g_new(PlugTestData, 1);
    data->machine = g_strdup(mname);
    data->cpu_model = "qemu";
    data->device_model = g_strdup("qemu-s390x-cpu");
    data->sockets = 1;
    data->cores = 3;
    data->threads = 1;
    data->maxcpus = data->sockets * data->cores * data->threads;

    data2 = g_memdup(data, sizeof(PlugTestData));
    data2->machine = g_strdup(data->machine);
    data2->device_model = g_strdup(data->device_model);

    path = g_strdup_printf("cpu-plug/%s/cpu-add/%ux%ux%u&maxcpus=%u",
                           mname, data->sockets, data->cores,
                           data->threads, data->maxcpus);
    qtest_add_data_func_full(path, data, test_plug_with_cpu_add,
                             test_data_free);
    g_free(path);

    path = g_strdup_printf("cpu-plug/%s/device-add/%ux%ux%u&maxcpus=%u",
                           mname, data2->sockets, data2->cores,
                           data2->threads, data2->maxcpus);
    qtest_add_data_func_full(path, data2, test_plug_with_device_add,
                             test_data_free);
    g_free(path);
}

int main(int argc, char **argv)
{
    const char *arch = qtest_get_arch();

    g_test_init(&argc, &argv, NULL);

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        qtest_cb_for_every_machine(add_pc_test_case, g_test_quick());
    } else if (g_str_equal(arch, "ppc64")) {
        qtest_cb_for_every_machine(add_pseries_test_case, g_test_quick());
    } else if (g_str_equal(arch, "s390x")) {
        qtest_cb_for_every_machine(add_s390x_test_case, g_test_quick());
    }

    return g_test_run();
}

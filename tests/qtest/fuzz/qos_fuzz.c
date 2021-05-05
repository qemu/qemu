/*
 * QOS-assisted fuzzing helpers
 *
 * Copyright (c) 2018 Emanuele Giuseppe Esposito <e.emanuelegiuseppe@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "exec/memory.h"
#include "qemu/main-loop.h"

#include "tests/qtest/libqos/libqtest.h"
#include "tests/qtest/libqos/malloc.h"
#include "tests/qtest/libqos/qgraph.h"
#include "tests/qtest/libqos/qgraph_internal.h"
#include "tests/qtest/libqos/qos_external.h"

#include "fuzz.h"
#include "qos_fuzz.h"

#include "qapi/qapi-commands-machine.h"
#include "qapi/qapi-commands-qom.h"


void *fuzz_qos_obj;
QGuestAllocator *fuzz_qos_alloc;

static const char *fuzz_target_name;
static char **fuzz_path_vec;

static void qos_set_machines_devices_available(void)
{
    MachineInfoList *mach_info;
    ObjectTypeInfoList *type_info;

    mach_info = qmp_query_machines(&error_abort);
    machines_apply_to_node(mach_info);
    qapi_free_MachineInfoList(mach_info);

    type_info = qmp_qom_list_types(true, "device", true, true,
                                   &error_abort);
    types_apply_to_node(type_info);
    qapi_free_ObjectTypeInfoList(type_info);
}

static char **current_path;

void *qos_allocate_objects(QTestState *qts, QGuestAllocator **p_alloc)
{
    return allocate_objects(qts, current_path + 1, p_alloc);
}

static GString *qos_build_main_args(void)
{
    char **path = fuzz_path_vec;
    QOSGraphNode *test_node;
    GString *cmd_line;
    void *test_arg;

    if (!path) {
        fprintf(stderr, "QOS Path not found\n");
        abort();
    }

    /* Before test */
    cmd_line = g_string_new(path[0]);
    current_path = path;
    test_node = qos_graph_get_node(path[(g_strv_length(path) - 1)]);
    test_arg = test_node->u.test.arg;
    if (test_node->u.test.before) {
        test_arg = test_node->u.test.before(cmd_line, test_arg);
    }
    /* Prepend the arguments that we need */
    g_string_prepend(cmd_line,
            TARGET_NAME " -display none -machine accel=qtest -m 64 ");
    return cmd_line;
}

/*
 * This function is largely a copy of qos-test.c:walk_path. Since walk_path
 * is itself a callback, its a little annoying to add another argument/layer of
 * indirection
 */
static void walk_path(QOSGraphNode *orig_path, int len)
{
    QOSGraphNode *path;
    QOSGraphEdge *edge;

    /*
     * etype set to QEDGE_CONSUMED_BY so that machine can add to the command
     * line
     */
    QOSEdgeType etype = QEDGE_CONSUMED_BY;

    /* twice QOS_PATH_MAX_ELEMENT_SIZE since each edge can have its arg */
    char **path_vec = g_new0(char *, (QOS_PATH_MAX_ELEMENT_SIZE * 2));
    int path_vec_size = 0;

    char *after_cmd, *before_cmd, *after_device;
    GString *after_device_str = g_string_new("");
    char *node_name = orig_path->name, *path_str;

    GString *cmd_line = g_string_new("");
    GString *cmd_line2 = g_string_new("");

    path = qos_graph_get_node(node_name); /* root */
    node_name = qos_graph_edge_get_dest(path->path_edge); /* machine name */

    path_vec[path_vec_size++] = node_name;
    path_vec[path_vec_size++] = qos_get_machine_type(node_name);

    for (;;) {
        path = qos_graph_get_node(node_name);
        if (!path->path_edge) {
            break;
        }

        node_name = qos_graph_edge_get_dest(path->path_edge);

        /* append node command line + previous edge command line */
        if (path->command_line && etype == QEDGE_CONSUMED_BY) {
            g_string_append(cmd_line, path->command_line);
            g_string_append(cmd_line, after_device_str->str);
            g_string_truncate(after_device_str, 0);
        }

        path_vec[path_vec_size++] = qos_graph_edge_get_name(path->path_edge);
        /* detect if edge has command line args */
        after_cmd = qos_graph_edge_get_after_cmd_line(path->path_edge);
        after_device = qos_graph_edge_get_extra_device_opts(path->path_edge);
        before_cmd = qos_graph_edge_get_before_cmd_line(path->path_edge);
        edge = qos_graph_get_edge(path->name, node_name);
        etype = qos_graph_edge_get_type(edge);

        if (before_cmd) {
            g_string_append(cmd_line, before_cmd);
        }
        if (after_cmd) {
            g_string_append(cmd_line2, after_cmd);
        }
        if (after_device) {
            g_string_append(after_device_str, after_device);
        }
    }

    path_vec[path_vec_size++] = NULL;
    g_string_append(cmd_line, after_device_str->str);
    g_string_free(after_device_str, true);

    g_string_append(cmd_line, cmd_line2->str);
    g_string_free(cmd_line2, true);

    /*
     * here position 0 has <arch>/<machine>, position 1 has <machine>.
     * The path must not have the <arch>, qtest_add_data_func adds it.
     */
    path_str = g_strjoinv("/", path_vec + 1);

    /* Check that this is the test we care about: */
    char *test_name = strrchr(path_str, '/') + 1;
    if (strcmp(test_name, fuzz_target_name) == 0) {
        /*
         * put arch/machine in position 1 so run_one_test can do its work
         * and add the command line at position 0.
         */
        path_vec[1] = path_vec[0];
        path_vec[0] = g_string_free(cmd_line, false);

        fuzz_path_vec = path_vec;
    } else {
        g_free(path_vec);
    }

    g_free(path_str);
}

static GString *qos_get_cmdline(FuzzTarget *t)
{
    /*
     * Set a global variable that we use to identify the qos_path for our
     * fuzz_target
     */
    fuzz_target_name = t->name;
    qos_set_machines_devices_available();
    qos_graph_foreach_test_path(walk_path);
    return qos_build_main_args();
}

void fuzz_add_qos_target(
        FuzzTarget *fuzz_opts,
        const char *interface,
        QOSGraphTestOptions *opts
        )
{
    qos_add_test(fuzz_opts->name, interface, NULL, opts);
    fuzz_opts->get_init_cmdline = qos_get_cmdline;
    fuzz_add_target(fuzz_opts);
}

void qos_init_path(QTestState *s)
{
    fuzz_qos_obj = qos_allocate_objects(s , &fuzz_qos_alloc);
}

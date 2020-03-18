/*
 * QOS-assisted fuzzing helpers
 *
 * Copyright (c) 2018 Emanuele Giuseppe Esposito <e.emanuelegiuseppe@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2 as published by the Free Software Foundation.
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
#include "exec/address-spaces.h"
#include "sysemu/sysemu.h"
#include "qemu/main-loop.h"

#include "tests/qtest/libqtest.h"
#include "tests/qtest/libqos/malloc.h"
#include "tests/qtest/libqos/qgraph.h"
#include "tests/qtest/libqos/qgraph_internal.h"
#include "tests/qtest/libqos/qos_external.h"

#include "fuzz.h"
#include "qos_fuzz.h"

#include "qapi/qapi-commands-machine.h"
#include "qapi/qapi-commands-qom.h"
#include "qapi/qmp/qlist.h"


void *fuzz_qos_obj;
QGuestAllocator *fuzz_qos_alloc;

static const char *fuzz_target_name;
static char **fuzz_path_vec;

/*
 * Replaced the qmp commands with direct qmp_marshal calls.
 * Probably there is a better way to do this
 */
static void qos_set_machines_devices_available(void)
{
    QDict *req = qdict_new();
    QObject *response;
    QDict *args = qdict_new();
    QList *lst;

    qmp_marshal_query_machines(NULL, &response, &error_abort);
    lst = qobject_to(QList, response);
    apply_to_qlist(lst, true);

    qobject_unref(response);


    qdict_put_str(req, "execute", "qom-list-types");
    qdict_put_str(args, "implements", "device");
    qdict_put_bool(args, "abstract", true);
    qdict_put_obj(req, "arguments", (QObject *) args);

    qmp_marshal_qom_list_types(args, &response, &error_abort);
    lst = qobject_to(QList, response);
    apply_to_qlist(lst, false);
    qobject_unref(response);
    qobject_unref(req);
}

static char **current_path;

void *qos_allocate_objects(QTestState *qts, QGuestAllocator **p_alloc)
{
    return allocate_objects(qts, current_path + 1, p_alloc);
}

static const char *qos_build_main_args(void)
{
    char **path = fuzz_path_vec;
    QOSGraphNode *test_node;
    GString *cmd_line = g_string_new(path[0]);
    void *test_arg;

    if (!path) {
        fprintf(stderr, "QOS Path not found\n");
        abort();
    }

    /* Before test */
    current_path = path;
    test_node = qos_graph_get_node(path[(g_strv_length(path) - 1)]);
    test_arg = test_node->u.test.arg;
    if (test_node->u.test.before) {
        test_arg = test_node->u.test.before(cmd_line, test_arg);
    }
    /* Prepend the arguments that we need */
    g_string_prepend(cmd_line,
            TARGET_NAME " -display none -machine accel=qtest -m 64 ");
    return cmd_line->str;
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

static const char *qos_get_cmdline(FuzzTarget *t)
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

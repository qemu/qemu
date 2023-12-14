/*
 * libqos driver framework
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
#include <getopt.h>
#include "../libqtest.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qbool.h"
#include "qapi/qmp/qstring.h"
#include "qemu/module.h"
#include "qapi/qmp/qlist.h"
#include "libqos-malloc.h"
#include "qgraph.h"
#include "qgraph_internal.h"
#include "qos_external.h"

static void machine_apply_to_node(const char *name)
{
    char *machine_name = g_strconcat(qtest_get_arch(), "/", name, NULL);

    qos_graph_node_set_availability(machine_name, true);
    g_free(machine_name);
}

void machines_apply_to_node(MachineInfoList *mach_info)
{
    MachineInfoList *tail;

    for (tail = mach_info; tail; tail = tail->next) {
        machine_apply_to_node(tail->value->name);
        if (tail->value->alias) {
            machine_apply_to_node(tail->value->alias);
        }
    }
}

static void type_apply_to_node(const char *name, bool is_abstract)
{
    qos_graph_node_set_availability(name, true);
    if (is_abstract) {
        qos_delete_cmd_line(name);
    }
}

void types_apply_to_node(ObjectTypeInfoList *type_info)
{
    ObjectTypeInfoList *tail;

    for (tail = type_info; tail; tail = tail->next) {
        type_apply_to_node(tail->value->name, tail->value->abstract);
    }
}

static QGuestAllocator *get_machine_allocator(QOSGraphObject *obj)
{
    return obj->get_driver(obj, "memory");
}

/**
 * allocate_objects(): given an array of nodes @arg,
 * walks the path invoking all constructors and
 * passing the corresponding parameter in order to
 * continue the objects allocation.
 * Once the test is reached, return the object it consumes.
 *
 * Since the machine and QEDGE_CONSUMED_BY nodes allocate
 * memory in the constructor, g_test_queue_destroy is used so
 * that after execution they can be safely free'd.  (The test's
 * ->before callback is also welcome to use g_test_queue_destroy).
 *
 * Note: as specified in walk_path() too, @arg is an array of
 * char *, where arg[0] is a pointer to the command line
 * string that will be used to properly start QEMU when executing
 * the test, and the remaining elements represent the actual objects
 * that will be allocated.
 */
void *allocate_objects(QTestState *qts, char **path, QGuestAllocator **p_alloc)
{
    int current = 0;
    QGuestAllocator *alloc;
    QOSGraphObject *parent = NULL;
    QOSGraphEdge *edge;
    QOSGraphNode *node;
    void *edge_arg;
    void *obj;

    node = qos_graph_get_node(path[current]);
    g_assert(node->type == QNODE_MACHINE);

    obj = qos_machine_new(node, qts);
    qos_object_queue_destroy(obj);

    alloc = get_machine_allocator(obj);
    if (p_alloc) {
        *p_alloc = alloc;
    }

    for (;;) {
        if (node->type != QNODE_INTERFACE) {
            qos_object_start_hw(obj);
            parent = obj;
        }

        /* follow edge and get object for next node constructor */
        current++;
        edge = qos_graph_get_edge(path[current - 1], path[current]);
        node = qos_graph_get_node(path[current]);

        if (node->type == QNODE_TEST) {
            g_assert(qos_graph_edge_get_type(edge) == QEDGE_CONSUMED_BY);
            return obj;
        }

        switch (qos_graph_edge_get_type(edge)) {
        case QEDGE_PRODUCES:
            obj = parent->get_driver(parent, path[current]);
            break;

        case QEDGE_CONSUMED_BY:
            edge_arg = qos_graph_edge_get_arg(edge);
            obj = qos_driver_new(node, obj, alloc, edge_arg);
            qos_object_queue_destroy(obj);
            break;

        case QEDGE_CONTAINS:
            obj = parent->get_device(parent, path[current]);
            break;
        }
    }
}


/*
 * libqos driver framework
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
#include <getopt.h>
#include "libqtest.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qbool.h"
#include "qapi/qmp/qstring.h"
#include "qemu/module.h"
#include "qapi/qmp/qlist.h"
#include "libqos/malloc.h"
#include "libqos/qgraph.h"
#include "libqos/qgraph_internal.h"
#include "libqos/qos_external.h"



void apply_to_node(const char *name, bool is_machine, bool is_abstract)
{
    char *machine_name = NULL;
    if (is_machine) {
        const char *arch = qtest_get_arch();
        machine_name = g_strconcat(arch, "/", name, NULL);
        name = machine_name;
    }
    qos_graph_node_set_availability(name, true);
    if (is_abstract) {
        qos_delete_cmd_line(name);
    }
    g_free(machine_name);
}

/**
 * apply_to_qlist(): using QMP queries QEMU for a list of
 * machines and devices available, and sets the respective node
 * as true. If a node is found, also all its produced and contained
 * child are marked available.
 *
 * See qos_graph_node_set_availability() for more info
 */
void apply_to_qlist(QList *list, bool is_machine)
{
    const QListEntry *p;
    const char *name;
    bool abstract;
    QDict *minfo;
    QObject *qobj;
    QString *qstr;
    QBool *qbool;

    for (p = qlist_first(list); p; p = qlist_next(p)) {
        minfo = qobject_to(QDict, qlist_entry_obj(p));
        qobj = qdict_get(minfo, "name");
        qstr = qobject_to(QString, qobj);
        name = qstring_get_str(qstr);

        qobj = qdict_get(minfo, "abstract");
        if (qobj) {
            qbool = qobject_to(QBool, qobj);
            abstract = qbool_get_bool(qbool);
        } else {
            abstract = false;
        }

        apply_to_node(name, is_machine, abstract);
        qobj = qdict_get(minfo, "alias");
        if (qobj) {
            qstr = qobject_to(QString, qobj);
            name = qstring_get_str(qstr);
            apply_to_node(name, is_machine, abstract);
        }
    }
}

QGuestAllocator *get_machine_allocator(QOSGraphObject *obj)
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


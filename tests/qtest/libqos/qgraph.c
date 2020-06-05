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
#include "libqtest.h"
#include "qemu/queue.h"
#include "libqos/qgraph_internal.h"
#include "libqos/qgraph.h"

#define QGRAPH_PRINT_DEBUG 0
#define QOS_ROOT ""
typedef struct QOSStackElement QOSStackElement;

/* Graph Edge.*/
struct QOSGraphEdge {
    QOSEdgeType type;
    char *dest;
    void *arg;                /* just for QEDGE_CONTAINS
                               * and QEDGE_CONSUMED_BY */
    char *extra_device_opts;  /* added to -device option, "," is
                               * automatically added
                               */
    char *before_cmd_line;    /* added before node cmd_line */
    char *after_cmd_line;     /* added after -device options */
    char *edge_name;          /* used by QEDGE_CONTAINS */
    QSLIST_ENTRY(QOSGraphEdge) edge_list;
};

typedef QSLIST_HEAD(, QOSGraphEdge) QOSGraphEdgeList;

/**
 * Stack used to keep track of the discovered path when using
 * the DFS algorithm
 */
struct QOSStackElement {
    QOSGraphNode *node;
    QOSStackElement *parent;
    QOSGraphEdge *parent_edge;
    int length;
};

/* Each enty in these hash table will consist of <string, node/edge> pair. */
static GHashTable *edge_table;
static GHashTable *node_table;

/* stack used by the DFS algorithm to store the path from machine to test */
static QOSStackElement qos_node_stack[QOS_PATH_MAX_ELEMENT_SIZE];
static int qos_node_tos;

/**
 * add_edge(): creates an edge of type @type
 * from @source to @dest node, and inserts it in the
 * edges hash table
 *
 * Nodes @source and @dest do not necessarily need to exist.
 * Possibility to add also options (see #QOSGraphEdgeOptions)
 * edge->edge_name is used as identifier for get_device relationships,
 * so by default is equal to @dest.
 */
static void add_edge(const char *source, const char *dest,
                     QOSEdgeType type, QOSGraphEdgeOptions *opts)
{
    char *key;
    QOSGraphEdgeList *list = g_hash_table_lookup(edge_table, source);
    QOSGraphEdgeOptions def_opts = { };

    if (!list) {
        list = g_new0(QOSGraphEdgeList, 1);
        key = g_strdup(source);
        g_hash_table_insert(edge_table, key, list);
    }

    if (!opts) {
        opts = &def_opts;
    }

    QOSGraphEdge *edge = g_new0(QOSGraphEdge, 1);
    edge->type = type;
    edge->dest = g_strdup(dest);
    edge->edge_name = g_strdup(opts->edge_name ?: dest);
    edge->arg = g_memdup(opts->arg, opts->size_arg);

    edge->before_cmd_line =
        opts->before_cmd_line ? g_strconcat(" ", opts->before_cmd_line, NULL) : NULL;
    edge->extra_device_opts =
        opts->extra_device_opts ? g_strconcat(",", opts->extra_device_opts, NULL) : NULL;
    edge->after_cmd_line =
        opts->after_cmd_line ? g_strconcat(" ", opts->after_cmd_line, NULL) : NULL;

    QSLIST_INSERT_HEAD(list, edge, edge_list);
}

/* destroy_edges(): frees all edges inside a given @list */
static void destroy_edges(void *list)
{
    QOSGraphEdge *temp;
    QOSGraphEdgeList *elist = list;

    while (!QSLIST_EMPTY(elist)) {
        temp = QSLIST_FIRST(elist);
        QSLIST_REMOVE_HEAD(elist, edge_list);
        g_free(temp->dest);
        g_free(temp->before_cmd_line);
        g_free(temp->after_cmd_line);
        g_free(temp->extra_device_opts);
        g_free(temp->edge_name);
        g_free(temp->arg);
        g_free(temp);
    }
    g_free(elist);
}

/**
 * create_node(): creates a node @name of type @type
 * and inserts it to the nodes hash table.
 * By default, node is not available.
 */
static QOSGraphNode *create_node(const char *name, QOSNodeType type)
{
    if (g_hash_table_lookup(node_table, name)) {
        g_printerr("Node %s already created\n", name);
        abort();
    }

    QOSGraphNode *node = g_new0(QOSGraphNode, 1);
    node->type = type;
    node->available = false;
    node->name = g_strdup(name);
    g_hash_table_insert(node_table, node->name, node);
    return node;
}

/**
 * destroy_node(): frees a node @val from the nodes hash table.
 * Note that node->name is not free'd since it will represent the
 * hash table key
 */
static void destroy_node(void *val)
{
    QOSGraphNode *node = val;
    g_free(node->command_line);
    g_free(node);
}

/**
 * destroy_string(): frees @key from the nodes hash table.
 * Actually frees the node->name
 */
static void destroy_string(void *key)
{
    g_free(key);
}

/**
 * search_node(): search for a node @key in the nodes hash table
 * Returns the QOSGraphNode if found, #NULL otherwise
 */
static QOSGraphNode *search_node(const char *key)
{
    return g_hash_table_lookup(node_table, key);
}

/**
 * get_edgelist(): returns the edge list (value) assigned to
 * the @key in the edge hash table.
 * This list will contain all edges with source equal to @key
 *
 * Returns: on success: the %QOSGraphEdgeList
 *          otherwise: abort()
 */
static QOSGraphEdgeList *get_edgelist(const char *key)
{
    return g_hash_table_lookup(edge_table, key);
}

/**
 * search_list_edges(): search for an edge with destination @dest
 * in the given @edgelist.
 *
 * Returns: on success: the %QOSGraphEdge
 *          otherwise: #NULL
 */
static QOSGraphEdge *search_list_edges(QOSGraphEdgeList *edgelist,
                                       const char *dest)
{
    QOSGraphEdge *tmp, *next;
    if (!edgelist) {
        return NULL;
    }
    QSLIST_FOREACH_SAFE(tmp, edgelist, edge_list, next) {
        if (g_strcmp0(tmp->dest, dest) == 0) {
            break;
        }
    }
    return tmp;
}

/**
 * search_machine(): search for a machine @name in the node hash
 * table. A machine is the child of the root node.
 * This function forces the research in the childs of the root,
 * to check the node is a proper machine
 *
 * Returns: on success: the %QOSGraphNode
 *          otherwise: #NULL
 */
static QOSGraphNode *search_machine(const char *name)
{
    QOSGraphNode *n;
    QOSGraphEdgeList *root_list = get_edgelist(QOS_ROOT);
    QOSGraphEdge *e = search_list_edges(root_list, name);
    if (!e) {
        return NULL;
    }
    n = search_node(e->dest);
    if (n->type == QNODE_MACHINE) {
        return n;
    }
    return NULL;
}

/**
 * create_interface(): checks if there is already
 * a node @node in the node hash table, if not
 * creates a node @node of type #QNODE_INTERFACE
 * and inserts it. If there is one, check it's
 * a #QNODE_INTERFACE and abort() if it's not.
 */
static void create_interface(const char *node)
{
    QOSGraphNode *interface;
    interface = search_node(node);
    if (!interface) {
        create_node(node, QNODE_INTERFACE);
    } else if (interface->type != QNODE_INTERFACE) {
        fprintf(stderr, "Error: Node %s is not an interface\n", node);
        abort();
    }
}

/**
 * build_machine_cmd_line(): builds the command line for the machine
 * @node. The node name must be a valid qemu identifier, since it
 * will be used to build the command line.
 *
 * It is also possible to pass an optional @args that will be
 * concatenated to the command line.
 *
 * For machines, prepend -M to the machine name. ", @rgs" is added
 * after the -M <machine> command.
 */
static void build_machine_cmd_line(QOSGraphNode *node, const char *args)
{
    char *machine = qos_get_machine_type(node->name);
    if (args) {
        node->command_line = g_strconcat("-M ", machine, ",", args, NULL);
    } else {
        node->command_line = g_strconcat("-M ", machine, " ", NULL);
    }
}

/**
 * build_driver_cmd_line(): builds the command line for the driver
 * @node. The node name must be a valid qemu identifier, since it
 * will be used to build the command line.
 *
 * Driver do not need additional command line, since it will be
 * provided by the edge options.
 *
 * For drivers, prepend -device to the node name.
 */
static void build_driver_cmd_line(QOSGraphNode *node)
{
    node->command_line = g_strconcat(" -device ", node->name, NULL);
}

/* qos_print_cb(): callback prints all path found by the DFS algorithm. */
static void qos_print_cb(QOSGraphNode *path, int length)
{
    #if QGRAPH_PRINT_DEBUG
        printf("%d elements\n", length);

        if (!path) {
            return;
        }

        while (path->path_edge) {
            printf("%s ", path->name);
            switch (path->path_edge->type) {
            case QEDGE_PRODUCES:
                printf("--PRODUCES--> ");
                break;
            case QEDGE_CONSUMED_BY:
                printf("--CONSUMED_BY--> ");
                break;
            case QEDGE_CONTAINS:
                printf("--CONTAINS--> ");
                break;
            }
            path = search_node(path->path_edge->dest);
        }

        printf("%s\n\n", path->name);
    #endif
}

/* qos_push(): push a node @el and edge @e in the qos_node_stack */
static void qos_push(QOSGraphNode *el, QOSStackElement *parent,
                     QOSGraphEdge *e)
{
    int len = 0; /* root is not counted */
    if (qos_node_tos == QOS_PATH_MAX_ELEMENT_SIZE) {
        g_printerr("QOSStack: full stack, cannot push");
        abort();
    }

    if (parent) {
        len = parent->length + 1;
    }
    qos_node_stack[qos_node_tos++] = (QOSStackElement) {
        .node = el,
        .parent = parent,
        .parent_edge = e,
        .length = len,
    };
}

/* qos_tos(): returns the top of stack, without popping */
static QOSStackElement *qos_tos(void)
{
    return &qos_node_stack[qos_node_tos - 1];
}

/* qos_pop(): pops an element from the tos, setting it unvisited*/
static QOSStackElement *qos_pop(void)
{
    if (qos_node_tos == 0) {
        g_printerr("QOSStack: empty stack, cannot pop");
        abort();
    }
    QOSStackElement *e = qos_tos();
    e->node->visited = false;
    qos_node_tos--;
    return e;
}

/**
 * qos_reverse_path(): reverses the found path, going from
 * test-to-machine to machine-to-test
 */
static QOSGraphNode *qos_reverse_path(QOSStackElement *el)
{
    if (!el) {
        return NULL;
    }

    el->node->path_edge = NULL;

    while (el->parent) {
        el->parent->node->path_edge = el->parent_edge;
        el = el->parent;
    }

    return el->node;
}

/**
 * qos_traverse_graph(): graph-walking algorithm, using Depth First Search it
 * starts from the root @machine and walks all possible path until it
 * reaches a test node.
 * At that point, it reverses the path found and invokes the @callback.
 *
 * Being Depth First Search, time complexity is O(|V| + |E|), while
 * space is O(|V|). In this case, the maximum stack size is set by
 * QOS_PATH_MAX_ELEMENT_SIZE.
 */
static void qos_traverse_graph(QOSGraphNode *root, QOSTestCallback callback)
{
    QOSGraphNode *v, *dest_node, *path;
    QOSStackElement *s_el;
    QOSGraphEdge *e, *next;
    QOSGraphEdgeList *list;

    qos_push(root, NULL, NULL);

    while (qos_node_tos > 0) {
        s_el = qos_tos();
        v = s_el->node;
        if (v->visited) {
            qos_pop();
            continue;
        }
        v->visited = true;
        list = get_edgelist(v->name);
        if (!list) {
            qos_pop();
            if (v->type == QNODE_TEST) {
                v->visited = false;
                path = qos_reverse_path(s_el);
                callback(path, s_el->length);
            }
        } else {
            QSLIST_FOREACH_SAFE(e, list, edge_list, next) {
                dest_node = search_node(e->dest);

                if (!dest_node) {
                    fprintf(stderr, "node %s in %s -> %s does not exist\n",
                            e->dest, v->name, e->dest);
                    abort();
                }

                if (!dest_node->visited && dest_node->available) {
                    qos_push(dest_node, s_el, e);
                }
            }
        }
    }
}

/* QGRAPH API*/

QOSGraphNode *qos_graph_get_node(const char *key)
{
    return search_node(key);
}

bool qos_graph_has_node(const char *node)
{
    QOSGraphNode *n = search_node(node);
    return n != NULL;
}

QOSNodeType qos_graph_get_node_type(const char *node)
{
    QOSGraphNode *n = search_node(node);
    if (n) {
        return n->type;
    }
    return -1;
}

bool qos_graph_get_node_availability(const char *node)
{
    QOSGraphNode *n = search_node(node);
    if (n) {
        return n->available;
    }
    return false;
}

QOSGraphEdge *qos_graph_get_edge(const char *node, const char *dest)
{
    QOSGraphEdgeList *list = get_edgelist(node);
    return search_list_edges(list, dest);
}

QOSEdgeType qos_graph_edge_get_type(QOSGraphEdge *edge)
{
    if (!edge) {
        return -1;
    }
    return edge->type;
}

char *qos_graph_edge_get_dest(QOSGraphEdge *edge)
{
    if (!edge) {
        return NULL;
    }
    return edge->dest;
}

void *qos_graph_edge_get_arg(QOSGraphEdge *edge)
{
    if (!edge) {
        return NULL;
    }
    return edge->arg;
}

char *qos_graph_edge_get_after_cmd_line(QOSGraphEdge *edge)
{
    if (!edge) {
        return NULL;
    }
    return edge->after_cmd_line;
}

char *qos_graph_edge_get_before_cmd_line(QOSGraphEdge *edge)
{
    if (!edge) {
        return NULL;
    }
    return edge->before_cmd_line;
}

char *qos_graph_edge_get_extra_device_opts(QOSGraphEdge *edge)
{
    if (!edge) {
        return NULL;
    }
    return edge->extra_device_opts;
}

char *qos_graph_edge_get_name(QOSGraphEdge *edge)
{
    if (!edge) {
        return NULL;
    }
    return edge->edge_name;
}

bool qos_graph_has_edge(const char *start, const char *dest)
{
    QOSGraphEdgeList *list = get_edgelist(start);
    QOSGraphEdge *e = search_list_edges(list, dest);
    return e != NULL;
}

QOSGraphNode *qos_graph_get_machine(const char *node)
{
    return search_machine(node);
}

bool qos_graph_has_machine(const char *node)
{
    QOSGraphNode *m = search_machine(node);
    return m != NULL;
}

void qos_print_graph(void)
{
    qos_graph_foreach_test_path(qos_print_cb);
}

void qos_graph_init(void)
{
    if (!node_table) {
        node_table = g_hash_table_new_full(g_str_hash, g_str_equal,
                                           destroy_string, destroy_node);
        create_node(QOS_ROOT, QNODE_DRIVER);
    }

    if (!edge_table) {
        edge_table = g_hash_table_new_full(g_str_hash, g_str_equal,
                                           destroy_string, destroy_edges);
    }
}

void qos_graph_destroy(void)
{
    if (node_table) {
        g_hash_table_destroy(node_table);
    }

    if (edge_table) {
        g_hash_table_destroy(edge_table);
    }

    node_table = NULL;
    edge_table = NULL;
}

void qos_node_destroy(void *key)
{
    g_hash_table_remove(node_table, key);
}

void qos_edge_destroy(void *key)
{
    g_hash_table_remove(edge_table, key);
}

void qos_add_test(const char *name, const char *interface,
                  QOSTestFunc test_func, QOSGraphTestOptions *opts)
{
    QOSGraphNode *node;
    char *test_name = g_strdup_printf("%s-tests/%s", interface, name);
    QOSGraphTestOptions def_opts = { };

    if (!opts) {
        opts = &def_opts;
    }
    node = create_node(test_name, QNODE_TEST);
    node->u.test.function = test_func;
    node->u.test.arg = opts->arg;
    assert(!opts->edge.arg);
    assert(!opts->edge.size_arg);

    node->u.test.before = opts->before;
    node->u.test.subprocess = opts->subprocess;
    node->available = true;
    add_edge(interface, test_name, QEDGE_CONSUMED_BY, &opts->edge);
    g_free(test_name);
}

void qos_node_create_machine(const char *name, QOSCreateMachineFunc function)
{
    qos_node_create_machine_args(name, function, NULL);
}

void qos_node_create_machine_args(const char *name,
                                  QOSCreateMachineFunc function,
                                  const char *opts)
{
    QOSGraphNode *node = create_node(name, QNODE_MACHINE);
    build_machine_cmd_line(node, opts);
    node->u.machine.constructor = function;
    add_edge(QOS_ROOT, name, QEDGE_CONTAINS, NULL);
}

void qos_node_create_driver(const char *name, QOSCreateDriverFunc function)
{
    QOSGraphNode *node = create_node(name, QNODE_DRIVER);
    build_driver_cmd_line(node);
    node->u.driver.constructor = function;
}

void qos_node_contains(const char *container, const char *contained,
                       QOSGraphEdgeOptions *opts, ...)
{
    va_list va;

    if (opts == NULL) {
        add_edge(container, contained, QEDGE_CONTAINS, NULL);
        return;
    }

    va_start(va, opts);
    do {
        add_edge(container, contained, QEDGE_CONTAINS, opts);
        opts = va_arg(va, QOSGraphEdgeOptions *);
    } while (opts != NULL);

    va_end(va);
}

void qos_node_produces(const char *producer, const char *interface)
{
    create_interface(interface);
    add_edge(producer, interface, QEDGE_PRODUCES, NULL);
}

void qos_node_consumes(const char *consumer, const char *interface,
                       QOSGraphEdgeOptions *opts)
{
    create_interface(interface);
    add_edge(interface, consumer, QEDGE_CONSUMED_BY, opts);
}

void qos_graph_node_set_availability(const char *node, bool av)
{
    QOSGraphEdgeList *elist;
    QOSGraphNode *n = search_node(node);
    QOSGraphEdge *e, *next;
    if (!n) {
        return;
    }
    n->available = av;
    elist = get_edgelist(node);
    if (!elist) {
        return;
    }
    QSLIST_FOREACH_SAFE(e, elist, edge_list, next) {
        if (e->type == QEDGE_CONTAINS || e->type == QEDGE_PRODUCES) {
            qos_graph_node_set_availability(e->dest, av);
        }
    }
}

void qos_graph_foreach_test_path(QOSTestCallback fn)
{
    QOSGraphNode *root = qos_graph_get_node(QOS_ROOT);
    qos_traverse_graph(root, fn);
}

QOSGraphObject *qos_machine_new(QOSGraphNode *node, QTestState *qts)
{
    QOSGraphObject *obj;

    g_assert(node->type == QNODE_MACHINE);
    obj = node->u.machine.constructor(qts);
    obj->free = g_free;
    return obj;
}

QOSGraphObject *qos_driver_new(QOSGraphNode *node, QOSGraphObject *parent,
                               QGuestAllocator *alloc, void *arg)
{
    QOSGraphObject *obj;

    g_assert(node->type == QNODE_DRIVER);
    obj = node->u.driver.constructor(parent, alloc, arg);
    obj->free = g_free;
    return obj;
}

void qos_object_destroy(QOSGraphObject *obj)
{
    if (!obj) {
        return;
    }
    if (obj->destructor) {
        obj->destructor(obj);
    }
    if (obj->free) {
        obj->free(obj);
    }
}

void qos_object_queue_destroy(QOSGraphObject *obj)
{
    g_test_queue_destroy((GDestroyNotify) qos_object_destroy, obj);
}

void qos_object_start_hw(QOSGraphObject *obj)
{
    if (obj->start_hw) {
        obj->start_hw(obj);
    }
}

char *qos_get_machine_type(char *name)
{
    while (*name != '\0' && *name != '/') {
        name++;
    }

    if (!*name || !name[1]) {
        fprintf(stderr, "Machine name has to be of the form <arch>/<machine>\n");
        abort();
    }

    return name + 1;
}

void qos_delete_cmd_line(const char *name)
{
    QOSGraphNode *node = search_node(name);
    if (node) {
        g_free(node->command_line);
        node->command_line = NULL;
    }
}

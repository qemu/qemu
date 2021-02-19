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

#ifndef QGRAPH_INTERNAL_H
#define QGRAPH_INTERNAL_H

/* This header is declaring additional helper functions defined in
 * qgraph.c
 * It should not be included in tests
 */

#include "qgraph.h"

typedef struct QOSGraphMachine QOSGraphMachine;
typedef enum QOSEdgeType QOSEdgeType;
typedef enum QOSNodeType QOSNodeType;

/* callback called when the walk path algorithm found a
 * valid path
 */
typedef void (*QOSTestCallback) (QOSGraphNode *path, int len);

/* edge types*/
enum QOSEdgeType {
    QEDGE_CONTAINS,
    QEDGE_PRODUCES,
    QEDGE_CONSUMED_BY
};

/* node types*/
enum QOSNodeType {
    QNODE_MACHINE,
    QNODE_DRIVER,
    QNODE_INTERFACE,
    QNODE_TEST
};

/* Graph Node */
struct QOSGraphNode {
    QOSNodeType type;
    bool available;     /* set by QEMU via QMP, used during graph walk */
    bool visited;       /* used during graph walk */
    char *name;         /* used to identify the node */
    char *qemu_name;    /* optional: see qos_node_create_driver_named() */
    char *command_line; /* used to start QEMU at test execution */
    union {
        struct {
            QOSCreateDriverFunc constructor;
        } driver;
        struct {
            QOSCreateMachineFunc constructor;
        } machine;
        struct {
            QOSTestFunc function;
            void *arg;
            QOSBeforeTest before;
            bool subprocess;
        } test;
    } u;

    /**
     * only used when traversing the path, never rely on that except in the
     * qos_traverse_graph callback function
     */
    QOSGraphEdge *path_edge;
};

/**
 * qos_graph_get_node(): returns the node mapped to that @key.
 * It performs an hash map search O(1)
 *
 * Returns: on success: the %QOSGraphNode
 *          otherwise: #NULL
 */
QOSGraphNode *qos_graph_get_node(const char *key);

/**
 * qos_graph_has_node(): returns #TRUE if the node
 * has map has a node mapped to that @key.
 */
bool qos_graph_has_node(const char *node);

/**
 * qos_graph_get_node_type(): returns the %QOSNodeType
 * of the node @node.
 * It performs an hash map search O(1)
 * Returns: on success: the %QOSNodeType
 *          otherwise: #-1
 */
QOSNodeType qos_graph_get_node_type(const char *node);

/**
 * qos_graph_get_node_availability(): returns the availability (boolean)
 * of the node @node.
 */
bool qos_graph_get_node_availability(const char *node);

/**
 * qos_graph_get_edge(): returns the edge
 * linking of the node @node with @dest.
 *
 * Returns: on success: the %QOSGraphEdge
 *          otherwise: #NULL
 */
QOSGraphEdge *qos_graph_get_edge(const char *node, const char *dest);

/**
 * qos_graph_edge_get_type(): returns the edge type
 * of the edge @edge.
 *
 * Returns: on success: the %QOSEdgeType
 *          otherwise: #-1
 */
QOSEdgeType qos_graph_edge_get_type(QOSGraphEdge *edge);

/**
 * qos_graph_edge_get_dest(): returns the name of the node
 * pointed as destination of edge @edge.
 *
 * Returns: on success: the destination
 *          otherwise: #NULL
 */
char *qos_graph_edge_get_dest(QOSGraphEdge *edge);

/**
 * qos_graph_has_edge(): returns #TRUE if there
 * exists an edge from @start to @dest.
 */
bool qos_graph_has_edge(const char *start, const char *dest);

/**
 * qos_graph_edge_get_arg(): returns the args assigned
 * to that @edge.
 *
 * Returns: on success: the arg
 *          otherwise: #NULL
 */
void *qos_graph_edge_get_arg(QOSGraphEdge *edge);

/**
 * qos_graph_edge_get_after_cmd_line(): returns the edge
 * command line that will be added after all the node arguments
 * and all the before_cmd_line arguments.
 *
 * Returns: on success: the char* arg
 *          otherwise: #NULL
 */
char *qos_graph_edge_get_after_cmd_line(QOSGraphEdge *edge);

/**
 * qos_graph_edge_get_before_cmd_line(): returns the edge
 * command line that will be added before the node command
 * line argument.
 *
 * Returns: on success: the char* arg
 *          otherwise: #NULL
 */
char *qos_graph_edge_get_before_cmd_line(QOSGraphEdge *edge);

/**
 * qos_graph_edge_get_extra_device_opts(): returns the arg
 * command line that will be added to the node command
 * line argument.
 *
 * Returns: on success: the char* arg
 *          otherwise: #NULL
 */
char *qos_graph_edge_get_extra_device_opts(QOSGraphEdge *edge);

/**
 * qos_graph_edge_get_name(): returns the name
 * assigned to the destination node (different only)
 * if there are multiple devices with the same node name
 * e.g. a node has two "generic-sdhci", "emmc" and "sdcard"
 * there will be two edges with edge_name ="emmc" and "sdcard"
 *
 * Returns always the char* edge_name
 */
char *qos_graph_edge_get_name(QOSGraphEdge *edge);

/**
 * qos_graph_get_machine(): returns the machine assigned
 * to that @node name.
 *
 * It performs a search only trough the list of machines
 * (i.e. the QOS_ROOT child).
 *
 * Returns: on success: the %QOSGraphNode
 *          otherwise: #NULL
 */
QOSGraphNode *qos_graph_get_machine(const char *node);

/**
 * qos_graph_has_machine(): returns #TRUE if the node
 * has map has a node mapped to that @node.
 */
bool qos_graph_has_machine(const char *node);


/**
 * qos_print_graph(): walks the graph and prints
 * all machine-to-test paths.
 */
void qos_print_graph(void);

/**
 * qos_graph_foreach_test_path(): executes the Depth First search
 * algorithm and applies @fn to all discovered paths.
 *
 * See qos_traverse_graph() in qgraph.c for more info on
 * how it works.
 */
void qos_graph_foreach_test_path(QOSTestCallback fn);

/**
 * qos_get_machine_type(): return QEMU machine type for a machine node.
 * This function requires every machine @name to be in the form
 * <arch>/<machine_name>, like "arm/raspi2" or "x86_64/pc".
 *
 * The function will validate the format and return a pointer to
 * @machine to <machine_name>.  For example, when passed "x86_64/pc"
 * it will return "pc".
 *
 * Note that this function *does not* allocate any new string.
 */
char *qos_get_machine_type(char *name);

/**
 * qos_delete_cmd_line(): delete the
 * command line present in node mapped with key @name.
 *
 * This function is called when the QMP query returns a node with
 * { "abstract" : true } attribute.
 */
void qos_delete_cmd_line(const char *name);

/**
 * qos_graph_node_set_availability(): sets the node identified
 * by @node with availability @av.
 */
void qos_graph_node_set_availability(const char *node, bool av);

/*
 * Prepends a '#' character in front for not breaking TAP output format.
 */
#define qos_printf(...) printf("# " __VA_ARGS__)

/*
 * Intended for printing something literally, i.e. for appending text as is
 * to a line already been started by qos_printf() before.
 */
#define qos_printf_literal printf

#endif

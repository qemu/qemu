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

#ifndef QGRAPH_H
#define QGRAPH_H

#include <gmodule.h>
#include "qemu/module.h"
#include "malloc.h"

/* maximum path length */
#define QOS_PATH_MAX_ELEMENT_SIZE 50

typedef struct QOSGraphObject QOSGraphObject;
typedef struct QOSGraphNode QOSGraphNode;
typedef struct QOSGraphEdge QOSGraphEdge;
typedef struct QOSGraphEdgeOptions QOSGraphEdgeOptions;
typedef struct QOSGraphTestOptions QOSGraphTestOptions;

/* Constructor for drivers, machines and test */
typedef void *(*QOSCreateDriverFunc) (void *parent, QGuestAllocator *alloc,
                                      void *addr);
typedef void *(*QOSCreateMachineFunc) (QTestState *qts);
typedef void (*QOSTestFunc) (void *parent, void *arg, QGuestAllocator *alloc);

/* QOSGraphObject functions */
typedef void *(*QOSGetDriver) (void *object, const char *interface);
typedef QOSGraphObject *(*QOSGetDevice) (void *object, const char *name);
typedef void (*QOSDestructorFunc) (QOSGraphObject *object);
typedef void (*QOSStartFunct) (QOSGraphObject *object);

/* Test options functions */
typedef void *(*QOSBeforeTest) (GString *cmd_line, void *arg);

/**
 * struct QOSGraphEdgeOptions:
 * Edge options to be passed to the contains/consumes \*_args function.
 * @arg: optional arg that will be used by dest edge
 * @size_arg: @arg size that will be used by dest edge
 * @extra_device_opts: optional additional command line for dest
 *                     edge, used to add additional attributes
 *                     *after* the node command line, the
 *                     framework automatically prepends ","
 *                     to this argument.
 * @before_cmd_line: optional additional command line for dest
 *                   edge, used to add additional attributes
 *                   *before* the node command line, usually
 *                   other non-node represented commands,
 *                   like "-fdsev synt"
 * @after_cmd_line: optional extra command line to be added
 *                  after the device command. This option
 *                  is used to add other devices
 *                  command line that depend on current node.
 *                  Automatically prepends " " to this argument
 * @edge_name: optional edge to differentiate multiple
 *             devices with same node name
 */
struct QOSGraphEdgeOptions {
    void *arg;
    uint32_t size_arg;
    const char *extra_device_opts;
    const char *before_cmd_line;
    const char *after_cmd_line;
    const char *edge_name;
};

/**
 * struct QOSGraphTestOptions:
 * Test options to be passed to the test functions.
 * @edge: edge arguments that will be used by test.
 *        Note that test *does not* use edge_name,
 *        and uses instead arg and size_arg as
 *        data arg for its test function.
 * @arg:  if @before is non-NULL, pass @arg there.
 *        Otherwise pass it to the test function.
 * @before: executed before the test. Used to add
 *          additional parameters to the command line
 *          and modify the argument to the test function.
 * @subprocess: run the test in a subprocess.
 */
struct QOSGraphTestOptions {
    QOSGraphEdgeOptions edge;
    void *arg;
    QOSBeforeTest before;
    bool subprocess;
};

/**
 * struct QOSGraphObject:
 * Each driver, test or machine of this framework will have a
 * QOSGraphObject as first field.
 *
 * This set of functions offered by QOSGraphObject are executed
 * in different stages of the framework:
 * @get_driver: see @get_device
 * @get_device: Once a machine-to-test path has been
 *              found, the framework traverses it again and allocates all the
 *              nodes, using the provided constructor. To satisfy their
 *              relations, i.e. for produces or contains, where a struct
 *              constructor needs an external parameter represented by the
 *              previous node, the framework will call
 *              @get_device (for contains) or @get_driver (for produces),
 *              depending on the edge type, passing them the name of the next
 *              node to be taken and getting from them the corresponding
 *              pointer to the actual structure of the next node to
 *              be used in the path.
 * @start_hw: This function is executed after all the path objects
 *            have been allocated, but before the test is run. It starts the
 *            hw, setting the initial configurations (\*_device_enable) and
 *            making it ready for the test.
 * @destructor: Opposite to the node constructor, destroys the object.
 *              This function is called after the test has been executed, and
 *              performs a complete cleanup of each node allocated field.
 *              In case no constructor is provided, no destructor will be
 *              called.
 * @free: free the memory associated to the QOSGraphObject and its contained
 *        children
 */
struct QOSGraphObject {
    QOSGetDriver get_driver;
    QOSGetDevice get_device;
    QOSStartFunct start_hw;
    QOSDestructorFunc destructor;
    GDestroyNotify free;
};

/**
 * qos_graph_init(): initialize the framework, creates two hash
 * tables: one for the nodes and another for the edges.
 */
void qos_graph_init(void);

/**
 * qos_graph_destroy(): deallocates all the hash tables,
 * freeing all nodes and edges.
 */
void qos_graph_destroy(void);

/**
 * qos_node_destroy(): removes and frees a node from the
 * nodes hash table.
 * @key: Name of the node
 */
void qos_node_destroy(void *key);

/**
 * qos_edge_destroy(): removes and frees an edge from the
 * edges hash table.
 * @key: Name of the node
 */
void qos_edge_destroy(void *key);

/**
 * qos_add_test(): adds a test node @name to the nodes hash table.
 * @name: Name of the test
 * @interface: Name of the interface node it consumes
 * @test_func: Actual test to perform
 * @opts: Facultative options (see %QOSGraphTestOptions)
 *
 * The test will consume a @interface node, and once the
 * graph walking algorithm has found it, the @test_func will be
 * executed. It also has the possibility to
 * add an optional @opts (see %QOSGraphTestOptions).
 *
 * For tests, opts->edge.arg and size_arg represent the arg to pass
 * to @test_func
 */
void qos_add_test(const char *name, const char *interface,
                  QOSTestFunc test_func,
                  QOSGraphTestOptions *opts);

/**
 * qos_node_create_machine(): creates the machine @name and
 * adds it to the node hash table.
 * @name: Name of the machine
 * @function: Machine constructor
 *
 * This node will be of type QNODE_MACHINE and have @function
 * as constructor
 */
void qos_node_create_machine(const char *name, QOSCreateMachineFunc function);

/**
 * qos_node_create_machine_args(): same as qos_node_create_machine,
 * but with the possibility to add an optional ", @opts" after -M machine
 * command line.
 * @name: Name of the machine
 * @function: Machine constructor
 * @opts: Optional additional command line
 */
void qos_node_create_machine_args(const char *name,
                                  QOSCreateMachineFunc function,
                                  const char *opts);

/**
 * qos_node_create_driver(): creates the driver @name and
 * adds it to the node hash table.
 * @name: Name of the driver
 * @function: Driver constructor
 *
 * This node will be of type QNODE_DRIVER and have @function
 * as constructor
 */
void qos_node_create_driver(const char *name, QOSCreateDriverFunc function);

/**
 * qos_node_create_driver_named(): behaves as qos_node_create_driver() with the
 * extension of allowing to specify a different node name vs. associated QEMU
 * device name.
 * @name: Custom, unique name of the node to be created
 * @qemu_name: Actual (official) QEMU driver name the node shall be
 * associated with
 * @function: Driver constructor
 *
 * Use this function instead of qos_node_create_driver() if you need to create
 * several instances of the same QEMU device. You are free to choose a custom
 * node name, however the chosen node name must always be unique.
 */
void qos_node_create_driver_named(const char *name, const char *qemu_name,
                                  QOSCreateDriverFunc function);

/**
 * qos_node_contains(): creates one or more edges of type QEDGE_CONTAINS
 * and adds them to the edge list mapped to @container in the
 * edge hash table.
 * @container: Source node that "contains"
 * @contained: Destination node that "is contained"
 * @opts: Facultative options (see %QOSGraphEdgeOptions)
 *
 * The edges will have @container as source and @contained as destination.
 *
 * If @opts is NULL, a single edge will be added with no options.
 * If @opts is non-NULL, the arguments after @contained represent a
 * NULL-terminated list of %QOSGraphEdgeOptions structs, and an
 * edge will be added for each of them.
 *
 * This function can be useful when there are multiple devices
 * with the same node name contained in a machine/other node
 *
 * For example, if ``arm/raspi2b`` contains 2 ``generic-sdhci``
 * devices, the right commands will be:
 *
 * .. code::
 *
 *    qos_node_create_machine("arm/raspi2b");
 *    qos_node_create_driver("generic-sdhci", constructor);
 *    // assume rest of the fields are set NULL
 *    QOSGraphEdgeOptions op1 = { .edge_name = "emmc" };
 *    QOSGraphEdgeOptions op2 = { .edge_name = "sdcard" };
 *    qos_node_contains("arm/raspi2b", "generic-sdhci", &op1, &op2, NULL);
 *
 * Of course this also requires that the @container's get_device function
 * should implement a case for "emmc" and "sdcard".
 *
 * For contains, op1.arg and op1.size_arg represent the arg to pass
 * to @contained constructor to properly initialize it.
 */
void qos_node_contains(const char *container, const char *contained,
                       QOSGraphEdgeOptions *opts, ...);

/**
 * qos_node_produces(): creates an edge of type QEDGE_PRODUCES and
 * adds it to the edge list mapped to @producer in the
 * edge hash table.
 * @producer: Source node that "produces"
 * @interface: Interface node that "is produced"
 *
 * This edge will have @producer as source and @interface as destination.
 */
void qos_node_produces(const char *producer, const char *interface);

/**
 * qos_node_consumes():  creates an edge of type QEDGE_CONSUMED_BY and
 * adds it to the edge list mapped to @interface in the
 * edge hash table.
 * @consumer: Node that "consumes"
 * @interface: Interface node that "is consumed by"
 * @opts: Facultative options (see %QOSGraphEdgeOptions)
 *
 * This edge will have @interface as source and @consumer as destination.
 * It also has the possibility to add an optional @opts
 * (see %QOSGraphEdgeOptions)
 */
void qos_node_consumes(const char *consumer, const char *interface,
                       QOSGraphEdgeOptions *opts);

/**
 * qos_invalidate_command_line(): invalidates current command line, so that
 * qgraph framework cannot try to cache the current command line and
 * forces QEMU to restart.
 */
void qos_invalidate_command_line(void);

/**
 * qos_get_current_command_line(): return the command line required by the
 * machine and driver objects.  This is the same string that was passed to
 * the test's "before" callback, if any.
 */
const char *qos_get_current_command_line(void);

/**
 * qos_allocate_objects():
 * @qts: The #QTestState that will be referred to by the machine object.
 * @p_alloc: Where to store the allocator for the machine object, or %NULL.
 *
 * Allocate driver objects for the current test
 * path, but relative to the QTestState @qts.
 *
 * Returns a test object just like the one that was passed to
 * the test function, but relative to @qts.
 */
void *qos_allocate_objects(QTestState *qts, QGuestAllocator **p_alloc);

/**
 * qos_object_destroy(): calls the destructor for @obj
 * @obj: A #QOSGraphObject to destroy
 */
void qos_object_destroy(QOSGraphObject *obj);

/**
 * qos_object_queue_destroy(): queue the destructor for @obj so that it is
 * called at the end of the test
 * @obj: A #QOSGraphObject to destroy
 */
void qos_object_queue_destroy(QOSGraphObject *obj);

/**
 * qos_object_start_hw(): calls the start_hw function for @obj
 * @obj: A #QOSGraphObject containing the start_hw function
 */
void qos_object_start_hw(QOSGraphObject *obj);

/**
 * qos_machine_new(): instantiate a new machine node
 * @node: Machine node to be instantiated
 * @qts: A #QTestState that will be referred to by the machine object.
 *
 * Returns a machine object.
 */
QOSGraphObject *qos_machine_new(QOSGraphNode *node, QTestState *qts);

/**
 * qos_machine_new(): instantiate a new driver node
 * @node: A driver node to be instantiated
 * @parent: A #QOSGraphObject to be consumed by the new driver node
 * @alloc: An allocator to be used by the new driver node.
 * @arg: The argument for the consumed-by edge to @node.
 *
 * Calls the constructor for the driver object.
 */
QOSGraphObject *qos_driver_new(QOSGraphNode *node, QOSGraphObject *parent,
                               QGuestAllocator *alloc, void *arg);

/**
 * qos_dump_graph(): prints all currently existing nodes and
 * edges to stdout. Just for debugging purposes.
 *
 * All qtests add themselves to the overall qos graph by calling qgraph
 * functions that add device nodes and edges between the individual graph
 * nodes for tests. As the actual graph is assmbled at runtime by the qos
 * subsystem, it is sometimes not obvious how the overall graph looks like.
 * E.g. when writing new tests it may happen that those new tests are simply
 * ignored by the qtest framework.
 *
 * This function allows to identify problems in the created qgraph. Keep in
 * mind: only tests with a path down from the actual test case node (leaf) up
 * to the graph's root node are actually executed by the qtest framework. And
 * the qtest framework uses QMP to automatically check which QEMU drivers are
 * actually currently available, and accordingly qos marks certain pathes as
 * 'unavailable' in such cases (e.g. when QEMU was compiled without support for
 * a certain feature).
 */
void qos_dump_graph(void);

#endif

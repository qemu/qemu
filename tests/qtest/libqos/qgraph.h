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
typedef struct QOSGraphNodeOptions QOSGraphNodeOptions;
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
 * SECTION: qgraph.h
 * @title: Qtest Driver Framework
 * @short_description: interfaces to organize drivers and tests
 *                     as nodes in a graph
 *
 * This Qgraph API provides all basic functions to create a graph
 * and instantiate nodes representing machines, drivers and tests
 * representing their relations with CONSUMES, PRODUCES, and CONTAINS
 * edges.
 *
 * The idea is to have a framework where each test asks for a specific
 * driver, and the framework takes care of allocating the proper devices
 * required and passing the correct command line arguments to QEMU.
 *
 * A node can be of four types:
 * - QNODE_MACHINE:   for example "arm/raspi2"
 * - QNODE_DRIVER:    for example "generic-sdhci"
 * - QNODE_INTERFACE: for example "sdhci" (interface for all "-sdhci" drivers)
 *                     an interface is not explicitly created, it will be auto-
 *                     matically instantiated when a node consumes or produces
 *                     it.
 * - QNODE_TEST:      for example "sdhci-test", consumes an interface and tests
 *                    the functions provided
 *
 * Notes for the nodes:
 * - QNODE_MACHINE: each machine struct must have a QGuestAllocator and
 *                  implement get_driver to return the allocator passing
 *                  "memory". The function can also return NULL if the
 *                  allocator is not set.
 * - QNODE_DRIVER:  driver names must be unique, and machines and nodes
 *                  planned to be "consumed" by other nodes must match QEMU
 *                  drivers name, otherwise they won't be discovered
 *
 * An edge relation between two nodes (drivers or machines) X and Y can be:
 * - X CONSUMES Y: Y can be plugged into X
 * - X PRODUCES Y: X provides the interface Y
 * - X CONTAINS Y: Y is part of X component
 *
 * Basic framework steps are the following:
 * - All nodes and edges are created in their respective
 *   machine/driver/test files
 * - The framework starts QEMU and asks for a list of available devices
 *   and machines (note that only machines and "consumed" nodes are mapped
 *   1:1 with QEMU devices)
 * - The framework walks the graph starting from the available machines and
 *   performs a Depth First Search for tests
 * - Once a test is found, the path is walked again and all drivers are
 *   allocated accordingly and the final interface is passed to the test
 * - The test is executed
 * - Unused objects are cleaned and the path discovery is continued
 *
 * Depending on the QEMU binary used, only some drivers/machines will be
 * available and only test that are reached by them will be executed.
 *
 * <example>
 *   <title>Creating new driver an its interface</title>
 *   <programlisting>
 #include "libqos/qgraph.h"

 struct My_driver {
     QOSGraphObject obj;
     Node_produced prod;
     Node_contained cont;
 }

 static void my_destructor(QOSGraphObject *obj)
 {
    g_free(obj);
 }

 static void my_get_driver(void *object, const char *interface) {
    My_driver *dev = object;
    if (!g_strcmp0(interface, "my_interface")) {
        return &dev->prod;
    }
    abort();
 }

 static void my_get_device(void *object, const char *device) {
    My_driver *dev = object;
    if (!g_strcmp0(device, "my_driver_contained")) {
        return &dev->cont;
    }
    abort();
 }

 static void *my_driver_constructor(void *node_consumed,
                                    QOSGraphObject *alloc)
 {
    My_driver dev = g_new(My_driver, 1);
    // get the node pointed by the produce edge
    dev->obj.get_driver = my_get_driver;
    // get the node pointed by the contains
    dev->obj.get_device = my_get_device;
    // free the object
    dev->obj.destructor = my_destructor;
    do_something_with_node_consumed(node_consumed);
    // set all fields of contained device
    init_contained_device(&dev->cont);
    return &dev->obj;
 }

 static void register_my_driver(void)
 {
     qos_node_create_driver("my_driver", my_driver_constructor);
     // contained drivers don't need a constructor,
     // they will be init by the parent.
     qos_node_create_driver("my_driver_contained", NULL);

    // For the sake of this example, assume machine x86_64/pc contains
    // "other_node".
    // This relation, along with the machine and "other_node" creation,
    // should be defined in the x86_64_pc-machine.c file.
    // "my_driver" will then consume "other_node"
    qos_node_contains("my_driver", "my_driver_contained");
    qos_node_produces("my_driver", "my_interface");
    qos_node_consumes("my_driver", "other_node");
 }
 *   </programlisting>
 * </example>
 *
 * In the above example, all possible types of relations are created:
 * node "my_driver" consumes, contains and produces other nodes.
 * more specifically:
 * x86_64/pc -->contains--> other_node <--consumes-- my_driver
 *                                                       |
 *                      my_driver_contained <--contains--+
 *                                                       |
 *                             my_interface <--produces--+
 *
 * or inverting the consumes edge in consumed_by:
 *
 * x86_64/pc -->contains--> other_node --consumed_by--> my_driver
 *                                                           |
 *                          my_driver_contained <--contains--+
 *                                                           |
 *                                 my_interface <--produces--+
 *
 * <example>
 *   <title>Creating new test</title>
 *   <programlisting>
 * #include "libqos/qgraph.h"
 *
 * static void my_test_function(void *obj, void *data)
 * {
 *    Node_produced *interface_to_test = obj;
 *    // test interface_to_test
 * }
 *
 * static void register_my_test(void)
 * {
 *    qos_add_test("my_interface", "my_test", my_test_function);
 * }
 *
 * libqos_init(register_my_test);
 *
 *   </programlisting>
 * </example>
 *
 * Here a new test is created, consuming "my_interface" node
 * and creating a valid path from a machine to a test.
 * Final graph will be like this:
 * x86_64/pc -->contains--> other_node <--consumes-- my_driver
 *                                                        |
 *                       my_driver_contained <--contains--+
 *                                                        |
 *        my_test --consumes--> my_interface <--produces--+
 *
 * or inverting the consumes edge in consumed_by:
 *
 * x86_64/pc -->contains--> other_node --consumed_by--> my_driver
 *                                                           |
 *                          my_driver_contained <--contains--+
 *                                                           |
 *        my_test <--consumed_by-- my_interface <--produces--+
 *
 * Assuming there the binary is
 * QTEST_QEMU_BINARY=x86_64-softmmu/qemu-system-x86_64
 * a valid test path will be:
 * "/x86_64/pc/other_node/my_driver/my_interface/my_test".
 *
 * Additional examples are also in libqos/test-qgraph.c
 *
 * Command line:
 * Command line is built by using node names and optional arguments
 * passed by the user when building the edges.
 *
 * There are three types of command line arguments:
 * - in node      : created from the node name. For example, machines will
 *                  have "-M <machine>" to its command line, while devices
 *                  "-device <device>". It is automatically done by the
 *                   framework.
 * - after node   : added as additional argument to the node name.
 *                  This argument is added optionally when creating edges,
 *                  by setting the parameter @after_cmd_line and
 *                  @extra_edge_opts in #QOSGraphEdgeOptions.
 *                  The framework automatically adds
 *                  a comma before @extra_edge_opts,
 *                  because it is going to add attributes
 *                  after the destination node pointed by
 *                  the edge containing these options, and automatically
 *                  adds a space before @after_cmd_line, because it
 *                  adds an additional device, not an attribute.
 * - before node  : added as additional argument to the node name.
 *                  This argument is added optionally when creating edges,
 *                  by setting the parameter @before_cmd_line in
 *                  #QOSGraphEdgeOptions. This attribute
 *                  is going to add attributes before the destination node
 *                  pointed by the edge containing these options. It is
 *                  helpful to commands that are not node-representable,
 *                  such as "-fdsev" or "-netdev".
 *
 * While adding command line in edges is always used, not all nodes names are
 * used in every path walk: this is because the contained or produced ones
 * are already added by QEMU, so only nodes that "consumes" will be used to
 * build the command line. Also, nodes that will have { "abstract" : true }
 * as QMP attribute will loose their command line, since they are not proper
 * devices to be added in QEMU.
 *
 * Example:
 *
 QOSGraphEdgeOptions opts = {
     .arg = NULL,
     .size_arg = 0,
     .after_cmd_line = "-device other",
     .before_cmd_line = "-netdev something",
     .extra_edge_opts = "addr=04.0",
 };
 QOSGraphNode * node = qos_node_create_driver("my_node", constructor);
 qos_node_consumes_args("my_node", "interface", &opts);
 *
 * Will produce the following command line:
 * "-netdev something -device my_node,addr=04.0 -device other"
 */

/**
 * Edge options to be passed to the contains/consumes *_args function.
 */
struct QOSGraphEdgeOptions {
    void *arg;                    /*
                                   * optional arg that will be used by
                                   * dest edge
                                   */
    uint32_t size_arg;            /*
                                   * optional arg size that will be used by
                                   * dest edge
                                   */
    const char *extra_device_opts;/*
                                   *optional additional command line for dest
                                   * edge, used to add additional attributes
                                   * *after* the node command line, the
                                   * framework automatically prepends ","
                                   * to this argument.
                                   */
    const char *before_cmd_line;  /*
                                   * optional additional command line for dest
                                   * edge, used to add additional attributes
                                   * *before* the node command line, usually
                                   * other non-node represented commands,
                                   * like "-fdsev synt"
                                   */
    const char *after_cmd_line;   /*
                                   * optional extra command line to be added
                                   * after the device command. This option
                                   * is used to add other devices
                                   * command line that depend on current node.
                                   * Automatically prepends " " to this
                                   * argument
                                   */
    const char *edge_name;        /*
                                   * optional edge to differentiate multiple
                                   * devices with same node name
                                   */
};

/**
 * Test options to be passed to the test functions.
 */
struct QOSGraphTestOptions {
    QOSGraphEdgeOptions edge;   /* edge arguments that will be used by test.
                                 * Note that test *does not* use edge_name,
                                 * and uses instead arg and size_arg as
                                 * data arg for its test function.
                                 */
    void *arg;                  /* passed to the .before function, or to the
                                 * test function if there is no .before
                                 * function
                                 */
    QOSBeforeTest before;       /* executed before the test. Can add
                                 * additional parameters to the command line
                                 * and modify the argument to the test function.
                                 */
    bool subprocess;            /* run the test in a subprocess */
};

/**
 * Each driver, test or machine of this framework will have a
 * QOSGraphObject as first field.
 *
 * This set of functions offered by QOSGraphObject are executed
 * in different stages of the framework:
 * - get_driver / get_device : Once a machine-to-test path has been
 * found, the framework traverses it again and allocates all the
 * nodes, using the provided constructor. To satisfy their relations,
 * i.e. for produces or contains, where a struct constructor needs
 * an external parameter represented by the previous node,
 * the framework will call get_device (for contains) or
 * get_driver (for produces), depending on the edge type, passing
 * them the name of the next node to be taken and getting from them
 * the corresponding pointer to the actual structure of the next node to
 * be used in the path.
 *
 * - start_hw: This function is executed after all the path objects
 * have been allocated, but before the test is run. It starts the hw, setting
 * the initial configurations (*_device_enable) and making it ready for the
 * test.
 *
 * - destructor: Opposite to the node constructor, destroys the object.
 * This function is called after the test has been executed, and performs
 * a complete cleanup of each node allocated field. In case no constructor
 * is provided, no destructor will be called.
 *
 */
struct QOSGraphObject {
    /* for produces edges, returns void * */
    QOSGetDriver get_driver;
    /* for contains edges, returns a QOSGraphObject * */
    QOSGetDevice get_device;
    /* start the hw, get ready for the test */
    QOSStartFunct start_hw;
    /* destroy this QOSGraphObject */
    QOSDestructorFunc destructor;
    /* free the memory associated to the QOSGraphObject and its contained
     * children */
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
 * qos_node_destroy(): removes and frees a node from the,
 * nodes hash table.
 */
void qos_node_destroy(void *key);

/**
 * qos_edge_destroy(): removes and frees an edge from the,
 * edges hash table.
 */
void qos_edge_destroy(void *key);

/**
 * qos_add_test(): adds a test node @name to the nodes hash table.
 *
 * The test will consume a @interface node, and once the
 * graph walking algorithm has found it, the @test_func will be
 * executed. It also has the possibility to
 * add an optional @opts (see %QOSGraphNodeOptions).
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
 *
 * This node will be of type QNODE_MACHINE and have @function
 * as constructor
 */
void qos_node_create_machine(const char *name, QOSCreateMachineFunc function);

/**
 * qos_node_create_machine_args(): same as qos_node_create_machine,
 * but with the possibility to add an optional ", @opts" after -M machine
 * command line.
 */
void qos_node_create_machine_args(const char *name,
                                  QOSCreateMachineFunc function,
                                  const char *opts);

/**
 * qos_node_create_driver(): creates the driver @name and
 * adds it to the node hash table.
 *
 * This node will be of type QNODE_DRIVER and have @function
 * as constructor
 */
void qos_node_create_driver(const char *name, QOSCreateDriverFunc function);

/**
 * qos_node_contains(): creates one or more edges of type QEDGE_CONTAINS
 * and adds them to the edge list mapped to @container in the
 * edge hash table.
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
 * For example, if "arm/raspi2" contains 2 "generic-sdhci"
 * devices, the right commands will be:
 * qos_node_create_machine("arm/raspi2");
 * qos_node_create_driver("generic-sdhci", constructor);
 * //assume rest of the fields are set NULL
 * QOSGraphEdgeOptions op1 = { .edge_name = "emmc" };
 * QOSGraphEdgeOptions op2 = { .edge_name = "sdcard" };
 * qos_node_contains("arm/raspi2", "generic-sdhci", &op1, &op2, NULL);
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
 *
 * This edge will have @producer as source and @interface as destination.
 */
void qos_node_produces(const char *producer, const char *interface);

/**
 * qos_node_consumes():  creates an edge of type QEDGE_CONSUMED_BY and
 * adds it to the edge list mapped to @interface in the
 * edge hash table.
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
 * @alloc: Where to store the allocator for the machine object, or %NULL.
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
 */
void qos_object_destroy(QOSGraphObject *obj);

/**
 * qos_object_queue_destroy(): queue the destructor for @obj so that it is
 * called at the end of the test
 */
void qos_object_queue_destroy(QOSGraphObject *obj);

/**
 * qos_object_start_hw(): calls the start_hw function for @obj
 */
void qos_object_start_hw(QOSGraphObject *obj);

/**
 * qos_machine_new(): instantiate a new machine node
 * @node: A machine node to be instantiated
 * @qts: The #QTestState that will be referred to by the machine object.
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


#endif

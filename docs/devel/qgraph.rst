.. _qgraph:

========================================
Qtest Driver Framework
========================================

This Qgraph API provides all basic functions to create a graph
and instantiate nodes representing machines, drivers and tests
representing their relations with ``CONSUMES``, ``PRODUCES``, and
``CONTAINS`` edges.

The idea is to have a framework where each test asks for a specific
driver, and the framework takes care of allocating the proper devices
required and passing the correct command line arguments to QEMU.

Nodes
^^^^^^

A node can be of four types:

- **QNODE_MACHINE**:   for example ``arm/raspi2``
- **QNODE_DRIVER**:    for example ``generic-sdhci``
- **QNODE_INTERFACE**: for example ``sdhci`` (interface for all ``-sdhci``
  drivers).
  An interface is not explicitly created, it will be automatically
  instantiated when a node consumes or produces it.
- **QNODE_TEST**:      for example ``sdhci-test``, consumes an interface and
  tests the functions provided.

Notes for the nodes:

- QNODE_MACHINE: each machine struct must have a ``QGuestAllocator`` and
  implement ``get_driver()`` to return the allocator mapped to the interface
  "memory". The function can also return ``NULL`` if the allocator
  is not set.
- QNODE_DRIVER:  driver names must be unique, and machines and nodes
  planned to be "consumed" by other nodes must match QEMU
  drivers name, otherwise they won't be discovered

Edges
^^^^^^

An edge relation between two nodes (drivers or machines) `X` and `Y` can be:

- ``X CONSUMES Y``: `Y` can be plugged into `X`
- ``X PRODUCES Y``: `X` provides the interface `Y`
- ``X CONTAINS Y``: `Y` is part of `X` component

Execution steps
^^^^^^^^^^^^^^^

The basic framework steps are the following:

- All nodes and edges are created in their respective
  machine/driver/test files
- The framework starts QEMU and asks for a list of available devices
  and machines (note that only machines and "consumed" nodes are mapped
  1:1 with QEMU devices)
- The framework walks the graph starting from the available machines and
  performs a Depth First Search for tests
- Once a test is found, the path is walked again and all drivers are
  allocated accordingly and the final interface is passed to the test
- The test is executed
- Unused objects are cleaned and the path discovery is continued

Depending on the QEMU binary used, only some drivers/machines will be
available and only test that are reached by them will be executed.

Creating a new driver and its interface
"""""""""""""""""""""""""""""""""""""""""

.. code::

    #include "qgraph.h"

    struct My_driver {
        QOSGraphObject obj;
        Node_produced prod;
        Node_contained cont;
    }

    static void my_destructor(QOSGraphObject *obj)
    {
        g_free(obj);
    }

    static void *my_get_driver(void *object, const char *interface) {
        My_driver *dev = object;
        if (!g_strcmp0(interface, "my_interface")) {
            return &dev->prod;
        }
        abort();
    }

    static void *my_get_device(void *object, const char *device) {
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

        // For the sake of this example, assume machine x86_64/pc
        // contains "other_node".
        // This relation, along with the machine and "other_node"
        // creation, should be defined in the x86_64_pc-machine.c file.
        // "my_driver" will then consume "other_node"
        qos_node_contains("my_driver", "my_driver_contained");
        qos_node_produces("my_driver", "my_interface");
        qos_node_consumes("my_driver", "other_node");
    }

In the above example, all possible types of relations are created:
node "my_driver" consumes, contains and produces other nodes.
More specifically::

  x86_64/pc -->contains--> other_node <--consumes-- my_driver
                                                        |
                       my_driver_contained <--contains--+
                                                        |
                              my_interface <--produces--+

or inverting the consumes edge in consumed_by::

  x86_64/pc -->contains--> other_node --consumed_by--> my_driver
                                                            |
                           my_driver_contained <--contains--+
                                                            |
                                  my_interface <--produces--+

Creating new test
"""""""""""""""""

.. code::

    #include "qgraph.h"

    static void my_test_function(void *obj, void *data)
    {
        Node_produced *interface_to_test = obj;
        // test interface_to_test
    }

    static void register_my_test(void)
    {
        qos_add_test("my_interface", "my_test", my_test_function);
    }

    libqos_init(register_my_test);

Here a new test is created, consuming "my_interface" node
and creating a valid path from a machine to a test.
Final graph will be like this::

  x86_64/pc --contains--> other_node <--consumes-- my_driver
                                                         |
                        my_driver_contained <--contains--+
                                                         |
         my_test --consumes--> my_interface <--produces--+

or inverting the consumes edge in consumed_by::

  x86_64/pc --contains--> other_node --consumed_by--> my_driver
                                                            |
                           my_driver_contained <--contains--+
                                                            |
         my_test <--consumed_by-- my_interface <--produces--+

Assuming there the binary is
``QTEST_QEMU_BINARY=./qemu-system-x86_64``
a valid test path will be:
``/x86_64/pc/other_node/my_driver/my_interface/my_test``.

Additional examples are also in ``test-qgraph.c``

Command line:
""""""""""""""

Command line is built by using node names and optional arguments
passed by the user when building the edges.

There are three types of command line arguments:

- ``in node``      : created from the node name. For example, machines will
  have ``-M <machine>`` to its command line, while devices
  ``-device <device>``. It is automatically done by the framework.
- ``after node``   : added as additional argument to the node name.
  This argument is added optionally when creating edges,
  by setting the parameter @after_cmd_line and
  @extra_edge_opts in #QOSGraphEdgeOptions.
  The framework automatically adds
  a comma before @extra_edge_opts,
  because it is going to add attributes
  after the destination node pointed by
  the edge containing these options, and automatically
  adds a space before @after_cmd_line, because it
  adds an additional device, not an attribute.
- ``before node``  : added as additional argument to the node name.
  This argument is added optionally when creating edges,
  by setting the parameter @before_cmd_line in
  #QOSGraphEdgeOptions. This attribute
  is going to add attributes before the destination node
  pointed by the edge containing these options. It is
  helpful to commands that are not node-representable,
  such as ``-fdsev`` or ``-netdev``.

While adding command line in edges is always used, not all nodes names are
used in every path walk: this is because the contained or produced ones
are already added by QEMU, so only nodes that "consumes" will be used to
build the command line. Also, nodes that will have ``{ "abstract" : true }``
as QMP attribute will loose their command line, since they are not proper
devices to be added in QEMU.

Example::

    QOSGraphEdgeOptions opts = {
        .arg = NULL,
        .size_arg = 0,
        .after_cmd_line = "-device other",
        .before_cmd_line = "-netdev something",
        .extra_edge_opts = "addr=04.0",
    };
    QOSGraphNodeS *node = qos_node_create_driver("my_node", constructor);
    qos_node_consumes_args("my_node", "interface", &opts);

Will produce the following command line:
``-netdev something -device my_node,addr=04.0 -device other``

Qgraph API reference
^^^^^^^^^^^^^^^^^^^^

.. kernel-doc:: tests/qtest/libqos/qgraph.h

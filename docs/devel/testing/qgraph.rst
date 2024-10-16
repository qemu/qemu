.. _qgraph:

Qtest Driver Framework
======================

In order to test a specific driver, plain libqos tests need to
take care of booting QEMU with the right machine and devices.
This makes each test "hardcoded" for a specific configuration, reducing
the possible coverage that it can reach.

For example, the sdhci device is supported on both x86_64 and ARM boards,
therefore a generic sdhci test should test all machines and drivers that
support that device.
Using only libqos APIs, the test has to manually take care of
covering all the setups, and build the correct command line.

This also introduces backward compatibility issues: if a device/driver command
line name is changed, all tests that use that will not work
properly anymore and need to be adjusted.

The aim of qgraph is to create a graph of drivers, machines and tests such that
a test aimed to a certain driver does not have to care of
booting the right QEMU machine, pick the right device, build the command line
and so on. Instead, it only defines what type of device it is testing
(interface in qgraph terms) and the framework takes care of
covering all supported types of devices and machine architectures.

Following the above example, an interface would be ``sdhci``,
so the sdhci-test should only care of linking its qgraph node with
that interface. In this way, if the command line of a sdhci driver
is changed, only the respective qgraph driver node has to be adjusted.

QGraph concepts
---------------

The graph is composed by nodes that represent machines, drivers, tests
and edges that define the relationships between them (``CONSUMES``, ``PRODUCES``, and
``CONTAINS``).

Nodes
~~~~~

A node can be of four types:

- **QNODE_MACHINE**:   for example ``arm/raspi2b``
- **QNODE_DRIVER**:    for example ``generic-sdhci``
- **QNODE_INTERFACE**: for example ``sdhci`` (interface for all ``-sdhci``
  drivers).
  An interface is not explicitly created, it will be automatically
  instantiated when a node consumes or produces it.
  An interface is simply a struct that abstracts the various drivers
  for the same type of device, and offers an API to the nodes that
  use it ("consume" relation in qgraph terms) that is implemented/backed up by the drivers that implement it ("produce" relation in qgraph terms).
- **QNODE_TEST**:      for example ``sdhci-test``. A test consumes an interface
  and tests the functions provided by it.

Notes for the nodes:

- QNODE_MACHINE: each machine struct must have a ``QGuestAllocator`` and
  implement ``get_driver()`` to return the allocator mapped to the interface
  "memory". The function can also return ``NULL`` if the allocator
  is not set.
- QNODE_DRIVER:  driver names must be unique, and machines and nodes
  planned to be "consumed" by other nodes must match QEMU
  drivers name, otherwise they won't be discovered

Edges
~~~~~

An edge relation between two nodes (drivers or machines) ``X`` and ``Y`` can be:

- ``X CONSUMES Y``: ``Y`` can be plugged into ``X``
- ``X PRODUCES Y``: ``X`` provides the interface ``Y``
- ``X CONTAINS Y``: ``Y`` is part of ``X`` component

Execution steps
~~~~~~~~~~~~~~~

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

Command line
~~~~~~~~~~~~

Command line is built by using node names and optional arguments
passed by the user when building the edges.

There are three types of command line arguments:

- ``in node``      : created from the node name. For example, machines will
  have ``-M <machine>`` to its command line, while devices
  ``-device <device>``. It is automatically done by the framework.
- ``after node``   : added as additional argument to the node name.
  This argument is added optionally when creating edges,
  by setting the parameter ``after_cmd_line`` and
  ``extra_edge_opts`` in ``QOSGraphEdgeOptions``.
  The framework automatically adds
  a comma before ``extra_edge_opts``,
  because it is going to add attributes
  after the destination node pointed by
  the edge containing these options, and automatically
  adds a space before ``after_cmd_line``, because it
  adds an additional device, not an attribute.
- ``before node``  : added as additional argument to the node name.
  This argument is added optionally when creating edges,
  by setting the parameter ``before_cmd_line`` in
  ``QOSGraphEdgeOptions``. This attribute
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
        .before_cmd_line = "-drive id=drv0,if=none,file=null-co://,"
                           "file.read-zeroes=on,format=raw",
        .after_cmd_line = "-device scsi-hd,bus=vs0.0,drive=drv0",

        opts.extra_device_opts = "id=vs0";
    };

    qos_node_create_driver("virtio-scsi-device",
                            virtio_scsi_device_create);
    qos_node_consumes("virtio-scsi-device", "virtio-bus", &opts);

Will produce the following command line:
``-drive id=drv0,if=none,file=null-co://, -device virtio-scsi-device,id=vs0 -device scsi-hd,bus=vs0.0,drive=drv0``

Troubleshooting unavailable tests
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If there is no path from an available machine to a test then that test will be
unavailable and won't execute. This can happen if a test or driver did not set
up its qgraph node correctly. It can also happen if the necessary machine type
or device is missing from the QEMU binary because it was compiled out or
otherwise.

It is possible to troubleshoot unavailable tests by running::

  $ QTEST_QEMU_BINARY=build/qemu-system-x86_64 build/tests/qtest/qos-test --verbose
  # ALL QGRAPH EDGES: {
  #   src='virtio-net'
  #      |-> dest='virtio-net-tests/vhost-user/multiqueue' type=2 (node=0x559142109e30)
  #      |-> dest='virtio-net-tests/vhost-user/migrate' type=2 (node=0x559142109d00)
  #   src='virtio-net-pci'
  #      |-> dest='virtio-net' type=1 (node=0x55914210d740)
  #   src='pci-bus'
  #      |-> dest='virtio-net-pci' type=2 (node=0x55914210d880)
  #   src='pci-bus-pc'
  #      |-> dest='pci-bus' type=1 (node=0x559142103f40)
  #   src='i440FX-pcihost'
  #      |-> dest='pci-bus-pc' type=0 (node=0x55914210ac70)
  #   src='x86_64/pc'
  #      |-> dest='i440FX-pcihost' type=0 (node=0x5591421117f0)
  #   src=''
  #      |-> dest='x86_64/pc' type=0 (node=0x559142111600)
  #      |-> dest='arm/raspi2b' type=0 (node=0x559142110740)
  ...
  # }
  # ALL QGRAPH NODES: {
  #   name='virtio-net-tests/announce-self' type=3 cmd_line='(null)' [available]
  #   name='arm/raspi2b' type=0 cmd_line='-M raspi2b ' [UNAVAILABLE]
  ...
  # }

The ``virtio-net-tests/announce-self`` test is listed as "available" in the
"ALL QGRAPH NODES" output. This means the test will execute. We can follow the
qgraph path in the "ALL QGRAPH EDGES" output as follows: '' -> 'x86_64/pc' ->
'i440FX-pcihost' -> 'pci-bus-pc' -> 'pci-bus' -> 'virtio-net-pci' ->
'virtio-net'. The root of the qgraph is '' and the depth first search begins
there.

The ``arm/raspi2b`` machine node is listed as "UNAVAILABLE". Although it is
reachable from the root via '' -> 'arm/raspi2b' the node is unavailable because
the QEMU binary did not list it when queried by the framework. This is expected
because we used the ``qemu-system-x86_64`` binary which does not support ARM
machine types.

If a test is unexpectedly listed as "UNAVAILABLE", first check that the "ALL
QGRAPH EDGES" output reports edge connectivity from the root ('') to the test.
If there is no connectivity then the qgraph nodes were not set up correctly and
the driver or test code is incorrect. If there is connectivity, check the
availability of each node in the path in the "ALL QGRAPH NODES" output. The
first unavailable node in the path is the reason why the test is unavailable.
Typically this is because the QEMU binary lacks support for the necessary
machine type or device.

Creating a new driver and its interface
---------------------------------------

Here we continue the ``sdhci`` use case, with the following scenario:

- ``sdhci-test`` aims to test the ``read[q,w], writeq`` functions
  offered by the ``sdhci`` drivers.
- The current ``sdhci`` device is supported by both ``x86_64/pc`` and ``ARM``
  (in this example we focus on the ``arm-raspi2b``) machines.
- QEMU offers 2 types of drivers: ``QSDHCI_MemoryMapped`` for ``ARM`` and
  ``QSDHCI_PCI`` for ``x86_64/pc``. Both implement the
  ``read[q,w], writeq`` functions.

In order to implement such scenario in qgraph, the test developer needs to:

- Create the ``x86_64/pc`` machine node. This machine uses the
  ``pci-bus`` architecture so it ``contains`` a PCI driver,
  ``pci-bus-pc``. The actual path is

  ``x86_64/pc --contains--> 1440FX-pcihost --contains-->
  pci-bus-pc --produces--> pci-bus``.

  For the sake of this example,
  we do not focus on the PCI interface implementation.
- Create the ``sdhci-pci`` driver node, representing ``QSDHCI_PCI``.
  The driver uses the PCI bus (and its API),
  so it must ``consume`` the ``pci-bus`` generic interface (which abstracts
  all the pci drivers available)

  ``sdhci-pci --consumes--> pci-bus``
- Create an ``arm/raspi2b`` machine node. This machine ``contains``
  a ``generic-sdhci`` memory mapped ``sdhci`` driver node, representing
  ``QSDHCI_MemoryMapped``.

  ``arm/raspi2b --contains--> generic-sdhci``
- Create the ``sdhci`` interface node. This interface offers the
  functions that are shared by all ``sdhci`` devices.
  The interface is produced by ``sdhci-pci`` and ``generic-sdhci``,
  the available architecture-specific drivers.

  ``sdhci-pci --produces--> sdhci``

  ``generic-sdhci --produces--> sdhci``
- Create the ``sdhci-test`` test node. The test ``consumes`` the
  ``sdhci`` interface, using its API. It doesn't need to look at
  the supported machines or drivers.

  ``sdhci-test --consumes--> sdhci``

``arm-raspi2b`` machine, simplified from
``tests/qtest/libqos/arm-raspi2-machine.c``::

    #include "qgraph.h"

    struct QRaspi2Machine {
        QOSGraphObject obj;
        QGuestAllocator alloc;
        QSDHCI_MemoryMapped sdhci;
    };

    static void *raspi2_get_driver(void *object, const char *interface)
    {
        QRaspi2Machine *machine = object;
        if (!g_strcmp0(interface, "memory")) {
            return &machine->alloc;
        }

        fprintf(stderr, "%s not present in arm/raspi2b\n", interface);
        g_assert_not_reached();
    }

    static QOSGraphObject *raspi2_get_device(void *obj,
                                                const char *device)
    {
        QRaspi2Machine *machine = obj;
        if (!g_strcmp0(device, "generic-sdhci")) {
            return &machine->sdhci.obj;
        }

        fprintf(stderr, "%s not present in arm/raspi2b\n", device);
        g_assert_not_reached();
    }

    static void *qos_create_machine_arm_raspi2(QTestState *qts)
    {
        QRaspi2Machine *machine = g_new0(QRaspi2Machine, 1);

        alloc_init(&machine->alloc, ...);

        /* Get node(s) contained inside (CONTAINS) */
        machine->obj.get_device = raspi2_get_device;

        /* Get node(s) produced (PRODUCES) */
        machine->obj.get_driver = raspi2_get_driver;

        /* free the object */
        machine->obj.destructor = raspi2_destructor;
        qos_init_sdhci_mm(&machine->sdhci, ...);
        return &machine->obj;
    }

    static void raspi2_register_nodes(void)
    {
        /* arm/raspi2b --contains--> generic-sdhci */
        qos_node_create_machine("arm/raspi2b",
                                 qos_create_machine_arm_raspi2);
        qos_node_contains("arm/raspi2b", "generic-sdhci", NULL);
    }

    libqos_init(raspi2_register_nodes);

``x86_64/pc`` machine, simplified from
``tests/qtest/libqos/x86_64_pc-machine.c``::

    #include "qgraph.h"

    struct i440FX_pcihost {
        QOSGraphObject obj;
        QPCIBusPC pci;
    };

    struct QX86PCMachine {
        QOSGraphObject obj;
        QGuestAllocator alloc;
        i440FX_pcihost bridge;
    };

    /* i440FX_pcihost */

    static QOSGraphObject *i440FX_host_get_device(void *obj,
                                                const char *device)
    {
        i440FX_pcihost *host = obj;
        if (!g_strcmp0(device, "pci-bus-pc")) {
            return &host->pci.obj;
        }
        fprintf(stderr, "%s not present in i440FX-pcihost\n", device);
        g_assert_not_reached();
    }

    /* x86_64/pc machine */

    static void *pc_get_driver(void *object, const char *interface)
    {
        QX86PCMachine *machine = object;
        if (!g_strcmp0(interface, "memory")) {
            return &machine->alloc;
        }

        fprintf(stderr, "%s not present in x86_64/pc\n", interface);
        g_assert_not_reached();
    }

    static QOSGraphObject *pc_get_device(void *obj, const char *device)
    {
        QX86PCMachine *machine = obj;
        if (!g_strcmp0(device, "i440FX-pcihost")) {
            return &machine->bridge.obj;
        }

        fprintf(stderr, "%s not present in x86_64/pc\n", device);
        g_assert_not_reached();
    }

    static void *qos_create_machine_pc(QTestState *qts)
    {
        QX86PCMachine *machine = g_new0(QX86PCMachine, 1);

        /* Get node(s) contained inside (CONTAINS) */
        machine->obj.get_device = pc_get_device;

        /* Get node(s) produced (PRODUCES) */
        machine->obj.get_driver = pc_get_driver;

        /* free the object */
        machine->obj.destructor = pc_destructor;
        pc_alloc_init(&machine->alloc, qts, ALLOC_NO_FLAGS);

        /* Get node(s) contained inside (CONTAINS) */
        machine->bridge.obj.get_device = i440FX_host_get_device;

        return &machine->obj;
    }

    static void pc_machine_register_nodes(void)
    {
        /* x86_64/pc --contains--> 1440FX-pcihost --contains-->
         * pci-bus-pc [--produces--> pci-bus (in pci.h)] */
        qos_node_create_machine("x86_64/pc", qos_create_machine_pc);
        qos_node_contains("x86_64/pc", "i440FX-pcihost", NULL);

        /* contained drivers don't need a constructor,
         * they will be init by the parent */
        qos_node_create_driver("i440FX-pcihost", NULL);
        qos_node_contains("i440FX-pcihost", "pci-bus-pc", NULL);
    }

    libqos_init(pc_machine_register_nodes);

``sdhci`` taken from ``tests/qtest/libqos/sdhci.c``::

    /* Interface node, offers the sdhci API */
    struct QSDHCI {
        uint16_t (*readw)(QSDHCI *s, uint32_t reg);
        uint64_t (*readq)(QSDHCI *s, uint32_t reg);
        void (*writeq)(QSDHCI *s, uint32_t reg, uint64_t val);
        /* other fields */
    };

    /* Memory Mapped implementation of QSDHCI */
    struct QSDHCI_MemoryMapped {
        QOSGraphObject obj;
        QSDHCI sdhci;
        /* other driver-specific fields */
    };

    /* PCI implementation of QSDHCI */
    struct QSDHCI_PCI {
        QOSGraphObject obj;
        QSDHCI sdhci;
        /* other driver-specific fields */
    };

    /* Memory mapped implementation of QSDHCI */

    static void *sdhci_mm_get_driver(void *obj, const char *interface)
    {
        QSDHCI_MemoryMapped *smm = obj;
        if (!g_strcmp0(interface, "sdhci")) {
            return &smm->sdhci;
        }
        fprintf(stderr, "%s not present in generic-sdhci\n", interface);
        g_assert_not_reached();
    }

    void qos_init_sdhci_mm(QSDHCI_MemoryMapped *sdhci, QTestState *qts,
                        uint32_t addr, QSDHCIProperties *common)
    {
        /* Get node contained inside (CONTAINS) */
        sdhci->obj.get_driver = sdhci_mm_get_driver;

        /* SDHCI interface API */
        sdhci->sdhci.readw = sdhci_mm_readw;
        sdhci->sdhci.readq = sdhci_mm_readq;
        sdhci->sdhci.writeq = sdhci_mm_writeq;
        sdhci->qts = qts;
    }

    /* PCI implementation of QSDHCI */

    static void *sdhci_pci_get_driver(void *object,
                                      const char *interface)
    {
        QSDHCI_PCI *spci = object;
        if (!g_strcmp0(interface, "sdhci")) {
            return &spci->sdhci;
        }

        fprintf(stderr, "%s not present in sdhci-pci\n", interface);
        g_assert_not_reached();
    }

    static void *sdhci_pci_create(void *pci_bus,
                                  QGuestAllocator *alloc,
                                  void *addr)
    {
        QSDHCI_PCI *spci = g_new0(QSDHCI_PCI, 1);
        QPCIBus *bus = pci_bus;
        uint64_t barsize;

        qpci_device_init(&spci->dev, bus, addr);

        /* SDHCI interface API */
        spci->sdhci.readw = sdhci_pci_readw;
        spci->sdhci.readq = sdhci_pci_readq;
        spci->sdhci.writeq = sdhci_pci_writeq;

        /* Get node(s) produced (PRODUCES) */
        spci->obj.get_driver = sdhci_pci_get_driver;

        spci->obj.start_hw = sdhci_pci_start_hw;
        spci->obj.destructor = sdhci_destructor;
        return &spci->obj;
    }

    static void qsdhci_register_nodes(void)
    {
        QOSGraphEdgeOptions opts = {
            .extra_device_opts = "addr=04.0",
        };

        /* generic-sdhci */
        /* generic-sdhci --produces--> sdhci */
        qos_node_create_driver("generic-sdhci", NULL);
        qos_node_produces("generic-sdhci", "sdhci");

        /* sdhci-pci */
        /* sdhci-pci --produces--> sdhci
         * sdhci-pci --consumes--> pci-bus */
        qos_node_create_driver("sdhci-pci", sdhci_pci_create);
        qos_node_produces("sdhci-pci", "sdhci");
        qos_node_consumes("sdhci-pci", "pci-bus", &opts);
    }

    libqos_init(qsdhci_register_nodes);

In the above example, all possible types of relations are created::

  x86_64/pc --contains--> 1440FX-pcihost --contains--> pci-bus-pc
                                                            |
               sdhci-pci --consumes--> pci-bus <--produces--+
                  |
                  +--produces--+
                               |
                               v
                             sdhci
                               ^
                               |
                               +--produces-- +
                                             |
               arm/raspi2b --contains--> generic-sdhci

or inverting the consumes edge in consumed_by::

  x86_64/pc --contains--> 1440FX-pcihost --contains--> pci-bus-pc
                                                            |
            sdhci-pci <--consumed by-- pci-bus <--produces--+
                |
                +--produces--+
                             |
                             v
                            sdhci
                             ^
                             |
                             +--produces-- +
                                           |
            arm/raspi2b --contains--> generic-sdhci

Adding a new test
-----------------

Given the above setup, adding a new test is very simple.
``sdhci-test``, taken from ``tests/qtest/sdhci-test.c``::

    static void check_capab_sdma(QSDHCI *s, bool supported)
    {
        uint64_t capab, capab_sdma;

        capab = s->readq(s, SDHC_CAPAB);
        capab_sdma = FIELD_EX64(capab, SDHC_CAPAB, SDMA);
        g_assert_cmpuint(capab_sdma, ==, supported);
    }

    static void test_registers(void *obj, void *data,
                                QGuestAllocator *alloc)
    {
        QSDHCI *s = obj;

        /* example test */
        check_capab_sdma(s, s->props.capab.sdma);
    }

    static void register_sdhci_test(void)
    {
        /* sdhci-test --consumes--> sdhci */
        qos_add_test("registers", "sdhci", test_registers, NULL);
    }

    libqos_init(register_sdhci_test);

Here a new test is created, consuming ``sdhci`` interface node
and creating a valid path from both machines to a test.
Final graph will be like this::

  x86_64/pc --contains--> 1440FX-pcihost --contains--> pci-bus-pc
                                                            |
               sdhci-pci --consumes--> pci-bus <--produces--+
                  |
                  +--produces--+
                               |
                               v
                             sdhci <--consumes-- sdhci-test
                               ^
                               |
                               +--produces-- +
                                             |
               arm/raspi2b --contains--> generic-sdhci

or inverting the consumes edge in consumed_by::

  x86_64/pc --contains--> 1440FX-pcihost --contains--> pci-bus-pc
                                                            |
            sdhci-pci <--consumed by-- pci-bus <--produces--+
                |
                +--produces--+
                             |
                             v
                            sdhci --consumed by--> sdhci-test
                             ^
                             |
                             +--produces-- +
                                           |
            arm/raspi2b --contains--> generic-sdhci

Assuming there the binary is
``QTEST_QEMU_BINARY=./qemu-system-x86_64``
a valid test path will be:
``/x86_64/pc/1440FX-pcihost/pci-bus-pc/pci-bus/sdhci-pc/sdhci/sdhci-test``

and for the binary ``QTEST_QEMU_BINARY=./qemu-system-arm``:

``/arm/raspi2b/generic-sdhci/sdhci/sdhci-test``

Additional examples are also in ``test-qgraph.c``

Qgraph API reference
--------------------

.. kernel-doc:: tests/qtest/libqos/qgraph.h

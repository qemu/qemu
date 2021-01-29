This is the design document for multi-process QEMU. It does not
necessarily reflect the status of the current implementation, which
may lack features or be considerably different from what is described
in this document. This document is still useful as a description of
the goals and general direction of this feature.

Please refer to the following wiki for latest details:
https://wiki.qemu.org/Features/MultiProcessQEMU

Multi-process QEMU
===================

QEMU is often used as the hypervisor for virtual machines running in the
Oracle cloud. Since one of the advantages of cloud computing is the
ability to run many VMs from different tenants in the same cloud
infrastructure, a guest that compromised its hypervisor could
potentially use the hypervisor's access privileges to access data it is
not authorized for.

QEMU can be susceptible to security attacks because it is a large,
monolithic program that provides many features to the VMs it services.
Many of these features can be configured out of QEMU, but even a reduced
configuration QEMU has a large amount of code a guest can potentially
attack. Separating QEMU reduces the attack surface by aiding to
limit each component in the system to only access the resources that
it needs to perform its job.

QEMU services
-------------

QEMU can be broadly described as providing three main services. One is a
VM control point, where VMs can be created, migrated, re-configured, and
destroyed. A second is to emulate the CPU instructions within the VM,
often accelerated by HW virtualization features such as Intel's VT
extensions. Finally, it provides IO services to the VM by emulating HW
IO devices, such as disk and network devices.

A multi-process QEMU
~~~~~~~~~~~~~~~~~~~~

A multi-process QEMU involves separating QEMU services into separate
host processes. Each of these processes can be given only the privileges
it needs to provide its service, e.g., a disk service could be given
access only to the disk images it provides, and not be allowed to
access other files, or any network devices. An attacker who compromised
this service would not be able to use this exploit to access files or
devices beyond what the disk service was given access to.

A QEMU control process would remain, but in multi-process mode, will
have no direct interfaces to the VM. During VM execution, it would still
provide the user interface to hot-plug devices or live migrate the VM.

A first step in creating a multi-process QEMU is to separate IO services
from the main QEMU program, which would continue to provide CPU
emulation. i.e., the control process would also be the CPU emulation
process. In a later phase, CPU emulation could be separated from the
control process.

Separating IO services
----------------------

Separating IO services into individual host processes is a good place to
begin for a couple of reasons. One is the sheer number of IO devices QEMU
can emulate provides a large surface of interfaces which could potentially
be exploited, and, indeed, have been a source of exploits in the past.
Another is the modular nature of QEMU device emulation code provides
interface points where the QEMU functions that perform device emulation
can be separated from the QEMU functions that manage the emulation of
guest CPU instructions. The devices emulated in the separate process are
referred to as remote devices.

QEMU device emulation
~~~~~~~~~~~~~~~~~~~~~

QEMU uses an object oriented SW architecture for device emulation code.
Configured objects are all compiled into the QEMU binary, then objects
are instantiated by name when used by the guest VM. For example, the
code to emulate a device named "foo" is always present in QEMU, but its
instantiation code is only run when the device is included in the target
VM. (e.g., via the QEMU command line as *-device foo*)

The object model is hierarchical, so device emulation code names its
parent object (such as "pci-device" for a PCI device) and QEMU will
instantiate a parent object before calling the device's instantiation
code.

Current separation models
~~~~~~~~~~~~~~~~~~~~~~~~~

In order to separate the device emulation code from the CPU emulation
code, the device object code must run in a different process. There are
a couple of existing QEMU features that can run emulation code
separately from the main QEMU process. These are examined below.

vhost user model
^^^^^^^^^^^^^^^^

Virtio guest device drivers can be connected to vhost user applications
in order to perform their IO operations. This model uses special virtio
device drivers in the guest and vhost user device objects in QEMU, but
once the QEMU vhost user code has configured the vhost user application,
mission-mode IO is performed by the application. The vhost user
application is a daemon process that can be contacted via a known UNIX
domain socket.

vhost socket
''''''''''''

As mentioned above, one of the tasks of the vhost device object within
QEMU is to contact the vhost application and send it configuration
information about this device instance. As part of the configuration
process, the application can also be sent other file descriptors over
the socket, which then can be used by the vhost user application in
various ways, some of which are described below.

vhost MMIO store acceleration
'''''''''''''''''''''''''''''

VMs are often run using HW virtualization features via the KVM kernel
driver. This driver allows QEMU to accelerate the emulation of guest CPU
instructions by running the guest in a virtual HW mode. When the guest
executes instructions that cannot be executed by virtual HW mode,
execution returns to the KVM driver so it can inform QEMU to emulate the
instructions in SW.

One of the events that can cause a return to QEMU is when a guest device
driver accesses an IO location. QEMU then dispatches the memory
operation to the corresponding QEMU device object. In the case of a
vhost user device, the memory operation would need to be sent over a
socket to the vhost application. This path is accelerated by the QEMU
virtio code by setting up an eventfd file descriptor that the vhost
application can directly receive MMIO store notifications from the KVM
driver, instead of needing them to be sent to the QEMU process first.

vhost interrupt acceleration
''''''''''''''''''''''''''''

Another optimization used by the vhost application is the ability to
directly inject interrupts into the VM via the KVM driver, again,
bypassing the need to send the interrupt back to the QEMU process first.
The QEMU virtio setup code configures the KVM driver with an eventfd
that triggers the device interrupt in the guest when the eventfd is
written. This irqfd file descriptor is then passed to the vhost user
application program.

vhost access to guest memory
''''''''''''''''''''''''''''

The vhost application is also allowed to directly access guest memory,
instead of needing to send the data as messages to QEMU. This is also
done with file descriptors sent to the vhost user application by QEMU.
These descriptors can be passed to ``mmap()`` by the vhost application
to map the guest address space into the vhost application.

IOMMUs introduce another level of complexity, since the address given to
the guest virtio device to DMA to or from is not a guest physical
address. This case is handled by having vhost code within QEMU register
as a listener for IOMMU mapping changes. The vhost application maintains
a cache of IOMMMU translations: sending translation requests back to
QEMU on cache misses, and in turn receiving flush requests from QEMU
when mappings are purged.

applicability to device separation
''''''''''''''''''''''''''''''''''

Much of the vhost model can be re-used by separated device emulation. In
particular, the ideas of using a socket between QEMU and the device
emulation application, using a file descriptor to inject interrupts into
the VM via KVM, and allowing the application to ``mmap()`` the guest
should be re used.

There are, however, some notable differences between how a vhost
application works and the needs of separated device emulation. The most
basic is that vhost uses custom virtio device drivers which always
trigger IO with MMIO stores. A separated device emulation model must
work with existing IO device models and guest device drivers. MMIO loads
break vhost store acceleration since they are synchronous - guest
progress cannot continue until the load has been emulated. By contrast,
stores are asynchronous, the guest can continue after the store event
has been sent to the vhost application.

Another difference is that in the vhost user model, a single daemon can
support multiple QEMU instances. This is contrary to the security regime
desired, in which the emulation application should only be allowed to
access the files or devices the VM it's running on behalf of can access.
#### qemu-io model

Qemu-io is a test harness used to test changes to the QEMU block backend
object code. (e.g., the code that implements disk images for disk driver
emulation) Qemu-io is not a device emulation application per se, but it
does compile the QEMU block objects into a separate binary from the main
QEMU one. This could be useful for disk device emulation, since its
emulation applications will need to include the QEMU block objects.

New separation model based on proxy objects
-------------------------------------------

A different model based on proxy objects in the QEMU program
communicating with remote emulation programs could provide separation
while minimizing the changes needed to the device emulation code. The
rest of this section is a discussion of how a proxy object model would
work.

Remote emulation processes
~~~~~~~~~~~~~~~~~~~~~~~~~~

The remote emulation process will run the QEMU object hierarchy without
modification. The device emulation objects will be also be based on the
QEMU code, because for anything but the simplest device, it would not be
a tractable to re-implement both the object model and the many device
backends that QEMU has.

The processes will communicate with the QEMU process over UNIX domain
sockets. The processes can be executed either as standalone processes,
or be executed by QEMU. In both cases, the host backends the emulation
processes will provide are specified on its command line, as they would
be for QEMU. For example:

::

    disk-proc -blockdev driver=file,node-name=file0,filename=disk-file0  \
    -blockdev driver=qcow2,node-name=drive0,file=file0

would indicate process *disk-proc* uses a qcow2 emulated disk named
*file0* as its backend.

Emulation processes may emulate more than one guest controller. A common
configuration might be to put all controllers of the same device class
(e.g., disk, network, etc.) in a single process, so that all backends of
the same type can be managed by a single QMP monitor.

communication with QEMU
^^^^^^^^^^^^^^^^^^^^^^^

The first argument to the remote emulation process will be a Unix domain
socket that connects with the Proxy object. This is a required argument.

::

    disk-proc <socket number> <backend list>

remote process QMP monitor
^^^^^^^^^^^^^^^^^^^^^^^^^^

Remote emulation processes can be monitored via QMP, similar to QEMU
itself. The QMP monitor socket is specified the same as for a QEMU
process:

::

    disk-proc -qmp unix:/tmp/disk-mon,server

can be monitored over the UNIX socket path */tmp/disk-mon*.

QEMU command line
~~~~~~~~~~~~~~~~~

Each remote device emulated in a remote process on the host is
represented as a *-device* of type *pci-proxy-dev*. A socket
sub-option to this option specifies the Unix socket that connects
to the remote process. An *id* sub-option is required, and it should
be the same id as used in the remote process.

::

    qemu-system-x86_64 ... -device pci-proxy-dev,id=lsi0,socket=3

can be used to add a device emulated in a remote process


QEMU management of remote processes
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

QEMU is not aware of the type of type of the remote PCI device. It is
a pass through device as far as QEMU is concerned.

communication with emulation process
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

primary channel
'''''''''''''''

The primary channel (referred to as com in the code) is used to bootstrap
the remote process. It is also used to pass on device-agnostic commands
like reset.

per-device channels
'''''''''''''''''''

Each remote device communicates with QEMU using a dedicated communication
channel. The proxy object sets up this channel using the primary
channel during its initialization.

QEMU device proxy objects
~~~~~~~~~~~~~~~~~~~~~~~~~

QEMU has an object model based on sub-classes inherited from the
"object" super-class. The sub-classes that are of interest here are the
"device" and "bus" sub-classes whose child sub-classes make up the
device tree of a QEMU emulated system.

The proxy object model will use device proxy objects to replace the
device emulation code within the QEMU process. These objects will live
in the same place in the object and bus hierarchies as the objects they
replace. i.e., the proxy object for an LSI SCSI controller will be a
sub-class of the "pci-device" class, and will have the same PCI bus
parent and the same SCSI bus child objects as the LSI controller object
it replaces.

It is worth noting that the same proxy object is used to mediate with
all types of remote PCI devices.

object initialization
^^^^^^^^^^^^^^^^^^^^^

The Proxy device objects are initialized in the exact same manner in
which any other QEMU device would be initialized.

In addition, the Proxy objects perform the following two tasks:
- Parses the "socket" sub option and connects to the remote process
using this channel
- Uses the "id" sub-option to connect to the emulated device on the
separate process

class\_init
'''''''''''

The ``class_init()`` method of a proxy object will, in general behave
similarly to the object it replaces, including setting any static
properties and methods needed by the proxy.

instance\_init / realize
''''''''''''''''''''''''

The ``instance_init()`` and ``realize()`` functions would only need to
perform tasks related to being a proxy, such are registering its own
MMIO handlers, or creating a child bus that other proxy devices can be
attached to later.

Other tasks will be device-specific. For example, PCI device objects
will initialize the PCI config space in order to make a valid PCI device
tree within the QEMU process.

address space registration
^^^^^^^^^^^^^^^^^^^^^^^^^^

Most devices are driven by guest device driver accesses to IO addresses
or ports. The QEMU device emulation code uses QEMU's memory region
function calls (such as ``memory_region_init_io()``) to add callback
functions that QEMU will invoke when the guest accesses the device's
areas of the IO address space. When a guest driver does access the
device, the VM will exit HW virtualization mode and return to QEMU,
which will then lookup and execute the corresponding callback function.

A proxy object would need to mirror the memory region calls the actual
device emulator would perform in its initialization code, but with its
own callbacks. When invoked by QEMU as a result of a guest IO operation,
they will forward the operation to the device emulation process.

PCI config space
^^^^^^^^^^^^^^^^

PCI devices also have a configuration space that can be accessed by the
guest driver. Guest accesses to this space is not handled by the device
emulation object, but by its PCI parent object. Much of this space is
read-only, but certain registers (especially BAR and MSI-related ones)
need to be propagated to the emulation process.

PCI parent proxy
''''''''''''''''

One way to propagate guest PCI config accesses is to create a
"pci-device-proxy" class that can serve as the parent of a PCI device
proxy object. This class's parent would be "pci-device" and it would
override the PCI parent's ``config_read()`` and ``config_write()``
methods with ones that forward these operations to the emulation
program.

interrupt receipt
^^^^^^^^^^^^^^^^^

A proxy for a device that generates interrupts will need to create a
socket to receive interrupt indications from the emulation process. An
incoming interrupt indication would then be sent up to its bus parent to
be injected into the guest. For example, a PCI device object may use
``pci_set_irq()``.

live migration
^^^^^^^^^^^^^^

The proxy will register to save and restore any *vmstate* it needs over
a live migration event. The device proxy does not need to manage the
remote device's *vmstate*; that will be handled by the remote process
proxy (see below).

QEMU remote device operation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Generic device operations, such as DMA, will be performed by the remote
process proxy by sending messages to the remote process.

DMA operations
^^^^^^^^^^^^^^

DMA operations would be handled much like vhost applications do. One of
the initial messages sent to the emulation process is a guest memory
table. Each entry in this table consists of a file descriptor and size
that the emulation process can ``mmap()`` to directly access guest
memory, similar to ``vhost_user_set_mem_table()``. Note guest memory
must be backed by file descriptors, such as when QEMU is given the
*-mem-path* command line option.

IOMMU operations
^^^^^^^^^^^^^^^^

When the emulated system includes an IOMMU, the remote process proxy in
QEMU will need to create a socket for IOMMU requests from the emulation
process. It will handle those requests with an
``address_space_get_iotlb_entry()`` call. In order to handle IOMMU
unmaps, the remote process proxy will also register as a listener on the
device's DMA address space. When an IOMMU memory region is created
within the DMA address space, an IOMMU notifier for unmaps will be added
to the memory region that will forward unmaps to the emulation process
over the IOMMU socket.

device hot-plug via QMP
^^^^^^^^^^^^^^^^^^^^^^^

An QMP "device\_add" command can add a device emulated by a remote
process. It will also have "rid" option to the command, just as the
*-device* command line option does. The remote process may either be one
started at QEMU startup, or be one added by the "add-process" QMP
command described above. In either case, the remote process proxy will
forward the new device's JSON description to the corresponding emulation
process.

live migration
^^^^^^^^^^^^^^

The remote process proxy will also register for live migration
notifications with ``vmstate_register()``. When called to save state,
the proxy will send the remote process a secondary socket file
descriptor to save the remote process's device *vmstate* over. The
incoming byte stream length and data will be saved as the proxy's
*vmstate*. When the proxy is resumed on its new host, this *vmstate*
will be extracted, and a secondary socket file descriptor will be sent
to the new remote process through which it receives the *vmstate* in
order to restore the devices there.

device emulation in remote process
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The parts of QEMU that the emulation program will need include the
object model; the memory emulation objects; the device emulation objects
of the targeted device, and any dependent devices; and, the device's
backends. It will also need code to setup the machine environment,
handle requests from the QEMU process, and route machine-level requests
(such as interrupts or IOMMU mappings) back to the QEMU process.

initialization
^^^^^^^^^^^^^^

The process initialization sequence will follow the same sequence
followed by QEMU. It will first initialize the backend objects, then
device emulation objects. The JSON descriptions sent by the QEMU process
will drive which objects need to be created.

-  address spaces

Before the device objects are created, the initial address spaces and
memory regions must be configured with ``memory_map_init()``. This
creates a RAM memory region object (*system\_memory*) and an IO memory
region object (*system\_io*).

-  RAM

RAM memory region creation will follow how ``pc_memory_init()`` creates
them, but must use ``memory_region_init_ram_from_fd()`` instead of
``memory_region_allocate_system_memory()``. The file descriptors needed
will be supplied by the guest memory table from above. Those RAM regions
would then be added to the *system\_memory* memory region with
``memory_region_add_subregion()``.

-  PCI

IO initialization will be driven by the JSON descriptions sent from the
QEMU process. For a PCI device, a PCI bus will need to be created with
``pci_root_bus_new()``, and a PCI memory region will need to be created
and added to the *system\_memory* memory region with
``memory_region_add_subregion_overlap()``. The overlap version is
required for architectures where PCI memory overlaps with RAM memory.

MMIO handling
^^^^^^^^^^^^^

The device emulation objects will use ``memory_region_init_io()`` to
install their MMIO handlers, and ``pci_register_bar()`` to associate
those handlers with a PCI BAR, as they do within QEMU currently.

In order to use ``address_space_rw()`` in the emulation process to
handle MMIO requests from QEMU, the PCI physical addresses must be the
same in the QEMU process and the device emulation process. In order to
accomplish that, guest BAR programming must also be forwarded from QEMU
to the emulation process.

interrupt injection
^^^^^^^^^^^^^^^^^^^

When device emulation wants to inject an interrupt into the VM, the
request climbs the device's bus object hierarchy until the point where a
bus object knows how to signal the interrupt to the guest. The details
depend on the type of interrupt being raised.

-  PCI pin interrupts

On x86 systems, there is an emulated IOAPIC object attached to the root
PCI bus object, and the root PCI object forwards interrupt requests to
it. The IOAPIC object, in turn, calls the KVM driver to inject the
corresponding interrupt into the VM. The simplest way to handle this in
an emulation process would be to setup the root PCI bus driver (via
``pci_bus_irqs()``) to send a interrupt request back to the QEMU
process, and have the device proxy object reflect it up the PCI tree
there.

-  PCI MSI/X interrupts

PCI MSI/X interrupts are implemented in HW as DMA writes to a
CPU-specific PCI address. In QEMU on x86, a KVM APIC object receives
these DMA writes, then calls into the KVM driver to inject the interrupt
into the VM. A simple emulation process implementation would be to send
the MSI DMA address from QEMU as a message at initialization, then
install an address space handler at that address which forwards the MSI
message back to QEMU.

DMA operations
^^^^^^^^^^^^^^

When a emulation object wants to DMA into or out of guest memory, it
first must use dma\_memory\_map() to convert the DMA address to a local
virtual address. The emulation process memory region objects setup above
will be used to translate the DMA address to a local virtual address the
device emulation code can access.

IOMMU
^^^^^

When an IOMMU is in use in QEMU, DMA translation uses IOMMU memory
regions to translate the DMA address to a guest physical address before
that physical address can be translated to a local virtual address. The
emulation process will need similar functionality.

-  IOTLB cache

The emulation process will maintain a cache of recent IOMMU translations
(the IOTLB). When the translate() callback of an IOMMU memory region is
invoked, the IOTLB cache will be searched for an entry that will map the
DMA address to a guest PA. On a cache miss, a message will be sent back
to QEMU requesting the corresponding translation entry, which be both be
used to return a guest address and be added to the cache.

-  IOTLB purge

The IOMMU emulation will also need to act on unmap requests from QEMU.
These happen when the guest IOMMU driver purges an entry from the
guest's translation table.

live migration
^^^^^^^^^^^^^^

When a remote process receives a live migration indication from QEMU, it
will set up a channel using the received file descriptor with
``qio_channel_socket_new_fd()``. This channel will be used to create a
*QEMUfile* that can be passed to ``qemu_save_device_state()`` to send
the process's device state back to QEMU. This method will be reversed on
restore - the channel will be passed to ``qemu_loadvm_state()`` to
restore the device state.

Accelerating device emulation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The messages that are required to be sent between QEMU and the emulation
process can add considerable latency to IO operations. The optimizations
described below attempt to ameliorate this effect by allowing the
emulation process to communicate directly with the kernel KVM driver.
The KVM file descriptors created would be passed to the emulation process
via initialization messages, much like the guest memory table is done.
#### MMIO acceleration

Vhost user applications can receive guest virtio driver stores directly
from KVM. The issue with the eventfd mechanism used by vhost user is
that it does not pass any data with the event indication, so it cannot
handle guest loads or guest stores that carry store data. This concept
could, however, be expanded to cover more cases.

The expanded idea would require a new type of KVM device:
*KVM\_DEV\_TYPE\_USER*. This device has two file descriptors: a master
descriptor that QEMU can use for configuration, and a slave descriptor
that the emulation process can use to receive MMIO notifications. QEMU
would create both descriptors using the KVM driver, and pass the slave
descriptor to the emulation process via an initialization message.

data structures
^^^^^^^^^^^^^^^

-  guest physical range

The guest physical range structure describes the address range that a
device will respond to. It includes the base and length of the range, as
well as which bus the range resides on (e.g., on an x86machine, it can
specify whether the range refers to memory or IO addresses).

A device can have multiple physical address ranges it responds to (e.g.,
a PCI device can have multiple BARs), so the structure will also include
an enumerated identifier to specify which of the device's ranges is
being referred to.

+--------+----------------------------+
| Name   | Description                |
+========+============================+
| addr   | range base address         |
+--------+----------------------------+
| len    | range length               |
+--------+----------------------------+
| bus    | addr type (memory or IO)   |
+--------+----------------------------+
| id     | range ID (e.g., PCI BAR)   |
+--------+----------------------------+

-  MMIO request structure

This structure describes an MMIO operation. It includes which guest
physical range the MMIO was within, the offset within that range, the
MMIO type (e.g., load or store), and its length and data. It also
includes a sequence number that can be used to reply to the MMIO, and
the CPU that issued the MMIO.

+----------+------------------------+
| Name     | Description            |
+==========+========================+
| rid      | range MMIO is within   |
+----------+------------------------+
| offset   | offset withing *rid*   |
+----------+------------------------+
| type     | e.g., load or store    |
+----------+------------------------+
| len      | MMIO length            |
+----------+------------------------+
| data     | store data             |
+----------+------------------------+
| seq      | sequence ID            |
+----------+------------------------+

-  MMIO request queues

MMIO request queues are FIFO arrays of MMIO request structures. There
are two queues: pending queue is for MMIOs that haven't been read by the
emulation program, and the sent queue is for MMIOs that haven't been
acknowledged. The main use of the second queue is to validate MMIO
replies from the emulation program.

-  scoreboard

Each CPU in the VM is emulated in QEMU by a separate thread, so multiple
MMIOs may be waiting to be consumed by an emulation program and multiple
threads may be waiting for MMIO replies. The scoreboard would contain a
wait queue and sequence number for the per-CPU threads, allowing them to
be individually woken when the MMIO reply is received from the emulation
program. It also tracks the number of posted MMIO stores to the device
that haven't been replied to, in order to satisfy the PCI constraint
that a load to a device will not complete until all previous stores to
that device have been completed.

-  device shadow memory

Some MMIO loads do not have device side-effects. These MMIOs can be
completed without sending a MMIO request to the emulation program if the
emulation program shares a shadow image of the device's memory image
with the KVM driver.

The emulation program will ask the KVM driver to allocate memory for the
shadow image, and will then use ``mmap()`` to directly access it. The
emulation program can control KVM access to the shadow image by sending
KVM an access map telling it which areas of the image have no
side-effects (and can be completed immediately), and which require a
MMIO request to the emulation program. The access map can also inform
the KVM drive which size accesses are allowed to the image.

master descriptor
^^^^^^^^^^^^^^^^^

The master descriptor is used by QEMU to configure the new KVM device.
The descriptor would be returned by the KVM driver when QEMU issues a
*KVM\_CREATE\_DEVICE* ``ioctl()`` with a *KVM\_DEV\_TYPE\_USER* type.

KVM\_DEV\_TYPE\_USER device ops


The *KVM\_DEV\_TYPE\_USER* operations vector will be registered by a
``kvm_register_device_ops()`` call when the KVM system in initialized by
``kvm_init()``. These device ops are called by the KVM driver when QEMU
executes certain ``ioctl()`` operations on its KVM file descriptor. They
include:

-  create

This routine is called when QEMU issues a *KVM\_CREATE\_DEVICE*
``ioctl()`` on its per-VM file descriptor. It will allocate and
initialize a KVM user device specific data structure, and assign the
*kvm\_device* private field to it.

-  ioctl

This routine is invoked when QEMU issues an ``ioctl()`` on the master
descriptor. The ``ioctl()`` commands supported are defined by the KVM
device type. *KVM\_DEV\_TYPE\_USER* ones will need several commands:

*KVM\_DEV\_USER\_SLAVE\_FD* creates the slave file descriptor that will
be passed to the device emulation program. Only one slave can be created
by each master descriptor. The file operations performed by this
descriptor are described below.

The *KVM\_DEV\_USER\_PA\_RANGE* command configures a guest physical
address range that the slave descriptor will receive MMIO notifications
for. The range is specified by a guest physical range structure
argument. For buses that assign addresses to devices dynamically, this
command can be executed while the guest is running, such as the case
when a guest changes a device's PCI BAR registers.

*KVM\_DEV\_USER\_PA\_RANGE* will use ``kvm_io_bus_register_dev()`` to
register *kvm\_io\_device\_ops* callbacks to be invoked when the guest
performs a MMIO operation within the range. When a range is changed,
``kvm_io_bus_unregister_dev()`` is used to remove the previous
instantiation.

*KVM\_DEV\_USER\_TIMEOUT* will configure a timeout value that specifies
how long KVM will wait for the emulation process to respond to a MMIO
indication.

-  destroy

This routine is called when the VM instance is destroyed. It will need
to destroy the slave descriptor; and free any memory allocated by the
driver, as well as the *kvm\_device* structure itself.

slave descriptor
^^^^^^^^^^^^^^^^

The slave descriptor will have its own file operations vector, which
responds to system calls on the descriptor performed by the device
emulation program.

-  read

A read returns any pending MMIO requests from the KVM driver as MMIO
request structures. Multiple structures can be returned if there are
multiple MMIO operations pending. The MMIO requests are moved from the
pending queue to the sent queue, and if there are threads waiting for
space in the pending to add new MMIO operations, they will be woken
here.

-  write

A write also consists of a set of MMIO requests. They are compared to
the MMIO requests in the sent queue. Matches are removed from the sent
queue, and any threads waiting for the reply are woken. If a store is
removed, then the number of posted stores in the per-CPU scoreboard is
decremented. When the number is zero, and a non side-effect load was
waiting for posted stores to complete, the load is continued.

-  ioctl

There are several ioctl()s that can be performed on the slave
descriptor.

A *KVM\_DEV\_USER\_SHADOW\_SIZE* ``ioctl()`` causes the KVM driver to
allocate memory for the shadow image. This memory can later be
``mmap()``\ ed by the emulation process to share the emulation's view of
device memory with the KVM driver.

A *KVM\_DEV\_USER\_SHADOW\_CTRL* ``ioctl()`` controls access to the
shadow image. It will send the KVM driver a shadow control map, which
specifies which areas of the image can complete guest loads without
sending the load request to the emulation program. It will also specify
the size of load operations that are allowed.

-  poll

An emulation program will use the ``poll()`` call with a *POLLIN* flag
to determine if there are MMIO requests waiting to be read. It will
return if the pending MMIO request queue is not empty.

-  mmap

This call allows the emulation program to directly access the shadow
image allocated by the KVM driver. As device emulation updates device
memory, changes with no side-effects will be reflected in the shadow,
and the KVM driver can satisfy guest loads from the shadow image without
needing to wait for the emulation program.

kvm\_io\_device ops
^^^^^^^^^^^^^^^^^^^

Each KVM per-CPU thread can handle MMIO operation on behalf of the guest
VM. KVM will use the MMIO's guest physical address to search for a
matching *kvm\_io\_device* to see if the MMIO can be handled by the KVM
driver instead of exiting back to QEMU. If a match is found, the
corresponding callback will be invoked.

-  read

This callback is invoked when the guest performs a load to the device.
Loads with side-effects must be handled synchronously, with the KVM
driver putting the QEMU thread to sleep waiting for the emulation
process reply before re-starting the guest. Loads that do not have
side-effects may be optimized by satisfying them from the shadow image,
if there are no outstanding stores to the device by this CPU. PCI memory
ordering demands that a load cannot complete before all older stores to
the same device have been completed.

-  write

Stores can be handled asynchronously unless the pending MMIO request
queue is full. In this case, the QEMU thread must sleep waiting for
space in the queue. Stores will increment the number of posted stores in
the per-CPU scoreboard, in order to implement the PCI ordering
constraint above.

interrupt acceleration
^^^^^^^^^^^^^^^^^^^^^^

This performance optimization would work much like a vhost user
application does, where the QEMU process sets up *eventfds* that cause
the device's corresponding interrupt to be triggered by the KVM driver.
These irq file descriptors are sent to the emulation process at
initialization, and are used when the emulation code raises a device
interrupt.

intx acceleration
'''''''''''''''''

Traditional PCI pin interrupts are level based, so, in addition to an
irq file descriptor, a re-sampling file descriptor needs to be sent to
the emulation program. This second file descriptor allows multiple
devices sharing an irq to be notified when the interrupt has been
acknowledged by the guest, so they can re-trigger the interrupt if their
device has not de-asserted its interrupt.

intx irq descriptor


The irq descriptors are created by the proxy object
``using event_notifier_init()`` to create the irq and re-sampling
*eventds*, and ``kvm_vm_ioctl(KVM_IRQFD)`` to bind them to an interrupt.
The interrupt route can be found with
``pci_device_route_intx_to_irq()``.

intx routing changes


Intx routing can be changed when the guest programs the APIC the device
pin is connected to. The proxy object in QEMU will use
``pci_device_set_intx_routing_notifier()`` to be informed of any guest
changes to the route. This handler will broadly follow the VFIO
interrupt logic to change the route: de-assigning the existing irq
descriptor from its route, then assigning it the new route. (see
``vfio_intx_update()``)

MSI/X acceleration
''''''''''''''''''

MSI/X interrupts are sent as DMA transactions to the host. The interrupt
data contains a vector that is programmed by the guest, A device may have
multiple MSI interrupts associated with it, so multiple irq descriptors
may need to be sent to the emulation program.

MSI/X irq descriptor


This case will also follow the VFIO example. For each MSI/X interrupt,
an *eventfd* is created, a virtual interrupt is allocated by
``kvm_irqchip_add_msi_route()``, and the virtual interrupt is bound to
the eventfd with ``kvm_irqchip_add_irqfd_notifier()``.

MSI/X config space changes


The guest may dynamically update several MSI-related tables in the
device's PCI config space. These include per-MSI interrupt enables and
vector data. Additionally, MSIX tables exist in device memory space, not
config space. Much like the BAR case above, the proxy object must look
at guest config space programming to keep the MSI interrupt state
consistent between QEMU and the emulation program.

--------------

Disaggregated CPU emulation
---------------------------

After IO services have been disaggregated, a second phase would be to
separate a process to handle CPU instruction emulation from the main
QEMU control function. There are no object separation points for this
code, so the first task would be to create one.

Host access controls
--------------------

Separating QEMU relies on the host OS's access restriction mechanisms to
enforce that the differing processes can only access the objects they
are entitled to. There are a couple types of mechanisms usually provided
by general purpose OSs.

Discretionary access control
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Discretionary access control allows each user to control who can access
their files. In Linux, this type of control is usually too coarse for
QEMU separation, since it only provides three separate access controls:
one for the same user ID, the second for users IDs with the same group
ID, and the third for all other user IDs. Each device instance would
need a separate user ID to provide access control, which is likely to be
unwieldy for dynamically created VMs.

Mandatory access control
~~~~~~~~~~~~~~~~~~~~~~~~

Mandatory access control allows the OS to add an additional set of
controls on top of discretionary access for the OS to control. It also
adds other attributes to processes and files such as types, roles, and
categories, and can establish rules for how processes and files can
interact.

Type enforcement
^^^^^^^^^^^^^^^^

Type enforcement assigns a *type* attribute to processes and files, and
allows rules to be written on what operations a process with a given
type can perform on a file with a given type. QEMU separation could take
advantage of type enforcement by running the emulation processes with
different types, both from the main QEMU process, and from the emulation
processes of different classes of devices.

For example, guest disk images and disk emulation processes could have
types separate from the main QEMU process and non-disk emulation
processes, and the type rules could prevent processes other than disk
emulation ones from accessing guest disk images. Similarly, network
emulation processes can have a type separate from the main QEMU process
and non-network emulation process, and only that type can access the
host tun/tap device used to provide guest networking.

Category enforcement
^^^^^^^^^^^^^^^^^^^^

Category enforcement assigns a set of numbers within a given range to
the process or file. The process is granted access to the file if the
process's set is a superset of the file's set. This enforcement can be
used to separate multiple instances of devices in the same class.

For example, if there are multiple disk devices provides to a guest,
each device emulation process could be provisioned with a separate
category. The different device emulation processes would not be able to
access each other's backing disk images.

Alternatively, categories could be used in lieu of the type enforcement
scheme described above. In this scenario, different categories would be
used to prevent device emulation processes in different classes from
accessing resources assigned to other classes.

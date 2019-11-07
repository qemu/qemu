================================
POWER9 XIVE interrupt controller
================================

The POWER9 processor comes with a new interrupt controller
architecture, called XIVE as "eXternal Interrupt Virtualization
Engine".

Compared to the previous architecture, the main characteristics of
XIVE are to support a larger number of interrupt sources and to
deliver interrupts directly to virtual processors without hypervisor
assistance. This removes the context switches required for the
delivery process.


XIVE architecture
=================

The XIVE IC is composed of three sub-engines, each taking care of a
processing layer of external interrupts:

- Interrupt Virtualization Source Engine (IVSE), or Source Controller
  (SC). These are found in PCI PHBs, in the Processor Service
  Interface (PSI) host bridge Controller, but also inside the main
  controller for the core IPIs and other sub-chips (NX, CAP, NPU) of
  the chip/processor. They are configured to feed the IVRE with
  events.
- Interrupt Virtualization Routing Engine (IVRE) or Virtualization
  Controller (VC). It handles event coalescing and perform interrupt
  routing by matching an event source number with an Event
  Notification Descriptor (END).
- Interrupt Virtualization Presentation Engine (IVPE) or Presentation
  Controller (PC). It maintains the interrupt context state of each
  thread and handles the delivery of the external interrupt to the
  thread.

::

                XIVE Interrupt Controller
                +------------------------------------+      IPIs
                | +---------+ +---------+ +--------+ |    +-------+
                | |IVRE     | |Common Q | |IVPE    |----> | CORES |
                | |     esb | |         | |        |----> |       |
                | |     eas | |  Bridge | |   tctx |----> |       |
                | |SC   end | |         | |    nvt | |    |       |
    +------+    | +---------+ +----+----+ +--------+ |    +-+-+-+-+
    | RAM  |    +------------------|-----------------+      | | |
    |      |                       |                        | | |
    |      |                       |                        | | |
    |      |  +--------------------v------------------------v-v-v--+    other
    |      <--+                     Power Bus                      +--> chips
    |  esb |  +---------+-----------------------+------------------+
    |  eas |            |                       |
    |  end |         +--|------+                |
    |  nvt |       +----+----+ |           +----+----+
    +------+       |IVSE     | |           |IVSE     |
                   |         | |           |         |
                   | PQ-bits | |           | PQ-bits |
                   | local   |-+           |  in VC  |
                   +---------+             +---------+
                      PCIe                 NX,NPU,CAPI


    PQ-bits: 2 bits source state machine (P:pending Q:queued)
    esb: Event State Buffer (Array of PQ bits in an IVSE)
    eas: Event Assignment Structure
    end: Event Notification Descriptor
    nvt: Notification Virtual Target
    tctx: Thread interrupt Context registers



XIVE internal tables
--------------------

Each of the sub-engines uses a set of tables to redirect interrupts
from event sources to CPU threads.

::

                                            +-------+
    User or O/S                             |  EQ   |
        or                          +------>|entries|
    Hypervisor                      |       |  ..   |
      Memory                        |       +-------+
                                    |           ^
                                    |           |
               +-------------------------------------------------+
                                    |           |
    Hypervisor      +------+    +---+--+    +---+--+   +------+
      Memory        | ESB  |    | EAT  |    | ENDT |   | NVTT |
     (skiboot)      +----+-+    +----+-+    +----+-+   +------+
                      ^  |        ^  |        ^  |       ^
                      |  |        |  |        |  |       |
               +-------------------------------------------------+
                      |  |        |  |        |  |       |
                      |  |        |  |        |  |       |
                 +----|--|--------|--|--------|--|-+   +-|-----+    +------+
                 |    |  |        |  |        |  | |   | | tctx|    |Thread|
     IPI or   ---+    +  v        +  v        +  v |---| +  .. |----->     |
    HW events    |                                 |   |       |    |      |
                 |             IVRE                |   | IVPE  |    +------+
                 +---------------------------------+   +-------+


The IVSE have a 2-bits state machine, P for pending and Q for queued,
for each source that allows events to be triggered. They are stored in
an Event State Buffer (ESB) array and can be controlled by MMIOs.

If the event is let through, the IVRE looks up in the Event Assignment
Structure (EAS) table for an Event Notification Descriptor (END)
configured for the source. Each Event Notification Descriptor defines
a notification path to a CPU and an in-memory Event Queue, in which
will be enqueued an EQ data for the O/S to pull.

The IVPE determines if a Notification Virtual Target (NVT) can handle
the event by scanning the thread contexts of the VCPUs dispatched on
the processor HW threads. It maintains the interrupt context state of
each thread in a NVT table.

XIVE thread interrupt context
-----------------------------

The XIVE presenter can generate four different exceptions to its
HW threads:

- hypervisor exception
- O/S exception
- Event-Based Branch (user level)
- msgsnd (doorbell)

Each exception has a state independent from the others called a Thread
Interrupt Management context. This context is a set of registers which
lets the thread handle priority management and interrupt
acknowledgment among other things. The most important ones being :

- Interrupt Priority Register  (PIPR)
- Interrupt Pending Buffer     (IPB)
- Current Processor Priority   (CPPR)
- Notification Source Register (NSR)

TIMA
~~~~

The Thread Interrupt Management registers are accessible through a
specific MMIO region, called the Thread Interrupt Management Area
(TIMA), four aligned pages, each exposing a different view of the
registers. First page (page address ending in ``0b00``) gives access
to the entire context and is reserved for the ring 0 view for the
physical thread context. The second (page address ending in ``0b01``)
is for the hypervisor, ring 1 view. The third (page address ending in
``0b10``) is for the operating system, ring 2 view. The fourth (page
address ending in ``0b11``) is for user level, ring 3 view.

Interrupt flow from an O/S perspective
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

After an event data has been enqueued in the O/S Event Queue, the IVPE
raises the bit corresponding to the priority of the pending interrupt
in the register IBP (Interrupt Pending Buffer) to indicate that an
event is pending in one of the 8 priority queues. The Pending
Interrupt Priority Register (PIPR) is also updated using the IPB. This
register represent the priority of the most favored pending
notification.

The PIPR is then compared to the Current Processor Priority
Register (CPPR). If it is more favored (numerically less than), the
CPU interrupt line is raised and the EO bit of the Notification Source
Register (NSR) is updated to notify the presence of an exception for
the O/S. The O/S acknowledges the interrupt with a special load in the
Thread Interrupt Management Area.

The O/S handles the interrupt and when done, performs an EOI using a
MMIO operation on the ESB management page of the associate source.

Overview of the QEMU models for XIVE
====================================

The XiveSource models the IVSE in general, internal and external. It
handles the source ESBs and the MMIO interface to control them.

The XiveNotifier is a small helper interface interconnecting the
XiveSource to the XiveRouter.

The XiveRouter is an abstract model acting as a combined IVRE and
IVPE. It routes event notifications using the EAS and END tables to
the IVPE sub-engine which does a CAM scan to find a CPU to deliver the
exception. Storage should be provided by the inheriting classes.

XiveEnDSource is a special source object. It exposes the END ESB MMIOs
of the Event Queues which are used for coalescing event notifications
and for escalation. Not used on the field, only to sync the EQ cache
in OPAL.

Finally, the XiveTCTX contains the interrupt state context of a thread,
four sets of registers, one for each exception that can be delivered
to a CPU. These contexts are scanned by the IVPE to find a matching VP
when a notification is triggered. It also models the Thread Interrupt
Management Area (TIMA), which exposes the thread context registers to
the CPU for interrupt management.

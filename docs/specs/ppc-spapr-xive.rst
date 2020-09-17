XIVE for sPAPR (pseries machines)
=================================

The POWER9 processor comes with a new interrupt controller
architecture, called XIVE as "eXternal Interrupt Virtualization
Engine". It supports a larger number of interrupt sources and offers
virtualization features which enables the HW to deliver interrupts
directly to virtual processors without hypervisor assistance.

A QEMU ``pseries`` machine (which is PAPR compliant) using POWER9
processors can run under two interrupt modes:

- *Legacy Compatibility Mode*

  the hypervisor provides identical interfaces and similar
  functionality to PAPR+ Version 2.7.  This is the default mode

  It is also referred as *XICS* in QEMU.

- *XIVE native exploitation mode*

  the hypervisor provides new interfaces to manage the XIVE control
  structures, and provides direct control for interrupt management
  through MMIO pages.

Which interrupt modes can be used by the machine is negotiated with
the guest O/S during the Client Architecture Support negotiation
sequence. The two modes are mutually exclusive.

Both interrupt mode share the same IRQ number space. See below for the
layout.

CAS Negotiation
---------------

QEMU advertises the supported interrupt modes in the device tree
property ``ibm,arch-vec-5-platform-support`` in byte 23 and the OS
Selection for XIVE is indicated in the ``ibm,architecture-vec-5``
property byte 23.

The interrupt modes supported by the machine depend on the CPU type
(POWER9 is required for XIVE) but also on the machine property
``ic-mode`` which can be set on the command line. It can take the
following values: ``xics``, ``xive``, and ``dual`` which is the
default mode. ``dual`` means that both modes XICS **and** XIVE are
supported and if the guest OS supports XIVE, this mode will be
selected.

The chosen interrupt mode is activated after a reconfiguration done
in a machine reset.

KVM negotiation
---------------

When the guest starts under KVM, the capabilities of the host kernel
and QEMU are also negotiated. Depending on the version of the host
kernel, KVM will advertise the XIVE capability to QEMU or not.

Nevertheless, the available interrupt modes in the machine should not
depend on the XIVE KVM capability of the host. On older kernels
without XIVE KVM support, QEMU will use the emulated XIVE device as a
fallback and on newer kernels (>=5.2), the KVM XIVE device.

XIVE native exploitation mode is not supported for KVM nested guests,
VMs running under a L1 hypervisor (KVM on pSeries). In that case, the
hypervisor will not advertise the KVM capability and QEMU will use the
emulated XIVE device, same as for older versions of KVM.

As a final refinement, the user can also switch the use of the KVM
device with the machine option ``kernel_irqchip``.


XIVE support in KVM
~~~~~~~~~~~~~~~~~~~

For guest OSes supporting XIVE, the resulting interrupt modes on host
kernels with XIVE KVM support are the following:

==============  =============  =============  ================
ic-mode                            kernel_irqchip
--------------  ----------------------------------------------
/               allowed        off            on
                (default)
==============  =============  =============  ================
dual (default)  XIVE KVM       XIVE emul.     XIVE KVM
xive            XIVE KVM       XIVE emul.     XIVE KVM
xics            XICS KVM       XICS emul.     XICS KVM
==============  =============  =============  ================

For legacy guest OSes without XIVE support, the resulting interrupt
modes are the following:

==============  =============  =============  ================
ic-mode                            kernel_irqchip
--------------  ----------------------------------------------
/               allowed        off            on
                (default)
==============  =============  =============  ================
dual (default)  XICS KVM       XICS emul.     XICS KVM
xive            QEMU error(3)  QEMU error(3)  QEMU error(3)
xics            XICS KVM       XICS emul.     XICS KVM
==============  =============  =============  ================

(3) QEMU fails at CAS with ``Guest requested unavailable interrupt
    mode (XICS), either don't set the ic-mode machine property or try
    ic-mode=xics or ic-mode=dual``


No XIVE support in KVM
~~~~~~~~~~~~~~~~~~~~~~

For guest OSes supporting XIVE, the resulting interrupt modes on host
kernels without XIVE KVM support are the following:

==============  =============  =============  ================
ic-mode                            kernel_irqchip
--------------  ----------------------------------------------
/               allowed        off            on
                (default)
==============  =============  =============  ================
dual (default)  XIVE emul.(1)  XIVE emul.     QEMU error (2)
xive            XIVE emul.(1)  XIVE emul.     QEMU error (2)
xics            XICS KVM       XICS emul.     XICS KVM
==============  =============  =============  ================


(1) QEMU warns with ``warning: kernel_irqchip requested but unavailable:
    IRQ_XIVE capability must be present for KVM``
    In some cases (old host kernels or KVM nested guests), one may hit a
    QEMU/KVM incompatibility due to device destruction in reset. QEMU fails
    with ``KVM is incompatible with ic-mode=dual,kernel-irqchip=on``
(2) QEMU fails with ``kernel_irqchip requested but unavailable:
    IRQ_XIVE capability must be present for KVM``


For legacy guest OSes without XIVE support, the resulting interrupt
modes are the following:

==============  =============  =============  ================
ic-mode                            kernel_irqchip
--------------  ----------------------------------------------
/               allowed        off            on
                (default)
==============  =============  =============  ================
dual (default)  QEMU error(4)  XICS emul.     QEMU error(4)
xive            QEMU error(3)  QEMU error(3)  QEMU error(3)
xics            XICS KVM       XICS emul.     XICS KVM
==============  =============  =============  ================

(3) QEMU fails at CAS with ``Guest requested unavailable interrupt
    mode (XICS), either don't set the ic-mode machine property or try
    ic-mode=xics or ic-mode=dual``
(4) QEMU/KVM incompatibility due to device destruction in reset. QEMU fails
    with ``KVM is incompatible with ic-mode=dual,kernel-irqchip=on``


XIVE Device tree properties
---------------------------

The properties for the PAPR interrupt controller node when the *XIVE
native exploitation mode* is selected should contain:

- ``device_type``

  value should be "power-ivpe".

- ``compatible``

  value should be "ibm,power-ivpe".

- ``reg``

  contains the base address and size of the thread interrupt
  managnement areas (TIMA), for the User level and for the Guest OS
  level. Only the Guest OS level is taken into account today.

- ``ibm,xive-eq-sizes``

  the size of the event queues. One cell per size supported, contains
  log2 of size, in ascending order.

- ``ibm,xive-lisn-ranges``

  the IRQ interrupt number ranges assigned to the guest for the IPIs.

The root node also exports :

- ``ibm,plat-res-int-priorities``

  contains a list of priorities that the hypervisor has reserved for
  its own use.

IRQ number space
----------------

IRQ Number space of the ``pseries`` machine is 8K wide and is the same
for both interrupt mode. The different ranges are defined as follow :

- ``0x0000 .. 0x0FFF`` 4K CPU IPIs (only used under XIVE)
- ``0x1000 .. 0x1000`` 1 EPOW
- ``0x1001 .. 0x1001`` 1 HOTPLUG
- ``0x1002 .. 0x10FF`` unused
- ``0x1100 .. 0x11FF`` 256 VIO devices
- ``0x1200 .. 0x127F`` 32x4 LSIs for PHB devices
- ``0x1280 .. 0x12FF`` unused
- ``0x1300 .. 0x1FFF`` PHB MSIs (dynamically allocated)

Monitoring XIVE
---------------

The state of the XIVE interrupt controller can be queried through the
monitor commands ``info pic``. The output comes in two parts.

First, the state of the thread interrupt context registers is dumped
for each CPU :

::

   (qemu) info pic
   CPU[0000]:   QW   NSR CPPR IPB LSMFB ACK# INC AGE PIPR  W2
   CPU[0000]: USER    00   00  00    00   00  00  00   00  00000000
   CPU[0000]:   OS    00   ff  00    00   ff  00  ff   ff  80000400
   CPU[0000]: POOL    00   00  00    00   00  00  00   00  00000000
   CPU[0000]: PHYS    00   00  00    00   00  00  00   ff  00000000
   ...

In the case of a ``pseries`` machine, QEMU acts as the hypervisor and only
the O/S and USER register rings make sense. ``W2`` contains the vCPU CAM
line which is set to the VP identifier.

Then comes the routing information which aggregates the EAS and the
END configuration:

::

   ...
   LISN         PQ    EISN     CPU/PRIO EQ
   00000000 MSI --    00000010   0/6    380/16384 @1fe3e0000 ^1 [ 80000010 ... ]
   00000001 MSI --    00000010   1/6    305/16384 @1fc230000 ^1 [ 80000010 ... ]
   00000002 MSI --    00000010   2/6    220/16384 @1fc2f0000 ^1 [ 80000010 ... ]
   00000003 MSI --    00000010   3/6    201/16384 @1fc390000 ^1 [ 80000010 ... ]
   00000004 MSI -Q  M 00000000
   00000005 MSI -Q  M 00000000
   00000006 MSI -Q  M 00000000
   00000007 MSI -Q  M 00000000
   00001000 MSI --    00000012   0/6    380/16384 @1fe3e0000 ^1 [ 80000010 ... ]
   00001001 MSI --    00000013   0/6    380/16384 @1fe3e0000 ^1 [ 80000010 ... ]
   00001100 MSI --    00000100   1/6    305/16384 @1fc230000 ^1 [ 80000010 ... ]
   00001101 MSI -Q  M 00000000
   00001200 LSI -Q  M 00000000
   00001201 LSI -Q  M 00000000
   00001202 LSI -Q  M 00000000
   00001203 LSI -Q  M 00000000
   00001300 MSI --    00000102   1/6    305/16384 @1fc230000 ^1 [ 80000010 ... ]
   00001301 MSI --    00000103   2/6    220/16384 @1fc2f0000 ^1 [ 80000010 ... ]
   00001302 MSI --    00000104   3/6    201/16384 @1fc390000 ^1 [ 80000010 ... ]

The source information and configuration:

- The ``LISN`` column outputs the interrupt number of the source in
  range ``[ 0x0 ... 0x1FFF ]`` and its type : ``MSI`` or ``LSI``
- The ``PQ`` column reflects the state of the PQ bits of the source :

  - ``--`` source is ready to take events
  - ``P-`` an event was sent and an EOI is PENDING
  - ``PQ`` an event was QUEUED
  - ``-Q`` source is OFF

  a ``M`` indicates that source is *MASKED* at the EAS level,

The targeting configuration :

- The ``EISN`` column is the event data that will be queued in the event
  queue of the O/S.
- The ``CPU/PRIO`` column is the tuple defining the CPU number and
  priority queue serving the source.
- The ``EQ`` column outputs :

  - the current index of the event queue/ the max number of entries
  - the O/S event queue address
  - the toggle bit
  - the last entries that were pushed in the event queue.

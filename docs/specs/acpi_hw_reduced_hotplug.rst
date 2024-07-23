==================================================
QEMU and ACPI BIOS Generic Event Device interface
==================================================

The ACPI *Generic Event Device* (GED) is a HW reduced platform
specific device introduced in ACPI v6.1 that handles all platform
events, including the hotplug ones. GED is modelled as a device
in the namespace with a _HID defined to be ACPI0013. This document
describes the interface between QEMU and the ACPI BIOS.

GED allows HW reduced platforms to handle interrupts in ACPI ASL
statements. It follows a very similar approach to the _EVT method
from GPIO events. All interrupts are listed in  _CRS and the handler
is written in _EVT method. However, the QEMU implementation uses a
single interrupt for the GED device, relying on an IO memory region
to communicate the type of device affected by the interrupt. This way,
we can support up to 32 events with a unique interrupt.

**Here is an example,**

::

   Device (\_SB.GED)
   {
       Name (_HID, "ACPI0013")
       Name (_UID, Zero)
       Name (_CRS, ResourceTemplate ()
       {
           Interrupt (ResourceConsumer, Edge, ActiveHigh, Exclusive, ,, )
           {
               0x00000029,
           }
       })
       OperationRegion (EREG, SystemMemory, 0x09080000, 0x04)
       Field (EREG, DWordAcc, NoLock, WriteAsZeros)
       {
           ESEL,   32
       }
       Method (_EVT, 1, Serialized)
       {
           Local0 = ESEL // ESEL = IO memory region which specifies the
                         // device type.
           If (((Local0 & One) == One))
           {
               MethodEvent1()
           }
           If ((Local0 & 0x2) == 0x2)
           {
               MethodEvent2()
           }
           ...
       }
   }

GED IO interface (4 byte access)
--------------------------------
**read access:**

::

   [0x0-0x3] Event selector bit field (32 bit) set by QEMU.

    bits:
       0: Memory hotplug event
       1: System power down event
       2: NVDIMM hotplug event
       3: CPU hotplug event
    4-31: Reserved

**write_access:**

Nothing is expected to be written into GED IO memory

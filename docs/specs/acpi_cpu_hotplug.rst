QEMU<->ACPI BIOS CPU hotplug interface
======================================

QEMU supports CPU hotplug via ACPI. This document
describes the interface between QEMU and the ACPI BIOS.

ACPI BIOS GPE.2 handler is dedicated for notifying OS about CPU hot-add
and hot-remove events.


Legacy ACPI CPU hotplug interface registers
-------------------------------------------

CPU present bitmap for:

- ICH9-LPC (IO port 0x0cd8-0xcf7, 1-byte access)
- PIIX-PM  (IO port 0xaf00-0xaf1f, 1-byte access)
- One bit per CPU. Bit position reflects corresponding CPU APIC ID. Read-only.
- The first DWORD in bitmap is used in write mode to switch from legacy
  to modern CPU hotplug interface, write 0 into it to do switch.

QEMU sets corresponding CPU bit on hot-add event and issues SCI
with GPE.2 event set. CPU present map is read by ACPI BIOS GPE.2 handler
to notify OS about CPU hot-add events. CPU hot-remove isn't supported.


Modern ACPI CPU hotplug interface registers
-------------------------------------------

Register block base address:

- ICH9-LPC IO port 0x0cd8
- PIIX-PM  IO port 0xaf00

Register block size:

- ACPI_CPU_HOTPLUG_REG_LEN = 12

All accesses to registers described below, imply little-endian byte order.

Reserved registers behavior:

- write accesses are ignored
- read accesses return all bits set to 0.

The last stored value in 'CPU selector' must refer to a possible CPU, otherwise

- reads from any register return 0
- writes to any other register are ignored until valid value is stored into it

On QEMU start, 'CPU selector' is initialized to a valid value, on reset it
keeps the current value.

Read access behavior
^^^^^^^^^^^^^^^^^^^^

offset [0x0-0x3]
  Command data 2: (DWORD access)

  If value last stored in 'Command field' is:

  0:
    reads as 0x0
  3:
    upper 32 bits of architecture specific CPU ID value
  other values:
    reserved

offset [0x4]
  CPU device status fields: (1 byte access)

  bits:

  0:
    Device is enabled and may be used by guest
  1:
    Device insert event, used to distinguish device for which
    no device check event to OSPM was issued.
    It's valid only when bit 0 is set.
  2:
    Device remove event, used to distinguish device for which
    no device eject request to OSPM was issued. Firmware must
    ignore this bit.
  3:
    reserved and should be ignored by OSPM
  4:
    if set to 1, OSPM requests firmware to perform device eject.
  5-7:
    reserved and should be ignored by OSPM

offset [0x5-0x7]
  reserved

offset [0x8]
  Command data: (DWORD access)

  If value last stored in 'Command field' is one of:

  0:
    contains 'CPU selector' value of a CPU with pending event[s]
  3:
    lower 32 bits of architecture specific CPU ID value
    (in x86 case: APIC ID)
  otherwise:
    contains 0

Write access behavior
^^^^^^^^^^^^^^^^^^^^^

offset [0x0-0x3]
  CPU selector: (DWORD access)

  Selects active CPU device. All following accesses to other
  registers will read/store data from/to selected CPU.
  Valid values: [0 .. max_cpus)

offset [0x4]
  CPU device control fields: (1 byte access)

  bits:

  0:
    reserved, OSPM must clear it before writing to register.
  1:
    if set to 1 clears device insert event, set by OSPM
    after it has emitted device check event for the
    selected CPU device
  2:
    if set to 1 clears device remove event, set by OSPM
    after it has emitted device eject request for the
    selected CPU device.
  3:
    if set to 1 initiates device eject, set by OSPM when it
    triggers CPU device removal and calls _EJ0 method or by firmware
    when bit #4 is set. In case bit #4 were set, it's cleared as
    part of device eject.
  4:
    if set to 1, OSPM hands over device eject to firmware.
    Firmware shall issue device eject request as described above
    (bit #3) and OSPM should not touch device eject bit (#3) in case
    it's asked firmware to perform CPU device eject.
  5-7:
    reserved, OSPM must clear them before writing to register

offset[0x5]
  Command field: (1 byte access)

  value:

  0:
    selects a CPU device with inserting/removing events and
    following reads from 'Command data' register return
    selected CPU ('CPU selector' value).
    If no CPU with events found, the current 'CPU selector' doesn't
    change and corresponding insert/remove event flags are not modified.

  1:
    following writes to 'Command data' register set OST event
    register in QEMU
  2:
    following writes to 'Command data' register set OST status
    register in QEMU
  3:
    following reads from 'Command data' and 'Command data 2' return
    architecture specific CPU ID value for currently selected CPU.
  other values:
    reserved

offset [0x6-0x7]
  reserved

offset [0x8]
  Command data: (DWORD access)

  If last stored 'Command field' value is:

  1:
    stores value into OST event register
  2:
    stores value into OST status register, triggers
    ACPI_DEVICE_OST QMP event from QEMU to external applications
    with current values of OST event and status registers.
  other values:
    reserved

Typical usecases
----------------

(x86) Detecting and enabling modern CPU hotplug interface
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

QEMU starts with legacy CPU hotplug interface enabled. Detecting and
switching to modern interface is based on the 2 legacy CPU hotplug features:

#. Writes into CPU bitmap are ignored.
#. CPU bitmap always has bit #0 set, corresponding to boot CPU.

Use following steps to detect and enable modern CPU hotplug interface:

#. Store 0x0 to the 'CPU selector' register, attempting to switch to modern mode
#. Store 0x0 to the 'CPU selector' register, to ensure valid selector value
#. Store 0x0 to the 'Command field' register
#. Read the 'Command data 2' register.
   If read value is 0x0, the modern interface is enabled.
   Otherwise legacy or no CPU hotplug interface available

Get a cpu with pending event
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

#. Store 0x0 to the 'CPU selector' register.
#. Store 0x0 to the 'Command field' register.
#. Read the 'CPU device status fields' register.
#. If both bit #1 and bit #2 are clear in the value read, there is no CPU
   with a pending event and selected CPU remains unchanged.
#. Otherwise, read the 'Command data' register. The value read is the
   selector of the CPU with the pending event (which is already selected).

Enumerate CPUs present/non present CPUs
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

#. Set the present CPU count to 0.
#. Set the iterator to 0.
#. Store 0x0 to the 'CPU selector' register, to ensure that it's in
   a valid state and that access to other registers won't be ignored.
#. Store 0x0 to the 'Command field' register to make 'Command data'
   register return 'CPU selector' value of selected CPU
#. Read the 'CPU device status fields' register.
#. If bit #0 is set, increment the present CPU count.
#. Increment the iterator.
#. Store the iterator to the 'CPU selector' register.
#. Read the 'Command data' register.
#. If the value read is not zero, goto 05.
#. Otherwise store 0x0 to the 'CPU selector' register, to put it
   into a valid state and exit.
   The iterator at this point equals "max_cpus".

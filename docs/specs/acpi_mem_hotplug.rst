QEMU<->ACPI BIOS memory hotplug interface
=========================================

ACPI BIOS GPE.3 handler is dedicated for notifying OS about memory hot-add
and hot-remove events.

Memory hot-plug interface (IO port 0xa00-0xa17, 1-4 byte access)
----------------------------------------------------------------

Read access behavior
^^^^^^^^^^^^^^^^^^^^

[0x0-0x3]
  Lo part of memory device phys address
[0x4-0x7]
  Hi part of memory device phys address
[0x8-0xb]
  Lo part of memory device size in bytes
[0xc-0xf]
  Hi part of memory device size in bytes
[0x10-0x13]
  Memory device proximity domain
[0x14]
  Memory device status fields

  bits:

  0:
    Device is enabled and may be used by guest
  1:
    Device insert event, used to distinguish device for which
    no device check event to OSPM was issued.
    It's valid only when bit 1 is set.
  2:
    Device remove event, used to distinguish device for which
    no device eject request to OSPM was issued.
  3-7:
    reserved and should be ignored by OSPM

[0x15-0x17]
  reserved

Write access behavior
^^^^^^^^^^^^^^^^^^^^^


[0x0-0x3]
  Memory device slot selector, selects active memory device.
  All following accesses to other registers in 0xa00-0xa17
  region will read/store data from/to selected memory device.
[0x4-0x7]
  OST event code reported by OSPM
[0x8-0xb]
  OST status code reported by OSPM
[0xc-0x13]
  reserved, writes into it are ignored
[0x14]
  Memory device control fields

  bits:

  0:
    reserved, OSPM must clear it before writing to register.
    Due to BUG in versions prior 2.4 that field isn't cleared
    when other fields are written. Keep it reserved and don't
    try to reuse it.
  1:
    if set to 1 clears device insert event, set by OSPM
    after it has emitted device check event for the
    selected memory device
  2:
    if set to 1 clears device remove event, set by OSPM
    after it has emitted device eject request for the
    selected memory device
  3:
    if set to 1 initiates device eject, set by OSPM when it
    triggers memory device removal and calls _EJ0 method
  4-7:
    reserved, OSPM must clear them before writing to register

Selecting memory device slot beyond present range has no effect on platform:

- write accesses to memory hot-plug registers not documented above are ignored
- read accesses to memory hot-plug registers not documented above return
  all bits set to 1.

Memory hot remove process diagram
---------------------------------

::

   +-------------+     +-----------------------+      +------------------+
   |  1. QEMU    |     | 2. QEMU               |      |3. QEMU           |
   |  device_del +---->+ device unplug request +----->+Send SCI to guest,|
   |             |     |         cb            |      |return control to |
   |             |     |                       |      |management        |
   +-------------+     +-----------------------+      +------------------+

   +---------------------------------------------------------------------+

   +---------------------+              +-------------------------+
   | OSPM:               | remove event | OSPM:                   |
   | send Eject Request, |              | Scan memory devices     |
   | clear remove event  +<-------------+ for event flags         |
   |                     |              |                         |
   +---------------------+              +-------------------------+
             |
             |
   +---------v--------+            +-----------------------+
   | Guest OS:        |  success   | OSPM:                 |
   | process Ejection +----------->+ Execute _EJ0 method,  |
   | request          |            | set eject bit in flags|
   +------------------+            +-----------------------+
             |failure                         |
             v                                v
   +------------------------+      +-----------------------+
   | OSPM:                  |      | QEMU:                 |
   | set OST event & status |      | call device unplug cb |
   | fields                 |      |                       |
   +------------------------+      +-----------------------+
            |                                  |
            v                                  v
   +------------------+              +-------------------+
   |QEMU:             |              |QEMU:              |
   |Send OST QMP event|              |Send device deleted|
   |                  |              |QMP event          |
   +------------------+              |                   |
                                     +-------------------+

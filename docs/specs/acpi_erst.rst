ACPI ERST DEVICE
================

The ACPI ERST device is utilized to support the ACPI Error Record
Serialization Table, ERST, functionality. This feature is designed for
storing error records in persistent storage for future reference
and/or debugging.

The ACPI specification[1], in Chapter "ACPI Platform Error Interfaces
(APEI)", and specifically subsection "Error Serialization", outlines a
method for storing error records into persistent storage.

The format of error records is described in the UEFI specification[2],
in Appendix N "Common Platform Error Record".

While the ACPI specification allows for an NVRAM "mode" (see
GET_ERROR_LOG_ADDRESS_RANGE_ATTRIBUTES) where non-volatile RAM is
directly exposed for direct access by the OS/guest, this device
implements the non-NVRAM "mode". This non-NVRAM "mode" is what is
implemented by most BIOS (since flash memory requires programming
operations in order to update its contents). Furthermore, as of the
time of this writing, Linux only supports the non-NVRAM "mode".


Background/Motivation
---------------------

Linux uses the persistent storage filesystem, pstore, to record
information (eg. dmesg tail) upon panics and shutdowns.  Pstore is
independent of, and runs before, kdump.  In certain scenarios (ie.
hosts/guests with root filesystems on NFS/iSCSI where networking
software and/or hardware fails, and thus kdump fails), pstore may
contain information available for post-mortem debugging.

Two common storage backends for the pstore filesystem are ACPI ERST
and UEFI. Most BIOS implement ACPI ERST. UEFI is not utilized in all
guests. With QEMU supporting ACPI ERST, it becomes a viable pstore
storage backend for virtual machines (as it is now for bare metal
machines).

Enabling support for ACPI ERST facilitates a consistent method to
capture kernel panic information in a wide range of guests: from
resource-constrained microvms to very large guests, and in particular,
in direct-boot environments (which would lack UEFI run-time services).

Note that Microsoft Windows also utilizes the ACPI ERST for certain
crash information, if available[3].


Configuration|Usage
-------------------

To use ACPI ERST, a memory-backend-file object and acpi-erst device
can be created, for example:

 qemu ...
 -object memory-backend-file,id=erstnvram,mem-path=acpi-erst.backing,size=0x10000,share=on \
 -device acpi-erst,memdev=erstnvram

For proper operation, the ACPI ERST device needs a memory-backend-file
object with the following parameters:

 - id: The id of the memory-backend-file object is used to associate
   this memory with the acpi-erst device.
 - size: The size of the ACPI ERST backing storage. This parameter is
   required.
 - mem-path: The location of the ACPI ERST backing storage file. This
   parameter is also required.
 - share: The share=on parameter is required so that updates to the
   ERST backing store are written to the file.

and ERST device:

 - memdev: Is the object id of the memory-backend-file.
 - record_size: Specifies the size of the records (or slots) in the
   backend storage. Must be a power of two value greater than or
   equal to 4096 (PAGE_SIZE).


PCI Interface
-------------

The ERST device is a PCI device with two BARs, one for accessing the
programming registers, and the other for accessing the record exchange
buffer.

BAR0 contains the programming interface consisting of ACTION and VALUE
64-bit registers.  All ERST actions/operations/side effects happen on
the write to the ACTION, by design. Any data needed by the action must
be placed into VALUE prior to writing ACTION.  Reading the VALUE
simply returns the register contents, which can be updated by a
previous ACTION.

BAR1 contains the 8KiB record exchange buffer, which is the
implemented maximum record size.


Backend Storage Format
----------------------

The backend storage is divided into fixed size "slots", 8KiB in
length, with each slot storing a single record.  Not all slots need to
be occupied, and they need not be occupied in a contiguous fashion.
The ability to clear/erase specific records allows for the formation
of unoccupied slots.

Slot 0 contains a backend storage header that identifies the contents
as ERST and also facilitates efficient access to the records.
Depending upon the size of the backend storage, additional slots will
be designated to be a part of the slot 0 header. For example, at 8KiB,
the slot 0 header can accommodate 1021 records. Thus a storage size
of 8MiB (8KiB * 1024) requires an additional slot for use by the
header. In this scenario, slot 0 and slot 1 form the backend storage
header, and records can be stored starting at slot 2.

Below is an example layout of the backend storage format (for storage
size less than 8MiB). The size of the storage is a multiple of 8KiB,
and contains N number of slots to store records. The example below
shows two records (in CPER format) in the backend storage, while the
remaining slots are empty/available.

::

 Slot   Record
        <------------------ 8KiB -------------------->
        +--------------------------------------------+
    0   | storage header                             |
        +--------------------------------------------+
    1   | empty/available                            |
        +--------------------------------------------+
    2   | CPER                                       |
        +--------------------------------------------+
    3   | CPER                                       |
        +--------------------------------------------+
  ...   |                                            |
        +--------------------------------------------+
    N   | empty/available                            |
        +--------------------------------------------+

The storage header consists of some basic information and an array
of CPER record_id's to efficiently access records in the backend
storage.

All fields in the header are stored in little endian format.

::

  +--------------------------------------------+
  | magic                                      | 0x0000
  +--------------------------------------------+
  | record_offset        | record_size         | 0x0008
  +--------------------------------------------+
  | record_count         | reserved | version  | 0x0010
  +--------------------------------------------+
  | record_id[0]                               | 0x0018
  +--------------------------------------------+
  | record_id[1]                               | 0x0020
  +--------------------------------------------+
  | record_id[...]                             |
  +--------------------------------------------+
  | record_id[N]                               | 0x1FF8
  +--------------------------------------------+

The 'magic' field contains the value 0x524F545354535245.

The 'record_size' field contains the value 0x2000, 8KiB.

The 'record_offset' field points to the first record_id in the array,
0x0018.

The 'version' field contains 0x0100, the first version.

The 'record_count' field contains the number of valid records in the
backend storage.

The 'record_id' array fields are the 64-bit record identifiers of the
CPER record in the corresponding slot. Stated differently, the
location of a CPER record_id in the record_id[] array provides the
slot index for the corresponding record in the backend storage.

Note that, for example, with a backend storage less than 8MiB, slot 0
contains the header, so the record_id[0] will never contain a valid
CPER record_id. Instead slot 1 is the first available slot and thus
record_id_[1] may contain a CPER.

A 'record_id' of all 0s or all 1s indicates an invalid record (ie. the
slot is available).


References
----------

[1] "Advanced Configuration and Power Interface Specification",
    version 4.0, June 2009.

[2] "Unified Extensible Firmware Interface Specification",
    version 2.1, October 2008.

[3] "Windows Hardware Error Architecture", specifically
    "Error Record Persistence Mechanism".

====================================================
QEMU/Guest Firmware Interface for AMD SEV and SEV-ES
====================================================

Overview
========

The guest firmware image (OVMF) may contain some configuration entries
which are used by QEMU before the guest launches.  These are listed in a
GUIDed table at a known location in the firmware image.  QEMU parses
this table when it loads the firmware image into memory, and then QEMU
reads individual entries when their values are needed.

Though nothing in the table structure is SEV-specific, currently all the
entries in the table are related to SEV and SEV-ES features.


Table parsing in QEMU
---------------------

The table is parsed from the footer: first the presence of the table
footer GUID (96b582de-1fb2-45f7-baea-a366c55a082d) at 0xffffffd0 is
verified.  If that is found, two bytes at 0xffffffce are the entire
table length.

Then the table is scanned backwards looking for the specific entry GUID.

QEMU files related to parsing and scanning the OVMF table:
 - ``hw/i386/pc_sysfw_ovmf.c``

The edk2 firmware code that constructs this structure is in the
`OVMF Reset Vector file`_.


Table memory layout
-------------------

+------------+--------+-----------------------------------------+
|    GPA     | Length |               Description               |
+============+========+=========================================+
| 0xffffff80 | 4      | Zero padding                            |
+------------+--------+-----------------------------------------+
| 0xffffff84 | 4      | SEV hashes table base address           |
+------------+--------+-----------------------------------------+
| 0xffffff88 | 4      | SEV hashes table size (=0x400)          |
+------------+--------+-----------------------------------------+
| 0xffffff8c | 2      | SEV hashes table entry length (=0x1a)   |
+------------+--------+-----------------------------------------+
| 0xffffff8e | 16     | SEV hashes table GUID:                  |
|            |        | 7255371f-3a3b-4b04-927b-1da6efa8d454    |
+------------+--------+-----------------------------------------+
| 0xffffff9e | 4      | SEV secret block base address           |
+------------+--------+-----------------------------------------+
| 0xffffffa2 | 4      | SEV secret block size (=0xc00)          |
+------------+--------+-----------------------------------------+
| 0xffffffa6 | 2      | SEV secret block entry length (=0x1a)   |
+------------+--------+-----------------------------------------+
| 0xffffffa8 | 16     | SEV secret block GUID:                  |
|            |        | 4c2eb361-7d9b-4cc3-8081-127c90d3d294    |
+------------+--------+-----------------------------------------+
| 0xffffffb8 | 4      | SEV-ES AP reset RIP                     |
+------------+--------+-----------------------------------------+
| 0xffffffbc | 2      | SEV-ES reset block entry length (=0x16) |
+------------+--------+-----------------------------------------+
| 0xffffffbe | 16     | SEV-ES reset block entry GUID:          |
|            |        | 00f771de-1a7e-4fcb-890e-68c77e2fb44e    |
+------------+--------+-----------------------------------------+
| 0xffffffce | 2      | Length of entire table including table  |
|            |        | footer GUID and length (=0x72)          |
+------------+--------+-----------------------------------------+
| 0xffffffd0 | 16     | OVMF GUIDed table footer GUID:          |
|            |        | 96b582de-1fb2-45f7-baea-a366c55a082d    |
+------------+--------+-----------------------------------------+
| 0xffffffe0 | 8      | Application processor entry point code  |
+------------+--------+-----------------------------------------+
| 0xffffffe8 | 8      | "\0\0\0\0VTF\0"                         |
+------------+--------+-----------------------------------------+
| 0xfffffff0 | 16     | Reset vector code                       |
+------------+--------+-----------------------------------------+


Table entries description
=========================

SEV-ES reset block
------------------

Entry GUID: 00f771de-1a7e-4fcb-890e-68c77e2fb44e

For the initial boot of an AP under SEV-ES, the "reset" RIP must be
programmed to the RAM area defined by this entry.  The entry's format
is:

* IP value [0:15]
* CS segment base [31:16]

A hypervisor reads the CS segment base and IP value.  The CS segment
base value represents the high order 16-bits of the CS segment base, so
the hypervisor must left shift the value of the CS segment base by 16
bits to form the full CS segment base for the CS segment register. It
would then program the EIP register with the IP value as read.


SEV secret block
----------------

Entry GUID: 4c2eb361-7d9b-4cc3-8081-127c90d3d294

This describes the guest RAM area where the hypervisor should inject the
Guest Owner secret (using SEV_LAUNCH_SECRET).


SEV hashes table
----------------

Entry GUID: 7255371f-3a3b-4b04-927b-1da6efa8d454

This describes the guest RAM area where the hypervisor should install a
table describing the hashes of certain firmware configuration device
files that would otherwise be passed in unchecked.  The current use is
for the kernel, initrd and command line values, but others may be added.


.. _OVMF Reset Vector file:
   https://github.com/tianocore/edk2/blob/master/OvmfPkg/ResetVector/Ia16/ResetVectorVtf0.asm

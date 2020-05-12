APEI tables generating and CPER record
======================================

..
   Copyright (c) 2020 HUAWEI TECHNOLOGIES CO., LTD.

   This work is licensed under the terms of the GNU GPL, version 2 or later.
   See the COPYING file in the top-level directory.

Design Details
--------------

::

         etc/acpi/tables                           etc/hardware_errors
      ====================                   ===============================
  + +--------------------------+            +----------------------------+
  | | HEST                     | +--------->|    error_block_address1    |------+
  | +--------------------------+ |          +----------------------------+      |
  | | GHES1                    | | +------->|    error_block_address2    |------+-+
  | +--------------------------+ | |        +----------------------------+      | |
  | | .................        | | |        |      ..............        |      | |
  | | error_status_address-----+-+ |        -----------------------------+      | |
  | | .................        |   |   +--->|    error_block_addressN    |------+-+---+
  | | read_ack_register--------+-+ |   |    +----------------------------+      | |   |
  | | read_ack_preserve        | +-+---+--->|     read_ack_register1     |      | |   |
  | | read_ack_write           |   |   |    +----------------------------+      | |   |
  + +--------------------------+   | +-+--->|     read_ack_register2     |      | |   |
  | | GHES2                    |   | | |    +----------------------------+      | |   |
  + +--------------------------+   | | |    |       .............        |      | |   |
  | | .................        |   | | |    +----------------------------+      | |   |
  | | error_status_address-----+---+ | | +->|     read_ack_registerN     |      | |   |
  | | .................        |     | | |  +----------------------------+      | |   |
  | | read_ack_register--------+-----+ | |  |Generic Error Status Block 1|<-----+ |   |
  | | read_ack_preserve        |       | |  |-+------------------------+-+        |   |
  | | read_ack_write           |       | |  | |          CPER          | |        |   |
  + +--------------------------|       | |  | |          CPER          | |        |   |
  | | ...............          |       | |  | |          ....          | |        |   |
  + +--------------------------+       | |  | |          CPER          | |        |   |
  | | GHESN                    |       | |  |-+------------------------+-|        |   |
  + +--------------------------+       | |  |Generic Error Status Block 2|<-------+   |
  | | .................        |       | |  |-+------------------------+-+            |
  | | error_status_address-----+-------+ |  | |           CPER         | |            |
  | | .................        |         |  | |           CPER         | |            |
  | | read_ack_register--------+---------+  | |           ....         | |            |
  | | read_ack_preserve        |            | |           CPER         | |            |
  | | read_ack_write           |            +-+------------------------+-+            |
  + +--------------------------+            |         ..........         |            |
                                            |----------------------------+            |
                                            |Generic Error Status Block N |<----------+
                                            |-+-------------------------+-+
                                            | |          CPER           | |
                                            | |          CPER           | |
                                            | |          ....           | |
                                            | |          CPER           | |
                                            +-+-------------------------+-+


(1) QEMU generates the ACPI HEST table. This table goes in the current
    "etc/acpi/tables" fw_cfg blob. Each error source has different
    notification types.

(2) A new fw_cfg blob called "etc/hardware_errors" is introduced. QEMU
    also needs to populate this blob. The "etc/hardware_errors" fw_cfg blob
    contains an address registers table and an Error Status Data Block table.

(3) The address registers table contains N Error Block Address entries
    and N Read Ack Register entries. The size for each entry is 8-byte.
    The Error Status Data Block table contains N Error Status Data Block
    entries. The size for each entry is 4096(0x1000) bytes. The total size
    for the "etc/hardware_errors" fw_cfg blob is (N * 8 * 2 + N * 4096) bytes.
    N is the number of the kinds of hardware error sources.

(4) QEMU generates the ACPI linker/loader script for the firmware. The
    firmware pre-allocates memory for "etc/acpi/tables", "etc/hardware_errors"
    and copies blob contents there.

(5) QEMU generates N ADD_POINTER commands, which patch addresses in the
    "error_status_address" fields of the HEST table with a pointer to the
    corresponding "address registers" in the "etc/hardware_errors" blob.

(6) QEMU generates N ADD_POINTER commands, which patch addresses in the
    "read_ack_register" fields of the HEST table with a pointer to the
    corresponding "read_ack_register" within the "etc/hardware_errors" blob.

(7) QEMU generates N ADD_POINTER commands for the firmware, which patch
    addresses in the "error_block_address" fields with a pointer to the
    respective "Error Status Data Block" in the "etc/hardware_errors" blob.

(8) QEMU defines a third and write-only fw_cfg blob which is called
    "etc/hardware_errors_addr". Through that blob, the firmware can send back
    the guest-side allocation addresses to QEMU. The "etc/hardware_errors_addr"
    blob contains a 8-byte entry. QEMU generates a single WRITE_POINTER command
    for the firmware. The firmware will write back the start address of
    "etc/hardware_errors" blob to the fw_cfg file "etc/hardware_errors_addr".

(9) When QEMU gets a SIGBUS from the kernel, QEMU writes CPER into corresponding
    "Error Status Data Block", guest memory, and then injects platform specific
    interrupt (in case of arm/virt machine it's Synchronous External Abort) as a
    notification which is necessary for notifying the guest.

(10) This notification (in virtual hardware) will be handled by the guest
     kernel, on receiving notification, guest APEI driver could read the CPER error
     and take appropriate action.

(11) kvm_arch_on_sigbus_vcpu() uses source_id as index in "etc/hardware_errors" to
     find out "Error Status Data Block" entry corresponding to error source. So supported
     source_id values should be assigned here and not be changed afterwards to make sure
     that guest will write error into expected "Error Status Data Block" even if guest was
     migrated to a newer QEMU.

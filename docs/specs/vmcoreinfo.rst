=================
VMCoreInfo device
=================

The ``-device vmcoreinfo`` will create a ``fw_cfg`` entry for a guest to
store dump details.

``etc/vmcoreinfo``
==================

A guest may use this ``fw_cfg`` entry to add information details to QEMU
dumps.

The entry of 16 bytes has the following layout, in little-endian::

    #define VMCOREINFO_FORMAT_NONE 0x0
    #define VMCOREINFO_FORMAT_ELF 0x1

    struct FWCfgVMCoreInfo {
        uint16_t host_format;  /* formats host supports */
        uint16_t guest_format; /* format guest supplies */
        uint32_t size;         /* size of vmcoreinfo region */
        uint64_t paddr;        /* physical address of vmcoreinfo region */
    };

Only full write (of 16 bytes) are considered valid for further
processing of entry values.

A write of 0 in ``guest_format`` will disable further processing of
vmcoreinfo entry values & content.

You may write a ``guest_format`` that is not supported by the host, in
which case the entry data can be ignored by QEMU (but you may still
access it through a debugger, via ``vmcoreinfo_realize::vmcoreinfo_state``).

Format & content
================

As of QEMU 2.11, only ``VMCOREINFO_FORMAT_ELF`` is supported.

The entry gives location and size of an ELF note that is appended in
qemu dumps.

The note format/class must be of the target bitness and the size must
be less than 1Mb.

If the ELF note name is ``VMCOREINFO``, it is expected to be the Linux
vmcoreinfo note (see `the kernel documentation for its format
<https://www.kernel.org/doc/Documentation/ABI/testing/sysfs-kernel-vmcoreinfo>`_).
In this case, qemu dump code will read the content
as a key=value text file, looking for ``NUMBER(phys_base)`` key
value. The value is expected to be more accurate than architecture
guess of the value. This is useful for KASLR-enabled guest with
ancient tools not handling the ``VMCOREINFO`` note.

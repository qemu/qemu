===========================================
QEMU Firmware Configuration (fw_cfg) Device
===========================================

Guest-side Hardware Interface
=============================

This hardware interface allows the guest to retrieve various data items
(blobs) that can influence how the firmware configures itself, or may
contain tables to be installed for the guest OS. Examples include device
boot order, ACPI and SMBIOS tables, virtual machine UUID, SMP and NUMA
information, kernel/initrd images for direct (Linux) kernel booting, etc.

Selector (Control) Register
---------------------------

* Write only
* Location: platform dependent (IOport or MMIO)
* Width: 16-bit
* Endianness: little-endian (if IOport), or big-endian (if MMIO)

A write to this register sets the index of a firmware configuration
item which can subsequently be accessed via the data register.

Setting the selector register will cause the data offset to be set
to zero. The data offset impacts which data is accessed via the data
register, and is explained below.

Bit14 of the selector register indicates whether the configuration
setting is being written. A value of 0 means the item is only being
read, and all write access to the data port will be ignored. A value
of 1 means the item's data can be overwritten by writes to the data
register. In other words, configuration write mode is enabled when
the selector value is between 0x4000-0x7fff or 0xc000-0xffff.

.. NOTE::
      As of QEMU v2.4, writes to the fw_cfg data register are no
      longer supported, and will be ignored (treated as no-ops)!

.. NOTE::
      As of QEMU v2.9, writes are reinstated, but only through the DMA
      interface (see below). Furthermore, writeability of any specific item is
      governed independently of Bit14 in the selector key value.

Bit15 of the selector register indicates whether the configuration
setting is architecture specific. A value of 0 means the item is a
generic configuration item. A value of 1 means the item is specific
to a particular architecture. In other words, generic configuration
items are accessed with a selector value between 0x0000-0x7fff, and
architecture specific configuration items are accessed with a selector
value between 0x8000-0xffff.

Data Register
-------------

* Read/Write (writes ignored as of QEMU v2.4, but see the DMA interface)
* Location: platform dependent (IOport\ [#placement]_ or MMIO)
* Width: 8-bit (if IOport), 8/16/32/64-bit (if MMIO)
* Endianness: string-preserving

.. [#placement]
    On platforms where the data register is exposed as an IOport, its
    port number will always be one greater than the port number of the
    selector register. In other words, the two ports overlap, and can not
    be mapped separately.

The data register allows access to an array of bytes for each firmware
configuration data item. The specific item is selected by writing to
the selector register, as described above.

Initially following a write to the selector register, the data offset
will be set to zero. Each successful access to the data register will
increment the data offset by the appropriate access width.

Each firmware configuration item has a maximum length of data
associated with the item. After the data offset has passed the
end of this maximum data length, then any reads will return a data
value of 0x00, and all writes will be ignored.

An N-byte wide read of the data register will return the next available
N bytes of the selected firmware configuration item, as a substring, in
increasing address order, similar to memcpy().

Register Locations
------------------

x86, x86_64
    * Selector Register IOport: 0x510
    * Data Register IOport:     0x511
    * DMA Address IOport:       0x514

Arm
    * Selector Register address: Base + 8 (2 bytes)
    * Data Register address:     Base + 0 (8 bytes)
    * DMA Address address:       Base + 16 (8 bytes)

ACPI Interface
--------------

The fw_cfg device is defined with ACPI ID ``QEMU0002``. Since we expect
ACPI tables to be passed into the guest through the fw_cfg device itself,
the guest-side firmware can not use ACPI to find fw_cfg. However, once the
firmware is finished setting up ACPI tables and hands control over to the
guest kernel, the latter can use the fw_cfg ACPI node for a more accurate
inventory of in-use IOport or MMIO regions.

Firmware Configuration Items
----------------------------

Signature (Key 0x0000, ``FW_CFG_SIGNATURE``)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The presence of the fw_cfg selector and data registers can be verified
by selecting the "signature" item using key 0x0000 (``FW_CFG_SIGNATURE``),
and reading four bytes from the data register. If the fw_cfg device is
present, the four bytes read will contain the characters ``QEMU``.

If the DMA interface is available, then reading the DMA Address
Register returns 0x51454d5520434647 (``QEMU CFG`` in big-endian format).

Revision / feature bitmap (Key 0x0001, ``FW_CFG_ID``)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A 32-bit little-endian unsigned int, this item is used to check for enabled
features.

- Bit 0: traditional interface. Always set.
- Bit 1: DMA interface.

File Directory (Key 0x0019, ``FW_CFG_FILE_DIR``)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. highlight:: c

Firmware configuration items stored at selector keys 0x0020 or higher
(``FW_CFG_FILE_FIRST`` or higher) have an associated entry in a directory
structure, which makes it easier for guest-side firmware to identify
and retrieve them. The format of this file directory (from ``fw_cfg.h`` in
the QEMU source tree) is shown here, slightly annotated for clarity::

    struct FWCfgFiles {		/* the entire file directory fw_cfg item */
        uint32_t count;		/* number of entries, in big-endian format */
        struct FWCfgFile f[];	/* array of file entries, see below */
    };

    struct FWCfgFile {		/* an individual file entry, 64 bytes total */
        uint32_t size;		/* size of referenced fw_cfg item, big-endian */
        uint16_t select;	/* selector key of fw_cfg item, big-endian */
        uint16_t reserved;
        char name[56];		/* fw_cfg item name, NUL-terminated ascii */
    };

All Other Data Items
~~~~~~~~~~~~~~~~~~~~

Please consult the QEMU source for the most up-to-date and authoritative list
of selector keys and their respective items' purpose, format and writeability.

Ranges
~~~~~~

Theoretically, there may be up to 0x4000 generic firmware configuration
items, and up to 0x4000 architecturally specific ones.

===============  ===========
Selector Reg.    Range Usage
===============  ===========
0x0000 - 0x3fff  Generic (0x0000 - 0x3fff, generally RO, possibly RW through
                 the DMA interface in QEMU v2.9+)
0x4000 - 0x7fff  Generic (0x0000 - 0x3fff, RW, ignored in QEMU v2.4+)
0x8000 - 0xbfff  Arch. Specific (0x0000 - 0x3fff, generally RO, possibly RW
                 through the DMA interface in QEMU v2.9+)
0xc000 - 0xffff  Arch. Specific (0x0000 - 0x3fff, RW, ignored in v2.4+)
===============  ===========

In practice, the number of allowed firmware configuration items depends on the
machine type/version.

Guest-side DMA Interface
========================

If bit 1 of the feature bitmap is set, the DMA interface is present. This does
not replace the existing fw_cfg interface, it is an add-on. This interface
can be used through the 64-bit wide address register.

The address register is in big-endian format. The value for the register is 0
at startup and after an operation. A write to the least significant half (at
offset 4) triggers an operation. This means that operations with 32-bit
addresses can be triggered with just one write, whereas operations with
64-bit addresses can be triggered with one 64-bit write or two 32-bit writes,
starting with the most significant half (at offset 0).

In this register, the physical address of a ``FWCfgDmaAccess`` structure in RAM
should be written. This is the format of the ``FWCfgDmaAccess`` structure::

    typedef struct FWCfgDmaAccess {
        uint32_t control;
        uint32_t length;
        uint64_t address;
    } FWCfgDmaAccess;

The fields of the structure are in big endian mode, and the field at the lowest
address is the ``control`` field.

The ``control`` field has the following bits:

- Bit 0: Error
- Bit 1: Read
- Bit 2: Skip
- Bit 3: Select. The upper 16 bits are the selected index.
- Bit 4: Write

When an operation is triggered, if the ``control`` field has bit 3 set, the
upper 16 bits are interpreted as an index of a firmware configuration item.
This has the same effect as writing the selector register.

If the ``control`` field has bit 1 set, a read operation will be performed.
``length`` bytes for the current selector and offset will be copied into the
physical RAM address specified by the ``address`` field.

If the ``control`` field has bit 4 set (and not bit 1), a write operation will be
performed. ``length`` bytes will be copied from the physical RAM address
specified by the ``address`` field to the current selector and offset. QEMU
prevents starting or finishing the write beyond the end of the item associated
with the current selector (i.e., the item cannot be resized). Truncated writes
are dropped entirely. Writes to read-only items are also rejected. All of these
write errors set bit 0 (the error bit) in the ``control`` field.

If the ``control`` field has bit 2 set (and neither bit 1 nor bit 4), a skip
operation will be performed. The offset for the current selector will be
advanced ``length`` bytes.

To check the result, read the ``control`` field:

Error bit set
    Something went wrong.
All bits cleared
    Transfer finished successfully.
Otherwise
    Transfer still in progress
    (doesn't happen today due to implementation not being async,
    but may in the future).

Externally Provided Items
=========================

Since v2.4, "file" fw_cfg items (i.e., items with selector keys above
``FW_CFG_FILE_FIRST``, and with a corresponding entry in the fw_cfg file
directory structure) may be inserted via the QEMU command line, using
the following syntax::

    -fw_cfg [name=]<item_name>,file=<path>

Or::

    -fw_cfg [name=]<item_name>,string=<string>

Since v5.1, QEMU allows some objects to generate fw_cfg-specific content,
the content is then associated with a "file" item using the 'gen_id' option
in the command line, using the following syntax::

    -object <generator-type>,id=<generated_id>,[generator-specific-options] \
    -fw_cfg [name=]<item_name>,gen_id=<generated_id>

See QEMU man page for more documentation.

Using item_name with plain ASCII characters only is recommended.

Item names beginning with ``opt/`` are reserved for users.  QEMU will
never create entries with such names unless explicitly ordered by the
user.

To avoid clashes among different users, it is strongly recommended
that you use names beginning with ``opt/RFQDN/``, where RFQDN is a reverse
fully qualified domain name you control.  For instance, if SeaBIOS
wanted to define additional names, the prefix ``opt/org.seabios/`` would
be appropriate.

For historical reasons, ``opt/ovmf/`` is reserved for OVMF firmware.

Prefix ``opt/org.qemu/`` is reserved for QEMU itself.

Use of names not beginning with ``opt/`` is potentially dangerous and
entirely unsupported.  QEMU will warn if you try.

Use of names not beginning with ``opt/`` is tolerated with 'gen_id' (that
is, the warning is suppressed), but you must know exactly what you're
doing.

All externally provided fw_cfg items are read-only to the guest.

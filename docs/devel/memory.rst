==============
The memory API
==============

The memory API models the memory and I/O buses and controllers of a QEMU
machine.  It attempts to allow modelling of:

- ordinary RAM
- memory-mapped I/O (MMIO)
- memory controllers that can dynamically reroute physical memory regions
  to different destinations

The memory model provides support for

- tracking RAM changes by the guest
- setting up coalesced memory for kvm
- setting up ioeventfd regions for kvm

Memory is modelled as an acyclic graph of MemoryRegion objects.  Sinks
(leaves) are RAM and MMIO regions, while other nodes represent
buses, memory controllers, and memory regions that have been rerouted.

In addition to MemoryRegion objects, the memory API provides AddressSpace
objects for every root and possibly for intermediate MemoryRegions too.
These represent memory as seen from the CPU or a device's viewpoint.

Types of regions
----------------

There are multiple types of memory regions (all represented by a single C type
MemoryRegion):

- RAM: a RAM region is simply a range of host memory that can be made available
  to the guest.
  You typically initialize these with memory_region_init_ram().  Some special
  purposes require the variants memory_region_init_resizeable_ram(),
  memory_region_init_ram_from_file(), or memory_region_init_ram_ptr().

- MMIO: a range of guest memory that is implemented by host callbacks;
  each read or write causes a callback to be called on the host.
  You initialize these with memory_region_init_io(), passing it a
  MemoryRegionOps structure describing the callbacks.

- ROM: a ROM memory region works like RAM for reads (directly accessing
  a region of host memory), and forbids writes. You initialize these with
  memory_region_init_rom().

- ROM device: a ROM device memory region works like RAM for reads
  (directly accessing a region of host memory), but like MMIO for
  writes (invoking a callback).  You initialize these with
  memory_region_init_rom_device().

- IOMMU region: an IOMMU region translates addresses of accesses made to it
  and forwards them to some other target memory region.  As the name suggests,
  these are only needed for modelling an IOMMU, not for simple devices.
  You initialize these with memory_region_init_iommu().

- container: a container simply includes other memory regions, each at
  a different offset.  Containers are useful for grouping several regions
  into one unit.  For example, a PCI BAR may be composed of a RAM region
  and an MMIO region.

  A container's subregions are usually non-overlapping.  In some cases it is
  useful to have overlapping regions; for example a memory controller that
  can overlay a subregion of RAM with MMIO or ROM, or a PCI controller
  that does not prevent card from claiming overlapping BARs.

  You initialize a pure container with memory_region_init().

- alias: a subsection of another region.  Aliases allow a region to be
  split apart into discontiguous regions.  Examples of uses are memory banks
  used when the guest address space is smaller than the amount of RAM
  addressed, or a memory controller that splits main memory to expose a "PCI
  hole".  Aliases may point to any type of region, including other aliases,
  but an alias may not point back to itself, directly or indirectly.
  You initialize these with memory_region_init_alias().

- reservation region: a reservation region is primarily for debugging.
  It claims I/O space that is not supposed to be handled by QEMU itself.
  The typical use is to track parts of the address space which will be
  handled by the host kernel when KVM is enabled.  You initialize these
  by passing a NULL callback parameter to memory_region_init_io().

It is valid to add subregions to a region which is not a pure container
(that is, to an MMIO, RAM or ROM region). This means that the region
will act like a container, except that any addresses within the container's
region which are not claimed by any subregion are handled by the
container itself (ie by its MMIO callbacks or RAM backing). However
it is generally possible to achieve the same effect with a pure container
one of whose subregions is a low priority "background" region covering
the whole address range; this is often clearer and is preferred.
Subregions cannot be added to an alias region.

Migration
---------

Where the memory region is backed by host memory (RAM, ROM and
ROM device memory region types), this host memory needs to be
copied to the destination on migration. These APIs which allocate
the host memory for you will also register the memory so it is
migrated:

- memory_region_init_ram()
- memory_region_init_rom()
- memory_region_init_rom_device()

For most devices and boards this is the correct thing. If you
have a special case where you need to manage the migration of
the backing memory yourself, you can call the functions:

- memory_region_init_ram_nomigrate()
- memory_region_init_rom_nomigrate()
- memory_region_init_rom_device_nomigrate()

which only initialize the MemoryRegion and leave handling
migration to the caller.

The functions:

- memory_region_init_resizeable_ram()
- memory_region_init_ram_from_file()
- memory_region_init_ram_from_fd()
- memory_region_init_ram_ptr()
- memory_region_init_ram_device_ptr()

are for special cases only, and so they do not automatically
register the backing memory for migration; the caller must
manage migration if necessary.

Region names
------------

Regions are assigned names by the constructor.  For most regions these are
only used for debugging purposes, but RAM regions also use the name to identify
live migration sections.  This means that RAM region names need to have ABI
stability.

Region lifecycle
----------------

A region is created by one of the memory_region_init*() functions and
attached to an object, which acts as its owner or parent.  QEMU ensures
that the owner object remains alive as long as the region is visible to
the guest, or as long as the region is in use by a virtual CPU or another
device.  For example, the owner object will not die between an
address_space_map operation and the corresponding address_space_unmap.

After creation, a region can be added to an address space or a
container with memory_region_add_subregion(), and removed using
memory_region_del_subregion().

Various region attributes (read-only, dirty logging, coalesced mmio,
ioeventfd) can be changed during the region lifecycle.  They take effect
as soon as the region is made visible.  This can be immediately, later,
or never.

Destruction of a memory region happens automatically when the owner
object dies.

If however the memory region is part of a dynamically allocated data
structure, you should call object_unparent() to destroy the memory region
before the data structure is freed.  For an example see VFIOMSIXInfo
and VFIOQuirk in hw/vfio/pci.c.

You must not destroy a memory region as long as it may be in use by a
device or CPU.  In order to do this, as a general rule do not create or
destroy memory regions dynamically during a device's lifetime, and only
call object_unparent() in the memory region owner's instance_finalize
callback.  The dynamically allocated data structure that contains the
memory region then should obviously be freed in the instance_finalize
callback as well.

If you break this rule, the following situation can happen:

- the memory region's owner had a reference taken via memory_region_ref
  (for example by address_space_map)

- the region is unparented, and has no owner anymore

- when address_space_unmap is called, the reference to the memory region's
  owner is leaked.


There is an exception to the above rule: it is okay to call
object_unparent at any time for an alias or a container region.  It is
therefore also okay to create or destroy alias and container regions
dynamically during a device's lifetime.

This exceptional usage is valid because aliases and containers only help
QEMU building the guest's memory map; they are never accessed directly.
memory_region_ref and memory_region_unref are never called on aliases
or containers, and the above situation then cannot happen.  Exploiting
this exception is rarely necessary, and therefore it is discouraged,
but nevertheless it is used in a few places.

For regions that "have no owner" (NULL is passed at creation time), the
machine object is actually used as the owner.  Since instance_finalize is
never called for the machine object, you must never call object_unparent
on regions that have no owner, unless they are aliases or containers.


Overlapping regions and priority
--------------------------------
Usually, regions may not overlap each other; a memory address decodes into
exactly one target.  In some cases it is useful to allow regions to overlap,
and sometimes to control which of an overlapping regions is visible to the
guest.  This is done with memory_region_add_subregion_overlap(), which
allows the region to overlap any other region in the same container, and
specifies a priority that allows the core to decide which of two regions at
the same address are visible (highest wins).
Priority values are signed, and the default value is zero. This means that
you can use memory_region_add_subregion_overlap() both to specify a region
that must sit 'above' any others (with a positive priority) and also a
background region that sits 'below' others (with a negative priority).

If the higher priority region in an overlap is a container or alias, then
the lower priority region will appear in any "holes" that the higher priority
region has left by not mapping subregions to that area of its address range.
(This applies recursively -- if the subregions are themselves containers or
aliases that leave holes then the lower priority region will appear in these
holes too.)

For example, suppose we have a container A of size 0x8000 with two subregions
B and C. B is a container mapped at 0x2000, size 0x4000, priority 2; C is
an MMIO region mapped at 0x0, size 0x6000, priority 1. B currently has two
of its own subregions: D of size 0x1000 at offset 0 and E of size 0x1000 at
offset 0x2000. As a diagram::

        0      1000   2000   3000   4000   5000   6000   7000   8000
        |------|------|------|------|------|------|------|------|
  A:    [                                                      ]
  C:    [CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC]
  B:                  [                          ]
  D:                  [DDDDD]
  E:                                [EEEEE]

The regions that will be seen within this address range then are::

  [CCCCCCCCCCCC][DDDDD][CCCCC][EEEEE][CCCCC]

Since B has higher priority than C, its subregions appear in the flat map
even where they overlap with C. In ranges where B has not mapped anything
C's region appears.

If B had provided its own MMIO operations (ie it was not a pure container)
then these would be used for any addresses in its range not handled by
D or E, and the result would be::

  [CCCCCCCCCCCC][DDDDD][BBBBB][EEEEE][BBBBB]

Priority values are local to a container, because the priorities of two
regions are only compared when they are both children of the same container.
This means that the device in charge of the container (typically modelling
a bus or a memory controller) can use them to manage the interaction of
its child regions without any side effects on other parts of the system.
In the example above, the priorities of D and E are unimportant because
they do not overlap each other. It is the relative priority of B and C
that causes D and E to appear on top of C: D and E's priorities are never
compared against the priority of C.

Visibility
----------
The memory core uses the following rules to select a memory region when the
guest accesses an address:

- all direct subregions of the root region are matched against the address, in
  descending priority order

  - if the address lies outside the region offset/size, the subregion is
    discarded
  - if the subregion is a leaf (RAM or MMIO), the search terminates, returning
    this leaf region
  - if the subregion is a container, the same algorithm is used within the
    subregion (after the address is adjusted by the subregion offset)
  - if the subregion is an alias, the search is continued at the alias target
    (after the address is adjusted by the subregion offset and alias offset)
  - if a recursive search within a container or alias subregion does not
    find a match (because of a "hole" in the container's coverage of its
    address range), then if this is a container with its own MMIO or RAM
    backing the search terminates, returning the container itself. Otherwise
    we continue with the next subregion in priority order

- if none of the subregions match the address then the search terminates
  with no match found

Example memory map
------------------

::

  system_memory: container@0-2^48-1
   |
   +---- lomem: alias@0-0xdfffffff ---> #ram (0-0xdfffffff)
   |
   +---- himem: alias@0x100000000-0x11fffffff ---> #ram (0xe0000000-0xffffffff)
   |
   +---- vga-window: alias@0xa0000-0xbffff ---> #pci (0xa0000-0xbffff)
   |      (prio 1)
   |
   +---- pci-hole: alias@0xe0000000-0xffffffff ---> #pci (0xe0000000-0xffffffff)

  pci (0-2^32-1)
   |
   +--- vga-area: container@0xa0000-0xbffff
   |      |
   |      +--- alias@0x00000-0x7fff  ---> #vram (0x010000-0x017fff)
   |      |
   |      +--- alias@0x08000-0xffff  ---> #vram (0x020000-0x027fff)
   |
   +---- vram: ram@0xe1000000-0xe1ffffff
   |
   +---- vga-mmio: mmio@0xe2000000-0xe200ffff

  ram: ram@0x00000000-0xffffffff

This is a (simplified) PC memory map. The 4GB RAM block is mapped into the
system address space via two aliases: "lomem" is a 1:1 mapping of the first
3.5GB; "himem" maps the last 0.5GB at address 4GB.  This leaves 0.5GB for the
so-called PCI hole, that allows a 32-bit PCI bus to exist in a system with
4GB of memory.

The memory controller diverts addresses in the range 640K-768K to the PCI
address space.  This is modelled using the "vga-window" alias, mapped at a
higher priority so it obscures the RAM at the same addresses.  The vga window
can be removed by programming the memory controller; this is modelled by
removing the alias and exposing the RAM underneath.

The pci address space is not a direct child of the system address space, since
we only want parts of it to be visible (we accomplish this using aliases).
It has two subregions: vga-area models the legacy vga window and is occupied
by two 32K memory banks pointing at two sections of the framebuffer.
In addition the vram is mapped as a BAR at address e1000000, and an additional
BAR containing MMIO registers is mapped after it.

Note that if the guest maps a BAR outside the PCI hole, it would not be
visible as the pci-hole alias clips it to a 0.5GB range.

MMIO Operations
---------------

MMIO regions are provided with ->read() and ->write() callbacks,
which are sufficient for most devices. Some devices change behaviour
based on the attributes used for the memory transaction, or need
to be able to respond that the access should provoke a bus error
rather than completing successfully; those devices can use the
->read_with_attrs() and ->write_with_attrs() callbacks instead.

In addition various constraints can be supplied to control how these
callbacks are called:

- .valid.min_access_size, .valid.max_access_size define the access sizes
  (in bytes) which the device accepts; accesses outside this range will
  have device and bus specific behaviour (ignored, or machine check)
- .valid.unaligned specifies that the *device being modelled* supports
  unaligned accesses; if false, unaligned accesses will invoke the
  appropriate bus or CPU specific behaviour.
- .impl.min_access_size, .impl.max_access_size define the access sizes
  (in bytes) supported by the *implementation*; other access sizes will be
  emulated using the ones available.  For example a 4-byte write will be
  emulated using four 1-byte writes, if .impl.max_access_size = 1.
- .impl.unaligned specifies that the *implementation* supports unaligned
  accesses; if false, unaligned accesses will be emulated by two aligned
  accesses.

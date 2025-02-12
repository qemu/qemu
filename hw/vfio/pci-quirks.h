/*
 * vfio generic region quirks (mostly backdoors to PCI config space)
 *
 * Copyright Red Hat, Inc. 2012-2015
 *
 * Authors:
 *  Alex Williamson <alex.williamson@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */
#ifndef HW_VFIO_VFIO_PCI_QUIRKS_H
#define HW_VFIO_VFIO_PCI_QUIRKS_H

#include "qemu/osdep.h"
#include "exec/memop.h"

/*
 * The generic window quirks operate on an address and data register,
 * vfio_generic_window_address_quirk handles the address register and
 * vfio_generic_window_data_quirk handles the data register.  These ops
 * pass reads and writes through to hardware until a value matching the
 * stored address match/mask is written.  When this occurs, the data
 * register access emulated PCI config space for the device rather than
 * passing through accesses.  This enables devices where PCI config space
 * is accessible behind a window register to maintain the virtualization
 * provided through vfio.
 */
typedef struct VFIOConfigWindowMatch {
    uint32_t match;
    uint32_t mask;
} VFIOConfigWindowMatch;

typedef struct VFIOConfigWindowQuirk {
    struct VFIOPCIDevice *vdev;

    uint32_t address_val;

    uint32_t address_offset;
    uint32_t data_offset;

    bool window_enabled;
    uint8_t bar;

    MemoryRegion *addr_mem;
    MemoryRegion *data_mem;

    uint32_t nr_matches;
    VFIOConfigWindowMatch matches[];
} VFIOConfigWindowQuirk;

extern const MemoryRegionOps vfio_generic_window_address_quirk;
extern const MemoryRegionOps vfio_generic_window_data_quirk;

/*
 * The generic mirror quirk handles devices which expose PCI config space
 * through a region within a BAR.  When enabled, reads and writes are
 * redirected through to emulated PCI config space.  XXX if PCI config space
 * used memory regions, this could just be an alias.
 */
typedef struct VFIOConfigMirrorQuirk {
    struct VFIOPCIDevice *vdev;
    uint32_t offset; /* Offset in BAR */
    uint32_t config_offset; /* Offset in PCI config space */
    uint8_t bar;
    MemoryRegion *mem;
    uint8_t data[];
} VFIOConfigMirrorQuirk;

extern const MemoryRegionOps vfio_generic_mirror_quirk;

#endif /* HW_VFIO_VFIO_PCI_QUIRKS_H */

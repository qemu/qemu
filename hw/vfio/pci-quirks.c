/*
 * device quirks for PCI devices
 *
 * Copyright Red Hat, Inc. 2012-2015
 *
 * Authors:
 *  Alex Williamson <alex.williamson@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include "pci.h"
#include "trace.h"
#include "qemu/range.h"

#define PCI_ANY_ID (~0)

/* Use uin32_t for vendor & device so PCI_ANY_ID expands and cannot match hw */
static bool vfio_pci_is(VFIOPCIDevice *vdev, uint32_t vendor, uint32_t device)
{
    PCIDevice *pdev = &vdev->pdev;

    return (vendor == PCI_ANY_ID ||
            vendor == pci_get_word(pdev->config + PCI_VENDOR_ID)) &&
           (device == PCI_ANY_ID ||
            device == pci_get_word(pdev->config + PCI_DEVICE_ID));
}

/*
 * List of device ids/vendor ids for which to disable
 * option rom loading. This avoids the guest hangs during rom
 * execution as noticed with the BCM 57810 card for lack of a
 * more better way to handle such issues.
 * The  user can still override by specifying a romfile or
 * rombar=1.
 * Please see https://bugs.launchpad.net/qemu/+bug/1284874
 * for an analysis of the 57810 card hang. When adding
 * a new vendor id/device id combination below, please also add
 * your card/environment details and information that could
 * help in debugging to the bug tracking this issue
 */
static const struct {
    uint32_t vendor;
    uint32_t device;
} romblacklist[] = {
    { 0x14e4, 0x168e }, /* Broadcom BCM 57810 */
};

bool vfio_blacklist_opt_rom(VFIOPCIDevice *vdev)
{
    int i;

    for (i = 0 ; i < ARRAY_SIZE(romblacklist); i++) {
        if (vfio_pci_is(vdev, romblacklist[i].vendor, romblacklist[i].device)) {
            trace_vfio_quirk_rom_blacklisted(vdev->vbasedev.name,
                                             romblacklist[i].vendor,
                                             romblacklist[i].device);
            return true;
        }
    }
    return false;
}

/*
 * Device specific quirks
 */

/* Is range1 fully contained within range2?  */
static bool vfio_range_contained(uint64_t first1, uint64_t len1,
                                 uint64_t first2, uint64_t len2) {
    return (first1 >= first2 && first1 + len1 <= first2 + len2);
}

static bool vfio_flags_enabled(uint8_t flags, uint8_t mask)
{
    return (mask && (flags & mask) == mask);
}

static uint64_t vfio_generic_window_quirk_read(void *opaque,
                                               hwaddr addr, unsigned size)
{
    VFIOLegacyQuirk *quirk = opaque;
    VFIOPCIDevice *vdev = quirk->vdev;
    uint64_t data;

    if (vfio_flags_enabled(quirk->data.flags, quirk->data.read_flags) &&
        ranges_overlap(addr, size,
                       quirk->data.data_offset, quirk->data.data_size)) {
        hwaddr offset = addr - quirk->data.data_offset;

        if (!vfio_range_contained(addr, size, quirk->data.data_offset,
                                  quirk->data.data_size)) {
            hw_error("%s: window data read not fully contained: %s",
                     __func__, memory_region_name(quirk->mem));
        }

        data = vfio_pci_read_config(&vdev->pdev,
                                    quirk->data.address_val + offset, size);

        trace_vfio_generic_window_quirk_read(memory_region_name(quirk->mem),
                                             vdev->vbasedev.name,
                                             quirk->data.bar,
                                             addr, size, data);
    } else {
        data = vfio_region_read(&vdev->bars[quirk->data.bar].region,
                                addr + quirk->data.base_offset, size);
    }

    return data;
}

static void vfio_generic_window_quirk_write(void *opaque, hwaddr addr,
                                            uint64_t data, unsigned size)
{
    VFIOLegacyQuirk *quirk = opaque;
    VFIOPCIDevice *vdev = quirk->vdev;

    if (ranges_overlap(addr, size,
                       quirk->data.address_offset, quirk->data.address_size)) {

        if (addr != quirk->data.address_offset) {
            hw_error("%s: offset write into address window: %s",
                     __func__, memory_region_name(quirk->mem));
        }

        if ((data & ~quirk->data.address_mask) == quirk->data.address_match) {
            quirk->data.flags |= quirk->data.write_flags |
                                 quirk->data.read_flags;
            quirk->data.address_val = data & quirk->data.address_mask;
        } else {
            quirk->data.flags &= ~(quirk->data.write_flags |
                                   quirk->data.read_flags);
        }
    }

    if (vfio_flags_enabled(quirk->data.flags, quirk->data.write_flags) &&
        ranges_overlap(addr, size,
                       quirk->data.data_offset, quirk->data.data_size)) {
        hwaddr offset = addr - quirk->data.data_offset;

        if (!vfio_range_contained(addr, size, quirk->data.data_offset,
                                  quirk->data.data_size)) {
            hw_error("%s: window data write not fully contained: %s",
                     __func__, memory_region_name(quirk->mem));
        }

        vfio_pci_write_config(&vdev->pdev,
                              quirk->data.address_val + offset, data, size);
        trace_vfio_generic_window_quirk_write(memory_region_name(quirk->mem),
                                              vdev->vbasedev.name,
                                              quirk->data.bar,
                                              addr, data, size);
        return;
    }

    vfio_region_write(&vdev->bars[quirk->data.bar].region,
                   addr + quirk->data.base_offset, data, size);
}

static const MemoryRegionOps vfio_generic_window_quirk = {
    .read = vfio_generic_window_quirk_read,
    .write = vfio_generic_window_quirk_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static uint64_t vfio_generic_quirk_read(void *opaque,
                                        hwaddr addr, unsigned size)
{
    VFIOLegacyQuirk *quirk = opaque;
    VFIOPCIDevice *vdev = quirk->vdev;
    hwaddr base = quirk->data.address_match & TARGET_PAGE_MASK;
    hwaddr offset = quirk->data.address_match & ~TARGET_PAGE_MASK;
    uint64_t data;

    if (vfio_flags_enabled(quirk->data.flags, quirk->data.read_flags) &&
        ranges_overlap(addr, size, offset, quirk->data.address_mask + 1)) {
        if (!vfio_range_contained(addr, size, offset,
                                  quirk->data.address_mask + 1)) {
            hw_error("%s: read not fully contained: %s",
                     __func__, memory_region_name(quirk->mem));
        }

        data = vfio_pci_read_config(&vdev->pdev, addr - offset, size);

        trace_vfio_generic_quirk_read(memory_region_name(quirk->mem),
                                      vdev->vbasedev.name, quirk->data.bar,
                                      addr + base, size, data);
    } else {
        data = vfio_region_read(&vdev->bars[quirk->data.bar].region,
                                addr + base, size);
    }

    return data;
}

static void vfio_generic_quirk_write(void *opaque, hwaddr addr,
                                     uint64_t data, unsigned size)
{
    VFIOLegacyQuirk *quirk = opaque;
    VFIOPCIDevice *vdev = quirk->vdev;
    hwaddr base = quirk->data.address_match & TARGET_PAGE_MASK;
    hwaddr offset = quirk->data.address_match & ~TARGET_PAGE_MASK;

    if (vfio_flags_enabled(quirk->data.flags, quirk->data.write_flags) &&
        ranges_overlap(addr, size, offset, quirk->data.address_mask + 1)) {
        if (!vfio_range_contained(addr, size, offset,
                                  quirk->data.address_mask + 1)) {
            hw_error("%s: write not fully contained: %s",
                     __func__, memory_region_name(quirk->mem));
        }

        vfio_pci_write_config(&vdev->pdev, addr - offset, data, size);

        trace_vfio_generic_quirk_write(memory_region_name(quirk->mem),
                                       vdev->vbasedev.name, quirk->data.bar,
                                       addr + base, data, size);
    } else {
        vfio_region_write(&vdev->bars[quirk->data.bar].region,
                          addr + base, data, size);
    }
}

static const MemoryRegionOps vfio_generic_quirk = {
    .read = vfio_generic_quirk_read,
    .write = vfio_generic_quirk_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

#define PCI_VENDOR_ID_ATI               0x1002

/*
 * Radeon HD cards (HD5450 & HD7850) report the upper byte of the I/O port BAR
 * through VGA register 0x3c3.  On newer cards, the I/O port BAR is always
 * BAR4 (older cards like the X550 used BAR1, but we don't care to support
 * those).  Note that on bare metal, a read of 0x3c3 doesn't always return the
 * I/O port BAR address.  Originally this was coded to return the virtual BAR
 * address only if the physical register read returns the actual BAR address,
 * but users have reported greater success if we return the virtual address
 * unconditionally.
 */
static uint64_t vfio_ati_3c3_quirk_read(void *opaque,
                                        hwaddr addr, unsigned size)
{
    VFIOPCIDevice *vdev = opaque;
    uint64_t data = vfio_pci_read_config(&vdev->pdev,
                                         PCI_BASE_ADDRESS_4 + 1, size);

    trace_vfio_quirk_ati_3c3_read(vdev->vbasedev.name, data);

    return data;
}

static const MemoryRegionOps vfio_ati_3c3_quirk = {
    .read = vfio_ati_3c3_quirk_read,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void vfio_vga_probe_ati_3c3_quirk(VFIOPCIDevice *vdev)
{
    VFIOQuirk *quirk;

    /*
     * As long as the BAR is >= 256 bytes it will be aligned such that the
     * lower byte is always zero.  Filter out anything else, if it exists.
     */
    if (!vfio_pci_is(vdev, PCI_VENDOR_ID_ATI, PCI_ANY_ID) ||
        !vdev->bars[4].ioport || vdev->bars[4].region.size < 256) {
        return;
    }

    quirk = g_malloc0(sizeof(*quirk));
    quirk->mem = g_malloc0_n(sizeof(MemoryRegion), 1);
    quirk->nr_mem = 1;

    memory_region_init_io(quirk->mem, OBJECT(vdev), &vfio_ati_3c3_quirk, vdev,
                          "vfio-ati-3c3-quirk", 1);
    memory_region_add_subregion(&vdev->vga.region[QEMU_PCI_VGA_IO_HI].mem,
                                3 /* offset 3 bytes from 0x3c0 */, quirk->mem);

    QLIST_INSERT_HEAD(&vdev->vga.region[QEMU_PCI_VGA_IO_HI].quirks,
                      quirk, next);

    trace_vfio_quirk_ati_3c3_probe(vdev->vbasedev.name);
}

/*
 * Newer ATI/AMD devices, including HD5450 and HD7850, have a window to PCI
 * config space through MMIO BAR2 at offset 0x4000.  Nothing seems to access
 * the MMIO space directly, but a window to this space is provided through
 * I/O port BAR4.  Offset 0x0 is the address register and offset 0x4 is the
 * data register.  When the address is programmed to a range of 0x4000-0x4fff
 * PCI configuration space is available.  Experimentation seems to indicate
 * that only read-only access is provided, but we drop writes when the window
 * is enabled to config space nonetheless.
 */
static void vfio_probe_ati_bar4_window_quirk(VFIOPCIDevice *vdev, int nr)
{
    PCIDevice *pdev = &vdev->pdev;
    VFIOQuirk *quirk;
    VFIOLegacyQuirk *legacy;

    if (!vdev->has_vga || nr != 4 ||
        pci_get_word(pdev->config + PCI_VENDOR_ID) != PCI_VENDOR_ID_ATI) {
        return;
    }

    quirk = g_malloc0(sizeof(*quirk));
    quirk->data = legacy = g_malloc0(sizeof(*legacy));
    quirk->mem = legacy->mem = g_malloc0_n(sizeof(MemoryRegion), 1);
    quirk->nr_mem = 1;
    legacy->vdev = vdev;
    legacy->data.address_size = 4;
    legacy->data.data_offset = 4;
    legacy->data.data_size = 4;
    legacy->data.address_match = 0x4000;
    legacy->data.address_mask = PCIE_CONFIG_SPACE_SIZE - 1;
    legacy->data.bar = nr;
    legacy->data.read_flags = legacy->data.write_flags = 1;

    memory_region_init_io(quirk->mem, OBJECT(vdev),
                          &vfio_generic_window_quirk, legacy,
                          "vfio-ati-bar4-window-quirk", 8);
    memory_region_add_subregion_overlap(&vdev->bars[nr].region.mem,
                          legacy->data.base_offset, quirk->mem, 1);

    QLIST_INSERT_HEAD(&vdev->bars[nr].quirks, quirk, next);

    trace_vfio_probe_ati_bar4_window_quirk(vdev->vbasedev.name);
}

/*
 * Trap the BAR2 MMIO window to config space as well.
 */
static void vfio_probe_ati_bar2_4000_quirk(VFIOPCIDevice *vdev, int nr)
{
    PCIDevice *pdev = &vdev->pdev;
    VFIOQuirk *quirk;
    VFIOLegacyQuirk *legacy;

    /* Only enable on newer devices where BAR2 is 64bit */
    if (!vdev->has_vga || nr != 2 || !vdev->bars[2].mem64 ||
        pci_get_word(pdev->config + PCI_VENDOR_ID) != PCI_VENDOR_ID_ATI) {
        return;
    }

    quirk = g_malloc0(sizeof(*quirk));
    quirk->data = legacy = g_malloc0(sizeof(*legacy));
    quirk->mem = legacy->mem = g_malloc0_n(sizeof(MemoryRegion), 1);
    quirk->nr_mem = 1;
    legacy->vdev = vdev;
    legacy->data.flags = legacy->data.read_flags = legacy->data.write_flags = 1;
    legacy->data.address_match = 0x4000;
    legacy->data.address_mask = PCIE_CONFIG_SPACE_SIZE - 1;
    legacy->data.bar = nr;

    memory_region_init_io(quirk->mem, OBJECT(vdev), &vfio_generic_quirk, legacy,
                          "vfio-ati-bar2-4000-quirk",
                          TARGET_PAGE_ALIGN(legacy->data.address_mask + 1));
    memory_region_add_subregion_overlap(&vdev->bars[nr].region.mem,
                          legacy->data.address_match & TARGET_PAGE_MASK,
                          quirk->mem, 1);

    QLIST_INSERT_HEAD(&vdev->bars[nr].quirks, quirk, next);

    trace_vfio_probe_ati_bar2_4000_quirk(vdev->vbasedev.name);
}

/*
 * Older ATI/AMD cards like the X550 have a similar window to that above.
 * I/O port BAR1 provides a window to a mirror of PCI config space located
 * in BAR2 at offset 0xf00.  We don't care to support such older cards, but
 * note it for future reference.
 */

#define PCI_VENDOR_ID_NVIDIA                    0x10de

/*
 * Nvidia has several different methods to get to config space, the
 * nouveu project has several of these documented here:
 * https://github.com/pathscale/envytools/tree/master/hwdocs
 *
 * The first quirk is actually not documented in envytools and is found
 * on 10de:01d1 (NVIDIA Corporation G72 [GeForce 7300 LE]).  This is an
 * NV46 chipset.  The backdoor uses the legacy VGA I/O ports to access
 * the mirror of PCI config space found at BAR0 offset 0x1800.  The access
 * sequence first writes 0x338 to I/O port 0x3d4.  The target offset is
 * then written to 0x3d0.  Finally 0x538 is written for a read and 0x738
 * is written for a write to 0x3d4.  The BAR0 offset is then accessible
 * through 0x3d0.  This quirk doesn't seem to be necessary on newer cards
 * that use the I/O port BAR5 window but it doesn't hurt to leave it.
 */
typedef enum {NONE = 0, SELECT, WINDOW, READ, WRITE} VFIONvidia3d0State;
static const char *nv3d0_states[] = { "NONE", "SELECT",
                                      "WINDOW", "READ", "WRITE" };

typedef struct VFIONvidia3d0Quirk {
    VFIOPCIDevice *vdev;
    VFIONvidia3d0State state;
    uint32_t offset;
} VFIONvidia3d0Quirk;

static uint64_t vfio_nvidia_3d4_quirk_read(void *opaque,
                                           hwaddr addr, unsigned size)
{
    VFIONvidia3d0Quirk *quirk = opaque;
    VFIOPCIDevice *vdev = quirk->vdev;

    quirk->state = NONE;

    return vfio_vga_read(&vdev->vga.region[QEMU_PCI_VGA_IO_HI],
                         addr + 0x14, size);
}

static void vfio_nvidia_3d4_quirk_write(void *opaque, hwaddr addr,
                                        uint64_t data, unsigned size)
{
    VFIONvidia3d0Quirk *quirk = opaque;
    VFIOPCIDevice *vdev = quirk->vdev;
    VFIONvidia3d0State old_state = quirk->state;

    quirk->state = NONE;

    switch (data) {
    case 0x338:
        if (old_state == NONE) {
            quirk->state = SELECT;
            trace_vfio_quirk_nvidia_3d0_state(vdev->vbasedev.name,
                                              nv3d0_states[quirk->state]);
        }
        break;
    case 0x538:
        if (old_state == WINDOW) {
            quirk->state = READ;
            trace_vfio_quirk_nvidia_3d0_state(vdev->vbasedev.name,
                                              nv3d0_states[quirk->state]);
        }
        break;
    case 0x738:
        if (old_state == WINDOW) {
            quirk->state = WRITE;
            trace_vfio_quirk_nvidia_3d0_state(vdev->vbasedev.name,
                                              nv3d0_states[quirk->state]);
        }
        break;
    }

    vfio_vga_write(&vdev->vga.region[QEMU_PCI_VGA_IO_HI],
                   addr + 0x14, data, size);
}

static const MemoryRegionOps vfio_nvidia_3d4_quirk = {
    .read = vfio_nvidia_3d4_quirk_read,
    .write = vfio_nvidia_3d4_quirk_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static uint64_t vfio_nvidia_3d0_quirk_read(void *opaque,
                                           hwaddr addr, unsigned size)
{
    VFIONvidia3d0Quirk *quirk = opaque;
    VFIOPCIDevice *vdev = quirk->vdev;
    VFIONvidia3d0State old_state = quirk->state;
    uint64_t data = vfio_vga_read(&vdev->vga.region[QEMU_PCI_VGA_IO_HI],
                                  addr + 0x10, size);

    quirk->state = NONE;

    if (old_state == READ &&
        (quirk->offset & ~(PCI_CONFIG_SPACE_SIZE - 1)) == 0x1800) {
        uint8_t offset = quirk->offset & (PCI_CONFIG_SPACE_SIZE - 1);

        data = vfio_pci_read_config(&vdev->pdev, offset, size);
        trace_vfio_quirk_nvidia_3d0_read(vdev->vbasedev.name,
                                         offset, size, data);
    }

    return data;
}

static void vfio_nvidia_3d0_quirk_write(void *opaque, hwaddr addr,
                                        uint64_t data, unsigned size)
{
    VFIONvidia3d0Quirk *quirk = opaque;
    VFIOPCIDevice *vdev = quirk->vdev;
    VFIONvidia3d0State old_state = quirk->state;

    quirk->state = NONE;

    if (old_state == SELECT) {
        quirk->offset = (uint32_t)data;
        quirk->state = WINDOW;
        trace_vfio_quirk_nvidia_3d0_state(vdev->vbasedev.name,
                                          nv3d0_states[quirk->state]);
    } else if (old_state == WRITE) {
        if ((quirk->offset & ~(PCI_CONFIG_SPACE_SIZE - 1)) == 0x1800) {
            uint8_t offset = quirk->offset & (PCI_CONFIG_SPACE_SIZE - 1);

            vfio_pci_write_config(&vdev->pdev, offset, data, size);
            trace_vfio_quirk_nvidia_3d0_write(vdev->vbasedev.name,
                                              offset, data, size);
            return;
        }
    }

    vfio_vga_write(&vdev->vga.region[QEMU_PCI_VGA_IO_HI],
                   addr + 0x10, data, size);
}

static const MemoryRegionOps vfio_nvidia_3d0_quirk = {
    .read = vfio_nvidia_3d0_quirk_read,
    .write = vfio_nvidia_3d0_quirk_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void vfio_vga_probe_nvidia_3d0_quirk(VFIOPCIDevice *vdev)
{
    VFIOQuirk *quirk;
    VFIONvidia3d0Quirk *data;

    if (!vfio_pci_is(vdev, PCI_VENDOR_ID_NVIDIA, PCI_ANY_ID) ||
        !vdev->bars[1].region.size) {
        return;
    }

    quirk = g_malloc0(sizeof(*quirk));
    quirk->data = data = g_malloc0(sizeof(*data));
    quirk->mem = g_malloc0_n(sizeof(MemoryRegion), 2);
    quirk->nr_mem = 2;
    data->vdev = vdev;

    memory_region_init_io(&quirk->mem[0], OBJECT(vdev), &vfio_nvidia_3d4_quirk,
                          data, "vfio-nvidia-3d4-quirk", 2);
    memory_region_add_subregion(&vdev->vga.region[QEMU_PCI_VGA_IO_HI].mem,
                                0x14 /* 0x3c0 + 0x14 */, &quirk->mem[0]);

    memory_region_init_io(&quirk->mem[1], OBJECT(vdev), &vfio_nvidia_3d0_quirk,
                          data, "vfio-nvidia-3d0-quirk", 2);
    memory_region_add_subregion(&vdev->vga.region[QEMU_PCI_VGA_IO_HI].mem,
                                0x10 /* 0x3c0 + 0x10 */, &quirk->mem[1]);

    QLIST_INSERT_HEAD(&vdev->vga.region[QEMU_PCI_VGA_IO_HI].quirks,
                      quirk, next);

    trace_vfio_quirk_nvidia_3d0_probe(vdev->vbasedev.name);
}

/*
 * The second quirk is documented in envytools.  The I/O port BAR5 is just
 * a set of address/data ports to the MMIO BARs.  The BAR we care about is
 * again BAR0.  This backdoor is apparently a bit newer than the one above
 * so we need to not only trap 256 bytes @0x1800, but all of PCI config
 * space, including extended space is available at the 4k @0x88000.
 */
enum {
    NV_BAR5_ADDRESS = 0x1,
    NV_BAR5_ENABLE = 0x2,
    NV_BAR5_MASTER = 0x4,
    NV_BAR5_VALID = 0x7,
};

static void vfio_nvidia_bar5_window_quirk_write(void *opaque, hwaddr addr,
                                                uint64_t data, unsigned size)
{
    VFIOLegacyQuirk *quirk = opaque;

    switch (addr) {
    case 0x0:
        if (data & 0x1) {
            quirk->data.flags |= NV_BAR5_MASTER;
        } else {
            quirk->data.flags &= ~NV_BAR5_MASTER;
        }
        break;
    case 0x4:
        if (data & 0x1) {
            quirk->data.flags |= NV_BAR5_ENABLE;
        } else {
            quirk->data.flags &= ~NV_BAR5_ENABLE;
        }
        break;
    case 0x8:
        if (quirk->data.flags & NV_BAR5_MASTER) {
            if ((data & ~0xfff) == 0x88000) {
                quirk->data.flags |= NV_BAR5_ADDRESS;
                quirk->data.address_val = data & 0xfff;
            } else if ((data & ~0xff) == 0x1800) {
                quirk->data.flags |= NV_BAR5_ADDRESS;
                quirk->data.address_val = data & 0xff;
            } else {
                quirk->data.flags &= ~NV_BAR5_ADDRESS;
            }
        }
        break;
    }

    vfio_generic_window_quirk_write(opaque, addr, data, size);
}

static const MemoryRegionOps vfio_nvidia_bar5_window_quirk = {
    .read = vfio_generic_window_quirk_read,
    .write = vfio_nvidia_bar5_window_quirk_write,
    .valid.min_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void vfio_probe_nvidia_bar5_window_quirk(VFIOPCIDevice *vdev, int nr)
{
    PCIDevice *pdev = &vdev->pdev;
    VFIOQuirk *quirk;
    VFIOLegacyQuirk *legacy;

    if (!vdev->has_vga || nr != 5 ||
        pci_get_word(pdev->config + PCI_VENDOR_ID) != PCI_VENDOR_ID_NVIDIA) {
        return;
    }

    quirk = g_malloc0(sizeof(*quirk));
    quirk->data = legacy = g_malloc0(sizeof(*legacy));
    quirk->mem = legacy->mem = g_malloc0_n(sizeof(MemoryRegion), 1);
    quirk->nr_mem = 1;
    legacy->vdev = vdev;
    legacy->data.read_flags = legacy->data.write_flags = NV_BAR5_VALID;
    legacy->data.address_offset = 0x8;
    legacy->data.address_size = 0; /* actually 4, but avoids generic code */
    legacy->data.data_offset = 0xc;
    legacy->data.data_size = 4;
    legacy->data.bar = nr;

    memory_region_init_io(quirk->mem, OBJECT(vdev),
                          &vfio_nvidia_bar5_window_quirk, legacy,
                          "vfio-nvidia-bar5-window-quirk", 16);
    memory_region_add_subregion_overlap(&vdev->bars[nr].region.mem,
                                        0, quirk->mem, 1);

    QLIST_INSERT_HEAD(&vdev->bars[nr].quirks, quirk, next);

    trace_vfio_probe_nvidia_bar5_window_quirk(vdev->vbasedev.name);
}

static void vfio_nvidia_88000_quirk_write(void *opaque, hwaddr addr,
                                          uint64_t data, unsigned size)
{
    VFIOLegacyQuirk *quirk = opaque;
    VFIOPCIDevice *vdev = quirk->vdev;
    PCIDevice *pdev = &vdev->pdev;
    hwaddr base = quirk->data.address_match & TARGET_PAGE_MASK;

    vfio_generic_quirk_write(opaque, addr, data, size);

    /*
     * Nvidia seems to acknowledge MSI interrupts by writing 0xff to the
     * MSI capability ID register.  Both the ID and next register are
     * read-only, so we allow writes covering either of those to real hw.
     * NB - only fixed for the 0x88000 MMIO window.
     */
    if ((pdev->cap_present & QEMU_PCI_CAP_MSI) &&
        vfio_range_contained(addr, size, pdev->msi_cap, PCI_MSI_FLAGS)) {
        vfio_region_write(&vdev->bars[quirk->data.bar].region,
                          addr + base, data, size);
    }
}

static const MemoryRegionOps vfio_nvidia_88000_quirk = {
    .read = vfio_generic_quirk_read,
    .write = vfio_nvidia_88000_quirk_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

/*
 * Finally, BAR0 itself.  We want to redirect any accesses to either
 * 0x1800 or 0x88000 through the PCI config space access functions.
 *
 * NB - quirk at a page granularity or else they don't seem to work when
 *      BARs are mmap'd
 *
 * Here's offset 0x88000...
 */
static void vfio_probe_nvidia_bar0_88000_quirk(VFIOPCIDevice *vdev, int nr)
{
    PCIDevice *pdev = &vdev->pdev;
    VFIOQuirk *quirk;
    VFIOLegacyQuirk *legacy;
    uint16_t vendor, class;

    vendor = pci_get_word(pdev->config + PCI_VENDOR_ID);
    class = pci_get_word(pdev->config + PCI_CLASS_DEVICE);

    if (nr != 0 || vendor != PCI_VENDOR_ID_NVIDIA ||
        class != PCI_CLASS_DISPLAY_VGA) {
        return;
    }

    quirk = g_malloc0(sizeof(*quirk));
    quirk->data = legacy = g_malloc0(sizeof(*legacy));
    quirk->mem = legacy->mem = g_malloc0_n(sizeof(MemoryRegion), 1);
    quirk->nr_mem = 1;
    legacy->vdev = vdev;
    legacy->data.flags = legacy->data.read_flags = legacy->data.write_flags = 1;
    legacy->data.address_match = 0x88000;
    legacy->data.address_mask = PCIE_CONFIG_SPACE_SIZE - 1;
    legacy->data.bar = nr;

    memory_region_init_io(quirk->mem, OBJECT(vdev), &vfio_nvidia_88000_quirk,
                          legacy, "vfio-nvidia-bar0-88000-quirk",
                          TARGET_PAGE_ALIGN(legacy->data.address_mask + 1));
    memory_region_add_subregion_overlap(&vdev->bars[nr].region.mem,
                          legacy->data.address_match & TARGET_PAGE_MASK,
                          quirk->mem, 1);

    QLIST_INSERT_HEAD(&vdev->bars[nr].quirks, quirk, next);

    trace_vfio_probe_nvidia_bar0_88000_quirk(vdev->vbasedev.name);
}

/*
 * And here's the same for BAR0 offset 0x1800...
 */
static void vfio_probe_nvidia_bar0_1800_quirk(VFIOPCIDevice *vdev, int nr)
{
    PCIDevice *pdev = &vdev->pdev;
    VFIOQuirk *quirk;
    VFIOLegacyQuirk *legacy;

    if (!vdev->has_vga || nr != 0 ||
        pci_get_word(pdev->config + PCI_VENDOR_ID) != PCI_VENDOR_ID_NVIDIA) {
        return;
    }

    /* Log the chipset ID */
    trace_vfio_probe_nvidia_bar0_1800_quirk_id(
            (unsigned int)(vfio_region_read(&vdev->bars[0].region, 0, 4) >> 20)
            & 0xff);

    quirk = g_malloc0(sizeof(*quirk));
    quirk->data = legacy = g_malloc0(sizeof(*legacy));
    quirk->mem = legacy->mem = g_malloc0_n(sizeof(MemoryRegion), 1);
    quirk->nr_mem = 1;
    legacy->vdev = vdev;
    legacy->data.flags = legacy->data.read_flags = legacy->data.write_flags = 1;
    legacy->data.address_match = 0x1800;
    legacy->data.address_mask = PCI_CONFIG_SPACE_SIZE - 1;
    legacy->data.bar = nr;

    memory_region_init_io(quirk->mem, OBJECT(vdev), &vfio_generic_quirk, legacy,
                          "vfio-nvidia-bar0-1800-quirk",
                          TARGET_PAGE_ALIGN(legacy->data.address_mask + 1));
    memory_region_add_subregion_overlap(&vdev->bars[nr].region.mem,
                          legacy->data.address_match & TARGET_PAGE_MASK,
                          quirk->mem, 1);

    QLIST_INSERT_HEAD(&vdev->bars[nr].quirks, quirk, next);

    trace_vfio_probe_nvidia_bar0_1800_quirk(vdev->vbasedev.name);
}

/*
 * TODO - Some Nvidia devices provide config access to their companion HDA
 * device and even to their parent bridge via these config space mirrors.
 * Add quirks for those regions.
 */

#define PCI_VENDOR_ID_REALTEK 0x10ec

/*
 * RTL8168 devices have a backdoor that can access the MSI-X table.  At BAR2
 * offset 0x70 there is a dword data register, offset 0x74 is a dword address
 * register.  According to the Linux r8169 driver, the MSI-X table is addressed
 * when the "type" portion of the address register is set to 0x1.  This appears
 * to be bits 16:30.  Bit 31 is both a write indicator and some sort of
 * "address latched" indicator.  Bits 12:15 are a mask field, which we can
 * ignore because the MSI-X table should always be accessed as a dword (full
 * mask).  Bits 0:11 is offset within the type.
 *
 * Example trace:
 *
 * Read from MSI-X table offset 0
 * vfio: vfio_bar_write(0000:05:00.0:BAR2+0x74, 0x1f000, 4) // store read addr
 * vfio: vfio_bar_read(0000:05:00.0:BAR2+0x74, 4) = 0x8001f000 // latch
 * vfio: vfio_bar_read(0000:05:00.0:BAR2+0x70, 4) = 0xfee00398 // read data
 *
 * Write 0xfee00000 to MSI-X table offset 0
 * vfio: vfio_bar_write(0000:05:00.0:BAR2+0x70, 0xfee00000, 4) // write data
 * vfio: vfio_bar_write(0000:05:00.0:BAR2+0x74, 0x8001f000, 4) // do write
 * vfio: vfio_bar_read(0000:05:00.0:BAR2+0x74, 4) = 0x1f000 // complete
 */
static uint64_t vfio_rtl8168_window_quirk_read(void *opaque,
                                               hwaddr addr, unsigned size)
{
    VFIOLegacyQuirk *quirk = opaque;
    VFIOPCIDevice *vdev = quirk->vdev;
    uint64_t val = 0;

    if (!quirk->data.flags) { /* Non-MSI-X table access */
        return vfio_region_read(&vdev->bars[quirk->data.bar].region,
                                addr + 0x70, size);
    }

    switch (addr) {
    case 4: /* address */
        val = quirk->data.address_match ^ 0x80000000U; /* latch/complete */
        break;
    case 0: /* data */
        if ((vdev->pdev.cap_present & QEMU_PCI_CAP_MSIX)) {
            memory_region_dispatch_read(&vdev->pdev.msix_table_mmio,
                                (hwaddr)(quirk->data.address_match & 0xfff),
                                &val, size, MEMTXATTRS_UNSPECIFIED);
        }
        break;
    }

    trace_vfio_rtl8168_quirk_read(vdev->vbasedev.name,
                                  addr ? "address" : "data", val);
    return val;
}

static void vfio_rtl8168_window_quirk_write(void *opaque, hwaddr addr,
                                            uint64_t data, unsigned size)
{
    VFIOLegacyQuirk *quirk = opaque;
    VFIOPCIDevice *vdev = quirk->vdev;

    switch (addr) {
    case 4: /* address */
        if ((data & 0x7fff0000) == 0x10000) { /* MSI-X table */
            quirk->data.flags = 1; /* Activate reads */
            quirk->data.address_match = data;

            trace_vfio_rtl8168_quirk_write(vdev->vbasedev.name, data);

            if (data & 0x80000000U) { /* Do write */
                if (vdev->pdev.cap_present & QEMU_PCI_CAP_MSIX) {
                    hwaddr offset = data & 0xfff;
                    uint64_t val = quirk->data.address_mask;

                    trace_vfio_rtl8168_quirk_msix(vdev->vbasedev.name,
                                                  (uint16_t)offset, val);

                    /* Write to the proper guest MSI-X table instead */
                    memory_region_dispatch_write(&vdev->pdev.msix_table_mmio,
                                                 offset, val, size,
                                                 MEMTXATTRS_UNSPECIFIED);
                }
                return; /* Do not write guest MSI-X data to hardware */
            }
        } else {
            quirk->data.flags = 0; /* De-activate reads, non-MSI-X */
        }
        break;
    case 0: /* data */
        quirk->data.address_mask = data;
        break;
    }

    vfio_region_write(&vdev->bars[quirk->data.bar].region,
                      addr + 0x70, data, size);
}

static const MemoryRegionOps vfio_rtl8168_window_quirk = {
    .read = vfio_rtl8168_window_quirk_read,
    .write = vfio_rtl8168_window_quirk_write,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void vfio_probe_rtl8168_bar2_window_quirk(VFIOPCIDevice *vdev, int nr)
{
    PCIDevice *pdev = &vdev->pdev;
    VFIOQuirk *quirk;
    VFIOLegacyQuirk *legacy;

    if (pci_get_word(pdev->config + PCI_VENDOR_ID) != PCI_VENDOR_ID_REALTEK ||
        pci_get_word(pdev->config + PCI_DEVICE_ID) != 0x8168 || nr != 2) {
        return;
    }

    quirk = g_malloc0(sizeof(*quirk));
    quirk->data = legacy = g_malloc0(sizeof(*legacy));
    quirk->mem = legacy->mem = g_malloc0_n(sizeof(MemoryRegion), 1);
    quirk->nr_mem = 1;
    legacy->vdev = vdev;
    legacy->data.bar = nr;

    memory_region_init_io(quirk->mem, OBJECT(vdev), &vfio_rtl8168_window_quirk,
                          legacy, "vfio-rtl8168-window-quirk", 8);
    memory_region_add_subregion_overlap(&vdev->bars[nr].region.mem,
                                        0x70, quirk->mem, 1);

    QLIST_INSERT_HEAD(&vdev->bars[nr].quirks, quirk, next);

    trace_vfio_rtl8168_quirk_enable(vdev->vbasedev.name);
}

/*
 * Common quirk probe entry points.
 */
void vfio_vga_quirk_setup(VFIOPCIDevice *vdev)
{
    vfio_vga_probe_ati_3c3_quirk(vdev);
    vfio_vga_probe_nvidia_3d0_quirk(vdev);
}

void vfio_vga_quirk_teardown(VFIOPCIDevice *vdev)
{
    VFIOQuirk *quirk;
    int i, j;

    for (i = 0; i < ARRAY_SIZE(vdev->vga.region); i++) {
        QLIST_FOREACH(quirk, &vdev->vga.region[i].quirks, next) {
            for (j = 0; j < quirk->nr_mem; j++) {
                memory_region_del_subregion(&vdev->vga.region[i].mem,
                                            &quirk->mem[j]);
            }
        }
    }
}

void vfio_vga_quirk_free(VFIOPCIDevice *vdev)
{
    int i, j;

    for (i = 0; i < ARRAY_SIZE(vdev->vga.region); i++) {
        while (!QLIST_EMPTY(&vdev->vga.region[i].quirks)) {
            VFIOQuirk *quirk = QLIST_FIRST(&vdev->vga.region[i].quirks);
            QLIST_REMOVE(quirk, next);
            for (j = 0; j < quirk->nr_mem; j++) {
                object_unparent(OBJECT(&quirk->mem[j]));
            }
            g_free(quirk->mem);
            g_free(quirk->data);
            g_free(quirk);
        }
    }
}

void vfio_bar_quirk_setup(VFIOPCIDevice *vdev, int nr)
{
    vfio_probe_ati_bar4_window_quirk(vdev, nr);
    vfio_probe_ati_bar2_4000_quirk(vdev, nr);
    vfio_probe_nvidia_bar5_window_quirk(vdev, nr);
    vfio_probe_nvidia_bar0_88000_quirk(vdev, nr);
    vfio_probe_nvidia_bar0_1800_quirk(vdev, nr);
    vfio_probe_rtl8168_bar2_window_quirk(vdev, nr);
}

void vfio_bar_quirk_teardown(VFIOPCIDevice *vdev, int nr)
{
    VFIOBAR *bar = &vdev->bars[nr];
    VFIOQuirk *quirk;
    int i;

    QLIST_FOREACH(quirk, &bar->quirks, next) {
        for (i = 0; i < quirk->nr_mem; i++) {
            memory_region_del_subregion(&bar->region.mem, &quirk->mem[i]);
        }
    }
}

void vfio_bar_quirk_free(VFIOPCIDevice *vdev, int nr)
{
    VFIOBAR *bar = &vdev->bars[nr];
    int i;

    while (!QLIST_EMPTY(&bar->quirks)) {
        VFIOQuirk *quirk = QLIST_FIRST(&bar->quirks);
        QLIST_REMOVE(quirk, next);
        for (i = 0; i < quirk->nr_mem; i++) {
            object_unparent(OBJECT(&quirk->mem[i]));
        }
        g_free(quirk->mem);
        g_free(quirk->data);
        g_free(quirk);
    }
}

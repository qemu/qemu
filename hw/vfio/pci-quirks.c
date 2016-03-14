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

#include "qemu/osdep.h"
#include "pci.h"
#include "trace.h"
#include "qemu/range.h"

/* Use uin32_t for vendor & device so PCI_ANY_ID expands and cannot match hw */
static bool vfio_pci_is(VFIOPCIDevice *vdev, uint32_t vendor, uint32_t device)
{
    return (vendor == PCI_ANY_ID || vendor == vdev->vendor_id) &&
           (device == PCI_ANY_ID || device == vdev->device_id);
}

static bool vfio_is_vga(VFIOPCIDevice *vdev)
{
    PCIDevice *pdev = &vdev->pdev;
    uint16_t class = pci_get_word(pdev->config + PCI_CLASS_DEVICE);

    return class == PCI_CLASS_DISPLAY_VGA;
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
 * Device specific region quirks (mostly backdoors to PCI config space)
 */

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

static uint64_t vfio_generic_window_quirk_address_read(void *opaque,
                                                       hwaddr addr,
                                                       unsigned size)
{
    VFIOConfigWindowQuirk *window = opaque;
    VFIOPCIDevice *vdev = window->vdev;

    return vfio_region_read(&vdev->bars[window->bar].region,
                            addr + window->address_offset, size);
}

static void vfio_generic_window_quirk_address_write(void *opaque, hwaddr addr,
                                                    uint64_t data,
                                                    unsigned size)
{
    VFIOConfigWindowQuirk *window = opaque;
    VFIOPCIDevice *vdev = window->vdev;
    int i;

    window->window_enabled = false;

    vfio_region_write(&vdev->bars[window->bar].region,
                      addr + window->address_offset, data, size);

    for (i = 0; i < window->nr_matches; i++) {
        if ((data & ~window->matches[i].mask) == window->matches[i].match) {
            window->window_enabled = true;
            window->address_val = data & window->matches[i].mask;
            trace_vfio_quirk_generic_window_address_write(vdev->vbasedev.name,
                                    memory_region_name(window->addr_mem), data);
            break;
        }
    }
}

static const MemoryRegionOps vfio_generic_window_address_quirk = {
    .read = vfio_generic_window_quirk_address_read,
    .write = vfio_generic_window_quirk_address_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static uint64_t vfio_generic_window_quirk_data_read(void *opaque,
                                                    hwaddr addr, unsigned size)
{
    VFIOConfigWindowQuirk *window = opaque;
    VFIOPCIDevice *vdev = window->vdev;
    uint64_t data;

    /* Always read data reg, discard if window enabled */
    data = vfio_region_read(&vdev->bars[window->bar].region,
                            addr + window->data_offset, size);

    if (window->window_enabled) {
        data = vfio_pci_read_config(&vdev->pdev, window->address_val, size);
        trace_vfio_quirk_generic_window_data_read(vdev->vbasedev.name,
                                    memory_region_name(window->data_mem), data);
    }

    return data;
}

static void vfio_generic_window_quirk_data_write(void *opaque, hwaddr addr,
                                                 uint64_t data, unsigned size)
{
    VFIOConfigWindowQuirk *window = opaque;
    VFIOPCIDevice *vdev = window->vdev;

    if (window->window_enabled) {
        vfio_pci_write_config(&vdev->pdev, window->address_val, data, size);
        trace_vfio_quirk_generic_window_data_write(vdev->vbasedev.name,
                                    memory_region_name(window->data_mem), data);
        return;
    }

    vfio_region_write(&vdev->bars[window->bar].region,
                      addr + window->data_offset, data, size);
}

static const MemoryRegionOps vfio_generic_window_data_quirk = {
    .read = vfio_generic_window_quirk_data_read,
    .write = vfio_generic_window_quirk_data_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

/*
 * The generic mirror quirk handles devices which expose PCI config space
 * through a region within a BAR.  When enabled, reads and writes are
 * redirected through to emulated PCI config space.  XXX if PCI config space
 * used memory regions, this could just be an alias.
 */
typedef struct VFIOConfigMirrorQuirk {
    struct VFIOPCIDevice *vdev;
    uint32_t offset;
    uint8_t bar;
    MemoryRegion *mem;
} VFIOConfigMirrorQuirk;

static uint64_t vfio_generic_quirk_mirror_read(void *opaque,
                                               hwaddr addr, unsigned size)
{
    VFIOConfigMirrorQuirk *mirror = opaque;
    VFIOPCIDevice *vdev = mirror->vdev;
    uint64_t data;

    /* Read and discard in case the hardware cares */
    (void)vfio_region_read(&vdev->bars[mirror->bar].region,
                           addr + mirror->offset, size);

    data = vfio_pci_read_config(&vdev->pdev, addr, size);
    trace_vfio_quirk_generic_mirror_read(vdev->vbasedev.name,
                                         memory_region_name(mirror->mem),
                                         addr, data);
    return data;
}

static void vfio_generic_quirk_mirror_write(void *opaque, hwaddr addr,
                                            uint64_t data, unsigned size)
{
    VFIOConfigMirrorQuirk *mirror = opaque;
    VFIOPCIDevice *vdev = mirror->vdev;

    vfio_pci_write_config(&vdev->pdev, addr, data, size);
    trace_vfio_quirk_generic_mirror_write(vdev->vbasedev.name,
                                          memory_region_name(mirror->mem),
                                          addr, data);
}

static const MemoryRegionOps vfio_generic_mirror_quirk = {
    .read = vfio_generic_quirk_mirror_read,
    .write = vfio_generic_quirk_mirror_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

/* Is range1 fully contained within range2?  */
static bool vfio_range_contained(uint64_t first1, uint64_t len1,
                                 uint64_t first2, uint64_t len2) {
    return (first1 >= first2 && first1 + len1 <= first2 + len2);
}

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
    quirk->mem = g_new0(MemoryRegion, 1);
    quirk->nr_mem = 1;

    memory_region_init_io(quirk->mem, OBJECT(vdev), &vfio_ati_3c3_quirk, vdev,
                          "vfio-ati-3c3-quirk", 1);
    memory_region_add_subregion(&vdev->vga->region[QEMU_PCI_VGA_IO_HI].mem,
                                3 /* offset 3 bytes from 0x3c0 */, quirk->mem);

    QLIST_INSERT_HEAD(&vdev->vga->region[QEMU_PCI_VGA_IO_HI].quirks,
                      quirk, next);

    trace_vfio_quirk_ati_3c3_probe(vdev->vbasedev.name);
}

/*
 * Newer ATI/AMD devices, including HD5450 and HD7850, have a mirror to PCI
 * config space through MMIO BAR2 at offset 0x4000.  Nothing seems to access
 * the MMIO space directly, but a window to this space is provided through
 * I/O port BAR4.  Offset 0x0 is the address register and offset 0x4 is the
 * data register.  When the address is programmed to a range of 0x4000-0x4fff
 * PCI configuration space is available.  Experimentation seems to indicate
 * that read-only may be provided by hardware.
 */
static void vfio_probe_ati_bar4_quirk(VFIOPCIDevice *vdev, int nr)
{
    VFIOQuirk *quirk;
    VFIOConfigWindowQuirk *window;

    /* This windows doesn't seem to be used except by legacy VGA code */
    if (!vfio_pci_is(vdev, PCI_VENDOR_ID_ATI, PCI_ANY_ID) ||
        !vdev->has_vga || nr != 4) {
        return;
    }

    quirk = g_malloc0(sizeof(*quirk));
    quirk->mem = g_new0(MemoryRegion, 2);
    quirk->nr_mem = 2;
    window = quirk->data = g_malloc0(sizeof(*window) +
                                     sizeof(VFIOConfigWindowMatch));
    window->vdev = vdev;
    window->address_offset = 0;
    window->data_offset = 4;
    window->nr_matches = 1;
    window->matches[0].match = 0x4000;
    window->matches[0].mask = vdev->config_size - 1;
    window->bar = nr;
    window->addr_mem = &quirk->mem[0];
    window->data_mem = &quirk->mem[1];

    memory_region_init_io(window->addr_mem, OBJECT(vdev),
                          &vfio_generic_window_address_quirk, window,
                          "vfio-ati-bar4-window-address-quirk", 4);
    memory_region_add_subregion_overlap(vdev->bars[nr].region.mem,
                                        window->address_offset,
                                        window->addr_mem, 1);

    memory_region_init_io(window->data_mem, OBJECT(vdev),
                          &vfio_generic_window_data_quirk, window,
                          "vfio-ati-bar4-window-data-quirk", 4);
    memory_region_add_subregion_overlap(vdev->bars[nr].region.mem,
                                        window->data_offset,
                                        window->data_mem, 1);

    QLIST_INSERT_HEAD(&vdev->bars[nr].quirks, quirk, next);

    trace_vfio_quirk_ati_bar4_probe(vdev->vbasedev.name);
}

/*
 * Trap the BAR2 MMIO mirror to config space as well.
 */
static void vfio_probe_ati_bar2_quirk(VFIOPCIDevice *vdev, int nr)
{
    VFIOQuirk *quirk;
    VFIOConfigMirrorQuirk *mirror;

    /* Only enable on newer devices where BAR2 is 64bit */
    if (!vfio_pci_is(vdev, PCI_VENDOR_ID_ATI, PCI_ANY_ID) ||
        !vdev->has_vga || nr != 2 || !vdev->bars[2].mem64) {
        return;
    }

    quirk = g_malloc0(sizeof(*quirk));
    mirror = quirk->data = g_malloc0(sizeof(*mirror));
    mirror->mem = quirk->mem = g_new0(MemoryRegion, 1);
    quirk->nr_mem = 1;
    mirror->vdev = vdev;
    mirror->offset = 0x4000;
    mirror->bar = nr;

    memory_region_init_io(mirror->mem, OBJECT(vdev),
                          &vfio_generic_mirror_quirk, mirror,
                          "vfio-ati-bar2-4000-quirk", PCI_CONFIG_SPACE_SIZE);
    memory_region_add_subregion_overlap(vdev->bars[nr].region.mem,
                                        mirror->offset, mirror->mem, 1);

    QLIST_INSERT_HEAD(&vdev->bars[nr].quirks, quirk, next);

    trace_vfio_quirk_ati_bar2_probe(vdev->vbasedev.name);
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

    return vfio_vga_read(&vdev->vga->region[QEMU_PCI_VGA_IO_HI],
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

    vfio_vga_write(&vdev->vga->region[QEMU_PCI_VGA_IO_HI],
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
    uint64_t data = vfio_vga_read(&vdev->vga->region[QEMU_PCI_VGA_IO_HI],
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

    vfio_vga_write(&vdev->vga->region[QEMU_PCI_VGA_IO_HI],
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
    quirk->mem = g_new0(MemoryRegion, 2);
    quirk->nr_mem = 2;
    data->vdev = vdev;

    memory_region_init_io(&quirk->mem[0], OBJECT(vdev), &vfio_nvidia_3d4_quirk,
                          data, "vfio-nvidia-3d4-quirk", 2);
    memory_region_add_subregion(&vdev->vga->region[QEMU_PCI_VGA_IO_HI].mem,
                                0x14 /* 0x3c0 + 0x14 */, &quirk->mem[0]);

    memory_region_init_io(&quirk->mem[1], OBJECT(vdev), &vfio_nvidia_3d0_quirk,
                          data, "vfio-nvidia-3d0-quirk", 2);
    memory_region_add_subregion(&vdev->vga->region[QEMU_PCI_VGA_IO_HI].mem,
                                0x10 /* 0x3c0 + 0x10 */, &quirk->mem[1]);

    QLIST_INSERT_HEAD(&vdev->vga->region[QEMU_PCI_VGA_IO_HI].quirks,
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
typedef struct VFIONvidiaBAR5Quirk {
    uint32_t master;
    uint32_t enable;
    MemoryRegion *addr_mem;
    MemoryRegion *data_mem;
    bool enabled;
    VFIOConfigWindowQuirk window; /* last for match data */
} VFIONvidiaBAR5Quirk;

static void vfio_nvidia_bar5_enable(VFIONvidiaBAR5Quirk *bar5)
{
    VFIOPCIDevice *vdev = bar5->window.vdev;

    if (((bar5->master & bar5->enable) & 0x1) == bar5->enabled) {
        return;
    }

    bar5->enabled = !bar5->enabled;
    trace_vfio_quirk_nvidia_bar5_state(vdev->vbasedev.name,
                                       bar5->enabled ?  "Enable" : "Disable");
    memory_region_set_enabled(bar5->addr_mem, bar5->enabled);
    memory_region_set_enabled(bar5->data_mem, bar5->enabled);
}

static uint64_t vfio_nvidia_bar5_quirk_master_read(void *opaque,
                                                   hwaddr addr, unsigned size)
{
    VFIONvidiaBAR5Quirk *bar5 = opaque;
    VFIOPCIDevice *vdev = bar5->window.vdev;

    return vfio_region_read(&vdev->bars[5].region, addr, size);
}

static void vfio_nvidia_bar5_quirk_master_write(void *opaque, hwaddr addr,
                                                uint64_t data, unsigned size)
{
    VFIONvidiaBAR5Quirk *bar5 = opaque;
    VFIOPCIDevice *vdev = bar5->window.vdev;

    vfio_region_write(&vdev->bars[5].region, addr, data, size);

    bar5->master = data;
    vfio_nvidia_bar5_enable(bar5);
}

static const MemoryRegionOps vfio_nvidia_bar5_quirk_master = {
    .read = vfio_nvidia_bar5_quirk_master_read,
    .write = vfio_nvidia_bar5_quirk_master_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static uint64_t vfio_nvidia_bar5_quirk_enable_read(void *opaque,
                                                   hwaddr addr, unsigned size)
{
    VFIONvidiaBAR5Quirk *bar5 = opaque;
    VFIOPCIDevice *vdev = bar5->window.vdev;

    return vfio_region_read(&vdev->bars[5].region, addr + 4, size);
}

static void vfio_nvidia_bar5_quirk_enable_write(void *opaque, hwaddr addr,
                                                uint64_t data, unsigned size)
{
    VFIONvidiaBAR5Quirk *bar5 = opaque;
    VFIOPCIDevice *vdev = bar5->window.vdev;

    vfio_region_write(&vdev->bars[5].region, addr + 4, data, size);

    bar5->enable = data;
    vfio_nvidia_bar5_enable(bar5);
}

static const MemoryRegionOps vfio_nvidia_bar5_quirk_enable = {
    .read = vfio_nvidia_bar5_quirk_enable_read,
    .write = vfio_nvidia_bar5_quirk_enable_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void vfio_probe_nvidia_bar5_quirk(VFIOPCIDevice *vdev, int nr)
{
    VFIOQuirk *quirk;
    VFIONvidiaBAR5Quirk *bar5;
    VFIOConfigWindowQuirk *window;

    if (!vfio_pci_is(vdev, PCI_VENDOR_ID_NVIDIA, PCI_ANY_ID) ||
        !vdev->has_vga || nr != 5) {
        return;
    }

    quirk = g_malloc0(sizeof(*quirk));
    quirk->mem = g_new0(MemoryRegion, 4);
    quirk->nr_mem = 4;
    bar5 = quirk->data = g_malloc0(sizeof(*bar5) +
                                   (sizeof(VFIOConfigWindowMatch) * 2));
    window = &bar5->window;

    window->vdev = vdev;
    window->address_offset = 0x8;
    window->data_offset = 0xc;
    window->nr_matches = 2;
    window->matches[0].match = 0x1800;
    window->matches[0].mask = PCI_CONFIG_SPACE_SIZE - 1;
    window->matches[1].match = 0x88000;
    window->matches[1].mask = vdev->config_size - 1;
    window->bar = nr;
    window->addr_mem = bar5->addr_mem = &quirk->mem[0];
    window->data_mem = bar5->data_mem = &quirk->mem[1];

    memory_region_init_io(window->addr_mem, OBJECT(vdev),
                          &vfio_generic_window_address_quirk, window,
                          "vfio-nvidia-bar5-window-address-quirk", 4);
    memory_region_add_subregion_overlap(vdev->bars[nr].region.mem,
                                        window->address_offset,
                                        window->addr_mem, 1);
    memory_region_set_enabled(window->addr_mem, false);

    memory_region_init_io(window->data_mem, OBJECT(vdev),
                          &vfio_generic_window_data_quirk, window,
                          "vfio-nvidia-bar5-window-data-quirk", 4);
    memory_region_add_subregion_overlap(vdev->bars[nr].region.mem,
                                        window->data_offset,
                                        window->data_mem, 1);
    memory_region_set_enabled(window->data_mem, false);

    memory_region_init_io(&quirk->mem[2], OBJECT(vdev),
                          &vfio_nvidia_bar5_quirk_master, bar5,
                          "vfio-nvidia-bar5-master-quirk", 4);
    memory_region_add_subregion_overlap(vdev->bars[nr].region.mem,
                                        0, &quirk->mem[2], 1);

    memory_region_init_io(&quirk->mem[3], OBJECT(vdev),
                          &vfio_nvidia_bar5_quirk_enable, bar5,
                          "vfio-nvidia-bar5-enable-quirk", 4);
    memory_region_add_subregion_overlap(vdev->bars[nr].region.mem,
                                        4, &quirk->mem[3], 1);

    QLIST_INSERT_HEAD(&vdev->bars[nr].quirks, quirk, next);

    trace_vfio_quirk_nvidia_bar5_probe(vdev->vbasedev.name);
}

/*
 * Finally, BAR0 itself.  We want to redirect any accesses to either
 * 0x1800 or 0x88000 through the PCI config space access functions.
 */
static void vfio_nvidia_quirk_mirror_write(void *opaque, hwaddr addr,
                                           uint64_t data, unsigned size)
{
    VFIOConfigMirrorQuirk *mirror = opaque;
    VFIOPCIDevice *vdev = mirror->vdev;
    PCIDevice *pdev = &vdev->pdev;

    vfio_generic_quirk_mirror_write(opaque, addr, data, size);

    /*
     * Nvidia seems to acknowledge MSI interrupts by writing 0xff to the
     * MSI capability ID register.  Both the ID and next register are
     * read-only, so we allow writes covering either of those to real hw.
     */
    if ((pdev->cap_present & QEMU_PCI_CAP_MSI) &&
        vfio_range_contained(addr, size, pdev->msi_cap, PCI_MSI_FLAGS)) {
        vfio_region_write(&vdev->bars[mirror->bar].region,
                          addr + mirror->offset, data, size);
        trace_vfio_quirk_nvidia_bar0_msi_ack(vdev->vbasedev.name);
    }
}

static const MemoryRegionOps vfio_nvidia_mirror_quirk = {
    .read = vfio_generic_quirk_mirror_read,
    .write = vfio_nvidia_quirk_mirror_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void vfio_probe_nvidia_bar0_quirk(VFIOPCIDevice *vdev, int nr)
{
    VFIOQuirk *quirk;
    VFIOConfigMirrorQuirk *mirror;

    if (!vfio_pci_is(vdev, PCI_VENDOR_ID_NVIDIA, PCI_ANY_ID) ||
        !vfio_is_vga(vdev) || nr != 0) {
        return;
    }

    quirk = g_malloc0(sizeof(*quirk));
    mirror = quirk->data = g_malloc0(sizeof(*mirror));
    mirror->mem = quirk->mem = g_new0(MemoryRegion, 1);
    quirk->nr_mem = 1;
    mirror->vdev = vdev;
    mirror->offset = 0x88000;
    mirror->bar = nr;

    memory_region_init_io(mirror->mem, OBJECT(vdev),
                          &vfio_nvidia_mirror_quirk, mirror,
                          "vfio-nvidia-bar0-88000-mirror-quirk",
                          vdev->config_size);
    memory_region_add_subregion_overlap(vdev->bars[nr].region.mem,
                                        mirror->offset, mirror->mem, 1);

    QLIST_INSERT_HEAD(&vdev->bars[nr].quirks, quirk, next);

    /* The 0x1800 offset mirror only seems to get used by legacy VGA */
    if (vdev->has_vga) {
        quirk = g_malloc0(sizeof(*quirk));
        mirror = quirk->data = g_malloc0(sizeof(*mirror));
        mirror->mem = quirk->mem = g_new0(MemoryRegion, 1);
        quirk->nr_mem = 1;
        mirror->vdev = vdev;
        mirror->offset = 0x1800;
        mirror->bar = nr;

        memory_region_init_io(mirror->mem, OBJECT(vdev),
                              &vfio_nvidia_mirror_quirk, mirror,
                              "vfio-nvidia-bar0-1800-mirror-quirk",
                              PCI_CONFIG_SPACE_SIZE);
        memory_region_add_subregion_overlap(vdev->bars[nr].region.mem,
                                            mirror->offset, mirror->mem, 1);

        QLIST_INSERT_HEAD(&vdev->bars[nr].quirks, quirk, next);
    }

    trace_vfio_quirk_nvidia_bar0_probe(vdev->vbasedev.name);
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
typedef struct VFIOrtl8168Quirk {
    VFIOPCIDevice *vdev;
    uint32_t addr;
    uint32_t data;
    bool enabled;
} VFIOrtl8168Quirk;

static uint64_t vfio_rtl8168_quirk_address_read(void *opaque,
                                                hwaddr addr, unsigned size)
{
    VFIOrtl8168Quirk *rtl = opaque;
    VFIOPCIDevice *vdev = rtl->vdev;
    uint64_t data = vfio_region_read(&vdev->bars[2].region, addr + 0x74, size);

    if (rtl->enabled) {
        data = rtl->addr ^ 0x80000000U; /* latch/complete */
        trace_vfio_quirk_rtl8168_fake_latch(vdev->vbasedev.name, data);
    }

    return data;
}

static void vfio_rtl8168_quirk_address_write(void *opaque, hwaddr addr,
                                             uint64_t data, unsigned size)
{
    VFIOrtl8168Quirk *rtl = opaque;
    VFIOPCIDevice *vdev = rtl->vdev;

    rtl->enabled = false;

    if ((data & 0x7fff0000) == 0x10000) { /* MSI-X table */
        rtl->enabled = true;
        rtl->addr = (uint32_t)data;

        if (data & 0x80000000U) { /* Do write */
            if (vdev->pdev.cap_present & QEMU_PCI_CAP_MSIX) {
                hwaddr offset = data & 0xfff;
                uint64_t val = rtl->data;

                trace_vfio_quirk_rtl8168_msix_write(vdev->vbasedev.name,
                                                    (uint16_t)offset, val);

                /* Write to the proper guest MSI-X table instead */
                memory_region_dispatch_write(&vdev->pdev.msix_table_mmio,
                                             offset, val, size,
                                             MEMTXATTRS_UNSPECIFIED);
            }
            return; /* Do not write guest MSI-X data to hardware */
        }
    }

    vfio_region_write(&vdev->bars[2].region, addr + 0x74, data, size);
}

static const MemoryRegionOps vfio_rtl_address_quirk = {
    .read = vfio_rtl8168_quirk_address_read,
    .write = vfio_rtl8168_quirk_address_write,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static uint64_t vfio_rtl8168_quirk_data_read(void *opaque,
                                             hwaddr addr, unsigned size)
{
    VFIOrtl8168Quirk *rtl = opaque;
    VFIOPCIDevice *vdev = rtl->vdev;
    uint64_t data = vfio_region_read(&vdev->bars[2].region, addr + 0x74, size);

    if (rtl->enabled && (vdev->pdev.cap_present & QEMU_PCI_CAP_MSIX)) {
        hwaddr offset = rtl->addr & 0xfff;
        memory_region_dispatch_read(&vdev->pdev.msix_table_mmio, offset,
                                    &data, size, MEMTXATTRS_UNSPECIFIED);
        trace_vfio_quirk_rtl8168_msix_read(vdev->vbasedev.name, offset, data);
    }

    return data;
}

static void vfio_rtl8168_quirk_data_write(void *opaque, hwaddr addr,
                                          uint64_t data, unsigned size)
{
    VFIOrtl8168Quirk *rtl = opaque;
    VFIOPCIDevice *vdev = rtl->vdev;

    rtl->data = (uint32_t)data;

    vfio_region_write(&vdev->bars[2].region, addr + 0x70, data, size);
}

static const MemoryRegionOps vfio_rtl_data_quirk = {
    .read = vfio_rtl8168_quirk_data_read,
    .write = vfio_rtl8168_quirk_data_write,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void vfio_probe_rtl8168_bar2_quirk(VFIOPCIDevice *vdev, int nr)
{
    VFIOQuirk *quirk;
    VFIOrtl8168Quirk *rtl;

    if (!vfio_pci_is(vdev, PCI_VENDOR_ID_REALTEK, 0x8168) || nr != 2) {
        return;
    }

    quirk = g_malloc0(sizeof(*quirk));
    quirk->mem = g_new0(MemoryRegion, 2);
    quirk->nr_mem = 2;
    quirk->data = rtl = g_malloc0(sizeof(*rtl));
    rtl->vdev = vdev;

    memory_region_init_io(&quirk->mem[0], OBJECT(vdev),
                          &vfio_rtl_address_quirk, rtl,
                          "vfio-rtl8168-window-address-quirk", 4);
    memory_region_add_subregion_overlap(vdev->bars[nr].region.mem,
                                        0x74, &quirk->mem[0], 1);

    memory_region_init_io(&quirk->mem[1], OBJECT(vdev),
                          &vfio_rtl_data_quirk, rtl,
                          "vfio-rtl8168-window-data-quirk", 4);
    memory_region_add_subregion_overlap(vdev->bars[nr].region.mem,
                                        0x70, &quirk->mem[1], 1);

    QLIST_INSERT_HEAD(&vdev->bars[nr].quirks, quirk, next);

    trace_vfio_quirk_rtl8168_probe(vdev->vbasedev.name);
}

/*
 * Common quirk probe entry points.
 */
void vfio_vga_quirk_setup(VFIOPCIDevice *vdev)
{
    vfio_vga_probe_ati_3c3_quirk(vdev);
    vfio_vga_probe_nvidia_3d0_quirk(vdev);
}

void vfio_vga_quirk_exit(VFIOPCIDevice *vdev)
{
    VFIOQuirk *quirk;
    int i, j;

    for (i = 0; i < ARRAY_SIZE(vdev->vga->region); i++) {
        QLIST_FOREACH(quirk, &vdev->vga->region[i].quirks, next) {
            for (j = 0; j < quirk->nr_mem; j++) {
                memory_region_del_subregion(&vdev->vga->region[i].mem,
                                            &quirk->mem[j]);
            }
        }
    }
}

void vfio_vga_quirk_finalize(VFIOPCIDevice *vdev)
{
    int i, j;

    for (i = 0; i < ARRAY_SIZE(vdev->vga->region); i++) {
        while (!QLIST_EMPTY(&vdev->vga->region[i].quirks)) {
            VFIOQuirk *quirk = QLIST_FIRST(&vdev->vga->region[i].quirks);
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
    vfio_probe_ati_bar4_quirk(vdev, nr);
    vfio_probe_ati_bar2_quirk(vdev, nr);
    vfio_probe_nvidia_bar5_quirk(vdev, nr);
    vfio_probe_nvidia_bar0_quirk(vdev, nr);
    vfio_probe_rtl8168_bar2_quirk(vdev, nr);
}

void vfio_bar_quirk_exit(VFIOPCIDevice *vdev, int nr)
{
    VFIOBAR *bar = &vdev->bars[nr];
    VFIOQuirk *quirk;
    int i;

    QLIST_FOREACH(quirk, &bar->quirks, next) {
        for (i = 0; i < quirk->nr_mem; i++) {
            memory_region_del_subregion(bar->region.mem, &quirk->mem[i]);
        }
    }
}

void vfio_bar_quirk_finalize(VFIOPCIDevice *vdev, int nr)
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

/*
 * Reset quirks
 */

/*
 * AMD Radeon PCI config reset, based on Linux:
 *   drivers/gpu/drm/radeon/ci_smc.c:ci_is_smc_running()
 *   drivers/gpu/drm/radeon/radeon_device.c:radeon_pci_config_reset
 *   drivers/gpu/drm/radeon/ci_smc.c:ci_reset_smc()
 *   drivers/gpu/drm/radeon/ci_smc.c:ci_stop_smc_clock()
 * IDs: include/drm/drm_pciids.h
 * Registers: http://cgit.freedesktop.org/~agd5f/linux/commit/?id=4e2aa447f6f0
 *
 * Bonaire and Hawaii GPUs do not respond to a bus reset.  This is a bug in the
 * hardware that should be fixed on future ASICs.  The symptom of this is that
 * once the accerlated driver loads, Windows guests will bsod on subsequent
 * attmpts to load the driver, such as after VM reset or shutdown/restart.  To
 * work around this, we do an AMD specific PCI config reset, followed by an SMC
 * reset.  The PCI config reset only works if SMC firmware is running, so we
 * have a dependency on the state of the device as to whether this reset will
 * be effective.  There are still cases where we won't be able to kick the
 * device into working, but this greatly improves the usability overall.  The
 * config reset magic is relatively common on AMD GPUs, but the setup and SMC
 * poking is largely ASIC specific.
 */
static bool vfio_radeon_smc_is_running(VFIOPCIDevice *vdev)
{
    uint32_t clk, pc_c;

    /*
     * Registers 200h and 204h are index and data registers for accessing
     * indirect configuration registers within the device.
     */
    vfio_region_write(&vdev->bars[5].region, 0x200, 0x80000004, 4);
    clk = vfio_region_read(&vdev->bars[5].region, 0x204, 4);
    vfio_region_write(&vdev->bars[5].region, 0x200, 0x80000370, 4);
    pc_c = vfio_region_read(&vdev->bars[5].region, 0x204, 4);

    return (!(clk & 1) && (0x20100 <= pc_c));
}

/*
 * The scope of a config reset is controlled by a mode bit in the misc register
 * and a fuse, exposed as a bit in another register.  The fuse is the default
 * (0 = GFX, 1 = whole GPU), the misc bit is a toggle, with the forumula
 * scope = !(misc ^ fuse), where the resulting scope is defined the same as
 * the fuse.  A truth table therefore tells us that if misc == fuse, we need
 * to flip the value of the bit in the misc register.
 */
static void vfio_radeon_set_gfx_only_reset(VFIOPCIDevice *vdev)
{
    uint32_t misc, fuse;
    bool a, b;

    vfio_region_write(&vdev->bars[5].region, 0x200, 0xc00c0000, 4);
    fuse = vfio_region_read(&vdev->bars[5].region, 0x204, 4);
    b = fuse & 64;

    vfio_region_write(&vdev->bars[5].region, 0x200, 0xc0000010, 4);
    misc = vfio_region_read(&vdev->bars[5].region, 0x204, 4);
    a = misc & 2;

    if (a == b) {
        vfio_region_write(&vdev->bars[5].region, 0x204, misc ^ 2, 4);
        vfio_region_read(&vdev->bars[5].region, 0x204, 4); /* flush */
    }
}

static int vfio_radeon_reset(VFIOPCIDevice *vdev)
{
    PCIDevice *pdev = &vdev->pdev;
    int i, ret = 0;
    uint32_t data;

    /* Defer to a kernel implemented reset */
    if (vdev->vbasedev.reset_works) {
        trace_vfio_quirk_ati_bonaire_reset_skipped(vdev->vbasedev.name);
        return -ENODEV;
    }

    /* Enable only memory BAR access */
    vfio_pci_write_config(pdev, PCI_COMMAND, PCI_COMMAND_MEMORY, 2);

    /* Reset only works if SMC firmware is loaded and running */
    if (!vfio_radeon_smc_is_running(vdev)) {
        ret = -EINVAL;
        trace_vfio_quirk_ati_bonaire_reset_no_smc(vdev->vbasedev.name);
        goto out;
    }

    /* Make sure only the GFX function is reset */
    vfio_radeon_set_gfx_only_reset(vdev);

    /* AMD PCI config reset */
    vfio_pci_write_config(pdev, 0x7c, 0x39d5e86b, 4);
    usleep(100);

    /* Read back the memory size to make sure we're out of reset */
    for (i = 0; i < 100000; i++) {
        if (vfio_region_read(&vdev->bars[5].region, 0x5428, 4) != 0xffffffff) {
            goto reset_smc;
        }
        usleep(1);
    }

    trace_vfio_quirk_ati_bonaire_reset_timeout(vdev->vbasedev.name);

reset_smc:
    /* Reset SMC */
    vfio_region_write(&vdev->bars[5].region, 0x200, 0x80000000, 4);
    data = vfio_region_read(&vdev->bars[5].region, 0x204, 4);
    data |= 1;
    vfio_region_write(&vdev->bars[5].region, 0x204, data, 4);

    /* Disable SMC clock */
    vfio_region_write(&vdev->bars[5].region, 0x200, 0x80000004, 4);
    data = vfio_region_read(&vdev->bars[5].region, 0x204, 4);
    data |= 1;
    vfio_region_write(&vdev->bars[5].region, 0x204, data, 4);

    trace_vfio_quirk_ati_bonaire_reset_done(vdev->vbasedev.name);

out:
    /* Restore PCI command register */
    vfio_pci_write_config(pdev, PCI_COMMAND, 0, 2);

    return ret;
}

void vfio_setup_resetfn_quirk(VFIOPCIDevice *vdev)
{
    switch (vdev->vendor_id) {
    case 0x1002:
        switch (vdev->device_id) {
        /* Bonaire */
        case 0x6649: /* Bonaire [FirePro W5100] */
        case 0x6650:
        case 0x6651:
        case 0x6658: /* Bonaire XTX [Radeon R7 260X] */
        case 0x665c: /* Bonaire XT [Radeon HD 7790/8770 / R9 260 OEM] */
        case 0x665d: /* Bonaire [Radeon R7 200 Series] */
        /* Hawaii */
        case 0x67A0: /* Hawaii XT GL [FirePro W9100] */
        case 0x67A1: /* Hawaii PRO GL [FirePro W8100] */
        case 0x67A2:
        case 0x67A8:
        case 0x67A9:
        case 0x67AA:
        case 0x67B0: /* Hawaii XT [Radeon R9 290X] */
        case 0x67B1: /* Hawaii PRO [Radeon R9 290] */
        case 0x67B8:
        case 0x67B9:
        case 0x67BA:
        case 0x67BE:
            vdev->resetfn = vfio_radeon_reset;
            trace_vfio_quirk_ati_bonaire_reset(vdev->vbasedev.name);
            break;
        }
        break;
    }
}

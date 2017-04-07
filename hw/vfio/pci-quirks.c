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
#include "qemu/error-report.h"
#include "qemu/range.h"
#include "qapi/error.h"
#include "hw/nvram/fw_cfg.h"
#include "pci.h"
#include "trace.h"

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
        !vdev->vga || nr != 4) {
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
        !vdev->vga || nr != 2 || !vdev->bars[2].mem64) {
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
        !vdev->vga || nr != 5 || !vdev->bars[5].ioport) {
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
    if (vdev->vga) {
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
    uint64_t data = vfio_region_read(&vdev->bars[2].region, addr + 0x70, size);

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
 * Intel IGD support
 *
 * Obviously IGD is not a discrete device, this is evidenced not only by it
 * being integrated into the CPU, but by the various chipset and BIOS
 * dependencies that it brings along with it.  Intel is trying to move away
 * from this and Broadwell and newer devices can run in what Intel calls
 * "Universal Pass-Through" mode, or UPT.  Theoretically in UPT mode, nothing
 * more is required beyond assigning the IGD device to a VM.  There are
 * however support limitations to this mode.  It only supports IGD as a
 * secondary graphics device in the VM and it doesn't officially support any
 * physical outputs.
 *
 * The code here attempts to enable what we'll call legacy mode assignment,
 * IGD retains most of the capabilities we expect for it to have on bare
 * metal.  To enable this mode, the IGD device must be assigned to the VM
 * at PCI address 00:02.0, it must have a ROM, it very likely needs VGA
 * support, we must have VM BIOS support for reserving and populating some
 * of the required tables, and we need to tweak the chipset with revisions
 * and IDs and an LPC/ISA bridge device.  The intention is to make all of
 * this happen automatically by installing the device at the correct VM PCI
 * bus address.  If any of the conditions are not met, we cross our fingers
 * and hope the user knows better.
 *
 * NB - It is possible to enable physical outputs in UPT mode by supplying
 * an OpRegion table.  We don't do this by default because the guest driver
 * behaves differently if an OpRegion is provided and no monitor is attached
 * vs no OpRegion and a monitor being attached or not.  Effectively, if a
 * headless setup is desired, the OpRegion gets in the way of that.
 */

/*
 * This presumes the device is already known to be an Intel VGA device, so we
 * take liberties in which device ID bits match which generation.  This should
 * not be taken as an indication that all the devices are supported, or even
 * supportable, some of them don't even support VT-d.
 * See linux:include/drm/i915_pciids.h for IDs.
 */
static int igd_gen(VFIOPCIDevice *vdev)
{
    if ((vdev->device_id & 0xfff) == 0xa84) {
        return 8; /* Broxton */
    }

    switch (vdev->device_id & 0xff00) {
    /* Old, untested, unavailable, unknown */
    case 0x0000:
    case 0x2500:
    case 0x2700:
    case 0x2900:
    case 0x2a00:
    case 0x2e00:
    case 0x3500:
    case 0xa000:
        return -1;
    /* SandyBridge, IvyBridge, ValleyView, Haswell */
    case 0x0100:
    case 0x0400:
    case 0x0a00:
    case 0x0c00:
    case 0x0d00:
    case 0x0f00:
        return 6;
    /* BroadWell, CherryView, SkyLake, KabyLake */
    case 0x1600:
    case 0x1900:
    case 0x2200:
    case 0x5900:
        return 8;
    }

    return 8; /* Assume newer is compatible */
}

typedef struct VFIOIGDQuirk {
    struct VFIOPCIDevice *vdev;
    uint32_t index;
    uint32_t bdsm;
} VFIOIGDQuirk;

#define IGD_GMCH 0x50 /* Graphics Control Register */
#define IGD_BDSM 0x5c /* Base Data of Stolen Memory */
#define IGD_ASLS 0xfc /* ASL Storage Register */

/*
 * The OpRegion includes the Video BIOS Table, which seems important for
 * telling the driver what sort of outputs it has.  Without this, the device
 * may work in the guest, but we may not get output.  This also requires BIOS
 * support to reserve and populate a section of guest memory sufficient for
 * the table and to write the base address of that memory to the ASLS register
 * of the IGD device.
 */
int vfio_pci_igd_opregion_init(VFIOPCIDevice *vdev,
                               struct vfio_region_info *info, Error **errp)
{
    int ret;

    vdev->igd_opregion = g_malloc0(info->size);
    ret = pread(vdev->vbasedev.fd, vdev->igd_opregion,
                info->size, info->offset);
    if (ret != info->size) {
        error_setg(errp, "failed to read IGD OpRegion");
        g_free(vdev->igd_opregion);
        vdev->igd_opregion = NULL;
        return -EINVAL;
    }

    /*
     * Provide fw_cfg with a copy of the OpRegion which the VM firmware is to
     * allocate 32bit reserved memory for, copy these contents into, and write
     * the reserved memory base address to the device ASLS register at 0xFC.
     * Alignment of this reserved region seems flexible, but using a 4k page
     * alignment seems to work well.  This interface assumes a single IGD
     * device, which may be at VM address 00:02.0 in legacy mode or another
     * address in UPT mode.
     *
     * NB, there may be future use cases discovered where the VM should have
     * direct interaction with the host OpRegion, in which case the write to
     * the ASLS register would trigger MemoryRegion setup to enable that.
     */
    fw_cfg_add_file(fw_cfg_find(), "etc/igd-opregion",
                    vdev->igd_opregion, info->size);

    trace_vfio_pci_igd_opregion_enabled(vdev->vbasedev.name);

    pci_set_long(vdev->pdev.config + IGD_ASLS, 0);
    pci_set_long(vdev->pdev.wmask + IGD_ASLS, ~0);
    pci_set_long(vdev->emulated_config_bits + IGD_ASLS, ~0);

    return 0;
}

/*
 * The rather short list of registers that we copy from the host devices.
 * The LPC/ISA bridge values are definitely needed to support the vBIOS, the
 * host bridge values may or may not be needed depending on the guest OS.
 * Since we're only munging revision and subsystem values on the host bridge,
 * we don't require our own device.  The LPC/ISA bridge needs to be our very
 * own though.
 */
typedef struct {
    uint8_t offset;
    uint8_t len;
} IGDHostInfo;

static const IGDHostInfo igd_host_bridge_infos[] = {
    {PCI_REVISION_ID,         2},
    {PCI_SUBSYSTEM_VENDOR_ID, 2},
    {PCI_SUBSYSTEM_ID,        2},
};

static const IGDHostInfo igd_lpc_bridge_infos[] = {
    {PCI_VENDOR_ID,           2},
    {PCI_DEVICE_ID,           2},
    {PCI_REVISION_ID,         2},
    {PCI_SUBSYSTEM_VENDOR_ID, 2},
    {PCI_SUBSYSTEM_ID,        2},
};

static int vfio_pci_igd_copy(VFIOPCIDevice *vdev, PCIDevice *pdev,
                             struct vfio_region_info *info,
                             const IGDHostInfo *list, int len)
{
    int i, ret;

    for (i = 0; i < len; i++) {
        ret = pread(vdev->vbasedev.fd, pdev->config + list[i].offset,
                    list[i].len, info->offset + list[i].offset);
        if (ret != list[i].len) {
            error_report("IGD copy failed: %m");
            return -errno;
        }
    }

    return 0;
}

/*
 * Stuff a few values into the host bridge.
 */
static int vfio_pci_igd_host_init(VFIOPCIDevice *vdev,
                                  struct vfio_region_info *info)
{
    PCIBus *bus;
    PCIDevice *host_bridge;
    int ret;

    bus = pci_device_root_bus(&vdev->pdev);
    host_bridge = pci_find_device(bus, 0, PCI_DEVFN(0, 0));

    if (!host_bridge) {
        error_report("Can't find host bridge");
        return -ENODEV;
    }

    ret = vfio_pci_igd_copy(vdev, host_bridge, info, igd_host_bridge_infos,
                            ARRAY_SIZE(igd_host_bridge_infos));
    if (!ret) {
        trace_vfio_pci_igd_host_bridge_enabled(vdev->vbasedev.name);
    }

    return ret;
}

/*
 * IGD LPC/ISA bridge support code.  The vBIOS needs this, but we can't write
 * arbitrary values into just any bridge, so we must create our own.  We try
 * to handle if the user has created it for us, which they might want to do
 * to enable multifunction so we don't occupy the whole PCI slot.
 */
static void vfio_pci_igd_lpc_bridge_realize(PCIDevice *pdev, Error **errp)
{
    if (pdev->devfn != PCI_DEVFN(0x1f, 0)) {
        error_setg(errp, "VFIO dummy ISA/LPC bridge must have address 1f.0");
    }
}

static void vfio_pci_igd_lpc_bridge_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->desc = "VFIO dummy ISA/LPC bridge for IGD assignment";
    dc->hotpluggable = false;
    k->realize = vfio_pci_igd_lpc_bridge_realize;
    k->class_id = PCI_CLASS_BRIDGE_ISA;
}

static TypeInfo vfio_pci_igd_lpc_bridge_info = {
    .name = "vfio-pci-igd-lpc-bridge",
    .parent = TYPE_PCI_DEVICE,
    .class_init = vfio_pci_igd_lpc_bridge_class_init,
};

static void vfio_pci_igd_register_types(void)
{
    type_register_static(&vfio_pci_igd_lpc_bridge_info);
}

type_init(vfio_pci_igd_register_types)

static int vfio_pci_igd_lpc_init(VFIOPCIDevice *vdev,
                                 struct vfio_region_info *info)
{
    PCIDevice *lpc_bridge;
    int ret;

    lpc_bridge = pci_find_device(pci_device_root_bus(&vdev->pdev),
                                 0, PCI_DEVFN(0x1f, 0));
    if (!lpc_bridge) {
        lpc_bridge = pci_create_simple(pci_device_root_bus(&vdev->pdev),
                                 PCI_DEVFN(0x1f, 0), "vfio-pci-igd-lpc-bridge");
    }

    ret = vfio_pci_igd_copy(vdev, lpc_bridge, info, igd_lpc_bridge_infos,
                            ARRAY_SIZE(igd_lpc_bridge_infos));
    if (!ret) {
        trace_vfio_pci_igd_lpc_bridge_enabled(vdev->vbasedev.name);
    }

    return ret;
}

/*
 * IGD Gen8 and newer support up to 8MB for the GTT and use a 64bit PTE
 * entry, older IGDs use 2MB and 32bit.  Each PTE maps a 4k page.  Therefore
 * we either have 2M/4k * 4 = 2k or 8M/4k * 8 = 16k as the maximum iobar index
 * for programming the GTT.
 *
 * See linux:include/drm/i915_drm.h for shift and mask values.
 */
static int vfio_igd_gtt_max(VFIOPCIDevice *vdev)
{
    uint32_t gmch = vfio_pci_read_config(&vdev->pdev, IGD_GMCH, sizeof(gmch));
    int ggms, gen = igd_gen(vdev);

    gmch = vfio_pci_read_config(&vdev->pdev, IGD_GMCH, sizeof(gmch));
    ggms = (gmch >> (gen < 8 ? 8 : 6)) & 0x3;
    if (gen > 6) {
        ggms = 1 << ggms;
    }

    ggms *= 1024 * 1024;

    return (ggms / (4 * 1024)) * (gen < 8 ? 4 : 8);
}

/*
 * The IGD ROM will make use of stolen memory (GGMS) for support of VESA modes.
 * Somehow the host stolen memory range is used for this, but how the ROM gets
 * it is a mystery, perhaps it's hardcoded into the ROM.  Thankfully though, it
 * reprograms the GTT through the IOBAR where we can trap it and transpose the
 * programming to the VM allocated buffer.  That buffer gets reserved by the VM
 * firmware via the fw_cfg entry added below.  Here we're just monitoring the
 * IOBAR address and data registers to detect a write sequence targeting the
 * GTTADR.  This code is developed by observed behavior and doesn't have a
 * direct spec reference, unfortunately.
 */
static uint64_t vfio_igd_quirk_data_read(void *opaque,
                                         hwaddr addr, unsigned size)
{
    VFIOIGDQuirk *igd = opaque;
    VFIOPCIDevice *vdev = igd->vdev;

    igd->index = ~0;

    return vfio_region_read(&vdev->bars[4].region, addr + 4, size);
}

static void vfio_igd_quirk_data_write(void *opaque, hwaddr addr,
                                      uint64_t data, unsigned size)
{
    VFIOIGDQuirk *igd = opaque;
    VFIOPCIDevice *vdev = igd->vdev;
    uint64_t val = data;
    int gen = igd_gen(vdev);

    /*
     * Programming the GGMS starts at index 0x1 and uses every 4th index (ie.
     * 0x1, 0x5, 0x9, 0xd,...).  For pre-Gen8 each 4-byte write is a whole PTE
     * entry, with 0th bit enable set.  For Gen8 and up, PTEs are 64bit, so
     * entries 0x5 & 0xd are the high dword, in our case zero.  Each PTE points
     * to a 4k page, which we translate to a page from the VM allocated region,
     * pointed to by the BDSM register.  If this is not set, we fail.
     *
     * We trap writes to the full configured GTT size, but we typically only
     * see the vBIOS writing up to (nearly) the 1MB barrier.  In fact it often
     * seems to miss the last entry for an even 1MB GTT.  Doing a gratuitous
     * write of that last entry does work, but is hopefully unnecessary since
     * we clear the previous GTT on initialization.
     */
    if ((igd->index % 4 == 1) && igd->index < vfio_igd_gtt_max(vdev)) {
        if (gen < 8 || (igd->index % 8 == 1)) {
            uint32_t base;

            base = pci_get_long(vdev->pdev.config + IGD_BDSM);
            if (!base) {
                hw_error("vfio-igd: Guest attempted to program IGD GTT before "
                         "BIOS reserved stolen memory.  Unsupported BIOS?");
            }

            val = data - igd->bdsm + base;
        } else {
            val = 0; /* upper 32bits of pte, we only enable below 4G PTEs */
        }

        trace_vfio_pci_igd_bar4_write(vdev->vbasedev.name,
                                      igd->index, data, val);
    }

    vfio_region_write(&vdev->bars[4].region, addr + 4, val, size);

    igd->index = ~0;
}

static const MemoryRegionOps vfio_igd_data_quirk = {
    .read = vfio_igd_quirk_data_read,
    .write = vfio_igd_quirk_data_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static uint64_t vfio_igd_quirk_index_read(void *opaque,
                                          hwaddr addr, unsigned size)
{
    VFIOIGDQuirk *igd = opaque;
    VFIOPCIDevice *vdev = igd->vdev;

    igd->index = ~0;

    return vfio_region_read(&vdev->bars[4].region, addr, size);
}

static void vfio_igd_quirk_index_write(void *opaque, hwaddr addr,
                                       uint64_t data, unsigned size)
{
    VFIOIGDQuirk *igd = opaque;
    VFIOPCIDevice *vdev = igd->vdev;

    igd->index = data;

    vfio_region_write(&vdev->bars[4].region, addr, data, size);
}

static const MemoryRegionOps vfio_igd_index_quirk = {
    .read = vfio_igd_quirk_index_read,
    .write = vfio_igd_quirk_index_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void vfio_probe_igd_bar4_quirk(VFIOPCIDevice *vdev, int nr)
{
    struct vfio_region_info *rom = NULL, *opregion = NULL,
                            *host = NULL, *lpc = NULL;
    VFIOQuirk *quirk;
    VFIOIGDQuirk *igd;
    PCIDevice *lpc_bridge;
    int i, ret, ggms_mb, gms_mb = 0, gen;
    uint64_t *bdsm_size;
    uint32_t gmch;
    uint16_t cmd_orig, cmd;
    Error *err = NULL;

    /*
     * This must be an Intel VGA device at address 00:02.0 for us to even
     * consider enabling legacy mode.  The vBIOS has dependencies on the
     * PCI bus address.
     */
    if (!vfio_pci_is(vdev, PCI_VENDOR_ID_INTEL, PCI_ANY_ID) ||
        !vfio_is_vga(vdev) || nr != 4 ||
        &vdev->pdev != pci_find_device(pci_device_root_bus(&vdev->pdev),
                                       0, PCI_DEVFN(0x2, 0))) {
        return;
    }

    /*
     * We need to create an LPC/ISA bridge at PCI bus address 00:1f.0 that we
     * can stuff host values into, so if there's already one there and it's not
     * one we can hack on, legacy mode is no-go.  Sorry Q35.
     */
    lpc_bridge = pci_find_device(pci_device_root_bus(&vdev->pdev),
                                 0, PCI_DEVFN(0x1f, 0));
    if (lpc_bridge && !object_dynamic_cast(OBJECT(lpc_bridge),
                                           "vfio-pci-igd-lpc-bridge")) {
        error_report("IGD device %s cannot support legacy mode due to existing "
                     "devices at address 1f.0", vdev->vbasedev.name);
        return;
    }

    /*
     * IGD is not a standard, they like to change their specs often.  We
     * only attempt to support back to SandBridge and we hope that newer
     * devices maintain compatibility with generation 8.
     */
    gen = igd_gen(vdev);
    if (gen != 6 && gen != 8) {
        error_report("IGD device %s is unsupported in legacy mode, "
                     "try SandyBridge or newer", vdev->vbasedev.name);
        return;
    }

    /*
     * Most of what we're doing here is to enable the ROM to run, so if
     * there's no ROM, there's no point in setting up this quirk.
     * NB. We only seem to get BIOS ROMs, so a UEFI VM would need CSM support.
     */
    ret = vfio_get_region_info(&vdev->vbasedev,
                               VFIO_PCI_ROM_REGION_INDEX, &rom);
    if ((ret || !rom->size) && !vdev->pdev.romfile) {
        error_report("IGD device %s has no ROM, legacy mode disabled",
                     vdev->vbasedev.name);
        goto out;
    }

    /*
     * Ignore the hotplug corner case, mark the ROM failed, we can't
     * create the devices we need for legacy mode in the hotplug scenario.
     */
    if (vdev->pdev.qdev.hotplugged) {
        error_report("IGD device %s hotplugged, ROM disabled, "
                     "legacy mode disabled", vdev->vbasedev.name);
        vdev->rom_read_failed = true;
        goto out;
    }

    /*
     * Check whether we have all the vfio device specific regions to
     * support legacy mode (added in Linux v4.6).  If not, bail.
     */
    ret = vfio_get_dev_region_info(&vdev->vbasedev,
                        VFIO_REGION_TYPE_PCI_VENDOR_TYPE | PCI_VENDOR_ID_INTEL,
                        VFIO_REGION_SUBTYPE_INTEL_IGD_OPREGION, &opregion);
    if (ret) {
        error_report("IGD device %s does not support OpRegion access,"
                     "legacy mode disabled", vdev->vbasedev.name);
        goto out;
    }

    ret = vfio_get_dev_region_info(&vdev->vbasedev,
                        VFIO_REGION_TYPE_PCI_VENDOR_TYPE | PCI_VENDOR_ID_INTEL,
                        VFIO_REGION_SUBTYPE_INTEL_IGD_HOST_CFG, &host);
    if (ret) {
        error_report("IGD device %s does not support host bridge access,"
                     "legacy mode disabled", vdev->vbasedev.name);
        goto out;
    }

    ret = vfio_get_dev_region_info(&vdev->vbasedev,
                        VFIO_REGION_TYPE_PCI_VENDOR_TYPE | PCI_VENDOR_ID_INTEL,
                        VFIO_REGION_SUBTYPE_INTEL_IGD_LPC_CFG, &lpc);
    if (ret) {
        error_report("IGD device %s does not support LPC bridge access,"
                     "legacy mode disabled", vdev->vbasedev.name);
        goto out;
    }

    gmch = vfio_pci_read_config(&vdev->pdev, IGD_GMCH, 4);

    /*
     * If IGD VGA Disable is clear (expected) and VGA is not already enabled,
     * try to enable it.  Probably shouldn't be using legacy mode without VGA,
     * but also no point in us enabling VGA if disabled in hardware.
     */
    if (!(gmch & 0x2) && !vdev->vga && vfio_populate_vga(vdev, &err)) {
        error_reportf_err(err, ERR_PREFIX, vdev->vbasedev.name);
        error_report("IGD device %s failed to enable VGA access, "
                     "legacy mode disabled", vdev->vbasedev.name);
        goto out;
    }

    /* Create our LPC/ISA bridge */
    ret = vfio_pci_igd_lpc_init(vdev, lpc);
    if (ret) {
        error_report("IGD device %s failed to create LPC bridge, "
                     "legacy mode disabled", vdev->vbasedev.name);
        goto out;
    }

    /* Stuff some host values into the VM PCI host bridge */
    ret = vfio_pci_igd_host_init(vdev, host);
    if (ret) {
        error_report("IGD device %s failed to modify host bridge, "
                     "legacy mode disabled", vdev->vbasedev.name);
        goto out;
    }

    /* Setup OpRegion access */
    ret = vfio_pci_igd_opregion_init(vdev, opregion, &err);
    if (ret) {
        error_append_hint(&err, "IGD legacy mode disabled\n");
        error_reportf_err(err, ERR_PREFIX, vdev->vbasedev.name);
        goto out;
    }

    /* Setup our quirk to munge GTT addresses to the VM allocated buffer */
    quirk = g_malloc0(sizeof(*quirk));
    quirk->mem = g_new0(MemoryRegion, 2);
    quirk->nr_mem = 2;
    igd = quirk->data = g_malloc0(sizeof(*igd));
    igd->vdev = vdev;
    igd->index = ~0;
    igd->bdsm = vfio_pci_read_config(&vdev->pdev, IGD_BDSM, 4);
    igd->bdsm &= ~((1 << 20) - 1); /* 1MB aligned */

    memory_region_init_io(&quirk->mem[0], OBJECT(vdev), &vfio_igd_index_quirk,
                          igd, "vfio-igd-index-quirk", 4);
    memory_region_add_subregion_overlap(vdev->bars[nr].region.mem,
                                        0, &quirk->mem[0], 1);

    memory_region_init_io(&quirk->mem[1], OBJECT(vdev), &vfio_igd_data_quirk,
                          igd, "vfio-igd-data-quirk", 4);
    memory_region_add_subregion_overlap(vdev->bars[nr].region.mem,
                                        4, &quirk->mem[1], 1);

    QLIST_INSERT_HEAD(&vdev->bars[nr].quirks, quirk, next);

    /* Determine the size of stolen memory needed for GTT */
    ggms_mb = (gmch >> (gen < 8 ? 8 : 6)) & 0x3;
    if (gen > 6) {
        ggms_mb = 1 << ggms_mb;
    }

    /*
     * Assume we have no GMS memory, but allow it to be overrided by device
     * option (experimental).  The spec doesn't actually allow zero GMS when
     * when IVD (IGD VGA Disable) is clear, but the claim is that it's unused,
     * so let's not waste VM memory for it.
     */
    gmch &= ~((gen < 8 ? 0x1f : 0xff) << (gen < 8 ? 3 : 8));

    if (vdev->igd_gms) {
        if (vdev->igd_gms <= 0x10) {
            gms_mb = vdev->igd_gms * 32;
            gmch |= vdev->igd_gms << (gen < 8 ? 3 : 8);
        } else {
            error_report("Unsupported IGD GMS value 0x%x", vdev->igd_gms);
            vdev->igd_gms = 0;
        }
    }

    /*
     * Request reserved memory for stolen memory via fw_cfg.  VM firmware
     * must allocate a 1MB aligned reserved memory region below 4GB with
     * the requested size (in bytes) for use by the Intel PCI class VGA
     * device at VM address 00:02.0.  The base address of this reserved
     * memory region must be written to the device BDSM regsiter at PCI
     * config offset 0x5C.
     */
    bdsm_size = g_malloc(sizeof(*bdsm_size));
    *bdsm_size = cpu_to_le64((ggms_mb + gms_mb) * 1024 * 1024);
    fw_cfg_add_file(fw_cfg_find(), "etc/igd-bdsm-size",
                    bdsm_size, sizeof(*bdsm_size));

    /* GMCH is read-only, emulated */
    pci_set_long(vdev->pdev.config + IGD_GMCH, gmch);
    pci_set_long(vdev->pdev.wmask + IGD_GMCH, 0);
    pci_set_long(vdev->emulated_config_bits + IGD_GMCH, ~0);

    /* BDSM is read-write, emulated.  The BIOS needs to be able to write it */
    pci_set_long(vdev->pdev.config + IGD_BDSM, 0);
    pci_set_long(vdev->pdev.wmask + IGD_BDSM, ~0);
    pci_set_long(vdev->emulated_config_bits + IGD_BDSM, ~0);

    /*
     * This IOBAR gives us access to GTTADR, which allows us to write to
     * the GTT itself.  So let's go ahead and write zero to all the GTT
     * entries to avoid spurious DMA faults.  Be sure I/O access is enabled
     * before talking to the device.
     */
    if (pread(vdev->vbasedev.fd, &cmd_orig, sizeof(cmd_orig),
              vdev->config_offset + PCI_COMMAND) != sizeof(cmd_orig)) {
        error_report("IGD device %s - failed to read PCI command register",
                     vdev->vbasedev.name);
    }

    cmd = cmd_orig | PCI_COMMAND_IO;

    if (pwrite(vdev->vbasedev.fd, &cmd, sizeof(cmd),
               vdev->config_offset + PCI_COMMAND) != sizeof(cmd)) {
        error_report("IGD device %s - failed to write PCI command register",
                     vdev->vbasedev.name);
    }

    for (i = 1; i < vfio_igd_gtt_max(vdev); i += 4) {
        vfio_region_write(&vdev->bars[4].region, 0, i, 4);
        vfio_region_write(&vdev->bars[4].region, 4, 0, 4);
    }

    if (pwrite(vdev->vbasedev.fd, &cmd_orig, sizeof(cmd_orig),
               vdev->config_offset + PCI_COMMAND) != sizeof(cmd_orig)) {
        error_report("IGD device %s - failed to restore PCI command register",
                     vdev->vbasedev.name);
    }

    trace_vfio_pci_igd_bdsm_enabled(vdev->vbasedev.name, ggms_mb + gms_mb);

out:
    g_free(rom);
    g_free(opregion);
    g_free(host);
    g_free(lpc);
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
    vfio_probe_igd_bar4_quirk(vdev, nr);
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

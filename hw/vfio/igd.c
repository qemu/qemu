/*
 * IGD device quirks
 *
 * Copyright Red Hat, Inc. 2016
 *
 * Authors:
 *  Alex Williamson <alex.williamson@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/nvram/fw_cfg.h"
#include "pci.h"
#include "trace.h"

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

static const TypeInfo vfio_pci_igd_lpc_bridge_info = {
    .name = "vfio-pci-igd-lpc-bridge",
    .parent = TYPE_PCI_DEVICE,
    .class_init = vfio_pci_igd_lpc_bridge_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
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

    ggms *= MiB;

    return (ggms / (4 * KiB)) * (gen < 8 ? 4 : 8);
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

void vfio_probe_igd_bar4_quirk(VFIOPCIDevice *vdev, int nr)
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
        error_reportf_err(err, VFIO_MSG_PREFIX, vdev->vbasedev.name);
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
        error_reportf_err(err, VFIO_MSG_PREFIX, vdev->vbasedev.name);
        goto out;
    }

    /* Setup our quirk to munge GTT addresses to the VM allocated buffer */
    quirk = vfio_quirk_alloc(2);
    igd = quirk->data = g_malloc0(sizeof(*igd));
    igd->vdev = vdev;
    igd->index = ~0;
    igd->bdsm = vfio_pci_read_config(&vdev->pdev, IGD_BDSM, 4);
    igd->bdsm &= ~((1 * MiB) - 1); /* 1MB aligned */

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
     * Assume we have no GMS memory, but allow it to be overridden by device
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
     * memory region must be written to the device BDSM register at PCI
     * config offset 0x5C.
     */
    bdsm_size = g_malloc(sizeof(*bdsm_size));
    *bdsm_size = cpu_to_le64((ggms_mb + gms_mb) * MiB);
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

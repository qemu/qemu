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
#include "qapi/qmp/qerror.h"
#include "hw/boards.h"
#include "hw/hw.h"
#include "hw/nvram/fw_cfg.h"
#include "pci.h"
#include "pci-quirks.h"
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
    /*
     * Device IDs for Broxton/Apollo Lake are 0x0a84, 0x1a84, 0x1a85, 0x5a84
     * and 0x5a85, match bit 11:1 here
     * Prefix 0x0a is taken by Haswell, this rule should be matched first.
     */
    if ((vdev->device_id & 0xffe) == 0xa84) {
        return 9;
    }

    switch (vdev->device_id & 0xff00) {
    case 0x0100:    /* SandyBridge, IvyBridge */
        return 6;
    case 0x0400:    /* Haswell */
    case 0x0a00:    /* Haswell */
    case 0x0c00:    /* Haswell */
    case 0x0d00:    /* Haswell */
    case 0x0f00:    /* Valleyview/Bay Trail */
        return 7;
    case 0x1600:    /* Broadwell */
    case 0x2200:    /* Cherryview */
        return 8;
    case 0x1900:    /* Skylake */
    case 0x3100:    /* Gemini Lake */
    case 0x5900:    /* Kaby Lake */
    case 0x3e00:    /* Coffee Lake */
    case 0x9B00:    /* Comet Lake */
        return 9;
    case 0x8A00:    /* Ice Lake */
    case 0x4500:    /* Elkhart Lake */
    case 0x4E00:    /* Jasper Lake */
        return 11;
    case 0x9A00:    /* Tiger Lake */
    case 0x4C00:    /* Rocket Lake */
    case 0x4600:    /* Alder Lake */
    case 0xA700:    /* Raptor Lake */
        return 12;
    }

    /*
     * Unfortunately, Intel changes it's specification quite often. This makes
     * it impossible to use a suitable default value for unknown devices.
     * Return -1 for not applying any generation-specific quirks.
     */
    return -1;
}

#define IGD_ASLS 0xfc /* ASL Storage Register */
#define IGD_GMCH 0x50 /* Graphics Control Register */
#define IGD_BDSM 0x5c /* Base Data of Stolen Memory */
#define IGD_BDSM_GEN11 0xc0 /* Base Data of Stolen Memory of gen 11 and later */

#define IGD_GMCH_VGA_DISABLE        BIT(1)
#define IGD_GMCH_GEN6_GMS_SHIFT     3       /* SNB_GMCH in i915 */
#define IGD_GMCH_GEN6_GMS_MASK      0x1f
#define IGD_GMCH_GEN8_GMS_SHIFT     8       /* BDW_GMCH in i915 */
#define IGD_GMCH_GEN8_GMS_MASK      0xff

static uint64_t igd_stolen_memory_size(int gen, uint32_t gmch)
{
    uint64_t gms;

    if (gen < 8) {
        gms = (gmch >> IGD_GMCH_GEN6_GMS_SHIFT) & IGD_GMCH_GEN6_GMS_MASK;
    } else {
        gms = (gmch >> IGD_GMCH_GEN8_GMS_SHIFT) & IGD_GMCH_GEN8_GMS_MASK;
    }

    if (gen < 9) {
            return gms * 32 * MiB;
    } else {
        if (gms < 0xf0) {
            return gms * 32 * MiB;
        } else {
            return (gms - 0xf0 + 1) * 4 * MiB;
        }
    }

    return 0;
}

/*
 * The OpRegion includes the Video BIOS Table, which seems important for
 * telling the driver what sort of outputs it has.  Without this, the device
 * may work in the guest, but we may not get output.  This also requires BIOS
 * support to reserve and populate a section of guest memory sufficient for
 * the table and to write the base address of that memory to the ASLS register
 * of the IGD device.
 */
static bool vfio_pci_igd_opregion_init(VFIOPCIDevice *vdev,
                                       struct vfio_region_info *info,
                                       Error **errp)
{
    int ret;

    vdev->igd_opregion = g_malloc0(info->size);
    ret = pread(vdev->vbasedev.fd, vdev->igd_opregion,
                info->size, info->offset);
    if (ret != info->size) {
        error_setg(errp, "failed to read IGD OpRegion");
        g_free(vdev->igd_opregion);
        vdev->igd_opregion = NULL;
        return false;
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

    return true;
}

static bool vfio_pci_igd_opregion_detect(VFIOPCIDevice *vdev,
                                         struct vfio_region_info **opregion)
{
    int ret;

    ret = vfio_device_get_region_info_type(&vdev->vbasedev,
                    VFIO_REGION_TYPE_PCI_VENDOR_TYPE | PCI_VENDOR_ID_INTEL,
                    VFIO_REGION_SUBTYPE_INTEL_IGD_OPREGION, opregion);
    if (ret) {
        return false;
    }

    /* Hotplugging is not supported for opregion access */
    if (DEVICE(vdev)->hotplugged) {
        warn_report("IGD device detected, but OpRegion is not supported "
                    "on hotplugged device.");
        return false;
    }

    return true;
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
    PCIDevice *pdev = PCI_DEVICE(vdev);
    PCIBus *bus;
    PCIDevice *host_bridge;
    int ret;

    bus = pci_device_root_bus(pdev);
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

static void vfio_pci_igd_lpc_bridge_class_init(ObjectClass *klass,
                                               const void *data)
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
    .interfaces = (const InterfaceInfo[]) {
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
    PCIDevice *pdev = PCI_DEVICE(vdev);
    PCIDevice *lpc_bridge;
    int ret;

    lpc_bridge = pci_find_device(pci_device_root_bus(pdev),
                                 0, PCI_DEVFN(0x1f, 0));
    if (!lpc_bridge) {
        lpc_bridge = pci_create_simple(pci_device_root_bus(pdev),
                                 PCI_DEVFN(0x1f, 0), "vfio-pci-igd-lpc-bridge");
    }

    ret = vfio_pci_igd_copy(vdev, lpc_bridge, info, igd_lpc_bridge_infos,
                            ARRAY_SIZE(igd_lpc_bridge_infos));
    if (!ret) {
        trace_vfio_pci_igd_lpc_bridge_enabled(vdev->vbasedev.name);
    }

    return ret;
}

static bool vfio_pci_igd_setup_lpc_bridge(VFIOPCIDevice *vdev, Error **errp)
{
    struct vfio_region_info *host = NULL;
    struct vfio_region_info *lpc = NULL;
    PCIDevice *pdev = PCI_DEVICE(vdev);
    PCIDevice *lpc_bridge;
    int ret;

    /*
     * Copying IDs or creating new devices are not supported on hotplug
     */
    if (DEVICE(vdev)->hotplugged) {
        error_setg(errp, "IGD LPC is not supported on hotplugged device");
        return false;
    }

    /*
     * We need to create an LPC/ISA bridge at PCI bus address 00:1f.0 that we
     * can stuff host values into, so if there's already one there and it's not
     * one we can hack on, this quirk is no-go.  Sorry Q35.
     */
    lpc_bridge = pci_find_device(pci_device_root_bus(pdev),
                                 0, PCI_DEVFN(0x1f, 0));
    if (lpc_bridge && !object_dynamic_cast(OBJECT(lpc_bridge),
                                           "vfio-pci-igd-lpc-bridge")) {
        error_setg(errp,
                   "Cannot create LPC bridge due to existing device at 1f.0");
        return false;
    }

    /*
     * Check whether we have all the vfio device specific regions to
     * support LPC quirk (added in Linux v4.6).
     */
    ret = vfio_device_get_region_info_type(&vdev->vbasedev,
                        VFIO_REGION_TYPE_PCI_VENDOR_TYPE | PCI_VENDOR_ID_INTEL,
                        VFIO_REGION_SUBTYPE_INTEL_IGD_LPC_CFG, &lpc);
    if (ret) {
        error_setg(errp, "IGD LPC bridge access is not supported by kernel");
        return false;
    }

    ret = vfio_device_get_region_info_type(&vdev->vbasedev,
                        VFIO_REGION_TYPE_PCI_VENDOR_TYPE | PCI_VENDOR_ID_INTEL,
                        VFIO_REGION_SUBTYPE_INTEL_IGD_HOST_CFG, &host);
    if (ret) {
        error_setg(errp, "IGD host bridge access is not supported by kernel");
        return false;
    }

    /* Create/modify LPC bridge */
    ret = vfio_pci_igd_lpc_init(vdev, lpc);
    if (ret) {
        error_setg(errp, "Failed to create/modify LPC bridge for IGD");
        return false;
    }

    /* Stuff some host values into the VM PCI host bridge */
    ret = vfio_pci_igd_host_init(vdev, host);
    if (ret) {
        error_setg(errp, "Failed to modify host bridge for IGD");
        return false;
    }

    return true;
}

static bool vfio_pci_igd_override_gms(int gen, uint32_t gms, uint32_t *gmch)
{
    bool ret = false;

    if (gen == -1) {
        error_report("x-igd-gms is not supported on this device");
    } else if (gen < 8) {
        if (gms <= 0x10) {
            *gmch &= ~(IGD_GMCH_GEN6_GMS_MASK << IGD_GMCH_GEN6_GMS_SHIFT);
            *gmch |= gms << IGD_GMCH_GEN6_GMS_SHIFT;
            ret = true;
        } else {
            error_report(QERR_INVALID_PARAMETER_VALUE, "x-igd-gms", "0~0x10");
        }
    } else if (gen == 8) {
        if (gms <= 0x40) {
            *gmch &= ~(IGD_GMCH_GEN8_GMS_MASK << IGD_GMCH_GEN8_GMS_SHIFT);
            *gmch |= gms << IGD_GMCH_GEN8_GMS_SHIFT;
            ret = true;
        } else {
            error_report(QERR_INVALID_PARAMETER_VALUE, "x-igd-gms", "0~0x40");
        }
    } else {
        /* 0x0  to 0x40: 32MB increments starting at 0MB */
        /* 0xf0 to 0xfe: 4MB increments starting at 4MB */
        if ((gms <= 0x40) || (gms >= 0xf0 && gms <= 0xfe)) {
            *gmch &= ~(IGD_GMCH_GEN8_GMS_MASK << IGD_GMCH_GEN8_GMS_SHIFT);
            *gmch |= gms << IGD_GMCH_GEN8_GMS_SHIFT;
            ret = true;
        } else {
            error_report(QERR_INVALID_PARAMETER_VALUE,
                         "x-igd-gms", "0~0x40 or 0xf0~0xfe");
        }
    }

    return ret;
}

#define IGD_GGC_MMIO_OFFSET     0x108040
#define IGD_BDSM_MMIO_OFFSET    0x1080C0

void vfio_probe_igd_bar0_quirk(VFIOPCIDevice *vdev, int nr)
{
    VFIOQuirk *ggc_quirk, *bdsm_quirk;
    VFIOConfigMirrorQuirk *ggc_mirror, *bdsm_mirror;
    int gen;

    if (!vfio_pci_is(vdev, PCI_VENDOR_ID_INTEL, PCI_ANY_ID) ||
        !vfio_is_base_display(vdev) || nr != 0) {
        return;
    }

    /* Only on IGD Gen6-12 device needs quirks in BAR 0 */
    gen = igd_gen(vdev);
    if (gen < 6) {
        return;
    }

    if (vdev->igd_gms) {
        ggc_quirk = vfio_quirk_alloc(1);
        ggc_mirror = ggc_quirk->data = g_malloc0(sizeof(*ggc_mirror));
        ggc_mirror->mem = ggc_quirk->mem;
        ggc_mirror->vdev = vdev;
        ggc_mirror->bar = nr;
        ggc_mirror->offset = IGD_GGC_MMIO_OFFSET;
        ggc_mirror->config_offset = IGD_GMCH;

        memory_region_init_io(ggc_mirror->mem, OBJECT(vdev),
                              &vfio_generic_mirror_quirk, ggc_mirror,
                              "vfio-igd-ggc-quirk", 2);
        memory_region_add_subregion_overlap(vdev->bars[nr].region.mem,
                                            ggc_mirror->offset, ggc_mirror->mem,
                                            1);

        QLIST_INSERT_HEAD(&vdev->bars[nr].quirks, ggc_quirk, next);
    }

    bdsm_quirk = vfio_quirk_alloc(1);
    bdsm_mirror = bdsm_quirk->data = g_malloc0(sizeof(*bdsm_mirror));
    bdsm_mirror->mem = bdsm_quirk->mem;
    bdsm_mirror->vdev = vdev;
    bdsm_mirror->bar = nr;
    bdsm_mirror->offset = IGD_BDSM_MMIO_OFFSET;
    bdsm_mirror->config_offset = (gen < 11) ? IGD_BDSM : IGD_BDSM_GEN11;

    memory_region_init_io(bdsm_mirror->mem, OBJECT(vdev),
                          &vfio_generic_mirror_quirk, bdsm_mirror,
                          "vfio-igd-bdsm-quirk", (gen < 11) ? 4 : 8);
    memory_region_add_subregion_overlap(vdev->bars[nr].region.mem,
                                        bdsm_mirror->offset, bdsm_mirror->mem,
                                        1);

    QLIST_INSERT_HEAD(&vdev->bars[nr].quirks, bdsm_quirk, next);
}

static bool vfio_pci_igd_config_quirk(VFIOPCIDevice *vdev, Error **errp)
{
    struct vfio_region_info *opregion = NULL;
    PCIDevice *pdev = PCI_DEVICE(vdev);
    int ret, gen;
    uint64_t gms_size = 0;
    uint64_t *bdsm_size;
    uint32_t gmch;
    bool legacy_mode_enabled = false;
    Error *err = NULL;

    if (!vfio_pci_is(vdev, PCI_VENDOR_ID_INTEL, PCI_ANY_ID) ||
        !vfio_is_base_display(vdev)) {
        return true;
    }

    /* IGD device always comes with OpRegion */
    if (!vfio_pci_igd_opregion_detect(vdev, &opregion)) {
        return true;
    }
    info_report("OpRegion detected on Intel display %x.", vdev->device_id);

    gen = igd_gen(vdev);
    gmch = vfio_pci_read_config(pdev, IGD_GMCH, 4);

    /*
     * For backward compatibility, enable legacy mode when
     * - Device geneation is 6 to 9 (including both)
     * - IGD exposes itself as VGA controller and claims VGA cycles on host
     * - Machine type is i440fx (pc_piix)
     * - IGD device is at guest BDF 00:02.0
     * - Not manually disabled by x-igd-legacy-mode=off
     */
    if ((vdev->igd_legacy_mode != ON_OFF_AUTO_OFF) &&
        vfio_is_vga(vdev) &&
        (gen >= 6 && gen <= 9) &&
        !(gmch & IGD_GMCH_VGA_DISABLE) &&
        !strcmp(MACHINE_GET_CLASS(qdev_get_machine())->family, "pc_piix") &&
        (pdev == pci_find_device(pci_device_root_bus(pdev),
        0, PCI_DEVFN(0x2, 0)))) {
        /*
         * IGD legacy mode requires:
         * - VBIOS in ROM BAR or file
         * - VGA IO/MMIO ranges are claimed by IGD
         * - OpRegion
         * - Same LPC bridge and Host bridge VID/DID/SVID/SSID as host
         */
        struct vfio_region_info *rom = NULL;

        legacy_mode_enabled = true;
        info_report("IGD legacy mode enabled, "
                    "use x-igd-legacy-mode=off to disable it if unwanted.");

        /*
         * Most of what we're doing here is to enable the ROM to run, so if
         * there's no ROM, there's no point in setting up this quirk.
         * NB. We only seem to get BIOS ROMs, so UEFI VM would need CSM support.
         */
        ret = vfio_device_get_region_info(&vdev->vbasedev,
                                          VFIO_PCI_ROM_REGION_INDEX, &rom);
        if ((ret || !rom->size) && !pdev->romfile) {
            error_setg(&err, "Device has no ROM");
            goto error;
        }

        /*
         * If VGA is not already enabled, try to enable it. We shouldn't be
         * using legacy mode without VGA.
         */
        if (!vdev->vga) {
            if (vfio_populate_vga(vdev, &err)) {
                vfio_pci_config_register_vga(vdev);
            } else {
                error_setg(&err, "Unable to enable VGA access");
                goto error;
            }
        }

        /* Enable OpRegion and LPC bridge quirk */
        vdev->features |= VFIO_FEATURE_ENABLE_IGD_OPREGION;
        vdev->features |= VFIO_FEATURE_ENABLE_IGD_LPC;
    } else if (vdev->igd_legacy_mode == ON_OFF_AUTO_ON) {
        error_setg(&err,
                   "Machine is not i440fx, assigned BDF is not 00:02.0, "
                   "or device %04x (gen %d) doesn't support legacy mode",
                   vdev->device_id, gen);
        goto error;
    }

    /* Setup OpRegion access */
    if ((vdev->features & VFIO_FEATURE_ENABLE_IGD_OPREGION) &&
        !vfio_pci_igd_opregion_init(vdev, opregion, errp)) {
        goto error;
    }

    /* Setup LPC bridge / Host bridge PCI IDs */
    if ((vdev->features & VFIO_FEATURE_ENABLE_IGD_LPC) &&
        !vfio_pci_igd_setup_lpc_bridge(vdev, errp)) {
        goto error;
    }

    /*
     * ASLS (OpRegion address) is read-only, emulated
     * It contains HPA, guest firmware need to reprogram it with GPA.
     */
    pci_set_long(pdev->config + IGD_ASLS, 0);
    pci_set_long(pdev->wmask + IGD_ASLS, ~0);
    pci_set_long(vdev->emulated_config_bits + IGD_ASLS, ~0);

    /*
     * Allow user to override dsm size using x-igd-gms option, in multiples of
     * 32MiB. This option should only be used when the desired size cannot be
     * set from DVMT Pre-Allocated option in host BIOS.
     */
    if (vdev->igd_gms) {
        if (!vfio_pci_igd_override_gms(gen, vdev->igd_gms, &gmch)) {
            return false;
        }

        /* GMCH is read-only, emulated */
        pci_set_long(pdev->config + IGD_GMCH, gmch);
        pci_set_long(pdev->wmask + IGD_GMCH, 0);
        pci_set_long(vdev->emulated_config_bits + IGD_GMCH, ~0);
    }

    if (gen > 0) {
        gms_size = igd_stolen_memory_size(gen, gmch);

        /* BDSM is read-write, emulated. BIOS needs to be able to write it */
        if (gen < 11) {
            pci_set_long(pdev->config + IGD_BDSM, 0);
            pci_set_long(pdev->wmask + IGD_BDSM, ~0);
            pci_set_long(vdev->emulated_config_bits + IGD_BDSM, ~0);
        } else {
            pci_set_quad(pdev->config + IGD_BDSM_GEN11, 0);
            pci_set_quad(pdev->wmask + IGD_BDSM_GEN11, ~0);
            pci_set_quad(vdev->emulated_config_bits + IGD_BDSM_GEN11, ~0);
        }
    }

    /*
     * Request reserved memory for stolen memory via fw_cfg.  VM firmware
     * must allocate a 1MB aligned reserved memory region below 4GB with
     * the requested size (in bytes) for use by the IGD device. The base
     * address of this reserved memory region must be written to the
     * device BDSM register.
     * For newer device without BDSM register, this fw_cfg item is 0.
     */
    bdsm_size = g_malloc(sizeof(*bdsm_size));
    *bdsm_size = cpu_to_le64(gms_size);
    fw_cfg_add_file(fw_cfg_find(), "etc/igd-bdsm-size",
                    bdsm_size, sizeof(*bdsm_size));

    trace_vfio_pci_igd_bdsm_enabled(vdev->vbasedev.name, (gms_size / MiB));

    return true;

error:
    /*
     * When legacy mode is implicity enabled, continue on error,
     * to keep compatibility
     */
    if (legacy_mode_enabled && (vdev->igd_legacy_mode == ON_OFF_AUTO_AUTO)) {
        error_report_err(err);
        error_report("IGD legacy mode disabled");
        return true;
    }

    error_propagate(errp, err);
    return false;
}

/*
 * KVMGT/GVT-g vGPU exposes an emulated OpRegion. So far, users have to specify
 * x-igd-opregion=on to enable the access.
 * TODO: Check VID/DID and enable opregion access automatically
 */
static bool vfio_pci_kvmgt_config_quirk(VFIOPCIDevice *vdev, Error **errp)
{
    struct vfio_region_info *opregion = NULL;
    int gen;

    if (!vfio_pci_is(vdev, PCI_VENDOR_ID_INTEL, PCI_ANY_ID) ||
        !vfio_is_vga(vdev)) {
        return true;
    }

    /* FIXME: Cherryview is Gen8, but don't support GVT-g */
    gen = igd_gen(vdev);
    if (gen != 8 && gen != 9) {
        return true;
    }

    if (!vfio_pci_igd_opregion_detect(vdev, &opregion)) {
        /* Should never reach here, KVMGT always emulates OpRegion */
        return false;
    }

    if ((vdev->features & VFIO_FEATURE_ENABLE_IGD_OPREGION) &&
        !vfio_pci_igd_opregion_init(vdev, opregion, errp)) {
        return false;
    }

    return true;
}

bool vfio_probe_igd_config_quirk(VFIOPCIDevice *vdev, Error **errp)
{
    /* KVMGT/GVT-g vGPU is exposed as mdev */
    if (vdev->vbasedev.mdev) {
        return vfio_pci_kvmgt_config_quirk(vdev, errp);
    }

    return vfio_pci_igd_config_quirk(vdev, errp);
}

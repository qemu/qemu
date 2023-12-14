/*
 * graphics passthrough
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "xen_pt.h"
#include "xen-host-pci-device.h"

static unsigned long igd_guest_opregion;
static unsigned long igd_host_opregion;

#define XEN_PCI_INTEL_OPREGION_MASK 0xfff

typedef struct VGARegion {
    int type;           /* Memory or port I/O */
    uint64_t guest_base_addr;
    uint64_t machine_base_addr;
    uint64_t size;    /* size of the region */
    int rc;
} VGARegion;

#define IORESOURCE_IO           0x00000100
#define IORESOURCE_MEM          0x00000200

static struct VGARegion vga_args[] = {
    {
        .type = IORESOURCE_IO,
        .guest_base_addr = 0x3B0,
        .machine_base_addr = 0x3B0,
        .size = 0xC,
        .rc = -1,
    },
    {
        .type = IORESOURCE_IO,
        .guest_base_addr = 0x3C0,
        .machine_base_addr = 0x3C0,
        .size = 0x20,
        .rc = -1,
    },
    {
        .type = IORESOURCE_MEM,
        .guest_base_addr = 0xa0000 >> XC_PAGE_SHIFT,
        .machine_base_addr = 0xa0000 >> XC_PAGE_SHIFT,
        .size = 0x20,
        .rc = -1,
    },
};

/*
 * register VGA resources for the domain with assigned gfx
 */
int xen_pt_register_vga_regions(XenHostPCIDevice *dev)
{
    int i = 0;

    if (!is_igd_vga_passthrough(dev)) {
        return 0;
    }

    for (i = 0 ; i < ARRAY_SIZE(vga_args); i++) {
        if (vga_args[i].type == IORESOURCE_IO) {
            vga_args[i].rc = xc_domain_ioport_mapping(xen_xc, xen_domid,
                            vga_args[i].guest_base_addr,
                            vga_args[i].machine_base_addr,
                            vga_args[i].size, DPCI_ADD_MAPPING);
        } else {
            vga_args[i].rc = xc_domain_memory_mapping(xen_xc, xen_domid,
                            vga_args[i].guest_base_addr,
                            vga_args[i].machine_base_addr,
                            vga_args[i].size, DPCI_ADD_MAPPING);
        }

        if (vga_args[i].rc) {
            XEN_PT_ERR(NULL, "VGA %s mapping failed! (rc: %i)\n",
                    vga_args[i].type == IORESOURCE_IO ? "ioport" : "memory",
                    vga_args[i].rc);
            return vga_args[i].rc;
        }
    }

    return 0;
}

/*
 * unregister VGA resources for the domain with assigned gfx
 */
int xen_pt_unregister_vga_regions(XenHostPCIDevice *dev)
{
    int i = 0;
    int ret = 0;

    if (!is_igd_vga_passthrough(dev)) {
        return 0;
    }

    for (i = 0 ; i < ARRAY_SIZE(vga_args); i++) {
        if (vga_args[i].type == IORESOURCE_IO) {
            vga_args[i].rc = xc_domain_ioport_mapping(xen_xc, xen_domid,
                            vga_args[i].guest_base_addr,
                            vga_args[i].machine_base_addr,
                            vga_args[i].size, DPCI_REMOVE_MAPPING);
        } else {
            vga_args[i].rc = xc_domain_memory_mapping(xen_xc, xen_domid,
                            vga_args[i].guest_base_addr,
                            vga_args[i].machine_base_addr,
                            vga_args[i].size, DPCI_REMOVE_MAPPING);
        }

        if (vga_args[i].rc) {
            XEN_PT_ERR(NULL, "VGA %s unmapping failed! (rc: %i)\n",
                    vga_args[i].type == IORESOURCE_IO ? "ioport" : "memory",
                    vga_args[i].rc);
            return vga_args[i].rc;
        }
    }

    if (igd_guest_opregion) {
        ret = xc_domain_memory_mapping(xen_xc, xen_domid,
                (unsigned long)(igd_guest_opregion >> XC_PAGE_SHIFT),
                (unsigned long)(igd_host_opregion >> XC_PAGE_SHIFT),
                3,
                DPCI_REMOVE_MAPPING);
        if (ret) {
            return ret;
        }
    }

    return 0;
}

static void *get_vgabios(XenPCIPassthroughState *s, int *size,
                       XenHostPCIDevice *dev)
{
    return pci_assign_dev_load_option_rom(&s->dev, size,
                                          dev->domain, dev->bus,
                                          dev->dev, dev->func);
}

/* Refer to Seabios. */
struct rom_header {
    uint16_t signature;
    uint8_t size;
    uint8_t initVector[4];
    uint8_t reserved[17];
    uint16_t pcioffset;
    uint16_t pnpoffset;
} __attribute__((packed));

struct pci_data {
    uint32_t signature;
    uint16_t vendor;
    uint16_t device;
    uint16_t vitaldata;
    uint16_t dlen;
    uint8_t drevision;
    uint8_t class_lo;
    uint16_t class_hi;
    uint16_t ilen;
    uint16_t irevision;
    uint8_t type;
    uint8_t indicator;
    uint16_t reserved;
} __attribute__((packed));

void xen_pt_setup_vga(XenPCIPassthroughState *s, XenHostPCIDevice *dev,
                     Error **errp)
{
    unsigned char *bios = NULL;
    struct rom_header *rom;
    int bios_size;
    char *c = NULL;
    char checksum = 0;
    uint32_t len = 0;
    struct pci_data *pd = NULL;

    if (!is_igd_vga_passthrough(dev)) {
        error_setg(errp, "Need to enable igd-passthrough");
        return;
    }

    bios = get_vgabios(s, &bios_size, dev);
    if (!bios) {
        error_setg(errp, "VGA: Can't get VBIOS");
        return;
    }

    if (bios_size < sizeof(struct rom_header)) {
        error_setg(errp, "VGA: VBIOS image corrupt (too small)");
        return;
    }

    /* Currently we fixed this address as a primary. */
    rom = (struct rom_header *)bios;

    if (rom->pcioffset + sizeof(struct pci_data) > bios_size) {
        error_setg(errp, "VGA: VBIOS image corrupt (bad pcioffset field)");
        return;
    }

    pd = (void *)(bios + (unsigned char)rom->pcioffset);

    /* We may need to fixup Device Identification. */
    if (pd->device != s->real_device.device_id) {
        pd->device = s->real_device.device_id;

        len = rom->size * 512;
        if (len > bios_size) {
            error_setg(errp, "VGA: VBIOS image corrupt (bad size field)");
            return;
        }

        /* Then adjust the bios checksum */
        for (c = (char *)bios; c < ((char *)bios + len); c++) {
            checksum += *c;
        }
        if (checksum) {
            bios[len - 1] -= checksum;
            XEN_PT_LOG(&s->dev, "vga bios checksum is adjusted %x!\n",
                       checksum);
        }
    }

    /* Currently we fixed this address as a primary for legacy BIOS. */
    cpu_physical_memory_write(0xc0000, bios, bios_size);
}

uint32_t igd_read_opregion(XenPCIPassthroughState *s)
{
    uint32_t val = 0;

    if (!igd_guest_opregion) {
        return val;
    }

    val = igd_guest_opregion;

    XEN_PT_LOG(&s->dev, "Read opregion val=%x\n", val);
    return val;
}

#define XEN_PCI_INTEL_OPREGION_PAGES 0x3
#define XEN_PCI_INTEL_OPREGION_ENABLE_ACCESSED 0x1
void igd_write_opregion(XenPCIPassthroughState *s, uint32_t val)
{
    int ret;

    if (igd_guest_opregion) {
        XEN_PT_LOG(&s->dev, "opregion register already been set, ignoring %x\n",
                   val);
        return;
    }

    /* We just work with LE. */
    xen_host_pci_get_block(&s->real_device, XEN_PCI_INTEL_OPREGION,
            (uint8_t *)&igd_host_opregion, 4);
    igd_guest_opregion = (unsigned long)(val & ~XEN_PCI_INTEL_OPREGION_MASK)
                            | (igd_host_opregion & XEN_PCI_INTEL_OPREGION_MASK);

    ret = xc_domain_iomem_permission(xen_xc, xen_domid,
            (unsigned long)(igd_host_opregion >> XC_PAGE_SHIFT),
            XEN_PCI_INTEL_OPREGION_PAGES,
            XEN_PCI_INTEL_OPREGION_ENABLE_ACCESSED);

    if (ret) {
        XEN_PT_ERR(&s->dev, "[%d]:Can't enable to access IGD host opregion:"
                    " 0x%lx.\n", ret,
                    (unsigned long)(igd_host_opregion >> XC_PAGE_SHIFT)),
        igd_guest_opregion = 0;
        return;
    }

    ret = xc_domain_memory_mapping(xen_xc, xen_domid,
            (unsigned long)(igd_guest_opregion >> XC_PAGE_SHIFT),
            (unsigned long)(igd_host_opregion >> XC_PAGE_SHIFT),
            XEN_PCI_INTEL_OPREGION_PAGES,
            DPCI_ADD_MAPPING);

    if (ret) {
        XEN_PT_ERR(&s->dev, "[%d]:Can't map IGD host opregion:0x%lx to"
                    " guest opregion:0x%lx.\n", ret,
                    (unsigned long)(igd_host_opregion >> XC_PAGE_SHIFT),
                    (unsigned long)(igd_guest_opregion >> XC_PAGE_SHIFT));
        igd_guest_opregion = 0;
        return;
    }

    XEN_PT_LOG(&s->dev, "Map OpRegion: 0x%lx -> 0x%lx\n",
                    (unsigned long)(igd_host_opregion >> XC_PAGE_SHIFT),
                    (unsigned long)(igd_guest_opregion >> XC_PAGE_SHIFT));
}

typedef struct {
    uint16_t gpu_device_id;
    uint16_t pch_device_id;
    uint8_t pch_revision_id;
} IGDDeviceIDInfo;

/*
 * In real world different GPU should have different PCH. But actually
 * the different PCH DIDs likely map to different PCH SKUs. We do the
 * same thing for the GPU. For PCH, the different SKUs are going to be
 * all the same silicon design and implementation, just different
 * features turn on and off with fuses. The SW interfaces should be
 * consistent across all SKUs in a given family (eg LPT). But just same
 * features may not be supported.
 *
 * Most of these different PCH features probably don't matter to the
 * Gfx driver, but obviously any difference in display port connections
 * will so it should be fine with any PCH in case of passthrough.
 *
 * So currently use one PCH version, 0x8c4e, to cover all HSW(Haswell)
 * scenarios, 0x9cc3 for BDW(Broadwell).
 */
static const IGDDeviceIDInfo igd_combo_id_infos[] = {
    /* HSW Classic */
    {0x0402, 0x8c4e, 0x04}, /* HSWGT1D, HSWD_w7 */
    {0x0406, 0x8c4e, 0x04}, /* HSWGT1M, HSWM_w7 */
    {0x0412, 0x8c4e, 0x04}, /* HSWGT2D, HSWD_w7 */
    {0x0416, 0x8c4e, 0x04}, /* HSWGT2M, HSWM_w7 */
    {0x041E, 0x8c4e, 0x04}, /* HSWGT15D, HSWD_w7 */
    /* HSW ULT */
    {0x0A06, 0x8c4e, 0x04}, /* HSWGT1UT, HSWM_w7 */
    {0x0A16, 0x8c4e, 0x04}, /* HSWGT2UT, HSWM_w7 */
    {0x0A26, 0x8c4e, 0x06}, /* HSWGT3UT, HSWM_w7 */
    {0x0A2E, 0x8c4e, 0x04}, /* HSWGT3UT28W, HSWM_w7 */
    {0x0A1E, 0x8c4e, 0x04}, /* HSWGT2UX, HSWM_w7 */
    {0x0A0E, 0x8c4e, 0x04}, /* HSWGT1ULX, HSWM_w7 */
    /* HSW CRW */
    {0x0D26, 0x8c4e, 0x04}, /* HSWGT3CW, HSWM_w7 */
    {0x0D22, 0x8c4e, 0x04}, /* HSWGT3CWDT, HSWD_w7 */
    /* HSW Server */
    {0x041A, 0x8c4e, 0x04}, /* HSWSVGT2, HSWD_w7 */
    /* HSW SRVR */
    {0x040A, 0x8c4e, 0x04}, /* HSWSVGT1, HSWD_w7 */
    /* BSW */
    {0x1606, 0x9cc3, 0x03}, /* BDWULTGT1, BDWM_w7 */
    {0x1616, 0x9cc3, 0x03}, /* BDWULTGT2, BDWM_w7 */
    {0x1626, 0x9cc3, 0x03}, /* BDWULTGT3, BDWM_w7 */
    {0x160E, 0x9cc3, 0x03}, /* BDWULXGT1, BDWM_w7 */
    {0x161E, 0x9cc3, 0x03}, /* BDWULXGT2, BDWM_w7 */
    {0x1602, 0x9cc3, 0x03}, /* BDWHALOGT1, BDWM_w7 */
    {0x1612, 0x9cc3, 0x03}, /* BDWHALOGT2, BDWM_w7 */
    {0x1622, 0x9cc3, 0x03}, /* BDWHALOGT3, BDWM_w7 */
    {0x162B, 0x9cc3, 0x03}, /* BDWHALO28W, BDWM_w7 */
    {0x162A, 0x9cc3, 0x03}, /* BDWGT3WRKS, BDWM_w7 */
    {0x162D, 0x9cc3, 0x03}, /* BDWGT3SRVR, BDWM_w7 */
};

static void isa_bridge_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    dc->desc        = "ISA bridge faked to support IGD PT";
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    k->vendor_id    = PCI_VENDOR_ID_INTEL;
    k->class_id     = PCI_CLASS_BRIDGE_ISA;
};

static const TypeInfo isa_bridge_info = {
    .name          = "igd-passthrough-isa-bridge",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIDevice),
    .class_init = isa_bridge_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void pt_graphics_register_types(void)
{
    type_register_static(&isa_bridge_info);
}
type_init(pt_graphics_register_types)

void xen_igd_passthrough_isa_bridge_create(XenPCIPassthroughState *s,
                                           XenHostPCIDevice *dev)
{
    PCIBus *bus = pci_get_bus(&s->dev);
    struct PCIDevice *bridge_dev;
    int i, num;
    const uint16_t gpu_dev_id = dev->device_id;
    uint16_t pch_dev_id = 0xffff;
    uint8_t pch_rev_id = 0;

    num = ARRAY_SIZE(igd_combo_id_infos);
    for (i = 0; i < num; i++) {
        if (gpu_dev_id == igd_combo_id_infos[i].gpu_device_id) {
            pch_dev_id = igd_combo_id_infos[i].pch_device_id;
            pch_rev_id = igd_combo_id_infos[i].pch_revision_id;
        }
    }

    if (pch_dev_id == 0xffff) {
        return;
    }

    /* Currently IGD drivers always need to access PCH by 1f.0. */
    bridge_dev = pci_create_simple(bus, PCI_DEVFN(0x1f, 0),
                                   "igd-passthrough-isa-bridge");

    /*
     * Note that vendor id is always PCI_VENDOR_ID_INTEL.
     */
    if (!bridge_dev) {
        fprintf(stderr, "set igd-passthrough-isa-bridge failed!\n");
        return;
    }
    pci_config_set_device_id(bridge_dev->config, pch_dev_id);
    pci_config_set_revision(bridge_dev->config, pch_rev_id);
}

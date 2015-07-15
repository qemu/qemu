/*
 * graphics passthrough
 */
#include "xen_pt.h"
#include "xen-host-pci-device.h"
#include "hw/xen/xen_backend.h"

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

    return 0;
}

static void *get_vgabios(XenPCIPassthroughState *s, int *size,
                       XenHostPCIDevice *dev)
{
    return pci_assign_dev_load_option_rom(&s->dev, OBJECT(&s->dev), size,
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

int xen_pt_setup_vga(XenPCIPassthroughState *s, XenHostPCIDevice *dev)
{
    unsigned char *bios = NULL;
    struct rom_header *rom;
    int bios_size;
    char *c = NULL;
    char checksum = 0;
    uint32_t len = 0;
    struct pci_data *pd = NULL;

    if (!is_igd_vga_passthrough(dev)) {
        return -1;
    }

    bios = get_vgabios(s, &bios_size, dev);
    if (!bios) {
        XEN_PT_ERR(&s->dev, "VGA: Can't getting VBIOS!\n");
        return -1;
    }

    /* Currently we fixed this address as a primary. */
    rom = (struct rom_header *)bios;
    pd = (void *)(bios + (unsigned char)rom->pcioffset);

    /* We may need to fixup Device Identification. */
    if (pd->device != s->real_device.device_id) {
        pd->device = s->real_device.device_id;

        len = rom->size * 512;
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
    cpu_physical_memory_rw(0xc0000, bios, bios_size, 1);
    return 0;
}

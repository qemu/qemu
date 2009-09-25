#ifndef QEMU_PCI_H
#define QEMU_PCI_H

#include "qemu-common.h"

#include "qdev.h"

/* PCI includes legacy ISA access.  */
#include "isa.h"

/* PCI bus */

extern target_phys_addr_t pci_mem_base;

#define PCI_DEVFN(slot, func)   ((((slot) & 0x1f) << 3) | ((func) & 0x07))
#define PCI_SLOT(devfn)         (((devfn) >> 3) & 0x1f)
#define PCI_FUNC(devfn)         ((devfn) & 0x07)

/* Class, Vendor and Device IDs from Linux's pci_ids.h */
#include "pci_ids.h"

/* QEMU-specific Vendor and Device ID definitions */

/* IBM (0x1014) */
#define PCI_DEVICE_ID_IBM_440GX          0x027f
#define PCI_DEVICE_ID_IBM_OPENPIC2       0xffff

/* Hitachi (0x1054) */
#define PCI_VENDOR_ID_HITACHI            0x1054
#define PCI_DEVICE_ID_HITACHI_SH7751R    0x350e

/* Apple (0x106b) */
#define PCI_DEVICE_ID_APPLE_343S1201     0x0010
#define PCI_DEVICE_ID_APPLE_UNI_N_I_PCI  0x001e
#define PCI_DEVICE_ID_APPLE_UNI_N_PCI    0x001f
#define PCI_DEVICE_ID_APPLE_UNI_N_KEYL   0x0022
#define PCI_DEVICE_ID_APPLE_IPID_USB     0x003f

/* Realtek (0x10ec) */
#define PCI_DEVICE_ID_REALTEK_8029       0x8029

/* Xilinx (0x10ee) */
#define PCI_DEVICE_ID_XILINX_XC2VP30     0x0300

/* Marvell (0x11ab) */
#define PCI_DEVICE_ID_MARVELL_GT6412X    0x4620

/* QEMU/Bochs VGA (0x1234) */
#define PCI_VENDOR_ID_QEMU               0x1234
#define PCI_DEVICE_ID_QEMU_VGA           0x1111

/* VMWare (0x15ad) */
#define PCI_VENDOR_ID_VMWARE             0x15ad
#define PCI_DEVICE_ID_VMWARE_SVGA2       0x0405
#define PCI_DEVICE_ID_VMWARE_SVGA        0x0710
#define PCI_DEVICE_ID_VMWARE_NET         0x0720
#define PCI_DEVICE_ID_VMWARE_SCSI        0x0730
#define PCI_DEVICE_ID_VMWARE_IDE         0x1729

/* Intel (0x8086) */
#define PCI_DEVICE_ID_INTEL_82551IT      0x1209
#define PCI_DEVICE_ID_INTEL_82557        0x1229

/* Red Hat / Qumranet (for QEMU) -- see pci-ids.txt */
#define PCI_VENDOR_ID_REDHAT_QUMRANET    0x1af4
#define PCI_SUBVENDOR_ID_REDHAT_QUMRANET 0x1af4
#define PCI_SUBDEVICE_ID_QEMU            0x1100

#define PCI_DEVICE_ID_VIRTIO_NET         0x1000
#define PCI_DEVICE_ID_VIRTIO_BLOCK       0x1001
#define PCI_DEVICE_ID_VIRTIO_BALLOON     0x1002
#define PCI_DEVICE_ID_VIRTIO_CONSOLE     0x1003

typedef void PCIConfigWriteFunc(PCIDevice *pci_dev,
                                uint32_t address, uint32_t data, int len);
typedef uint32_t PCIConfigReadFunc(PCIDevice *pci_dev,
                                   uint32_t address, int len);
typedef void PCIMapIORegionFunc(PCIDevice *pci_dev, int region_num,
                                uint32_t addr, uint32_t size, int type);
typedef int PCIUnregisterFunc(PCIDevice *pci_dev);

#define PCI_ADDRESS_SPACE_MEM		0x00
#define PCI_ADDRESS_SPACE_IO		0x01
#define PCI_ADDRESS_SPACE_MEM_PREFETCH	0x08

typedef struct PCIIORegion {
    uint32_t addr; /* current PCI mapping address. -1 means not mapped */
    uint32_t size;
    uint8_t type;
    PCIMapIORegionFunc *map_func;
} PCIIORegion;

#define PCI_ROM_SLOT 6
#define PCI_NUM_REGIONS 7

/* Declarations from linux/pci_regs.h */
#define PCI_VENDOR_ID		0x00	/* 16 bits */
#define PCI_DEVICE_ID		0x02	/* 16 bits */
#define PCI_COMMAND		0x04	/* 16 bits */
#define  PCI_COMMAND_IO		0x1	/* Enable response in I/O space */
#define  PCI_COMMAND_MEMORY	0x2	/* Enable response in Memory space */
#define  PCI_COMMAND_MASTER	0x4	/* Enable bus master */
#define PCI_STATUS              0x06    /* 16 bits */
#define PCI_REVISION_ID         0x08    /* 8 bits  */
#define PCI_CLASS_PROG		0x09	/* Reg. Level Programming Interface */
#define PCI_CLASS_DEVICE        0x0a    /* Device class */
#define PCI_CACHE_LINE_SIZE	0x0c	/* 8 bits */
#define PCI_LATENCY_TIMER	0x0d	/* 8 bits */
#define PCI_HEADER_TYPE         0x0e    /* 8 bits */
#define  PCI_HEADER_TYPE_NORMAL		0
#define  PCI_HEADER_TYPE_BRIDGE		1
#define  PCI_HEADER_TYPE_CARDBUS	2
#define  PCI_HEADER_TYPE_MULTI_FUNCTION 0x80
#define PCI_BASE_ADDRESS_0	0x10	/* 32 bits */
#define PCI_PRIMARY_BUS		0x18	/* Primary bus number */
#define PCI_SECONDARY_BUS	0x19	/* Secondary bus number */
#define PCI_SEC_STATUS		0x1e	/* Secondary status register, only bit 14 used */
#define PCI_SUBSYSTEM_VENDOR_ID 0x2c    /* 16 bits */
#define PCI_SUBSYSTEM_ID        0x2e    /* 16 bits */
#define PCI_CAPABILITY_LIST	0x34	/* Offset of first capability list entry */
#define PCI_INTERRUPT_LINE	0x3c	/* 8 bits */
#define PCI_INTERRUPT_PIN	0x3d	/* 8 bits */
#define PCI_MIN_GNT		0x3e	/* 8 bits */
#define PCI_MAX_LAT		0x3f	/* 8 bits */

/* Capability lists */
#define PCI_CAP_LIST_ID		0	/* Capability ID */
#define PCI_CAP_LIST_NEXT	1	/* Next capability in the list */

#define PCI_REVISION            0x08    /* obsolete, use PCI_REVISION_ID */
#define PCI_SUBVENDOR_ID        0x2c    /* obsolete, use PCI_SUBSYSTEM_VENDOR_ID */
#define PCI_SUBDEVICE_ID        0x2e    /* obsolete, use PCI_SUBSYSTEM_ID */

/* Bits in the PCI Status Register (PCI 2.3 spec) */
#define PCI_STATUS_RESERVED1	0x007
#define PCI_STATUS_INT_STATUS	0x008
#define PCI_STATUS_CAP_LIST	0x010
#define PCI_STATUS_66MHZ	0x020
#define PCI_STATUS_RESERVED2	0x040
#define PCI_STATUS_FAST_BACK	0x080
#define PCI_STATUS_DEVSEL	0x600

#define PCI_STATUS_RESERVED_MASK_LO (PCI_STATUS_RESERVED1 | \
                PCI_STATUS_INT_STATUS | PCI_STATUS_CAPABILITIES | \
                PCI_STATUS_66MHZ | PCI_STATUS_RESERVED2 | PCI_STATUS_FAST_BACK)

#define PCI_STATUS_RESERVED_MASK_HI (PCI_STATUS_DEVSEL >> 8)

/* Bits in the PCI Command Register (PCI 2.3 spec) */
#define PCI_COMMAND_RESERVED	0xf800

#define PCI_COMMAND_RESERVED_MASK_HI (PCI_COMMAND_RESERVED >> 8)

/* Size of the standard PCI config header */
#define PCI_CONFIG_HEADER_SIZE 0x40
/* Size of the standard PCI config space */
#define PCI_CONFIG_SPACE_SIZE 0x100

/* Bits in cap_present field. */
enum {
    QEMU_PCI_CAP_MSIX = 0x1,
};

struct PCIDevice {
    DeviceState qdev;
    /* PCI config space */
    uint8_t config[PCI_CONFIG_SPACE_SIZE];

    /* Used to enable config checks on load. Note that writeable bits are
     * never checked even if set in cmask. */
    uint8_t cmask[PCI_CONFIG_SPACE_SIZE];

    /* Used to implement R/W bytes */
    uint8_t wmask[PCI_CONFIG_SPACE_SIZE];

    /* Used to allocate config space for capabilities. */
    uint8_t used[PCI_CONFIG_SPACE_SIZE];

    /* the following fields are read only */
    PCIBus *bus;
    uint32_t devfn;
    char name[64];
    PCIIORegion io_regions[PCI_NUM_REGIONS];

    /* do not access the following fields */
    PCIConfigReadFunc *config_read;
    PCIConfigWriteFunc *config_write;

    /* IRQ objects for the INTA-INTD pins.  */
    qemu_irq *irq;

    /* Current IRQ levels.  Used internally by the generic PCI code.  */
    int irq_state[4];

    /* Capability bits */
    uint32_t cap_present;

    /* Offset of MSI-X capability in config space */
    uint8_t msix_cap;

    /* MSI-X entries */
    int msix_entries_nr;

    /* Space to store MSIX table */
    uint8_t *msix_table_page;
    /* MMIO index used to map MSIX table and pending bit entries. */
    int msix_mmio_index;
    /* Reference-count for entries actually in use by driver. */
    unsigned *msix_entry_used;
    /* Region including the MSI-X table */
    uint32_t msix_bar_size;
    /* Version id needed for VMState */
    int32_t version_id;
    /* How much space does an MSIX table need. */
    /* The spec requires giving the table structure
     * a 4K aligned region all by itself. Align it to
     * target pages so that drivers can do passthrough
     * on the rest of the region. */
    target_phys_addr_t msix_page_size;
};

PCIDevice *pci_register_device(PCIBus *bus, const char *name,
                               int instance_size, int devfn,
                               PCIConfigReadFunc *config_read,
                               PCIConfigWriteFunc *config_write);

void pci_register_bar(PCIDevice *pci_dev, int region_num,
                            uint32_t size, int type,
                            PCIMapIORegionFunc *map_func);

int pci_add_capability(PCIDevice *pci_dev, uint8_t cap_id, uint8_t cap_size);

void pci_del_capability(PCIDevice *pci_dev, uint8_t cap_id, uint8_t cap_size);

void pci_reserve_capability(PCIDevice *pci_dev, uint8_t offset, uint8_t size);

uint8_t pci_find_capability(PCIDevice *pci_dev, uint8_t cap_id);


uint32_t pci_default_read_config(PCIDevice *d,
                                 uint32_t address, int len);
void pci_default_write_config(PCIDevice *d,
                              uint32_t address, uint32_t val, int len);
void pci_device_save(PCIDevice *s, QEMUFile *f);
int pci_device_load(PCIDevice *s, QEMUFile *f);

typedef void (*pci_set_irq_fn)(void *opaque, int irq_num, int level);
typedef int (*pci_map_irq_fn)(PCIDevice *pci_dev, int irq_num);
void pci_bus_new_inplace(PCIBus *bus, DeviceState *parent,
                         const char *name, int devfn_min);
PCIBus *pci_bus_new(DeviceState *parent, const char *name, int devfn_min);
void pci_bus_irqs(PCIBus *bus, pci_set_irq_fn set_irq, pci_map_irq_fn map_irq,
                  void *irq_opaque, int nirq);
PCIBus *pci_register_bus(DeviceState *parent, const char *name,
                         pci_set_irq_fn set_irq, pci_map_irq_fn map_irq,
                         void *irq_opaque, int devfn_min, int nirq);

int pci_nic_supported(const char *model);
PCIDevice *pci_nic_init(NICInfo *nd, const char *default_model,
                        const char *default_devaddr);
void pci_data_write(void *opaque, uint32_t addr, uint32_t val, int len);
uint32_t pci_data_read(void *opaque, uint32_t addr, int len);
int pci_bus_num(PCIBus *s);
void pci_for_each_device(int bus_num, void (*fn)(PCIDevice *d));
PCIBus *pci_find_bus(int bus_num);
PCIDevice *pci_find_device(int bus_num, int slot, int function);

int pci_read_devaddr(Monitor *mon, const char *addr, int *domp, int *busp,
                     unsigned *slotp);

void pci_info(Monitor *mon);
PCIBus *pci_bridge_init(PCIBus *bus, int devfn, uint16_t vid, uint16_t did,
                        pci_map_irq_fn map_irq, const char *name);

static inline void
pci_set_byte(uint8_t *config, uint8_t val)
{
    *config = val;
}

static inline uint8_t
pci_get_byte(uint8_t *config)
{
    return *config;
}

static inline void
pci_set_word(uint8_t *config, uint16_t val)
{
    cpu_to_le16wu((uint16_t *)config, val);
}

static inline uint16_t
pci_get_word(uint8_t *config)
{
    return le16_to_cpupu((uint16_t *)config);
}

static inline void
pci_set_long(uint8_t *config, uint32_t val)
{
    cpu_to_le32wu((uint32_t *)config, val);
}

static inline uint32_t
pci_get_long(uint8_t *config)
{
    return le32_to_cpupu((uint32_t *)config);
}

static inline void
pci_config_set_vendor_id(uint8_t *pci_config, uint16_t val)
{
    pci_set_word(&pci_config[PCI_VENDOR_ID], val);
}

static inline void
pci_config_set_device_id(uint8_t *pci_config, uint16_t val)
{
    pci_set_word(&pci_config[PCI_DEVICE_ID], val);
}

static inline void
pci_config_set_class(uint8_t *pci_config, uint16_t val)
{
    pci_set_word(&pci_config[PCI_CLASS_DEVICE], val);
}

typedef int (*pci_qdev_initfn)(PCIDevice *dev);
typedef struct {
    DeviceInfo qdev;
    pci_qdev_initfn init;
    PCIUnregisterFunc *exit;
    PCIConfigReadFunc *config_read;
    PCIConfigWriteFunc *config_write;
} PCIDeviceInfo;

void pci_qdev_register(PCIDeviceInfo *info);
void pci_qdev_register_many(PCIDeviceInfo *info);

PCIDevice *pci_create(const char *name, const char *devaddr);
PCIDevice *pci_create_noinit(PCIBus *bus, int devfn, const char *name);
PCIDevice *pci_create_simple(PCIBus *bus, int devfn, const char *name);

/* lsi53c895a.c */
#define LSI_MAX_DEVS 7

/* vmware_vga.c */
void pci_vmsvga_init(PCIBus *bus);

/* usb-uhci.c */
void usb_uhci_piix3_init(PCIBus *bus, int devfn);
void usb_uhci_piix4_init(PCIBus *bus, int devfn);

/* usb-ohci.c */
void usb_ohci_init_pci(struct PCIBus *bus, int devfn);

/* prep_pci.c */
PCIBus *pci_prep_init(qemu_irq *pic);

/* apb_pci.c */
PCIBus *pci_apb_init(target_phys_addr_t special_base,
                     target_phys_addr_t mem_base,
                     qemu_irq *pic, PCIBus **bus2, PCIBus **bus3);

/* sh_pci.c */
PCIBus *sh_pci_register_bus(pci_set_irq_fn set_irq, pci_map_irq_fn map_irq,
                            void *pic, int devfn_min, int nirq);

#endif

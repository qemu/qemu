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

#define PCI_DEVICES_MAX 64

/* Declarations from linux/pci_regs.h */
#define PCI_VENDOR_ID		0x00	/* 16 bits */
#define PCI_DEVICE_ID		0x02	/* 16 bits */
#define PCI_COMMAND		0x04	/* 16 bits */
#define  PCI_COMMAND_IO		0x1	/* Enable response in I/O space */
#define  PCI_COMMAND_MEMORY	0x2	/* Enable response in Memory space */
#define PCI_STATUS              0x06    /* 16 bits */
#define PCI_REVISION_ID         0x08    /* 8 bits  */
#define PCI_CLASS_DEVICE        0x0a    /* Device class */
#define PCI_HEADER_TYPE         0x0e    /* 8 bits */
#define  PCI_HEADER_TYPE_NORMAL		0
#define  PCI_HEADER_TYPE_BRIDGE		1
#define  PCI_HEADER_TYPE_CARDBUS	2
#define  PCI_HEADER_TYPE_MULTI_FUNCTION 0x80
#define PCI_SUBSYSTEM_VENDOR_ID 0x2c    /* 16 bits */
#define PCI_SUBSYSTEM_ID        0x2e    /* 16 bits */
#define PCI_INTERRUPT_LINE	0x3c	/* 8 bits */
#define PCI_INTERRUPT_PIN	0x3d	/* 8 bits */
#define PCI_MIN_GNT		0x3e	/* 8 bits */
#define PCI_MAX_LAT		0x3f	/* 8 bits */

#define PCI_REVISION            0x08    /* obsolete, use PCI_REVISION_ID */
#define PCI_SUBVENDOR_ID        0x2c    /* obsolete, use PCI_SUBSYSTEM_VENDOR_ID */
#define PCI_SUBDEVICE_ID        0x2e    /* obsolete, use PCI_SUBSYSTEM_ID */

/* Bits in the PCI Status Register (PCI 2.3 spec) */
#define PCI_STATUS_RESERVED1	0x007
#define PCI_STATUS_INT_STATUS	0x008
#define PCI_STATUS_CAPABILITIES	0x010
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

struct PCIDevice {
    DeviceState qdev;
    /* PCI config space */
    uint8_t config[256];

    /* the following fields are read only */
    PCIBus *bus;
    int devfn;
    char name[64];
    PCIIORegion io_regions[PCI_NUM_REGIONS];

    /* do not access the following fields */
    PCIConfigReadFunc *config_read;
    PCIConfigWriteFunc *config_write;
    PCIUnregisterFunc *unregister;
    /* ??? This is a PC-specific hack, and should be removed.  */
    int irq_index;

    /* IRQ objects for the INTA-INTD pins.  */
    qemu_irq *irq;

    /* Current IRQ levels.  Used internally by the generic PCI code.  */
    int irq_state[4];
};

PCIDevice *pci_register_device(PCIBus *bus, const char *name,
                               int instance_size, int devfn,
                               PCIConfigReadFunc *config_read,
                               PCIConfigWriteFunc *config_write);
int pci_unregister_device(PCIDevice *pci_dev);

void pci_register_io_region(PCIDevice *pci_dev, int region_num,
                            uint32_t size, int type,
                            PCIMapIORegionFunc *map_func);

uint32_t pci_default_read_config(PCIDevice *d,
                                 uint32_t address, int len);
void pci_default_write_config(PCIDevice *d,
                              uint32_t address, uint32_t val, int len);
void pci_device_save(PCIDevice *s, QEMUFile *f);
int pci_device_load(PCIDevice *s, QEMUFile *f);

typedef void (*pci_set_irq_fn)(qemu_irq *pic, int irq_num, int level);
typedef int (*pci_map_irq_fn)(PCIDevice *pci_dev, int irq_num);
PCIBus *pci_register_bus(pci_set_irq_fn set_irq, pci_map_irq_fn map_irq,
                         qemu_irq *pic, int devfn_min, int nirq);

PCIDevice *pci_nic_init(PCIBus *bus, NICInfo *nd, int devfn,
                  const char *default_model);
void pci_data_write(void *opaque, uint32_t addr, uint32_t val, int len);
uint32_t pci_data_read(void *opaque, uint32_t addr, int len);
int pci_bus_num(PCIBus *s);
void pci_for_each_device(int bus_num, void (*fn)(PCIDevice *d));
PCIBus *pci_find_bus(int bus_num);
PCIDevice *pci_find_device(int bus_num, int slot, int function);

int pci_read_devaddr(const char *addr, int *domp, int *busp, unsigned *slotp);
int pci_assign_devaddr(const char *addr, int *domp, int *busp, unsigned *slotp);

void pci_info(Monitor *mon);
PCIBus *pci_bridge_init(PCIBus *bus, int devfn, uint16_t vid, uint16_t did,
                        pci_map_irq_fn map_irq, const char *name);

static inline void
pci_config_set_vendor_id(uint8_t *pci_config, uint16_t val)
{
    cpu_to_le16wu((uint16_t *)&pci_config[PCI_VENDOR_ID], val);
}

static inline void
pci_config_set_device_id(uint8_t *pci_config, uint16_t val)
{
    cpu_to_le16wu((uint16_t *)&pci_config[PCI_DEVICE_ID], val);
}

static inline void
pci_config_set_class(uint8_t *pci_config, uint16_t val)
{
    cpu_to_le16wu((uint16_t *)&pci_config[PCI_CLASS_DEVICE], val);
}

typedef void (*pci_qdev_initfn)(PCIDevice *dev);
void pci_qdev_register(const char *name, int size, pci_qdev_initfn init);

PCIDevice *pci_create_simple(PCIBus *bus, int devfn, const char *name);

/* lsi53c895a.c */
#define LSI_MAX_DEVS 7
void lsi_scsi_attach(void *opaque, BlockDriverState *bd, int id);
void *lsi_scsi_init(PCIBus *bus, int devfn);

/* vmware_vga.c */
void pci_vmsvga_init(PCIBus *bus);

/* usb-uhci.c */
void usb_uhci_piix3_init(PCIBus *bus, int devfn);
void usb_uhci_piix4_init(PCIBus *bus, int devfn);

/* usb-ohci.c */
void usb_ohci_init_pci(struct PCIBus *bus, int num_ports, int devfn);

/* eepro100.c */

PCIDevice *pci_i82551_init(PCIBus *bus, NICInfo *nd, int devfn);
PCIDevice *pci_i82557b_init(PCIBus *bus, NICInfo *nd, int devfn);
PCIDevice *pci_i82559er_init(PCIBus *bus, NICInfo *nd, int devfn);

/* ne2000.c */

PCIDevice *pci_ne2000_init(PCIBus *bus, NICInfo *nd, int devfn);

/* rtl8139.c */

PCIDevice *pci_rtl8139_init(PCIBus *bus, NICInfo *nd, int devfn);

/* e1000.c */
PCIDevice *pci_e1000_init(PCIBus *bus, NICInfo *nd, int devfn);

/* pcnet.c */
PCIDevice *pci_pcnet_init(PCIBus *bus, NICInfo *nd, int devfn);

/* prep_pci.c */
PCIBus *pci_prep_init(qemu_irq *pic);

/* apb_pci.c */
PCIBus *pci_apb_init(target_phys_addr_t special_base,
                     target_phys_addr_t mem_base,
                     qemu_irq *pic, PCIBus **bus2, PCIBus **bus3);

/* sh_pci.c */
PCIBus *sh_pci_register_bus(pci_set_irq_fn set_irq, pci_map_irq_fn map_irq,
                            qemu_irq *pic, int devfn_min, int nirq);

#endif

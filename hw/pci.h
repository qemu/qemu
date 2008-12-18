#ifndef QEMU_PCI_H
#define QEMU_PCI_H

/* PCI includes legacy ISA access.  */
#include "isa.h"

/* PCI bus */

extern target_phys_addr_t pci_mem_base;

/* see pci-ids.txt */
#define PCI_VENDOR_ID_REDHAT_QUMRANET    0x1af4
#define PCI_SUBVENDOR_ID_REDHAT_QUMRANET 0x1af4
#define PCI_SUBDEVICE_ID_QEMU            0x1100

#define PCI_DEVICE_ID_VIRTIO_NET         0x1000
#define PCI_DEVICE_ID_VIRTIO_BLOCK       0x1001
#define PCI_DEVICE_ID_VIRTIO_BALLOON     0x1002

typedef void PCIConfigWriteFunc(PCIDevice *pci_dev,
                                uint32_t address, uint32_t data, int len);
typedef uint32_t PCIConfigReadFunc(PCIDevice *pci_dev,
                                   uint32_t address, int len);
typedef void PCIMapIORegionFunc(PCIDevice *pci_dev, int region_num,
                                uint32_t addr, uint32_t size, int type);

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

#define PCI_VENDOR_ID		0x00	/* 16 bits */
#define PCI_DEVICE_ID		0x02	/* 16 bits */
#define PCI_COMMAND		0x04	/* 16 bits */
#define  PCI_COMMAND_IO		0x1	/* Enable response in I/O space */
#define  PCI_COMMAND_MEMORY	0x2	/* Enable response in Memory space */
#define PCI_REVISION            0x08
#define PCI_CLASS_DEVICE        0x0a    /* Device class */
#define PCI_SUBVENDOR_ID	0x2c	/* 16 bits */
#define PCI_SUBDEVICE_ID	0x2e	/* 16 bits */
#define PCI_INTERRUPT_LINE	0x3c	/* 8 bits */
#define PCI_INTERRUPT_PIN	0x3d	/* 8 bits */
#define PCI_MIN_GNT		0x3e	/* 8 bits */
#define PCI_MAX_LAT		0x3f	/* 8 bits */

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

void pci_nic_init(PCIBus *bus, NICInfo *nd, int devfn);
void pci_data_write(void *opaque, uint32_t addr, uint32_t val, int len);
uint32_t pci_data_read(void *opaque, uint32_t addr, int len);
int pci_bus_num(PCIBus *s);
void pci_for_each_device(int bus_num, void (*fn)(PCIDevice *d));

void pci_info(void);
PCIBus *pci_bridge_init(PCIBus *bus, int devfn, uint32_t id,
                        pci_map_irq_fn map_irq, const char *name);

/* lsi53c895a.c */
#define LSI_MAX_DEVS 7
void lsi_scsi_attach(void *opaque, BlockDriverState *bd, int id);
void *lsi_scsi_init(PCIBus *bus, int devfn);

/* vmware_vga.c */
void pci_vmsvga_init(PCIBus *bus, DisplayState *ds, uint8_t *vga_ram_base,
                     unsigned long vga_ram_offset, int vga_ram_size);

/* usb-uhci.c */
void usb_uhci_piix3_init(PCIBus *bus, int devfn);
void usb_uhci_piix4_init(PCIBus *bus, int devfn);

/* usb-ohci.c */
void usb_ohci_init_pci(struct PCIBus *bus, int num_ports, int devfn);

/* eepro100.c */

void pci_i82551_init(PCIBus *bus, NICInfo *nd, int devfn);
void pci_i82557b_init(PCIBus *bus, NICInfo *nd, int devfn);
void pci_i82559er_init(PCIBus *bus, NICInfo *nd, int devfn);

/* ne2000.c */

void pci_ne2000_init(PCIBus *bus, NICInfo *nd, int devfn);

/* rtl8139.c */

void pci_rtl8139_init(PCIBus *bus, NICInfo *nd, int devfn);

/* e1000.c */
void pci_e1000_init(PCIBus *bus, NICInfo *nd, int devfn);

/* pcnet.c */
void pci_pcnet_init(PCIBus *bus, NICInfo *nd, int devfn);

/* prep_pci.c */
PCIBus *pci_prep_init(qemu_irq *pic);

/* apb_pci.c */
PCIBus *pci_apb_init(target_phys_addr_t special_base, target_phys_addr_t mem_base,
                     qemu_irq *pic);

/* sh_pci.c */
PCIBus *sh_pci_register_bus(pci_set_irq_fn set_irq, pci_map_irq_fn map_irq,
                            qemu_irq *pic, int devfn_min, int nirq);

#endif

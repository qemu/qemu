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

typedef uint64_t pcibus_t;
#define FMT_PCIBUS                      PRIx64

typedef void PCIConfigWriteFunc(PCIDevice *pci_dev,
                                uint32_t address, uint32_t data, int len);
typedef uint32_t PCIConfigReadFunc(PCIDevice *pci_dev,
                                   uint32_t address, int len);
typedef void PCIMapIORegionFunc(PCIDevice *pci_dev, int region_num,
                                pcibus_t addr, pcibus_t size, int type);
typedef int PCIUnregisterFunc(PCIDevice *pci_dev);

typedef struct PCIIORegion {
    pcibus_t addr; /* current PCI mapping address. -1 means not mapped */
#define PCI_BAR_UNMAPPED (~(pcibus_t)0)
    pcibus_t size;
    pcibus_t filtered_size;
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
#define  PCI_COMMAND_INTX_DISABLE 0x400 /* INTx Emulation Disable */
#define PCI_STATUS              0x06    /* 16 bits */
#define  PCI_STATUS_INTERRUPT   0x08
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
#define  PCI_BASE_ADDRESS_SPACE_IO	0x01
#define  PCI_BASE_ADDRESS_SPACE_MEMORY	0x00
#define  PCI_BASE_ADDRESS_MEM_TYPE_64	0x04	/* 64 bit address */
#define  PCI_BASE_ADDRESS_MEM_PREFETCH	0x08	/* prefetchable? */
#define PCI_PRIMARY_BUS		0x18	/* Primary bus number */
#define PCI_SECONDARY_BUS	0x19	/* Secondary bus number */
#define PCI_SUBORDINATE_BUS	0x1a	/* Highest bus number behind the bridge */
#define PCI_IO_BASE             0x1c    /* I/O range behind the bridge */
#define PCI_IO_LIMIT            0x1d
#define  PCI_IO_RANGE_TYPE_32	0x01
#define  PCI_IO_RANGE_MASK      (~0x0fUL)
#define PCI_SEC_STATUS		0x1e	/* Secondary status register, only bit 14 used */
#define PCI_MEMORY_BASE         0x20    /* Memory range behind */
#define PCI_MEMORY_LIMIT        0x22
#define  PCI_MEMORY_RANGE_MASK  (~0x0fUL)
#define PCI_PREF_MEMORY_BASE    0x24    /* Prefetchable memory range behind */
#define PCI_PREF_MEMORY_LIMIT   0x26
#define  PCI_PREF_RANGE_MASK    (~0x0fUL)
#define  PCI_PREF_RANGE_TYPE_64 0x01
#define PCI_PREF_BASE_UPPER32   0x28    /* Upper half of prefetchable memory range */
#define PCI_PREF_LIMIT_UPPER32	0x2c
#define PCI_SUBSYSTEM_VENDOR_ID 0x2c    /* 16 bits */
#define PCI_SUBSYSTEM_ID        0x2e    /* 16 bits */
#define PCI_ROM_ADDRESS		0x30	/* Bits 31..11 are address, 10..1 reserved */
#define  PCI_ROM_ADDRESS_ENABLE	0x01
#define PCI_IO_BASE_UPPER16     0x30    /* Upper half of I/O addresses */
#define PCI_IO_LIMIT_UPPER16    0x32
#define PCI_CAPABILITY_LIST	0x34	/* Offset of first capability list entry */
#define PCI_ROM_ADDRESS1	0x38	/* Same as PCI_ROM_ADDRESS, but for htype 1 */
#define PCI_INTERRUPT_LINE	0x3c	/* 8 bits */
#define PCI_INTERRUPT_PIN	0x3d	/* 8 bits */
#define PCI_MIN_GNT		0x3e	/* 8 bits */
#define PCI_BRIDGE_CONTROL      0x3e
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
#define PCI_STATUS_DEVSEL_MEDIUM 0x200
#define PCI_STATUS_DEVSEL_SLOW	0x400
#define PCI_STATUS_DEVSEL	0x600
#define  PCI_STATUS_SIG_TARGET_ABORT	0x800 /* Set on target abort */
#define  PCI_STATUS_REC_MASTER_ABORT	0x2000 /* Set on master abort */

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
/* Size of the standart PCIe config space: 4KB */
#define PCIE_CONFIG_SPACE_SIZE  0x1000

#define PCI_NUM_PINS 4 /* A-D */

/* Bits in cap_present field. */
enum {
    QEMU_PCI_CAP_MSIX = 0x1,
    QEMU_PCI_CAP_EXPRESS = 0x2,
};

struct PCIDevice {
    DeviceState qdev;
    /* PCI config space */
    uint8_t *config;

    /* Used to enable config checks on load. Note that writeable bits are
     * never checked even if set in cmask. */
    uint8_t *cmask;

    /* Used to implement R/W bytes */
    uint8_t *wmask;

    /* Used to allocate config space for capabilities. */
    uint8_t *used;

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
    uint8_t irq_state;

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

    /* Location of option rom */
    char *romfile;
    ram_addr_t rom_offset;
};

PCIDevice *pci_register_device(PCIBus *bus, const char *name,
                               int instance_size, int devfn,
                               PCIConfigReadFunc *config_read,
                               PCIConfigWriteFunc *config_write);

void pci_register_bar(PCIDevice *pci_dev, int region_num,
                            pcibus_t size, int type,
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
typedef int (*pci_hotplug_fn)(PCIDevice *pci_dev, int state);
void pci_bus_new_inplace(PCIBus *bus, DeviceState *parent,
                         const char *name, int devfn_min);
PCIBus *pci_bus_new(DeviceState *parent, const char *name, int devfn_min);
void pci_bus_irqs(PCIBus *bus, pci_set_irq_fn set_irq, pci_map_irq_fn map_irq,
                  void *irq_opaque, int nirq);
void pci_bus_hotplug(PCIBus *bus, pci_hotplug_fn hotplug);
PCIBus *pci_register_bus(DeviceState *parent, const char *name,
                         pci_set_irq_fn set_irq, pci_map_irq_fn map_irq,
                         void *irq_opaque, int devfn_min, int nirq);

PCIDevice *pci_nic_init(NICInfo *nd, const char *default_model,
                        const char *default_devaddr);
PCIDevice *pci_nic_init_nofail(NICInfo *nd, const char *default_model,
                               const char *default_devaddr);
int pci_bus_num(PCIBus *s);
void pci_for_each_device(PCIBus *bus, int bus_num, void (*fn)(PCIBus *bus, PCIDevice *d));
PCIBus *pci_find_root_bus(int domain);
PCIBus *pci_find_bus(PCIBus *bus, int bus_num);
PCIDevice *pci_find_device(PCIBus *bus, int bus_num, int slot, int function);
PCIBus *pci_get_bus_devfn(int *devfnp, const char *devaddr);

int pci_read_devaddr(Monitor *mon, const char *addr, int *domp, int *busp,
                     unsigned *slotp);

void pci_info(Monitor *mon);
PCIBus *pci_bridge_init(PCIBus *bus, int devfn, uint16_t vid, uint16_t did,
                        pci_map_irq_fn map_irq, const char *name);
PCIDevice *pci_bridge_get_device(PCIBus *bus);

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
pci_set_quad(uint8_t *config, uint64_t val)
{
    cpu_to_le64w((uint64_t *)config, val);
}

static inline uint64_t
pci_get_quad(uint8_t *config)
{
    return le64_to_cpup((uint64_t *)config);
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

    /* pci config header type */
    uint8_t header_type;

    /* pcie stuff */
    int is_express;   /* is this device pci express? */

    /* rom bar */
    const char *romfile;
} PCIDeviceInfo;

void pci_qdev_register(PCIDeviceInfo *info);
void pci_qdev_register_many(PCIDeviceInfo *info);

PCIDevice *pci_create(PCIBus *bus, int devfn, const char *name);
PCIDevice *pci_create_simple(PCIBus *bus, int devfn, const char *name);

static inline int pci_is_express(PCIDevice *d)
{
    return d->cap_present & QEMU_PCI_CAP_EXPRESS;
}

static inline uint32_t pci_config_size(PCIDevice *d)
{
    return pci_is_express(d) ? PCIE_CONFIG_SPACE_SIZE : PCI_CONFIG_SPACE_SIZE;
}

/* These are not pci specific. Should move into a separate header.
 * Only pci.c uses them, so keep them here for now.
 */

/* Get last byte of a range from offset + length.
 * Undefined for ranges that wrap around 0. */
static inline uint64_t range_get_last(uint64_t offset, uint64_t len)
{
    return offset + len - 1;
}

/* Check whether a given range covers a given byte. */
static inline int range_covers_byte(uint64_t offset, uint64_t len,
                                    uint64_t byte)
{
    return offset <= byte && byte <= range_get_last(offset, len);
}

/* Check whether 2 given ranges overlap.
 * Undefined if ranges that wrap around 0. */
static inline int ranges_overlap(uint64_t first1, uint64_t len1,
                                 uint64_t first2, uint64_t len2)
{
    uint64_t last1 = range_get_last(first1, len1);
    uint64_t last2 = range_get_last(first2, len2);

    return !(last2 < first1 || last1 < first2);
}

#endif

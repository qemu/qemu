#ifndef QEMU_PCI_H
#define QEMU_PCI_H

#include "qemu-common.h"
#include "qobject.h"

#include "qdev.h"
#include "memory.h"

/* PCI includes legacy ISA access.  */
#include "isa.h"

#include "pcie.h"

/* PCI bus */

#define PCI_DEVFN(slot, func)   ((((slot) & 0x1f) << 3) | ((func) & 0x07))
#define PCI_SLOT(devfn)         (((devfn) >> 3) & 0x1f)
#define PCI_FUNC(devfn)         ((devfn) & 0x07)
#define PCI_SLOT_MAX            32
#define PCI_FUNC_MAX            8

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
#define PCI_DEVICE_ID_INTEL_82801IR      0x2922

/* Red Hat / Qumranet (for QEMU) -- see pci-ids.txt */
#define PCI_VENDOR_ID_REDHAT_QUMRANET    0x1af4
#define PCI_SUBVENDOR_ID_REDHAT_QUMRANET 0x1af4
#define PCI_SUBDEVICE_ID_QEMU            0x1100

#define PCI_DEVICE_ID_VIRTIO_NET         0x1000
#define PCI_DEVICE_ID_VIRTIO_BLOCK       0x1001
#define PCI_DEVICE_ID_VIRTIO_BALLOON     0x1002
#define PCI_DEVICE_ID_VIRTIO_CONSOLE     0x1003

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
    MemoryRegion *memory;
    MemoryRegion *address_space;
} PCIIORegion;

#define PCI_ROM_SLOT 6
#define PCI_NUM_REGIONS 7

#include "pci_regs.h"

/* PCI HEADER_TYPE */
#define  PCI_HEADER_TYPE_MULTI_FUNCTION 0x80

/* Size of the standard PCI config header */
#define PCI_CONFIG_HEADER_SIZE 0x40
/* Size of the standard PCI config space */
#define PCI_CONFIG_SPACE_SIZE 0x100
/* Size of the standart PCIe config space: 4KB */
#define PCIE_CONFIG_SPACE_SIZE  0x1000

#define PCI_NUM_PINS 4 /* A-D */

/* Bits in cap_present field. */
enum {
    QEMU_PCI_CAP_MSI = 0x1,
    QEMU_PCI_CAP_MSIX = 0x2,
    QEMU_PCI_CAP_EXPRESS = 0x4,

    /* multifunction capable device */
#define QEMU_PCI_CAP_MULTIFUNCTION_BITNR        3
    QEMU_PCI_CAP_MULTIFUNCTION = (1 << QEMU_PCI_CAP_MULTIFUNCTION_BITNR),

    /* command register SERR bit enabled */
#define QEMU_PCI_CAP_SERR_BITNR 4
    QEMU_PCI_CAP_SERR = (1 << QEMU_PCI_CAP_SERR_BITNR),
};

struct PCIDevice {
    DeviceState qdev;
    /* PCI config space */
    uint8_t *config;

    /* Used to enable config checks on load. Note that writable bits are
     * never checked even if set in cmask. */
    uint8_t *cmask;

    /* Used to implement R/W bytes */
    uint8_t *wmask;

    /* Used to implement RW1C(Write 1 to Clear) bytes */
    uint8_t *w1cmask;

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
    MemoryRegion msix_mmio;
    /* Reference-count for entries actually in use by driver. */
    unsigned *msix_entry_used;
    /* Region including the MSI-X table */
    uint32_t msix_bar_size;
    /* Version id needed for VMState */
    int32_t version_id;

    /* Offset of MSI capability in config space */
    uint8_t msi_cap;

    /* PCI Express */
    PCIExpressDevice exp;

    /* Location of option rom */
    char *romfile;
    bool has_rom;
    MemoryRegion rom;
    uint32_t rom_bar;
};

PCIDevice *pci_register_device(PCIBus *bus, const char *name,
                               int instance_size, int devfn,
                               PCIConfigReadFunc *config_read,
                               PCIConfigWriteFunc *config_write);

void pci_register_bar_region(PCIDevice *pci_dev, int region_num,
                             uint8_t attr, MemoryRegion *memory);
pcibus_t pci_get_bar_addr(PCIDevice *pci_dev, int region_num);

int pci_add_capability(PCIDevice *pdev, uint8_t cap_id,
                       uint8_t offset, uint8_t size);

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

typedef enum {
    PCI_HOTPLUG_DISABLED,
    PCI_HOTPLUG_ENABLED,
    PCI_COLDPLUG_ENABLED,
} PCIHotplugState;

typedef int (*pci_hotplug_fn)(DeviceState *qdev, PCIDevice *pci_dev,
                              PCIHotplugState state);
void pci_bus_new_inplace(PCIBus *bus, DeviceState *parent,
                         const char *name,
                         MemoryRegion *address_space_mem,
                         MemoryRegion *address_space_io,
                         uint8_t devfn_min);
PCIBus *pci_bus_new(DeviceState *parent, const char *name,
                    MemoryRegion *address_space_mem,
                    MemoryRegion *address_space_io,
                    uint8_t devfn_min);
void pci_bus_irqs(PCIBus *bus, pci_set_irq_fn set_irq, pci_map_irq_fn map_irq,
                  void *irq_opaque, int nirq);
int pci_bus_get_irq_level(PCIBus *bus, int irq_num);
void pci_bus_hotplug(PCIBus *bus, pci_hotplug_fn hotplug, DeviceState *dev);
PCIBus *pci_register_bus(DeviceState *parent, const char *name,
                         pci_set_irq_fn set_irq, pci_map_irq_fn map_irq,
                         void *irq_opaque,
                         MemoryRegion *address_space_mem,
                         MemoryRegion *address_space_io,
                         uint8_t devfn_min, int nirq);
void pci_device_reset(PCIDevice *dev);
void pci_bus_reset(PCIBus *bus);

void pci_bus_set_mem_base(PCIBus *bus, target_phys_addr_t base);

PCIDevice *pci_nic_init(NICInfo *nd, const char *default_model,
                        const char *default_devaddr);
PCIDevice *pci_nic_init_nofail(NICInfo *nd, const char *default_model,
                               const char *default_devaddr);
int pci_bus_num(PCIBus *s);
void pci_for_each_device(PCIBus *bus, int bus_num, void (*fn)(PCIBus *bus, PCIDevice *d));
PCIBus *pci_find_root_bus(int domain);
int pci_find_domain(const PCIBus *bus);
PCIBus *pci_find_bus(PCIBus *bus, int bus_num);
PCIDevice *pci_find_device(PCIBus *bus, int bus_num, uint8_t devfn);
int pci_qdev_find_device(const char *id, PCIDevice **pdev);
PCIBus *pci_get_bus_devfn(int *devfnp, const char *devaddr);

int pci_parse_devaddr(const char *addr, int *domp, int *busp,
                      unsigned int *slotp, unsigned int *funcp);
int pci_read_devaddr(Monitor *mon, const char *addr, int *domp, int *busp,
                     unsigned *slotp);

void do_pci_info_print(Monitor *mon, const QObject *data);
void do_pci_info(Monitor *mon, QObject **ret_data);
void pci_bridge_update_mappings(PCIBus *b);

void pci_device_deassert_intx(PCIDevice *dev);

static inline void
pci_set_byte(uint8_t *config, uint8_t val)
{
    *config = val;
}

static inline uint8_t
pci_get_byte(const uint8_t *config)
{
    return *config;
}

static inline void
pci_set_word(uint8_t *config, uint16_t val)
{
    cpu_to_le16wu((uint16_t *)config, val);
}

static inline uint16_t
pci_get_word(const uint8_t *config)
{
    return le16_to_cpupu((const uint16_t *)config);
}

static inline void
pci_set_long(uint8_t *config, uint32_t val)
{
    cpu_to_le32wu((uint32_t *)config, val);
}

static inline uint32_t
pci_get_long(const uint8_t *config)
{
    return le32_to_cpupu((const uint32_t *)config);
}

static inline void
pci_set_quad(uint8_t *config, uint64_t val)
{
    cpu_to_le64w((uint64_t *)config, val);
}

static inline uint64_t
pci_get_quad(const uint8_t *config)
{
    return le64_to_cpup((const uint64_t *)config);
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
pci_config_set_revision(uint8_t *pci_config, uint8_t val)
{
    pci_set_byte(&pci_config[PCI_REVISION_ID], val);
}

static inline void
pci_config_set_class(uint8_t *pci_config, uint16_t val)
{
    pci_set_word(&pci_config[PCI_CLASS_DEVICE], val);
}

static inline void
pci_config_set_prog_interface(uint8_t *pci_config, uint8_t val)
{
    pci_set_byte(&pci_config[PCI_CLASS_PROG], val);
}

static inline void
pci_config_set_interrupt_pin(uint8_t *pci_config, uint8_t val)
{
    pci_set_byte(&pci_config[PCI_INTERRUPT_PIN], val);
}

/*
 * helper functions to do bit mask operation on configuration space.
 * Just to set bit, use test-and-set and discard returned value.
 * Just to clear bit, use test-and-clear and discard returned value.
 * NOTE: They aren't atomic.
 */
static inline uint8_t
pci_byte_test_and_clear_mask(uint8_t *config, uint8_t mask)
{
    uint8_t val = pci_get_byte(config);
    pci_set_byte(config, val & ~mask);
    return val & mask;
}

static inline uint8_t
pci_byte_test_and_set_mask(uint8_t *config, uint8_t mask)
{
    uint8_t val = pci_get_byte(config);
    pci_set_byte(config, val | mask);
    return val & mask;
}

static inline uint16_t
pci_word_test_and_clear_mask(uint8_t *config, uint16_t mask)
{
    uint16_t val = pci_get_word(config);
    pci_set_word(config, val & ~mask);
    return val & mask;
}

static inline uint16_t
pci_word_test_and_set_mask(uint8_t *config, uint16_t mask)
{
    uint16_t val = pci_get_word(config);
    pci_set_word(config, val | mask);
    return val & mask;
}

static inline uint32_t
pci_long_test_and_clear_mask(uint8_t *config, uint32_t mask)
{
    uint32_t val = pci_get_long(config);
    pci_set_long(config, val & ~mask);
    return val & mask;
}

static inline uint32_t
pci_long_test_and_set_mask(uint8_t *config, uint32_t mask)
{
    uint32_t val = pci_get_long(config);
    pci_set_long(config, val | mask);
    return val & mask;
}

static inline uint64_t
pci_quad_test_and_clear_mask(uint8_t *config, uint64_t mask)
{
    uint64_t val = pci_get_quad(config);
    pci_set_quad(config, val & ~mask);
    return val & mask;
}

static inline uint64_t
pci_quad_test_and_set_mask(uint8_t *config, uint64_t mask)
{
    uint64_t val = pci_get_quad(config);
    pci_set_quad(config, val | mask);
    return val & mask;
}

typedef int (*pci_qdev_initfn)(PCIDevice *dev);
typedef struct {
    DeviceInfo qdev;
    pci_qdev_initfn init;
    PCIUnregisterFunc *exit;
    PCIConfigReadFunc *config_read;
    PCIConfigWriteFunc *config_write;

    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t revision;
    uint16_t class_id;
    uint16_t subsystem_vendor_id;       /* only for header type = 0 */
    uint16_t subsystem_id;              /* only for header type = 0 */

    /*
     * pci-to-pci bridge or normal device.
     * This doesn't mean pci host switch.
     * When card bus bridge is supported, this would be enhanced.
     */
    int is_bridge;

    /* pcie stuff */
    int is_express;   /* is this device pci express? */

    /* device isn't hot-pluggable */
    int no_hotplug;

    /* rom bar */
    const char *romfile;
} PCIDeviceInfo;

void pci_qdev_register(PCIDeviceInfo *info);
void pci_qdev_register_many(PCIDeviceInfo *info);

PCIDevice *pci_create_multifunction(PCIBus *bus, int devfn, bool multifunction,
                                    const char *name);
PCIDevice *pci_create_simple_multifunction(PCIBus *bus, int devfn,
                                           bool multifunction,
                                           const char *name);
PCIDevice *pci_try_create_multifunction(PCIBus *bus, int devfn,
                                        bool multifunction,
                                        const char *name);
PCIDevice *pci_create(PCIBus *bus, int devfn, const char *name);
PCIDevice *pci_create_simple(PCIBus *bus, int devfn, const char *name);
PCIDevice *pci_try_create(PCIBus *bus, int devfn, const char *name);

static inline int pci_is_express(const PCIDevice *d)
{
    return d->cap_present & QEMU_PCI_CAP_EXPRESS;
}

static inline uint32_t pci_config_size(const PCIDevice *d)
{
    return pci_is_express(d) ? PCIE_CONFIG_SPACE_SIZE : PCI_CONFIG_SPACE_SIZE;
}

#endif

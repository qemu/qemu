#ifndef QEMU_PCI_H
#define QEMU_PCI_H

#include "exec/memory.h"
#include "sysemu/dma.h"

/* PCI includes legacy ISA access.  */
#include "hw/isa/isa.h"

#include "hw/pci/pcie.h"
#include "qom/object.h"

extern bool pci_available;

/* PCI bus */

#define PCI_DEVFN(slot, func)   ((((slot) & 0x1f) << 3) | ((func) & 0x07))
#define PCI_BUS_NUM(x)          (((x) >> 8) & 0xff)
#define PCI_SLOT(devfn)         (((devfn) >> 3) & 0x1f)
#define PCI_FUNC(devfn)         ((devfn) & 0x07)
#define PCI_BUILD_BDF(bus, devfn)     ((bus << 8) | (devfn))
#define PCI_BUS_MAX             256
#define PCI_DEVFN_MAX           256
#define PCI_SLOT_MAX            32
#define PCI_FUNC_MAX            8

/* Class, Vendor and Device IDs from Linux's pci_ids.h */
#include "hw/pci/pci_ids.h"

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
#define PCI_DEVICE_ID_QEMU_IPMI          0x1112

/* VMWare (0x15ad) */
#define PCI_VENDOR_ID_VMWARE             0x15ad
#define PCI_DEVICE_ID_VMWARE_SVGA2       0x0405
#define PCI_DEVICE_ID_VMWARE_SVGA        0x0710
#define PCI_DEVICE_ID_VMWARE_NET         0x0720
#define PCI_DEVICE_ID_VMWARE_SCSI        0x0730
#define PCI_DEVICE_ID_VMWARE_PVSCSI      0x07C0
#define PCI_DEVICE_ID_VMWARE_IDE         0x1729
#define PCI_DEVICE_ID_VMWARE_VMXNET3     0x07B0

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
#define PCI_DEVICE_ID_VIRTIO_SCSI        0x1004
#define PCI_DEVICE_ID_VIRTIO_RNG         0x1005
#define PCI_DEVICE_ID_VIRTIO_9P          0x1009
#define PCI_DEVICE_ID_VIRTIO_VSOCK       0x1012
#define PCI_DEVICE_ID_VIRTIO_PMEM        0x1013
#define PCI_DEVICE_ID_VIRTIO_IOMMU       0x1014
#define PCI_DEVICE_ID_VIRTIO_MEM         0x1015

#define PCI_VENDOR_ID_REDHAT             0x1b36
#define PCI_DEVICE_ID_REDHAT_BRIDGE      0x0001
#define PCI_DEVICE_ID_REDHAT_SERIAL      0x0002
#define PCI_DEVICE_ID_REDHAT_SERIAL2     0x0003
#define PCI_DEVICE_ID_REDHAT_SERIAL4     0x0004
#define PCI_DEVICE_ID_REDHAT_TEST        0x0005
#define PCI_DEVICE_ID_REDHAT_ROCKER      0x0006
#define PCI_DEVICE_ID_REDHAT_SDHCI       0x0007
#define PCI_DEVICE_ID_REDHAT_PCIE_HOST   0x0008
#define PCI_DEVICE_ID_REDHAT_PXB         0x0009
#define PCI_DEVICE_ID_REDHAT_BRIDGE_SEAT 0x000a
#define PCI_DEVICE_ID_REDHAT_PXB_PCIE    0x000b
#define PCI_DEVICE_ID_REDHAT_PCIE_RP     0x000c
#define PCI_DEVICE_ID_REDHAT_XHCI        0x000d
#define PCI_DEVICE_ID_REDHAT_PCIE_BRIDGE 0x000e
#define PCI_DEVICE_ID_REDHAT_MDPY        0x000f
#define PCI_DEVICE_ID_REDHAT_QXL         0x0100

#define FMT_PCIBUS                      PRIx64

typedef uint64_t pcibus_t;

struct PCIHostDeviceAddress {
    unsigned int domain;
    unsigned int bus;
    unsigned int slot;
    unsigned int function;
};

typedef void PCIConfigWriteFunc(PCIDevice *pci_dev,
                                uint32_t address, uint32_t data, int len);
typedef uint32_t PCIConfigReadFunc(PCIDevice *pci_dev,
                                   uint32_t address, int len);
typedef void PCIMapIORegionFunc(PCIDevice *pci_dev, int region_num,
                                pcibus_t addr, pcibus_t size, int type);
typedef void PCIUnregisterFunc(PCIDevice *pci_dev);

typedef struct PCIIORegion {
    pcibus_t addr; /* current PCI mapping address. -1 means not mapped */
#define PCI_BAR_UNMAPPED (~(pcibus_t)0)
    pcibus_t size;
    uint8_t type;
    MemoryRegion *memory;
    MemoryRegion *address_space;
} PCIIORegion;

#define PCI_ROM_SLOT 6
#define PCI_NUM_REGIONS 7

enum {
    QEMU_PCI_VGA_MEM,
    QEMU_PCI_VGA_IO_LO,
    QEMU_PCI_VGA_IO_HI,
    QEMU_PCI_VGA_NUM_REGIONS,
};

#define QEMU_PCI_VGA_MEM_BASE 0xa0000
#define QEMU_PCI_VGA_MEM_SIZE 0x20000
#define QEMU_PCI_VGA_IO_LO_BASE 0x3b0
#define QEMU_PCI_VGA_IO_LO_SIZE 0xc
#define QEMU_PCI_VGA_IO_HI_BASE 0x3c0
#define QEMU_PCI_VGA_IO_HI_SIZE 0x20

#include "hw/pci/pci_regs.h"

/* PCI HEADER_TYPE */
#define  PCI_HEADER_TYPE_MULTI_FUNCTION 0x80

/* Size of the standard PCI config header */
#define PCI_CONFIG_HEADER_SIZE 0x40
/* Size of the standard PCI config space */
#define PCI_CONFIG_SPACE_SIZE 0x100
/* Size of the standard PCIe config space: 4KB */
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

    /* command register SERR bit enabled - unused since QEMU v5.0 */
#define QEMU_PCI_CAP_SERR_BITNR 4
    QEMU_PCI_CAP_SERR = (1 << QEMU_PCI_CAP_SERR_BITNR),
    /* Standard hot plug controller. */
#define QEMU_PCI_SHPC_BITNR 5
    QEMU_PCI_CAP_SHPC = (1 << QEMU_PCI_SHPC_BITNR),
#define QEMU_PCI_SLOTID_BITNR 6
    QEMU_PCI_CAP_SLOTID = (1 << QEMU_PCI_SLOTID_BITNR),
    /* PCI Express capability - Power Controller Present */
#define QEMU_PCIE_SLTCAP_PCP_BITNR 7
    QEMU_PCIE_SLTCAP_PCP = (1 << QEMU_PCIE_SLTCAP_PCP_BITNR),
    /* Link active status in endpoint capability is always set */
#define QEMU_PCIE_LNKSTA_DLLLA_BITNR 8
    QEMU_PCIE_LNKSTA_DLLLA = (1 << QEMU_PCIE_LNKSTA_DLLLA_BITNR),
#define QEMU_PCIE_EXTCAP_INIT_BITNR 9
    QEMU_PCIE_EXTCAP_INIT = (1 << QEMU_PCIE_EXTCAP_INIT_BITNR),
};

#define TYPE_PCI_DEVICE "pci-device"
typedef struct PCIDeviceClass PCIDeviceClass;
DECLARE_OBJ_CHECKERS(PCIDevice, PCIDeviceClass,
                     PCI_DEVICE, TYPE_PCI_DEVICE)

/* Implemented by devices that can be plugged on PCI Express buses */
#define INTERFACE_PCIE_DEVICE "pci-express-device"

/* Implemented by devices that can be plugged on Conventional PCI buses */
#define INTERFACE_CONVENTIONAL_PCI_DEVICE "conventional-pci-device"

typedef struct PCIINTxRoute {
    enum {
        PCI_INTX_ENABLED,
        PCI_INTX_INVERTED,
        PCI_INTX_DISABLED,
    } mode;
    int irq;
} PCIINTxRoute;

struct PCIDeviceClass {
    DeviceClass parent_class;

    void (*realize)(PCIDevice *dev, Error **errp);
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
    bool is_bridge;

    /* rom bar */
    const char *romfile;
};

typedef void (*PCIINTxRoutingNotifier)(PCIDevice *dev);
typedef int (*MSIVectorUseNotifier)(PCIDevice *dev, unsigned int vector,
                                      MSIMessage msg);
typedef void (*MSIVectorReleaseNotifier)(PCIDevice *dev, unsigned int vector);
typedef void (*MSIVectorPollNotifier)(PCIDevice *dev,
                                      unsigned int vector_start,
                                      unsigned int vector_end);

enum PCIReqIDType {
    PCI_REQ_ID_INVALID = 0,
    PCI_REQ_ID_BDF,
    PCI_REQ_ID_SECONDARY_BUS,
    PCI_REQ_ID_MAX,
};
typedef enum PCIReqIDType PCIReqIDType;

struct PCIReqIDCache {
    PCIDevice *dev;
    PCIReqIDType type;
};
typedef struct PCIReqIDCache PCIReqIDCache;

struct PCIDevice {
    DeviceState qdev;
    bool partially_hotplugged;

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
    int32_t devfn;
    /* Cached device to fetch requester ID from, to avoid the PCI
     * tree walking every time we invoke PCI request (e.g.,
     * MSI). For conventional PCI root complex, this field is
     * meaningless. */
    PCIReqIDCache requester_id_cache;
    char name[64];
    PCIIORegion io_regions[PCI_NUM_REGIONS];
    AddressSpace bus_master_as;
    MemoryRegion bus_master_container_region;
    MemoryRegion bus_master_enable_region;

    /* do not access the following fields */
    PCIConfigReadFunc *config_read;
    PCIConfigWriteFunc *config_write;

    /* Legacy PCI VGA regions */
    MemoryRegion *vga_regions[QEMU_PCI_VGA_NUM_REGIONS];
    bool has_vga;

    /* Current IRQ levels.  Used internally by the generic PCI code.  */
    uint8_t irq_state;

    /* Capability bits */
    uint32_t cap_present;

    /* Offset of MSI-X capability in config space */
    uint8_t msix_cap;

    /* MSI-X entries */
    int msix_entries_nr;

    /* Space to store MSIX table & pending bit array */
    uint8_t *msix_table;
    uint8_t *msix_pba;
    /* MemoryRegion container for msix exclusive BAR setup */
    MemoryRegion msix_exclusive_bar;
    /* Memory Regions for MSIX table and pending bit entries. */
    MemoryRegion msix_table_mmio;
    MemoryRegion msix_pba_mmio;
    /* Reference-count for entries actually in use by driver. */
    unsigned *msix_entry_used;
    /* MSIX function mask set or MSIX disabled */
    bool msix_function_masked;
    /* Version id needed for VMState */
    int32_t version_id;

    /* Offset of MSI capability in config space */
    uint8_t msi_cap;

    /* PCI Express */
    PCIExpressDevice exp;

    /* SHPC */
    SHPCDevice *shpc;

    /* Location of option rom */
    char *romfile;
    bool has_rom;
    MemoryRegion rom;
    uint32_t rom_bar;

    /* INTx routing notifier */
    PCIINTxRoutingNotifier intx_routing_notifier;

    /* MSI-X notifiers */
    MSIVectorUseNotifier msix_vector_use_notifier;
    MSIVectorReleaseNotifier msix_vector_release_notifier;
    MSIVectorPollNotifier msix_vector_poll_notifier;

    /* ID of standby device in net_failover pair */
    char *failover_pair_id;
};

void pci_register_bar(PCIDevice *pci_dev, int region_num,
                      uint8_t attr, MemoryRegion *memory);
void pci_register_vga(PCIDevice *pci_dev, MemoryRegion *mem,
                      MemoryRegion *io_lo, MemoryRegion *io_hi);
void pci_unregister_vga(PCIDevice *pci_dev);
pcibus_t pci_get_bar_addr(PCIDevice *pci_dev, int region_num);

int pci_add_capability(PCIDevice *pdev, uint8_t cap_id,
                       uint8_t offset, uint8_t size,
                       Error **errp);

void pci_del_capability(PCIDevice *pci_dev, uint8_t cap_id, uint8_t cap_size);

uint8_t pci_find_capability(PCIDevice *pci_dev, uint8_t cap_id);


uint32_t pci_default_read_config(PCIDevice *d,
                                 uint32_t address, int len);
void pci_default_write_config(PCIDevice *d,
                              uint32_t address, uint32_t val, int len);
void pci_device_save(PCIDevice *s, QEMUFile *f);
int pci_device_load(PCIDevice *s, QEMUFile *f);
MemoryRegion *pci_address_space(PCIDevice *dev);
MemoryRegion *pci_address_space_io(PCIDevice *dev);

/*
 * Should not normally be used by devices. For use by sPAPR target
 * where QEMU emulates firmware.
 */
int pci_bar(PCIDevice *d, int reg);

typedef void (*pci_set_irq_fn)(void *opaque, int irq_num, int level);
typedef int (*pci_map_irq_fn)(PCIDevice *pci_dev, int irq_num);
typedef PCIINTxRoute (*pci_route_irq_fn)(void *opaque, int pin);

#define TYPE_PCI_BUS "PCI"
OBJECT_DECLARE_TYPE(PCIBus, PCIBusClass, PCI_BUS)
#define TYPE_PCIE_BUS "PCIE"

bool pci_bus_is_express(PCIBus *bus);

void pci_root_bus_new_inplace(PCIBus *bus, size_t bus_size, DeviceState *parent,
                              const char *name,
                              MemoryRegion *address_space_mem,
                              MemoryRegion *address_space_io,
                              uint8_t devfn_min, const char *typename);
PCIBus *pci_root_bus_new(DeviceState *parent, const char *name,
                         MemoryRegion *address_space_mem,
                         MemoryRegion *address_space_io,
                         uint8_t devfn_min, const char *typename);
void pci_root_bus_cleanup(PCIBus *bus);
void pci_bus_irqs(PCIBus *bus, pci_set_irq_fn set_irq, pci_map_irq_fn map_irq,
                  void *irq_opaque, int nirq);
void pci_bus_irqs_cleanup(PCIBus *bus);
int pci_bus_get_irq_level(PCIBus *bus, int irq_num);
/* 0 <= pin <= 3 0 = INTA, 1 = INTB, 2 = INTC, 3 = INTD */
static inline int pci_swizzle(int slot, int pin)
{
    return (slot + pin) % PCI_NUM_PINS;
}
int pci_swizzle_map_irq_fn(PCIDevice *pci_dev, int pin);
PCIBus *pci_register_root_bus(DeviceState *parent, const char *name,
                              pci_set_irq_fn set_irq, pci_map_irq_fn map_irq,
                              void *irq_opaque,
                              MemoryRegion *address_space_mem,
                              MemoryRegion *address_space_io,
                              uint8_t devfn_min, int nirq,
                              const char *typename);
void pci_unregister_root_bus(PCIBus *bus);
void pci_bus_set_route_irq_fn(PCIBus *, pci_route_irq_fn);
PCIINTxRoute pci_device_route_intx_to_irq(PCIDevice *dev, int pin);
bool pci_intx_route_changed(PCIINTxRoute *old, PCIINTxRoute *new);
void pci_bus_fire_intx_routing_notifier(PCIBus *bus);
void pci_device_set_intx_routing_notifier(PCIDevice *dev,
                                          PCIINTxRoutingNotifier notifier);
void pci_device_reset(PCIDevice *dev);

PCIDevice *pci_nic_init_nofail(NICInfo *nd, PCIBus *rootbus,
                               const char *default_model,
                               const char *default_devaddr);

PCIDevice *pci_vga_init(PCIBus *bus);

static inline PCIBus *pci_get_bus(const PCIDevice *dev)
{
    return PCI_BUS(qdev_get_parent_bus(DEVICE(dev)));
}
int pci_bus_num(PCIBus *s);
static inline int pci_dev_bus_num(const PCIDevice *dev)
{
    return pci_bus_num(pci_get_bus(dev));
}

int pci_bus_numa_node(PCIBus *bus);
void pci_for_each_device(PCIBus *bus, int bus_num,
                         void (*fn)(PCIBus *bus, PCIDevice *d, void *opaque),
                         void *opaque);
void pci_for_each_device_reverse(PCIBus *bus, int bus_num,
                                 void (*fn)(PCIBus *bus, PCIDevice *d,
                                            void *opaque),
                                 void *opaque);
void pci_for_each_bus_depth_first(PCIBus *bus,
                                  void *(*begin)(PCIBus *bus, void *parent_state),
                                  void (*end)(PCIBus *bus, void *state),
                                  void *parent_state);
PCIDevice *pci_get_function_0(PCIDevice *pci_dev);

/* Use this wrapper when specific scan order is not required. */
static inline
void pci_for_each_bus(PCIBus *bus,
                      void (*fn)(PCIBus *bus, void *opaque),
                      void *opaque)
{
    pci_for_each_bus_depth_first(bus, NULL, fn, opaque);
}

PCIBus *pci_device_root_bus(const PCIDevice *d);
const char *pci_root_bus_path(PCIDevice *dev);
PCIDevice *pci_find_device(PCIBus *bus, int bus_num, uint8_t devfn);
int pci_qdev_find_device(const char *id, PCIDevice **pdev);
void pci_bus_get_w64_range(PCIBus *bus, Range *range);

void pci_device_deassert_intx(PCIDevice *dev);

typedef AddressSpace *(*PCIIOMMUFunc)(PCIBus *, void *, int);

AddressSpace *pci_device_iommu_address_space(PCIDevice *dev);
void pci_setup_iommu(PCIBus *bus, PCIIOMMUFunc fn, void *opaque);

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
    stw_le_p(config, val);
}

static inline uint16_t
pci_get_word(const uint8_t *config)
{
    return lduw_le_p(config);
}

static inline void
pci_set_long(uint8_t *config, uint32_t val)
{
    stl_le_p(config, val);
}

static inline uint32_t
pci_get_long(const uint8_t *config)
{
    return ldl_le_p(config);
}

/*
 * PCI capabilities and/or their fields
 * are generally DWORD aligned only so
 * mechanism used by pci_set/get_quad()
 * must be tolerant to unaligned pointers
 *
 */
static inline void
pci_set_quad(uint8_t *config, uint64_t val)
{
    stq_le_p(config, val);
}

static inline uint64_t
pci_get_quad(const uint8_t *config)
{
    return ldq_le_p(config);
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

/* Access a register specified by a mask */
static inline void
pci_set_byte_by_mask(uint8_t *config, uint8_t mask, uint8_t reg)
{
    uint8_t val = pci_get_byte(config);
    uint8_t rval = reg << ctz32(mask);
    pci_set_byte(config, (~mask & val) | (mask & rval));
}

static inline uint8_t
pci_get_byte_by_mask(uint8_t *config, uint8_t mask)
{
    uint8_t val = pci_get_byte(config);
    return (val & mask) >> ctz32(mask);
}

static inline void
pci_set_word_by_mask(uint8_t *config, uint16_t mask, uint16_t reg)
{
    uint16_t val = pci_get_word(config);
    uint16_t rval = reg << ctz32(mask);
    pci_set_word(config, (~mask & val) | (mask & rval));
}

static inline uint16_t
pci_get_word_by_mask(uint8_t *config, uint16_t mask)
{
    uint16_t val = pci_get_word(config);
    return (val & mask) >> ctz32(mask);
}

static inline void
pci_set_long_by_mask(uint8_t *config, uint32_t mask, uint32_t reg)
{
    uint32_t val = pci_get_long(config);
    uint32_t rval = reg << ctz32(mask);
    pci_set_long(config, (~mask & val) | (mask & rval));
}

static inline uint32_t
pci_get_long_by_mask(uint8_t *config, uint32_t mask)
{
    uint32_t val = pci_get_long(config);
    return (val & mask) >> ctz32(mask);
}

static inline void
pci_set_quad_by_mask(uint8_t *config, uint64_t mask, uint64_t reg)
{
    uint64_t val = pci_get_quad(config);
    uint64_t rval = reg << ctz32(mask);
    pci_set_quad(config, (~mask & val) | (mask & rval));
}

static inline uint64_t
pci_get_quad_by_mask(uint8_t *config, uint64_t mask)
{
    uint64_t val = pci_get_quad(config);
    return (val & mask) >> ctz32(mask);
}

PCIDevice *pci_new_multifunction(int devfn, bool multifunction,
                                    const char *name);
PCIDevice *pci_new(int devfn, const char *name);
bool pci_realize_and_unref(PCIDevice *dev, PCIBus *bus, Error **errp);

PCIDevice *pci_create_simple_multifunction(PCIBus *bus, int devfn,
                                           bool multifunction,
                                           const char *name);
PCIDevice *pci_create_simple(PCIBus *bus, int devfn, const char *name);

void lsi53c8xx_handle_legacy_cmdline(DeviceState *lsi_dev);

qemu_irq pci_allocate_irq(PCIDevice *pci_dev);
void pci_set_irq(PCIDevice *pci_dev, int level);

static inline void pci_irq_assert(PCIDevice *pci_dev)
{
    pci_set_irq(pci_dev, 1);
}

static inline void pci_irq_deassert(PCIDevice *pci_dev)
{
    pci_set_irq(pci_dev, 0);
}

/*
 * FIXME: PCI does not work this way.
 * All the callers to this method should be fixed.
 */
static inline void pci_irq_pulse(PCIDevice *pci_dev)
{
    pci_irq_assert(pci_dev);
    pci_irq_deassert(pci_dev);
}

static inline int pci_is_express(const PCIDevice *d)
{
    return d->cap_present & QEMU_PCI_CAP_EXPRESS;
}

static inline int pci_is_express_downstream_port(const PCIDevice *d)
{
    uint8_t type;

    if (!pci_is_express(d) || !d->exp.exp_cap) {
        return 0;
    }

    type = pcie_cap_get_type(d);

    return type == PCI_EXP_TYPE_DOWNSTREAM || type == PCI_EXP_TYPE_ROOT_PORT;
}

static inline uint32_t pci_config_size(const PCIDevice *d)
{
    return pci_is_express(d) ? PCIE_CONFIG_SPACE_SIZE : PCI_CONFIG_SPACE_SIZE;
}

static inline uint16_t pci_get_bdf(PCIDevice *dev)
{
    return PCI_BUILD_BDF(pci_bus_num(pci_get_bus(dev)), dev->devfn);
}

uint16_t pci_requester_id(PCIDevice *dev);

/* DMA access functions */
static inline AddressSpace *pci_get_address_space(PCIDevice *dev)
{
    return &dev->bus_master_as;
}

static inline int pci_dma_rw(PCIDevice *dev, dma_addr_t addr,
                             void *buf, dma_addr_t len, DMADirection dir)
{
    return dma_memory_rw(pci_get_address_space(dev), addr, buf, len, dir);
}

static inline int pci_dma_read(PCIDevice *dev, dma_addr_t addr,
                               void *buf, dma_addr_t len)
{
    return pci_dma_rw(dev, addr, buf, len, DMA_DIRECTION_TO_DEVICE);
}

static inline int pci_dma_write(PCIDevice *dev, dma_addr_t addr,
                                const void *buf, dma_addr_t len)
{
    return pci_dma_rw(dev, addr, (void *) buf, len, DMA_DIRECTION_FROM_DEVICE);
}

#define PCI_DMA_DEFINE_LDST(_l, _s, _bits)                              \
    static inline uint##_bits##_t ld##_l##_pci_dma(PCIDevice *dev,      \
                                                   dma_addr_t addr)     \
    {                                                                   \
        return ld##_l##_dma(pci_get_address_space(dev), addr);          \
    }                                                                   \
    static inline void st##_s##_pci_dma(PCIDevice *dev,                 \
                                        dma_addr_t addr, uint##_bits##_t val) \
    {                                                                   \
        st##_s##_dma(pci_get_address_space(dev), addr, val);            \
    }

PCI_DMA_DEFINE_LDST(ub, b, 8);
PCI_DMA_DEFINE_LDST(uw_le, w_le, 16)
PCI_DMA_DEFINE_LDST(l_le, l_le, 32);
PCI_DMA_DEFINE_LDST(q_le, q_le, 64);
PCI_DMA_DEFINE_LDST(uw_be, w_be, 16)
PCI_DMA_DEFINE_LDST(l_be, l_be, 32);
PCI_DMA_DEFINE_LDST(q_be, q_be, 64);

#undef PCI_DMA_DEFINE_LDST

static inline void *pci_dma_map(PCIDevice *dev, dma_addr_t addr,
                                dma_addr_t *plen, DMADirection dir)
{
    void *buf;

    buf = dma_memory_map(pci_get_address_space(dev), addr, plen, dir);
    return buf;
}

static inline void pci_dma_unmap(PCIDevice *dev, void *buffer, dma_addr_t len,
                                 DMADirection dir, dma_addr_t access_len)
{
    dma_memory_unmap(pci_get_address_space(dev), buffer, len, dir, access_len);
}

static inline void pci_dma_sglist_init(QEMUSGList *qsg, PCIDevice *dev,
                                       int alloc_hint)
{
    qemu_sglist_init(qsg, DEVICE(dev), alloc_hint, pci_get_address_space(dev));
}

extern const VMStateDescription vmstate_pci_device;

#define VMSTATE_PCI_DEVICE(_field, _state) {                         \
    .name       = (stringify(_field)),                               \
    .size       = sizeof(PCIDevice),                                 \
    .vmsd       = &vmstate_pci_device,                               \
    .flags      = VMS_STRUCT,                                        \
    .offset     = vmstate_offset_value(_state, _field, PCIDevice),   \
}

#define VMSTATE_PCI_DEVICE_POINTER(_field, _state) {                 \
    .name       = (stringify(_field)),                               \
    .size       = sizeof(PCIDevice),                                 \
    .vmsd       = &vmstate_pci_device,                               \
    .flags      = VMS_STRUCT|VMS_POINTER,                            \
    .offset     = vmstate_offset_pointer(_state, _field, PCIDevice), \
}

MSIMessage pci_get_msi_message(PCIDevice *dev, int vector);

#endif

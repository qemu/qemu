#ifndef QEMU_PCI_DEVICE_H
#define QEMU_PCI_DEVICE_H

#include "hw/pci/pci.h"
#include "hw/pci/pcie.h"
#include "hw/pci/pcie_doe.h"
#include "system/spdm-socket.h"

#define TYPE_PCI_DEVICE "pci-device"
typedef struct PCIDeviceClass PCIDeviceClass;
DECLARE_OBJ_CHECKERS(PCIDevice, PCIDeviceClass,
                     PCI_DEVICE, TYPE_PCI_DEVICE)

/*
 * Implemented by devices that can be plugged on CXL buses. In the spec, this is
 * actually a "CXL Component, but we name it device to match the PCI naming.
 */
#define INTERFACE_CXL_DEVICE "cxl-device"

/* Implemented by devices that can be plugged on PCI Express buses */
#define INTERFACE_PCIE_DEVICE "pci-express-device"

/* Implemented by devices that can be plugged on Conventional PCI buses */
#define INTERFACE_CONVENTIONAL_PCI_DEVICE "conventional-pci-device"

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

    const char *romfile;                /* rom bar */

    bool sriov_vf_user_creatable;
};

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
    bool enabled;

    /* PCI config space */
    uint8_t *config;

    /*
     * Used to enable config checks on load. Note that writable bits are
     * never checked even if set in cmask.
     */
    uint8_t *cmask;

    /* Used to implement R/W bytes */
    uint8_t *wmask;

    /* Used to implement RW1C(Write 1 to Clear) bytes */
    uint8_t *w1cmask;

    /* Used to allocate config space for capabilities. */
    uint8_t *used;

    /* the following fields are read only */
    int32_t devfn;
    /*
     * Cached device to fetch requester ID from, to avoid the PCI tree
     * walking every time we invoke PCI request (e.g., MSI). For
     * conventional PCI root complex, this field is meaningless.
     */
    PCIReqIDCache requester_id_cache;
    char name[64];
    PCIIORegion io_regions[PCI_NUM_REGIONS];
    AddressSpace bus_master_as;
    bool is_master;
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

    /* Offset of PM capability in config space */
    uint8_t pm_cap;

    /* Offset of MSI-X capability in config space */
    uint8_t msix_cap;

    /* MSI-X entries */
    int msix_entries_nr;

    /* Space to store MSIX table & pending bit array */
    uint8_t *msix_table;
    uint8_t *msix_pba;

    /* May be used by INTx or MSI during interrupt notification */
    void *irq_opaque;

    MSITriggerFunc *msi_trigger;
    MSIPrepareMessageFunc *msi_prepare_message;
    MSIxPrepareMessageFunc *msix_prepare_message;

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
    uint32_t romsize;
    bool has_rom;
    MemoryRegion rom;
    int32_t rom_bar;

    /* INTx routing notifier */
    PCIINTxRoutingNotifier intx_routing_notifier;

    /* MSI-X notifiers */
    MSIVectorUseNotifier msix_vector_use_notifier;
    MSIVectorReleaseNotifier msix_vector_release_notifier;
    MSIVectorPollNotifier msix_vector_poll_notifier;

    /* SPDM */
    uint16_t spdm_port;
    SpdmTransportType spdm_trans;

    /* DOE */
    DOECap doe_spdm;

    /* ID of standby device in net_failover pair */
    char *failover_pair_id;
    uint32_t acpi_index;

    /*
     * Indirect DMA region bounce buffer size as configured for the device. This
     * is a configuration parameter that is reflected into bus_master_as when
     * realizing the device.
     */
    uint32_t max_bounce_buffer_size;

    char *sriov_pf;
};

static inline int pci_intx(PCIDevice *pci_dev)
{
    return pci_get_byte(pci_dev->config + PCI_INTERRUPT_PIN) - 1;
}

static inline int pci_is_cxl(const PCIDevice *d)
{
    return d->cap_present & QEMU_PCIE_CAP_CXL;
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

static inline int pci_is_vf(const PCIDevice *d)
{
    return d->sriov_pf || d->exp.sriov_vf.pf != NULL;
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

/**
 * pci_dma_rw: Read from or write to an address space from PCI device.
 *
 * Return a MemTxResult indicating whether the operation succeeded
 * or failed (eg unassigned memory, device rejected the transaction,
 * IOMMU fault).
 *
 * @dev: #PCIDevice doing the memory access
 * @addr: address within the #PCIDevice address space
 * @buf: buffer with the data transferred
 * @len: the number of bytes to read or write
 * @dir: indicates the transfer direction
 */
static inline MemTxResult pci_dma_rw(PCIDevice *dev, dma_addr_t addr,
                                     void *buf, dma_addr_t len,
                                     DMADirection dir, MemTxAttrs attrs)
{
    return dma_memory_rw(pci_get_address_space(dev), addr, buf, len,
                         dir, attrs);
}

/**
 * pci_dma_read: Read from an address space from PCI device.
 *
 * Return a MemTxResult indicating whether the operation succeeded
 * or failed (eg unassigned memory, device rejected the transaction,
 * IOMMU fault).  Called within RCU critical section.
 *
 * @dev: #PCIDevice doing the memory access
 * @addr: address within the #PCIDevice address space
 * @buf: buffer with the data transferred
 * @len: length of the data transferred
 */
static inline MemTxResult pci_dma_read(PCIDevice *dev, dma_addr_t addr,
                                       void *buf, dma_addr_t len)
{
    return pci_dma_rw(dev, addr, buf, len,
                      DMA_DIRECTION_TO_DEVICE, MEMTXATTRS_UNSPECIFIED);
}

/**
 * pci_dma_write: Write to address space from PCI device.
 *
 * Return a MemTxResult indicating whether the operation succeeded
 * or failed (eg unassigned memory, device rejected the transaction,
 * IOMMU fault).
 *
 * @dev: #PCIDevice doing the memory access
 * @addr: address within the #PCIDevice address space
 * @buf: buffer with the data transferred
 * @len: the number of bytes to write
 */
static inline MemTxResult pci_dma_write(PCIDevice *dev, dma_addr_t addr,
                                        const void *buf, dma_addr_t len)
{
    return pci_dma_rw(dev, addr, (void *) buf, len,
                      DMA_DIRECTION_FROM_DEVICE, MEMTXATTRS_UNSPECIFIED);
}

#define PCI_DMA_DEFINE_LDST(_l, _s, _bits) \
    static inline MemTxResult ld##_l##_pci_dma(PCIDevice *dev, \
                                               dma_addr_t addr, \
                                               uint##_bits##_t *val, \
                                               MemTxAttrs attrs) \
    { \
        return ld##_l##_dma(pci_get_address_space(dev), addr, val, attrs); \
    } \
    static inline MemTxResult st##_s##_pci_dma(PCIDevice *dev, \
                                               dma_addr_t addr, \
                                               uint##_bits##_t val, \
                                               MemTxAttrs attrs) \
    { \
        return st##_s##_dma(pci_get_address_space(dev), addr, val, attrs); \
    }

PCI_DMA_DEFINE_LDST(ub, b, 8);
PCI_DMA_DEFINE_LDST(uw_le, w_le, 16)
PCI_DMA_DEFINE_LDST(l_le, l_le, 32);
PCI_DMA_DEFINE_LDST(q_le, q_le, 64);
PCI_DMA_DEFINE_LDST(uw_be, w_be, 16)
PCI_DMA_DEFINE_LDST(l_be, l_be, 32);
PCI_DMA_DEFINE_LDST(q_be, q_be, 64);

#undef PCI_DMA_DEFINE_LDST

/**
 * pci_dma_map: Map device PCI address space range into host virtual address
 * @dev: #PCIDevice to be accessed
 * @addr: address within that device's address space
 * @plen: pointer to length of buffer; updated on return to indicate
 *        if only a subset of the requested range has been mapped
 * @dir: indicates the transfer direction
 *
 * Return: A host pointer, or %NULL if the resources needed to
 *         perform the mapping are exhausted (in that case *@plen
 *         is set to zero).
 */
static inline void *pci_dma_map(PCIDevice *dev, dma_addr_t addr,
                                dma_addr_t *plen, DMADirection dir)
{
    return dma_memory_map(pci_get_address_space(dev), addr, plen, dir,
                          MEMTXATTRS_UNSPECIFIED);
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
    .flags      = VMS_STRUCT | VMS_POINTER,                          \
    .offset     = vmstate_offset_pointer(_state, _field, PCIDevice), \
}

#endif

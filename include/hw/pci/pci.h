#ifndef QEMU_PCI_H
#define QEMU_PCI_H

#include "system/memory.h"
#include "system/dma.h"
#include "system/host_iommu_device.h"

/* PCI includes legacy ISA access.  */
#include "hw/isa/isa.h"

extern bool pci_available;

/* PCI bus */

#define PCI_DEVFN(slot, func)   ((((slot) & 0x1f) << 3) | ((func) & 0x07))
#define PCI_BUS_NUM(x)          (((x) >> 8) & 0xff)
#define PCI_SLOT(devfn)         (((devfn) >> 3) & 0x1f)
#define PCI_FUNC(devfn)         ((devfn) & 0x07)
#define PCI_BUILD_BDF(bus, devfn)     (((bus) << 8) | (devfn))
#define PCI_BDF_TO_DEVFN(x)     ((x) & 0xff)
#define PCI_BUS_MAX             256
#define PCI_DEVFN_MAX           256
#define PCI_SLOT_MAX            32
#define PCI_FUNC_MAX            8

#define PCI_SBDF(seg, bus, dev, func) \
            ((((uint32_t)(seg)) << 16) | \
            (PCI_BUILD_BDF(bus, PCI_DEVFN(dev, func))))

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

/* legacy virtio-pci devices */
#define PCI_DEVICE_ID_VIRTIO_NET         0x1000
#define PCI_DEVICE_ID_VIRTIO_BLOCK       0x1001
#define PCI_DEVICE_ID_VIRTIO_BALLOON     0x1002
#define PCI_DEVICE_ID_VIRTIO_CONSOLE     0x1003
#define PCI_DEVICE_ID_VIRTIO_SCSI        0x1004
#define PCI_DEVICE_ID_VIRTIO_RNG         0x1005
#define PCI_DEVICE_ID_VIRTIO_9P          0x1009
#define PCI_DEVICE_ID_VIRTIO_VSOCK       0x1012

/*
 * modern virtio-pci devices get their id assigned automatically,
 * there is no need to add #defines here.  It gets calculated as
 *
 * PCI_DEVICE_ID = PCI_DEVICE_ID_VIRTIO_10_BASE +
 *                 virtio_bus_get_vdev_id(bus)
 */
#define PCI_DEVICE_ID_VIRTIO_10_BASE     0x1040

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
#define PCI_DEVICE_ID_REDHAT_NVME        0x0010
#define PCI_DEVICE_ID_REDHAT_PVPANIC     0x0011
#define PCI_DEVICE_ID_REDHAT_ACPI_ERST   0x0012
#define PCI_DEVICE_ID_REDHAT_UFS         0x0013
#define PCI_DEVICE_ID_REDHAT_RISCV_IOMMU 0x0014
#define PCI_DEVICE_ID_REDHAT_QXL         0x0100

#define FMT_PCIBUS                      PRIx64

typedef uint64_t pcibus_t;

struct PCIHostDeviceAddress {
    unsigned int domain;
    unsigned int bus;
    unsigned int slot;
    unsigned int function;
};

/*
 * Represents the Address Type (AT) field in a PCI request,
 * see MemTxAttrs.address_type
 */
typedef enum PCIAddressType {
    PCI_AT_UNTRANSLATED = 0, /* Default when no attribute is set */
    PCI_AT_TRANSLATED = 1,
} PCIAddressType;

typedef void PCIConfigWriteFunc(PCIDevice *pci_dev,
                                uint32_t address, uint32_t data, int len);
typedef uint32_t PCIConfigReadFunc(PCIDevice *pci_dev,
                                   uint32_t address, int len);
typedef void PCIMapIORegionFunc(PCIDevice *pci_dev, int region_num,
                                pcibus_t addr, pcibus_t size, int type);
typedef void PCIUnregisterFunc(PCIDevice *pci_dev);

typedef void MSITriggerFunc(PCIDevice *dev, MSIMessage msg);
typedef MSIMessage MSIPrepareMessageFunc(PCIDevice *dev, unsigned vector);
typedef MSIMessage MSIxPrepareMessageFunc(PCIDevice *dev, unsigned vector);

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
#define QEMU_PCIE_CXL_BITNR 10
    QEMU_PCIE_CAP_CXL = (1 << QEMU_PCIE_CXL_BITNR),
#define QEMU_PCIE_ERR_UNC_MASK_BITNR 11
    QEMU_PCIE_ERR_UNC_MASK = (1 << QEMU_PCIE_ERR_UNC_MASK_BITNR),
#define QEMU_PCIE_ARI_NEXTFN_1_BITNR 12
    QEMU_PCIE_ARI_NEXTFN_1 = (1 << QEMU_PCIE_ARI_NEXTFN_1_BITNR),
#define QEMU_PCIE_EXT_TAG_BITNR 13
    QEMU_PCIE_EXT_TAG = (1 << QEMU_PCIE_EXT_TAG_BITNR),
#define QEMU_PCI_CAP_PM_BITNR 14
    QEMU_PCI_CAP_PM = (1 << QEMU_PCI_CAP_PM_BITNR),
#define QEMU_PCI_SKIP_RESET_ON_CPR_BITNR 15
    QEMU_PCI_SKIP_RESET_ON_CPR = (1 << QEMU_PCI_SKIP_RESET_ON_CPR_BITNR),
};

typedef struct PCIINTxRoute {
    enum {
        PCI_INTX_ENABLED,
        PCI_INTX_INVERTED,
        PCI_INTX_DISABLED,
    } mode;
    int irq;
} PCIINTxRoute;

typedef void (*PCIINTxRoutingNotifier)(PCIDevice *dev);
typedef int (*MSIVectorUseNotifier)(PCIDevice *dev, unsigned int vector,
                                      MSIMessage msg);
typedef void (*MSIVectorReleaseNotifier)(PCIDevice *dev, unsigned int vector);
typedef void (*MSIVectorPollNotifier)(PCIDevice *dev,
                                      unsigned int vector_start,
                                      unsigned int vector_end);

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
#define TYPE_CXL_BUS "CXL"

typedef void (*pci_bus_dev_fn)(PCIBus *b, PCIDevice *d, void *opaque);
typedef void (*pci_bus_fn)(PCIBus *b, void *opaque);
typedef void *(*pci_bus_ret_fn)(PCIBus *b, void *opaque);

bool pci_bus_is_express(const PCIBus *bus);

void pci_root_bus_init(PCIBus *bus, size_t bus_size, DeviceState *parent,
                       const char *name,
                       MemoryRegion *mem, MemoryRegion *io,
                       uint8_t devfn_min, const char *typename);
PCIBus *pci_root_bus_new(DeviceState *parent, const char *name,
                         MemoryRegion *mem, MemoryRegion *io,
                         uint8_t devfn_min, const char *typename);
void pci_root_bus_cleanup(PCIBus *bus);
void pci_bus_irqs(PCIBus *bus, pci_set_irq_fn set_irq,
                  void *irq_opaque, int nirq);
void pci_bus_map_irqs(PCIBus *bus, pci_map_irq_fn map_irq);
void pci_bus_irqs_cleanup(PCIBus *bus);
int pci_bus_get_irq_level(PCIBus *bus, int irq_num);
uint32_t pci_bus_get_slot_reserved_mask(PCIBus *bus);
void pci_bus_set_slot_reserved_mask(PCIBus *bus, uint32_t mask);
void pci_bus_clear_slot_reserved_mask(PCIBus *bus, uint32_t mask);
bool pci_bus_add_fw_cfg_extra_pci_roots(FWCfgState *fw_cfg,
                                        PCIBus *bus,
                                        Error **errp);
/* 0 <= pin <= 3 0 = INTA, 1 = INTB, 2 = INTC, 3 = INTD */
static inline int pci_swizzle(int slot, int pin)
{
    return (slot + pin) % PCI_NUM_PINS;
}
int pci_swizzle_map_irq_fn(PCIDevice *pci_dev, int pin);
PCIBus *pci_register_root_bus(DeviceState *parent, const char *name,
                              pci_set_irq_fn set_irq, pci_map_irq_fn map_irq,
                              void *irq_opaque,
                              MemoryRegion *mem, MemoryRegion *io,
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

void pci_init_nic_devices(PCIBus *bus, const char *default_model);
bool pci_init_nic_in_slot(PCIBus *rootbus, const char *default_model,
                          const char *alias, const char *devaddr);
PCIDevice *pci_vga_init(PCIBus *bus);

static inline PCIBus *pci_get_bus(const PCIDevice *dev)
{
    return PCI_BUS(qdev_get_parent_bus(DEVICE(dev)));
}
int pci_bus_num(PCIBus *s);
void pci_bus_range(PCIBus *bus, int *min_bus, int *max_bus);
static inline int pci_dev_bus_num(const PCIDevice *dev)
{
    return pci_bus_num(pci_get_bus(dev));
}

int pci_bus_numa_node(PCIBus *bus);
void pci_for_each_device(PCIBus *bus, int bus_num,
                         pci_bus_dev_fn fn,
                         void *opaque);
void pci_for_each_device_reverse(PCIBus *bus, int bus_num,
                                 pci_bus_dev_fn fn,
                                 void *opaque);
void pci_for_each_device_under_bus(PCIBus *bus,
                                   pci_bus_dev_fn fn, void *opaque);
void pci_for_each_device_under_bus_reverse(PCIBus *bus,
                                           pci_bus_dev_fn fn,
                                           void *opaque);
void pci_for_each_bus_depth_first(PCIBus *bus, pci_bus_ret_fn begin,
                                  pci_bus_fn end, void *parent_state);
PCIDevice *pci_get_function_0(PCIDevice *pci_dev);

/* Use this wrapper when specific scan order is not required. */
static inline
void pci_for_each_bus(PCIBus *bus, pci_bus_fn fn, void *opaque)
{
    pci_for_each_bus_depth_first(bus, NULL, fn, opaque);
}

PCIBus *pci_device_root_bus(const PCIDevice *d);
const char *pci_root_bus_path(PCIDevice *dev);
bool pci_bus_bypass_iommu(PCIBus *bus);
PCIDevice *pci_find_device(PCIBus *bus, int bus_num, uint8_t devfn);
int pci_qdev_find_device(const char *id, PCIDevice **pdev);
void pci_bus_get_w64_range(PCIBus *bus, Range *range);

void pci_device_deassert_intx(PCIDevice *dev);

/* Page Request Interface */
typedef enum {
    IOMMU_PRI_RESP_SUCCESS,
    IOMMU_PRI_RESP_INVALID_REQUEST,
    IOMMU_PRI_RESP_FAILURE,
} IOMMUPRIResponseCode;

typedef struct IOMMUPRIResponse {
    IOMMUPRIResponseCode response_code;
    uint16_t prgi;
} IOMMUPRIResponse;

struct IOMMUPRINotifier;

typedef void (*IOMMUPRINotify)(struct IOMMUPRINotifier *notifier,
                               IOMMUPRIResponse *response);

typedef struct IOMMUPRINotifier {
    IOMMUPRINotify notify;
} IOMMUPRINotifier;

#define PCI_PRI_PRGI_MASK 0x1ffU

/**
 * struct PCIIOMMUOps: callbacks structure for specific IOMMU handlers
 * of a PCIBus
 *
 * Allows to modify the behavior of some IOMMU operations of the PCI
 * framework for a set of devices on a PCI bus.
 */
typedef struct PCIIOMMUOps {
    /**
     * @get_address_space: get the address space for a set of devices
     * on a PCI bus.
     *
     * Mandatory callback which returns a pointer to an #AddressSpace
     *
     * @bus: the #PCIBus being accessed.
     *
     * @opaque: the data passed to pci_setup_iommu().
     *
     * @devfn: device and function number
     */
    AddressSpace * (*get_address_space)(PCIBus *bus, void *opaque, int devfn);
    /**
     * @set_iommu_device: attach a HostIOMMUDevice to a vIOMMU
     *
     * Optional callback, if not implemented in vIOMMU, then vIOMMU can't
     * retrieve host information from the associated HostIOMMUDevice.
     *
     * @bus: the #PCIBus of the PCI device.
     *
     * @opaque: the data passed to pci_setup_iommu().
     *
     * @devfn: device and function number of the PCI device.
     *
     * @dev: the #HostIOMMUDevice to attach.
     *
     * @errp: pass an Error out only when return false
     *
     * Returns: true if HostIOMMUDevice is attached or else false with errp set.
     */
    bool (*set_iommu_device)(PCIBus *bus, void *opaque, int devfn,
                             HostIOMMUDevice *dev, Error **errp);
    /**
     * @unset_iommu_device: detach a HostIOMMUDevice from a vIOMMU
     *
     * Optional callback.
     *
     * @bus: the #PCIBus of the PCI device.
     *
     * @opaque: the data passed to pci_setup_iommu().
     *
     * @devfn: device and function number of the PCI device.
     */
    void (*unset_iommu_device)(PCIBus *bus, void *opaque, int devfn);
    /**
     * @get_iotlb_info: get properties required to initialize a device IOTLB.
     *
     * Callback required if devices are allowed to cache translations.
     *
     * @opaque: the data passed to pci_setup_iommu().
     *
     * @addr_width: the address width of the IOMMU (output parameter).
     *
     * @min_page_size: the page size of the IOMMU (output parameter).
     */
    void (*get_iotlb_info)(void *opaque, uint8_t *addr_width,
                           uint32_t *min_page_size);
    /**
     * @init_iotlb_notifier: initialize an IOMMU notifier.
     *
     * Optional callback.
     *
     * @bus: the #PCIBus of the PCI device.
     *
     * @opaque: the data passed to pci_setup_iommu().
     *
     * @devfn: device and function number of the PCI device.
     *
     * @n: the notifier to be initialized.
     *
     * @fn: the callback to be installed.
     *
     * @user_opaque: a user pointer that can be used to track a state.
     */
    void (*init_iotlb_notifier)(PCIBus *bus, void *opaque, int devfn,
                                IOMMUNotifier *n, IOMMUNotify fn,
                                void *user_opaque);
    /**
     * @register_iotlb_notifier: setup an IOTLB invalidation notifier.
     *
     * Callback required if devices are allowed to cache translations.
     *
     * @bus: the #PCIBus of the PCI device.
     *
     * @opaque: the data passed to pci_setup_iommu().
     *
     * @devfn: device and function number of the PCI device.
     *
     * @pasid: the pasid of the address space to watch.
     *
     * @n: the notifier to register.
     */
    void (*register_iotlb_notifier)(PCIBus *bus, void *opaque, int devfn,
                                    uint32_t pasid, IOMMUNotifier *n);
    /**
     * @unregister_iotlb_notifier: remove an IOTLB invalidation notifier.
     *
     * Callback required if devices are allowed to cache translations.
     *
     * @bus: the #PCIBus of the PCI device.
     *
     * @opaque: the data passed to pci_setup_iommu().
     *
     * @devfn: device and function number of the PCI device.
     *
     * @pasid: the pasid of the address space to stop watching.
     *
     * @n: the notifier to unregister.
     */
    void (*unregister_iotlb_notifier)(PCIBus *bus, void *opaque, int devfn,
                                      uint32_t pasid, IOMMUNotifier *n);
    /**
     * @ats_request_translation: issue an ATS request.
     *
     * Callback required if devices are allowed to use the address
     * translation service.
     *
     * @bus: the #PCIBus of the PCI device.
     *
     * @opaque: the data passed to pci_setup_iommu().
     *
     * @devfn: device and function number of the PCI device.
     *
     * @pasid: the pasid of the address space to use for the request.
     *
     * @priv_req: privileged mode bit (PASID TLP).
     *
     * @exec_req: execute request bit (PASID TLP).
     *
     * @addr: start address of the memory range to be translated.
     *
     * @length: length of the memory range in bytes.
     *
     * @no_write: request a read-only translation (if supported).
     *
     * @result: buffer in which the TLB entries will be stored.
     *
     * @result_length: result buffer length.
     *
     * @err_count: number of untranslated subregions.
     *
     * Returns: the number of translations stored in the result buffer, or
     * -ENOMEM if the buffer is not large enough.
     */
    ssize_t (*ats_request_translation)(PCIBus *bus, void *opaque, int devfn,
                                       uint32_t pasid, bool priv_req,
                                       bool exec_req, hwaddr addr,
                                       size_t length, bool no_write,
                                       IOMMUTLBEntry *result,
                                       size_t result_length,
                                       uint32_t *err_count);
    /**
     * @pri_register_notifier: setup the PRI completion callback.
     *
     * Callback required if devices are allowed to use the page request
     * interface.
     *
     * @bus: the #PCIBus of the PCI device.
     *
     * @opaque: the data passed to pci_setup_iommu().
     *
     * @devfn: device and function number of the PCI device.
     *
     * @pasid: the pasid of the address space to track.
     *
     * @notifier: the notifier to register.
     */
    void (*pri_register_notifier)(PCIBus *bus, void *opaque, int devfn,
                                  uint32_t pasid, IOMMUPRINotifier *notifier);
    /**
     * @pri_unregister_notifier: remove the PRI completion callback.
     *
     * Callback required if devices are allowed to use the page request
     * interface.
     *
     * @bus: the #PCIBus of the PCI device.
     *
     * @opaque: the data passed to pci_setup_iommu().
     *
     * @devfn: device and function number of the PCI device.
     *
     * @pasid: the pasid of the address space to stop tracking.
     */
    void (*pri_unregister_notifier)(PCIBus *bus, void *opaque, int devfn,
                                    uint32_t pasid);
    /**
     * @pri_request_page: issue a PRI request.
     *
     * Callback required if devices are allowed to use the page request
     * interface.
     *
     * @bus: the #PCIBus of the PCI device.
     *
     * @opaque: the data passed to pci_setup_iommu().
     *
     * @devfn: device and function number of the PCI device.
     *
     * @pasid: the pasid of the address space to use for the request.
     *
     * @priv_req: privileged mode bit (PASID TLP).
     *
     * @exec_req: execute request bit (PASID TLP).
     *
     * @addr: untranslated address of the requested page.
     *
     * @lpig: last page in group.
     *
     * @prgi: page request group index.
     *
     * @is_read: request read access.
     *
     * @is_write: request write access.
     */
    int (*pri_request_page)(PCIBus *bus, void *opaque, int devfn,
                            uint32_t pasid, bool priv_req, bool exec_req,
                            hwaddr addr, bool lpig, uint16_t prgi, bool is_read,
                            bool is_write);
} PCIIOMMUOps;

AddressSpace *pci_device_iommu_address_space(PCIDevice *dev);
bool pci_device_set_iommu_device(PCIDevice *dev, HostIOMMUDevice *hiod,
                                 Error **errp);
void pci_device_unset_iommu_device(PCIDevice *dev);

/**
 * pci_iommu_get_iotlb_info: get properties required to initialize a
 * device IOTLB.
 *
 * Returns 0 on success, or a negative errno otherwise.
 *
 * @dev: the device that wants to get the information.
 * @addr_width: the address width of the IOMMU (output parameter).
 * @min_page_size: the page size of the IOMMU (output parameter).
 */
int pci_iommu_get_iotlb_info(PCIDevice *dev, uint8_t *addr_width,
                             uint32_t *min_page_size);

/**
 * pci_iommu_init_iotlb_notifier: initialize an IOMMU notifier.
 *
 * This function is used by devices before registering an IOTLB notifier.
 *
 * @dev: the device.
 * @n: the notifier to be initialized.
 * @fn: the callback to be installed.
 * @opaque: a user pointer that can be used to track a state.
 */
int pci_iommu_init_iotlb_notifier(PCIDevice *dev, IOMMUNotifier *n,
                                  IOMMUNotify fn, void *opaque);

/**
 * pci_ats_request_translation: perform an ATS request.
 *
 * Returns the number of translations stored in @result in case of success,
 * a negative error code otherwise.
 * -ENOMEM is returned when the result buffer is not large enough to store
 * all the translations.
 *
 * @dev: the ATS-capable PCI device.
 * @pasid: the pasid of the address space in which the translation will be done.
 * @priv_req: privileged mode bit (PASID TLP).
 * @exec_req: execute request bit (PASID TLP).
 * @addr: start address of the memory range to be translated.
 * @length: length of the memory range in bytes.
 * @no_write: request a read-only translation (if supported).
 * @result: buffer in which the TLB entries will be stored.
 * @result_length: result buffer length.
 * @err_count: number of untranslated subregions.
 */
ssize_t pci_ats_request_translation(PCIDevice *dev, uint32_t pasid,
                                    bool priv_req, bool exec_req,
                                    hwaddr addr, size_t length,
                                    bool no_write, IOMMUTLBEntry *result,
                                    size_t result_length,
                                    uint32_t *err_count);

/**
 * pci_pri_request_page: perform a PRI request.
 *
 * Returns 0 if the PRI request has been sent to the guest OS,
 * an error code otherwise.
 *
 * @dev: the PRI-capable PCI device.
 * @pasid: the pasid of the address space in which the translation will be done.
 * @priv_req: privileged mode bit (PASID TLP).
 * @exec_req: execute request bit (PASID TLP).
 * @addr: untranslated address of the requested page.
 * @lpig: last page in group.
 * @prgi: page request group index.
 * @is_read: request read access.
 * @is_write: request write access.
 */
int pci_pri_request_page(PCIDevice *dev, uint32_t pasid, bool priv_req,
                         bool exec_req, hwaddr addr, bool lpig,
                         uint16_t prgi, bool is_read, bool is_write);

/**
 * pci_pri_register_notifier: register the PRI callback for a given address
 * space.
 *
 * Returns 0 on success, an error code otherwise.
 *
 * @dev: the PRI-capable PCI device.
 * @pasid: the pasid of the address space to track.
 * @notifier: the notifier to register.
 */
int pci_pri_register_notifier(PCIDevice *dev, uint32_t pasid,
                              IOMMUPRINotifier *notifier);

/**
 * pci_pri_unregister_notifier: remove the PRI callback from a given address
 * space.
 *
 * @dev: the PRI-capable PCI device.
 * @pasid: the pasid of the address space to stop tracking.
 */
void pci_pri_unregister_notifier(PCIDevice *dev, uint32_t pasid);

/**
 * pci_iommu_register_iotlb_notifier: register a notifier for changes to
 * IOMMU translation entries in a specific address space.
 *
 * Returns 0 on success, or a negative errno otherwise.
 *
 * @dev: the device that wants to get notified.
 * @pasid: the pasid of the address space to track.
 * @n: the notifier to register.
 */
int pci_iommu_register_iotlb_notifier(PCIDevice *dev, uint32_t pasid,
                                      IOMMUNotifier *n);

/**
 * pci_iommu_unregister_iotlb_notifier: unregister a notifier that has been
 * registered with pci_iommu_register_iotlb_notifier.
 *
 * Returns 0 on success, or a negative errno otherwise.
 *
 * @dev: the device that wants to stop notifications.
 * @pasid: the pasid of the address space to stop tracking.
 * @n: the notifier to unregister.
 */
int pci_iommu_unregister_iotlb_notifier(PCIDevice *dev, uint32_t pasid,
                                        IOMMUNotifier *n);

/**
 * pci_setup_iommu: Initialize specific IOMMU handlers for a PCIBus
 *
 * Let PCI host bridges define specific operations.
 *
 * @bus: the #PCIBus being updated.
 * @ops: the #PCIIOMMUOps
 * @opaque: passed to callbacks of the @ops structure.
 */
void pci_setup_iommu(PCIBus *bus, const PCIIOMMUOps *ops, void *opaque);

void pci_setup_iommu_per_bus(PCIBus *bus, const PCIIOMMUOps *ops, void *opaque);

pcibus_t pci_bar_address(PCIDevice *d,
                         int reg, uint8_t type, pcibus_t size);

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
    uint8_t rval;

    assert(mask);
    rval = reg << ctz32(mask);
    pci_set_byte(config, (~mask & val) | (mask & rval));
}

static inline void
pci_set_word_by_mask(uint8_t *config, uint16_t mask, uint16_t reg)
{
    uint16_t val = pci_get_word(config);
    uint16_t rval;

    assert(mask);
    rval = reg << ctz32(mask);
    pci_set_word(config, (~mask & val) | (mask & rval));
}

static inline void
pci_set_long_by_mask(uint8_t *config, uint32_t mask, uint32_t reg)
{
    uint32_t val = pci_get_long(config);
    uint32_t rval;

    assert(mask);
    rval = reg << ctz32(mask);
    pci_set_long(config, (~mask & val) | (mask & rval));
}

static inline void
pci_set_quad_by_mask(uint8_t *config, uint64_t mask, uint64_t reg)
{
    uint64_t val = pci_get_quad(config);
    uint64_t rval;

    assert(mask);
    rval = reg << ctz32(mask);
    pci_set_quad(config, (~mask & val) | (mask & rval));
}

PCIDevice *pci_new_multifunction(int devfn, const char *name);
PCIDevice *pci_new(int devfn, const char *name);
bool pci_realize_and_unref(PCIDevice *dev, PCIBus *bus, Error **errp);

PCIDevice *pci_create_simple_multifunction(PCIBus *bus, int devfn,
                                           const char *name);
PCIDevice *pci_create_simple(PCIBus *bus, int devfn, const char *name);

void lsi53c8xx_handle_legacy_cmdline(DeviceState *lsi_dev);

qemu_irq pci_allocate_irq(PCIDevice *pci_dev);
void pci_set_irq(PCIDevice *pci_dev, int level);
int pci_irq_disabled(PCIDevice *d);

static inline void pci_irq_assert(PCIDevice *pci_dev)
{
    pci_set_irq(pci_dev, 1);
}

static inline void pci_irq_deassert(PCIDevice *pci_dev)
{
    pci_set_irq(pci_dev, 0);
}

MSIMessage pci_get_msi_message(PCIDevice *dev, int vector);
void pci_set_enabled(PCIDevice *pci_dev, bool state);
void pci_set_power(PCIDevice *pci_dev, bool state);
int pci_pm_init(PCIDevice *pci_dev, uint8_t offset, Error **errp);

#endif

/*
 * vfio based device assignment support - PCI devices
 *
 * Copyright Red Hat, Inc. 2012-2015
 *
 * Authors:
 *  Alex Williamson <alex.williamson@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */
#ifndef HW_VFIO_VFIO_PCI_H
#define HW_VFIO_VFIO_PCI_H

#include "exec/memory.h"
#include "hw/pci/pci_device.h"
#include "hw/vfio/vfio-common.h"
#include "qemu/event_notifier.h"
#include "qemu/queue.h"
#include "qemu/timer.h"
#include "qom/object.h"
#include "sysemu/kvm.h"

#define PCI_ANY_ID (~0)

struct VFIOPCIDevice;

typedef struct VFIOIOEventFD {
    QLIST_ENTRY(VFIOIOEventFD) next;
    MemoryRegion *mr;
    hwaddr addr;
    unsigned size;
    uint64_t data;
    EventNotifier e;
    VFIORegion *region;
    hwaddr region_addr;
    bool dynamic; /* Added runtime, removed on device reset */
    bool vfio;
} VFIOIOEventFD;

typedef struct VFIOQuirk {
    QLIST_ENTRY(VFIOQuirk) next;
    void *data;
    QLIST_HEAD(, VFIOIOEventFD) ioeventfds;
    int nr_mem;
    MemoryRegion *mem;
    void (*reset)(struct VFIOPCIDevice *vdev, struct VFIOQuirk *quirk);
} VFIOQuirk;

typedef struct VFIOBAR {
    VFIORegion region;
    MemoryRegion *mr;
    size_t size;
    uint8_t type;
    bool ioport;
    bool mem64;
    QLIST_HEAD(, VFIOQuirk) quirks;
} VFIOBAR;

typedef struct VFIOVGARegion {
    MemoryRegion mem;
    off_t offset;
    int nr;
    QLIST_HEAD(, VFIOQuirk) quirks;
} VFIOVGARegion;

typedef struct VFIOVGA {
    off_t fd_offset;
    int fd;
    VFIOVGARegion region[QEMU_PCI_VGA_NUM_REGIONS];
} VFIOVGA;

typedef struct VFIOINTx {
    bool pending; /* interrupt pending */
    bool kvm_accel; /* set when QEMU bypass through KVM enabled */
    uint8_t pin; /* which pin to pull for qemu_set_irq */
    EventNotifier interrupt; /* eventfd triggered on interrupt */
    EventNotifier unmask; /* eventfd for unmask on QEMU bypass */
    PCIINTxRoute route; /* routing info for QEMU bypass */
    uint32_t mmap_timeout; /* delay to re-enable mmaps after interrupt */
    QEMUTimer *mmap_timer; /* enable mmaps after periods w/o interrupts */
} VFIOINTx;

typedef struct VFIOMSIVector {
    /*
     * Two interrupt paths are configured per vector.  The first, is only used
     * for interrupts injected via QEMU.  This is typically the non-accel path,
     * but may also be used when we want QEMU to handle masking and pending
     * bits.  The KVM path bypasses QEMU and is therefore higher performance,
     * but requires masking at the device.  virq is used to track the MSI route
     * through KVM, thus kvm_interrupt is only available when virq is set to a
     * valid (>= 0) value.
     */
    EventNotifier interrupt;
    EventNotifier kvm_interrupt;
    struct VFIOPCIDevice *vdev; /* back pointer to device */
    int virq;
    bool use;
} VFIOMSIVector;

enum {
    VFIO_INT_NONE = 0,
    VFIO_INT_INTx = 1,
    VFIO_INT_MSI  = 2,
    VFIO_INT_MSIX = 3,
};

/* Cache of MSI-X setup */
typedef struct VFIOMSIXInfo {
    uint8_t table_bar;
    uint8_t pba_bar;
    uint16_t entries;
    uint32_t table_offset;
    uint32_t pba_offset;
    unsigned long *pending;
} VFIOMSIXInfo;

#define TYPE_VFIO_PCI "vfio-pci"
OBJECT_DECLARE_SIMPLE_TYPE(VFIOPCIDevice, VFIO_PCI)

struct VFIOPCIDevice {
    PCIDevice pdev;
    VFIODevice vbasedev;
    VFIOINTx intx;
    unsigned int config_size;
    uint8_t *emulated_config_bits; /* QEMU emulated bits, little-endian */
    off_t config_offset; /* Offset of config space region within device fd */
    unsigned int rom_size;
    off_t rom_offset; /* Offset of ROM region within device fd */
    void *rom;
    int msi_cap_size;
    VFIOMSIVector *msi_vectors;
    VFIOMSIXInfo *msix;
    int nr_vectors; /* Number of MSI/MSIX vectors currently in use */
    int interrupt; /* Current interrupt type */
    VFIOBAR bars[PCI_NUM_REGIONS - 1]; /* No ROM */
    VFIOVGA *vga; /* 0xa0000, 0x3b0, 0x3c0 */
    void *igd_opregion;
    PCIHostDeviceAddress host;
    EventNotifier err_notifier;
    EventNotifier req_notifier;
    int (*resetfn)(struct VFIOPCIDevice *);
    uint32_t vendor_id;
    uint32_t device_id;
    uint32_t sub_vendor_id;
    uint32_t sub_device_id;
    uint32_t features;
#define VFIO_FEATURE_ENABLE_VGA_BIT 0
#define VFIO_FEATURE_ENABLE_VGA (1 << VFIO_FEATURE_ENABLE_VGA_BIT)
#define VFIO_FEATURE_ENABLE_REQ_BIT 1
#define VFIO_FEATURE_ENABLE_REQ (1 << VFIO_FEATURE_ENABLE_REQ_BIT)
#define VFIO_FEATURE_ENABLE_IGD_OPREGION_BIT 2
#define VFIO_FEATURE_ENABLE_IGD_OPREGION \
                                (1 << VFIO_FEATURE_ENABLE_IGD_OPREGION_BIT)
    OnOffAuto display;
    uint32_t display_xres;
    uint32_t display_yres;
    int32_t bootindex;
    uint32_t igd_gms;
    OffAutoPCIBAR msix_relo;
    uint8_t pm_cap;
    uint8_t nv_gpudirect_clique;
    bool pci_aer;
    bool req_enabled;
    bool has_flr;
    bool has_pm_reset;
    bool rom_read_failed;
    bool no_kvm_intx;
    bool no_kvm_msi;
    bool no_kvm_msix;
    bool no_geforce_quirks;
    bool no_kvm_ioeventfd;
    bool no_vfio_ioeventfd;
    bool enable_ramfb;
    bool defer_kvm_irq_routing;
    VFIODisplay *dpy;
    Notifier irqchip_change_notifier;
};

/* Use uin32_t for vendor & device so PCI_ANY_ID expands and cannot match hw */
static inline bool vfio_pci_is(VFIOPCIDevice *vdev, uint32_t vendor, uint32_t device)
{
    return (vendor == PCI_ANY_ID || vendor == vdev->vendor_id) &&
           (device == PCI_ANY_ID || device == vdev->device_id);
}

static inline bool vfio_is_vga(VFIOPCIDevice *vdev)
{
    PCIDevice *pdev = &vdev->pdev;
    uint16_t class = pci_get_word(pdev->config + PCI_CLASS_DEVICE);

    return class == PCI_CLASS_DISPLAY_VGA;
}

uint32_t vfio_pci_read_config(PCIDevice *pdev, uint32_t addr, int len);
void vfio_pci_write_config(PCIDevice *pdev,
                           uint32_t addr, uint32_t val, int len);

uint64_t vfio_vga_read(void *opaque, hwaddr addr, unsigned size);
void vfio_vga_write(void *opaque, hwaddr addr, uint64_t data, unsigned size);

bool vfio_opt_rom_in_denylist(VFIOPCIDevice *vdev);
void vfio_vga_quirk_setup(VFIOPCIDevice *vdev);
void vfio_vga_quirk_exit(VFIOPCIDevice *vdev);
void vfio_vga_quirk_finalize(VFIOPCIDevice *vdev);
void vfio_bar_quirk_setup(VFIOPCIDevice *vdev, int nr);
void vfio_bar_quirk_exit(VFIOPCIDevice *vdev, int nr);
void vfio_bar_quirk_finalize(VFIOPCIDevice *vdev, int nr);
void vfio_setup_resetfn_quirk(VFIOPCIDevice *vdev);
int vfio_add_virt_caps(VFIOPCIDevice *vdev, Error **errp);
void vfio_quirk_reset(VFIOPCIDevice *vdev);
VFIOQuirk *vfio_quirk_alloc(int nr_mem);
void vfio_probe_igd_bar4_quirk(VFIOPCIDevice *vdev, int nr);

extern const PropertyInfo qdev_prop_nv_gpudirect_clique;

int vfio_populate_vga(VFIOPCIDevice *vdev, Error **errp);

int vfio_pci_igd_opregion_init(VFIOPCIDevice *vdev,
                               struct vfio_region_info *info,
                               Error **errp);
int vfio_pci_nvidia_v100_ram_init(VFIOPCIDevice *vdev, Error **errp);
int vfio_pci_nvlink2_init(VFIOPCIDevice *vdev, Error **errp);

void vfio_display_reset(VFIOPCIDevice *vdev);
int vfio_display_probe(VFIOPCIDevice *vdev, Error **errp);
void vfio_display_finalize(VFIOPCIDevice *vdev);

#endif /* HW_VFIO_VFIO_PCI_H */

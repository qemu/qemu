#ifndef XEN_HOST_PCI_DEVICE_H
#define XEN_HOST_PCI_DEVICE_H

#include "hw/pci/pci.h"

enum {
    XEN_HOST_PCI_REGION_TYPE_IO = 1 << 1,
    XEN_HOST_PCI_REGION_TYPE_MEM = 1 << 2,
    XEN_HOST_PCI_REGION_TYPE_PREFETCH = 1 << 3,
    XEN_HOST_PCI_REGION_TYPE_MEM_64 = 1 << 4,
};

typedef struct XenHostPCIIORegion {
    pcibus_t base_addr;
    pcibus_t size;
    uint8_t type;
    uint8_t bus_flags; /* Bus-specific bits */
} XenHostPCIIORegion;

typedef struct XenHostPCIDevice {
    uint16_t domain;
    uint8_t bus;
    uint8_t dev;
    uint8_t func;

    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t class_code;
    int irq;

    XenHostPCIIORegion io_regions[PCI_NUM_REGIONS - 1];
    XenHostPCIIORegion rom;

    bool is_virtfn;

    int config_fd;
} XenHostPCIDevice;

void xen_host_pci_device_get(XenHostPCIDevice *d, uint16_t domain,
                             uint8_t bus, uint8_t dev, uint8_t func,
                             Error **errp);
void xen_host_pci_device_put(XenHostPCIDevice *pci_dev);
bool xen_host_pci_device_closed(XenHostPCIDevice *d);

int xen_host_pci_get_byte(XenHostPCIDevice *d, int pos, uint8_t *p);
int xen_host_pci_get_word(XenHostPCIDevice *d, int pos, uint16_t *p);
int xen_host_pci_get_long(XenHostPCIDevice *d, int pos, uint32_t *p);
int xen_host_pci_get_block(XenHostPCIDevice *d, int pos, uint8_t *buf,
                           int len);
int xen_host_pci_set_byte(XenHostPCIDevice *d, int pos, uint8_t data);
int xen_host_pci_set_word(XenHostPCIDevice *d, int pos, uint16_t data);
int xen_host_pci_set_long(XenHostPCIDevice *d, int pos, uint32_t data);
int xen_host_pci_set_block(XenHostPCIDevice *d, int pos, uint8_t *buf,
                           int len);

int xen_host_pci_find_ext_cap_offset(XenHostPCIDevice *s, uint32_t cap);

#endif /* !XEN_HOST_PCI_DEVICE_H_ */

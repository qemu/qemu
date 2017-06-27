#ifndef SHPC_H
#define SHPC_H

#include "qemu-common.h"
#include "exec/memory.h"
#include "hw/hotplug.h"
#include "hw/pci/pci.h"

struct SHPCDevice {
    /* Capability offset in device's config space */
    int cap;

    /* # of hot-pluggable slots */
    int nslots;

    /* SHPC WRS: working register set */
    uint8_t *config;

    /* Used to enable checks on load. Note that writable bits are
     * never checked even if set in cmask. */
    uint8_t *cmask;

    /* Used to implement R/W bytes */
    uint8_t *wmask;

    /* Used to implement RW1C(Write 1 to Clear) bytes */
    uint8_t *w1cmask;

    /* MMIO for the SHPC BAR */
    MemoryRegion mmio;

    /* Bus controlled by this SHPC */
    PCIBus *sec_bus;

    /* MSI already requested for this event */
    int msi_requested;
};

void shpc_reset(PCIDevice *d);
int shpc_bar_size(PCIDevice *dev);
int shpc_init(PCIDevice *dev, PCIBus *sec_bus, MemoryRegion *bar,
              unsigned off, Error **errp);
void shpc_cleanup(PCIDevice *dev, MemoryRegion *bar);
void shpc_free(PCIDevice *dev);
void shpc_cap_write_config(PCIDevice *d, uint32_t addr, uint32_t val, int len);


void shpc_device_hotplug_cb(HotplugHandler *hotplug_dev, DeviceState *dev,
                            Error **errp);
void shpc_device_hot_unplug_request_cb(HotplugHandler *hotplug_dev,
                                       DeviceState *dev, Error **errp);

extern VMStateInfo shpc_vmstate_info;
#define SHPC_VMSTATE(_field, _type,  _test) \
    VMSTATE_BUFFER_UNSAFE_INFO_TEST(_field, _type, _test, 0, \
                                    shpc_vmstate_info, 0)

static inline bool shpc_present(const PCIDevice *dev)
{
    return dev->cap_present & QEMU_PCI_CAP_SHPC;
}

#endif

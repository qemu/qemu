/* HPPA cores and system support chips.  */

#ifndef HW_HPPA_SYS_H
#define HW_HPPA_SYS_H

#include "hw/pci/pci.h"
#include "hw/pci/pci_host.h"
#include "hw/boards.h"
#include "hw/intc/i8259.h"

#include "hppa_hardware.h"

PCIBus *dino_init(MemoryRegion *, qemu_irq *, qemu_irq *);
DeviceState *lasi_init(MemoryRegion *);
#define enable_lasi_lan()       0

#define TYPE_DINO_PCI_HOST_BRIDGE "dino-pcihost"

/* hppa_pci.c.  */
extern const MemoryRegionOps hppa_pci_ignore_ops;
extern const MemoryRegionOps hppa_pci_conf1_ops;
extern const MemoryRegionOps hppa_pci_iack_ops;

#endif

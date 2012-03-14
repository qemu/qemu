/* Alpha cores and system support chips.  */

#ifndef HW_ALPHA_H
#define HW_ALPHA_H 1

#include "pci.h"
#include "pci_host.h"
#include "ide.h"
#include "net.h"
#include "pc.h"
#include "irq.h"


PCIBus *typhoon_init(ram_addr_t, ISABus **, qemu_irq *, CPUAlphaState *[4],
                     pci_map_irq_fn);

/* alpha_pci.c.  */
extern const MemoryRegionOps alpha_pci_bw_io_ops;
extern const MemoryRegionOps alpha_pci_conf1_ops;
extern const MemoryRegionOps alpha_pci_iack_ops;

void alpha_pci_vga_setup(PCIBus *pci_bus);

#endif

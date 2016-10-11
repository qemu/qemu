/* Alpha cores and system support chips.  */

#ifndef HW_ALPHA_SYS_H
#define HW_ALPHA_SYS_H

#include "target/alpha/cpu-qom.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_host.h"
#include "hw/ide.h"
#include "hw/i386/pc.h"
#include "hw/irq.h"


PCIBus *typhoon_init(ram_addr_t, ISABus **, qemu_irq *, AlphaCPU *[4],
                     pci_map_irq_fn);

/* alpha_pci.c.  */
extern const MemoryRegionOps alpha_pci_ignore_ops;
extern const MemoryRegionOps alpha_pci_conf1_ops;
extern const MemoryRegionOps alpha_pci_iack_ops;

#endif

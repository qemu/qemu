/* Alpha cores and system support chips.  */

#ifndef HW_ALPHA_SYS_H
#define HW_ALPHA_SYS_H

#include "target/alpha/cpu-qom.h"
#include "hw/pci/pci.h"
#include "hw/boards.h"
#include "hw/intc/i8259.h"


PCIBus *typhoon_init(MemoryRegion *, qemu_irq *, qemu_irq *, AlphaCPU *[4],
                     pci_map_irq_fn, uint8_t devfn_min);

/* alpha_pci.c.  */
extern const MemoryRegionOps alpha_pci_ignore_ops;
extern const MemoryRegionOps alpha_pci_conf1_ops;
extern const MemoryRegionOps alpha_pci_iack_ops;

#endif

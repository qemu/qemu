#ifndef HW_IDE_H
#define HW_IDE_H

#include "isa.h"
#include "pci.h"
#include "memory.h"

#define MAX_IDE_DEVS	2

/* ide-isa.c */
ISADevice *isa_ide_init(ISABus *bus, int iobase, int iobase2, int isairq,
                        DriveInfo *hd0, DriveInfo *hd1);

/* ide-pci.c */
void pci_cmd646_ide_init(PCIBus *bus, DriveInfo **hd_table,
                         int secondary_ide_enabled);
PCIDevice *pci_piix3_xen_ide_init(PCIBus *bus, DriveInfo **hd_table, int devfn);
PCIDevice *pci_piix3_ide_init(PCIBus *bus, DriveInfo **hd_table, int devfn);
PCIDevice *pci_piix4_ide_init(PCIBus *bus, DriveInfo **hd_table, int devfn);
void vt82c686b_ide_init(PCIBus *bus, DriveInfo **hd_table, int devfn);

/* ide-macio.c */
MemoryRegion *pmac_ide_init (DriveInfo **hd_table, qemu_irq irq,
		   void *dbdma, int channel, qemu_irq dma_irq);

/* ide-mmio.c */
void mmio_ide_init (target_phys_addr_t membase, target_phys_addr_t membase2,
                    MemoryRegion *address_space,
                    qemu_irq irq, int shift,
                    DriveInfo *hd0, DriveInfo *hd1);

void ide_get_bs(BlockDriverState *bs[], BusState *qbus);

/* ide/core.c */
void ide_drive_get(DriveInfo **hd, int max_bus);

#endif /* HW_IDE_H */

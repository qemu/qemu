#ifndef HW_IDE_H
#define HW_IDE_H

#include "qdev.h"

/* ide-isa.c */
void isa_ide_init(int iobase, int iobase2, qemu_irq irq,
                  BlockDriverState *hd0, BlockDriverState *hd1);

/* ide-pci.c */
void pci_cmd646_ide_init(PCIBus *bus, BlockDriverState **hd_table,
                         int secondary_ide_enabled);
void pci_piix3_ide_init(PCIBus *bus, BlockDriverState **hd_table, int devfn,
                        qemu_irq *pic);
void pci_piix4_ide_init(PCIBus *bus, BlockDriverState **hd_table, int devfn,
                        qemu_irq *pic);

#endif /* HW_IDE_H */

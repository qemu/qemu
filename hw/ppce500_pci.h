#ifndef PPCE500_PCI_H
#define PPCE500_PCI_H

static inline int ppce500_pci_map_irq_slot(int devno, int irq_num)
{
    return (devno + irq_num) % 4;
}

#endif

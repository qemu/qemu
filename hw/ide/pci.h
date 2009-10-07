#ifndef HW_IDE_PCI_H
#define HW_IDE_PCI_H

#include <hw/ide/internal.h>

typedef struct PCIIDEState {
    PCIDevice dev;
    IDEBus bus[2];
    BMDMAState bmdma[2];
    uint32_t secondary; /* used only for cmd646 */
} PCIIDEState;

void bmdma_cmd_writeb(void *opaque, uint32_t addr, uint32_t val);
uint32_t bmdma_addr_readb(void *opaque, uint32_t addr);
void bmdma_addr_writeb(void *opaque, uint32_t addr, uint32_t val);
uint32_t bmdma_addr_readw(void *opaque, uint32_t addr);
void bmdma_addr_writew(void *opaque, uint32_t addr, uint32_t val);
uint32_t bmdma_addr_readl(void *opaque, uint32_t addr);
void bmdma_addr_writel(void *opaque, uint32_t addr, uint32_t val);
void pci_ide_create_devs(PCIDevice *dev, DriveInfo **hd_table);

extern const VMStateDescription vmstate_ide_pci;
#endif

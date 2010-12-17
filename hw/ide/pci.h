#ifndef HW_IDE_PCI_H
#define HW_IDE_PCI_H

#include <hw/ide/internal.h>

typedef struct BMDMAState {
    IDEDMA dma;
    uint8_t cmd;
    uint8_t status;
    uint32_t addr;

    IDEBus *bus;
    /* current transfer state */
    uint32_t cur_addr;
    uint32_t cur_prd_last;
    uint32_t cur_prd_addr;
    uint32_t cur_prd_len;
    uint8_t unit;
    BlockDriverCompletionFunc *dma_cb;
    int64_t sector_num;
    uint32_t nsector;
    IORange addr_ioport;
    QEMUBH *bh;
    qemu_irq irq;
} BMDMAState;

typedef struct PCIIDEState {
    PCIDevice dev;
    IDEBus bus[2];
    BMDMAState bmdma[2];
    uint32_t secondary; /* used only for cmd646 */
} PCIIDEState;


static inline IDEState *bmdma_active_if(BMDMAState *bmdma)
{
    assert(bmdma->unit != (uint8_t)-1);
    return bmdma->bus->ifs + bmdma->unit;
}


void bmdma_init(IDEBus *bus, BMDMAState *bm);
void bmdma_cmd_writeb(void *opaque, uint32_t addr, uint32_t val);
extern const IORangeOps bmdma_addr_ioport_ops;
void pci_ide_create_devs(PCIDevice *dev, DriveInfo **hd_table);

extern const VMStateDescription vmstate_ide_pci;
#endif

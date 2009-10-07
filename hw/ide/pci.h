#ifndef HW_IDE_PCI_H
#define HW_IDE_PCI_H

#include <hw/ide/internal.h>

#define IDE_TYPE_PIIX3   0
#define IDE_TYPE_CMD646  1
#define IDE_TYPE_PIIX4   2

typedef struct PCIIDEState {
    PCIDevice dev;
    IDEBus bus[2];
    BMDMAState bmdma[2];
    int type; /* see IDE_TYPE_xxx */
    uint32_t secondary;
} PCIIDEState;

#endif

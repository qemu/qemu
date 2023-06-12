#ifndef HW_PARALLEL_H
#define HW_PARALLEL_H

#include "exec/ioport.h"
#include "exec/memory.h"
#include "hw/isa/isa.h"
#include "hw/irq.h"
#include "chardev/char-fe.h"
#include "chardev/char.h"

typedef struct ParallelState {
    MemoryRegion iomem;
    uint8_t dataw;
    uint8_t datar;
    uint8_t status;
    uint8_t control;
    qemu_irq irq;
    int irq_pending;
    CharBackend chr;
    int hw_driver;
    int epp_timeout;
    uint32_t last_read_offset; /* For debugging */
    /* Memory-mapped interface */
    int it_shift;
    PortioList portio_list;
} ParallelState;

void parallel_hds_isa_init(ISABus *bus, int n);

bool parallel_mm_init(MemoryRegion *address_space,
                      hwaddr base, int it_shift, qemu_irq irq,
                      Chardev *chr);

#endif

#ifndef HW_PARALLEL_H
#define HW_PARALLEL_H

#include "exec/memory.h"
#include "hw/isa/isa.h"
#include "chardev/char.h"

void parallel_hds_isa_init(ISABus *bus, int n);

bool parallel_mm_init(MemoryRegion *address_space,
                      hwaddr base, int it_shift, qemu_irq irq,
                      Chardev *chr);

#endif

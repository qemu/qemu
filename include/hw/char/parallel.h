#ifndef HW_PARALLEL_H
#define HW_PARALLEL_H

#include "hw/isa/isa.h"
#include "chardev/char.h"

#define TYPE_ISA_PARALLEL "isa-parallel"

void parallel_hds_isa_init(ISABus *bus, int n);

bool parallel_mm_init(MemoryRegion *address_space,
                      hwaddr base, int it_shift, qemu_irq irq,
                      Chardev *chr);

#endif

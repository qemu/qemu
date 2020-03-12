#ifndef HW_VMPORT_H
#define HW_VMPORT_H

#include "hw/isa/isa.h"

#define TYPE_VMPORT "vmport"
typedef uint32_t (VMPortReadFunc)(void *opaque, uint32_t address);

static inline void vmport_init(ISABus *bus)
{
    isa_create_simple(bus, TYPE_VMPORT);
}

void vmport_register(unsigned char command, VMPortReadFunc *func, void *opaque);

#endif

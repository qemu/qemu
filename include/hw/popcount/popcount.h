#ifndef HW_POPCOUNT_H
#define HW_POPCOUNT_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_POPCOUNT "POPCOUNT"

typedef struct popState popState;

DECLARE_INSTANCE_CHECKER(popState, POPCOUNT, TYPE_POPCOUNT)

struct popState
{
    MemoryRegion reset;
    MemoryRegion mmio;
    uint32_t write_reg;
    uint32_t bitcount;
};

popState *popcount_create(MemoryRegion *address_space, hwaddr base);

#endif //HW_POPCOUNT_H

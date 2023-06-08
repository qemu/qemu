/* AUTHOR:       Matteo Vidali
 * AUTHOR EMAIL: mvidali@iu.edu - mmvidali@gmail.com
 *
 * DESC:
 * The header file for the corresponding hardware description file present
 * in qemu315/hw/popcount/popcount.c
*/

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
    uint32_t write_reg; // This is uncecessary, its unaccessable in userspace
    uint32_t bitcount;
};

popState *popcount_create(MemoryRegion *address_space, hwaddr base);

#endif //HW_POPCOUNT_H

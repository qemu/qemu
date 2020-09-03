#ifndef E500_CCSR_H
#define E500_CCSR_H

#include "hw/sysbus.h"
#include "qom/object.h"

struct PPCE500CCSRState {
    /*< private >*/
    SysBusDevice parent;
    /*< public >*/

    MemoryRegion ccsr_space;
};
typedef struct PPCE500CCSRState PPCE500CCSRState;

#define TYPE_CCSR "e500-ccsr"
#define CCSR(obj) OBJECT_CHECK(PPCE500CCSRState, (obj), TYPE_CCSR)

#endif /* E500_CCSR_H */

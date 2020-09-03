#ifndef CHIPIDEA_H
#define CHIPIDEA_H

#include "hw/usb/hcd-ehci.h"
#include "qom/object.h"

struct ChipideaState {
    /*< private >*/
    EHCISysBusState parent_obj;

    MemoryRegion iomem[3];
};
typedef struct ChipideaState ChipideaState;

#define TYPE_CHIPIDEA "usb-chipidea"
#define CHIPIDEA(obj) OBJECT_CHECK(ChipideaState, (obj), TYPE_CHIPIDEA)

#endif /* CHIPIDEA_H */

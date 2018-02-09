#ifndef IMX_GPCV2_H
#define IMX_GPCV2_H

#include "hw/sysbus.h"

enum IMXGPCv2Registers {
    GPC_NUM        = 0xE00 / sizeof(uint32_t),
};

typedef struct IMXGPCv2State {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    uint32_t     regs[GPC_NUM];
} IMXGPCv2State;

#define TYPE_IMX_GPCV2 "imx-gpcv2"
#define IMX_GPCV2(obj) OBJECT_CHECK(IMXGPCv2State, (obj), TYPE_IMX_GPCV2)

#endif /* IMX_GPCV2_H */

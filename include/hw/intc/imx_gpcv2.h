#ifndef IMX_GPCV2_H
#define IMX_GPCV2_H

#include "hw/sysbus.h"
#include "qom/object.h"

enum IMXGPCv2Registers {
    GPC_NUM        = 0xE00 / sizeof(uint32_t),
};

struct IMXGPCv2State {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    uint32_t     regs[GPC_NUM];
};
typedef struct IMXGPCv2State IMXGPCv2State;

#define TYPE_IMX_GPCV2 "imx-gpcv2"
DECLARE_INSTANCE_CHECKER(IMXGPCv2State, IMX_GPCV2,
                         TYPE_IMX_GPCV2)

#endif /* IMX_GPCV2_H */

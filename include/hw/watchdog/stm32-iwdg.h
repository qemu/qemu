#ifndef STM32_IWDG_H
#define STM32_IWDG_H

#include "hw/sysbus.h"
#include "qom/object.h"

/* For now this is based on the stm32l4r5. For others this might need some adaption/needs to be more generic.*/

#define TYPE_STM32_IWDG "stm32-iwdt"
OBJECT_DECLARE_TYPE(STM32IWDGState, STM32IWDGClass, STM32_IWDG)

#define STM32_IWDG_REGS_NUM        5

struct STM32IWDGState {
    SysBusDevice parent_obj;
    QEMUTimer *timer;

    MemoryRegion iomem;
    uint32_t regs[STM32_IWDG_REGS_NUM];
    bool register_locked;
};


struct STM32IWDGClass {
    SysBusDeviceClass parent_class;
};

#endif /* STM32_IWDG_H */

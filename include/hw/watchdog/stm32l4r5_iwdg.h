#ifndef STM32_IWDG_H
#define STM32_IWDG_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_STM32L4R5_IWDG "stm32l4r5_iwdg"
OBJECT_DECLARE_TYPE(Stm32l4r5IwdgState, Stm32l4r5IwdgClass, STM32L4R5_IWDG)

#define STM32_IWDG_REGS_NUM        5

struct Stm32l4r5IwdgState {
    SysBusDevice parent_obj;
    QEMUTimer *timer;

    MemoryRegion iomem;
    uint32_t regs[STM32_IWDG_REGS_NUM];
    bool register_locked;
};


struct Stm32l4r5IwdgClass {
    SysBusDeviceClass parent_class;
};

#endif /* STM32_IWDG_H */

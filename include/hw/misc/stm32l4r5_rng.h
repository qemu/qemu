#ifndef STM32L4R5_RNG_H
#define STM32L4R5_RNG_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_STM32L4R5_RNG "stm32l4r5_rng"
OBJECT_DECLARE_TYPE(STM32L4R5RNGState, STM32L4R5RNGClass, STM32L4R5_RNG)

#define STM32L4R5_RNG_REGS_NUM        3

#define STM32L4R5_RNG_REGS_SIZE       (4 * 3)

struct STM32L4R5RNGState {
    SysBusDevice parent_obj;

    QEMUTimer *timer;
    uint8_t data_read_cnt = 0;

    MemoryRegion iomem;
    uint32_t regs[STM32L4R5_RNG_REGS_NUM];

    qemu_irq irq;
};


struct STM32L4R5RNGClass {
    SysBusDeviceClass parent_class;
};

#endif /* STM32L4R5_RNG_H */

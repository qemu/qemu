#ifndef STM32L4R5_RNG_H
#define STM32L4R5_RNG_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_STM32L4R5_RNG "stm32l4r5_rng"
OBJECT_DECLARE_TYPE(Stm32l4r5RngState, Stm32l4r5RngClass, STM32L4R5_RNG)

#define STM32L4R5_RNG_REGS_NUM        3

#define STM32L4R5_RNG_REGS_SIZE       (4 * 3)

struct Stm32l4r5RngState {
    SysBusDevice parent_obj;

    QEMUTimer *timer;
    uint8_t data_read_cnt;

    MemoryRegion iomem;
    uint32_t regs[STM32L4R5_RNG_REGS_NUM];

    qemu_irq irq;
};


struct Stm32l4r5RngClass {
    SysBusDeviceClass parent_class;
};

#endif /* STM32L4R5_RNG_H */

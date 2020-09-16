#ifndef ALLWINNER_A10_PIC_H
#define ALLWINNER_A10_PIC_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_AW_A10_PIC  "allwinner-a10-pic"
OBJECT_DECLARE_SIMPLE_TYPE(AwA10PICState, AW_A10_PIC)

#define AW_A10_PIC_VECTOR       0
#define AW_A10_PIC_BASE_ADDR    4
#define AW_A10_PIC_PROTECT      8
#define AW_A10_PIC_NMI          0xc
#define AW_A10_PIC_IRQ_PENDING  0x10
#define AW_A10_PIC_FIQ_PENDING  0x20
#define AW_A10_PIC_SELECT       0x30
#define AW_A10_PIC_ENABLE       0x40
#define AW_A10_PIC_MASK         0x50

#define AW_A10_PIC_INT_NR       95
#define AW_A10_PIC_REG_NUM      DIV_ROUND_UP(AW_A10_PIC_INT_NR, 32)

struct AwA10PICState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/
    MemoryRegion iomem;
    qemu_irq parent_fiq;
    qemu_irq parent_irq;

    uint32_t vector;
    uint32_t base_addr;
    uint32_t protect;
    uint32_t nmi;
    uint32_t irq_pending[AW_A10_PIC_REG_NUM];
    uint32_t fiq_pending[AW_A10_PIC_REG_NUM];
    uint32_t select[AW_A10_PIC_REG_NUM];
    uint32_t enable[AW_A10_PIC_REG_NUM];
    uint32_t mask[AW_A10_PIC_REG_NUM];
    /*priority setting here*/
};

#endif

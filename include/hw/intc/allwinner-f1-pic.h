#ifndef ALLWINNER_F1_PIC_H
#define ALLWINNER_F1_PIC_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_AW_F1_PIC  "allwinner-f1-intc"
OBJECT_DECLARE_SIMPLE_TYPE(AwF1PICState, AW_F1_PIC)

#define AW_F1_PIC_VECTOR       0x00
#define AW_F1_PIC_BASE_ADDR    0x04
#define AW_F1_PIC_INT_CTRL     0x0c
#define AW_F1_PIC_PEND         0x10
#define AW_F1_PIC_EN           0x20
#define AW_F1_PIC_MASK         0x30
#define AW_F1_PIC_RESP         0x40
#define AW_F1_PIC_FF           0x50
#define AW_F1_PIC_PRIO         0x60

#define AW_F1_PIC_INT_NR       40
#define AW_F1_PIC_REG_NUM      DIV_ROUND_UP(AW_F1_PIC_INT_NR, 32)
#define AW_F1_PIC_PRI_REG_NUM  (AW_F1_PIC_REG_NUM*2)

struct AwF1PICState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/
    MemoryRegion iomem;
    qemu_irq     parent_fiq;
    qemu_irq     parent_irq;
    uint32_t     reset_addr;
    
    uint32_t vector;
    uint32_t base_addr;
    uint32_t nmi_int_ctrl;
    uint32_t pending[AW_F1_PIC_REG_NUM];
    uint32_t enable[AW_F1_PIC_REG_NUM];
    uint32_t mask[AW_F1_PIC_REG_NUM];
    // If the corresponding bit is set, 
    // the interrupt with the lower or the same priority level is masked.
    uint32_t response[AW_F1_PIC_REG_NUM];
    // Setting the corresponding bit forcing the corresponding interrupt.
    // Valid only when the corresponding interrupt enable bit is set.
    uint32_t fast_forcing[AW_F1_PIC_REG_NUM];
    /* priority setting here */
    uint32_t priority[AW_F1_PIC_PRI_REG_NUM];
};

#endif

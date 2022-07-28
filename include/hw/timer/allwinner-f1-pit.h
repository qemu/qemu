#ifndef ALLWINNER_F1_PIT_H
#define ALLWINNER_F1_PIT_H

#include "hw/ptimer.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_AW_F1_PIT "allwinner-f1-timer"
OBJECT_DECLARE_SIMPLE_TYPE(AwF1PITState, AW_F1_PIT)

#define AW_F1_TIMER_NR  3
#define AW_F1_PIT_TMR_IRQ       0x0001

#define AW_F1_PIT_TMR_IRQ_EN        0x00
#define AW_F1_PIT_TMR_IRQ_STA       0x04

#define AW_F1_PIT_TMR_BASE          0x10
#define AW_F1_PIT_TMR_BASE_END  \
    (AW_F1_PIT_TMR_BASE + 0x10 * AW_F1_TIMER_NR)


#define AW_F1_PIT_CTRL            0x00
#define AW_F1_PIT_CLK_SC24M     0x0004

#define AW_F1_PIT_TMR_EN        0x0001
#define AW_F1_PIT_TMR_RELOAD    0x0002
#define AW_F1_PIT_TMR_MODE      0x0080

#define AW_F1_PIT_INTV_VALUE      0x04
#define AW_F1_PIT_CUR_VALUE       0x08

#define AW_F1_PIT_AVS_CNT_CTL       0x80
#define AW_F1_PIT_AVS_CNT0          0x84
#define AW_F1_PIT_AVS_CNT1          0x88
#define AW_F1_PIT_AVS_CNT_DIV       0x8c

#define AW_F1_PIT_WDOG_IRQ_EN       0xa0
#define AW_F1_PIT_WDOG_IRQ      0x0001
#define AW_F1_PIT_WDOG_IRQ_STA      0xa4
#define AW_F1_PIT_WDOG_CTRL         0xb0
#define AW_F1_PIT_WDOG_RSTART   0x0001
#define AW_F1_PIT_WDOG_KEY_FIELD 0xa57
#define AW_F1_PIT_WDOG_CFG          0xb4
#define AW_F1_PIT_WDOG_CFG_SYS  0x0001
#define AW_F1_PIT_WDOG_CFG_IRQ  0x0002
#define AW_F1_PIT_WDOG_MODE         0xb8
#define AW_F1_PIT_WDOG_EN       0x0001


typedef struct AwF1TimerContext {
    AwF1PITState *container;
    int index;
} AwF1TimerContext;

struct AwF1PITState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/
    qemu_irq          irq[AW_F1_TIMER_NR];
    ptimer_state    * timer[AW_F1_TIMER_NR];
    AwF1TimerContext  timer_context[AW_F1_TIMER_NR];
    MemoryRegion      iomem;
    uint32_t clk_freq[4];

    uint32_t irq_enable;
    uint32_t irq_status;
    uint32_t control[AW_F1_TIMER_NR];
    uint32_t interval[AW_F1_TIMER_NR];
    uint32_t count[AW_F1_TIMER_NR];
    uint32_t watch_dog_mode;
    uint32_t watch_dog_control;
};

#endif

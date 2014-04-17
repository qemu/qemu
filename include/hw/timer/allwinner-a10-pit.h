#ifndef AW_A10_PIT_H
#define AW_A10_PIT_H

#include "hw/ptimer.h"

#define TYPE_AW_A10_PIT "allwinner-A10-timer"
#define AW_A10_PIT(obj) OBJECT_CHECK(AwA10PITState, (obj), TYPE_AW_A10_PIT)

#define AW_A10_PIT_TIMER_NR    6
#define AW_A10_PIT_TIMER_IRQ   0x1
#define AW_A10_PIT_WDOG_IRQ    0x100

#define AW_A10_PIT_TIMER_IRQ_EN    0
#define AW_A10_PIT_TIMER_IRQ_ST    0x4

#define AW_A10_PIT_TIMER_CONTROL   0x0
#define AW_A10_PIT_TIMER_EN        0x1
#define AW_A10_PIT_TIMER_RELOAD    0x2
#define AW_A10_PIT_TIMER_MODE      0x80

#define AW_A10_PIT_TIMER_INTERVAL  0x4
#define AW_A10_PIT_TIMER_COUNT     0x8
#define AW_A10_PIT_WDOG_CONTROL    0x90
#define AW_A10_PIT_WDOG_MODE       0x94

#define AW_A10_PIT_COUNT_CTL       0xa0
#define AW_A10_PIT_COUNT_RL_EN     0x2
#define AW_A10_PIT_COUNT_CLR_EN    0x1
#define AW_A10_PIT_COUNT_LO        0xa4
#define AW_A10_PIT_COUNT_HI        0xa8

#define AW_A10_PIT_TIMER_BASE      0x10
#define AW_A10_PIT_TIMER_BASE_END  \
    (AW_A10_PIT_TIMER_BASE * 6 + AW_A10_PIT_TIMER_COUNT)

#define AW_A10_PIT_DEFAULT_CLOCK   0x4

typedef struct AwA10PITState AwA10PITState;

typedef struct AwA10TimerContext {
    AwA10PITState *container;
    int index;
} AwA10TimerContext;

struct AwA10PITState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/
    qemu_irq irq[AW_A10_PIT_TIMER_NR];
    ptimer_state * timer[AW_A10_PIT_TIMER_NR];
    AwA10TimerContext timer_context[AW_A10_PIT_TIMER_NR];
    MemoryRegion iomem;
    uint32_t clk_freq[4];

    uint32_t irq_enable;
    uint32_t irq_status;
    uint32_t control[AW_A10_PIT_TIMER_NR];
    uint32_t interval[AW_A10_PIT_TIMER_NR];
    uint32_t count[AW_A10_PIT_TIMER_NR];
    uint32_t watch_dog_mode;
    uint32_t watch_dog_control;
    uint32_t count_lo;
    uint32_t count_hi;
    uint32_t count_ctl;
};

#endif

/*
 * QEMU Sparc SLAVIO timer controller emulation
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "hw/irq.h"
#include "hw/ptimer.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "trace.h"
#include "qemu/module.h"

/*
 * Registers of hardware timer in sun4m.
 *
 * This is the timer/counter part of chip STP2001 (Slave I/O), also
 * produced as NCR89C105. See
 * http://www.ibiblio.org/pub/historic-linux/early-ports/Sparc/NCR/NCR89C105.txt
 *
 * The 31-bit counter is incremented every 500ns by bit 9. Bits 8..0
 * are zero. Bit 31 is 1 when count has been reached.
 *
 * Per-CPU timers interrupt local CPU, system timer uses normal
 * interrupt routing.
 *
 */

#define MAX_CPUS 16

typedef struct CPUTimerState {
    qemu_irq irq;
    ptimer_state *timer;
    uint32_t count, counthigh, reached;
    /* processor only */
    uint32_t run;
    uint64_t limit;
} CPUTimerState;

#define TYPE_SLAVIO_TIMER "slavio_timer"
#define SLAVIO_TIMER(obj) \
    OBJECT_CHECK(SLAVIO_TIMERState, (obj), TYPE_SLAVIO_TIMER)

typedef struct SLAVIO_TIMERState {
    SysBusDevice parent_obj;

    uint32_t num_cpus;
    uint32_t cputimer_mode;
    CPUTimerState cputimer[MAX_CPUS + 1];
} SLAVIO_TIMERState;

typedef struct TimerContext {
    MemoryRegion iomem;
    SLAVIO_TIMERState *s;
    unsigned int timer_index; /* 0 for system, 1 ... MAX_CPUS for CPU timers */
} TimerContext;

#define SYS_TIMER_SIZE 0x14
#define CPU_TIMER_SIZE 0x10

#define TIMER_LIMIT         0
#define TIMER_COUNTER       1
#define TIMER_COUNTER_NORST 2
#define TIMER_STATUS        3
#define TIMER_MODE          4

#define TIMER_COUNT_MASK32 0xfffffe00
#define TIMER_LIMIT_MASK32 0x7fffffff
#define TIMER_MAX_COUNT64  0x7ffffffffffffe00ULL
#define TIMER_MAX_COUNT32  0x7ffffe00ULL
#define TIMER_REACHED      0x80000000
#define TIMER_PERIOD       500ULL // 500ns
#define LIMIT_TO_PERIODS(l) (((l) >> 9) - 1)
#define PERIODS_TO_LIMIT(l) (((l) + 1) << 9)

static int slavio_timer_is_user(TimerContext *tc)
{
    SLAVIO_TIMERState *s = tc->s;
    unsigned int timer_index = tc->timer_index;

    return timer_index != 0 && (s->cputimer_mode & (1 << (timer_index - 1)));
}

// Update count, set irq, update expire_time
// Convert from ptimer countdown units
static void slavio_timer_get_out(CPUTimerState *t)
{
    uint64_t count, limit;

    if (t->limit == 0) { /* free-run system or processor counter */
        limit = TIMER_MAX_COUNT32;
    } else {
        limit = t->limit;
    }
    count = limit - PERIODS_TO_LIMIT(ptimer_get_count(t->timer));

    trace_slavio_timer_get_out(t->limit, t->counthigh, t->count);
    t->count = count & TIMER_COUNT_MASK32;
    t->counthigh = count >> 32;
}

// timer callback
static void slavio_timer_irq(void *opaque)
{
    TimerContext *tc = opaque;
    SLAVIO_TIMERState *s = tc->s;
    CPUTimerState *t = &s->cputimer[tc->timer_index];

    slavio_timer_get_out(t);
    trace_slavio_timer_irq(t->counthigh, t->count);
    /* if limit is 0 (free-run), there will be no match */
    if (t->limit != 0) {
        t->reached = TIMER_REACHED;
    }
    /* there is no interrupt if user timer or free-run */
    if (!slavio_timer_is_user(tc) && t->limit != 0) {
        qemu_irq_raise(t->irq);
    }
}

static uint64_t slavio_timer_mem_readl(void *opaque, hwaddr addr,
                                       unsigned size)
{
    TimerContext *tc = opaque;
    SLAVIO_TIMERState *s = tc->s;
    uint32_t saddr, ret;
    unsigned int timer_index = tc->timer_index;
    CPUTimerState *t = &s->cputimer[timer_index];

    saddr = addr >> 2;
    switch (saddr) {
    case TIMER_LIMIT:
        // read limit (system counter mode) or read most signifying
        // part of counter (user mode)
        if (slavio_timer_is_user(tc)) {
            // read user timer MSW
            slavio_timer_get_out(t);
            ret = t->counthigh | t->reached;
        } else {
            // read limit
            // clear irq
            qemu_irq_lower(t->irq);
            t->reached = 0;
            ret = t->limit & TIMER_LIMIT_MASK32;
        }
        break;
    case TIMER_COUNTER:
        // read counter and reached bit (system mode) or read lsbits
        // of counter (user mode)
        slavio_timer_get_out(t);
        if (slavio_timer_is_user(tc)) { // read user timer LSW
            ret = t->count & TIMER_MAX_COUNT64;
        } else { // read limit
            ret = (t->count & TIMER_MAX_COUNT32) |
                t->reached;
        }
        break;
    case TIMER_STATUS:
        // only available in processor counter/timer
        // read start/stop status
        if (timer_index > 0) {
            ret = t->run;
        } else {
            ret = 0;
        }
        break;
    case TIMER_MODE:
        // only available in system counter
        // read user/system mode
        ret = s->cputimer_mode;
        break;
    default:
        trace_slavio_timer_mem_readl_invalid(addr);
        ret = 0;
        break;
    }
    trace_slavio_timer_mem_readl(addr, ret);
    return ret;
}

static void slavio_timer_mem_writel(void *opaque, hwaddr addr,
                                    uint64_t val, unsigned size)
{
    TimerContext *tc = opaque;
    SLAVIO_TIMERState *s = tc->s;
    uint32_t saddr;
    unsigned int timer_index = tc->timer_index;
    CPUTimerState *t = &s->cputimer[timer_index];

    trace_slavio_timer_mem_writel(addr, val);
    saddr = addr >> 2;
    switch (saddr) {
    case TIMER_LIMIT:
        ptimer_transaction_begin(t->timer);
        if (slavio_timer_is_user(tc)) {
            uint64_t count;

            // set user counter MSW, reset counter
            t->limit = TIMER_MAX_COUNT64;
            t->counthigh = val & (TIMER_MAX_COUNT64 >> 32);
            t->reached = 0;
            count = ((uint64_t)t->counthigh << 32) | t->count;
            trace_slavio_timer_mem_writel_limit(timer_index, count);
            ptimer_set_count(t->timer, LIMIT_TO_PERIODS(t->limit - count));
        } else {
            // set limit, reset counter
            qemu_irq_lower(t->irq);
            t->limit = val & TIMER_MAX_COUNT32;
            if (t->limit == 0) { /* free-run */
                ptimer_set_limit(t->timer,
                                 LIMIT_TO_PERIODS(TIMER_MAX_COUNT32), 1);
            } else {
                ptimer_set_limit(t->timer, LIMIT_TO_PERIODS(t->limit), 1);
            }
        }
        ptimer_transaction_commit(t->timer);
        break;
    case TIMER_COUNTER:
        if (slavio_timer_is_user(tc)) {
            uint64_t count;

            // set user counter LSW, reset counter
            t->limit = TIMER_MAX_COUNT64;
            t->count = val & TIMER_MAX_COUNT64;
            t->reached = 0;
            count = ((uint64_t)t->counthigh) << 32 | t->count;
            trace_slavio_timer_mem_writel_limit(timer_index, count);
            ptimer_transaction_begin(t->timer);
            ptimer_set_count(t->timer, LIMIT_TO_PERIODS(t->limit - count));
            ptimer_transaction_commit(t->timer);
        } else {
            trace_slavio_timer_mem_writel_counter_invalid();
        }
        break;
    case TIMER_COUNTER_NORST:
        // set limit without resetting counter
        t->limit = val & TIMER_MAX_COUNT32;
        ptimer_transaction_begin(t->timer);
        if (t->limit == 0) { /* free-run */
            ptimer_set_limit(t->timer, LIMIT_TO_PERIODS(TIMER_MAX_COUNT32), 0);
        } else {
            ptimer_set_limit(t->timer, LIMIT_TO_PERIODS(t->limit), 0);
        }
        ptimer_transaction_commit(t->timer);
        break;
    case TIMER_STATUS:
        ptimer_transaction_begin(t->timer);
        if (slavio_timer_is_user(tc)) {
            // start/stop user counter
            if (val & 1) {
                trace_slavio_timer_mem_writel_status_start(timer_index);
                ptimer_run(t->timer, 0);
            } else {
                trace_slavio_timer_mem_writel_status_stop(timer_index);
                ptimer_stop(t->timer);
            }
        }
        t->run = val & 1;
        ptimer_transaction_commit(t->timer);
        break;
    case TIMER_MODE:
        if (timer_index == 0) {
            unsigned int i;

            for (i = 0; i < s->num_cpus; i++) {
                unsigned int processor = 1 << i;
                CPUTimerState *curr_timer = &s->cputimer[i + 1];

                ptimer_transaction_begin(curr_timer->timer);
                // check for a change in timer mode for this processor
                if ((val & processor) != (s->cputimer_mode & processor)) {
                    if (val & processor) { // counter -> user timer
                        qemu_irq_lower(curr_timer->irq);
                        // counters are always running
                        if (!curr_timer->run) {
                            ptimer_stop(curr_timer->timer);
                        }
                        // user timer limit is always the same
                        curr_timer->limit = TIMER_MAX_COUNT64;
                        ptimer_set_limit(curr_timer->timer,
                                         LIMIT_TO_PERIODS(curr_timer->limit),
                                         1);
                        // set this processors user timer bit in config
                        // register
                        s->cputimer_mode |= processor;
                        trace_slavio_timer_mem_writel_mode_user(timer_index);
                    } else { // user timer -> counter
                        // start the counter
                        ptimer_run(curr_timer->timer, 0);
                        // clear this processors user timer bit in config
                        // register
                        s->cputimer_mode &= ~processor;
                        trace_slavio_timer_mem_writel_mode_counter(timer_index);
                    }
                }
                ptimer_transaction_commit(curr_timer->timer);
            }
        } else {
            trace_slavio_timer_mem_writel_mode_invalid();
        }
        break;
    default:
        trace_slavio_timer_mem_writel_invalid(addr);
        break;
    }
}

static const MemoryRegionOps slavio_timer_mem_ops = {
    .read = slavio_timer_mem_readl,
    .write = slavio_timer_mem_writel,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const VMStateDescription vmstate_timer = {
    .name ="timer",
    .version_id = 3,
    .minimum_version_id = 3,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(limit, CPUTimerState),
        VMSTATE_UINT32(count, CPUTimerState),
        VMSTATE_UINT32(counthigh, CPUTimerState),
        VMSTATE_UINT32(reached, CPUTimerState),
        VMSTATE_UINT32(run    , CPUTimerState),
        VMSTATE_PTIMER(timer, CPUTimerState),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_slavio_timer = {
    .name ="slavio_timer",
    .version_id = 3,
    .minimum_version_id = 3,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(cputimer, SLAVIO_TIMERState, MAX_CPUS + 1, 3,
                             vmstate_timer, CPUTimerState),
        VMSTATE_END_OF_LIST()
    }
};

static void slavio_timer_reset(DeviceState *d)
{
    SLAVIO_TIMERState *s = SLAVIO_TIMER(d);
    unsigned int i;
    CPUTimerState *curr_timer;

    for (i = 0; i <= MAX_CPUS; i++) {
        curr_timer = &s->cputimer[i];
        curr_timer->limit = 0;
        curr_timer->count = 0;
        curr_timer->reached = 0;
        if (i <= s->num_cpus) {
            ptimer_transaction_begin(curr_timer->timer);
            ptimer_set_limit(curr_timer->timer,
                             LIMIT_TO_PERIODS(TIMER_MAX_COUNT32), 1);
            ptimer_run(curr_timer->timer, 0);
            curr_timer->run = 1;
            ptimer_transaction_commit(curr_timer->timer);
        }
    }
    s->cputimer_mode = 0;
}

static void slavio_timer_init(Object *obj)
{
    SLAVIO_TIMERState *s = SLAVIO_TIMER(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);
    unsigned int i;
    TimerContext *tc;

    for (i = 0; i <= MAX_CPUS; i++) {
        uint64_t size;
        char timer_name[20];

        tc = g_malloc0(sizeof(TimerContext));
        tc->s = s;
        tc->timer_index = i;

        s->cputimer[i].timer = ptimer_init(slavio_timer_irq, tc,
                                           PTIMER_POLICY_DEFAULT);
        ptimer_transaction_begin(s->cputimer[i].timer);
        ptimer_set_period(s->cputimer[i].timer, TIMER_PERIOD);
        ptimer_transaction_commit(s->cputimer[i].timer);

        size = i == 0 ? SYS_TIMER_SIZE : CPU_TIMER_SIZE;
        snprintf(timer_name, sizeof(timer_name), "timer-%i", i);
        memory_region_init_io(&tc->iomem, obj, &slavio_timer_mem_ops, tc,
                              timer_name, size);
        sysbus_init_mmio(dev, &tc->iomem);

        sysbus_init_irq(dev, &s->cputimer[i].irq);
    }
}

static Property slavio_timer_properties[] = {
    DEFINE_PROP_UINT32("num_cpus",  SLAVIO_TIMERState, num_cpus,  0),
    DEFINE_PROP_END_OF_LIST(),
};

static void slavio_timer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = slavio_timer_reset;
    dc->vmsd = &vmstate_slavio_timer;
    device_class_set_props(dc, slavio_timer_properties);
}

static const TypeInfo slavio_timer_info = {
    .name          = TYPE_SLAVIO_TIMER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SLAVIO_TIMERState),
    .instance_init = slavio_timer_init,
    .class_init    = slavio_timer_class_init,
};

static void slavio_timer_register_types(void)
{
    type_register_static(&slavio_timer_info);
}

type_init(slavio_timer_register_types)

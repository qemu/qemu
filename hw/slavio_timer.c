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
#include "hw.h"
#include "sun4m.h"
#include "qemu-timer.h"

//#define DEBUG_TIMER

#ifdef DEBUG_TIMER
#define DPRINTF(fmt, ...)                                       \
    do { printf("TIMER: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) do {} while (0)
#endif

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

typedef struct SLAVIO_TIMERState {
    qemu_irq irq;
    ptimer_state *timer;
    uint32_t count, counthigh, reached;
    uint64_t limit;
    // processor only
    uint32_t running;
    struct SLAVIO_TIMERState *master;
    uint32_t slave_index;
    // system only
    uint32_t num_slaves;
    struct SLAVIO_TIMERState *slave[MAX_CPUS];
    uint32_t slave_mode;
} SLAVIO_TIMERState;

#define SYS_TIMER_SIZE 0x14
#define CPU_TIMER_SIZE 0x10

#define SYS_TIMER_OFFSET      0x10000ULL
#define CPU_TIMER_OFFSET(cpu) (0x1000ULL * cpu)

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
#define LIMIT_TO_PERIODS(l) ((l) >> 9)
#define PERIODS_TO_LIMIT(l) ((l) << 9)

static int slavio_timer_is_user(SLAVIO_TIMERState *s)
{
    return s->master && (s->master->slave_mode & (1 << s->slave_index));
}

// Update count, set irq, update expire_time
// Convert from ptimer countdown units
static void slavio_timer_get_out(SLAVIO_TIMERState *s)
{
    uint64_t count, limit;

    if (s->limit == 0) /* free-run processor or system counter */
        limit = TIMER_MAX_COUNT32;
    else
        limit = s->limit;

    if (s->timer)
        count = limit - PERIODS_TO_LIMIT(ptimer_get_count(s->timer));
    else
        count = 0;

    DPRINTF("get_out: limit %" PRIx64 " count %x%08x\n", s->limit,
            s->counthigh, s->count);
    s->count = count & TIMER_COUNT_MASK32;
    s->counthigh = count >> 32;
}

// timer callback
static void slavio_timer_irq(void *opaque)
{
    SLAVIO_TIMERState *s = opaque;

    slavio_timer_get_out(s);
    DPRINTF("callback: count %x%08x\n", s->counthigh, s->count);
    s->reached = TIMER_REACHED;
    if (!slavio_timer_is_user(s))
        qemu_irq_raise(s->irq);
}

static uint32_t slavio_timer_mem_readl(void *opaque, target_phys_addr_t addr)
{
    SLAVIO_TIMERState *s = opaque;
    uint32_t saddr, ret;

    saddr = addr >> 2;
    switch (saddr) {
    case TIMER_LIMIT:
        // read limit (system counter mode) or read most signifying
        // part of counter (user mode)
        if (slavio_timer_is_user(s)) {
            // read user timer MSW
            slavio_timer_get_out(s);
            ret = s->counthigh | s->reached;
        } else {
            // read limit
            // clear irq
            qemu_irq_lower(s->irq);
            s->reached = 0;
            ret = s->limit & TIMER_LIMIT_MASK32;
        }
        break;
    case TIMER_COUNTER:
        // read counter and reached bit (system mode) or read lsbits
        // of counter (user mode)
        slavio_timer_get_out(s);
        if (slavio_timer_is_user(s)) // read user timer LSW
            ret = s->count & TIMER_MAX_COUNT64;
        else // read limit
            ret = (s->count & TIMER_MAX_COUNT32) | s->reached;
        break;
    case TIMER_STATUS:
        // only available in processor counter/timer
        // read start/stop status
        ret = s->running;
        break;
    case TIMER_MODE:
        // only available in system counter
        // read user/system mode
        ret = s->slave_mode;
        break;
    default:
        DPRINTF("invalid read address " TARGET_FMT_plx "\n", addr);
        ret = 0;
        break;
    }
    DPRINTF("read " TARGET_FMT_plx " = %08x\n", addr, ret);

    return ret;
}

static void slavio_timer_mem_writel(void *opaque, target_phys_addr_t addr,
                                    uint32_t val)
{
    SLAVIO_TIMERState *s = opaque;
    uint32_t saddr;

    DPRINTF("write " TARGET_FMT_plx " %08x\n", addr, val);
    saddr = addr >> 2;
    switch (saddr) {
    case TIMER_LIMIT:
        if (slavio_timer_is_user(s)) {
            uint64_t count;

            // set user counter MSW, reset counter
            s->limit = TIMER_MAX_COUNT64;
            s->counthigh = val & (TIMER_MAX_COUNT64 >> 32);
            s->reached = 0;
            count = ((uint64_t)s->counthigh << 32) | s->count;
            DPRINTF("processor %d user timer set to %016llx\n", s->slave_index,
                    count);
            if (s->timer)
                ptimer_set_count(s->timer, LIMIT_TO_PERIODS(s->limit - count));
        } else {
            // set limit, reset counter
            qemu_irq_lower(s->irq);
            s->limit = val & TIMER_MAX_COUNT32;
            if (s->timer) {
                if (s->limit == 0) /* free-run */
                    ptimer_set_limit(s->timer,
                                     LIMIT_TO_PERIODS(TIMER_MAX_COUNT32), 1);
                else
                    ptimer_set_limit(s->timer, LIMIT_TO_PERIODS(s->limit), 1);
            }
        }
        break;
    case TIMER_COUNTER:
        if (slavio_timer_is_user(s)) {
            uint64_t count;

            // set user counter LSW, reset counter
            s->limit = TIMER_MAX_COUNT64;
            s->count = val & TIMER_MAX_COUNT64;
            s->reached = 0;
            count = ((uint64_t)s->counthigh) << 32 | s->count;
            DPRINTF("processor %d user timer set to %016llx\n", s->slave_index,
                    count);
            if (s->timer)
                ptimer_set_count(s->timer, LIMIT_TO_PERIODS(s->limit - count));
        } else
            DPRINTF("not user timer\n");
        break;
    case TIMER_COUNTER_NORST:
        // set limit without resetting counter
        s->limit = val & TIMER_MAX_COUNT32;
        if (s->timer) {
            if (s->limit == 0)	/* free-run */
                ptimer_set_limit(s->timer,
                                 LIMIT_TO_PERIODS(TIMER_MAX_COUNT32), 0);
            else
                ptimer_set_limit(s->timer, LIMIT_TO_PERIODS(s->limit), 0);
        }
        break;
    case TIMER_STATUS:
        if (slavio_timer_is_user(s)) {
            // start/stop user counter
            if ((val & 1) && !s->running) {
                DPRINTF("processor %d user timer started\n", s->slave_index);
                if (s->timer)
                    ptimer_run(s->timer, 0);
                s->running = 1;
            } else if (!(val & 1) && s->running) {
                DPRINTF("processor %d user timer stopped\n", s->slave_index);
                if (s->timer)
                    ptimer_stop(s->timer);
                s->running = 0;
            }
        }
        break;
    case TIMER_MODE:
        if (s->master == NULL) {
            unsigned int i;

            for (i = 0; i < s->num_slaves; i++) {
                unsigned int processor = 1 << i;

                // check for a change in timer mode for this processor
                if ((val & processor) != (s->slave_mode & processor)) {
                    if (val & processor) { // counter -> user timer
                        qemu_irq_lower(s->slave[i]->irq);
                        // counters are always running
                        ptimer_stop(s->slave[i]->timer);
                        s->slave[i]->running = 0;
                        // user timer limit is always the same
                        s->slave[i]->limit = TIMER_MAX_COUNT64;
                        ptimer_set_limit(s->slave[i]->timer,
                                         LIMIT_TO_PERIODS(s->slave[i]->limit),
                                         1);
                        // set this processors user timer bit in config
                        // register
                        s->slave_mode |= processor;
                        DPRINTF("processor %d changed from counter to user "
                                "timer\n", s->slave[i]->slave_index);
                    } else { // user timer -> counter
                        // stop the user timer if it is running
                        if (s->slave[i]->running)
                            ptimer_stop(s->slave[i]->timer);
                        // start the counter
                        ptimer_run(s->slave[i]->timer, 0);
                        s->slave[i]->running = 1;
                        // clear this processors user timer bit in config
                        // register
                        s->slave_mode &= ~processor;
                        DPRINTF("processor %d changed from user timer to "
                                "counter\n", s->slave[i]->slave_index);
                    }
                }
            }
        } else
            DPRINTF("not system timer\n");
        break;
    default:
        DPRINTF("invalid write address " TARGET_FMT_plx "\n", addr);
        break;
    }
}

static CPUReadMemoryFunc *slavio_timer_mem_read[3] = {
    NULL,
    NULL,
    slavio_timer_mem_readl,
};

static CPUWriteMemoryFunc *slavio_timer_mem_write[3] = {
    NULL,
    NULL,
    slavio_timer_mem_writel,
};

static void slavio_timer_save(QEMUFile *f, void *opaque)
{
    SLAVIO_TIMERState *s = opaque;

    qemu_put_be64s(f, &s->limit);
    qemu_put_be32s(f, &s->count);
    qemu_put_be32s(f, &s->counthigh);
    qemu_put_be32s(f, &s->reached);
    qemu_put_be32s(f, &s->running);
    if (s->timer)
        qemu_put_ptimer(f, s->timer);
}

static int slavio_timer_load(QEMUFile *f, void *opaque, int version_id)
{
    SLAVIO_TIMERState *s = opaque;

    if (version_id != 3)
        return -EINVAL;

    qemu_get_be64s(f, &s->limit);
    qemu_get_be32s(f, &s->count);
    qemu_get_be32s(f, &s->counthigh);
    qemu_get_be32s(f, &s->reached);
    qemu_get_be32s(f, &s->running);
    if (s->timer)
        qemu_get_ptimer(f, s->timer);

    return 0;
}

static void slavio_timer_reset(void *opaque)
{
    SLAVIO_TIMERState *s = opaque;

    s->limit = 0;
    s->count = 0;
    s->reached = 0;
    s->slave_mode = 0;
    if (!s->master || s->slave_index < s->master->num_slaves) {
        ptimer_set_limit(s->timer, LIMIT_TO_PERIODS(TIMER_MAX_COUNT32), 1);
        ptimer_run(s->timer, 0);
    }
    s->running = 1;
    qemu_irq_lower(s->irq);
}

static SLAVIO_TIMERState *slavio_timer_init(target_phys_addr_t addr,
                                            qemu_irq irq,
                                            SLAVIO_TIMERState *master,
                                            uint32_t slave_index)
{
    int slavio_timer_io_memory;
    SLAVIO_TIMERState *s;
    QEMUBH *bh;

    s = qemu_mallocz(sizeof(SLAVIO_TIMERState));
    s->irq = irq;
    s->master = master;
    s->slave_index = slave_index;
    if (!master || slave_index < master->num_slaves) {
        bh = qemu_bh_new(slavio_timer_irq, s);
        s->timer = ptimer_init(bh);
        ptimer_set_period(s->timer, TIMER_PERIOD);
    }

    slavio_timer_io_memory = cpu_register_io_memory(0, slavio_timer_mem_read,
                                                    slavio_timer_mem_write, s);
    if (master)
        cpu_register_physical_memory(addr, CPU_TIMER_SIZE,
                                     slavio_timer_io_memory);
    else
        cpu_register_physical_memory(addr, SYS_TIMER_SIZE,
                                     slavio_timer_io_memory);
    register_savevm("slavio_timer", addr, 3, slavio_timer_save,
                    slavio_timer_load, s);
    qemu_register_reset(slavio_timer_reset, s);
    slavio_timer_reset(s);

    return s;
}

void slavio_timer_init_all(target_phys_addr_t base, qemu_irq master_irq,
                           qemu_irq *cpu_irqs, unsigned int num_cpus)
{
    SLAVIO_TIMERState *master;
    unsigned int i;

    master = slavio_timer_init(base + SYS_TIMER_OFFSET, master_irq, NULL, 0);

    master->num_slaves = num_cpus;

    for (i = 0; i < MAX_CPUS; i++) {
        master->slave[i] = slavio_timer_init(base + (target_phys_addr_t)
                                             CPU_TIMER_OFFSET(i),
                                             cpu_irqs[i], master, i);
    }
}

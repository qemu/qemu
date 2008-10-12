/*
 *  APIC support
 *
 *  Copyright (c) 2004-2005 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "hw.h"
#include "pc.h"
#include "qemu-timer.h"

//#define DEBUG_APIC
//#define DEBUG_IOAPIC

/* APIC Local Vector Table */
#define APIC_LVT_TIMER   0
#define APIC_LVT_THERMAL 1
#define APIC_LVT_PERFORM 2
#define APIC_LVT_LINT0   3
#define APIC_LVT_LINT1   4
#define APIC_LVT_ERROR   5
#define APIC_LVT_NB      6

/* APIC delivery modes */
#define APIC_DM_FIXED	0
#define APIC_DM_LOWPRI	1
#define APIC_DM_SMI	2
#define APIC_DM_NMI	4
#define APIC_DM_INIT	5
#define APIC_DM_SIPI	6
#define APIC_DM_EXTINT	7

/* APIC destination mode */
#define APIC_DESTMODE_FLAT	0xf
#define APIC_DESTMODE_CLUSTER	1

#define APIC_TRIGGER_EDGE  0
#define APIC_TRIGGER_LEVEL 1

#define	APIC_LVT_TIMER_PERIODIC		(1<<17)
#define	APIC_LVT_MASKED			(1<<16)
#define	APIC_LVT_LEVEL_TRIGGER		(1<<15)
#define	APIC_LVT_REMOTE_IRR		(1<<14)
#define	APIC_INPUT_POLARITY		(1<<13)
#define	APIC_SEND_PENDING		(1<<12)

#define IOAPIC_NUM_PINS			0x18

#define ESR_ILLEGAL_ADDRESS (1 << 7)

#define APIC_SV_ENABLE (1 << 8)

#define MAX_APICS 255
#define MAX_APIC_WORDS 8

typedef struct APICState {
    CPUState *cpu_env;
    uint32_t apicbase;
    uint8_t id;
    uint8_t arb_id;
    uint8_t tpr;
    uint32_t spurious_vec;
    uint8_t log_dest;
    uint8_t dest_mode;
    uint32_t isr[8];  /* in service register */
    uint32_t tmr[8];  /* trigger mode register */
    uint32_t irr[8]; /* interrupt request register */
    uint32_t lvt[APIC_LVT_NB];
    uint32_t esr; /* error register */
    uint32_t icr[2];

    uint32_t divide_conf;
    int count_shift;
    uint32_t initial_count;
    int64_t initial_count_load_time, next_time;
    QEMUTimer *timer;
} APICState;

struct IOAPICState {
    uint8_t id;
    uint8_t ioregsel;

    uint32_t irr;
    uint64_t ioredtbl[IOAPIC_NUM_PINS];
};

static int apic_io_memory;
static APICState *local_apics[MAX_APICS + 1];
static int last_apic_id = 0;

static void apic_init_ipi(APICState *s);
static void apic_set_irq(APICState *s, int vector_num, int trigger_mode);
static void apic_update_irq(APICState *s);

/* Find first bit starting from msb */
static int fls_bit(uint32_t value)
{
#if defined(__GNUC__)
    return 31 - __builtin_clz(value);
#else
    unsigned int ret = 0;

    if (value > 0xffff)
        value >>= 16, ret = 16;
    if (value > 0xff)
        value >>= 8, ret += 8;
    if (value > 0xf)
        value >>= 4, ret += 4;
    if (value > 0x3)
        value >>= 2, ret += 2;
    return ret + (value >> 1);
#endif
}

/* Find first bit starting from lsb */
static int ffs_bit(uint32_t value)
{
#if defined(__GNUC__)
    return __builtin_ffs(value) - 1;
#else
    unsigned int ret = 0;

    if (!value)
        return 0;
    if (!(value & 0xffff))
        value >>= 16, ret = 16;
    if (!(value & 0xff))
        value >>= 8, ret += 8;
    if (!(value & 0xf))
        value >>= 4, ret += 4;
    if (!(value & 0x3))
        value >>= 2, ret += 2;
    if (!(value & 0x1))
        ret++;
    return ret;
#endif
}

static inline void set_bit(uint32_t *tab, int index)
{
    int i, mask;
    i = index >> 5;
    mask = 1 << (index & 0x1f);
    tab[i] |= mask;
}

static inline void reset_bit(uint32_t *tab, int index)
{
    int i, mask;
    i = index >> 5;
    mask = 1 << (index & 0x1f);
    tab[i] &= ~mask;
}

static void apic_local_deliver(CPUState *env, int vector)
{
    APICState *s = env->apic_state;
    uint32_t lvt = s->lvt[vector];
    int trigger_mode;

    if (lvt & APIC_LVT_MASKED)
        return;

    switch ((lvt >> 8) & 7) {
    case APIC_DM_SMI:
        cpu_interrupt(env, CPU_INTERRUPT_SMI);
        break;

    case APIC_DM_NMI:
        cpu_interrupt(env, CPU_INTERRUPT_NMI);
        break;

    case APIC_DM_EXTINT:
        cpu_interrupt(env, CPU_INTERRUPT_HARD);
        break;

    case APIC_DM_FIXED:
        trigger_mode = APIC_TRIGGER_EDGE;
        if ((vector == APIC_LVT_LINT0 || vector == APIC_LVT_LINT1) &&
            (lvt & APIC_LVT_LEVEL_TRIGGER))
            trigger_mode = APIC_TRIGGER_LEVEL;
        apic_set_irq(s, lvt & 0xff, trigger_mode);
    }
}

void apic_deliver_pic_intr(CPUState *env, int level)
{
    if (level)
        apic_local_deliver(env, APIC_LVT_LINT0);
    else {
        APICState *s = env->apic_state;
        uint32_t lvt = s->lvt[APIC_LVT_LINT0];

        switch ((lvt >> 8) & 7) {
        case APIC_DM_FIXED:
            if (!(lvt & APIC_LVT_LEVEL_TRIGGER))
                break;
            reset_bit(s->irr, lvt & 0xff);
            /* fall through */
        case APIC_DM_EXTINT:
            cpu_reset_interrupt(env, CPU_INTERRUPT_HARD);
            break;
        }
    }
}

#define foreach_apic(apic, deliver_bitmask, code) \
{\
    int __i, __j, __mask;\
    for(__i = 0; __i < MAX_APIC_WORDS; __i++) {\
        __mask = deliver_bitmask[__i];\
        if (__mask) {\
            for(__j = 0; __j < 32; __j++) {\
                if (__mask & (1 << __j)) {\
                    apic = local_apics[__i * 32 + __j];\
                    if (apic) {\
                        code;\
                    }\
                }\
            }\
        }\
    }\
}

static void apic_bus_deliver(const uint32_t *deliver_bitmask,
                             uint8_t delivery_mode,
                             uint8_t vector_num, uint8_t polarity,
                             uint8_t trigger_mode)
{
    APICState *apic_iter;

    switch (delivery_mode) {
        case APIC_DM_LOWPRI:
            /* XXX: search for focus processor, arbitration */
            {
                int i, d;
                d = -1;
                for(i = 0; i < MAX_APIC_WORDS; i++) {
                    if (deliver_bitmask[i]) {
                        d = i * 32 + ffs_bit(deliver_bitmask[i]);
                        break;
                    }
                }
                if (d >= 0) {
                    apic_iter = local_apics[d];
                    if (apic_iter) {
                        apic_set_irq(apic_iter, vector_num, trigger_mode);
                    }
                }
            }
            return;

        case APIC_DM_FIXED:
            break;

        case APIC_DM_SMI:
            foreach_apic(apic_iter, deliver_bitmask,
                cpu_interrupt(apic_iter->cpu_env, CPU_INTERRUPT_SMI) );
            return;

        case APIC_DM_NMI:
            foreach_apic(apic_iter, deliver_bitmask,
                cpu_interrupt(apic_iter->cpu_env, CPU_INTERRUPT_NMI) );
            return;

        case APIC_DM_INIT:
            /* normal INIT IPI sent to processors */
            foreach_apic(apic_iter, deliver_bitmask,
                         apic_init_ipi(apic_iter) );
            return;

        case APIC_DM_EXTINT:
            /* handled in I/O APIC code */
            break;

        default:
            return;
    }

    foreach_apic(apic_iter, deliver_bitmask,
                 apic_set_irq(apic_iter, vector_num, trigger_mode) );
}

void cpu_set_apic_base(CPUState *env, uint64_t val)
{
    APICState *s = env->apic_state;
#ifdef DEBUG_APIC
    printf("cpu_set_apic_base: %016" PRIx64 "\n", val);
#endif
    s->apicbase = (val & 0xfffff000) |
        (s->apicbase & (MSR_IA32_APICBASE_BSP | MSR_IA32_APICBASE_ENABLE));
    /* if disabled, cannot be enabled again */
    if (!(val & MSR_IA32_APICBASE_ENABLE)) {
        s->apicbase &= ~MSR_IA32_APICBASE_ENABLE;
        env->cpuid_features &= ~CPUID_APIC;
        s->spurious_vec &= ~APIC_SV_ENABLE;
    }
}

uint64_t cpu_get_apic_base(CPUState *env)
{
    APICState *s = env->apic_state;
#ifdef DEBUG_APIC
    printf("cpu_get_apic_base: %016" PRIx64 "\n", (uint64_t)s->apicbase);
#endif
    return s->apicbase;
}

void cpu_set_apic_tpr(CPUX86State *env, uint8_t val)
{
    APICState *s = env->apic_state;
    s->tpr = (val & 0x0f) << 4;
    apic_update_irq(s);
}

uint8_t cpu_get_apic_tpr(CPUX86State *env)
{
    APICState *s = env->apic_state;
    return s->tpr >> 4;
}

/* return -1 if no bit is set */
static int get_highest_priority_int(uint32_t *tab)
{
    int i;
    for(i = 7; i >= 0; i--) {
        if (tab[i] != 0) {
            return i * 32 + fls_bit(tab[i]);
        }
    }
    return -1;
}

static int apic_get_ppr(APICState *s)
{
    int tpr, isrv, ppr;

    tpr = (s->tpr >> 4);
    isrv = get_highest_priority_int(s->isr);
    if (isrv < 0)
        isrv = 0;
    isrv >>= 4;
    if (tpr >= isrv)
        ppr = s->tpr;
    else
        ppr = isrv << 4;
    return ppr;
}

static int apic_get_arb_pri(APICState *s)
{
    /* XXX: arbitration */
    return 0;
}

/* signal the CPU if an irq is pending */
static void apic_update_irq(APICState *s)
{
    int irrv, ppr;
    if (!(s->spurious_vec & APIC_SV_ENABLE))
        return;
    irrv = get_highest_priority_int(s->irr);
    if (irrv < 0)
        return;
    ppr = apic_get_ppr(s);
    if (ppr && (irrv & 0xf0) <= (ppr & 0xf0))
        return;
    cpu_interrupt(s->cpu_env, CPU_INTERRUPT_HARD);
}

static void apic_set_irq(APICState *s, int vector_num, int trigger_mode)
{
    set_bit(s->irr, vector_num);
    if (trigger_mode)
        set_bit(s->tmr, vector_num);
    else
        reset_bit(s->tmr, vector_num);
    apic_update_irq(s);
}

static void apic_eoi(APICState *s)
{
    int isrv;
    isrv = get_highest_priority_int(s->isr);
    if (isrv < 0)
        return;
    reset_bit(s->isr, isrv);
    /* XXX: send the EOI packet to the APIC bus to allow the I/O APIC to
            set the remote IRR bit for level triggered interrupts. */
    apic_update_irq(s);
}

static void apic_get_delivery_bitmask(uint32_t *deliver_bitmask,
                                      uint8_t dest, uint8_t dest_mode)
{
    APICState *apic_iter;
    int i;

    if (dest_mode == 0) {
        if (dest == 0xff) {
            memset(deliver_bitmask, 0xff, MAX_APIC_WORDS * sizeof(uint32_t));
        } else {
            memset(deliver_bitmask, 0x00, MAX_APIC_WORDS * sizeof(uint32_t));
            set_bit(deliver_bitmask, dest);
        }
    } else {
        /* XXX: cluster mode */
        memset(deliver_bitmask, 0x00, MAX_APIC_WORDS * sizeof(uint32_t));
        for(i = 0; i < MAX_APICS; i++) {
            apic_iter = local_apics[i];
            if (apic_iter) {
                if (apic_iter->dest_mode == 0xf) {
                    if (dest & apic_iter->log_dest)
                        set_bit(deliver_bitmask, i);
                } else if (apic_iter->dest_mode == 0x0) {
                    if ((dest & 0xf0) == (apic_iter->log_dest & 0xf0) &&
                        (dest & apic_iter->log_dest & 0x0f)) {
                        set_bit(deliver_bitmask, i);
                    }
                }
            }
        }
    }
}


static void apic_init_ipi(APICState *s)
{
    int i;

    s->tpr = 0;
    s->spurious_vec = 0xff;
    s->log_dest = 0;
    s->dest_mode = 0xf;
    memset(s->isr, 0, sizeof(s->isr));
    memset(s->tmr, 0, sizeof(s->tmr));
    memset(s->irr, 0, sizeof(s->irr));
    for(i = 0; i < APIC_LVT_NB; i++)
        s->lvt[i] = 1 << 16; /* mask LVT */
    s->esr = 0;
    memset(s->icr, 0, sizeof(s->icr));
    s->divide_conf = 0;
    s->count_shift = 0;
    s->initial_count = 0;
    s->initial_count_load_time = 0;
    s->next_time = 0;

    cpu_reset(s->cpu_env);

    if (!(s->apicbase & MSR_IA32_APICBASE_BSP))
        s->cpu_env->halted = 1;
}

/* send a SIPI message to the CPU to start it */
static void apic_startup(APICState *s, int vector_num)
{
    CPUState *env = s->cpu_env;
    if (!env->halted)
        return;
    env->eip = 0;
    cpu_x86_load_seg_cache(env, R_CS, vector_num << 8, vector_num << 12,
                           0xffff, 0);
    env->halted = 0;
}

static void apic_deliver(APICState *s, uint8_t dest, uint8_t dest_mode,
                         uint8_t delivery_mode, uint8_t vector_num,
                         uint8_t polarity, uint8_t trigger_mode)
{
    uint32_t deliver_bitmask[MAX_APIC_WORDS];
    int dest_shorthand = (s->icr[0] >> 18) & 3;
    APICState *apic_iter;

    switch (dest_shorthand) {
    case 0:
        apic_get_delivery_bitmask(deliver_bitmask, dest, dest_mode);
        break;
    case 1:
        memset(deliver_bitmask, 0x00, sizeof(deliver_bitmask));
        set_bit(deliver_bitmask, s->id);
        break;
    case 2:
        memset(deliver_bitmask, 0xff, sizeof(deliver_bitmask));
        break;
    case 3:
        memset(deliver_bitmask, 0xff, sizeof(deliver_bitmask));
        reset_bit(deliver_bitmask, s->id);
        break;
    }

    switch (delivery_mode) {
        case APIC_DM_INIT:
            {
                int trig_mode = (s->icr[0] >> 15) & 1;
                int level = (s->icr[0] >> 14) & 1;
                if (level == 0 && trig_mode == 1) {
                    foreach_apic(apic_iter, deliver_bitmask,
                                 apic_iter->arb_id = apic_iter->id );
                    return;
                }
            }
            break;

        case APIC_DM_SIPI:
            foreach_apic(apic_iter, deliver_bitmask,
                         apic_startup(apic_iter, vector_num) );
            return;
    }

    apic_bus_deliver(deliver_bitmask, delivery_mode, vector_num, polarity,
                     trigger_mode);
}

int apic_get_interrupt(CPUState *env)
{
    APICState *s = env->apic_state;
    int intno;

    /* if the APIC is installed or enabled, we let the 8259 handle the
       IRQs */
    if (!s)
        return -1;
    if (!(s->spurious_vec & APIC_SV_ENABLE))
        return -1;

    /* XXX: spurious IRQ handling */
    intno = get_highest_priority_int(s->irr);
    if (intno < 0)
        return -1;
    if (s->tpr && intno <= s->tpr)
        return s->spurious_vec & 0xff;
    reset_bit(s->irr, intno);
    set_bit(s->isr, intno);
    apic_update_irq(s);
    return intno;
}

int apic_accept_pic_intr(CPUState *env)
{
    APICState *s = env->apic_state;
    uint32_t lvt0;

    if (!s)
        return -1;

    lvt0 = s->lvt[APIC_LVT_LINT0];

    if ((s->apicbase & MSR_IA32_APICBASE_ENABLE) == 0 ||
        (lvt0 & APIC_LVT_MASKED) == 0)
        return 1;

    return 0;
}

static uint32_t apic_get_current_count(APICState *s)
{
    int64_t d;
    uint32_t val;
    d = (qemu_get_clock(vm_clock) - s->initial_count_load_time) >>
        s->count_shift;
    if (s->lvt[APIC_LVT_TIMER] & APIC_LVT_TIMER_PERIODIC) {
        /* periodic */
        val = s->initial_count - (d % ((uint64_t)s->initial_count + 1));
    } else {
        if (d >= s->initial_count)
            val = 0;
        else
            val = s->initial_count - d;
    }
    return val;
}

static void apic_timer_update(APICState *s, int64_t current_time)
{
    int64_t next_time, d;

    if (!(s->lvt[APIC_LVT_TIMER] & APIC_LVT_MASKED)) {
        d = (current_time - s->initial_count_load_time) >>
            s->count_shift;
        if (s->lvt[APIC_LVT_TIMER] & APIC_LVT_TIMER_PERIODIC) {
            if (!s->initial_count)
                goto no_timer;
            d = ((d / ((uint64_t)s->initial_count + 1)) + 1) * ((uint64_t)s->initial_count + 1);
        } else {
            if (d >= s->initial_count)
                goto no_timer;
            d = (uint64_t)s->initial_count + 1;
        }
        next_time = s->initial_count_load_time + (d << s->count_shift);
        qemu_mod_timer(s->timer, next_time);
        s->next_time = next_time;
    } else {
    no_timer:
        qemu_del_timer(s->timer);
    }
}

static void apic_timer(void *opaque)
{
    APICState *s = opaque;

    apic_local_deliver(s->cpu_env, APIC_LVT_TIMER);
    apic_timer_update(s, s->next_time);
}

static uint32_t apic_mem_readb(void *opaque, target_phys_addr_t addr)
{
    return 0;
}

static uint32_t apic_mem_readw(void *opaque, target_phys_addr_t addr)
{
    return 0;
}

static void apic_mem_writeb(void *opaque, target_phys_addr_t addr, uint32_t val)
{
}

static void apic_mem_writew(void *opaque, target_phys_addr_t addr, uint32_t val)
{
}

static uint32_t apic_mem_readl(void *opaque, target_phys_addr_t addr)
{
    CPUState *env;
    APICState *s;
    uint32_t val;
    int index;

    env = cpu_single_env;
    if (!env)
        return 0;
    s = env->apic_state;

    index = (addr >> 4) & 0xff;
    switch(index) {
    case 0x02: /* id */
        val = s->id << 24;
        break;
    case 0x03: /* version */
        val = 0x11 | ((APIC_LVT_NB - 1) << 16); /* version 0x11 */
        break;
    case 0x08:
        val = s->tpr;
        break;
    case 0x09:
        val = apic_get_arb_pri(s);
        break;
    case 0x0a:
        /* ppr */
        val = apic_get_ppr(s);
        break;
    case 0x0b:
        val = 0;
        break;
    case 0x0d:
        val = s->log_dest << 24;
        break;
    case 0x0e:
        val = s->dest_mode << 28;
        break;
    case 0x0f:
        val = s->spurious_vec;
        break;
    case 0x10 ... 0x17:
        val = s->isr[index & 7];
        break;
    case 0x18 ... 0x1f:
        val = s->tmr[index & 7];
        break;
    case 0x20 ... 0x27:
        val = s->irr[index & 7];
        break;
    case 0x28:
        val = s->esr;
        break;
    case 0x30:
    case 0x31:
        val = s->icr[index & 1];
        break;
    case 0x32 ... 0x37:
        val = s->lvt[index - 0x32];
        break;
    case 0x38:
        val = s->initial_count;
        break;
    case 0x39:
        val = apic_get_current_count(s);
        break;
    case 0x3e:
        val = s->divide_conf;
        break;
    default:
        s->esr |= ESR_ILLEGAL_ADDRESS;
        val = 0;
        break;
    }
#ifdef DEBUG_APIC
    printf("APIC read: %08x = %08x\n", (uint32_t)addr, val);
#endif
    return val;
}

static void apic_mem_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    CPUState *env;
    APICState *s;
    int index;

    env = cpu_single_env;
    if (!env)
        return;
    s = env->apic_state;

#ifdef DEBUG_APIC
    printf("APIC write: %08x = %08x\n", (uint32_t)addr, val);
#endif

    index = (addr >> 4) & 0xff;
    switch(index) {
    case 0x02:
        s->id = (val >> 24);
        break;
    case 0x03:
        break;
    case 0x08:
        s->tpr = val;
        apic_update_irq(s);
        break;
    case 0x09:
    case 0x0a:
        break;
    case 0x0b: /* EOI */
        apic_eoi(s);
        break;
    case 0x0d:
        s->log_dest = val >> 24;
        break;
    case 0x0e:
        s->dest_mode = val >> 28;
        break;
    case 0x0f:
        s->spurious_vec = val & 0x1ff;
        apic_update_irq(s);
        break;
    case 0x10 ... 0x17:
    case 0x18 ... 0x1f:
    case 0x20 ... 0x27:
    case 0x28:
        break;
    case 0x30:
        s->icr[0] = val;
        apic_deliver(s, (s->icr[1] >> 24) & 0xff, (s->icr[0] >> 11) & 1,
                     (s->icr[0] >> 8) & 7, (s->icr[0] & 0xff),
                     (s->icr[0] >> 14) & 1, (s->icr[0] >> 15) & 1);
        break;
    case 0x31:
        s->icr[1] = val;
        break;
    case 0x32 ... 0x37:
        {
            int n = index - 0x32;
            s->lvt[n] = val;
            if (n == APIC_LVT_TIMER)
                apic_timer_update(s, qemu_get_clock(vm_clock));
        }
        break;
    case 0x38:
        s->initial_count = val;
        s->initial_count_load_time = qemu_get_clock(vm_clock);
        apic_timer_update(s, s->initial_count_load_time);
        break;
    case 0x39:
        break;
    case 0x3e:
        {
            int v;
            s->divide_conf = val & 0xb;
            v = (s->divide_conf & 3) | ((s->divide_conf >> 1) & 4);
            s->count_shift = (v + 1) & 7;
        }
        break;
    default:
        s->esr |= ESR_ILLEGAL_ADDRESS;
        break;
    }
}

static void apic_save(QEMUFile *f, void *opaque)
{
    APICState *s = opaque;
    int i;

    qemu_put_be32s(f, &s->apicbase);
    qemu_put_8s(f, &s->id);
    qemu_put_8s(f, &s->arb_id);
    qemu_put_8s(f, &s->tpr);
    qemu_put_be32s(f, &s->spurious_vec);
    qemu_put_8s(f, &s->log_dest);
    qemu_put_8s(f, &s->dest_mode);
    for (i = 0; i < 8; i++) {
        qemu_put_be32s(f, &s->isr[i]);
        qemu_put_be32s(f, &s->tmr[i]);
        qemu_put_be32s(f, &s->irr[i]);
    }
    for (i = 0; i < APIC_LVT_NB; i++) {
        qemu_put_be32s(f, &s->lvt[i]);
    }
    qemu_put_be32s(f, &s->esr);
    qemu_put_be32s(f, &s->icr[0]);
    qemu_put_be32s(f, &s->icr[1]);
    qemu_put_be32s(f, &s->divide_conf);
    qemu_put_be32(f, s->count_shift);
    qemu_put_be32s(f, &s->initial_count);
    qemu_put_be64(f, s->initial_count_load_time);
    qemu_put_be64(f, s->next_time);

    qemu_put_timer(f, s->timer);
}

static int apic_load(QEMUFile *f, void *opaque, int version_id)
{
    APICState *s = opaque;
    int i;

    if (version_id > 2)
        return -EINVAL;

    /* XXX: what if the base changes? (registered memory regions) */
    qemu_get_be32s(f, &s->apicbase);
    qemu_get_8s(f, &s->id);
    qemu_get_8s(f, &s->arb_id);
    qemu_get_8s(f, &s->tpr);
    qemu_get_be32s(f, &s->spurious_vec);
    qemu_get_8s(f, &s->log_dest);
    qemu_get_8s(f, &s->dest_mode);
    for (i = 0; i < 8; i++) {
        qemu_get_be32s(f, &s->isr[i]);
        qemu_get_be32s(f, &s->tmr[i]);
        qemu_get_be32s(f, &s->irr[i]);
    }
    for (i = 0; i < APIC_LVT_NB; i++) {
        qemu_get_be32s(f, &s->lvt[i]);
    }
    qemu_get_be32s(f, &s->esr);
    qemu_get_be32s(f, &s->icr[0]);
    qemu_get_be32s(f, &s->icr[1]);
    qemu_get_be32s(f, &s->divide_conf);
    s->count_shift=qemu_get_be32(f);
    qemu_get_be32s(f, &s->initial_count);
    s->initial_count_load_time=qemu_get_be64(f);
    s->next_time=qemu_get_be64(f);

    if (version_id >= 2)
        qemu_get_timer(f, s->timer);
    return 0;
}

static void apic_reset(void *opaque)
{
    APICState *s = opaque;

    s->apicbase = 0xfee00000 |
        (s->id ? 0 : MSR_IA32_APICBASE_BSP) | MSR_IA32_APICBASE_ENABLE;

    apic_init_ipi(s);

    if (s->id == 0) {
        /*
         * LINT0 delivery mode on CPU #0 is set to ExtInt at initialization
         * time typically by BIOS, so PIC interrupt can be delivered to the
         * processor when local APIC is enabled.
         */
        s->lvt[APIC_LVT_LINT0] = 0x700;
    }
}

static CPUReadMemoryFunc *apic_mem_read[3] = {
    apic_mem_readb,
    apic_mem_readw,
    apic_mem_readl,
};

static CPUWriteMemoryFunc *apic_mem_write[3] = {
    apic_mem_writeb,
    apic_mem_writew,
    apic_mem_writel,
};

int apic_init(CPUState *env)
{
    APICState *s;

    if (last_apic_id >= MAX_APICS)
        return -1;
    s = qemu_mallocz(sizeof(APICState));
    if (!s)
        return -1;
    env->apic_state = s;
    s->id = last_apic_id++;
    env->cpuid_apic_id = s->id;
    s->cpu_env = env;

    apic_reset(s);

    /* XXX: mapping more APICs at the same memory location */
    if (apic_io_memory == 0) {
        /* NOTE: the APIC is directly connected to the CPU - it is not
           on the global memory bus. */
        apic_io_memory = cpu_register_io_memory(0, apic_mem_read,
                                                apic_mem_write, NULL);
        cpu_register_physical_memory(s->apicbase & ~0xfff, 0x1000,
                                     apic_io_memory);
    }
    s->timer = qemu_new_timer(vm_clock, apic_timer, s);

    register_savevm("apic", s->id, 2, apic_save, apic_load, s);
    qemu_register_reset(apic_reset, s);

    local_apics[s->id] = s;
    return 0;
}

static void ioapic_service(IOAPICState *s)
{
    uint8_t i;
    uint8_t trig_mode;
    uint8_t vector;
    uint8_t delivery_mode;
    uint32_t mask;
    uint64_t entry;
    uint8_t dest;
    uint8_t dest_mode;
    uint8_t polarity;
    uint32_t deliver_bitmask[MAX_APIC_WORDS];

    for (i = 0; i < IOAPIC_NUM_PINS; i++) {
        mask = 1 << i;
        if (s->irr & mask) {
            entry = s->ioredtbl[i];
            if (!(entry & APIC_LVT_MASKED)) {
                trig_mode = ((entry >> 15) & 1);
                dest = entry >> 56;
                dest_mode = (entry >> 11) & 1;
                delivery_mode = (entry >> 8) & 7;
                polarity = (entry >> 13) & 1;
                if (trig_mode == APIC_TRIGGER_EDGE)
                    s->irr &= ~mask;
                if (delivery_mode == APIC_DM_EXTINT)
                    vector = pic_read_irq(isa_pic);
                else
                    vector = entry & 0xff;

                apic_get_delivery_bitmask(deliver_bitmask, dest, dest_mode);
                apic_bus_deliver(deliver_bitmask, delivery_mode,
                                 vector, polarity, trig_mode);
            }
        }
    }
}

void ioapic_set_irq(void *opaque, int vector, int level)
{
    IOAPICState *s = opaque;

    if (vector >= 0 && vector < IOAPIC_NUM_PINS) {
        uint32_t mask = 1 << vector;
        uint64_t entry = s->ioredtbl[vector];

        if ((entry >> 15) & 1) {
            /* level triggered */
            if (level) {
                s->irr |= mask;
                ioapic_service(s);
            } else {
                s->irr &= ~mask;
            }
        } else {
            /* edge triggered */
            if (level) {
                s->irr |= mask;
                ioapic_service(s);
            }
        }
    }
}

static uint32_t ioapic_mem_readl(void *opaque, target_phys_addr_t addr)
{
    IOAPICState *s = opaque;
    int index;
    uint32_t val = 0;

    addr &= 0xff;
    if (addr == 0x00) {
        val = s->ioregsel;
    } else if (addr == 0x10) {
        switch (s->ioregsel) {
            case 0x00:
                val = s->id << 24;
                break;
            case 0x01:
                val = 0x11 | ((IOAPIC_NUM_PINS - 1) << 16); /* version 0x11 */
                break;
            case 0x02:
                val = 0;
                break;
            default:
                index = (s->ioregsel - 0x10) >> 1;
                if (index >= 0 && index < IOAPIC_NUM_PINS) {
                    if (s->ioregsel & 1)
                        val = s->ioredtbl[index] >> 32;
                    else
                        val = s->ioredtbl[index] & 0xffffffff;
                }
        }
#ifdef DEBUG_IOAPIC
        printf("I/O APIC read: %08x = %08x\n", s->ioregsel, val);
#endif
    }
    return val;
}

static void ioapic_mem_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    IOAPICState *s = opaque;
    int index;

    addr &= 0xff;
    if (addr == 0x00)  {
        s->ioregsel = val;
        return;
    } else if (addr == 0x10) {
#ifdef DEBUG_IOAPIC
        printf("I/O APIC write: %08x = %08x\n", s->ioregsel, val);
#endif
        switch (s->ioregsel) {
            case 0x00:
                s->id = (val >> 24) & 0xff;
                return;
            case 0x01:
            case 0x02:
                return;
            default:
                index = (s->ioregsel - 0x10) >> 1;
                if (index >= 0 && index < IOAPIC_NUM_PINS) {
                    if (s->ioregsel & 1) {
                        s->ioredtbl[index] &= 0xffffffff;
                        s->ioredtbl[index] |= (uint64_t)val << 32;
                    } else {
                        s->ioredtbl[index] &= ~0xffffffffULL;
                        s->ioredtbl[index] |= val;
                    }
                    ioapic_service(s);
                }
        }
    }
}

static void ioapic_save(QEMUFile *f, void *opaque)
{
    IOAPICState *s = opaque;
    int i;

    qemu_put_8s(f, &s->id);
    qemu_put_8s(f, &s->ioregsel);
    for (i = 0; i < IOAPIC_NUM_PINS; i++) {
        qemu_put_be64s(f, &s->ioredtbl[i]);
    }
}

static int ioapic_load(QEMUFile *f, void *opaque, int version_id)
{
    IOAPICState *s = opaque;
    int i;

    if (version_id != 1)
        return -EINVAL;

    qemu_get_8s(f, &s->id);
    qemu_get_8s(f, &s->ioregsel);
    for (i = 0; i < IOAPIC_NUM_PINS; i++) {
        qemu_get_be64s(f, &s->ioredtbl[i]);
    }
    return 0;
}

static void ioapic_reset(void *opaque)
{
    IOAPICState *s = opaque;
    int i;

    memset(s, 0, sizeof(*s));
    for(i = 0; i < IOAPIC_NUM_PINS; i++)
        s->ioredtbl[i] = 1 << 16; /* mask LVT */
}

static CPUReadMemoryFunc *ioapic_mem_read[3] = {
    ioapic_mem_readl,
    ioapic_mem_readl,
    ioapic_mem_readl,
};

static CPUWriteMemoryFunc *ioapic_mem_write[3] = {
    ioapic_mem_writel,
    ioapic_mem_writel,
    ioapic_mem_writel,
};

IOAPICState *ioapic_init(void)
{
    IOAPICState *s;
    int io_memory;

    s = qemu_mallocz(sizeof(IOAPICState));
    if (!s)
        return NULL;
    ioapic_reset(s);
    s->id = last_apic_id++;

    io_memory = cpu_register_io_memory(0, ioapic_mem_read,
                                       ioapic_mem_write, s);
    cpu_register_physical_memory(0xfec00000, 0x1000, io_memory);

    register_savevm("ioapic", 0, 1, ioapic_save, ioapic_load, s);
    qemu_register_reset(ioapic_reset, s);

    return s;
}

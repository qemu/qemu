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
#include "vl.h"

//#define DEBUG_APIC

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

#define APIC_TRIGGER_EDGE  0
#define APIC_TRIGGER_LEVEL 1

#define	APIC_LVT_TIMER_PERIODIC		(1<<17)
#define	APIC_LVT_MASKED			(1<<16)
#define	APIC_LVT_LEVEL_TRIGGER		(1<<15)
#define	APIC_LVT_REMOTE_IRR		(1<<14)
#define	APIC_INPUT_POLARITY		(1<<13)
#define	APIC_SEND_PENDING		(1<<12)

#define ESR_ILLEGAL_ADDRESS (1 << 7)

#define APIC_SV_ENABLE (1 << 8)

typedef struct APICState {
    CPUState *cpu_env;
    uint32_t apicbase;
    uint8_t id;
    uint8_t tpr;
    uint32_t spurious_vec;
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

static int apic_io_memory;

void cpu_set_apic_base(CPUState *env, uint64_t val)
{
    APICState *s = env->apic_state;
#ifdef DEBUG_APIC
    printf("cpu_set_apic_base: %016llx\n", val);
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
    printf("cpu_get_apic_base: %016llx\n", (uint64_t)s->apicbase);
#endif
    return s->apicbase;
}

void cpu_set_apic_tpr(CPUX86State *env, uint8_t val)
{
    APICState *s = env->apic_state;
    s->tpr = (val & 0x0f) << 4;
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
    for(i = 0;i < 8; i++) {
        if (tab[i] != 0) {
            return i * 32 + ffs(tab[i]) - 1;
        }
    }
    return -1;
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

/* signal the CPU if an irq is pending */
static void apic_update_irq(APICState *s)
{
    int irrv, isrv;
    irrv = get_highest_priority_int(s->irr);
    if (irrv < 0)
        return;
    isrv = get_highest_priority_int(s->isr);
    /* if the pending irq has less priority, we do not make a new request */
    if (isrv >= 0 && irrv >= isrv)
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
    apic_update_irq(s);
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
    reset_bit(s->irr, intno);
    set_bit(s->isr, intno);
    apic_update_irq(s);
    return intno;
}

static uint32_t apic_get_current_count(APICState *s)
{
    int64_t d;
    uint32_t val;
    d = (qemu_get_clock(vm_clock) - s->initial_count_load_time) >> 
        s->count_shift;
    if (s->lvt[APIC_LVT_TIMER] & APIC_LVT_TIMER_PERIODIC) {
        /* periodic */
        val = s->initial_count - (d % (s->initial_count + 1));
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
            d = ((d / (s->initial_count + 1)) + 1) * (s->initial_count + 1);
        } else {
            if (d >= s->initial_count)
                goto no_timer;
            d = s->initial_count + 1;
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

    if (!(s->lvt[APIC_LVT_TIMER] & APIC_LVT_MASKED)) {
        apic_set_irq(s, s->lvt[APIC_LVT_TIMER] & 0xff, APIC_TRIGGER_EDGE);
    }
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
    case 0x0a:
        /* ppr */
        val = apic_get_ppr(s);
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
    case 0x32 ... 0x37:
        val = s->lvt[index - 0x32];
        break;
    case 0x30:
    case 0x31:
        val = s->icr[index & 1];
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
    case 0x08:
        s->tpr = val;
        break;
    case 0x0b: /* EOI */
        apic_eoi(s);
        break;
    case 0x0f:
        s->spurious_vec = val & 0x1ff;
        break;
    case 0x30:
    case 0x31:
        s->icr[index & 1] = val;
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
    int i;

    s = malloc(sizeof(APICState));
    if (!s)
        return -1;
    memset(s, 0, sizeof(*s));
    env->apic_state = s;
    s->cpu_env = env;
    s->apicbase = 0xfee00000 | 
        MSR_IA32_APICBASE_BSP | MSR_IA32_APICBASE_ENABLE;
    for(i = 0; i < APIC_LVT_NB; i++)
        s->lvt[i] = 1 << 16; /* mask LVT */
    s->spurious_vec = 0xff;

    if (apic_io_memory == 0) {
        /* NOTE: the APIC is directly connected to the CPU - it is not
           on the global memory bus. */
        apic_io_memory = cpu_register_io_memory(0, apic_mem_read, 
                                                apic_mem_write, NULL);
        cpu_register_physical_memory(s->apicbase & ~0xfff, 0x1000, apic_io_memory);
    }
    s->timer = qemu_new_timer(vm_clock, apic_timer, s);
    return 0;
}

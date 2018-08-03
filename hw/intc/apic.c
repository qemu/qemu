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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "cpu.h"
#include "qemu/thread.h"
#include "hw/i386/apic_internal.h"
#include "hw/i386/apic.h"
#include "hw/i386/ioapic.h"
#include "hw/pci/msi.h"
#include "qemu/host-utils.h"
#include "trace.h"
#include "hw/i386/pc.h"
#include "hw/i386/apic-msidef.h"
#include "qapi/error.h"

#define MAX_APICS 255
#define MAX_APIC_WORDS 8

#define SYNC_FROM_VAPIC                 0x1
#define SYNC_TO_VAPIC                   0x2
#define SYNC_ISR_IRR_TO_VAPIC           0x4

static APICCommonState *local_apics[MAX_APICS + 1];

#define TYPE_APIC "apic"
#define APIC(obj) \
    OBJECT_CHECK(APICCommonState, (obj), TYPE_APIC)

static void apic_set_irq(APICCommonState *s, int vector_num, int trigger_mode);
static void apic_update_irq(APICCommonState *s);
static void apic_get_delivery_bitmask(uint32_t *deliver_bitmask,
                                      uint8_t dest, uint8_t dest_mode);

/* Find first bit starting from msb */
static int apic_fls_bit(uint32_t value)
{
    return 31 - clz32(value);
}

/* Find first bit starting from lsb */
static int apic_ffs_bit(uint32_t value)
{
    return ctz32(value);
}

static inline void apic_reset_bit(uint32_t *tab, int index)
{
    int i, mask;
    i = index >> 5;
    mask = 1 << (index & 0x1f);
    tab[i] &= ~mask;
}

/* return -1 if no bit is set */
static int get_highest_priority_int(uint32_t *tab)
{
    int i;
    for (i = 7; i >= 0; i--) {
        if (tab[i] != 0) {
            return i * 32 + apic_fls_bit(tab[i]);
        }
    }
    return -1;
}

static void apic_sync_vapic(APICCommonState *s, int sync_type)
{
    VAPICState vapic_state;
    size_t length;
    off_t start;
    int vector;

    if (!s->vapic_paddr) {
        return;
    }
    if (sync_type & SYNC_FROM_VAPIC) {
        cpu_physical_memory_read(s->vapic_paddr, &vapic_state,
                                 sizeof(vapic_state));
        s->tpr = vapic_state.tpr;
    }
    if (sync_type & (SYNC_TO_VAPIC | SYNC_ISR_IRR_TO_VAPIC)) {
        start = offsetof(VAPICState, isr);
        length = offsetof(VAPICState, enabled) - offsetof(VAPICState, isr);

        if (sync_type & SYNC_TO_VAPIC) {
            assert(qemu_cpu_is_self(CPU(s->cpu)));

            vapic_state.tpr = s->tpr;
            vapic_state.enabled = 1;
            start = 0;
            length = sizeof(VAPICState);
        }

        vector = get_highest_priority_int(s->isr);
        if (vector < 0) {
            vector = 0;
        }
        vapic_state.isr = vector & 0xf0;

        vapic_state.zero = 0;

        vector = get_highest_priority_int(s->irr);
        if (vector < 0) {
            vector = 0;
        }
        vapic_state.irr = vector & 0xff;

        cpu_physical_memory_write_rom(&address_space_memory,
                                      s->vapic_paddr + start,
                                      ((void *)&vapic_state) + start, length);
    }
}

static void apic_vapic_base_update(APICCommonState *s)
{
    apic_sync_vapic(s, SYNC_TO_VAPIC);
}

static void apic_local_deliver(APICCommonState *s, int vector)
{
    uint32_t lvt = s->lvt[vector];
    int trigger_mode;

    trace_apic_local_deliver(vector, (lvt >> 8) & 7);

    if (lvt & APIC_LVT_MASKED)
        return;

    switch ((lvt >> 8) & 7) {
    case APIC_DM_SMI:
        cpu_interrupt(CPU(s->cpu), CPU_INTERRUPT_SMI);
        break;

    case APIC_DM_NMI:
        cpu_interrupt(CPU(s->cpu), CPU_INTERRUPT_NMI);
        break;

    case APIC_DM_EXTINT:
        cpu_interrupt(CPU(s->cpu), CPU_INTERRUPT_HARD);
        break;

    case APIC_DM_FIXED:
        trigger_mode = APIC_TRIGGER_EDGE;
        if ((vector == APIC_LVT_LINT0 || vector == APIC_LVT_LINT1) &&
            (lvt & APIC_LVT_LEVEL_TRIGGER))
            trigger_mode = APIC_TRIGGER_LEVEL;
        apic_set_irq(s, lvt & 0xff, trigger_mode);
    }
}

void apic_deliver_pic_intr(DeviceState *dev, int level)
{
    APICCommonState *s = APIC(dev);

    if (level) {
        apic_local_deliver(s, APIC_LVT_LINT0);
    } else {
        uint32_t lvt = s->lvt[APIC_LVT_LINT0];

        switch ((lvt >> 8) & 7) {
        case APIC_DM_FIXED:
            if (!(lvt & APIC_LVT_LEVEL_TRIGGER))
                break;
            apic_reset_bit(s->irr, lvt & 0xff);
            /* fall through */
        case APIC_DM_EXTINT:
            apic_update_irq(s);
            break;
        }
    }
}

static void apic_external_nmi(APICCommonState *s)
{
    apic_local_deliver(s, APIC_LVT_LINT1);
}

#define foreach_apic(apic, deliver_bitmask, code) \
{\
    int __i, __j;\
    for(__i = 0; __i < MAX_APIC_WORDS; __i++) {\
        uint32_t __mask = deliver_bitmask[__i];\
        if (__mask) {\
            for(__j = 0; __j < 32; __j++) {\
                if (__mask & (1U << __j)) {\
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
                             uint8_t delivery_mode, uint8_t vector_num,
                             uint8_t trigger_mode)
{
    APICCommonState *apic_iter;

    switch (delivery_mode) {
        case APIC_DM_LOWPRI:
            /* XXX: search for focus processor, arbitration */
            {
                int i, d;
                d = -1;
                for(i = 0; i < MAX_APIC_WORDS; i++) {
                    if (deliver_bitmask[i]) {
                        d = i * 32 + apic_ffs_bit(deliver_bitmask[i]);
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
                cpu_interrupt(CPU(apic_iter->cpu), CPU_INTERRUPT_SMI)
            );
            return;

        case APIC_DM_NMI:
            foreach_apic(apic_iter, deliver_bitmask,
                cpu_interrupt(CPU(apic_iter->cpu), CPU_INTERRUPT_NMI)
            );
            return;

        case APIC_DM_INIT:
            /* normal INIT IPI sent to processors */
            foreach_apic(apic_iter, deliver_bitmask,
                         cpu_interrupt(CPU(apic_iter->cpu),
                                       CPU_INTERRUPT_INIT)
            );
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

void apic_deliver_irq(uint8_t dest, uint8_t dest_mode, uint8_t delivery_mode,
                      uint8_t vector_num, uint8_t trigger_mode)
{
    uint32_t deliver_bitmask[MAX_APIC_WORDS];

    trace_apic_deliver_irq(dest, dest_mode, delivery_mode, vector_num,
                           trigger_mode);

    apic_get_delivery_bitmask(deliver_bitmask, dest, dest_mode);
    apic_bus_deliver(deliver_bitmask, delivery_mode, vector_num, trigger_mode);
}

static void apic_set_base(APICCommonState *s, uint64_t val)
{
    s->apicbase = (val & 0xfffff000) |
        (s->apicbase & (MSR_IA32_APICBASE_BSP | MSR_IA32_APICBASE_ENABLE));
    /* if disabled, cannot be enabled again */
    if (!(val & MSR_IA32_APICBASE_ENABLE)) {
        s->apicbase &= ~MSR_IA32_APICBASE_ENABLE;
        cpu_clear_apic_feature(&s->cpu->env);
        s->spurious_vec &= ~APIC_SV_ENABLE;
    }
}

static void apic_set_tpr(APICCommonState *s, uint8_t val)
{
    /* Updates from cr8 are ignored while the VAPIC is active */
    if (!s->vapic_paddr) {
        s->tpr = val << 4;
        apic_update_irq(s);
    }
}

int apic_get_highest_priority_irr(DeviceState *dev)
{
    APICCommonState *s;

    if (!dev) {
        /* no interrupts */
        return -1;
    }
    s = APIC_COMMON(dev);
    return get_highest_priority_int(s->irr);
}

static uint8_t apic_get_tpr(APICCommonState *s)
{
    apic_sync_vapic(s, SYNC_FROM_VAPIC);
    return s->tpr >> 4;
}

int apic_get_ppr(APICCommonState *s)
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

static int apic_get_arb_pri(APICCommonState *s)
{
    /* XXX: arbitration */
    return 0;
}


/*
 * <0 - low prio interrupt,
 * 0  - no interrupt,
 * >0 - interrupt number
 */
static int apic_irq_pending(APICCommonState *s)
{
    int irrv, ppr;

    if (!(s->spurious_vec & APIC_SV_ENABLE)) {
        return 0;
    }

    irrv = get_highest_priority_int(s->irr);
    if (irrv < 0) {
        return 0;
    }
    ppr = apic_get_ppr(s);
    if (ppr && (irrv & 0xf0) <= (ppr & 0xf0)) {
        return -1;
    }

    return irrv;
}

/* signal the CPU if an irq is pending */
static void apic_update_irq(APICCommonState *s)
{
    CPUState *cpu;
    DeviceState *dev = (DeviceState *)s;

    cpu = CPU(s->cpu);
    if (!qemu_cpu_is_self(cpu)) {
        cpu_interrupt(cpu, CPU_INTERRUPT_POLL);
    } else if (apic_irq_pending(s) > 0) {
        cpu_interrupt(cpu, CPU_INTERRUPT_HARD);
    } else if (!apic_accept_pic_intr(dev) || !pic_get_output(isa_pic)) {
        cpu_reset_interrupt(cpu, CPU_INTERRUPT_HARD);
    }
}

void apic_poll_irq(DeviceState *dev)
{
    APICCommonState *s = APIC(dev);

    apic_sync_vapic(s, SYNC_FROM_VAPIC);
    apic_update_irq(s);
}

static void apic_set_irq(APICCommonState *s, int vector_num, int trigger_mode)
{
    apic_report_irq_delivered(!apic_get_bit(s->irr, vector_num));

    apic_set_bit(s->irr, vector_num);
    if (trigger_mode)
        apic_set_bit(s->tmr, vector_num);
    else
        apic_reset_bit(s->tmr, vector_num);
    if (s->vapic_paddr) {
        apic_sync_vapic(s, SYNC_ISR_IRR_TO_VAPIC);
        /*
         * The vcpu thread needs to see the new IRR before we pull its current
         * TPR value. That way, if we miss a lowering of the TRP, the guest
         * has the chance to notice the new IRR and poll for IRQs on its own.
         */
        smp_wmb();
        apic_sync_vapic(s, SYNC_FROM_VAPIC);
    }
    apic_update_irq(s);
}

static void apic_eoi(APICCommonState *s)
{
    int isrv;
    isrv = get_highest_priority_int(s->isr);
    if (isrv < 0)
        return;
    apic_reset_bit(s->isr, isrv);
    if (!(s->spurious_vec & APIC_SV_DIRECTED_IO) && apic_get_bit(s->tmr, isrv)) {
        ioapic_eoi_broadcast(isrv);
    }
    apic_sync_vapic(s, SYNC_FROM_VAPIC | SYNC_TO_VAPIC);
    apic_update_irq(s);
}

static int apic_find_dest(uint8_t dest)
{
    APICCommonState *apic = local_apics[dest];
    int i;

    if (apic && apic->id == dest)
        return dest;  /* shortcut in case apic->id == local_apics[dest]->id */

    for (i = 0; i < MAX_APICS; i++) {
        apic = local_apics[i];
	if (apic && apic->id == dest)
            return i;
        if (!apic)
            break;
    }

    return -1;
}

static void apic_get_delivery_bitmask(uint32_t *deliver_bitmask,
                                      uint8_t dest, uint8_t dest_mode)
{
    APICCommonState *apic_iter;
    int i;

    if (dest_mode == 0) {
        if (dest == 0xff) {
            memset(deliver_bitmask, 0xff, MAX_APIC_WORDS * sizeof(uint32_t));
        } else {
            int idx = apic_find_dest(dest);
            memset(deliver_bitmask, 0x00, MAX_APIC_WORDS * sizeof(uint32_t));
            if (idx >= 0)
                apic_set_bit(deliver_bitmask, idx);
        }
    } else {
        /* XXX: cluster mode */
        memset(deliver_bitmask, 0x00, MAX_APIC_WORDS * sizeof(uint32_t));
        for(i = 0; i < MAX_APICS; i++) {
            apic_iter = local_apics[i];
            if (apic_iter) {
                if (apic_iter->dest_mode == 0xf) {
                    if (dest & apic_iter->log_dest)
                        apic_set_bit(deliver_bitmask, i);
                } else if (apic_iter->dest_mode == 0x0) {
                    if ((dest & 0xf0) == (apic_iter->log_dest & 0xf0) &&
                        (dest & apic_iter->log_dest & 0x0f)) {
                        apic_set_bit(deliver_bitmask, i);
                    }
                }
            } else {
                break;
            }
        }
    }
}

static void apic_startup(APICCommonState *s, int vector_num)
{
    s->sipi_vector = vector_num;
    cpu_interrupt(CPU(s->cpu), CPU_INTERRUPT_SIPI);
}

void apic_sipi(DeviceState *dev)
{
    APICCommonState *s = APIC(dev);

    cpu_reset_interrupt(CPU(s->cpu), CPU_INTERRUPT_SIPI);

    if (!s->wait_for_sipi)
        return;
    cpu_x86_load_seg_cache_sipi(s->cpu, s->sipi_vector);
    s->wait_for_sipi = 0;
}

static void apic_deliver(DeviceState *dev, uint8_t dest, uint8_t dest_mode,
                         uint8_t delivery_mode, uint8_t vector_num,
                         uint8_t trigger_mode)
{
    APICCommonState *s = APIC(dev);
    uint32_t deliver_bitmask[MAX_APIC_WORDS];
    int dest_shorthand = (s->icr[0] >> 18) & 3;
    APICCommonState *apic_iter;

    switch (dest_shorthand) {
    case 0:
        apic_get_delivery_bitmask(deliver_bitmask, dest, dest_mode);
        break;
    case 1:
        memset(deliver_bitmask, 0x00, sizeof(deliver_bitmask));
        apic_set_bit(deliver_bitmask, s->id);
        break;
    case 2:
        memset(deliver_bitmask, 0xff, sizeof(deliver_bitmask));
        break;
    case 3:
        memset(deliver_bitmask, 0xff, sizeof(deliver_bitmask));
        apic_reset_bit(deliver_bitmask, s->id);
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

    apic_bus_deliver(deliver_bitmask, delivery_mode, vector_num, trigger_mode);
}

static bool apic_check_pic(APICCommonState *s)
{
    DeviceState *dev = (DeviceState *)s;

    if (!apic_accept_pic_intr(dev) || !pic_get_output(isa_pic)) {
        return false;
    }
    apic_deliver_pic_intr(dev, 1);
    return true;
}

int apic_get_interrupt(DeviceState *dev)
{
    APICCommonState *s = APIC(dev);
    int intno;

    /* if the APIC is installed or enabled, we let the 8259 handle the
       IRQs */
    if (!s)
        return -1;
    if (!(s->spurious_vec & APIC_SV_ENABLE))
        return -1;

    apic_sync_vapic(s, SYNC_FROM_VAPIC);
    intno = apic_irq_pending(s);

    /* if there is an interrupt from the 8259, let the caller handle
     * that first since ExtINT interrupts ignore the priority.
     */
    if (intno == 0 || apic_check_pic(s)) {
        apic_sync_vapic(s, SYNC_TO_VAPIC);
        return -1;
    } else if (intno < 0) {
        apic_sync_vapic(s, SYNC_TO_VAPIC);
        return s->spurious_vec & 0xff;
    }
    apic_reset_bit(s->irr, intno);
    apic_set_bit(s->isr, intno);
    apic_sync_vapic(s, SYNC_TO_VAPIC);

    apic_update_irq(s);

    return intno;
}

int apic_accept_pic_intr(DeviceState *dev)
{
    APICCommonState *s = APIC(dev);
    uint32_t lvt0;

    if (!s)
        return -1;

    lvt0 = s->lvt[APIC_LVT_LINT0];

    if ((s->apicbase & MSR_IA32_APICBASE_ENABLE) == 0 ||
        (lvt0 & APIC_LVT_MASKED) == 0)
        return 1;

    return 0;
}

static uint32_t apic_get_current_count(APICCommonState *s)
{
    int64_t d;
    uint32_t val;
    d = (qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - s->initial_count_load_time) >>
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

static void apic_timer_update(APICCommonState *s, int64_t current_time)
{
    if (apic_next_timer(s, current_time)) {
        timer_mod(s->timer, s->next_time);
    } else {
        timer_del(s->timer);
    }
}

static void apic_timer(void *opaque)
{
    APICCommonState *s = opaque;

    apic_local_deliver(s, APIC_LVT_TIMER);
    apic_timer_update(s, s->next_time);
}

static uint64_t apic_mem_read(void *opaque, hwaddr addr, unsigned size)
{
    DeviceState *dev;
    APICCommonState *s;
    uint32_t val;
    int index;

    if (size < 4) {
        return 0;
    }

    dev = cpu_get_current_apic();
    if (!dev) {
        return 0;
    }
    s = APIC(dev);

    index = (addr >> 4) & 0xff;
    switch(index) {
    case 0x02: /* id */
        val = s->id << 24;
        break;
    case 0x03: /* version */
        val = s->version | ((APIC_LVT_NB - 1) << 16);
        break;
    case 0x08:
        apic_sync_vapic(s, SYNC_FROM_VAPIC);
        if (apic_report_tpr_access) {
            cpu_report_tpr_access(&s->cpu->env, TPR_ACCESS_READ);
        }
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
        val = (s->dest_mode << 28) | 0xfffffff;
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
        s->esr |= APIC_ESR_ILLEGAL_ADDRESS;
        val = 0;
        break;
    }
    trace_apic_mem_readl(addr, val);
    return val;
}

static void apic_send_msi(MSIMessage *msi)
{
    uint64_t addr = msi->address;
    uint32_t data = msi->data;
    uint8_t dest = (addr & MSI_ADDR_DEST_ID_MASK) >> MSI_ADDR_DEST_ID_SHIFT;
    uint8_t vector = (data & MSI_DATA_VECTOR_MASK) >> MSI_DATA_VECTOR_SHIFT;
    uint8_t dest_mode = (addr >> MSI_ADDR_DEST_MODE_SHIFT) & 0x1;
    uint8_t trigger_mode = (data >> MSI_DATA_TRIGGER_SHIFT) & 0x1;
    uint8_t delivery = (data >> MSI_DATA_DELIVERY_MODE_SHIFT) & 0x7;
    /* XXX: Ignore redirection hint. */
    apic_deliver_irq(dest, dest_mode, delivery, vector, trigger_mode);
}

static void apic_mem_write(void *opaque, hwaddr addr, uint64_t val,
                           unsigned size)
{
    DeviceState *dev;
    APICCommonState *s;
    int index = (addr >> 4) & 0xff;

    if (size < 4) {
        return;
    }

    if (addr > 0xfff || !index) {
        /* MSI and MMIO APIC are at the same memory location,
         * but actually not on the global bus: MSI is on PCI bus
         * APIC is connected directly to the CPU.
         * Mapping them on the global bus happens to work because
         * MSI registers are reserved in APIC MMIO and vice versa. */
        MSIMessage msi = { .address = addr, .data = val };
        apic_send_msi(&msi);
        return;
    }

    dev = cpu_get_current_apic();
    if (!dev) {
        return;
    }
    s = APIC(dev);

    trace_apic_mem_writel(addr, val);

    switch(index) {
    case 0x02:
        s->id = (val >> 24);
        break;
    case 0x03:
        break;
    case 0x08:
        if (apic_report_tpr_access) {
            cpu_report_tpr_access(&s->cpu->env, TPR_ACCESS_WRITE);
        }
        s->tpr = val;
        apic_sync_vapic(s, SYNC_TO_VAPIC);
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
        apic_deliver(dev, (s->icr[1] >> 24) & 0xff, (s->icr[0] >> 11) & 1,
                     (s->icr[0] >> 8) & 7, (s->icr[0] & 0xff),
                     (s->icr[0] >> 15) & 1);
        break;
    case 0x31:
        s->icr[1] = val;
        break;
    case 0x32 ... 0x37:
        {
            int n = index - 0x32;
            s->lvt[n] = val;
            if (n == APIC_LVT_TIMER) {
                apic_timer_update(s, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
            } else if (n == APIC_LVT_LINT0 && apic_check_pic(s)) {
                apic_update_irq(s);
            }
        }
        break;
    case 0x38:
        s->initial_count = val;
        s->initial_count_load_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
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
        s->esr |= APIC_ESR_ILLEGAL_ADDRESS;
        break;
    }
}

static void apic_pre_save(APICCommonState *s)
{
    apic_sync_vapic(s, SYNC_FROM_VAPIC);
}

static void apic_post_load(APICCommonState *s)
{
    if (s->timer_expiry != -1) {
        timer_mod(s->timer, s->timer_expiry);
    } else {
        timer_del(s->timer);
    }
}

static const MemoryRegionOps apic_io_ops = {
    .read = apic_mem_read,
    .write = apic_mem_write,
    .impl.min_access_size = 1,
    .impl.max_access_size = 4,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void apic_realize(DeviceState *dev, Error **errp)
{
    APICCommonState *s = APIC(dev);

    if (s->id >= MAX_APICS) {
        error_setg(errp, "%s initialization failed. APIC ID %d is invalid",
                   object_get_typename(OBJECT(dev)), s->id);
        return;
    }

    memory_region_init_io(&s->io_memory, OBJECT(s), &apic_io_ops, s, "apic-msi",
                          APIC_SPACE_SIZE);

    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, apic_timer, s);
    local_apics[s->id] = s;

    msi_nonbroken = true;
}

static void apic_unrealize(DeviceState *dev, Error **errp)
{
    APICCommonState *s = APIC(dev);

    timer_del(s->timer);
    timer_free(s->timer);
    local_apics[s->id] = NULL;
}

static void apic_class_init(ObjectClass *klass, void *data)
{
    APICCommonClass *k = APIC_COMMON_CLASS(klass);

    k->realize = apic_realize;
    k->unrealize = apic_unrealize;
    k->set_base = apic_set_base;
    k->set_tpr = apic_set_tpr;
    k->get_tpr = apic_get_tpr;
    k->vapic_base_update = apic_vapic_base_update;
    k->external_nmi = apic_external_nmi;
    k->pre_save = apic_pre_save;
    k->post_load = apic_post_load;
    k->send_msi = apic_send_msi;
}

static const TypeInfo apic_info = {
    .name          = TYPE_APIC,
    .instance_size = sizeof(APICCommonState),
    .parent        = TYPE_APIC_COMMON,
    .class_init    = apic_class_init,
};

static void apic_register_types(void)
{
    type_register_static(&apic_info);
}

type_init(apic_register_types)

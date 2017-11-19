/*
 * CSKY tcip v1 emulation.
 *
 * Written by wanghb
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "sysemu/sysemu.h"
#include "hw/sysbus.h"
#include "hw/ptimer.h"
#include "qemu/log.h"
#include "trace.h"
#include "cpu.h"
#include "hw/csky/cskydev.h"

/* CoreTim */
#define CT_CSR_COUNTFLAG    (1 << 16)
#define CT_CSR_INTERNAL_CLK (1 << 2)
#define CT_CSR_TICKINT      (1 << 1)
#define CT_CSR_ENABLE       (1 << 0)

/* VIC */
#define VIC_ISR_VEC         0xff
#define VIC_ISR_INT         (1 << 10)
#define VIC_ISR_PEND_SHF    12
#define PR0                 0x400
#define PR28                0x41c
#define VIC_IPTR_EN         0x80000000
#define GET_IPTR_PRI(a)     ((a & 0xc0) >> 6)

#define TYPE_CSKY_TCIP_V1   "csky_tcip_v1"
#define CSKY_TCIP_V1(obj)   OBJECT_CHECK(csky_tcip_v1_state, (obj), \
                                         TYPE_CSKY_TCIP_V1)

typedef struct csky_tcip_v1_state {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    ptimer_state *timer;
    uint32_t coret_csr;
    uint32_t coret_rvr;
    uint32_t vic_iser;
    uint32_t vic_iwer;
    uint32_t vic_source; /* irq from device */
    uint32_t vic_ispr;
    uint32_t vic_pr[32]; /* for each vic_pr[i], only the
                            lowest two bits are valid */
    CPUCSKYState *env;
    qemu_irq irq;
} csky_tcip_v1_state;

static int coretim_irq_no;
uint32_t coretim_freq = 1000000000ll;

/* find the highest priority interrupt source
 * first poll priority_bitmap[0], then priority_bitmap[1], ...[2], ...[3]
 * count the number of trailing 0-bits starting from the least
 * significant bit position use builtin function ctz
 */
static uint32_t find_highest_priority_vec(uint32_t priority_bitmap[])
{
    uint32_t i;
    uint32_t int_vec = 0;
    for (i = 0; i < 4; i++) {
        if (priority_bitmap[i] == 0) {
            continue;
        }
        int_vec = __builtin_ctz(priority_bitmap[i]);
        break;
    }
    return int_vec;
}
/**************************************************************************
 * Description:
 *     Update the interrupt flag according the vic state and
 *     give the flag to cpu.
 * Argument:
 *     s  --- the pointer to the vic state
 * Return:
 *     void
 **************************************************************************/
static void csky_vic_v1_update(csky_tcip_v1_state *s)
{
    uint32_t int_req = 0;
    uint32_t int_best = 0;
    uint32_t int_active;
    uint32_t int_pend;
    uint32_t priority_bitmap[4] = {0, 0, 0, 0};
    uint32_t i;
    uint32_t flag = 0;
    uint32_t iptr_en = s->env->intc_signals.iptr & VIC_IPTR_EN;
    uint32_t iptr_pri = GET_IPTR_PRI(s->env->intc_signals.iptr);

    s->vic_ispr |= s->vic_source;
    int_req = s->vic_ispr & s->vic_iser;
    int_active = s->env->intc_signals.isr & VIC_ISR_VEC;

    /* There is no irq, clear the interrupt_request. */
    if ((int_req == 0) && (s->env->intc_signals.int_b == 0)) {
        qemu_set_irq(s->irq, 0);
        return;
    }

    /* generate the bitmap for each priority level and each interrupt source.
     * priority_bitmap[i] stands for priority level i, 0<=i<=3
     * and each bit of priority_bitmap[i] stands for one interrupt source
     */
    do {
        i = __builtin_clz(int_req);
        priority_bitmap[s->vic_pr[31 - i]] |= 1 << (31 - i);
        int_req &= ~(1 << (31 - i));
    } while (int_req != 0);

    int_best = find_highest_priority_vec(priority_bitmap);

    /* If psr.ee or psr.ie is not set or the irq are handling now,
     * just pending it and update the isr. */
    if (((s->env->cp0.psr & (PSR_EE_MASK | PSR_IE_MASK)) != 0x140)
        || ((s->env->intc_signals.iabr & (1 << int_best)) != 0))
    {
        s->env->intc_signals.isr = (int_active |
                                    ((int_best + 32) << VIC_ISR_PEND_SHF));
        return;
    }

    /* Response or Pending the new irq. */
    if ((s->env->intc_signals.iabr == 0)
        || ((s->vic_pr[int_best] < s->vic_pr[int_active - 32])
            && ((iptr_en == 0) || (s->vic_pr[int_best] < iptr_pri)))) {
        /* There is no irq before, or New irq that can nest the last one.  */
        s->vic_ispr &= ~(1 << int_best);
        if ((s->vic_ispr & s->vic_iser) == 0) {
            int_pend = 0;
        } else {
            priority_bitmap[s->vic_pr[int_best]] &= ~(1 << int_best);
            int_pend = find_highest_priority_vec(priority_bitmap) + 32;
        }

        s->env->intc_signals.isr = ((int_best + 32)
                                    | (int_pend << VIC_ISR_PEND_SHF));
        s->env->intc_signals.iabr |= 1 << int_best;
        flag = (int_best + 32) | VIC_ISR_INT;
        qemu_set_irq(s->irq, flag);
    } else {
        /* New irq, but can not nest the last irq. */
        s->env->intc_signals.isr = (int_active |
                                    ((int_best + 32) << VIC_ISR_PEND_SHF));
    }
}

/**************************************************************************
 * Description:
 *     Interrupt request from other devices to VIC.
 * Argument:
 *     opaque  ---  the pointer to VIC state.
 *     irq     ---  vector number of the interrupt request
 *     level   ---  set or clear the corresponding interrupt
 * Return:
 *     void
 **************************************************************************/
static void csky_vic_v1_set_irq(void *opaque, int irq, int level)
{
    csky_tcip_v1_state *s = (csky_tcip_v1_state *)opaque;
    if (level) {
        s->vic_source |= 1 << irq;
    } else {
        s->vic_source &= ~(1 << irq);
    }
    csky_vic_v1_update(s);
}

/**************************************************************************
 * Description:
 *     Read the value of TCIP registers.
 * Argument:
 *     opaque  --  the pointer of the TCIP state.
 *     offset  --  the offset of the register which will be read.
 * Return:
 *     The value of the corresponding TCIP register.
 **************************************************************************/
static uint64_t csky_tcip_v1_read(void *opaque, hwaddr offset, unsigned size)
{
    csky_tcip_v1_state *s = (csky_tcip_v1_state *)opaque;
    uint64_t ret = 0;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_tcip_v1_read: 0x%x must word align read\n",
                      (int)offset);
    }

    switch (offset) {
    case 0x10: /* CoreTim CSR */
        ret = s->coret_csr;
        s->coret_csr &= ~CT_CSR_COUNTFLAG;
        csky_vic_v1_set_irq(s, coretim_irq_no, 0);
        break;
    case 0x14: /* CoreTim ReloadValue */
        ret = s->coret_rvr;
        break;
    case 0x18: /* CoreTim CurrentValue */
        if (s->coret_csr & CT_CSR_ENABLE) {
            ret = ptimer_get_count(s->timer);
        }
        break;
    case 0x1c:
        break;

    case 0x100: /*ISER*/
    case 0x180: /*ICER*/
        ret = s->vic_iser;
        break;
    case 0x140: /*IWER*/
    case 0x1c0: /*IWDR*/
        ret =  s->vic_iwer;
        break;
    case 0x240: /*ISSR*/
    case 0x2c0: /*ICSR*/
        if (s->env->features & ABIV2_TEE) {
            if (s->env->psr_s == 1 && s->env->psr_t == 1) {
                ret = s->env->intc_signals.issr;
            } else {
                ret = 0;
            }
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "csky_tcip_v1_read: Bad register offset 0x%x\n",
                          (int)offset);
        }
        break;
    case 0x200: /*ISPR*/
    case 0x280: /*ICPR*/
        ret = s->vic_ispr;
        break;
    case 0x300: /*IABR*/
        ret = s->env->intc_signals.iabr;
        break;
    case PR0 ... PR28: /*PR[32]*/
        ret =  ((s->vic_pr[offset - PR0] << 6)
                | (s->vic_pr[offset - PR0 + 1] << 14)
                | (s->vic_pr[offset - PR0 + 2] << 22)
                | (s->vic_pr[offset - PR0 + 3] << 30));
        break;
    case 0xc00: /* VIC_ISR */
        ret = s->env->intc_signals.isr;
        break;
    case 0xc04: /* VIC_IPTR */
        ret = s->env->intc_signals.iptr;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_tcip_v1_read: Bad register offset 0x%x\n",
                      (int)offset);
    }

    return ret;
}

/**************************************************************************
 * Description:
 *     Write the value to TCIP registers.
 * Argument:
 *     opaque  --  the pointer of the TCIP state.
 *     offset  --  the offset of the register.
 *     value   --  the value which will be writen.
 * Return:
 *     Void
 **************************************************************************/
static void csky_tcip_v1_write(void *opaque, hwaddr offset, uint64_t value,
                               unsigned size)
{
    csky_tcip_v1_state *s = (csky_tcip_v1_state *)opaque;
    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_tcip_v1_write: 0x%x must word align write\n",
                      (int)offset);
    }

    switch (offset) {
    case 0x10:  /* CoreTim CSR */
        s->coret_csr = (s->coret_csr & CT_CSR_COUNTFLAG) | (value & 0x7);

        ptimer_set_limit(s->timer, s->coret_rvr, s->coret_csr & CT_CSR_ENABLE);
        if (s->coret_csr & CT_CSR_ENABLE) {
            ptimer_run(s->timer, 0);
        }
        if ((s->coret_csr & CT_CSR_COUNTFLAG) &&
            (s->coret_csr & CT_CSR_TICKINT)) {
            s->vic_source |= 1 << coretim_irq_no;
        } else {
            s->vic_source &= ~(1 << coretim_irq_no);
        }
        break;
    case 0x14:  /* CoreTim ReloadValue */
        s->coret_rvr = value & 0x00ffffff;
        if (s->coret_rvr == 0) {
            ptimer_stop(s->timer);
        } else if (s->coret_csr & CT_CSR_ENABLE) {
            ptimer_set_limit(s->timer, s->coret_rvr, 0);
            ptimer_run(s->timer, 0);
        }
        break;
    case 0x18:  /* CoreTim CurrentValue */
        ptimer_set_limit(s->timer, s->coret_rvr, 1);
        s->coret_csr &= ~CT_CSR_COUNTFLAG;
        s->vic_source &= ~(1 << coretim_irq_no);
        break;
    case 0x1c:
        break;

    case 0x100: /* ISER */
        s->vic_iser |= value;
        break;
    case 0x140: /* IWER */
        s->vic_iwer |= value;
        break;
    case 0x180: /* ICER */
        s->vic_iser &= ~value;
        break;
    case 0x1c0: /* IWDR */
        s->vic_iwer &= ~value;
        break;
    case 0x200: /* ISPR */
        s->vic_ispr |= value;
        break;
    case 0x240: /* ISSR */
        if (s->env->features & ABIV2_TEE) {
            if (s->env->psr_s == 1 && s->env->psr_t == 1) {
                s->env->intc_signals.issr |= value;
            }
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "csky_tcip_v1_write: Bad register offset 0x%x\n",
                          (int)offset);
        }
        break;
    case 0x280: /* ICPR */
        s->vic_ispr &= ~value;
        break;
    case 0x2c0: /* ICSR */
        if (s->env->features & ABIV2_TEE) {
            if (s->env->psr_s == 1 && s->env->psr_t == 1) {
                s->env->intc_signals.issr &= ~value;
            }
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "csky_tcip_v1_write: Bad register offset 0x%x\n",
                          (int)offset);
        }
        break;
    case 0x300: /* IABR */
        /* Useguide says "if write 0 to iabr, it will clear the active state,
         * but write 1 to iabr, it maybe cause an unpredictable error",
         * so in qemu when write to iabr, it will clear iabr anyway. */
        s->env->intc_signals.iabr = 0;
        break;
    case PR0 ... PR28: /* PR[32] */ /* big endian */
        s->vic_pr[offset - PR0] = (value >> 6) & 0x3;
        s->vic_pr[offset - PR0 + 1] = (value >> 14) & 0x3;
        s->vic_pr[offset - PR0 + 2] = (value >> 22) & 0x3;
        s->vic_pr[offset - PR0 + 3] = (value >> 30) & 0x3;
        break;
    case 0xc00: /* ISR */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Attempt to write a read-only register ISR!\n");
        break;
    case 0xc04: /* IPTR */
        if (s->env->features & ABIV2_TEE) {
            if (s->env->psr_s == 1 && s->env->psr_t == 1) {
                s->env->intc_signals.iptr = value;
            }
        } else {
            s->env->intc_signals.iptr = value;
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_tcip_v1_write: Bad register offset 0x%x\n",
                      (int)offset);
        return;
    }
    csky_vic_v1_update(s);
}

static const MemoryRegionOps csky_tcip_v1_ops = {
    .read = csky_tcip_v1_read,
    .write = csky_tcip_v1_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void csky_tcip_v1_reset(DeviceState *d)
{
    csky_tcip_v1_state *s = CSKY_TCIP_V1(d);

    s->coret_csr = CT_CSR_INTERNAL_CLK;
    s->vic_iser = 0;
    s->vic_iwer = 0;
    s->vic_source = 0;
    s->vic_ispr = 0;
    s->env->intc_signals.iabr = 0;
    s->env->intc_signals.isr = 0;
    s->env->intc_signals.iptr = 0;
    s->env->intc_signals.issr = 0;
    csky_vic_v1_update(s);
}

/**************************************************************************
* Description:
*     Interrupt handler: generate signals to cpu.
* Argument:
*     opaque  --  the pointer to CPU state.
*     irq     --  vector number of interrupt.
*     level   --  contains some important information to help
*                 handle the interrupt.
* Return:
*     void
**************************************************************************/
static void csky_vic_v1_cpu_handler(void *opaque, int irq, int level)
{
    CPUCSKYState *env = (CPUCSKYState *)opaque;
    CPUState *cs = CPU(csky_env_get_cpu(env));

    env->intc_signals.vec_b = level & VIC_ISR_VEC;
    env->intc_signals.avec_b = 0;
    env->intc_signals.fint_b = 0;
    env->intc_signals.int_b = (level & VIC_ISR_INT) >> 10;

    if (level & VIC_ISR_INT) {
        cpu_interrupt(cs, CPU_INTERRUPT_HARD);
    } else {
        cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
    }
}

/**************************************************************************
* Description:
*     Allocate irq for VIC and register the handler.
* Argument:
*     env  --  the pointer to CPU state.
* Return:
*     the pointer to the allocated irq
**************************************************************************/
qemu_irq *csky_vic_v1_init_cpu(CPUCSKYState *env, int coret_irq_num)
{
    coretim_irq_no = coret_irq_num;

    return qemu_allocate_irqs(csky_vic_v1_cpu_handler, env, 1);
}

/**************************************************************************
 * Description:
 *     Be called when current value of the timer reaches 0.
 * Argument:
 *     opaque  --  the pointer to TCIP state.
 * Return:
 *     void
 **************************************************************************/
static void csky_coretim_tick(void *opaque)
{
    csky_tcip_v1_state *s = (csky_tcip_v1_state *)opaque;

    ptimer_set_limit(s->timer, s->coret_rvr, 1);
    s->coret_csr |= CT_CSR_COUNTFLAG;
    if (s->coret_csr & CT_CSR_TICKINT) {
        csky_vic_v1_set_irq(s, coretim_irq_no, 1);
    }
}

/**************************************************************************
 * Description:
 *     Initial TCIP.
 * Argument:
 *     obj  --  the pointer to csky_tcip_v1_info.
 * Return:
 *     success or failure
 **************************************************************************/
static void csky_tcip_v1_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    csky_tcip_v1_state *s = CSKY_TCIP_V1(obj);
    CSKYCPU *cpu = CSKY_CPU(qemu_get_cpu(0));
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    QEMUBH *bh;

    if (cpu == NULL) {
        return;
    }

    s->coret_csr = CT_CSR_INTERNAL_CLK;
    s->env = &cpu->env;
    s->vic_iser = 0;
    s->vic_iwer = 0;
    s->vic_source = 0;
    s->vic_ispr = 0;
    s->env->intc_signals.iabr = 0;
    s->env->intc_signals.isr = 0;
    s->env->intc_signals.iptr = 0;
    s->env->intc_signals.issr = 0;

    /* CSKY VIC intialization */
    qdev_init_gpio_in(dev, csky_vic_v1_set_irq, 32);
    sysbus_init_irq(sbd, &s->irq);

    /* CSKY CoreTim intialization */
    bh = qemu_bh_new(csky_coretim_tick, s);
    s->timer = ptimer_init(bh, PTIMER_POLICY_DEFAULT);
    ptimer_set_freq(s->timer, coretim_freq);

    memory_region_init_io(&s->iomem, obj, &csky_tcip_v1_ops,
                          s, TYPE_CSKY_TCIP_V1, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription vmstate_tcip_v1 = {
    .name = TYPE_CSKY_TCIP_V1,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PTIMER(timer, csky_tcip_v1_state),
        VMSTATE_UINT32(coret_csr, csky_tcip_v1_state),
        VMSTATE_UINT32(coret_rvr, csky_tcip_v1_state),
        VMSTATE_UINT32(vic_iser, csky_tcip_v1_state),
        VMSTATE_UINT32(vic_iwer, csky_tcip_v1_state),
        VMSTATE_UINT32(vic_source, csky_tcip_v1_state),
        VMSTATE_UINT32(vic_ispr, csky_tcip_v1_state),
        VMSTATE_UINT32_ARRAY(vic_pr, csky_tcip_v1_state, 32),
        VMSTATE_END_OF_LIST()
    }
};

void csky_tcip_v1_set_freq(uint32_t freq)
{
    coretim_freq = freq;
}

static void csky_tcip_v1_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd  = &vmstate_tcip_v1;
    dc->reset = csky_tcip_v1_reset;
}

static const TypeInfo csky_tcip_v1_info = {
    .name          = TYPE_CSKY_TCIP_V1,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = csky_tcip_v1_init,
    .instance_size = sizeof(csky_tcip_v1_state),
    .class_init    = csky_tcip_v1_class_init,
};

static void csky_tcip_v1_register_types(void)
{
    type_register_static(&csky_tcip_v1_info);
}

type_init(csky_tcip_v1_register_types)

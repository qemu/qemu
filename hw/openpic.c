/*
 * OpenPIC emulation
 *
 * Copyright (c) 2004 Jocelyn Mayer
 *               2011 Alexander Graf
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
/*
 *
 * Based on OpenPic implementations:
 * - Intel GW80314 I/O companion chip developer's manual
 * - Motorola MPC8245 & MPC8540 user manuals.
 * - Motorola MCP750 (aka Raven) programmer manual.
 * - Motorola Harrier programmer manuel
 *
 * Serial interrupts, as implemented in Raven chipset are not supported yet.
 *
 */
#include "hw.h"
#include "ppc_mac.h"
#include "pci.h"
#include "openpic.h"
#include "sysbus.h"

//#define DEBUG_OPENPIC

#ifdef DEBUG_OPENPIC
#define DPRINTF(fmt, ...) do { printf(fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) do { } while (0)
#endif

#define MAX_CPU     15
#define MAX_SRC     256
#define MAX_TMR     4
#define VECTOR_BITS 8
#define MAX_IPI     4
#define MAX_IRQ     (MAX_SRC + MAX_IPI + MAX_TMR)
#define VID         0x03 /* MPIC version ID */

/* OpenPIC capability flags */
#define OPENPIC_FLAG_IDE_CRIT     (1 << 0)

/* OpenPIC address map */
#define OPENPIC_GLB_REG_START        0x0
#define OPENPIC_GLB_REG_SIZE         0x10F0
#define OPENPIC_TMR_REG_START        0x10F0
#define OPENPIC_TMR_REG_SIZE         0x220
#define OPENPIC_SRC_REG_START        0x10000
#define OPENPIC_SRC_REG_SIZE         (MAX_SRC * 0x20)
#define OPENPIC_CPU_REG_START        0x20000
#define OPENPIC_CPU_REG_SIZE         0x100 + ((MAX_CPU - 1) * 0x1000)

/* Raven */
#define RAVEN_MAX_CPU      2
#define RAVEN_MAX_EXT     48
#define RAVEN_MAX_IRQ     64
#define RAVEN_MAX_TMR      MAX_TMR
#define RAVEN_MAX_IPI      MAX_IPI

/* Interrupt definitions */
#define RAVEN_FE_IRQ     (RAVEN_MAX_EXT)     /* Internal functional IRQ */
#define RAVEN_ERR_IRQ    (RAVEN_MAX_EXT + 1) /* Error IRQ */
#define RAVEN_TMR_IRQ    (RAVEN_MAX_EXT + 2) /* First timer IRQ */
#define RAVEN_IPI_IRQ    (RAVEN_TMR_IRQ + RAVEN_MAX_TMR) /* First IPI IRQ */
/* First doorbell IRQ */
#define RAVEN_DBL_IRQ    (RAVEN_IPI_IRQ + (RAVEN_MAX_CPU * RAVEN_MAX_IPI))

/* FSL_MPIC_20 */
#define FSL_MPIC_20_MAX_CPU      1
#define FSL_MPIC_20_MAX_EXT     12
#define FSL_MPIC_20_MAX_INT     64
#define FSL_MPIC_20_MAX_IRQ     MAX_IRQ

/* Interrupt definitions */
/* IRQs, accessible through the IRQ region */
#define FSL_MPIC_20_EXT_IRQ      0x00
#define FSL_MPIC_20_INT_IRQ      0x10
#define FSL_MPIC_20_MSG_IRQ      0xb0
#define FSL_MPIC_20_MSI_IRQ      0xe0
/* These are available through separate regions, but
   for simplicity's sake mapped into the same number space */
#define FSL_MPIC_20_TMR_IRQ      0x100
#define FSL_MPIC_20_IPI_IRQ      0x104

/*
 * Block Revision Register1 (BRR1): QEMU does not fully emulate
 * any version on MPIC. So to start with, set the IP version to 0.
 *
 * NOTE: This is Freescale MPIC specific register. Keep it here till
 * this code is refactored for different variants of OPENPIC and MPIC.
 */
#define FSL_BRR1_IPID (0x0040 << 16) /* 16 bit IP-block ID */
#define FSL_BRR1_IPMJ (0x00 << 8) /* 8 bit IP major number */
#define FSL_BRR1_IPMN 0x00 /* 8 bit IP minor number */

#define FREP_NIRQ_SHIFT   16
#define FREP_NCPU_SHIFT    8
#define FREP_VID_SHIFT     0

#define VID_REVISION_1_2   2
#define VID_REVISION_1_3   3

#define VENI_GENERIC      0x00000000 /* Generic Vendor ID */

#define IDR_EP_SHIFT      31
#define IDR_EP_MASK       (1 << IDR_EP_SHIFT)
#define IDR_CI0_SHIFT     30
#define IDR_CI1_SHIFT     29
#define IDR_P1_SHIFT      1
#define IDR_P0_SHIFT      0

#define BF_WIDTH(_bits_) \
(((_bits_) + (sizeof(uint32_t) * 8) - 1) / (sizeof(uint32_t) * 8))

static inline void set_bit (uint32_t *field, int bit)
{
    field[bit >> 5] |= 1 << (bit & 0x1F);
}

static inline void reset_bit (uint32_t *field, int bit)
{
    field[bit >> 5] &= ~(1 << (bit & 0x1F));
}

static inline int test_bit (uint32_t *field, int bit)
{
    return (field[bit >> 5] & 1 << (bit & 0x1F)) != 0;
}

static int get_current_cpu(void)
{
  return cpu_single_env->cpu_index;
}

static uint32_t openpic_cpu_read_internal(void *opaque, hwaddr addr,
                                          int idx);
static void openpic_cpu_write_internal(void *opaque, hwaddr addr,
                                       uint32_t val, int idx);

typedef struct IRQ_queue_t {
    uint32_t queue[BF_WIDTH(MAX_IRQ)];
    int next;
    int priority;
} IRQ_queue_t;

typedef struct IRQ_src_t {
    uint32_t ipvp;  /* IRQ vector/priority register */
    uint32_t ide;   /* IRQ destination register */
    int last_cpu;
    int pending;    /* TRUE if IRQ is pending */
} IRQ_src_t;

#define IPVP_MASK_SHIFT       31
#define IPVP_MASK_MASK        (1 << IPVP_MASK_SHIFT)
#define IPVP_ACTIVITY_SHIFT   30
#define IPVP_ACTIVITY_MASK    (1 << IPVP_ACTIVITY_SHIFT)
#define IPVP_MODE_SHIFT       29
#define IPVP_MODE_MASK        (1 << IPVP_MODE_SHIFT)
#define IPVP_POLARITY_SHIFT   23
#define IPVP_POLARITY_MASK    (1 << IPVP_POLARITY_SHIFT)
#define IPVP_SENSE_SHIFT      22
#define IPVP_SENSE_MASK       (1 << IPVP_SENSE_SHIFT)

#define IPVP_PRIORITY_MASK     (0x1F << 16)
#define IPVP_PRIORITY(_ipvpr_) ((int)(((_ipvpr_) & IPVP_PRIORITY_MASK) >> 16))
#define IPVP_VECTOR_MASK       ((1 << VECTOR_BITS) - 1)
#define IPVP_VECTOR(_ipvpr_)   ((_ipvpr_) & IPVP_VECTOR_MASK)

typedef struct IRQ_dst_t {
    uint32_t pctp; /* CPU current task priority */
    uint32_t pcsr; /* CPU sensitivity register */
    IRQ_queue_t raised;
    IRQ_queue_t servicing;
    qemu_irq *irqs;
} IRQ_dst_t;

typedef struct OpenPICState {
    SysBusDevice busdev;
    MemoryRegion mem;

    /* Behavior control */
    uint32_t model;
    uint32_t flags;
    uint32_t nb_irqs;
    uint32_t vid;
    uint32_t veni; /* Vendor identification register */
    uint32_t spve_mask;
    uint32_t tifr_reset;
    uint32_t ipvp_reset;
    uint32_t ide_reset;
    uint32_t brr1;

    /* Sub-regions */
    MemoryRegion sub_io_mem[7];

    /* Global registers */
    uint32_t frep; /* Feature reporting register */
    uint32_t glbc; /* Global configuration register  */
    uint32_t pint; /* Processor initialization register */
    uint32_t spve; /* Spurious vector register */
    uint32_t tifr; /* Timer frequency reporting register */
    /* Source registers */
    IRQ_src_t src[MAX_IRQ];
    /* Local registers per output pin */
    IRQ_dst_t dst[MAX_CPU];
    uint32_t nb_cpus;
    /* Timer registers */
    struct {
        uint32_t ticc;  /* Global timer current count register */
        uint32_t tibc;  /* Global timer base count register */
    } timers[MAX_TMR];
    uint32_t max_irq;
    uint32_t irq_ipi0;
    uint32_t irq_tim0;
} OpenPICState;

static void openpic_irq_raise(OpenPICState *opp, int n_CPU, IRQ_src_t *src);

static inline void IRQ_setbit (IRQ_queue_t *q, int n_IRQ)
{
    set_bit(q->queue, n_IRQ);
}

static inline void IRQ_resetbit (IRQ_queue_t *q, int n_IRQ)
{
    reset_bit(q->queue, n_IRQ);
}

static inline int IRQ_testbit (IRQ_queue_t *q, int n_IRQ)
{
    return test_bit(q->queue, n_IRQ);
}

static void IRQ_check(OpenPICState *opp, IRQ_queue_t *q)
{
    int next, i;
    int priority;

    next = -1;
    priority = -1;
    for (i = 0; i < opp->max_irq; i++) {
        if (IRQ_testbit(q, i)) {
            DPRINTF("IRQ_check: irq %d set ipvp_pr=%d pr=%d\n",
                    i, IPVP_PRIORITY(opp->src[i].ipvp), priority);
            if (IPVP_PRIORITY(opp->src[i].ipvp) > priority) {
                next = i;
                priority = IPVP_PRIORITY(opp->src[i].ipvp);
            }
        }
    }
    q->next = next;
    q->priority = priority;
}

static int IRQ_get_next(OpenPICState *opp, IRQ_queue_t *q)
{
    if (q->next == -1) {
        /* XXX: optimize */
        IRQ_check(opp, q);
    }

    return q->next;
}

static void IRQ_local_pipe(OpenPICState *opp, int n_CPU, int n_IRQ)
{
    IRQ_dst_t *dst;
    IRQ_src_t *src;
    int priority;

    dst = &opp->dst[n_CPU];
    src = &opp->src[n_IRQ];
    priority = IPVP_PRIORITY(src->ipvp);
    if (priority <= dst->pctp) {
        /* Too low priority */
        DPRINTF("%s: IRQ %d has too low priority on CPU %d\n",
                __func__, n_IRQ, n_CPU);
        return;
    }
    if (IRQ_testbit(&dst->raised, n_IRQ)) {
        /* Interrupt miss */
        DPRINTF("%s: IRQ %d was missed on CPU %d\n",
                __func__, n_IRQ, n_CPU);
        return;
    }
    src->ipvp |= IPVP_ACTIVITY_MASK;
    IRQ_setbit(&dst->raised, n_IRQ);
    if (priority < dst->raised.priority) {
        /* An higher priority IRQ is already raised */
        DPRINTF("%s: IRQ %d is hidden by raised IRQ %d on CPU %d\n",
                __func__, n_IRQ, dst->raised.next, n_CPU);
        return;
    }
    IRQ_get_next(opp, &dst->raised);
    if (IRQ_get_next(opp, &dst->servicing) != -1 &&
        priority <= dst->servicing.priority) {
        DPRINTF("%s: IRQ %d is hidden by servicing IRQ %d on CPU %d\n",
                __func__, n_IRQ, dst->servicing.next, n_CPU);
        /* Already servicing a higher priority IRQ */
        return;
    }
    DPRINTF("Raise OpenPIC INT output cpu %d irq %d\n", n_CPU, n_IRQ);
    openpic_irq_raise(opp, n_CPU, src);
}

/* update pic state because registers for n_IRQ have changed value */
static void openpic_update_irq(OpenPICState *opp, int n_IRQ)
{
    IRQ_src_t *src;
    int i;

    src = &opp->src[n_IRQ];

    if (!src->pending) {
        /* no irq pending */
        DPRINTF("%s: IRQ %d is not pending\n", __func__, n_IRQ);
        return;
    }
    if (src->ipvp & IPVP_MASK_MASK) {
        /* Interrupt source is disabled */
        DPRINTF("%s: IRQ %d is disabled\n", __func__, n_IRQ);
        return;
    }
    if (IPVP_PRIORITY(src->ipvp) == 0) {
        /* Priority set to zero */
        DPRINTF("%s: IRQ %d has 0 priority\n", __func__, n_IRQ);
        return;
    }
    if (src->ipvp & IPVP_ACTIVITY_MASK) {
        /* IRQ already active */
        DPRINTF("%s: IRQ %d is already active\n", __func__, n_IRQ);
        return;
    }
    if (src->ide == 0x00000000) {
        /* No target */
        DPRINTF("%s: IRQ %d has no target\n", __func__, n_IRQ);
        return;
    }

    if (src->ide == (1 << src->last_cpu)) {
        /* Only one CPU is allowed to receive this IRQ */
        IRQ_local_pipe(opp, src->last_cpu, n_IRQ);
    } else if (!(src->ipvp & IPVP_MODE_MASK)) {
        /* Directed delivery mode */
        for (i = 0; i < opp->nb_cpus; i++) {
            if (src->ide & (1 << i)) {
                IRQ_local_pipe(opp, i, n_IRQ);
            }
        }
    } else {
        /* Distributed delivery mode */
        for (i = src->last_cpu + 1; i != src->last_cpu; i++) {
            if (i == opp->nb_cpus)
                i = 0;
            if (src->ide & (1 << i)) {
                IRQ_local_pipe(opp, i, n_IRQ);
                src->last_cpu = i;
                break;
            }
        }
    }
}

static void openpic_set_irq(void *opaque, int n_IRQ, int level)
{
    OpenPICState *opp = opaque;
    IRQ_src_t *src;

    src = &opp->src[n_IRQ];
    DPRINTF("openpic: set irq %d = %d ipvp=%08x\n",
            n_IRQ, level, src->ipvp);
    if (src->ipvp & IPVP_SENSE_MASK) {
        /* level-sensitive irq */
        src->pending = level;
        if (!level) {
            src->ipvp &= ~IPVP_ACTIVITY_MASK;
        }
    } else {
        /* edge-sensitive irq */
        if (level)
            src->pending = 1;
    }
    openpic_update_irq(opp, n_IRQ);
}

static void openpic_reset(DeviceState *d)
{
    OpenPICState *opp = FROM_SYSBUS(typeof (*opp), sysbus_from_qdev(d));
    int i;

    opp->glbc = 0x80000000;
    /* Initialise controller registers */
    opp->frep = ((opp->nb_irqs -1) << FREP_NIRQ_SHIFT) |
                ((opp->nb_cpus -1) << FREP_NCPU_SHIFT) |
                (opp->vid << FREP_VID_SHIFT);

    opp->pint = 0x00000000;
    opp->spve = -1 & opp->spve_mask;
    opp->tifr = opp->tifr_reset;
    /* Initialise IRQ sources */
    for (i = 0; i < opp->max_irq; i++) {
        opp->src[i].ipvp = opp->ipvp_reset;
        opp->src[i].ide  = opp->ide_reset;
    }
    /* Initialise IRQ destinations */
    for (i = 0; i < MAX_CPU; i++) {
        opp->dst[i].pctp      = 0x0000000F;
        opp->dst[i].pcsr      = 0x00000000;
        memset(&opp->dst[i].raised, 0, sizeof(IRQ_queue_t));
        opp->dst[i].raised.next = -1;
        memset(&opp->dst[i].servicing, 0, sizeof(IRQ_queue_t));
        opp->dst[i].servicing.next = -1;
    }
    /* Initialise timers */
    for (i = 0; i < MAX_TMR; i++) {
        opp->timers[i].ticc = 0x00000000;
        opp->timers[i].tibc = 0x80000000;
    }
    /* Go out of RESET state */
    opp->glbc = 0x00000000;
}

static inline uint32_t read_IRQreg_ide(OpenPICState *opp, int n_IRQ)
{
    return opp->src[n_IRQ].ide;
}

static inline uint32_t read_IRQreg_ipvp(OpenPICState *opp, int n_IRQ)
{
    return opp->src[n_IRQ].ipvp;
}

static inline void write_IRQreg_ide(OpenPICState *opp, int n_IRQ, uint32_t val)
{
    uint32_t tmp;

    tmp = val & 0xC0000000;
    tmp |= val & ((1ULL << MAX_CPU) - 1);
    opp->src[n_IRQ].ide = tmp;
    DPRINTF("Set IDE %d to 0x%08x\n", n_IRQ, opp->src[n_IRQ].ide);
}

static inline void write_IRQreg_ipvp(OpenPICState *opp, int n_IRQ, uint32_t val)
{
    /* NOTE: not fully accurate for special IRQs, but simple and sufficient */
    /* ACTIVITY bit is read-only */
    opp->src[n_IRQ].ipvp = (opp->src[n_IRQ].ipvp & 0x40000000)
                         | (val & 0x800F00FF);
    openpic_update_irq(opp, n_IRQ);
    DPRINTF("Set IPVP %d to 0x%08x -> 0x%08x\n", n_IRQ, val,
            opp->src[n_IRQ].ipvp);
}

static void openpic_gbl_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned len)
{
    OpenPICState *opp = opaque;
    IRQ_dst_t *dst;
    int idx;

    DPRINTF("%s: addr " TARGET_FMT_plx " <= %08x\n", __func__, addr, val);
    if (addr & 0xF)
        return;
    switch (addr) {
    case 0x00: /* Block Revision Register1 (BRR1) is Readonly */
        break;
    case 0x40:
    case 0x50:
    case 0x60:
    case 0x70:
    case 0x80:
    case 0x90:
    case 0xA0:
    case 0xB0:
        openpic_cpu_write_internal(opp, addr, val, get_current_cpu());
        break;
    case 0x1000: /* FREP */
        break;
    case 0x1020: /* GLBC */
        if (val & 0x80000000) {
            openpic_reset(&opp->busdev.qdev);
        }
        break;
    case 0x1080: /* VENI */
        break;
    case 0x1090: /* PINT */
        for (idx = 0; idx < opp->nb_cpus; idx++) {
            if ((val & (1 << idx)) && !(opp->pint & (1 << idx))) {
                DPRINTF("Raise OpenPIC RESET output for CPU %d\n", idx);
                dst = &opp->dst[idx];
                qemu_irq_raise(dst->irqs[OPENPIC_OUTPUT_RESET]);
            } else if (!(val & (1 << idx)) && (opp->pint & (1 << idx))) {
                DPRINTF("Lower OpenPIC RESET output for CPU %d\n", idx);
                dst = &opp->dst[idx];
                qemu_irq_lower(dst->irqs[OPENPIC_OUTPUT_RESET]);
            }
        }
        opp->pint = val;
        break;
    case 0x10A0: /* IPI_IPVP */
    case 0x10B0:
    case 0x10C0:
    case 0x10D0:
        {
            int idx;
            idx = (addr - 0x10A0) >> 4;
            write_IRQreg_ipvp(opp, opp->irq_ipi0 + idx, val);
        }
        break;
    case 0x10E0: /* SPVE */
        opp->spve = val & opp->spve_mask;
        break;
    default:
        break;
    }
}

static uint64_t openpic_gbl_read(void *opaque, hwaddr addr, unsigned len)
{
    OpenPICState *opp = opaque;
    uint32_t retval;

    DPRINTF("%s: addr " TARGET_FMT_plx "\n", __func__, addr);
    retval = 0xFFFFFFFF;
    if (addr & 0xF)
        return retval;
    switch (addr) {
    case 0x1000: /* FREP */
        retval = opp->frep;
        break;
    case 0x1020: /* GLBC */
        retval = opp->glbc;
        break;
    case 0x1080: /* VENI */
        retval = opp->veni;
        break;
    case 0x1090: /* PINT */
        retval = 0x00000000;
        break;
    case 0x00: /* Block Revision Register1 (BRR1) */
    case 0x40:
    case 0x50:
    case 0x60:
    case 0x70:
    case 0x80:
    case 0x90:
    case 0xA0:
    case 0xB0:
        retval = openpic_cpu_read_internal(opp, addr, get_current_cpu());
        break;
    case 0x10A0: /* IPI_IPVP */
    case 0x10B0:
    case 0x10C0:
    case 0x10D0:
        {
            int idx;
            idx = (addr - 0x10A0) >> 4;
            retval = read_IRQreg_ipvp(opp, opp->irq_ipi0 + idx);
        }
        break;
    case 0x10E0: /* SPVE */
        retval = opp->spve;
        break;
    default:
        break;
    }
    DPRINTF("%s: => %08x\n", __func__, retval);

    return retval;
}

static void openpic_tmr_write(void *opaque, hwaddr addr, uint64_t val,
                                unsigned len)
{
    OpenPICState *opp = opaque;
    int idx;

    DPRINTF("%s: addr %08x <= %08x\n", __func__, addr, val);
    if (addr & 0xF)
        return;
    idx = (addr >> 6) & 0x3;
    addr = addr & 0x30;

    if (addr == 0x0) {
        /* TIFR (TFRR) */
        opp->tifr = val;
        return;
    }
    switch (addr & 0x30) {
    case 0x00: /* TICC (GTCCR) */
        break;
    case 0x10: /* TIBC (GTBCR) */
        if ((opp->timers[idx].ticc & 0x80000000) != 0 &&
            (val & 0x80000000) == 0 &&
            (opp->timers[idx].tibc & 0x80000000) != 0)
            opp->timers[idx].ticc &= ~0x80000000;
        opp->timers[idx].tibc = val;
        break;
    case 0x20: /* TIVP (GTIVPR) */
        write_IRQreg_ipvp(opp, opp->irq_tim0 + idx, val);
        break;
    case 0x30: /* TIDE (GTIDR) */
        write_IRQreg_ide(opp, opp->irq_tim0 + idx, val);
        break;
    }
}

static uint64_t openpic_tmr_read(void *opaque, hwaddr addr, unsigned len)
{
    OpenPICState *opp = opaque;
    uint32_t retval = -1;
    int idx;

    DPRINTF("%s: addr %08x\n", __func__, addr);
    if (addr & 0xF) {
        goto out;
    }
    idx = (addr >> 6) & 0x3;
    if (addr == 0x0) {
        /* TIFR (TFRR) */
        retval = opp->tifr;
        goto out;
    }
    switch (addr & 0x30) {
    case 0x00: /* TICC (GTCCR) */
        retval = opp->timers[idx].ticc;
        break;
    case 0x10: /* TIBC (GTBCR) */
        retval = opp->timers[idx].tibc;
        break;
    case 0x20: /* TIPV (TIPV) */
        retval = read_IRQreg_ipvp(opp, opp->irq_tim0 + idx);
        break;
    case 0x30: /* TIDE (TIDR) */
        retval = read_IRQreg_ide(opp, opp->irq_tim0 + idx);
        break;
    }

out:
    DPRINTF("%s: => %08x\n", __func__, retval);

    return retval;
}

static void openpic_src_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned len)
{
    OpenPICState *opp = opaque;
    int idx;

    DPRINTF("%s: addr %08x <= %08x\n", __func__, addr, val);
    if (addr & 0xF)
        return;
    addr = addr & 0xFFF0;
    idx = addr >> 5;
    if (addr & 0x10) {
        /* EXDE / IFEDE / IEEDE */
        write_IRQreg_ide(opp, idx, val);
    } else {
        /* EXVP / IFEVP / IEEVP */
        write_IRQreg_ipvp(opp, idx, val);
    }
}

static uint64_t openpic_src_read(void *opaque, uint64_t addr, unsigned len)
{
    OpenPICState *opp = opaque;
    uint32_t retval;
    int idx;

    DPRINTF("%s: addr %08x\n", __func__, addr);
    retval = 0xFFFFFFFF;
    if (addr & 0xF)
        return retval;
    addr = addr & 0xFFF0;
    idx = addr >> 5;
    if (addr & 0x10) {
        /* EXDE / IFEDE / IEEDE */
        retval = read_IRQreg_ide(opp, idx);
    } else {
        /* EXVP / IFEVP / IEEVP */
        retval = read_IRQreg_ipvp(opp, idx);
    }
    DPRINTF("%s: => %08x\n", __func__, retval);

    return retval;
}

static void openpic_cpu_write_internal(void *opaque, hwaddr addr,
                                       uint32_t val, int idx)
{
    OpenPICState *opp = opaque;
    IRQ_src_t *src;
    IRQ_dst_t *dst;
    int s_IRQ, n_IRQ;

    DPRINTF("%s: cpu %d addr " TARGET_FMT_plx " <= %08x\n", __func__, idx,
            addr, val);
    if (addr & 0xF)
        return;
    dst = &opp->dst[idx];
    addr &= 0xFF0;
    switch (addr) {
    case 0x40: /* IPIDR */
    case 0x50:
    case 0x60:
    case 0x70:
        idx = (addr - 0x40) >> 4;
        /* we use IDE as mask which CPUs to deliver the IPI to still. */
        write_IRQreg_ide(opp, opp->irq_ipi0 + idx,
                         opp->src[opp->irq_ipi0 + idx].ide | val);
        openpic_set_irq(opp, opp->irq_ipi0 + idx, 1);
        openpic_set_irq(opp, opp->irq_ipi0 + idx, 0);
        break;
    case 0x80: /* PCTP */
        dst->pctp = val & 0x0000000F;
        break;
    case 0x90: /* WHOAMI */
        /* Read-only register */
        break;
    case 0xA0: /* PIAC */
        /* Read-only register */
        break;
    case 0xB0: /* PEOI */
        DPRINTF("PEOI\n");
        s_IRQ = IRQ_get_next(opp, &dst->servicing);
        IRQ_resetbit(&dst->servicing, s_IRQ);
        dst->servicing.next = -1;
        /* Set up next servicing IRQ */
        s_IRQ = IRQ_get_next(opp, &dst->servicing);
        /* Check queued interrupts. */
        n_IRQ = IRQ_get_next(opp, &dst->raised);
        src = &opp->src[n_IRQ];
        if (n_IRQ != -1 &&
            (s_IRQ == -1 ||
             IPVP_PRIORITY(src->ipvp) > dst->servicing.priority)) {
            DPRINTF("Raise OpenPIC INT output cpu %d irq %d\n",
                    idx, n_IRQ);
            openpic_irq_raise(opp, idx, src);
        }
        break;
    default:
        break;
    }
}

static void openpic_cpu_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned len)
{
    openpic_cpu_write_internal(opaque, addr, val, (addr & 0x1f000) >> 12);
}

static uint32_t openpic_cpu_read_internal(void *opaque, hwaddr addr,
                                          int idx)
{
    OpenPICState *opp = opaque;
    IRQ_src_t *src;
    IRQ_dst_t *dst;
    uint32_t retval;
    int n_IRQ;

    DPRINTF("%s: cpu %d addr " TARGET_FMT_plx "\n", __func__, idx, addr);
    retval = 0xFFFFFFFF;
    if (addr & 0xF)
        return retval;
    dst = &opp->dst[idx];
    addr &= 0xFF0;
    switch (addr) {
    case 0x00: /* Block Revision Register1 (BRR1) */
        retval = opp->brr1;
        break;
    case 0x80: /* PCTP */
        retval = dst->pctp;
        break;
    case 0x90: /* WHOAMI */
        retval = idx;
        break;
    case 0xA0: /* PIAC */
        DPRINTF("Lower OpenPIC INT output\n");
        qemu_irq_lower(dst->irqs[OPENPIC_OUTPUT_INT]);
        n_IRQ = IRQ_get_next(opp, &dst->raised);
        DPRINTF("PIAC: irq=%d\n", n_IRQ);
        if (n_IRQ == -1) {
            /* No more interrupt pending */
            retval = IPVP_VECTOR(opp->spve);
        } else {
            src = &opp->src[n_IRQ];
            if (!(src->ipvp & IPVP_ACTIVITY_MASK) ||
                !(IPVP_PRIORITY(src->ipvp) > dst->pctp)) {
                /* - Spurious level-sensitive IRQ
                 * - Priorities has been changed
                 *   and the pending IRQ isn't allowed anymore
                 */
                src->ipvp &= ~IPVP_ACTIVITY_MASK;
                retval = IPVP_VECTOR(opp->spve);
            } else {
                /* IRQ enter servicing state */
                IRQ_setbit(&dst->servicing, n_IRQ);
                retval = IPVP_VECTOR(src->ipvp);
            }
            IRQ_resetbit(&dst->raised, n_IRQ);
            dst->raised.next = -1;
            if (!(src->ipvp & IPVP_SENSE_MASK)) {
                /* edge-sensitive IRQ */
                src->ipvp &= ~IPVP_ACTIVITY_MASK;
                src->pending = 0;
            }

            if ((n_IRQ >= opp->irq_ipi0) &&  (n_IRQ < (opp->irq_ipi0 + MAX_IPI))) {
                src->ide &= ~(1 << idx);
                if (src->ide && !(src->ipvp & IPVP_SENSE_MASK)) {
                    /* trigger on CPUs that didn't know about it yet */
                    openpic_set_irq(opp, n_IRQ, 1);
                    openpic_set_irq(opp, n_IRQ, 0);
                    /* if all CPUs knew about it, set active bit again */
                    src->ipvp |= IPVP_ACTIVITY_MASK;
                }
            }
        }
        break;
    case 0xB0: /* PEOI */
        retval = 0;
        break;
    default:
        break;
    }
    DPRINTF("%s: => %08x\n", __func__, retval);

    return retval;
}

static uint64_t openpic_cpu_read(void *opaque, hwaddr addr, unsigned len)
{
    return openpic_cpu_read_internal(opaque, addr, (addr & 0x1f000) >> 12);
}

static const MemoryRegionOps openpic_glb_ops_le = {
    .write = openpic_gbl_write,
    .read  = openpic_gbl_read,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps openpic_glb_ops_be = {
    .write = openpic_gbl_write,
    .read  = openpic_gbl_read,
    .endianness = DEVICE_BIG_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps openpic_tmr_ops_le = {
    .write = openpic_tmr_write,
    .read  = openpic_tmr_read,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps openpic_tmr_ops_be = {
    .write = openpic_tmr_write,
    .read  = openpic_tmr_read,
    .endianness = DEVICE_BIG_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps openpic_cpu_ops_le = {
    .write = openpic_cpu_write,
    .read  = openpic_cpu_read,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps openpic_cpu_ops_be = {
    .write = openpic_cpu_write,
    .read  = openpic_cpu_read,
    .endianness = DEVICE_BIG_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps openpic_src_ops_le = {
    .write = openpic_src_write,
    .read  = openpic_src_read,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps openpic_src_ops_be = {
    .write = openpic_src_write,
    .read  = openpic_src_read,
    .endianness = DEVICE_BIG_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void openpic_save_IRQ_queue(QEMUFile* f, IRQ_queue_t *q)
{
    unsigned int i;

    for (i = 0; i < BF_WIDTH(MAX_IRQ); i++)
        qemu_put_be32s(f, &q->queue[i]);

    qemu_put_sbe32s(f, &q->next);
    qemu_put_sbe32s(f, &q->priority);
}

static void openpic_save(QEMUFile* f, void *opaque)
{
    OpenPICState *opp = (OpenPICState *)opaque;
    unsigned int i;

    qemu_put_be32s(f, &opp->glbc);
    qemu_put_be32s(f, &opp->veni);
    qemu_put_be32s(f, &opp->pint);
    qemu_put_be32s(f, &opp->spve);
    qemu_put_be32s(f, &opp->tifr);

    for (i = 0; i < opp->max_irq; i++) {
        qemu_put_be32s(f, &opp->src[i].ipvp);
        qemu_put_be32s(f, &opp->src[i].ide);
        qemu_put_sbe32s(f, &opp->src[i].last_cpu);
        qemu_put_sbe32s(f, &opp->src[i].pending);
    }

    qemu_put_be32s(f, &opp->nb_cpus);

    for (i = 0; i < opp->nb_cpus; i++) {
        qemu_put_be32s(f, &opp->dst[i].pctp);
        qemu_put_be32s(f, &opp->dst[i].pcsr);
        openpic_save_IRQ_queue(f, &opp->dst[i].raised);
        openpic_save_IRQ_queue(f, &opp->dst[i].servicing);
    }

    for (i = 0; i < MAX_TMR; i++) {
        qemu_put_be32s(f, &opp->timers[i].ticc);
        qemu_put_be32s(f, &opp->timers[i].tibc);
    }
}

static void openpic_load_IRQ_queue(QEMUFile* f, IRQ_queue_t *q)
{
    unsigned int i;

    for (i = 0; i < BF_WIDTH(MAX_IRQ); i++)
        qemu_get_be32s(f, &q->queue[i]);

    qemu_get_sbe32s(f, &q->next);
    qemu_get_sbe32s(f, &q->priority);
}

static int openpic_load(QEMUFile* f, void *opaque, int version_id)
{
    OpenPICState *opp = (OpenPICState *)opaque;
    unsigned int i;

    if (version_id != 1)
        return -EINVAL;

    qemu_get_be32s(f, &opp->glbc);
    qemu_get_be32s(f, &opp->veni);
    qemu_get_be32s(f, &opp->pint);
    qemu_get_be32s(f, &opp->spve);
    qemu_get_be32s(f, &opp->tifr);

    for (i = 0; i < opp->max_irq; i++) {
        qemu_get_be32s(f, &opp->src[i].ipvp);
        qemu_get_be32s(f, &opp->src[i].ide);
        qemu_get_sbe32s(f, &opp->src[i].last_cpu);
        qemu_get_sbe32s(f, &opp->src[i].pending);
    }

    qemu_get_be32s(f, &opp->nb_cpus);

    for (i = 0; i < opp->nb_cpus; i++) {
        qemu_get_be32s(f, &opp->dst[i].pctp);
        qemu_get_be32s(f, &opp->dst[i].pcsr);
        openpic_load_IRQ_queue(f, &opp->dst[i].raised);
        openpic_load_IRQ_queue(f, &opp->dst[i].servicing);
    }

    for (i = 0; i < MAX_TMR; i++) {
        qemu_get_be32s(f, &opp->timers[i].ticc);
        qemu_get_be32s(f, &opp->timers[i].tibc);
    }

    return 0;
}

static void openpic_irq_raise(OpenPICState *opp, int n_CPU, IRQ_src_t *src)
{
    int n_ci = IDR_CI0_SHIFT - n_CPU;

    if ((opp->flags & OPENPIC_FLAG_IDE_CRIT) && (src->ide & (1 << n_ci))) {
        qemu_irq_raise(opp->dst[n_CPU].irqs[OPENPIC_OUTPUT_CINT]);
    } else {
        qemu_irq_raise(opp->dst[n_CPU].irqs[OPENPIC_OUTPUT_INT]);
    }
}

struct memreg {
    const char             *name;
    MemoryRegionOps const  *ops;
    hwaddr      start_addr;
    ram_addr_t              size;
};

static int openpic_init(SysBusDevice *dev)
{
    OpenPICState *opp = FROM_SYSBUS(typeof (*opp), dev);
    int i, j;
    const struct memreg list_le[] = {
        {"glb", &openpic_glb_ops_le, OPENPIC_GLB_REG_START,
                                     OPENPIC_GLB_REG_SIZE},
        {"tmr", &openpic_tmr_ops_le, OPENPIC_TMR_REG_START,
                                     OPENPIC_TMR_REG_SIZE},
        {"src", &openpic_src_ops_le, OPENPIC_SRC_REG_START,
                                     OPENPIC_SRC_REG_SIZE},
        {"cpu", &openpic_cpu_ops_le, OPENPIC_CPU_REG_START,
                                     OPENPIC_CPU_REG_SIZE},
    };
    const struct memreg list_be[] = {
        {"glb", &openpic_glb_ops_be, OPENPIC_GLB_REG_START,
                                     OPENPIC_GLB_REG_SIZE},
        {"tmr", &openpic_tmr_ops_be, OPENPIC_TMR_REG_START,
                                     OPENPIC_TMR_REG_SIZE},
        {"src", &openpic_src_ops_be, OPENPIC_SRC_REG_START,
                                     OPENPIC_SRC_REG_SIZE},
        {"cpu", &openpic_cpu_ops_be, OPENPIC_CPU_REG_START,
                                     OPENPIC_CPU_REG_SIZE},
    };
    struct memreg const *list;

    switch (opp->model) {
    case OPENPIC_MODEL_FSL_MPIC_20:
    default:
        opp->flags |= OPENPIC_FLAG_IDE_CRIT;
        opp->nb_irqs = 80;
        opp->vid = VID_REVISION_1_2;
        opp->veni = VENI_GENERIC;
        opp->spve_mask = 0xFFFF;
        opp->tifr_reset = 0x00000000;
        opp->ipvp_reset = 0x80000000;
        opp->ide_reset = 0x00000001;
        opp->max_irq = FSL_MPIC_20_MAX_IRQ;
        opp->irq_ipi0 = FSL_MPIC_20_IPI_IRQ;
        opp->irq_tim0 = FSL_MPIC_20_TMR_IRQ;
        opp->brr1 = FSL_BRR1_IPID | FSL_BRR1_IPMJ | FSL_BRR1_IPMN;
        list = list_be;
        break;
    case OPENPIC_MODEL_RAVEN:
        opp->nb_irqs = RAVEN_MAX_EXT;
        opp->vid = VID_REVISION_1_3;
        opp->veni = VENI_GENERIC;
        opp->spve_mask = 0xFF;
        opp->tifr_reset = 0x003F7A00;
        opp->ipvp_reset = 0xA0000000;
        opp->ide_reset = 0x00000000;
        opp->max_irq = RAVEN_MAX_IRQ;
        opp->irq_ipi0 = RAVEN_IPI_IRQ;
        opp->irq_tim0 = RAVEN_TMR_IRQ;
        opp->brr1 = -1;
        list = list_le;

        /* Only UP supported today */
        if (opp->nb_cpus != 1) {
            return -EINVAL;
        }
        break;
    }

    memory_region_init(&opp->mem, "openpic", 0x40000);

    for (i = 0; i < ARRAY_SIZE(list_le); i++) {
        memory_region_init_io(&opp->sub_io_mem[i], list[i].ops, opp,
                              list[i].name, list[i].size);

        memory_region_add_subregion(&opp->mem, list[i].start_addr,
                                    &opp->sub_io_mem[i]);
    }

    for (i = 0; i < opp->nb_cpus; i++) {
        opp->dst[i].irqs = g_new(qemu_irq, OPENPIC_OUTPUT_NB);
        for (j = 0; j < OPENPIC_OUTPUT_NB; j++) {
            sysbus_init_irq(dev, &opp->dst[i].irqs[j]);
        }
    }

    register_savevm(&opp->busdev.qdev, "openpic", 0, 2,
                    openpic_save, openpic_load, opp);

    sysbus_init_mmio(dev, &opp->mem);
    qdev_init_gpio_in(&dev->qdev, openpic_set_irq, opp->max_irq);

    return 0;
}

static Property openpic_properties[] = {
    DEFINE_PROP_UINT32("model", OpenPICState, model, OPENPIC_MODEL_FSL_MPIC_20),
    DEFINE_PROP_UINT32("nb_cpus", OpenPICState, nb_cpus, 1),
    DEFINE_PROP_END_OF_LIST(),
};

static void openpic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = openpic_init;
    dc->props = openpic_properties;
    dc->reset = openpic_reset;
}

static TypeInfo openpic_info = {
    .name          = "openpic",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(OpenPICState),
    .class_init    = openpic_class_init,
};

static void openpic_register_types(void)
{
    type_register_static(&openpic_info);
}

type_init(openpic_register_types)

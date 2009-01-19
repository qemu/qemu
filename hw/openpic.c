/*
 * OpenPIC emulation
 *
 * Copyright (c) 2004 Jocelyn Mayer
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
 * - Intel GW80314 I/O compagnion chip developper's manual
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

//#define DEBUG_OPENPIC

#ifdef DEBUG_OPENPIC
#define DPRINTF(fmt, args...) do { printf(fmt , ##args); } while (0)
#else
#define DPRINTF(fmt, args...) do { } while (0)
#endif
#define ERROR(fmr, args...) do { printf("ERROR: " fmr , ##args); } while (0)

#define USE_MPCxxx /* Intel model is broken, for now */

#if defined (USE_INTEL_GW80314)
/* Intel GW80314 I/O Companion chip */

#define MAX_CPU     4
#define MAX_IRQ    32
#define MAX_DBL     4
#define MAX_MBX     4
#define MAX_TMR     4
#define VECTOR_BITS 8
#define MAX_IPI     0

#define VID (0x00000000)

#define OPENPIC_LITTLE_ENDIAN 1
#define OPENPIC_BIG_ENDIAN    0

#elif defined(USE_MPCxxx)

#define MAX_CPU     2
#define MAX_IRQ    64
#define EXT_IRQ    48
#define MAX_DBL     0
#define MAX_MBX     0
#define MAX_TMR     4
#define VECTOR_BITS 8
#define MAX_IPI     4
#define VID         0x03 /* MPIC version ID */
#define VENI        0x00000000 /* Vendor ID */

enum {
    IRQ_IPVP = 0,
    IRQ_IDE,
};

#define OPENPIC_LITTLE_ENDIAN 1
#define OPENPIC_BIG_ENDIAN    0

#else
#error "Please select which OpenPic implementation is to be emulated"
#endif

#if (OPENPIC_BIG_ENDIAN && !TARGET_WORDS_BIGENDIAN) || \
    (OPENPIC_LITTLE_ENDIAN && TARGET_WORDS_BIGENDIAN)
#define OPENPIC_SWAP
#endif

/* Interrupt definitions */
#define IRQ_FE     (EXT_IRQ)     /* Internal functional IRQ */
#define IRQ_ERR    (EXT_IRQ + 1) /* Error IRQ */
#define IRQ_TIM0   (EXT_IRQ + 2) /* First timer IRQ */
#if MAX_IPI > 0
#define IRQ_IPI0   (IRQ_TIM0 + MAX_TMR) /* First IPI IRQ */
#define IRQ_DBL0   (IRQ_IPI0 + (MAX_CPU * MAX_IPI)) /* First doorbell IRQ */
#else
#define IRQ_DBL0   (IRQ_TIM0 + MAX_TMR) /* First doorbell IRQ */
#define IRQ_MBX0   (IRQ_DBL0 + MAX_DBL) /* First mailbox IRQ */
#endif

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

enum {
    IRQ_EXTERNAL = 0x01,
    IRQ_INTERNAL = 0x02,
    IRQ_TIMER    = 0x04,
    IRQ_SPECIAL  = 0x08,
};

typedef struct IRQ_queue_t {
    uint32_t queue[BF_WIDTH(MAX_IRQ)];
    int next;
    int priority;
} IRQ_queue_t;

typedef struct IRQ_src_t {
    uint32_t ipvp;  /* IRQ vector/priority register */
    uint32_t ide;   /* IRQ destination register */
    int type;
    int last_cpu;
    int pending;    /* TRUE if IRQ is pending */
} IRQ_src_t;

enum IPVP_bits {
    IPVP_MASK     = 31,
    IPVP_ACTIVITY = 30,
    IPVP_MODE     = 29,
    IPVP_POLARITY = 23,
    IPVP_SENSE    = 22,
};
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

typedef struct openpic_t {
    PCIDevice pci_dev;
    int mem_index;
    /* Global registers */
    uint32_t frep; /* Feature reporting register */
    uint32_t glbc; /* Global configuration register  */
    uint32_t micr; /* MPIC interrupt configuration register */
    uint32_t veni; /* Vendor identification register */
    uint32_t pint; /* Processor initialization register */
    uint32_t spve; /* Spurious vector register */
    uint32_t tifr; /* Timer frequency reporting register */
    /* Source registers */
    IRQ_src_t src[MAX_IRQ];
    /* Local registers per output pin */
    IRQ_dst_t dst[MAX_CPU];
    int nb_cpus;
    /* Timer registers */
    struct {
	uint32_t ticc;  /* Global timer current count register */
	uint32_t tibc;  /* Global timer base count register */
    } timers[MAX_TMR];
#if MAX_DBL > 0
    /* Doorbell registers */
    uint32_t dar;        /* Doorbell activate register */
    struct {
	uint32_t dmr;    /* Doorbell messaging register */
    } doorbells[MAX_DBL];
#endif
#if MAX_MBX > 0
    /* Mailbox registers */
    struct {
	uint32_t mbr;    /* Mailbox register */
    } mailboxes[MAX_MAILBOXES];
#endif
    /* IRQ out is used when in bypass mode (not implemented) */
    qemu_irq irq_out;
} openpic_t;

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

static void IRQ_check (openpic_t *opp, IRQ_queue_t *q)
{
    int next, i;
    int priority;

    next = -1;
    priority = -1;
    for (i = 0; i < MAX_IRQ; i++) {
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

static int IRQ_get_next (openpic_t *opp, IRQ_queue_t *q)
{
    if (q->next == -1) {
        /* XXX: optimize */
	IRQ_check(opp, q);
    }

    return q->next;
}

static void IRQ_local_pipe (openpic_t *opp, int n_CPU, int n_IRQ)
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
    set_bit(&src->ipvp, IPVP_ACTIVITY);
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
    qemu_irq_raise(dst->irqs[OPENPIC_OUTPUT_INT]);
}

/* update pic state because registers for n_IRQ have changed value */
static void openpic_update_irq(openpic_t *opp, int n_IRQ)
{
    IRQ_src_t *src;
    int i;

    src = &opp->src[n_IRQ];

    if (!src->pending) {
        /* no irq pending */
        DPRINTF("%s: IRQ %d is not pending\n", __func__, n_IRQ);
        return;
    }
    if (test_bit(&src->ipvp, IPVP_MASK)) {
	/* Interrupt source is disabled */
        DPRINTF("%s: IRQ %d is disabled\n", __func__, n_IRQ);
	return;
    }
    if (IPVP_PRIORITY(src->ipvp) == 0) {
	/* Priority set to zero */
        DPRINTF("%s: IRQ %d has 0 priority\n", __func__, n_IRQ);
	return;
    }
    if (test_bit(&src->ipvp, IPVP_ACTIVITY)) {
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
    } else if (!test_bit(&src->ipvp, IPVP_MODE)) {
        /* Directed delivery mode */
        for (i = 0; i < opp->nb_cpus; i++) {
            if (test_bit(&src->ide, i))
                IRQ_local_pipe(opp, i, n_IRQ);
        }
    } else {
        /* Distributed delivery mode */
        for (i = src->last_cpu + 1; i != src->last_cpu; i++) {
            if (i == opp->nb_cpus)
                i = 0;
            if (test_bit(&src->ide, i)) {
                IRQ_local_pipe(opp, i, n_IRQ);
                src->last_cpu = i;
                break;
            }
        }
    }
}

static void openpic_set_irq(void *opaque, int n_IRQ, int level)
{
    openpic_t *opp = opaque;
    IRQ_src_t *src;

    src = &opp->src[n_IRQ];
    DPRINTF("openpic: set irq %d = %d ipvp=%08x\n",
            n_IRQ, level, src->ipvp);
    if (test_bit(&src->ipvp, IPVP_SENSE)) {
        /* level-sensitive irq */
        src->pending = level;
        if (!level)
            reset_bit(&src->ipvp, IPVP_ACTIVITY);
    } else {
        /* edge-sensitive irq */
        if (level)
            src->pending = 1;
    }
    openpic_update_irq(opp, n_IRQ);
}

static void openpic_reset (openpic_t *opp)
{
    int i;

    opp->glbc = 0x80000000;
    /* Initialise controller registers */
    opp->frep = ((EXT_IRQ - 1) << 16) | ((MAX_CPU - 1) << 8) | VID;
    opp->veni = VENI;
    opp->pint = 0x00000000;
    opp->spve = 0x000000FF;
    opp->tifr = 0x003F7A00;
    /* ? */
    opp->micr = 0x00000000;
    /* Initialise IRQ sources */
    for (i = 0; i < MAX_IRQ; i++) {
	opp->src[i].ipvp = 0xA0000000;
	opp->src[i].ide  = 0x00000000;
    }
    /* Initialise IRQ destinations */
    for (i = 0; i < MAX_CPU; i++) {
	opp->dst[i].pctp      = 0x0000000F;
	opp->dst[i].pcsr      = 0x00000000;
	memset(&opp->dst[i].raised, 0, sizeof(IRQ_queue_t));
	memset(&opp->dst[i].servicing, 0, sizeof(IRQ_queue_t));
    }
    /* Initialise timers */
    for (i = 0; i < MAX_TMR; i++) {
	opp->timers[i].ticc = 0x00000000;
	opp->timers[i].tibc = 0x80000000;
    }
    /* Initialise doorbells */
#if MAX_DBL > 0
    opp->dar = 0x00000000;
    for (i = 0; i < MAX_DBL; i++) {
	opp->doorbells[i].dmr  = 0x00000000;
    }
#endif
    /* Initialise mailboxes */
#if MAX_MBX > 0
    for (i = 0; i < MAX_MBX; i++) { /* ? */
	opp->mailboxes[i].mbr   = 0x00000000;
    }
#endif
    /* Go out of RESET state */
    opp->glbc = 0x00000000;
}

static inline uint32_t read_IRQreg (openpic_t *opp, int n_IRQ, uint32_t reg)
{
    uint32_t retval;

    switch (reg) {
    case IRQ_IPVP:
	retval = opp->src[n_IRQ].ipvp;
	break;
    case IRQ_IDE:
	retval = opp->src[n_IRQ].ide;
	break;
    }

    return retval;
}

static inline void write_IRQreg (openpic_t *opp, int n_IRQ,
                                 uint32_t reg, uint32_t val)
{
    uint32_t tmp;

    switch (reg) {
    case IRQ_IPVP:
        /* NOTE: not fully accurate for special IRQs, but simple and
           sufficient */
        /* ACTIVITY bit is read-only */
	opp->src[n_IRQ].ipvp =
            (opp->src[n_IRQ].ipvp & 0x40000000) |
            (val & 0x800F00FF);
        openpic_update_irq(opp, n_IRQ);
        DPRINTF("Set IPVP %d to 0x%08x -> 0x%08x\n",
                n_IRQ, val, opp->src[n_IRQ].ipvp);
	break;
    case IRQ_IDE:
	tmp = val & 0xC0000000;
        tmp |= val & ((1 << MAX_CPU) - 1);
	opp->src[n_IRQ].ide = tmp;
        DPRINTF("Set IDE %d to 0x%08x\n", n_IRQ, opp->src[n_IRQ].ide);
	break;
    }
}

#if 0 // Code provision for Intel model
#if MAX_DBL > 0
static uint32_t read_doorbell_register (openpic_t *opp,
					int n_dbl, uint32_t offset)
{
    uint32_t retval;

    switch (offset) {
    case DBL_IPVP_OFFSET:
	retval = read_IRQreg(opp, IRQ_DBL0 + n_dbl, IRQ_IPVP);
	break;
    case DBL_IDE_OFFSET:
	retval = read_IRQreg(opp, IRQ_DBL0 + n_dbl, IRQ_IDE);
	break;
    case DBL_DMR_OFFSET:
	retval = opp->doorbells[n_dbl].dmr;
	break;
    }

    return retval;
}

static void write_doorbell_register (penpic_t *opp, int n_dbl,
				     uint32_t offset, uint32_t value)
{
    switch (offset) {
    case DBL_IVPR_OFFSET:
	write_IRQreg(opp, IRQ_DBL0 + n_dbl, IRQ_IPVP, value);
	break;
    case DBL_IDE_OFFSET:
	write_IRQreg(opp, IRQ_DBL0 + n_dbl, IRQ_IDE, value);
	break;
    case DBL_DMR_OFFSET:
	opp->doorbells[n_dbl].dmr = value;
	break;
    }
}
#endif

#if MAX_MBX > 0
static uint32_t read_mailbox_register (openpic_t *opp,
				       int n_mbx, uint32_t offset)
{
    uint32_t retval;

    switch (offset) {
    case MBX_MBR_OFFSET:
	retval = opp->mailboxes[n_mbx].mbr;
	break;
    case MBX_IVPR_OFFSET:
	retval = read_IRQreg(opp, IRQ_MBX0 + n_mbx, IRQ_IPVP);
	break;
    case MBX_DMR_OFFSET:
	retval = read_IRQreg(opp, IRQ_MBX0 + n_mbx, IRQ_IDE);
	break;
    }

    return retval;
}

static void write_mailbox_register (openpic_t *opp, int n_mbx,
				    uint32_t address, uint32_t value)
{
    switch (offset) {
    case MBX_MBR_OFFSET:
	opp->mailboxes[n_mbx].mbr = value;
	break;
    case MBX_IVPR_OFFSET:
	write_IRQreg(opp, IRQ_MBX0 + n_mbx, IRQ_IPVP, value);
	break;
    case MBX_DMR_OFFSET:
	write_IRQreg(opp, IRQ_MBX0 + n_mbx, IRQ_IDE, value);
	break;
    }
}
#endif
#endif /* 0 : Code provision for Intel model */

static void openpic_gbl_write (void *opaque, uint32_t addr, uint32_t val)
{
    openpic_t *opp = opaque;
    IRQ_dst_t *dst;
    int idx;

    DPRINTF("%s: addr %08x <= %08x\n", __func__, addr, val);
    if (addr & 0xF)
        return;
#if defined OPENPIC_SWAP
    val = bswap32(val);
#endif
    addr &= 0xFF;
    switch (addr) {
    case 0x00: /* FREP */
        break;
    case 0x20: /* GLBC */
        if (val & 0x80000000)
            openpic_reset(opp);
        opp->glbc = val & ~0x80000000;
	break;
    case 0x80: /* VENI */
	break;
    case 0x90: /* PINT */
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
#if MAX_IPI > 0
    case 0xA0: /* IPI_IPVP */
    case 0xB0:
    case 0xC0:
    case 0xD0:
        {
            int idx;
            idx = (addr - 0xA0) >> 4;
            write_IRQreg(opp, IRQ_IPI0 + idx, IRQ_IPVP, val);
        }
        break;
#endif
    case 0xE0: /* SPVE */
        opp->spve = val & 0x000000FF;
        break;
    case 0xF0: /* TIFR */
        opp->tifr = val;
	break;
    default:
        break;
    }
}

static uint32_t openpic_gbl_read (void *opaque, uint32_t addr)
{
    openpic_t *opp = opaque;
    uint32_t retval;

    DPRINTF("%s: addr %08x\n", __func__, addr);
    retval = 0xFFFFFFFF;
    if (addr & 0xF)
        return retval;
    addr &= 0xFF;
    switch (addr) {
    case 0x00: /* FREP */
        retval = opp->frep;
        break;
    case 0x20: /* GLBC */
        retval = opp->glbc;
	break;
    case 0x80: /* VENI */
        retval = opp->veni;
	break;
    case 0x90: /* PINT */
        retval = 0x00000000;
	break;
#if MAX_IPI > 0
    case 0xA0: /* IPI_IPVP */
    case 0xB0:
    case 0xC0:
    case 0xD0:
        {
            int idx;
            idx = (addr - 0xA0) >> 4;
            retval = read_IRQreg(opp, IRQ_IPI0 + idx, IRQ_IPVP);
        }
	break;
#endif
    case 0xE0: /* SPVE */
        retval = opp->spve;
        break;
    case 0xF0: /* TIFR */
        retval = opp->tifr;
	break;
    default:
        break;
    }
    DPRINTF("%s: => %08x\n", __func__, retval);
#if defined OPENPIC_SWAP
    retval = bswap32(retval);
#endif

    return retval;
}

static void openpic_timer_write (void *opaque, uint32_t addr, uint32_t val)
{
    openpic_t *opp = opaque;
    int idx;

    DPRINTF("%s: addr %08x <= %08x\n", __func__, addr, val);
    if (addr & 0xF)
        return;
#if defined OPENPIC_SWAP
    val = bswap32(val);
#endif
    addr -= 0x1100;
    addr &= 0xFFFF;
    idx = (addr & 0xFFF0) >> 6;
    addr = addr & 0x30;
    switch (addr) {
    case 0x00: /* TICC */
        break;
    case 0x10: /* TIBC */
	if ((opp->timers[idx].ticc & 0x80000000) != 0 &&
	    (val & 0x80000000) == 0 &&
            (opp->timers[idx].tibc & 0x80000000) != 0)
	    opp->timers[idx].ticc &= ~0x80000000;
	opp->timers[idx].tibc = val;
	break;
    case 0x20: /* TIVP */
	write_IRQreg(opp, IRQ_TIM0 + idx, IRQ_IPVP, val);
	break;
    case 0x30: /* TIDE */
	write_IRQreg(opp, IRQ_TIM0 + idx, IRQ_IDE, val);
	break;
    }
}

static uint32_t openpic_timer_read (void *opaque, uint32_t addr)
{
    openpic_t *opp = opaque;
    uint32_t retval;
    int idx;

    DPRINTF("%s: addr %08x\n", __func__, addr);
    retval = 0xFFFFFFFF;
    if (addr & 0xF)
        return retval;
    addr -= 0x1100;
    addr &= 0xFFFF;
    idx = (addr & 0xFFF0) >> 6;
    addr = addr & 0x30;
    switch (addr) {
    case 0x00: /* TICC */
	retval = opp->timers[idx].ticc;
        break;
    case 0x10: /* TIBC */
	retval = opp->timers[idx].tibc;
	break;
    case 0x20: /* TIPV */
	retval = read_IRQreg(opp, IRQ_TIM0 + idx, IRQ_IPVP);
	break;
    case 0x30: /* TIDE */
	retval = read_IRQreg(opp, IRQ_TIM0 + idx, IRQ_IDE);
	break;
    }
    DPRINTF("%s: => %08x\n", __func__, retval);
#if defined OPENPIC_SWAP
    retval = bswap32(retval);
#endif

    return retval;
}

static void openpic_src_write (void *opaque, uint32_t addr, uint32_t val)
{
    openpic_t *opp = opaque;
    int idx;

    DPRINTF("%s: addr %08x <= %08x\n", __func__, addr, val);
    if (addr & 0xF)
        return;
#if defined OPENPIC_SWAP
    val = tswap32(val);
#endif
    addr = addr & 0xFFF0;
    idx = addr >> 5;
    if (addr & 0x10) {
        /* EXDE / IFEDE / IEEDE */
        write_IRQreg(opp, idx, IRQ_IDE, val);
    } else {
        /* EXVP / IFEVP / IEEVP */
        write_IRQreg(opp, idx, IRQ_IPVP, val);
    }
}

static uint32_t openpic_src_read (void *opaque, uint32_t addr)
{
    openpic_t *opp = opaque;
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
        retval = read_IRQreg(opp, idx, IRQ_IDE);
    } else {
        /* EXVP / IFEVP / IEEVP */
        retval = read_IRQreg(opp, idx, IRQ_IPVP);
    }
    DPRINTF("%s: => %08x\n", __func__, retval);
#if defined OPENPIC_SWAP
    retval = tswap32(retval);
#endif

    return retval;
}

static void openpic_cpu_write (void *opaque, uint32_t addr, uint32_t val)
{
    openpic_t *opp = opaque;
    IRQ_src_t *src;
    IRQ_dst_t *dst;
    int idx, s_IRQ, n_IRQ;

    DPRINTF("%s: addr %08x <= %08x\n", __func__, addr, val);
    if (addr & 0xF)
        return;
#if defined OPENPIC_SWAP
    val = bswap32(val);
#endif
    addr &= 0x1FFF0;
    idx = addr / 0x1000;
    dst = &opp->dst[idx];
    addr &= 0xFF0;
    switch (addr) {
#if MAX_IPI > 0
    case 0x40: /* PIPD */
    case 0x50:
    case 0x60:
    case 0x70:
        idx = (addr - 0x40) >> 4;
        write_IRQreg(opp, IRQ_IPI0 + idx, IRQ_IDE, val);
        openpic_set_irq(opp, IRQ_IPI0 + idx, 1);
        openpic_set_irq(opp, IRQ_IPI0 + idx, 0);
        break;
#endif
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
            qemu_irq_raise(dst->irqs[OPENPIC_OUTPUT_INT]);
        }
	break;
    default:
        break;
    }
}

static uint32_t openpic_cpu_read (void *opaque, uint32_t addr)
{
    openpic_t *opp = opaque;
    IRQ_src_t *src;
    IRQ_dst_t *dst;
    uint32_t retval;
    int idx, n_IRQ;

    DPRINTF("%s: addr %08x\n", __func__, addr);
    retval = 0xFFFFFFFF;
    if (addr & 0xF)
        return retval;
    addr &= 0x1FFF0;
    idx = addr / 0x1000;
    dst = &opp->dst[idx];
    addr &= 0xFF0;
    switch (addr) {
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
	    if (!test_bit(&src->ipvp, IPVP_ACTIVITY) ||
		!(IPVP_PRIORITY(src->ipvp) > dst->pctp)) {
		/* - Spurious level-sensitive IRQ
		 * - Priorities has been changed
		 *   and the pending IRQ isn't allowed anymore
		 */
		reset_bit(&src->ipvp, IPVP_ACTIVITY);
		retval = IPVP_VECTOR(opp->spve);
	    } else {
		/* IRQ enter servicing state */
		IRQ_setbit(&dst->servicing, n_IRQ);
		retval = IPVP_VECTOR(src->ipvp);
	    }
	    IRQ_resetbit(&dst->raised, n_IRQ);
	    dst->raised.next = -1;
	    if (!test_bit(&src->ipvp, IPVP_SENSE)) {
                /* edge-sensitive IRQ */
		reset_bit(&src->ipvp, IPVP_ACTIVITY);
                src->pending = 0;
            }
	}
	break;
    case 0xB0: /* PEOI */
	retval = 0;
	break;
#if MAX_IPI > 0
    case 0x40: /* IDE */
    case 0x50:
        idx = (addr - 0x40) >> 4;
        retval = read_IRQreg(opp, IRQ_IPI0 + idx, IRQ_IDE);
        break;
#endif
    default:
        break;
    }
    DPRINTF("%s: => %08x\n", __func__, retval);
#if defined OPENPIC_SWAP
    retval= bswap32(retval);
#endif

    return retval;
}

static void openpic_buggy_write (void *opaque,
                                 target_phys_addr_t addr, uint32_t val)
{
    printf("Invalid OPENPIC write access !\n");
}

static uint32_t openpic_buggy_read (void *opaque, target_phys_addr_t addr)
{
    printf("Invalid OPENPIC read access !\n");

    return -1;
}

static void openpic_writel (void *opaque,
                            target_phys_addr_t addr, uint32_t val)
{
    openpic_t *opp = opaque;

    addr &= 0x3FFFF;
    DPRINTF("%s: offset %08x val: %08x\n", __func__, (int)addr, val);
    if (addr < 0x1100) {
        /* Global registers */
        openpic_gbl_write(opp, addr, val);
    } else if (addr < 0x10000) {
        /* Timers registers */
        openpic_timer_write(opp, addr, val);
    } else if (addr < 0x20000) {
        /* Source registers */
        openpic_src_write(opp, addr, val);
    } else {
        /* CPU registers */
        openpic_cpu_write(opp, addr, val);
    }
}

static uint32_t openpic_readl (void *opaque,target_phys_addr_t addr)
{
    openpic_t *opp = opaque;
    uint32_t retval;

    addr &= 0x3FFFF;
    DPRINTF("%s: offset %08x\n", __func__, (int)addr);
    if (addr < 0x1100) {
        /* Global registers */
        retval = openpic_gbl_read(opp, addr);
    } else if (addr < 0x10000) {
        /* Timers registers */
        retval = openpic_timer_read(opp, addr);
    } else if (addr < 0x20000) {
        /* Source registers */
        retval = openpic_src_read(opp, addr);
    } else {
        /* CPU registers */
        retval = openpic_cpu_read(opp, addr);
    }

    return retval;
}

static CPUWriteMemoryFunc *openpic_write[] = {
    &openpic_buggy_write,
    &openpic_buggy_write,
    &openpic_writel,
};

static CPUReadMemoryFunc *openpic_read[] = {
    &openpic_buggy_read,
    &openpic_buggy_read,
    &openpic_readl,
};

static void openpic_map(PCIDevice *pci_dev, int region_num,
                        uint32_t addr, uint32_t size, int type)
{
    openpic_t *opp;

    DPRINTF("Map OpenPIC\n");
    opp = (openpic_t *)pci_dev;
    /* Global registers */
    DPRINTF("Register OPENPIC gbl   %08x => %08x\n",
            addr + 0x1000, addr + 0x1000 + 0x100);
    /* Timer registers */
    DPRINTF("Register OPENPIC timer %08x => %08x\n",
            addr + 0x1100, addr + 0x1100 + 0x40 * MAX_TMR);
    /* Interrupt source registers */
    DPRINTF("Register OPENPIC src   %08x => %08x\n",
            addr + 0x10000, addr + 0x10000 + 0x20 * (EXT_IRQ + 2));
    /* Per CPU registers */
    DPRINTF("Register OPENPIC dst   %08x => %08x\n",
            addr + 0x20000, addr + 0x20000 + 0x1000 * MAX_CPU);
    cpu_register_physical_memory(addr, 0x40000, opp->mem_index);
#if 0 // Don't implement ISU for now
    opp_io_memory = cpu_register_io_memory(0, openpic_src_read,
                                           openpic_src_write);
    cpu_register_physical_memory(isu_base, 0x20 * (EXT_IRQ + 2),
                                 opp_io_memory);
#endif
}

qemu_irq *openpic_init (PCIBus *bus, int *pmem_index, int nb_cpus,
                        qemu_irq **irqs, qemu_irq irq_out)
{
    openpic_t *opp;
    uint8_t *pci_conf;
    int i, m;

    /* XXX: for now, only one CPU is supported */
    if (nb_cpus != 1)
        return NULL;
    if (bus) {
        opp = (openpic_t *)pci_register_device(bus, "OpenPIC", sizeof(openpic_t),
                                               -1, NULL, NULL);
        if (opp == NULL)
            return NULL;
        pci_conf = opp->pci_dev.config;
        pci_conf[0x00] = 0x14; // IBM MPIC2
        pci_conf[0x01] = 0x10;
        pci_conf[0x02] = 0xFF;
        pci_conf[0x03] = 0xFF;
        pci_conf[0x0a] = 0x80; // PIC
        pci_conf[0x0b] = 0x08;
        pci_conf[0x0e] = 0x00; // header_type
        pci_conf[0x3d] = 0x00; // no interrupt pin

        /* Register I/O spaces */
        pci_register_io_region((PCIDevice *)opp, 0, 0x40000,
                               PCI_ADDRESS_SPACE_MEM, &openpic_map);
    } else {
        opp = qemu_mallocz(sizeof(openpic_t));
    }
    opp->mem_index = cpu_register_io_memory(0, openpic_read,
                                            openpic_write, opp);

    //    isu_base &= 0xFFFC0000;
    opp->nb_cpus = nb_cpus;
    /* Set IRQ types */
    for (i = 0; i < EXT_IRQ; i++) {
        opp->src[i].type = IRQ_EXTERNAL;
    }
    for (; i < IRQ_TIM0; i++) {
        opp->src[i].type = IRQ_SPECIAL;
    }
#if MAX_IPI > 0
    m = IRQ_IPI0;
#else
    m = IRQ_DBL0;
#endif
    for (; i < m; i++) {
        opp->src[i].type = IRQ_TIMER;
    }
    for (; i < MAX_IRQ; i++) {
        opp->src[i].type = IRQ_INTERNAL;
    }
    for (i = 0; i < nb_cpus; i++)
        opp->dst[i].irqs = irqs[i];
    opp->irq_out = irq_out;
    openpic_reset(opp);
    if (pmem_index)
        *pmem_index = opp->mem_index;

    return qemu_allocate_irqs(openpic_set_irq, opp, MAX_IRQ);
}

/*
 * Intel XScale PXA Programmable Interrupt Controller.
 *
 * Copyright (c) 2006 Openedhand Ltd.
 * Copyright (c) 2006 Thorsten Zitterell
 * Written by Andrzej Zaborowski <balrog@zabor.org>
 *
 * This code is licenced under the GPL.
 */

#include "hw.h"
#include "pxa.h"

#define ICIP	0x00	/* Interrupt Controller IRQ Pending register */
#define ICMR	0x04	/* Interrupt Controller Mask register */
#define ICLR	0x08	/* Interrupt Controller Level register */
#define ICFP	0x0c	/* Interrupt Controller FIQ Pending register */
#define ICPR	0x10	/* Interrupt Controller Pending register */
#define ICCR	0x14	/* Interrupt Controller Control register */
#define ICHP	0x18	/* Interrupt Controller Highest Priority register */
#define IPR0	0x1c	/* Interrupt Controller Priority register 0 */
#define IPR31	0x98	/* Interrupt Controller Priority register 31 */
#define ICIP2	0x9c	/* Interrupt Controller IRQ Pending register 2 */
#define ICMR2	0xa0	/* Interrupt Controller Mask register 2 */
#define ICLR2	0xa4	/* Interrupt Controller Level register 2 */
#define ICFP2	0xa8	/* Interrupt Controller FIQ Pending register 2 */
#define ICPR2	0xac	/* Interrupt Controller Pending register 2 */
#define IPR32	0xb0	/* Interrupt Controller Priority register 32 */
#define IPR39	0xcc	/* Interrupt Controller Priority register 39 */

#define PXA2XX_PIC_SRCS	40

typedef struct {
    CPUState *cpu_env;
    uint32_t int_enabled[2];
    uint32_t int_pending[2];
    uint32_t is_fiq[2];
    uint32_t int_idle;
    uint32_t priority[PXA2XX_PIC_SRCS];
} PXA2xxPICState;

static void pxa2xx_pic_update(void *opaque)
{
    uint32_t mask[2];
    PXA2xxPICState *s = (PXA2xxPICState *) opaque;

    if (s->cpu_env->halted) {
        mask[0] = s->int_pending[0] & (s->int_enabled[0] | s->int_idle);
        mask[1] = s->int_pending[1] & (s->int_enabled[1] | s->int_idle);
        if (mask[0] || mask[1])
            cpu_interrupt(s->cpu_env, CPU_INTERRUPT_EXITTB);
    }

    mask[0] = s->int_pending[0] & s->int_enabled[0];
    mask[1] = s->int_pending[1] & s->int_enabled[1];

    if ((mask[0] & s->is_fiq[0]) || (mask[1] & s->is_fiq[1]))
        cpu_interrupt(s->cpu_env, CPU_INTERRUPT_FIQ);
    else
        cpu_reset_interrupt(s->cpu_env, CPU_INTERRUPT_FIQ);

    if ((mask[0] & ~s->is_fiq[0]) || (mask[1] & ~s->is_fiq[1]))
        cpu_interrupt(s->cpu_env, CPU_INTERRUPT_HARD);
    else
        cpu_reset_interrupt(s->cpu_env, CPU_INTERRUPT_HARD);
}

/* Note: Here level means state of the signal on a pin, not
 * IRQ/FIQ distinction as in PXA Developer Manual.  */
static void pxa2xx_pic_set_irq(void *opaque, int irq, int level)
{
    PXA2xxPICState *s = (PXA2xxPICState *) opaque;
    int int_set = (irq >= 32);
    irq &= 31;

    if (level)
        s->int_pending[int_set] |= 1 << irq;
    else
        s->int_pending[int_set] &= ~(1 << irq);

    pxa2xx_pic_update(opaque);
}

static inline uint32_t pxa2xx_pic_highest(PXA2xxPICState *s) {
    int i, int_set, irq;
    uint32_t bit, mask[2];
    uint32_t ichp = 0x003f003f;	/* Both IDs invalid */

    mask[0] = s->int_pending[0] & s->int_enabled[0];
    mask[1] = s->int_pending[1] & s->int_enabled[1];

    for (i = PXA2XX_PIC_SRCS - 1; i >= 0; i --) {
        irq = s->priority[i] & 0x3f;
        if ((s->priority[i] & (1 << 31)) && irq < PXA2XX_PIC_SRCS) {
            /* Source peripheral ID is valid.  */
            bit = 1 << (irq & 31);
            int_set = (irq >= 32);

            if (mask[int_set] & bit & s->is_fiq[int_set]) {
                /* FIQ asserted */
                ichp &= 0xffff0000;
                ichp |= (1 << 15) | irq;
            }

            if (mask[int_set] & bit & ~s->is_fiq[int_set]) {
                /* IRQ asserted */
                ichp &= 0x0000ffff;
                ichp |= (1 << 31) | (irq << 16);
            }
        }
    }

    return ichp;
}

static uint32_t pxa2xx_pic_mem_read(void *opaque, target_phys_addr_t offset)
{
    PXA2xxPICState *s = (PXA2xxPICState *) opaque;

    switch (offset) {
    case ICIP:	/* IRQ Pending register */
        return s->int_pending[0] & ~s->is_fiq[0] & s->int_enabled[0];
    case ICIP2:	/* IRQ Pending register 2 */
        return s->int_pending[1] & ~s->is_fiq[1] & s->int_enabled[1];
    case ICMR:	/* Mask register */
        return s->int_enabled[0];
    case ICMR2:	/* Mask register 2 */
        return s->int_enabled[1];
    case ICLR:	/* Level register */
        return s->is_fiq[0];
    case ICLR2:	/* Level register 2 */
        return s->is_fiq[1];
    case ICCR:	/* Idle mask */
        return (s->int_idle == 0);
    case ICFP:	/* FIQ Pending register */
        return s->int_pending[0] & s->is_fiq[0] & s->int_enabled[0];
    case ICFP2:	/* FIQ Pending register 2 */
        return s->int_pending[1] & s->is_fiq[1] & s->int_enabled[1];
    case ICPR:	/* Pending register */
        return s->int_pending[0];
    case ICPR2:	/* Pending register 2 */
        return s->int_pending[1];
    case IPR0  ... IPR31:
        return s->priority[0  + ((offset - IPR0 ) >> 2)];
    case IPR32 ... IPR39:
        return s->priority[32 + ((offset - IPR32) >> 2)];
    case ICHP:	/* Highest Priority register */
        return pxa2xx_pic_highest(s);
    default:
        printf("%s: Bad register offset " REG_FMT "\n", __FUNCTION__, offset);
        return 0;
    }
}

static void pxa2xx_pic_mem_write(void *opaque, target_phys_addr_t offset,
                uint32_t value)
{
    PXA2xxPICState *s = (PXA2xxPICState *) opaque;

    switch (offset) {
    case ICMR:	/* Mask register */
        s->int_enabled[0] = value;
        break;
    case ICMR2:	/* Mask register 2 */
        s->int_enabled[1] = value;
        break;
    case ICLR:	/* Level register */
        s->is_fiq[0] = value;
        break;
    case ICLR2:	/* Level register 2 */
        s->is_fiq[1] = value;
        break;
    case ICCR:	/* Idle mask */
        s->int_idle = (value & 1) ? 0 : ~0;
        break;
    case IPR0  ... IPR31:
        s->priority[0  + ((offset - IPR0 ) >> 2)] = value & 0x8000003f;
        break;
    case IPR32 ... IPR39:
        s->priority[32 + ((offset - IPR32) >> 2)] = value & 0x8000003f;
        break;
    default:
        printf("%s: Bad register offset " REG_FMT "\n", __FUNCTION__, offset);
        return;
    }
    pxa2xx_pic_update(opaque);
}

/* Interrupt Controller Coprocessor Space Register Mapping */
static const int pxa2xx_cp_reg_map[0x10] = {
    [0x0 ... 0xf] = -1,
    [0x0] = ICIP,
    [0x1] = ICMR,
    [0x2] = ICLR,
    [0x3] = ICFP,
    [0x4] = ICPR,
    [0x5] = ICHP,
    [0x6] = ICIP2,
    [0x7] = ICMR2,
    [0x8] = ICLR2,
    [0x9] = ICFP2,
    [0xa] = ICPR2,
};

static uint32_t pxa2xx_pic_cp_read(void *opaque, int op2, int reg, int crm)
{
    target_phys_addr_t offset;

    if (pxa2xx_cp_reg_map[reg] == -1) {
        printf("%s: Bad register 0x%x\n", __FUNCTION__, reg);
        return 0;
    }

    offset = pxa2xx_cp_reg_map[reg];
    return pxa2xx_pic_mem_read(opaque, offset);
}

static void pxa2xx_pic_cp_write(void *opaque, int op2, int reg, int crm,
                uint32_t value)
{
    target_phys_addr_t offset;

    if (pxa2xx_cp_reg_map[reg] == -1) {
        printf("%s: Bad register 0x%x\n", __FUNCTION__, reg);
        return;
    }

    offset = pxa2xx_cp_reg_map[reg];
    pxa2xx_pic_mem_write(opaque, offset, value);
}

static CPUReadMemoryFunc * const pxa2xx_pic_readfn[] = {
    pxa2xx_pic_mem_read,
    pxa2xx_pic_mem_read,
    pxa2xx_pic_mem_read,
};

static CPUWriteMemoryFunc * const pxa2xx_pic_writefn[] = {
    pxa2xx_pic_mem_write,
    pxa2xx_pic_mem_write,
    pxa2xx_pic_mem_write,
};

static void pxa2xx_pic_save(QEMUFile *f, void *opaque)
{
    PXA2xxPICState *s = (PXA2xxPICState *) opaque;
    int i;

    for (i = 0; i < 2; i ++)
        qemu_put_be32s(f, &s->int_enabled[i]);
    for (i = 0; i < 2; i ++)
        qemu_put_be32s(f, &s->int_pending[i]);
    for (i = 0; i < 2; i ++)
        qemu_put_be32s(f, &s->is_fiq[i]);
    qemu_put_be32s(f, &s->int_idle);
    for (i = 0; i < PXA2XX_PIC_SRCS; i ++)
        qemu_put_be32s(f, &s->priority[i]);
}

static int pxa2xx_pic_load(QEMUFile *f, void *opaque, int version_id)
{
    PXA2xxPICState *s = (PXA2xxPICState *) opaque;
    int i;

    for (i = 0; i < 2; i ++)
        qemu_get_be32s(f, &s->int_enabled[i]);
    for (i = 0; i < 2; i ++)
        qemu_get_be32s(f, &s->int_pending[i]);
    for (i = 0; i < 2; i ++)
        qemu_get_be32s(f, &s->is_fiq[i]);
    qemu_get_be32s(f, &s->int_idle);
    for (i = 0; i < PXA2XX_PIC_SRCS; i ++)
        qemu_get_be32s(f, &s->priority[i]);

    pxa2xx_pic_update(opaque);
    return 0;
}

qemu_irq *pxa2xx_pic_init(target_phys_addr_t base, CPUState *env)
{
    PXA2xxPICState *s;
    int iomemtype;
    qemu_irq *qi;

    s = (PXA2xxPICState *)
            qemu_mallocz(sizeof(PXA2xxPICState));
    if (!s)
        return NULL;

    s->cpu_env = env;

    s->int_pending[0] = 0;
    s->int_pending[1] = 0;
    s->int_enabled[0] = 0;
    s->int_enabled[1] = 0;
    s->is_fiq[0] = 0;
    s->is_fiq[1] = 0;

    qi = qemu_allocate_irqs(pxa2xx_pic_set_irq, s, PXA2XX_PIC_SRCS);

    /* Enable IC memory-mapped registers access.  */
    iomemtype = cpu_register_io_memory(pxa2xx_pic_readfn,
                    pxa2xx_pic_writefn, s, DEVICE_NATIVE_ENDIAN);
    cpu_register_physical_memory(base, 0x00100000, iomemtype);

    /* Enable IC coprocessor access.  */
    cpu_arm_set_cp_io(env, 6, pxa2xx_pic_cp_read, pxa2xx_pic_cp_write, s);

    register_savevm(NULL, "pxa2xx_pic", 0, 0, pxa2xx_pic_save,
                    pxa2xx_pic_load, s);

    return qi;
}

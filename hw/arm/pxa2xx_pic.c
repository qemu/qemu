/*
 * Intel XScale PXA Programmable Interrupt Controller.
 *
 * Copyright (c) 2006 Openedhand Ltd.
 * Copyright (c) 2006 Thorsten Zitterell
 * Written by Andrzej Zaborowski <balrog@zabor.org>
 *
 * This code is licensed under the GPL.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "cpu.h"
#include "hw/arm/pxa.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qom/object.h"
#include "target/arm/cpregs.h"

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

#define TYPE_PXA2XX_PIC "pxa2xx_pic"
OBJECT_DECLARE_SIMPLE_TYPE(PXA2xxPICState, PXA2XX_PIC)

struct PXA2xxPICState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion iomem;
    ARMCPU *cpu;
    uint32_t int_enabled[2];
    uint32_t int_pending[2];
    uint32_t is_fiq[2];
    uint32_t int_idle;
    uint32_t priority[PXA2XX_PIC_SRCS];
};

static void pxa2xx_pic_update(void *opaque)
{
    uint32_t mask[2];
    PXA2xxPICState *s = (PXA2xxPICState *) opaque;
    CPUState *cpu = CPU(s->cpu);

    if (cpu->halted) {
        mask[0] = s->int_pending[0] & (s->int_enabled[0] | s->int_idle);
        mask[1] = s->int_pending[1] & (s->int_enabled[1] | s->int_idle);
        if (mask[0] || mask[1]) {
            cpu_interrupt(cpu, CPU_INTERRUPT_EXITTB);
        }
    }

    mask[0] = s->int_pending[0] & s->int_enabled[0];
    mask[1] = s->int_pending[1] & s->int_enabled[1];

    if ((mask[0] & s->is_fiq[0]) || (mask[1] & s->is_fiq[1])) {
        cpu_interrupt(cpu, CPU_INTERRUPT_FIQ);
    } else {
        cpu_reset_interrupt(cpu, CPU_INTERRUPT_FIQ);
    }

    if ((mask[0] & ~s->is_fiq[0]) || (mask[1] & ~s->is_fiq[1])) {
        cpu_interrupt(cpu, CPU_INTERRUPT_HARD);
    } else {
        cpu_reset_interrupt(cpu, CPU_INTERRUPT_HARD);
    }
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
        if ((s->priority[i] & (1U << 31)) && irq < PXA2XX_PIC_SRCS) {
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
                ichp |= (1U << 31) | (irq << 16);
            }
        }
    }

    return ichp;
}

static uint64_t pxa2xx_pic_mem_read(void *opaque, hwaddr offset,
                                    unsigned size)
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
        qemu_log_mask(LOG_GUEST_ERROR,
                      "pxa2xx_pic_mem_read: bad register offset 0x%" HWADDR_PRIx
                      "\n", offset);
        return 0;
    }
}

static void pxa2xx_pic_mem_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned size)
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
        qemu_log_mask(LOG_GUEST_ERROR,
                      "pxa2xx_pic_mem_write: bad register offset 0x%"
                      HWADDR_PRIx "\n", offset);
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

static uint64_t pxa2xx_pic_cp_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    int offset = pxa2xx_cp_reg_map[ri->crn];
    return pxa2xx_pic_mem_read(ri->opaque, offset, 4);
}

static void pxa2xx_pic_cp_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                uint64_t value)
{
    int offset = pxa2xx_cp_reg_map[ri->crn];
    pxa2xx_pic_mem_write(ri->opaque, offset, value, 4);
}

#define REGINFO_FOR_PIC_CP(NAME, CRN) \
    { .name = NAME, .cp = 6, .crn = CRN, .crm = 0, .opc1 = 0, .opc2 = 0, \
      .access = PL1_RW, .type = ARM_CP_IO, \
      .readfn = pxa2xx_pic_cp_read, .writefn = pxa2xx_pic_cp_write }

static const ARMCPRegInfo pxa_pic_cp_reginfo[] = {
    REGINFO_FOR_PIC_CP("ICIP", 0),
    REGINFO_FOR_PIC_CP("ICMR", 1),
    REGINFO_FOR_PIC_CP("ICLR", 2),
    REGINFO_FOR_PIC_CP("ICFP", 3),
    REGINFO_FOR_PIC_CP("ICPR", 4),
    REGINFO_FOR_PIC_CP("ICHP", 5),
    REGINFO_FOR_PIC_CP("ICIP2", 6),
    REGINFO_FOR_PIC_CP("ICMR2", 7),
    REGINFO_FOR_PIC_CP("ICLR2", 8),
    REGINFO_FOR_PIC_CP("ICFP2", 9),
    REGINFO_FOR_PIC_CP("ICPR2", 0xa),
};

static const MemoryRegionOps pxa2xx_pic_ops = {
    .read = pxa2xx_pic_mem_read,
    .write = pxa2xx_pic_mem_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int pxa2xx_pic_post_load(void *opaque, int version_id)
{
    pxa2xx_pic_update(opaque);
    return 0;
}

static void pxa2xx_pic_reset_hold(Object *obj)
{
    PXA2xxPICState *s = PXA2XX_PIC(obj);

    s->int_pending[0] = 0;
    s->int_pending[1] = 0;
    s->int_enabled[0] = 0;
    s->int_enabled[1] = 0;
    s->is_fiq[0] = 0;
    s->is_fiq[1] = 0;
}

DeviceState *pxa2xx_pic_init(hwaddr base, ARMCPU *cpu)
{
    DeviceState *dev = qdev_new(TYPE_PXA2XX_PIC);

    object_property_set_link(OBJECT(dev), "arm-cpu",
                             OBJECT(cpu), &error_abort);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);

    return dev;
}

static void pxa2xx_pic_realize(DeviceState *dev, Error **errp)
{
    PXA2xxPICState *s = PXA2XX_PIC(dev);

    qdev_init_gpio_in(dev, pxa2xx_pic_set_irq, PXA2XX_PIC_SRCS);

    /* Enable IC memory-mapped registers access.  */
    memory_region_init_io(&s->iomem, OBJECT(s), &pxa2xx_pic_ops, s,
                          "pxa2xx-pic", 0x00100000);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);

    /* Enable IC coprocessor access.  */
    define_arm_cp_regs_with_opaque(s->cpu, pxa_pic_cp_reginfo, s);
}

static const VMStateDescription vmstate_pxa2xx_pic_regs = {
    .name = "pxa2xx_pic",
    .version_id = 0,
    .minimum_version_id = 0,
    .post_load = pxa2xx_pic_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(int_enabled, PXA2xxPICState, 2),
        VMSTATE_UINT32_ARRAY(int_pending, PXA2xxPICState, 2),
        VMSTATE_UINT32_ARRAY(is_fiq, PXA2xxPICState, 2),
        VMSTATE_UINT32(int_idle, PXA2xxPICState),
        VMSTATE_UINT32_ARRAY(priority, PXA2xxPICState, PXA2XX_PIC_SRCS),
        VMSTATE_END_OF_LIST(),
    },
};

static Property pxa2xx_pic_properties[] = {
    DEFINE_PROP_LINK("arm-cpu", PXA2xxPICState, cpu,
                     TYPE_ARM_CPU, ARMCPU *),
    DEFINE_PROP_END_OF_LIST(),
};

static void pxa2xx_pic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    device_class_set_props(dc, pxa2xx_pic_properties);
    dc->realize = pxa2xx_pic_realize;
    dc->desc = "PXA2xx PIC";
    dc->vmsd = &vmstate_pxa2xx_pic_regs;
    rc->phases.hold = pxa2xx_pic_reset_hold;
}

static const TypeInfo pxa2xx_pic_info = {
    .name          = TYPE_PXA2XX_PIC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PXA2xxPICState),
    .class_init    = pxa2xx_pic_class_init,
};

static void pxa2xx_pic_register_types(void)
{
    type_register_static(&pxa2xx_pic_info);
}

type_init(pxa2xx_pic_register_types)

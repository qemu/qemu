/*
 * Samsung exynos4210 Interrupt Combiner
 *
 * Copyright (c) 2000 - 2011 Samsung Electronics Co., Ltd.
 * All rights reserved.
 *
 * Evgeny Voevodin <e.voevodin@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Exynos4210 Combiner represents an OR gate for SOC's IRQ lines. It combines
 * IRQ sources into groups and provides signal output to GIC from each group. It
 * is driven by common mask and enable/disable logic. Take a note that not all
 * IRQs are passed to GIC through Combiner.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/module.h"

#include "hw/arm/exynos4210.h"
#include "hw/hw.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"

//#define DEBUG_COMBINER

#ifdef DEBUG_COMBINER
#define DPRINTF(fmt, ...) \
        do { fprintf(stdout, "COMBINER: [%s:%d] " fmt, __func__ , __LINE__, \
                ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) do {} while (0)
#endif

#define    IIC_NGRP        64            /* Internal Interrupt Combiner
                                            Groups number */
#define    IIC_NIRQ        (IIC_NGRP * 8)/* Internal Interrupt Combiner
                                            Interrupts number */
#define IIC_REGION_SIZE    0x108         /* Size of memory mapped region */
#define IIC_REGSET_SIZE    0x41

/*
 * State for each output signal of internal combiner
 */
typedef struct CombinerGroupState {
    uint8_t src_mask;            /* 1 - source enabled, 0 - disabled */
    uint8_t src_pending;        /* Pending source interrupts before masking */
} CombinerGroupState;

#define TYPE_EXYNOS4210_COMBINER "exynos4210.combiner"
#define EXYNOS4210_COMBINER(obj) \
    OBJECT_CHECK(Exynos4210CombinerState, (obj), TYPE_EXYNOS4210_COMBINER)

typedef struct Exynos4210CombinerState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    struct CombinerGroupState group[IIC_NGRP];
    uint32_t reg_set[IIC_REGSET_SIZE];
    uint32_t icipsr[2];
    uint32_t external;          /* 1 means that this combiner is external */

    qemu_irq output_irq[IIC_NGRP];
} Exynos4210CombinerState;

static const VMStateDescription vmstate_exynos4210_combiner_group_state = {
    .name = "exynos4210.combiner.groupstate",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(src_mask, CombinerGroupState),
        VMSTATE_UINT8(src_pending, CombinerGroupState),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_exynos4210_combiner = {
    .name = "exynos4210.combiner",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(group, Exynos4210CombinerState, IIC_NGRP, 0,
                vmstate_exynos4210_combiner_group_state, CombinerGroupState),
        VMSTATE_UINT32_ARRAY(reg_set, Exynos4210CombinerState,
                IIC_REGSET_SIZE),
        VMSTATE_UINT32_ARRAY(icipsr, Exynos4210CombinerState, 2),
        VMSTATE_UINT32(external, Exynos4210CombinerState),
        VMSTATE_END_OF_LIST()
    }
};

/*
 * Get Combiner input GPIO into irqs structure
 */
void exynos4210_combiner_get_gpioin(Exynos4210Irq *irqs, DeviceState *dev,
        int ext)
{
    int n;
    int bit;
    int max;
    qemu_irq *irq;

    max = ext ? EXYNOS4210_MAX_EXT_COMBINER_IN_IRQ :
        EXYNOS4210_MAX_INT_COMBINER_IN_IRQ;
    irq = ext ? irqs->ext_combiner_irq : irqs->int_combiner_irq;

    /*
     * Some IRQs of Int/External Combiner are going to two Combiners groups,
     * so let split them.
     */
    for (n = 0; n < max; n++) {

        bit = EXYNOS4210_COMBINER_GET_BIT_NUM(n);

        switch (n) {
        /* MDNIE_LCD1 INTG1 */
        case EXYNOS4210_COMBINER_GET_IRQ_NUM(1, 0) ...
             EXYNOS4210_COMBINER_GET_IRQ_NUM(1, 3):
            irq[n] = qemu_irq_split(qdev_get_gpio_in(dev, n),
                    irq[EXYNOS4210_COMBINER_GET_IRQ_NUM(0, bit + 4)]);
            continue;

        /* TMU INTG3 */
        case EXYNOS4210_COMBINER_GET_IRQ_NUM(3, 4):
            irq[n] = qemu_irq_split(qdev_get_gpio_in(dev, n),
                    irq[EXYNOS4210_COMBINER_GET_IRQ_NUM(2, bit)]);
            continue;

        /* LCD1 INTG12 */
        case EXYNOS4210_COMBINER_GET_IRQ_NUM(12, 0) ...
             EXYNOS4210_COMBINER_GET_IRQ_NUM(12, 3):
            irq[n] = qemu_irq_split(qdev_get_gpio_in(dev, n),
                    irq[EXYNOS4210_COMBINER_GET_IRQ_NUM(11, bit + 4)]);
            continue;

        /* Multi-Core Timer INTG12 */
        case EXYNOS4210_COMBINER_GET_IRQ_NUM(12, 4) ...
             EXYNOS4210_COMBINER_GET_IRQ_NUM(12, 8):
               irq[n] = qemu_irq_split(qdev_get_gpio_in(dev, n),
                       irq[EXYNOS4210_COMBINER_GET_IRQ_NUM(1, bit + 4)]);
            continue;

        /* Multi-Core Timer INTG35 */
        case EXYNOS4210_COMBINER_GET_IRQ_NUM(35, 4) ...
             EXYNOS4210_COMBINER_GET_IRQ_NUM(35, 8):
            irq[n] = qemu_irq_split(qdev_get_gpio_in(dev, n),
                    irq[EXYNOS4210_COMBINER_GET_IRQ_NUM(1, bit + 4)]);
            continue;

        /* Multi-Core Timer INTG51 */
        case EXYNOS4210_COMBINER_GET_IRQ_NUM(51, 4) ...
             EXYNOS4210_COMBINER_GET_IRQ_NUM(51, 8):
            irq[n] = qemu_irq_split(qdev_get_gpio_in(dev, n),
                    irq[EXYNOS4210_COMBINER_GET_IRQ_NUM(1, bit + 4)]);
            continue;

        /* Multi-Core Timer INTG53 */
        case EXYNOS4210_COMBINER_GET_IRQ_NUM(53, 4) ...
             EXYNOS4210_COMBINER_GET_IRQ_NUM(53, 8):
            irq[n] = qemu_irq_split(qdev_get_gpio_in(dev, n),
                    irq[EXYNOS4210_COMBINER_GET_IRQ_NUM(1, bit + 4)]);
            continue;
        }

        irq[n] = qdev_get_gpio_in(dev, n);
    }
}

static uint64_t
exynos4210_combiner_read(void *opaque, hwaddr offset, unsigned size)
{
    struct Exynos4210CombinerState *s =
            (struct Exynos4210CombinerState *)opaque;
    uint32_t req_quad_base_n;    /* Base of registers quad. Multiply it by 4 and
                                   get a start of corresponding group quad */
    uint32_t grp_quad_base_n;    /* Base of group quad */
    uint32_t reg_n;              /* Register number inside the quad */
    uint32_t val;

    req_quad_base_n = offset >> 4;
    grp_quad_base_n = req_quad_base_n << 2;
    reg_n = (offset - (req_quad_base_n << 4)) >> 2;

    if (req_quad_base_n >= IIC_NGRP) {
        /* Read of ICIPSR register */
        return s->icipsr[reg_n];
    }

    val = 0;

    switch (reg_n) {
    /* IISTR */
    case 2:
        val |= s->group[grp_quad_base_n].src_pending;
        val |= s->group[grp_quad_base_n + 1].src_pending << 8;
        val |= s->group[grp_quad_base_n + 2].src_pending << 16;
        val |= s->group[grp_quad_base_n + 3].src_pending << 24;
        break;
    /* IIMSR */
    case 3:
        val |= s->group[grp_quad_base_n].src_mask &
        s->group[grp_quad_base_n].src_pending;
        val |= (s->group[grp_quad_base_n + 1].src_mask &
                s->group[grp_quad_base_n + 1].src_pending) << 8;
        val |= (s->group[grp_quad_base_n + 2].src_mask &
                s->group[grp_quad_base_n + 2].src_pending) << 16;
        val |= (s->group[grp_quad_base_n + 3].src_mask &
                s->group[grp_quad_base_n + 3].src_pending) << 24;
        break;
    default:
        if (offset >> 2 >= IIC_REGSET_SIZE) {
            hw_error("exynos4210.combiner: overflow of reg_set by 0x"
                    TARGET_FMT_plx "offset\n", offset);
        }
        val = s->reg_set[offset >> 2];
        return 0;
    }
    return val;
}

static void exynos4210_combiner_update(void *opaque, uint8_t group_n)
{
    struct Exynos4210CombinerState *s =
            (struct Exynos4210CombinerState *)opaque;

    /* Send interrupt if needed */
    if (s->group[group_n].src_mask & s->group[group_n].src_pending) {
#ifdef DEBUG_COMBINER
        if (group_n != 26) {
            /* skip uart */
            DPRINTF("%s raise IRQ[%d]\n", s->external ? "EXT" : "INT", group_n);
        }
#endif

        /* Set Combiner interrupt pending status after masking */
        if (group_n >= 32) {
            s->icipsr[1] |= 1 << (group_n - 32);
        } else {
            s->icipsr[0] |= 1 << group_n;
        }

        qemu_irq_raise(s->output_irq[group_n]);
    } else {
#ifdef DEBUG_COMBINER
        if (group_n != 26) {
            /* skip uart */
            DPRINTF("%s lower IRQ[%d]\n", s->external ? "EXT" : "INT", group_n);
        }
#endif

        /* Set Combiner interrupt pending status after masking */
        if (group_n >= 32) {
            s->icipsr[1] &= ~(1 << (group_n - 32));
        } else {
            s->icipsr[0] &= ~(1 << group_n);
        }

        qemu_irq_lower(s->output_irq[group_n]);
    }
}

static void exynos4210_combiner_write(void *opaque, hwaddr offset,
        uint64_t val, unsigned size)
{
    struct Exynos4210CombinerState *s =
            (struct Exynos4210CombinerState *)opaque;
    uint32_t req_quad_base_n;    /* Base of registers quad. Multiply it by 4 and
                                   get a start of corresponding group quad */
    uint32_t grp_quad_base_n;    /* Base of group quad */
    uint32_t reg_n;              /* Register number inside the quad */

    req_quad_base_n = offset >> 4;
    grp_quad_base_n = req_quad_base_n << 2;
    reg_n = (offset - (req_quad_base_n << 4)) >> 2;

    if (req_quad_base_n >= IIC_NGRP) {
        hw_error("exynos4210.combiner: unallowed write access at offset 0x"
                TARGET_FMT_plx "\n", offset);
        return;
    }

    if (reg_n > 1) {
        hw_error("exynos4210.combiner: unallowed write access at offset 0x"
                TARGET_FMT_plx "\n", offset);
        return;
    }

    if (offset >> 2 >= IIC_REGSET_SIZE) {
        hw_error("exynos4210.combiner: overflow of reg_set by 0x"
                TARGET_FMT_plx "offset\n", offset);
    }
    s->reg_set[offset >> 2] = val;

    switch (reg_n) {
    /* IIESR */
    case 0:
        /* FIXME: what if irq is pending, allowed by mask, and we allow it
         * again. Interrupt will rise again! */

        DPRINTF("%s enable IRQ for groups %d, %d, %d, %d\n",
                s->external ? "EXT" : "INT",
                grp_quad_base_n,
                grp_quad_base_n + 1,
                grp_quad_base_n + 2,
                grp_quad_base_n + 3);

        /* Enable interrupt sources */
        s->group[grp_quad_base_n].src_mask |= val & 0xFF;
        s->group[grp_quad_base_n + 1].src_mask |= (val & 0xFF00) >> 8;
        s->group[grp_quad_base_n + 2].src_mask |= (val & 0xFF0000) >> 16;
        s->group[grp_quad_base_n + 3].src_mask |= (val & 0xFF000000) >> 24;

        exynos4210_combiner_update(s, grp_quad_base_n);
        exynos4210_combiner_update(s, grp_quad_base_n + 1);
        exynos4210_combiner_update(s, grp_quad_base_n + 2);
        exynos4210_combiner_update(s, grp_quad_base_n + 3);
        break;
        /* IIECR */
    case 1:
        DPRINTF("%s disable IRQ for groups %d, %d, %d, %d\n",
                s->external ? "EXT" : "INT",
                grp_quad_base_n,
                grp_quad_base_n + 1,
                grp_quad_base_n + 2,
                grp_quad_base_n + 3);

        /* Disable interrupt sources */
        s->group[grp_quad_base_n].src_mask &= ~(val & 0xFF);
        s->group[grp_quad_base_n + 1].src_mask &= ~((val & 0xFF00) >> 8);
        s->group[grp_quad_base_n + 2].src_mask &= ~((val & 0xFF0000) >> 16);
        s->group[grp_quad_base_n + 3].src_mask &= ~((val & 0xFF000000) >> 24);

        exynos4210_combiner_update(s, grp_quad_base_n);
        exynos4210_combiner_update(s, grp_quad_base_n + 1);
        exynos4210_combiner_update(s, grp_quad_base_n + 2);
        exynos4210_combiner_update(s, grp_quad_base_n + 3);
        break;
    default:
        hw_error("exynos4210.combiner: unallowed write access at offset 0x"
                TARGET_FMT_plx "\n", offset);
        break;
    }
}

/* Get combiner group and bit from irq number */
static uint8_t get_combiner_group_and_bit(int irq, uint8_t *bit)
{
    *bit = irq - ((irq >> 3) << 3);
    return irq >> 3;
}

/* Process a change in an external IRQ input.  */
static void exynos4210_combiner_handler(void *opaque, int irq, int level)
{
    struct Exynos4210CombinerState *s =
            (struct Exynos4210CombinerState *)opaque;
    uint8_t bit_n, group_n;

    group_n = get_combiner_group_and_bit(irq, &bit_n);

    if (s->external && group_n >= EXYNOS4210_MAX_EXT_COMBINER_OUT_IRQ) {
        DPRINTF("%s unallowed IRQ group 0x%x\n", s->external ? "EXT" : "INT"
                , group_n);
        return;
    }

    if (level) {
        s->group[group_n].src_pending |= 1 << bit_n;
    } else {
        s->group[group_n].src_pending &= ~(1 << bit_n);
    }

    exynos4210_combiner_update(s, group_n);
}

static void exynos4210_combiner_reset(DeviceState *d)
{
    struct Exynos4210CombinerState *s = (struct Exynos4210CombinerState *)d;

    memset(&s->group, 0, sizeof(s->group));
    memset(&s->reg_set, 0, sizeof(s->reg_set));

    s->reg_set[0xC0 >> 2] = 0x01010101;
    s->reg_set[0xC4 >> 2] = 0x01010101;
    s->reg_set[0xD0 >> 2] = 0x01010101;
    s->reg_set[0xD4 >> 2] = 0x01010101;
}

static const MemoryRegionOps exynos4210_combiner_ops = {
    .read = exynos4210_combiner_read,
    .write = exynos4210_combiner_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

/*
 * Internal Combiner initialization.
 */
static void exynos4210_combiner_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    Exynos4210CombinerState *s = EXYNOS4210_COMBINER(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    unsigned int i;

    /* Allocate general purpose input signals and connect a handler to each of
     * them */
    qdev_init_gpio_in(dev, exynos4210_combiner_handler, IIC_NIRQ);

    /* Connect SysBusDev irqs to device specific irqs */
    for (i = 0; i < IIC_NGRP; i++) {
        sysbus_init_irq(sbd, &s->output_irq[i]);
    }

    memory_region_init_io(&s->iomem, obj, &exynos4210_combiner_ops, s,
                          "exynos4210-combiner", IIC_REGION_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static Property exynos4210_combiner_properties[] = {
    DEFINE_PROP_UINT32("external", Exynos4210CombinerState, external, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void exynos4210_combiner_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = exynos4210_combiner_reset;
    dc->props = exynos4210_combiner_properties;
    dc->vmsd = &vmstate_exynos4210_combiner;
}

static const TypeInfo exynos4210_combiner_info = {
    .name          = TYPE_EXYNOS4210_COMBINER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Exynos4210CombinerState),
    .instance_init = exynos4210_combiner_init,
    .class_init    = exynos4210_combiner_class_init,
};

static void exynos4210_combiner_register_types(void)
{
    type_register_static(&exynos4210_combiner_info);
}

type_init(exynos4210_combiner_register_types)

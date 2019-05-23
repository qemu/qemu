/*
 * TI OMAP interrupt controller emulation.
 *
 * Copyright (C) 2006-2008 Andrzej Zaborowski  <balrog@zabor.org>
 * Copyright (C) 2007-2008 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/arm/omap.h"
#include "hw/sysbus.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qapi/error.h"

/* Interrupt Handlers */
struct omap_intr_handler_bank_s {
    uint32_t irqs;
    uint32_t inputs;
    uint32_t mask;
    uint32_t fiq;
    uint32_t sens_edge;
    uint32_t swi;
    unsigned char priority[32];
};

#define TYPE_OMAP_INTC "common-omap-intc"
#define OMAP_INTC(obj) \
    OBJECT_CHECK(struct omap_intr_handler_s, (obj), TYPE_OMAP_INTC)

struct omap_intr_handler_s {
    SysBusDevice parent_obj;

    qemu_irq *pins;
    qemu_irq parent_intr[2];
    MemoryRegion mmio;
    void *iclk;
    void *fclk;
    unsigned char nbanks;
    int level_only;
    uint32_t size;

    uint8_t revision;

    /* state */
    uint32_t new_agr[2];
    int sir_intr[2];
    int autoidle;
    uint32_t mask;
    struct omap_intr_handler_bank_s bank[3];
};

static void omap_inth_sir_update(struct omap_intr_handler_s *s, int is_fiq)
{
    int i, j, sir_intr, p_intr, p;
    uint32_t level;
    sir_intr = 0;
    p_intr = 255;

    /* Find the interrupt line with the highest dynamic priority.
     * Note: 0 denotes the hightest priority.
     * If all interrupts have the same priority, the default order is IRQ_N,
     * IRQ_N-1,...,IRQ_0. */
    for (j = 0; j < s->nbanks; ++j) {
        level = s->bank[j].irqs & ~s->bank[j].mask &
                (is_fiq ? s->bank[j].fiq : ~s->bank[j].fiq);

        while (level != 0) {
            i = ctz32(level);
            p = s->bank[j].priority[i];
            if (p <= p_intr) {
                p_intr = p;
                sir_intr = 32 * j + i;
            }
            level &= level - 1;
        }
    }
    s->sir_intr[is_fiq] = sir_intr;
}

static inline void omap_inth_update(struct omap_intr_handler_s *s, int is_fiq)
{
    int i;
    uint32_t has_intr = 0;

    for (i = 0; i < s->nbanks; ++i)
        has_intr |= s->bank[i].irqs & ~s->bank[i].mask &
                (is_fiq ? s->bank[i].fiq : ~s->bank[i].fiq);

    if (s->new_agr[is_fiq] & has_intr & s->mask) {
        s->new_agr[is_fiq] = 0;
        omap_inth_sir_update(s, is_fiq);
        qemu_set_irq(s->parent_intr[is_fiq], 1);
    }
}

#define INT_FALLING_EDGE	0
#define INT_LOW_LEVEL		1

static void omap_set_intr(void *opaque, int irq, int req)
{
    struct omap_intr_handler_s *ih = (struct omap_intr_handler_s *) opaque;
    uint32_t rise;

    struct omap_intr_handler_bank_s *bank = &ih->bank[irq >> 5];
    int n = irq & 31;

    if (req) {
        rise = ~bank->irqs & (1 << n);
        if (~bank->sens_edge & (1 << n))
            rise &= ~bank->inputs;

        bank->inputs |= (1 << n);
        if (rise) {
            bank->irqs |= rise;
            omap_inth_update(ih, 0);
            omap_inth_update(ih, 1);
        }
    } else {
        rise = bank->sens_edge & bank->irqs & (1 << n);
        bank->irqs &= ~rise;
        bank->inputs &= ~(1 << n);
    }
}

/* Simplified version with no edge detection */
static void omap_set_intr_noedge(void *opaque, int irq, int req)
{
    struct omap_intr_handler_s *ih = (struct omap_intr_handler_s *) opaque;
    uint32_t rise;

    struct omap_intr_handler_bank_s *bank = &ih->bank[irq >> 5];
    int n = irq & 31;

    if (req) {
        rise = ~bank->inputs & (1 << n);
        if (rise) {
            bank->irqs |= bank->inputs |= rise;
            omap_inth_update(ih, 0);
            omap_inth_update(ih, 1);
        }
    } else
        bank->irqs = (bank->inputs &= ~(1 << n)) | bank->swi;
}

static uint64_t omap_inth_read(void *opaque, hwaddr addr,
                               unsigned size)
{
    struct omap_intr_handler_s *s = (struct omap_intr_handler_s *) opaque;
    int i, offset = addr;
    int bank_no = offset >> 8;
    int line_no;
    struct omap_intr_handler_bank_s *bank = &s->bank[bank_no];
    offset &= 0xff;

    switch (offset) {
    case 0x00:	/* ITR */
        return bank->irqs;

    case 0x04:	/* MIR */
        return bank->mask;

    case 0x10:	/* SIR_IRQ_CODE */
    case 0x14:  /* SIR_FIQ_CODE */
        if (bank_no != 0)
            break;
        line_no = s->sir_intr[(offset - 0x10) >> 2];
        bank = &s->bank[line_no >> 5];
        i = line_no & 31;
        if (((bank->sens_edge >> i) & 1) == INT_FALLING_EDGE)
            bank->irqs &= ~(1 << i);
        return line_no;

    case 0x18:	/* CONTROL_REG */
        if (bank_no != 0)
            break;
        return 0;

    case 0x1c:	/* ILR0 */
    case 0x20:	/* ILR1 */
    case 0x24:	/* ILR2 */
    case 0x28:	/* ILR3 */
    case 0x2c:	/* ILR4 */
    case 0x30:	/* ILR5 */
    case 0x34:	/* ILR6 */
    case 0x38:	/* ILR7 */
    case 0x3c:	/* ILR8 */
    case 0x40:	/* ILR9 */
    case 0x44:	/* ILR10 */
    case 0x48:	/* ILR11 */
    case 0x4c:	/* ILR12 */
    case 0x50:	/* ILR13 */
    case 0x54:	/* ILR14 */
    case 0x58:	/* ILR15 */
    case 0x5c:	/* ILR16 */
    case 0x60:	/* ILR17 */
    case 0x64:	/* ILR18 */
    case 0x68:	/* ILR19 */
    case 0x6c:	/* ILR20 */
    case 0x70:	/* ILR21 */
    case 0x74:	/* ILR22 */
    case 0x78:	/* ILR23 */
    case 0x7c:	/* ILR24 */
    case 0x80:	/* ILR25 */
    case 0x84:	/* ILR26 */
    case 0x88:	/* ILR27 */
    case 0x8c:	/* ILR28 */
    case 0x90:	/* ILR29 */
    case 0x94:	/* ILR30 */
    case 0x98:	/* ILR31 */
        i = (offset - 0x1c) >> 2;
        return (bank->priority[i] << 2) |
                (((bank->sens_edge >> i) & 1) << 1) |
                ((bank->fiq >> i) & 1);

    case 0x9c:	/* ISR */
        return 0x00000000;

    }
    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_inth_write(void *opaque, hwaddr addr,
                            uint64_t value, unsigned size)
{
    struct omap_intr_handler_s *s = (struct omap_intr_handler_s *) opaque;
    int i, offset = addr;
    int bank_no = offset >> 8;
    struct omap_intr_handler_bank_s *bank = &s->bank[bank_no];
    offset &= 0xff;

    switch (offset) {
    case 0x00:	/* ITR */
        /* Important: ignore the clearing if the IRQ is level-triggered and
           the input bit is 1 */
        bank->irqs &= value | (bank->inputs & bank->sens_edge);
        return;

    case 0x04:	/* MIR */
        bank->mask = value;
        omap_inth_update(s, 0);
        omap_inth_update(s, 1);
        return;

    case 0x10:	/* SIR_IRQ_CODE */
    case 0x14:	/* SIR_FIQ_CODE */
        OMAP_RO_REG(addr);
        break;

    case 0x18:	/* CONTROL_REG */
        if (bank_no != 0)
            break;
        if (value & 2) {
            qemu_set_irq(s->parent_intr[1], 0);
            s->new_agr[1] = ~0;
            omap_inth_update(s, 1);
        }
        if (value & 1) {
            qemu_set_irq(s->parent_intr[0], 0);
            s->new_agr[0] = ~0;
            omap_inth_update(s, 0);
        }
        return;

    case 0x1c:	/* ILR0 */
    case 0x20:	/* ILR1 */
    case 0x24:	/* ILR2 */
    case 0x28:	/* ILR3 */
    case 0x2c:	/* ILR4 */
    case 0x30:	/* ILR5 */
    case 0x34:	/* ILR6 */
    case 0x38:	/* ILR7 */
    case 0x3c:	/* ILR8 */
    case 0x40:	/* ILR9 */
    case 0x44:	/* ILR10 */
    case 0x48:	/* ILR11 */
    case 0x4c:	/* ILR12 */
    case 0x50:	/* ILR13 */
    case 0x54:	/* ILR14 */
    case 0x58:	/* ILR15 */
    case 0x5c:	/* ILR16 */
    case 0x60:	/* ILR17 */
    case 0x64:	/* ILR18 */
    case 0x68:	/* ILR19 */
    case 0x6c:	/* ILR20 */
    case 0x70:	/* ILR21 */
    case 0x74:	/* ILR22 */
    case 0x78:	/* ILR23 */
    case 0x7c:	/* ILR24 */
    case 0x80:	/* ILR25 */
    case 0x84:	/* ILR26 */
    case 0x88:	/* ILR27 */
    case 0x8c:	/* ILR28 */
    case 0x90:	/* ILR29 */
    case 0x94:	/* ILR30 */
    case 0x98:	/* ILR31 */
        i = (offset - 0x1c) >> 2;
        bank->priority[i] = (value >> 2) & 0x1f;
        bank->sens_edge &= ~(1 << i);
        bank->sens_edge |= ((value >> 1) & 1) << i;
        bank->fiq &= ~(1 << i);
        bank->fiq |= (value & 1) << i;
        return;

    case 0x9c:	/* ISR */
        for (i = 0; i < 32; i ++)
            if (value & (1 << i)) {
                omap_set_intr(s, 32 * bank_no + i, 1);
                return;
            }
        return;
    }
    OMAP_BAD_REG(addr);
}

static const MemoryRegionOps omap_inth_mem_ops = {
    .read = omap_inth_read,
    .write = omap_inth_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void omap_inth_reset(DeviceState *dev)
{
    struct omap_intr_handler_s *s = OMAP_INTC(dev);
    int i;

    for (i = 0; i < s->nbanks; ++i){
        s->bank[i].irqs = 0x00000000;
        s->bank[i].mask = 0xffffffff;
        s->bank[i].sens_edge = 0x00000000;
        s->bank[i].fiq = 0x00000000;
        s->bank[i].inputs = 0x00000000;
        s->bank[i].swi = 0x00000000;
        memset(s->bank[i].priority, 0, sizeof(s->bank[i].priority));

        if (s->level_only)
            s->bank[i].sens_edge = 0xffffffff;
    }

    s->new_agr[0] = ~0;
    s->new_agr[1] = ~0;
    s->sir_intr[0] = 0;
    s->sir_intr[1] = 0;
    s->autoidle = 0;
    s->mask = ~0;

    qemu_set_irq(s->parent_intr[0], 0);
    qemu_set_irq(s->parent_intr[1], 0);
}

static void omap_intc_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    struct omap_intr_handler_s *s = OMAP_INTC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    s->nbanks = 1;
    sysbus_init_irq(sbd, &s->parent_intr[0]);
    sysbus_init_irq(sbd, &s->parent_intr[1]);
    qdev_init_gpio_in(dev, omap_set_intr, s->nbanks * 32);
    memory_region_init_io(&s->mmio, obj, &omap_inth_mem_ops, s,
                          "omap-intc", s->size);
    sysbus_init_mmio(sbd, &s->mmio);
}

static void omap_intc_realize(DeviceState *dev, Error **errp)
{
    struct omap_intr_handler_s *s = OMAP_INTC(dev);

    if (!s->iclk) {
        error_setg(errp, "omap-intc: clk not connected");
    }
}

static Property omap_intc_properties[] = {
    DEFINE_PROP_UINT32("size", struct omap_intr_handler_s, size, 0x100),
    DEFINE_PROP_PTR("clk", struct omap_intr_handler_s, iclk),
    DEFINE_PROP_END_OF_LIST(),
};

static void omap_intc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = omap_inth_reset;
    dc->props = omap_intc_properties;
    /* Reason: pointer property "clk" */
    dc->user_creatable = false;
    dc->realize = omap_intc_realize;
}

static const TypeInfo omap_intc_info = {
    .name          = "omap-intc",
    .parent        = TYPE_OMAP_INTC,
    .instance_init = omap_intc_init,
    .class_init    = omap_intc_class_init,
};

static uint64_t omap2_inth_read(void *opaque, hwaddr addr,
                                unsigned size)
{
    struct omap_intr_handler_s *s = (struct omap_intr_handler_s *) opaque;
    int offset = addr;
    int bank_no, line_no;
    struct omap_intr_handler_bank_s *bank = NULL;

    if ((offset & 0xf80) == 0x80) {
        bank_no = (offset & 0x60) >> 5;
        if (bank_no < s->nbanks) {
            offset &= ~0x60;
            bank = &s->bank[bank_no];
        } else {
            OMAP_BAD_REG(addr);
            return 0;
        }
    }

    switch (offset) {
    case 0x00:	/* INTC_REVISION */
        return s->revision;

    case 0x10:	/* INTC_SYSCONFIG */
        return (s->autoidle >> 2) & 1;

    case 0x14:	/* INTC_SYSSTATUS */
        return 1;						/* RESETDONE */

    case 0x40:	/* INTC_SIR_IRQ */
        return s->sir_intr[0];

    case 0x44:	/* INTC_SIR_FIQ */
        return s->sir_intr[1];

    case 0x48:	/* INTC_CONTROL */
        return (!s->mask) << 2;					/* GLOBALMASK */

    case 0x4c:	/* INTC_PROTECTION */
        return 0;

    case 0x50:	/* INTC_IDLE */
        return s->autoidle & 3;

    /* Per-bank registers */
    case 0x80:	/* INTC_ITR */
        return bank->inputs;

    case 0x84:	/* INTC_MIR */
        return bank->mask;

    case 0x88:	/* INTC_MIR_CLEAR */
    case 0x8c:	/* INTC_MIR_SET */
        return 0;

    case 0x90:	/* INTC_ISR_SET */
        return bank->swi;

    case 0x94:	/* INTC_ISR_CLEAR */
        return 0;

    case 0x98:	/* INTC_PENDING_IRQ */
        return bank->irqs & ~bank->mask & ~bank->fiq;

    case 0x9c:	/* INTC_PENDING_FIQ */
        return bank->irqs & ~bank->mask & bank->fiq;

    /* Per-line registers */
    case 0x100 ... 0x300:	/* INTC_ILR */
        bank_no = (offset - 0x100) >> 7;
        if (bank_no > s->nbanks)
            break;
        bank = &s->bank[bank_no];
        line_no = (offset & 0x7f) >> 2;
        return (bank->priority[line_no] << 2) |
                ((bank->fiq >> line_no) & 1);
    }
    OMAP_BAD_REG(addr);
    return 0;
}

static void omap2_inth_write(void *opaque, hwaddr addr,
                             uint64_t value, unsigned size)
{
    struct omap_intr_handler_s *s = (struct omap_intr_handler_s *) opaque;
    int offset = addr;
    int bank_no, line_no;
    struct omap_intr_handler_bank_s *bank = NULL;

    if ((offset & 0xf80) == 0x80) {
        bank_no = (offset & 0x60) >> 5;
        if (bank_no < s->nbanks) {
            offset &= ~0x60;
            bank = &s->bank[bank_no];
        } else {
            OMAP_BAD_REG(addr);
            return;
        }
    }

    switch (offset) {
    case 0x10:	/* INTC_SYSCONFIG */
        s->autoidle &= 4;
        s->autoidle |= (value & 1) << 2;
        if (value & 2) {                                        /* SOFTRESET */
            omap_inth_reset(DEVICE(s));
        }
        return;

    case 0x48:	/* INTC_CONTROL */
        s->mask = (value & 4) ? 0 : ~0;				/* GLOBALMASK */
        if (value & 2) {					/* NEWFIQAGR */
            qemu_set_irq(s->parent_intr[1], 0);
            s->new_agr[1] = ~0;
            omap_inth_update(s, 1);
        }
        if (value & 1) {					/* NEWIRQAGR */
            qemu_set_irq(s->parent_intr[0], 0);
            s->new_agr[0] = ~0;
            omap_inth_update(s, 0);
        }
        return;

    case 0x4c:	/* INTC_PROTECTION */
        /* TODO: Make a bitmap (or sizeof(char)map) of access privileges
         * for every register, see Chapter 3 and 4 for privileged mode.  */
        if (value & 1)
            fprintf(stderr, "%s: protection mode enable attempt\n",
                            __func__);
        return;

    case 0x50:	/* INTC_IDLE */
        s->autoidle &= ~3;
        s->autoidle |= value & 3;
        return;

    /* Per-bank registers */
    case 0x84:	/* INTC_MIR */
        bank->mask = value;
        omap_inth_update(s, 0);
        omap_inth_update(s, 1);
        return;

    case 0x88:	/* INTC_MIR_CLEAR */
        bank->mask &= ~value;
        omap_inth_update(s, 0);
        omap_inth_update(s, 1);
        return;

    case 0x8c:	/* INTC_MIR_SET */
        bank->mask |= value;
        return;

    case 0x90:	/* INTC_ISR_SET */
        bank->irqs |= bank->swi |= value;
        omap_inth_update(s, 0);
        omap_inth_update(s, 1);
        return;

    case 0x94:	/* INTC_ISR_CLEAR */
        bank->swi &= ~value;
        bank->irqs = bank->swi & bank->inputs;
        return;

    /* Per-line registers */
    case 0x100 ... 0x300:	/* INTC_ILR */
        bank_no = (offset - 0x100) >> 7;
        if (bank_no > s->nbanks)
            break;
        bank = &s->bank[bank_no];
        line_no = (offset & 0x7f) >> 2;
        bank->priority[line_no] = (value >> 2) & 0x3f;
        bank->fiq &= ~(1 << line_no);
        bank->fiq |= (value & 1) << line_no;
        return;

    case 0x00:	/* INTC_REVISION */
    case 0x14:	/* INTC_SYSSTATUS */
    case 0x40:	/* INTC_SIR_IRQ */
    case 0x44:	/* INTC_SIR_FIQ */
    case 0x80:	/* INTC_ITR */
    case 0x98:	/* INTC_PENDING_IRQ */
    case 0x9c:	/* INTC_PENDING_FIQ */
        OMAP_RO_REG(addr);
        return;
    }
    OMAP_BAD_REG(addr);
}

static const MemoryRegionOps omap2_inth_mem_ops = {
    .read = omap2_inth_read,
    .write = omap2_inth_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void omap2_intc_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    struct omap_intr_handler_s *s = OMAP_INTC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    s->level_only = 1;
    s->nbanks = 3;
    sysbus_init_irq(sbd, &s->parent_intr[0]);
    sysbus_init_irq(sbd, &s->parent_intr[1]);
    qdev_init_gpio_in(dev, omap_set_intr_noedge, s->nbanks * 32);
    memory_region_init_io(&s->mmio, obj, &omap2_inth_mem_ops, s,
                          "omap2-intc", 0x1000);
    sysbus_init_mmio(sbd, &s->mmio);
}

static void omap2_intc_realize(DeviceState *dev, Error **errp)
{
    struct omap_intr_handler_s *s = OMAP_INTC(dev);

    if (!s->iclk) {
        error_setg(errp, "omap2-intc: iclk not connected");
        return;
    }
    if (!s->fclk) {
        error_setg(errp, "omap2-intc: fclk not connected");
        return;
    }
}

static Property omap2_intc_properties[] = {
    DEFINE_PROP_UINT8("revision", struct omap_intr_handler_s,
    revision, 0x21),
    DEFINE_PROP_PTR("iclk", struct omap_intr_handler_s, iclk),
    DEFINE_PROP_PTR("fclk", struct omap_intr_handler_s, fclk),
    DEFINE_PROP_END_OF_LIST(),
};

static void omap2_intc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = omap_inth_reset;
    dc->props = omap2_intc_properties;
    /* Reason: pointer property "iclk", "fclk" */
    dc->user_creatable = false;
    dc->realize = omap2_intc_realize;
}

static const TypeInfo omap2_intc_info = {
    .name          = "omap2-intc",
    .parent        = TYPE_OMAP_INTC,
    .instance_init = omap2_intc_init,
    .class_init    = omap2_intc_class_init,
};

static const TypeInfo omap_intc_type_info = {
    .name          = TYPE_OMAP_INTC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(struct omap_intr_handler_s),
    .abstract      = true,
};

static void omap_intc_register_types(void)
{
    type_register_static(&omap_intc_type_info);
    type_register_static(&omap_intc_info);
    type_register_static(&omap2_intc_info);
}

type_init(omap_intc_register_types)

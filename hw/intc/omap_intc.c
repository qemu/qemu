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
#include "hw/irq.h"
#include "hw/qdev-properties.h"
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

struct OMAPIntcState {
    SysBusDevice parent_obj;

    qemu_irq *pins;
    qemu_irq parent_intr[2];
    MemoryRegion mmio;
    void *iclk;
    void *fclk;
    unsigned char nbanks;
    int level_only;
    uint32_t size;

    /* state */
    uint32_t new_agr[2];
    int sir_intr[2];
    int autoidle;
    uint32_t mask;
    struct omap_intr_handler_bank_s bank[3];
};

static void omap_inth_sir_update(OMAPIntcState *s, int is_fiq)
{
    int i, j, sir_intr, p_intr, p;
    uint32_t level;
    sir_intr = 0;
    p_intr = 255;

    /* Find the interrupt line with the highest dynamic priority.
     * Note: 0 denotes the highest priority.
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

static inline void omap_inth_update(OMAPIntcState *s, int is_fiq)
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

#define INT_FALLING_EDGE    0
#define INT_LOW_LEVEL       1

static void omap_set_intr(void *opaque, int irq, int req)
{
    OMAPIntcState *ih = opaque;
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

static uint64_t omap_inth_read(void *opaque, hwaddr addr,
                               unsigned size)
{
    OMAPIntcState *s = opaque;
    int i, offset = addr;
    int bank_no = offset >> 8;
    int line_no;
    struct omap_intr_handler_bank_s *bank = &s->bank[bank_no];
    offset &= 0xff;

    switch (offset) {
    case 0x00:  /* ITR */
        return bank->irqs;

    case 0x04:  /* MIR */
        return bank->mask;

    case 0x10:  /* SIR_IRQ_CODE */
    case 0x14:  /* SIR_FIQ_CODE */
        if (bank_no != 0)
            break;
        line_no = s->sir_intr[(offset - 0x10) >> 2];
        bank = &s->bank[line_no >> 5];
        i = line_no & 31;
        if (((bank->sens_edge >> i) & 1) == INT_FALLING_EDGE)
            bank->irqs &= ~(1 << i);
        return line_no;

    case 0x18:  /* CONTROL_REG */
        if (bank_no != 0)
            break;
        return 0;

    case 0x1c:  /* ILR0 */
    case 0x20:  /* ILR1 */
    case 0x24:  /* ILR2 */
    case 0x28:  /* ILR3 */
    case 0x2c:  /* ILR4 */
    case 0x30:  /* ILR5 */
    case 0x34:  /* ILR6 */
    case 0x38:  /* ILR7 */
    case 0x3c:  /* ILR8 */
    case 0x40:  /* ILR9 */
    case 0x44:  /* ILR10 */
    case 0x48:  /* ILR11 */
    case 0x4c:  /* ILR12 */
    case 0x50:  /* ILR13 */
    case 0x54:  /* ILR14 */
    case 0x58:  /* ILR15 */
    case 0x5c:  /* ILR16 */
    case 0x60:  /* ILR17 */
    case 0x64:  /* ILR18 */
    case 0x68:  /* ILR19 */
    case 0x6c:  /* ILR20 */
    case 0x70:  /* ILR21 */
    case 0x74:  /* ILR22 */
    case 0x78:  /* ILR23 */
    case 0x7c:  /* ILR24 */
    case 0x80:  /* ILR25 */
    case 0x84:  /* ILR26 */
    case 0x88:  /* ILR27 */
    case 0x8c:  /* ILR28 */
    case 0x90:  /* ILR29 */
    case 0x94:  /* ILR30 */
    case 0x98:  /* ILR31 */
        i = (offset - 0x1c) >> 2;
        return (bank->priority[i] << 2) |
                (((bank->sens_edge >> i) & 1) << 1) |
                ((bank->fiq >> i) & 1);

    case 0x9c:  /* ISR */
        return 0x00000000;

    }
    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_inth_write(void *opaque, hwaddr addr,
                            uint64_t value, unsigned size)
{
    OMAPIntcState *s = opaque;
    int i, offset = addr;
    int bank_no = offset >> 8;
    struct omap_intr_handler_bank_s *bank = &s->bank[bank_no];
    offset &= 0xff;

    switch (offset) {
    case 0x00:  /* ITR */
        /* Important: ignore the clearing if the IRQ is level-triggered and
           the input bit is 1 */
        bank->irqs &= value | (bank->inputs & bank->sens_edge);
        return;

    case 0x04:  /* MIR */
        bank->mask = value;
        omap_inth_update(s, 0);
        omap_inth_update(s, 1);
        return;

    case 0x10:  /* SIR_IRQ_CODE */
    case 0x14:  /* SIR_FIQ_CODE */
        OMAP_RO_REG(addr);
        break;

    case 0x18:  /* CONTROL_REG */
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

    case 0x1c:  /* ILR0 */
    case 0x20:  /* ILR1 */
    case 0x24:  /* ILR2 */
    case 0x28:  /* ILR3 */
    case 0x2c:  /* ILR4 */
    case 0x30:  /* ILR5 */
    case 0x34:  /* ILR6 */
    case 0x38:  /* ILR7 */
    case 0x3c:  /* ILR8 */
    case 0x40:  /* ILR9 */
    case 0x44:  /* ILR10 */
    case 0x48:  /* ILR11 */
    case 0x4c:  /* ILR12 */
    case 0x50:  /* ILR13 */
    case 0x54:  /* ILR14 */
    case 0x58:  /* ILR15 */
    case 0x5c:  /* ILR16 */
    case 0x60:  /* ILR17 */
    case 0x64:  /* ILR18 */
    case 0x68:  /* ILR19 */
    case 0x6c:  /* ILR20 */
    case 0x70:  /* ILR21 */
    case 0x74:  /* ILR22 */
    case 0x78:  /* ILR23 */
    case 0x7c:  /* ILR24 */
    case 0x80:  /* ILR25 */
    case 0x84:  /* ILR26 */
    case 0x88:  /* ILR27 */
    case 0x8c:  /* ILR28 */
    case 0x90:  /* ILR29 */
    case 0x94:  /* ILR30 */
    case 0x98:  /* ILR31 */
        i = (offset - 0x1c) >> 2;
        bank->priority[i] = (value >> 2) & 0x1f;
        bank->sens_edge &= ~(1 << i);
        bank->sens_edge |= ((value >> 1) & 1) << i;
        bank->fiq &= ~(1 << i);
        bank->fiq |= (value & 1) << i;
        return;

    case 0x9c:  /* ISR */
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
    OMAPIntcState *s = OMAP_INTC(dev);
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
    OMAPIntcState *s = OMAP_INTC(obj);
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
    OMAPIntcState *s = OMAP_INTC(dev);

    if (!s->iclk) {
        error_setg(errp, "omap-intc: clk not connected");
    }
}

void omap_intc_set_iclk(OMAPIntcState *intc, omap_clk clk)
{
    intc->iclk = clk;
}

void omap_intc_set_fclk(OMAPIntcState *intc, omap_clk clk)
{
    intc->fclk = clk;
}

static const Property omap_intc_properties[] = {
    DEFINE_PROP_UINT32("size", OMAPIntcState, size, 0x100),
};

static void omap_intc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, omap_inth_reset);
    device_class_set_props(dc, omap_intc_properties);
    /* Reason: pointer property "clk" */
    dc->user_creatable = false;
    dc->realize = omap_intc_realize;
}

static const TypeInfo omap_intc_info = {
    .name          = TYPE_OMAP_INTC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(OMAPIntcState),
    .instance_init = omap_intc_init,
    .class_init    = omap_intc_class_init,
};

static void omap_intc_register_types(void)
{
    type_register_static(&omap_intc_info);
}

type_init(omap_intc_register_types)

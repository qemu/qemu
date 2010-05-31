/*
 * TI OMAP processors GPIO emulation.
 *
 * Copyright (C) 2006-2008 Andrzej Zaborowski  <balrog@zabor.org>
 * Copyright (C) 2007-2009 Nokia Corporation
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

#include "hw.h"
#include "omap.h"
/* General-Purpose I/O */
struct omap_gpio_s {
    qemu_irq irq;
    qemu_irq *in;
    qemu_irq handler[16];

    uint16_t inputs;
    uint16_t outputs;
    uint16_t dir;
    uint16_t edge;
    uint16_t mask;
    uint16_t ints;
    uint16_t pins;
};

static void omap_gpio_set(void *opaque, int line, int level)
{
    struct omap_gpio_s *s = (struct omap_gpio_s *) opaque;
    uint16_t prev = s->inputs;

    if (level)
        s->inputs |= 1 << line;
    else
        s->inputs &= ~(1 << line);

    if (((s->edge & s->inputs & ~prev) | (~s->edge & ~s->inputs & prev)) &
                    (1 << line) & s->dir & ~s->mask) {
        s->ints |= 1 << line;
        qemu_irq_raise(s->irq);
    }
}

static uint32_t omap_gpio_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_gpio_s *s = (struct omap_gpio_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;

    switch (offset) {
    case 0x00:	/* DATA_INPUT */
        return s->inputs & s->pins;

    case 0x04:	/* DATA_OUTPUT */
        return s->outputs;

    case 0x08:	/* DIRECTION_CONTROL */
        return s->dir;

    case 0x0c:	/* INTERRUPT_CONTROL */
        return s->edge;

    case 0x10:	/* INTERRUPT_MASK */
        return s->mask;

    case 0x14:	/* INTERRUPT_STATUS */
        return s->ints;

    case 0x18:	/* PIN_CONTROL (not in OMAP310) */
        OMAP_BAD_REG(addr);
        return s->pins;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_gpio_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_gpio_s *s = (struct omap_gpio_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;
    uint16_t diff;
    int ln;

    switch (offset) {
    case 0x00:	/* DATA_INPUT */
        OMAP_RO_REG(addr);
        return;

    case 0x04:	/* DATA_OUTPUT */
        diff = (s->outputs ^ value) & ~s->dir;
        s->outputs = value;
        while ((ln = ffs(diff))) {
            ln --;
            if (s->handler[ln])
                qemu_set_irq(s->handler[ln], (value >> ln) & 1);
            diff &= ~(1 << ln);
        }
        break;

    case 0x08:	/* DIRECTION_CONTROL */
        diff = s->outputs & (s->dir ^ value);
        s->dir = value;

        value = s->outputs & ~s->dir;
        while ((ln = ffs(diff))) {
            ln --;
            if (s->handler[ln])
                qemu_set_irq(s->handler[ln], (value >> ln) & 1);
            diff &= ~(1 << ln);
        }
        break;

    case 0x0c:	/* INTERRUPT_CONTROL */
        s->edge = value;
        break;

    case 0x10:	/* INTERRUPT_MASK */
        s->mask = value;
        break;

    case 0x14:	/* INTERRUPT_STATUS */
        s->ints &= ~value;
        if (!s->ints)
            qemu_irq_lower(s->irq);
        break;

    case 0x18:	/* PIN_CONTROL (not in OMAP310 TRM) */
        OMAP_BAD_REG(addr);
        s->pins = value;
        break;

    default:
        OMAP_BAD_REG(addr);
        return;
    }
}

/* *Some* sources say the memory region is 32-bit.  */
static CPUReadMemoryFunc * const omap_gpio_readfn[] = {
    omap_badwidth_read16,
    omap_gpio_read,
    omap_badwidth_read16,
};

static CPUWriteMemoryFunc * const omap_gpio_writefn[] = {
    omap_badwidth_write16,
    omap_gpio_write,
    omap_badwidth_write16,
};

void omap_gpio_reset(struct omap_gpio_s *s)
{
    s->inputs = 0;
    s->outputs = ~0;
    s->dir = ~0;
    s->edge = ~0;
    s->mask = ~0;
    s->ints = 0;
    s->pins = ~0;
}

struct omap_gpio_s *omap_gpio_init(target_phys_addr_t base,
                qemu_irq irq, omap_clk clk)
{
    int iomemtype;
    struct omap_gpio_s *s = (struct omap_gpio_s *)
            qemu_mallocz(sizeof(struct omap_gpio_s));

    s->irq = irq;
    s->in = qemu_allocate_irqs(omap_gpio_set, s, 16);
    omap_gpio_reset(s);

    iomemtype = cpu_register_io_memory(omap_gpio_readfn,
                    omap_gpio_writefn, s);
    cpu_register_physical_memory(base, 0x1000, iomemtype);

    return s;
}

qemu_irq *omap_gpio_in_get(struct omap_gpio_s *s)
{
    return s->in;
}

void omap_gpio_out_set(struct omap_gpio_s *s, int line, qemu_irq handler)
{
    if (line >= 16 || line < 0)
        hw_error("%s: No GPIO line %i\n", __FUNCTION__, line);
    s->handler[line] = handler;
}

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
                    omap_gpio_writefn, s, DEVICE_NATIVE_ENDIAN);
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

/* General-Purpose Interface of OMAP2 */
struct omap2_gpio_s {
    qemu_irq irq[2];
    qemu_irq wkup;
    qemu_irq *in;
    qemu_irq handler[32];

    uint8_t config[2];
    uint32_t inputs;
    uint32_t outputs;
    uint32_t dir;
    uint32_t level[2];
    uint32_t edge[2];
    uint32_t mask[2];
    uint32_t wumask;
    uint32_t ints[2];
    uint32_t debounce;
    uint8_t delay;
};

static inline void omap2_gpio_module_int_update(struct omap2_gpio_s *s,
                int line)
{
    qemu_set_irq(s->irq[line], s->ints[line] & s->mask[line]);
}

static void omap2_gpio_module_wake(struct omap2_gpio_s *s, int line)
{
    if (!(s->config[0] & (1 << 2)))			/* ENAWAKEUP */
        return;
    if (!(s->config[0] & (3 << 3)))			/* Force Idle */
        return;
    if (!(s->wumask & (1 << line)))
        return;

    qemu_irq_raise(s->wkup);
}

static inline void omap2_gpio_module_out_update(struct omap2_gpio_s *s,
                uint32_t diff)
{
    int ln;

    s->outputs ^= diff;
    diff &= ~s->dir;
    while ((ln = ffs(diff))) {
        ln --;
        qemu_set_irq(s->handler[ln], (s->outputs >> ln) & 1);
        diff &= ~(1 << ln);
    }
}

static void omap2_gpio_module_level_update(struct omap2_gpio_s *s, int line)
{
    s->ints[line] |= s->dir &
            ((s->inputs & s->level[1]) | (~s->inputs & s->level[0]));
    omap2_gpio_module_int_update(s, line);
}

static inline void omap2_gpio_module_int(struct omap2_gpio_s *s, int line)
{
    s->ints[0] |= 1 << line;
    omap2_gpio_module_int_update(s, 0);
    s->ints[1] |= 1 << line;
    omap2_gpio_module_int_update(s, 1);
    omap2_gpio_module_wake(s, line);
}

static void omap2_gpio_module_set(void *opaque, int line, int level)
{
    struct omap2_gpio_s *s = (struct omap2_gpio_s *) opaque;

    if (level) {
        if (s->dir & (1 << line) & ((~s->inputs & s->edge[0]) | s->level[1]))
            omap2_gpio_module_int(s, line);
        s->inputs |= 1 << line;
    } else {
        if (s->dir & (1 << line) & ((s->inputs & s->edge[1]) | s->level[0]))
            omap2_gpio_module_int(s, line);
        s->inputs &= ~(1 << line);
    }
}

static void omap2_gpio_module_reset(struct omap2_gpio_s *s)
{
    s->config[0] = 0;
    s->config[1] = 2;
    s->ints[0] = 0;
    s->ints[1] = 0;
    s->mask[0] = 0;
    s->mask[1] = 0;
    s->wumask = 0;
    s->dir = ~0;
    s->level[0] = 0;
    s->level[1] = 0;
    s->edge[0] = 0;
    s->edge[1] = 0;
    s->debounce = 0;
    s->delay = 0;
}

static uint32_t omap2_gpio_module_read(void *opaque, target_phys_addr_t addr)
{
    struct omap2_gpio_s *s = (struct omap2_gpio_s *) opaque;

    switch (addr) {
    case 0x00:	/* GPIO_REVISION */
        return 0x18;

    case 0x10:	/* GPIO_SYSCONFIG */
        return s->config[0];

    case 0x14:	/* GPIO_SYSSTATUS */
        return 0x01;

    case 0x18:	/* GPIO_IRQSTATUS1 */
        return s->ints[0];

    case 0x1c:	/* GPIO_IRQENABLE1 */
    case 0x60:	/* GPIO_CLEARIRQENABLE1 */
    case 0x64:	/* GPIO_SETIRQENABLE1 */
        return s->mask[0];

    case 0x20:	/* GPIO_WAKEUPENABLE */
    case 0x80:	/* GPIO_CLEARWKUENA */
    case 0x84:	/* GPIO_SETWKUENA */
        return s->wumask;

    case 0x28:	/* GPIO_IRQSTATUS2 */
        return s->ints[1];

    case 0x2c:	/* GPIO_IRQENABLE2 */
    case 0x70:	/* GPIO_CLEARIRQENABLE2 */
    case 0x74:	/* GPIO_SETIREQNEABLE2 */
        return s->mask[1];

    case 0x30:	/* GPIO_CTRL */
        return s->config[1];

    case 0x34:	/* GPIO_OE */
        return s->dir;

    case 0x38:	/* GPIO_DATAIN */
        return s->inputs;

    case 0x3c:	/* GPIO_DATAOUT */
    case 0x90:	/* GPIO_CLEARDATAOUT */
    case 0x94:	/* GPIO_SETDATAOUT */
        return s->outputs;

    case 0x40:	/* GPIO_LEVELDETECT0 */
        return s->level[0];

    case 0x44:	/* GPIO_LEVELDETECT1 */
        return s->level[1];

    case 0x48:	/* GPIO_RISINGDETECT */
        return s->edge[0];

    case 0x4c:	/* GPIO_FALLINGDETECT */
        return s->edge[1];

    case 0x50:	/* GPIO_DEBOUNCENABLE */
        return s->debounce;

    case 0x54:	/* GPIO_DEBOUNCINGTIME */
        return s->delay;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap2_gpio_module_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap2_gpio_s *s = (struct omap2_gpio_s *) opaque;
    uint32_t diff;
    int ln;

    switch (addr) {
    case 0x00:	/* GPIO_REVISION */
    case 0x14:	/* GPIO_SYSSTATUS */
    case 0x38:	/* GPIO_DATAIN */
        OMAP_RO_REG(addr);
        break;

    case 0x10:	/* GPIO_SYSCONFIG */
        if (((value >> 3) & 3) == 3)
            fprintf(stderr, "%s: bad IDLEMODE value\n", __FUNCTION__);
        if (value & 2)
            omap2_gpio_module_reset(s);
        s->config[0] = value & 0x1d;
        break;

    case 0x18:	/* GPIO_IRQSTATUS1 */
        if (s->ints[0] & value) {
            s->ints[0] &= ~value;
            omap2_gpio_module_level_update(s, 0);
        }
        break;

    case 0x1c:	/* GPIO_IRQENABLE1 */
        s->mask[0] = value;
        omap2_gpio_module_int_update(s, 0);
        break;

    case 0x20:	/* GPIO_WAKEUPENABLE */
        s->wumask = value;
        break;

    case 0x28:	/* GPIO_IRQSTATUS2 */
        if (s->ints[1] & value) {
            s->ints[1] &= ~value;
            omap2_gpio_module_level_update(s, 1);
        }
        break;

    case 0x2c:	/* GPIO_IRQENABLE2 */
        s->mask[1] = value;
        omap2_gpio_module_int_update(s, 1);
        break;

    case 0x30:	/* GPIO_CTRL */
        s->config[1] = value & 7;
        break;

    case 0x34:	/* GPIO_OE */
        diff = s->outputs & (s->dir ^ value);
        s->dir = value;

        value = s->outputs & ~s->dir;
        while ((ln = ffs(diff))) {
            diff &= ~(1 <<-- ln);
            qemu_set_irq(s->handler[ln], (value >> ln) & 1);
        }

        omap2_gpio_module_level_update(s, 0);
        omap2_gpio_module_level_update(s, 1);
        break;

    case 0x3c:	/* GPIO_DATAOUT */
        omap2_gpio_module_out_update(s, s->outputs ^ value);
        break;

    case 0x40:	/* GPIO_LEVELDETECT0 */
        s->level[0] = value;
        omap2_gpio_module_level_update(s, 0);
        omap2_gpio_module_level_update(s, 1);
        break;

    case 0x44:	/* GPIO_LEVELDETECT1 */
        s->level[1] = value;
        omap2_gpio_module_level_update(s, 0);
        omap2_gpio_module_level_update(s, 1);
        break;

    case 0x48:	/* GPIO_RISINGDETECT */
        s->edge[0] = value;
        break;

    case 0x4c:	/* GPIO_FALLINGDETECT */
        s->edge[1] = value;
        break;

    case 0x50:	/* GPIO_DEBOUNCENABLE */
        s->debounce = value;
        break;

    case 0x54:	/* GPIO_DEBOUNCINGTIME */
        s->delay = value;
        break;

    case 0x60:	/* GPIO_CLEARIRQENABLE1 */
        s->mask[0] &= ~value;
        omap2_gpio_module_int_update(s, 0);
        break;

    case 0x64:	/* GPIO_SETIRQENABLE1 */
        s->mask[0] |= value;
        omap2_gpio_module_int_update(s, 0);
        break;

    case 0x70:	/* GPIO_CLEARIRQENABLE2 */
        s->mask[1] &= ~value;
        omap2_gpio_module_int_update(s, 1);
        break;

    case 0x74:	/* GPIO_SETIREQNEABLE2 */
        s->mask[1] |= value;
        omap2_gpio_module_int_update(s, 1);
        break;

    case 0x80:	/* GPIO_CLEARWKUENA */
        s->wumask &= ~value;
        break;

    case 0x84:	/* GPIO_SETWKUENA */
        s->wumask |= value;
        break;

    case 0x90:	/* GPIO_CLEARDATAOUT */
        omap2_gpio_module_out_update(s, s->outputs & value);
        break;

    case 0x94:	/* GPIO_SETDATAOUT */
        omap2_gpio_module_out_update(s, ~s->outputs & value);
        break;

    default:
        OMAP_BAD_REG(addr);
        return;
    }
}

static uint32_t omap2_gpio_module_readp(void *opaque, target_phys_addr_t addr)
{
    return omap2_gpio_module_readp(opaque, addr) >> ((addr & 3) << 3);
}

static void omap2_gpio_module_writep(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    uint32_t cur = 0;
    uint32_t mask = 0xffff;

    switch (addr & ~3) {
    case 0x00:	/* GPIO_REVISION */
    case 0x14:	/* GPIO_SYSSTATUS */
    case 0x38:	/* GPIO_DATAIN */
        OMAP_RO_REG(addr);
        break;

    case 0x10:	/* GPIO_SYSCONFIG */
    case 0x1c:	/* GPIO_IRQENABLE1 */
    case 0x20:	/* GPIO_WAKEUPENABLE */
    case 0x2c:	/* GPIO_IRQENABLE2 */
    case 0x30:	/* GPIO_CTRL */
    case 0x34:	/* GPIO_OE */
    case 0x3c:	/* GPIO_DATAOUT */
    case 0x40:	/* GPIO_LEVELDETECT0 */
    case 0x44:	/* GPIO_LEVELDETECT1 */
    case 0x48:	/* GPIO_RISINGDETECT */
    case 0x4c:	/* GPIO_FALLINGDETECT */
    case 0x50:	/* GPIO_DEBOUNCENABLE */
    case 0x54:	/* GPIO_DEBOUNCINGTIME */
        cur = omap2_gpio_module_read(opaque, addr & ~3) &
                ~(mask << ((addr & 3) << 3));

        /* Fall through.  */
    case 0x18:	/* GPIO_IRQSTATUS1 */
    case 0x28:	/* GPIO_IRQSTATUS2 */
    case 0x60:	/* GPIO_CLEARIRQENABLE1 */
    case 0x64:	/* GPIO_SETIRQENABLE1 */
    case 0x70:	/* GPIO_CLEARIRQENABLE2 */
    case 0x74:	/* GPIO_SETIREQNEABLE2 */
    case 0x80:	/* GPIO_CLEARWKUENA */
    case 0x84:	/* GPIO_SETWKUENA */
    case 0x90:	/* GPIO_CLEARDATAOUT */
    case 0x94:	/* GPIO_SETDATAOUT */
        value <<= (addr & 3) << 3;
        omap2_gpio_module_write(opaque, addr, cur | value);
        break;

    default:
        OMAP_BAD_REG(addr);
        return;
    }
}

static CPUReadMemoryFunc * const omap2_gpio_module_readfn[] = {
    omap2_gpio_module_readp,
    omap2_gpio_module_readp,
    omap2_gpio_module_read,
};

static CPUWriteMemoryFunc * const omap2_gpio_module_writefn[] = {
    omap2_gpio_module_writep,
    omap2_gpio_module_writep,
    omap2_gpio_module_write,
};

static void omap2_gpio_module_init(struct omap2_gpio_s *s,
                struct omap_target_agent_s *ta, int region,
                qemu_irq mpu, qemu_irq dsp, qemu_irq wkup,
                omap_clk fclk, omap_clk iclk)
{
    int iomemtype;

    s->irq[0] = mpu;
    s->irq[1] = dsp;
    s->wkup = wkup;
    s->in = qemu_allocate_irqs(omap2_gpio_module_set, s, 32);

    iomemtype = l4_register_io_memory(omap2_gpio_module_readfn,
                    omap2_gpio_module_writefn, s);
    omap_l4_attach(ta, region, iomemtype);
}

struct omap_gpif_s {
    struct omap2_gpio_s module[5];
    int modules;

    int autoidle;
    int gpo;
};

void omap_gpif_reset(struct omap_gpif_s *s)
{
    int i;

    for (i = 0; i < s->modules; i ++)
        omap2_gpio_module_reset(s->module + i);

    s->autoidle = 0;
    s->gpo = 0;
}

static uint32_t omap_gpif_top_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_gpif_s *s = (struct omap_gpif_s *) opaque;

    switch (addr) {
    case 0x00:	/* IPGENERICOCPSPL_REVISION */
        return 0x18;

    case 0x10:	/* IPGENERICOCPSPL_SYSCONFIG */
        return s->autoidle;

    case 0x14:	/* IPGENERICOCPSPL_SYSSTATUS */
        return 0x01;

    case 0x18:	/* IPGENERICOCPSPL_IRQSTATUS */
        return 0x00;

    case 0x40:	/* IPGENERICOCPSPL_GPO */
        return s->gpo;

    case 0x50:	/* IPGENERICOCPSPL_GPI */
        return 0x00;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_gpif_top_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_gpif_s *s = (struct omap_gpif_s *) opaque;

    switch (addr) {
    case 0x00:	/* IPGENERICOCPSPL_REVISION */
    case 0x14:	/* IPGENERICOCPSPL_SYSSTATUS */
    case 0x18:	/* IPGENERICOCPSPL_IRQSTATUS */
    case 0x50:	/* IPGENERICOCPSPL_GPI */
        OMAP_RO_REG(addr);
        break;

    case 0x10:	/* IPGENERICOCPSPL_SYSCONFIG */
        if (value & (1 << 1))					/* SOFTRESET */
            omap_gpif_reset(s);
        s->autoidle = value & 1;
        break;

    case 0x40:	/* IPGENERICOCPSPL_GPO */
        s->gpo = value & 1;
        break;

    default:
        OMAP_BAD_REG(addr);
        return;
    }
}

static CPUReadMemoryFunc * const omap_gpif_top_readfn[] = {
    omap_gpif_top_read,
    omap_gpif_top_read,
    omap_gpif_top_read,
};

static CPUWriteMemoryFunc * const omap_gpif_top_writefn[] = {
    omap_gpif_top_write,
    omap_gpif_top_write,
    omap_gpif_top_write,
};

struct omap_gpif_s *omap2_gpio_init(struct omap_target_agent_s *ta,
                qemu_irq *irq, omap_clk *fclk, omap_clk iclk, int modules)
{
    int iomemtype, i;
    struct omap_gpif_s *s = (struct omap_gpif_s *)
            qemu_mallocz(sizeof(struct omap_gpif_s));
    int region[4] = { 0, 2, 4, 5 };

    s->modules = modules;
    for (i = 0; i < modules; i ++)
        omap2_gpio_module_init(s->module + i, ta, region[i],
                              irq[i], NULL, NULL, fclk[i], iclk);

    omap_gpif_reset(s);

    iomemtype = l4_register_io_memory(omap_gpif_top_readfn,
                    omap_gpif_top_writefn, s);
    omap_l4_attach(ta, 1, iomemtype);

    return s;
}

qemu_irq *omap2_gpio_in_get(struct omap_gpif_s *s, int start)
{
    if (start >= s->modules * 32 || start < 0)
        hw_error("%s: No GPIO line %i\n", __FUNCTION__, start);
    return s->module[start >> 5].in + (start & 31);
}

void omap2_gpio_out_set(struct omap_gpif_s *s, int line, qemu_irq handler)
{
    if (line >= s->modules * 32 || line < 0)
        hw_error("%s: No GPIO line %i\n", __FUNCTION__, line);
    s->module[line >> 5].handler[line & 31] = handler;
}

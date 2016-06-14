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

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/arm/omap.h"
#include "hw/sysbus.h"
#include "qemu/error-report.h"
#include "qapi/error.h"

struct omap_gpio_s {
    qemu_irq irq;
    qemu_irq handler[16];

    uint16_t inputs;
    uint16_t outputs;
    uint16_t dir;
    uint16_t edge;
    uint16_t mask;
    uint16_t ints;
    uint16_t pins;
};

#define TYPE_OMAP1_GPIO "omap-gpio"
#define OMAP1_GPIO(obj) \
    OBJECT_CHECK(struct omap_gpif_s, (obj), TYPE_OMAP1_GPIO)

struct omap_gpif_s {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    int mpu_model;
    void *clk;
    struct omap_gpio_s omap1;
};

/* General-Purpose I/O of OMAP1 */
static void omap_gpio_set(void *opaque, int line, int level)
{
    struct omap_gpio_s *s = &((struct omap_gpif_s *) opaque)->omap1;
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

static uint64_t omap_gpio_read(void *opaque, hwaddr addr,
                               unsigned size)
{
    struct omap_gpio_s *s = (struct omap_gpio_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;

    if (size != 2) {
        return omap_badwidth_read16(opaque, addr);
    }

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

static void omap_gpio_write(void *opaque, hwaddr addr,
                            uint64_t value, unsigned size)
{
    struct omap_gpio_s *s = (struct omap_gpio_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;
    uint16_t diff;
    int ln;

    if (size != 2) {
        omap_badwidth_write16(opaque, addr, value);
        return;
    }

    switch (offset) {
    case 0x00:	/* DATA_INPUT */
        OMAP_RO_REG(addr);
        return;

    case 0x04:	/* DATA_OUTPUT */
        diff = (s->outputs ^ value) & ~s->dir;
        s->outputs = value;
        while ((ln = ctz32(diff)) != 32) {
            if (s->handler[ln])
                qemu_set_irq(s->handler[ln], (value >> ln) & 1);
            diff &= ~(1 << ln);
        }
        break;

    case 0x08:	/* DIRECTION_CONTROL */
        diff = s->outputs & (s->dir ^ value);
        s->dir = value;

        value = s->outputs & ~s->dir;
        while ((ln = ctz32(diff)) != 32) {
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
static const MemoryRegionOps omap_gpio_ops = {
    .read = omap_gpio_read,
    .write = omap_gpio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void omap_gpio_reset(struct omap_gpio_s *s)
{
    s->inputs = 0;
    s->outputs = ~0;
    s->dir = ~0;
    s->edge = ~0;
    s->mask = ~0;
    s->ints = 0;
    s->pins = ~0;
}

struct omap2_gpio_s {
    qemu_irq irq[2];
    qemu_irq wkup;
    qemu_irq *handler;
    MemoryRegion iomem;

    uint8_t revision;
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

#define TYPE_OMAP2_GPIO "omap2-gpio"
#define OMAP2_GPIO(obj) \
    OBJECT_CHECK(struct omap2_gpif_s, (obj), TYPE_OMAP2_GPIO)

struct omap2_gpif_s {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    int mpu_model;
    void *iclk;
    void *fclk[6];
    int modulecount;
    struct omap2_gpio_s *modules;
    qemu_irq *handler;
    int autoidle;
    int gpo;
};

/* General-Purpose Interface of OMAP2/3 */
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
    while ((ln = ctz32(diff)) != 32) {
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

static void omap2_gpio_set(void *opaque, int line, int level)
{
    struct omap2_gpif_s *p = opaque;
    struct omap2_gpio_s *s = &p->modules[line >> 5];

    line &= 31;
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

static uint32_t omap2_gpio_module_read(void *opaque, hwaddr addr)
{
    struct omap2_gpio_s *s = (struct omap2_gpio_s *) opaque;

    switch (addr) {
    case 0x00:	/* GPIO_REVISION */
        return s->revision;

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

static void omap2_gpio_module_write(void *opaque, hwaddr addr,
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
        while ((ln = ctz32(diff)) != 32) {
            diff &= ~(1 << ln);
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

static uint32_t omap2_gpio_module_readp(void *opaque, hwaddr addr)
{
    return omap2_gpio_module_read(opaque, addr & ~3) >> ((addr & 3) << 3);
}

static void omap2_gpio_module_writep(void *opaque, hwaddr addr,
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

static const MemoryRegionOps omap2_gpio_module_ops = {
    .old_mmio = {
        .read = {
            omap2_gpio_module_readp,
            omap2_gpio_module_readp,
            omap2_gpio_module_read,
        },
        .write = {
            omap2_gpio_module_writep,
            omap2_gpio_module_writep,
            omap2_gpio_module_write,
        },
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void omap_gpif_reset(DeviceState *dev)
{
    struct omap_gpif_s *s = OMAP1_GPIO(dev);

    omap_gpio_reset(&s->omap1);
}

static void omap2_gpif_reset(DeviceState *dev)
{
    struct omap2_gpif_s *s = OMAP2_GPIO(dev);
    int i;

    for (i = 0; i < s->modulecount; i++) {
        omap2_gpio_module_reset(&s->modules[i]);
    }
    s->autoidle = 0;
    s->gpo = 0;
}

static uint64_t omap2_gpif_top_read(void *opaque, hwaddr addr,
                                    unsigned size)
{
    struct omap2_gpif_s *s = (struct omap2_gpif_s *) opaque;

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

static void omap2_gpif_top_write(void *opaque, hwaddr addr,
                                 uint64_t value, unsigned size)
{
    struct omap2_gpif_s *s = (struct omap2_gpif_s *) opaque;

    switch (addr) {
    case 0x00:	/* IPGENERICOCPSPL_REVISION */
    case 0x14:	/* IPGENERICOCPSPL_SYSSTATUS */
    case 0x18:	/* IPGENERICOCPSPL_IRQSTATUS */
    case 0x50:	/* IPGENERICOCPSPL_GPI */
        OMAP_RO_REG(addr);
        break;

    case 0x10:	/* IPGENERICOCPSPL_SYSCONFIG */
        if (value & (1 << 1))					/* SOFTRESET */
            omap2_gpif_reset(DEVICE(s));
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

static const MemoryRegionOps omap2_gpif_top_ops = {
    .read = omap2_gpif_top_read,
    .write = omap2_gpif_top_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void omap_gpio_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    struct omap_gpif_s *s = OMAP1_GPIO(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    qdev_init_gpio_in(dev, omap_gpio_set, 16);
    qdev_init_gpio_out(dev, s->omap1.handler, 16);
    sysbus_init_irq(sbd, &s->omap1.irq);
    memory_region_init_io(&s->iomem, obj, &omap_gpio_ops, &s->omap1,
                          "omap.gpio", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void omap_gpio_realize(DeviceState *dev, Error **errp)
{
    struct omap_gpif_s *s = OMAP1_GPIO(dev);

    if (!s->clk) {
        error_setg(errp, "omap-gpio: clk not connected");
    }
}

static void omap2_gpio_realize(DeviceState *dev, Error **errp)
{
    struct omap2_gpif_s *s = OMAP2_GPIO(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    int i;

    if (!s->iclk) {
        error_setg(errp, "omap2-gpio: iclk not connected");
        return;
    }

    s->modulecount = s->mpu_model < omap2430 ? 4
        : s->mpu_model < omap3430 ? 5
        : 6;

    if (s->mpu_model < omap3430) {
        memory_region_init_io(&s->iomem, OBJECT(dev), &omap2_gpif_top_ops, s,
                              "omap2.gpio", 0x1000);
        sysbus_init_mmio(sbd, &s->iomem);
    }

    s->modules = g_new0(struct omap2_gpio_s, s->modulecount);
    s->handler = g_new0(qemu_irq, s->modulecount * 32);
    qdev_init_gpio_in(dev, omap2_gpio_set, s->modulecount * 32);
    qdev_init_gpio_out(dev, s->handler, s->modulecount * 32);

    for (i = 0; i < s->modulecount; i++) {
        struct omap2_gpio_s *m = &s->modules[i];

        if (!s->fclk[i]) {
            error_setg(errp, "omap2-gpio: fclk%d not connected", i);
            return;
        }

        m->revision = (s->mpu_model < omap3430) ? 0x18 : 0x25;
        m->handler = &s->handler[i * 32];
        sysbus_init_irq(sbd, &m->irq[0]); /* mpu irq */
        sysbus_init_irq(sbd, &m->irq[1]); /* dsp irq */
        sysbus_init_irq(sbd, &m->wkup);
        memory_region_init_io(&m->iomem, OBJECT(dev), &omap2_gpio_module_ops, m,
                              "omap.gpio-module", 0x1000);
        sysbus_init_mmio(sbd, &m->iomem);
    }
}

/* Using qdev pointer properties for the clocks is not ideal.
 * qdev should support a generic means of defining a 'port' with
 * an arbitrary interface for connecting two devices. Then we
 * could reframe the omap clock API in terms of clock ports,
 * and get some type safety. For now the best qdev provides is
 * passing an arbitrary pointer.
 * (It's not possible to pass in the string which is the clock
 * name, because this device does not have the necessary information
 * (ie the struct omap_mpu_state_s*) to do the clockname to pointer
 * translation.)
 */

static Property omap_gpio_properties[] = {
    DEFINE_PROP_INT32("mpu_model", struct omap_gpif_s, mpu_model, 0),
    DEFINE_PROP_PTR("clk", struct omap_gpif_s, clk),
    DEFINE_PROP_END_OF_LIST(),
};

static void omap_gpio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = omap_gpio_realize;
    dc->reset = omap_gpif_reset;
    dc->props = omap_gpio_properties;
    /* Reason: pointer property "clk" */
    dc->cannot_instantiate_with_device_add_yet = true;
}

static const TypeInfo omap_gpio_info = {
    .name          = TYPE_OMAP1_GPIO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(struct omap_gpif_s),
    .instance_init = omap_gpio_init,
    .class_init    = omap_gpio_class_init,
};

static Property omap2_gpio_properties[] = {
    DEFINE_PROP_INT32("mpu_model", struct omap2_gpif_s, mpu_model, 0),
    DEFINE_PROP_PTR("iclk", struct omap2_gpif_s, iclk),
    DEFINE_PROP_PTR("fclk0", struct omap2_gpif_s, fclk[0]),
    DEFINE_PROP_PTR("fclk1", struct omap2_gpif_s, fclk[1]),
    DEFINE_PROP_PTR("fclk2", struct omap2_gpif_s, fclk[2]),
    DEFINE_PROP_PTR("fclk3", struct omap2_gpif_s, fclk[3]),
    DEFINE_PROP_PTR("fclk4", struct omap2_gpif_s, fclk[4]),
    DEFINE_PROP_PTR("fclk5", struct omap2_gpif_s, fclk[5]),
    DEFINE_PROP_END_OF_LIST(),
};

static void omap2_gpio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = omap2_gpio_realize;
    dc->reset = omap2_gpif_reset;
    dc->props = omap2_gpio_properties;
    /* Reason: pointer properties "iclk", "fclk0", ..., "fclk5" */
    dc->cannot_instantiate_with_device_add_yet = true;
}

static const TypeInfo omap2_gpio_info = {
    .name          = TYPE_OMAP2_GPIO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(struct omap2_gpif_s),
    .class_init    = omap2_gpio_class_init,
};

static void omap_gpio_register_types(void)
{
    type_register_static(&omap_gpio_info);
    type_register_static(&omap2_gpio_info);
}

type_init(omap_gpio_register_types)

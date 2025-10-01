/*
 * Copyright (c) 2006-2008 Openedhand Ltd.
 * Written by Andrzej Zaborowski <balrog@zabor.org>
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
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qom/object.h"

/* SCOOP devices */

#define TYPE_SCOOP "scoop"
OBJECT_DECLARE_SIMPLE_TYPE(ScoopInfo, SCOOP)

struct ScoopInfo {
    SysBusDevice parent_obj;

    qemu_irq handler[16];
    MemoryRegion iomem;
    uint16_t status;
    uint16_t power;
    uint32_t gpio_level;
    uint32_t gpio_dir;
    uint32_t prev_level;

    uint16_t mcr;
    uint16_t cdr;
    uint16_t ccr;
    uint16_t irr;
    uint16_t imr;
    uint16_t isr;
};

#define SCOOP_MCR       0x00
#define SCOOP_CDR       0x04
#define SCOOP_CSR       0x08
#define SCOOP_CPR       0x0c
#define SCOOP_CCR       0x10
#define SCOOP_IRR_IRM   0x14
#define SCOOP_IMR       0x18
#define SCOOP_ISR       0x1c
#define SCOOP_GPCR      0x20
#define SCOOP_GPWR      0x24
#define SCOOP_GPRR      0x28

static inline void scoop_gpio_handler_update(ScoopInfo *s)
{
    uint32_t level, diff;
    int bit;
    level = s->gpio_level & s->gpio_dir;

    for (diff = s->prev_level ^ level; diff; diff ^= 1 << bit) {
        bit = ctz32(diff);
        qemu_set_irq(s->handler[bit], (level >> bit) & 1);
    }

    s->prev_level = level;
}

static uint64_t scoop_read(void *opaque, hwaddr addr,
                           unsigned size)
{
    ScoopInfo *s = (ScoopInfo *) opaque;

    switch (addr & 0x3f) {
    case SCOOP_MCR:
        return s->mcr;
    case SCOOP_CDR:
        return s->cdr;
    case SCOOP_CSR:
        return s->status;
    case SCOOP_CPR:
        return s->power;
    case SCOOP_CCR:
        return s->ccr;
    case SCOOP_IRR_IRM:
        return s->irr;
    case SCOOP_IMR:
        return s->imr;
    case SCOOP_ISR:
        return s->isr;
    case SCOOP_GPCR:
        return s->gpio_dir;
    case SCOOP_GPWR:
    case SCOOP_GPRR:
        return s->gpio_level;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "scoop_read: bad register offset 0x%02" HWADDR_PRIx "\n",
                      addr);
    }

    return 0;
}

static void scoop_write(void *opaque, hwaddr addr,
                        uint64_t value, unsigned size)
{
    ScoopInfo *s = (ScoopInfo *) opaque;
    value &= 0xffff;

    switch (addr & 0x3f) {
    case SCOOP_MCR:
        s->mcr = value;
        break;
    case SCOOP_CDR:
        s->cdr = value;
        break;
    case SCOOP_CPR:
        s->power = value;
        if (value & 0x80) {
            s->power |= 0x8040;
        }
        break;
    case SCOOP_CCR:
        s->ccr = value;
        break;
    case SCOOP_IRR_IRM:
        s->irr = value;
        break;
    case SCOOP_IMR:
        s->imr = value;
        break;
    case SCOOP_ISR:
        s->isr = value;
        break;
    case SCOOP_GPCR:
        s->gpio_dir = value;
        scoop_gpio_handler_update(s);
        break;
    case SCOOP_GPWR:
    case SCOOP_GPRR:    /* GPRR is probably R/O in real HW */
        s->gpio_level = value & s->gpio_dir;
        scoop_gpio_handler_update(s);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "scoop_write: bad register offset 0x%02" HWADDR_PRIx "\n",
                      addr);
    }
}

static const MemoryRegionOps scoop_ops = {
    .read = scoop_read,
    .write = scoop_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void scoop_gpio_set(void *opaque, int line, int level)
{
    ScoopInfo *s = (ScoopInfo *) opaque;

    if (level) {
        s->gpio_level |= (1 << line);
    } else {
        s->gpio_level &= ~(1 << line);
    }
}

static void scoop_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    ScoopInfo *s = SCOOP(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    s->status = 0x02;
    qdev_init_gpio_out(dev, s->handler, 16);
    qdev_init_gpio_in(dev, scoop_gpio_set, 16);
    memory_region_init_io(&s->iomem, obj, &scoop_ops, s, "scoop", 0x1000);

    sysbus_init_mmio(sbd, &s->iomem);
}

static int scoop_post_load(void *opaque, int version_id)
{
    ScoopInfo *s = (ScoopInfo *) opaque;
    int i;
    uint32_t level;

    level = s->gpio_level & s->gpio_dir;

    for (i = 0; i < 16; i++) {
        qemu_set_irq(s->handler[i], (level >> i) & 1);
    }

    s->prev_level = level;

    return 0;
}

static bool is_version_0(void *opaque, int version_id)
{
    return version_id == 0;
}

static bool vmstate_scoop_validate(void *opaque, int version_id)
{
    ScoopInfo *s = opaque;

    return !(s->prev_level & 0xffff0000) &&
        !(s->gpio_level & 0xffff0000) &&
        !(s->gpio_dir & 0xffff0000);
}

static const VMStateDescription vmstate_scoop_regs = {
    .name = "scoop",
    .version_id = 1,
    .minimum_version_id = 0,
    .post_load = scoop_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT16(status, ScoopInfo),
        VMSTATE_UINT16(power, ScoopInfo),
        VMSTATE_UINT32(gpio_level, ScoopInfo),
        VMSTATE_UINT32(gpio_dir, ScoopInfo),
        VMSTATE_UINT32(prev_level, ScoopInfo),
        VMSTATE_VALIDATE("irq levels are 16 bit", vmstate_scoop_validate),
        VMSTATE_UINT16(mcr, ScoopInfo),
        VMSTATE_UINT16(cdr, ScoopInfo),
        VMSTATE_UINT16(ccr, ScoopInfo),
        VMSTATE_UINT16(irr, ScoopInfo),
        VMSTATE_UINT16(imr, ScoopInfo),
        VMSTATE_UINT16(isr, ScoopInfo),
        VMSTATE_UNUSED_TEST(is_version_0, 2),
        VMSTATE_END_OF_LIST(),
    },
};

static void scoop_sysbus_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Scoop2 Sharp custom ASIC";
    dc->vmsd = &vmstate_scoop_regs;
}

static const TypeInfo scoop_sysbus_info = {
    .name          = TYPE_SCOOP,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ScoopInfo),
    .instance_init = scoop_init,
    .class_init    = scoop_sysbus_class_init,
};

static void scoop_register_types(void)
{
    type_register_static(&scoop_sysbus_info);
}

type_init(scoop_register_types)

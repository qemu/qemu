/*
 *  QEMU model of the Milkymist System Controller.
 *
 *  Copyright (c) 2010 Michael Walle <michael@walle.cc>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Specification available at:
 *   http://milkymist.walle.cc/socdoc/ac97.pdf
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "trace.h"
#include "audio/audio.h"
#include "qemu/error-report.h"
#include "qemu/module.h"

enum {
    R_AC97_CTRL = 0,
    R_AC97_ADDR,
    R_AC97_DATAOUT,
    R_AC97_DATAIN,
    R_D_CTRL,
    R_D_ADDR,
    R_D_REMAINING,
    R_RESERVED,
    R_U_CTRL,
    R_U_ADDR,
    R_U_REMAINING,
    R_MAX
};

enum {
    AC97_CTRL_RQEN  = (1<<0),
    AC97_CTRL_WRITE = (1<<1),
};

enum {
    CTRL_EN = (1<<0),
};

#define TYPE_MILKYMIST_AC97 "milkymist-ac97"
#define MILKYMIST_AC97(obj) \
    OBJECT_CHECK(MilkymistAC97State, (obj), TYPE_MILKYMIST_AC97)

struct MilkymistAC97State {
    SysBusDevice parent_obj;

    MemoryRegion regs_region;

    QEMUSoundCard card;
    SWVoiceIn *voice_in;
    SWVoiceOut *voice_out;

    uint32_t regs[R_MAX];

    qemu_irq crrequest_irq;
    qemu_irq crreply_irq;
    qemu_irq dmar_irq;
    qemu_irq dmaw_irq;
};
typedef struct MilkymistAC97State MilkymistAC97State;

static void update_voices(MilkymistAC97State *s)
{
    if (s->regs[R_D_CTRL] & CTRL_EN) {
        AUD_set_active_out(s->voice_out, 1);
    } else {
        AUD_set_active_out(s->voice_out, 0);
    }

    if (s->regs[R_U_CTRL] & CTRL_EN) {
        AUD_set_active_in(s->voice_in, 1);
    } else {
        AUD_set_active_in(s->voice_in, 0);
    }
}

static uint64_t ac97_read(void *opaque, hwaddr addr,
                          unsigned size)
{
    MilkymistAC97State *s = opaque;
    uint32_t r = 0;

    addr >>= 2;
    switch (addr) {
    case R_AC97_CTRL:
    case R_AC97_ADDR:
    case R_AC97_DATAOUT:
    case R_AC97_DATAIN:
    case R_D_CTRL:
    case R_D_ADDR:
    case R_D_REMAINING:
    case R_U_CTRL:
    case R_U_ADDR:
    case R_U_REMAINING:
        r = s->regs[addr];
        break;

    default:
        error_report("milkymist_ac97: read access to unknown register 0x"
                TARGET_FMT_plx, addr << 2);
        break;
    }

    trace_milkymist_ac97_memory_read(addr << 2, r);

    return r;
}

static void ac97_write(void *opaque, hwaddr addr, uint64_t value,
                       unsigned size)
{
    MilkymistAC97State *s = opaque;

    trace_milkymist_ac97_memory_write(addr, value);

    addr >>= 2;
    switch (addr) {
    case R_AC97_CTRL:
        /* always raise an IRQ according to the direction */
        if (value & AC97_CTRL_RQEN) {
            if (value & AC97_CTRL_WRITE) {
                trace_milkymist_ac97_pulse_irq_crrequest();
                qemu_irq_pulse(s->crrequest_irq);
            } else {
                trace_milkymist_ac97_pulse_irq_crreply();
                qemu_irq_pulse(s->crreply_irq);
            }
        }

        /* RQEN is self clearing */
        s->regs[addr] = value & ~AC97_CTRL_RQEN;
        break;
    case R_D_CTRL:
    case R_U_CTRL:
        s->regs[addr] = value;
        update_voices(s);
        break;
    case R_AC97_ADDR:
    case R_AC97_DATAOUT:
    case R_AC97_DATAIN:
    case R_D_ADDR:
    case R_D_REMAINING:
    case R_U_ADDR:
    case R_U_REMAINING:
        s->regs[addr] = value;
        break;

    default:
        error_report("milkymist_ac97: write access to unknown register 0x"
                TARGET_FMT_plx, addr);
        break;
    }

}

static const MemoryRegionOps ac97_mmio_ops = {
    .read = ac97_read,
    .write = ac97_write,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void ac97_in_cb(void *opaque, int avail_b)
{
    MilkymistAC97State *s = opaque;
    uint8_t buf[4096];
    uint32_t remaining = s->regs[R_U_REMAINING];
    int temp = MIN(remaining, avail_b);
    uint32_t addr = s->regs[R_U_ADDR];
    int transferred = 0;

    trace_milkymist_ac97_in_cb(avail_b, remaining);

    /* prevent from raising an IRQ */
    if (temp == 0) {
        return;
    }

    while (temp) {
        int acquired, to_copy;

        to_copy = MIN(temp, sizeof(buf));
        acquired = AUD_read(s->voice_in, buf, to_copy);
        if (!acquired) {
            break;
        }

        cpu_physical_memory_write(addr, buf, acquired);

        temp -= acquired;
        addr += acquired;
        transferred += acquired;
    }

    trace_milkymist_ac97_in_cb_transferred(transferred);

    s->regs[R_U_ADDR] = addr;
    s->regs[R_U_REMAINING] -= transferred;

    if ((s->regs[R_U_CTRL] & CTRL_EN) && (s->regs[R_U_REMAINING] == 0)) {
        trace_milkymist_ac97_pulse_irq_dmaw();
        qemu_irq_pulse(s->dmaw_irq);
    }
}

static void ac97_out_cb(void *opaque, int free_b)
{
    MilkymistAC97State *s = opaque;
    uint8_t buf[4096];
    uint32_t remaining = s->regs[R_D_REMAINING];
    int temp = MIN(remaining, free_b);
    uint32_t addr = s->regs[R_D_ADDR];
    int transferred = 0;

    trace_milkymist_ac97_out_cb(free_b, remaining);

    /* prevent from raising an IRQ */
    if (temp == 0) {
        return;
    }

    while (temp) {
        int copied, to_copy;

        to_copy = MIN(temp, sizeof(buf));
        cpu_physical_memory_read(addr, buf, to_copy);
        copied = AUD_write(s->voice_out, buf, to_copy);
        if (!copied) {
            break;
        }
        temp -= copied;
        addr += copied;
        transferred += copied;
    }

    trace_milkymist_ac97_out_cb_transferred(transferred);

    s->regs[R_D_ADDR] = addr;
    s->regs[R_D_REMAINING] -= transferred;

    if ((s->regs[R_D_CTRL] & CTRL_EN) && (s->regs[R_D_REMAINING] == 0)) {
        trace_milkymist_ac97_pulse_irq_dmar();
        qemu_irq_pulse(s->dmar_irq);
    }
}

static void milkymist_ac97_reset(DeviceState *d)
{
    MilkymistAC97State *s = MILKYMIST_AC97(d);
    int i;

    for (i = 0; i < R_MAX; i++) {
        s->regs[i] = 0;
    }

    AUD_set_active_in(s->voice_in, 0);
    AUD_set_active_out(s->voice_out, 0);
}

static int ac97_post_load(void *opaque, int version_id)
{
    MilkymistAC97State *s = opaque;

    update_voices(s);

    return 0;
}

static void milkymist_ac97_init(Object *obj)
{
    MilkymistAC97State *s = MILKYMIST_AC97(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);

    sysbus_init_irq(dev, &s->crrequest_irq);
    sysbus_init_irq(dev, &s->crreply_irq);
    sysbus_init_irq(dev, &s->dmar_irq);
    sysbus_init_irq(dev, &s->dmaw_irq);

    memory_region_init_io(&s->regs_region, obj, &ac97_mmio_ops, s,
            "milkymist-ac97", R_MAX * 4);
    sysbus_init_mmio(dev, &s->regs_region);
}

static void milkymist_ac97_realize(DeviceState *dev, Error **errp)
{
    MilkymistAC97State *s = MILKYMIST_AC97(dev);
    struct audsettings as;

    AUD_register_card("Milkymist AC'97", &s->card);

    as.freq = 48000;
    as.nchannels = 2;
    as.fmt = AUDIO_FORMAT_S16;
    as.endianness = 1;

    s->voice_in = AUD_open_in(&s->card, s->voice_in,
            "mm_ac97.in", s, ac97_in_cb, &as);
    s->voice_out = AUD_open_out(&s->card, s->voice_out,
            "mm_ac97.out", s, ac97_out_cb, &as);
}

static const VMStateDescription vmstate_milkymist_ac97 = {
    .name = "milkymist-ac97",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = ac97_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, MilkymistAC97State, R_MAX),
        VMSTATE_END_OF_LIST()
    }
};

static Property milkymist_ac97_properties[] = {
    DEFINE_AUDIO_PROPERTIES(MilkymistAC97State, card),
    DEFINE_PROP_END_OF_LIST(),
};

static void milkymist_ac97_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = milkymist_ac97_realize;
    dc->reset = milkymist_ac97_reset;
    dc->vmsd = &vmstate_milkymist_ac97;
    device_class_set_props(dc, milkymist_ac97_properties);
}

static const TypeInfo milkymist_ac97_info = {
    .name          = TYPE_MILKYMIST_AC97,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MilkymistAC97State),
    .instance_init = milkymist_ac97_init,
    .class_init    = milkymist_ac97_class_init,
};

static void milkymist_ac97_register_types(void)
{
    type_register_static(&milkymist_ac97_info);
}

type_init(milkymist_ac97_register_types)

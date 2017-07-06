/*
 *  LatticeMico32 JTAG UART model.
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
 */

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "trace.h"
#include "chardev/char-fe.h"

#include "hw/char/lm32_juart.h"

enum {
    LM32_JUART_MIN_SAVE_VERSION = 0,
    LM32_JUART_CURRENT_SAVE_VERSION = 0,
    LM32_JUART_MAX_SAVE_VERSION = 0,
};

enum {
    JTX_FULL = (1<<8),
};

enum {
    JRX_FULL = (1<<8),
};

#define LM32_JUART(obj) OBJECT_CHECK(LM32JuartState, (obj), TYPE_LM32_JUART)

struct LM32JuartState {
    SysBusDevice parent_obj;

    CharBackend chr;

    uint32_t jtx;
    uint32_t jrx;
};
typedef struct LM32JuartState LM32JuartState;

uint32_t lm32_juart_get_jtx(DeviceState *d)
{
    LM32JuartState *s = LM32_JUART(d);

    trace_lm32_juart_get_jtx(s->jtx);
    return s->jtx;
}

uint32_t lm32_juart_get_jrx(DeviceState *d)
{
    LM32JuartState *s = LM32_JUART(d);

    trace_lm32_juart_get_jrx(s->jrx);
    return s->jrx;
}

void lm32_juart_set_jtx(DeviceState *d, uint32_t jtx)
{
    LM32JuartState *s = LM32_JUART(d);
    unsigned char ch = jtx & 0xff;

    trace_lm32_juart_set_jtx(s->jtx);

    s->jtx = jtx;
    /* XXX this blocks entire thread. Rewrite to use
     * qemu_chr_fe_write and background I/O callbacks */
    qemu_chr_fe_write_all(&s->chr, &ch, 1);
}

void lm32_juart_set_jrx(DeviceState *d, uint32_t jtx)
{
    LM32JuartState *s = LM32_JUART(d);

    trace_lm32_juart_set_jrx(s->jrx);
    s->jrx &= ~JRX_FULL;
}

static void juart_rx(void *opaque, const uint8_t *buf, int size)
{
    LM32JuartState *s = opaque;

    s->jrx = *buf | JRX_FULL;
}

static int juart_can_rx(void *opaque)
{
    LM32JuartState *s = opaque;

    return !(s->jrx & JRX_FULL);
}

static void juart_event(void *opaque, int event)
{
}

static void juart_reset(DeviceState *d)
{
    LM32JuartState *s = LM32_JUART(d);

    s->jtx = 0;
    s->jrx = 0;
}

static void lm32_juart_realize(DeviceState *dev, Error **errp)
{
    LM32JuartState *s = LM32_JUART(dev);

    qemu_chr_fe_set_handlers(&s->chr, juart_can_rx, juart_rx,
                             juart_event, NULL, s, NULL, true);
}

static const VMStateDescription vmstate_lm32_juart = {
    .name = "lm32-juart",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(jtx, LM32JuartState),
        VMSTATE_UINT32(jrx, LM32JuartState),
        VMSTATE_END_OF_LIST()
    }
};

static Property lm32_juart_properties[] = {
    DEFINE_PROP_CHR("chardev", LM32JuartState, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void lm32_juart_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = juart_reset;
    dc->vmsd = &vmstate_lm32_juart;
    dc->props = lm32_juart_properties;
    dc->realize = lm32_juart_realize;
}

static const TypeInfo lm32_juart_info = {
    .name          = TYPE_LM32_JUART,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(LM32JuartState),
    .class_init    = lm32_juart_class_init,
};

static void lm32_juart_register_types(void)
{
    type_register_static(&lm32_juart_info);
}

type_init(lm32_juart_register_types)

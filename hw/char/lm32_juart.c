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

#include "hw/hw.h"
#include "hw/sysbus.h"
#include "trace.h"
#include "sysemu/char.h"

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

    CharDriverState *chr;

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
    if (s->chr) {
        qemu_chr_fe_write_all(s->chr, &ch, 1);
    }
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

static int lm32_juart_init(SysBusDevice *dev)
{
    LM32JuartState *s = LM32_JUART(dev);

    s->chr = qemu_char_get_next_serial();
    if (s->chr) {
        qemu_chr_add_handlers(s->chr, juart_can_rx, juart_rx, juart_event, s);
    }

    return 0;
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

static void lm32_juart_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = lm32_juart_init;
    dc->reset = juart_reset;
    dc->vmsd = &vmstate_lm32_juart;
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

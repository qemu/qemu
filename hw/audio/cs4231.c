/*
 * QEMU Crystal CS4231 audio chip emulation
 *
 * Copyright (c) 2006 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "trace.h"

/*
 * In addition to Crystal CS4231 there is a DMA controller on Sparc.
 */
#define CS_SIZE 0x40
#define CS_REGS 16
#define CS_DREGS 32
#define CS_MAXDREG (CS_DREGS - 1)

#define TYPE_CS4231 "SUNW,CS4231"
#define CS4231(obj) \
    OBJECT_CHECK(CSState, (obj), TYPE_CS4231)

typedef struct CSState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;
    uint32_t regs[CS_REGS];
    uint8_t dregs[CS_DREGS];
} CSState;

#define CS_RAP(s) ((s)->regs[0] & CS_MAXDREG)
#define CS_VER 0xa0
#define CS_CDC_VER 0x8a

static void cs_reset(DeviceState *d)
{
    CSState *s = CS4231(d);

    memset(s->regs, 0, CS_REGS * 4);
    memset(s->dregs, 0, CS_DREGS);
    s->dregs[12] = CS_CDC_VER;
    s->dregs[25] = CS_VER;
}

static uint64_t cs_mem_read(void *opaque, hwaddr addr,
                            unsigned size)
{
    CSState *s = opaque;
    uint32_t saddr, ret;

    saddr = addr >> 2;
    switch (saddr) {
    case 1:
        switch (CS_RAP(s)) {
        case 3: // Write only
            ret = 0;
            break;
        default:
            ret = s->dregs[CS_RAP(s)];
            break;
        }
        trace_cs4231_mem_readl_dreg(CS_RAP(s), ret);
        break;
    default:
        ret = s->regs[saddr];
        trace_cs4231_mem_readl_reg(saddr, ret);
        break;
    }
    return ret;
}

static void cs_mem_write(void *opaque, hwaddr addr,
                         uint64_t val, unsigned size)
{
    CSState *s = opaque;
    uint32_t saddr;

    saddr = addr >> 2;
    trace_cs4231_mem_writel_reg(saddr, s->regs[saddr], val);
    switch (saddr) {
    case 1:
        trace_cs4231_mem_writel_dreg(CS_RAP(s), s->dregs[CS_RAP(s)], val);
        switch(CS_RAP(s)) {
        case 11:
        case 25: // Read only
            break;
        case 12:
            val &= 0x40;
            val |= CS_CDC_VER; // Codec version
            s->dregs[CS_RAP(s)] = val;
            break;
        default:
            s->dregs[CS_RAP(s)] = val;
            break;
        }
        break;
    case 2: // Read only
        break;
    case 4:
        if (val & 1) {
            cs_reset(DEVICE(s));
        }
        val &= 0x7f;
        s->regs[saddr] = val;
        break;
    default:
        s->regs[saddr] = val;
        break;
    }
}

static const MemoryRegionOps cs_mem_ops = {
    .read = cs_mem_read,
    .write = cs_mem_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_cs4231 = {
    .name ="cs4231",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, CSState, CS_REGS),
        VMSTATE_UINT8_ARRAY(dregs, CSState, CS_DREGS),
        VMSTATE_END_OF_LIST()
    }
};

static int cs4231_init1(SysBusDevice *dev)
{
    CSState *s = CS4231(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &cs_mem_ops, s, "cs4321",
                          CS_SIZE);
    sysbus_init_mmio(dev, &s->iomem);
    sysbus_init_irq(dev, &s->irq);

    return 0;
}

static Property cs4231_properties[] = {
    {.name = NULL},
};

static void cs4231_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = cs4231_init1;
    dc->reset = cs_reset;
    dc->vmsd = &vmstate_cs4231;
    dc->props = cs4231_properties;
}

static const TypeInfo cs4231_info = {
    .name          = TYPE_CS4231,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(CSState),
    .class_init    = cs4231_class_init,
};

static void cs4231_register_types(void)
{
    type_register_static(&cs4231_info);
}

type_init(cs4231_register_types)

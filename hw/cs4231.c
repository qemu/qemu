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

#include "sysbus.h"
#include "trace.h"

/*
 * In addition to Crystal CS4231 there is a DMA controller on Sparc.
 */
#define CS_SIZE 0x40
#define CS_REGS 16
#define CS_DREGS 32
#define CS_MAXDREG (CS_DREGS - 1)

typedef struct CSState {
    SysBusDevice busdev;
    qemu_irq irq;
    uint32_t regs[CS_REGS];
    uint8_t dregs[CS_DREGS];
} CSState;

#define CS_RAP(s) ((s)->regs[0] & CS_MAXDREG)
#define CS_VER 0xa0
#define CS_CDC_VER 0x8a

static void cs_reset(DeviceState *d)
{
    CSState *s = container_of(d, CSState, busdev.qdev);

    memset(s->regs, 0, CS_REGS * 4);
    memset(s->dregs, 0, CS_DREGS);
    s->dregs[12] = CS_CDC_VER;
    s->dregs[25] = CS_VER;
}

static uint32_t cs_mem_readl(void *opaque, target_phys_addr_t addr)
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

static void cs_mem_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
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
            cs_reset(&s->busdev.qdev);
        }
        val &= 0x7f;
        s->regs[saddr] = val;
        break;
    default:
        s->regs[saddr] = val;
        break;
    }
}

static CPUReadMemoryFunc * const cs_mem_read[3] = {
    cs_mem_readl,
    cs_mem_readl,
    cs_mem_readl,
};

static CPUWriteMemoryFunc * const cs_mem_write[3] = {
    cs_mem_writel,
    cs_mem_writel,
    cs_mem_writel,
};

static const VMStateDescription vmstate_cs4231 = {
    .name ="cs4231",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField []) {
        VMSTATE_UINT32_ARRAY(regs, CSState, CS_REGS),
        VMSTATE_UINT8_ARRAY(dregs, CSState, CS_DREGS),
        VMSTATE_END_OF_LIST()
    }
};

static int cs4231_init1(SysBusDevice *dev)
{
    int io;
    CSState *s = FROM_SYSBUS(CSState, dev);

    io = cpu_register_io_memory(cs_mem_read, cs_mem_write, s,
                                DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio(dev, CS_SIZE, io);
    sysbus_init_irq(dev, &s->irq);

    return 0;
}

static SysBusDeviceInfo cs4231_info = {
    .init = cs4231_init1,
    .qdev.name  = "SUNW,CS4231",
    .qdev.size  = sizeof(CSState),
    .qdev.vmsd  = &vmstate_cs4231,
    .qdev.reset = cs_reset,
    .qdev.props = (Property[]) {
        {.name = NULL}
    }
};

static void cs4231_register_devices(void)
{
    sysbus_register_withprop(&cs4231_info);
}

device_init(cs4231_register_devices)

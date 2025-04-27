/*
 * QEMU Xilinx OPB Interrupt Controller.
 *
 * Copyright (c) 2009 Edgar E. Iglesias.
 *
 * https://docs.amd.com/v/u/en-US/xps_intc
 * DS572: LogiCORE IP XPS Interrupt Controller (v2.01a)
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
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "qemu/module.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "qom/object.h"

#define D(x)

#define R_ISR       0
#define R_IPR       1
#define R_IER       2
#define R_IAR       3
#define R_SIE       4
#define R_CIE       5
#define R_IVR       6
#define R_MER       7
#define R_MAX       8

#define TYPE_XILINX_INTC "xlnx.xps-intc"
typedef struct XpsIntc XpsIntc;
DECLARE_INSTANCE_CHECKER(XpsIntc, XILINX_INTC, TYPE_XILINX_INTC)

struct XpsIntc
{
    SysBusDevice parent_obj;

    EndianMode model_endianness;
    MemoryRegion mmio;
    qemu_irq parent_irq;

    /* Configuration reg chosen at synthesis-time. QEMU populates
       the bits at board-setup.  */
    uint32_t c_kind_of_intr;

    /* Runtime control registers.  */
    uint32_t regs[R_MAX];
    /* state of the interrupt input pins */
    uint32_t irq_pin_state;
};

static void update_irq(XpsIntc *p)
{
    uint32_t i;

    /* level triggered interrupt */
    if (p->regs[R_MER] & 2) {
        p->regs[R_ISR] |= p->irq_pin_state & ~p->c_kind_of_intr;
    }

    /* Update the pending register.  */
    p->regs[R_IPR] = p->regs[R_ISR] & p->regs[R_IER];

    /* Update the vector register.  */
    for (i = 0; i < 32; i++) {
        if (p->regs[R_IPR] & (1U << i)) {
            break;
        }
    }
    if (i == 32)
        i = ~0;

    p->regs[R_IVR] = i;
    qemu_set_irq(p->parent_irq, (p->regs[R_MER] & 1) && p->regs[R_IPR]);
}

static uint64_t pic_read(void *opaque, hwaddr addr, unsigned int size)
{
    XpsIntc *p = opaque;
    uint32_t r = 0;

    addr >>= 2;
    switch (addr)
    {
        default:
            if (addr < ARRAY_SIZE(p->regs))
                r = p->regs[addr];
            break;

    }
    D(printf("%s %x=%x\n", __func__, addr * 4, r));
    return r;
}

static void pic_write(void *opaque, hwaddr addr,
                      uint64_t val64, unsigned int size)
{
    XpsIntc *p = opaque;
    uint32_t value = val64;

    addr >>= 2;
    D(qemu_log("%s addr=%x val=%x\n", __func__, addr * 4, value));
    switch (addr) 
    {
        case R_IAR:
            p->regs[R_ISR] &= ~value; /* ACK.  */
            break;
        case R_SIE:
            p->regs[R_IER] |= value;  /* Atomic set ie.  */
            break;
        case R_CIE:
            p->regs[R_IER] &= ~value; /* Atomic clear ie.  */
            break;
        case R_MER:
            p->regs[R_MER] = value & 0x3;
            break;
        case R_ISR:
            if ((p->regs[R_MER] & 2)) {
                break;
            }
            /* fallthrough */
        default:
            if (addr < ARRAY_SIZE(p->regs))
                p->regs[addr] = value;
            break;
    }
    update_irq(p);
}

static const MemoryRegionOps pic_ops[2] = {
    [0 ... 1] = {
        .read = pic_read,
        .write = pic_write,
        .impl = {
            .min_access_size = 4,
            .max_access_size = 4,
        },
        .valid = {
            /*
             * All XPS INTC registers are accessed through the PLB interface.
             * The base address for these registers is provided by the
             * configuration parameter, C_BASEADDR. Each register is 32 bits
             * although some bits may be unused and is accessed on a 4-byte
             * boundary offset from the base address.
             */
            .min_access_size = 4,
            .max_access_size = 4,
        },
    },
    [0].endianness = DEVICE_LITTLE_ENDIAN,
    [1].endianness = DEVICE_BIG_ENDIAN,
};

static void irq_handler(void *opaque, int irq, int level)
{
    XpsIntc *p = opaque;

    /* edge triggered interrupt */
    if (p->c_kind_of_intr & (1 << irq) && p->regs[R_MER] & 2) {
        p->regs[R_ISR] |= (level << irq);
    }

    p->irq_pin_state &= ~(1 << irq);
    p->irq_pin_state |= level << irq;
    update_irq(p);
}

static void xilinx_intc_init(Object *obj)
{
    XpsIntc *p = XILINX_INTC(obj);

    qdev_init_gpio_in(DEVICE(obj), irq_handler, 32);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &p->parent_irq);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &p->mmio);
}

static void xilinx_intc_realize(DeviceState *dev, Error **errp)
{
    XpsIntc *p = XILINX_INTC(dev);

    if (p->model_endianness == ENDIAN_MODE_UNSPECIFIED) {
        error_setg(errp, TYPE_XILINX_INTC " property 'endianness'"
                         " must be set to 'big' or 'little'");
        return;
    }

    memory_region_init_io(&p->mmio, OBJECT(dev),
                          &pic_ops[p->model_endianness == ENDIAN_MODE_BIG],
                          p, "xlnx.xps-intc",
                          R_MAX * 4);
}

static const Property xilinx_intc_properties[] = {
    DEFINE_PROP_ENDIAN_NODEFAULT("endianness", XpsIntc, model_endianness),
    DEFINE_PROP_UINT32("kind-of-intr", XpsIntc, c_kind_of_intr, 0),
};

static void xilinx_intc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = xilinx_intc_realize;
    device_class_set_props(dc, xilinx_intc_properties);
}

static const TypeInfo xilinx_intc_info = {
    .name          = TYPE_XILINX_INTC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XpsIntc),
    .instance_init = xilinx_intc_init,
    .class_init    = xilinx_intc_class_init,
};

static void xilinx_intc_register_types(void)
{
    type_register_static(&xilinx_intc_info);
}

type_init(xilinx_intc_register_types)

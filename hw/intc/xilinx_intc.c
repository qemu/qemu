/*
 * QEMU Xilinx OPB Interrupt Controller.
 *
 * Copyright (c) 2009 Edgar E. Iglesias.
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
#include "qemu/module.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"

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
#define XILINX_INTC(obj) OBJECT_CHECK(struct xlx_pic, (obj), TYPE_XILINX_INTC)

struct xlx_pic
{
    SysBusDevice parent_obj;

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

static void update_irq(struct xlx_pic *p)
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

static uint64_t
pic_read(void *opaque, hwaddr addr, unsigned int size)
{
    struct xlx_pic *p = opaque;
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

static void
pic_write(void *opaque, hwaddr addr,
          uint64_t val64, unsigned int size)
{
    struct xlx_pic *p = opaque;
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

static const MemoryRegionOps pic_ops = {
    .read = pic_read,
    .write = pic_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static void irq_handler(void *opaque, int irq, int level)
{
    struct xlx_pic *p = opaque;

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
    struct xlx_pic *p = XILINX_INTC(obj);

    qdev_init_gpio_in(DEVICE(obj), irq_handler, 32);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &p->parent_irq);

    memory_region_init_io(&p->mmio, obj, &pic_ops, p, "xlnx.xps-intc",
                          R_MAX * 4);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &p->mmio);
}

static Property xilinx_intc_properties[] = {
    DEFINE_PROP_UINT32("kind-of-intr", struct xlx_pic, c_kind_of_intr, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void xilinx_intc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, xilinx_intc_properties);
}

static const TypeInfo xilinx_intc_info = {
    .name          = TYPE_XILINX_INTC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(struct xlx_pic),
    .instance_init = xilinx_intc_init,
    .class_init    = xilinx_intc_class_init,
};

static void xilinx_intc_register_types(void)
{
    type_register_static(&xilinx_intc_info);
}

type_init(xilinx_intc_register_types)

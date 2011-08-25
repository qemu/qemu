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

#include "sysbus.h"
#include "hw.h"

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

struct xlx_pic
{
    SysBusDevice busdev;
    MemoryRegion mmio;
    qemu_irq parent_irq;

    /* Configuration reg chosen at synthesis-time. QEMU populates
       the bits at board-setup.  */
    uint32_t c_kind_of_intr;

    /* Runtime control registers.  */
    uint32_t regs[R_MAX];
};

static void update_irq(struct xlx_pic *p)
{
    uint32_t i;
    /* Update the pending register.  */
    p->regs[R_IPR] = p->regs[R_ISR] & p->regs[R_IER];

    /* Update the vector register.  */
    for (i = 0; i < 32; i++) {
        if (p->regs[R_IPR] & (1 << i))
            break;
    }
    if (i == 32)
        i = ~0;

    p->regs[R_IVR] = i;
    if ((p->regs[R_MER] & 1) && p->regs[R_IPR]) {
        qemu_irq_raise(p->parent_irq);
    } else {
        qemu_irq_lower(p->parent_irq);
    }
}

static uint64_t
pic_read(void *opaque, target_phys_addr_t addr, unsigned int size)
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
pic_write(void *opaque, target_phys_addr_t addr,
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

    if (!(p->regs[R_MER] & 2)) {
        qemu_irq_lower(p->parent_irq);
        return;
    }

    /* Update source flops. Don't clear unless level triggered.
       Edge triggered interrupts only go away when explicitely acked to
       the interrupt controller.  */
    if (!(p->c_kind_of_intr & (1 << irq)) || level) {
        p->regs[R_ISR] &= ~(1 << irq);
        p->regs[R_ISR] |= (level << irq);
    }
    update_irq(p);
}

static int xilinx_intc_init(SysBusDevice *dev)
{
    struct xlx_pic *p = FROM_SYSBUS(typeof (*p), dev);

    qdev_init_gpio_in(&dev->qdev, irq_handler, 32);
    sysbus_init_irq(dev, &p->parent_irq);

    memory_region_init_io(&p->mmio, &pic_ops, p, "xilinx-pic", R_MAX * 4);
    sysbus_init_mmio_region(dev, &p->mmio);
    return 0;
}

static SysBusDeviceInfo xilinx_intc_info = {
    .init = xilinx_intc_init,
    .qdev.name  = "xilinx,intc",
    .qdev.size  = sizeof(struct xlx_pic),
    .qdev.props = (Property[]) {
        DEFINE_PROP_UINT32("kind-of-intr", struct xlx_pic, c_kind_of_intr, 0),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void xilinx_intc_register(void)
{
    sysbus_register_withprop(&xilinx_intc_info);
}

device_init(xilinx_intc_register)

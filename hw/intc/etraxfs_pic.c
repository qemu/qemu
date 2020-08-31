/*
 * QEMU ETRAX Interrupt Controller.
 *
 * Copyright (c) 2008 Edgar E. Iglesias, Axis Communications AB.
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
#include "qom/object.h"

#define D(x)

#define R_RW_MASK   0
#define R_R_VECT    1
#define R_R_MASKED_VECT 2
#define R_R_NMI     3
#define R_R_GURU    4
#define R_MAX       5

#define TYPE_ETRAX_FS_PIC "etraxfs,pic"
DECLARE_INSTANCE_CHECKER(struct etrax_pic, ETRAX_FS_PIC,
                         TYPE_ETRAX_FS_PIC)

struct etrax_pic
{
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq parent_irq;
    qemu_irq parent_nmi;
    uint32_t regs[R_MAX];
};

static void pic_update(struct etrax_pic *fs)
{   
    uint32_t vector = 0;
    int i;

    fs->regs[R_R_MASKED_VECT] = fs->regs[R_R_VECT] & fs->regs[R_RW_MASK];

    /* The ETRAX interrupt controller signals interrupts to the core
       through an interrupt request wire and an irq vector bus. If 
       multiple interrupts are simultaneously active it chooses vector 
       0x30 and lets the sw choose the priorities.  */
    if (fs->regs[R_R_MASKED_VECT]) {
        uint32_t mv = fs->regs[R_R_MASKED_VECT];
        for (i = 0; i < 31; i++) {
            if (mv & 1) {
                vector = 0x31 + i;
                /* Check for multiple interrupts.  */
                if (mv > 1)
                    vector = 0x30;
                break;
            }
            mv >>= 1;
        }
    }

    qemu_set_irq(fs->parent_irq, vector);
}

static uint64_t
pic_read(void *opaque, hwaddr addr, unsigned int size)
{
    struct etrax_pic *fs = opaque;
    uint32_t rval;

    rval = fs->regs[addr >> 2];
    D(printf("%s %x=%x\n", __func__, addr, rval));
    return rval;
}

static void pic_write(void *opaque, hwaddr addr,
                      uint64_t value, unsigned int size)
{
    struct etrax_pic *fs = opaque;
    D(printf("%s addr=%x val=%x\n", __func__, addr, value));

    if (addr == R_RW_MASK) {
        fs->regs[R_RW_MASK] = value;
        pic_update(fs);
    }
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

static void nmi_handler(void *opaque, int irq, int level)
{   
    struct etrax_pic *fs = (void *)opaque;
    uint32_t mask;

    mask = 1 << irq;
    if (level)
        fs->regs[R_R_NMI] |= mask;
    else
        fs->regs[R_R_NMI] &= ~mask;

    qemu_set_irq(fs->parent_nmi, !!fs->regs[R_R_NMI]);
}

static void irq_handler(void *opaque, int irq, int level)
{
    struct etrax_pic *fs = (void *)opaque;

    if (irq >= 30) {
        nmi_handler(opaque, irq, level);
        return;
    }

    irq -= 1;
    fs->regs[R_R_VECT] &= ~(1 << irq);
    fs->regs[R_R_VECT] |= (!!level << irq);
    pic_update(fs);
}

static void etraxfs_pic_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    struct etrax_pic *s = ETRAX_FS_PIC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    qdev_init_gpio_in(dev, irq_handler, 32);
    sysbus_init_irq(sbd, &s->parent_irq);
    sysbus_init_irq(sbd, &s->parent_nmi);

    memory_region_init_io(&s->mmio, obj, &pic_ops, s,
                          "etraxfs-pic", R_MAX * 4);
    sysbus_init_mmio(sbd, &s->mmio);
}

static const TypeInfo etraxfs_pic_info = {
    .name          = TYPE_ETRAX_FS_PIC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(struct etrax_pic),
    .instance_init = etraxfs_pic_init,
};

static void etraxfs_pic_register_types(void)
{
    type_register_static(&etraxfs_pic_info);
}

type_init(etraxfs_pic_register_types)

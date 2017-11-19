/*
 * CSKY intc controller
 *
 * Written by lyc
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
#include "hw/sysbus.h"
#include "hw/ptimer.h"
#include "qemu/log.h"
#include "trace.h"
#include "cpu.h"
#include "hw/csky/cskydev.h"

#define INTC_ICR_MASK    0x1f
#define INTC_ICR_MFI     (1 << 12)
#define INTC_ICR_ME      (1 << 13)
#define INTC_ICR_FVE     (1 << 14)
#define INTC_ICR_AVE     (1 << 15)

#define INTC_ISR_VEC     0x7f
#define INTC_ISR_FINT    (1 << 8)
#define INTC_ISR_INT     (1 << 9)

#define PR0              0x40
#define PR28             0x5c
#define INTC_LEVEL       (1 << 10)

#define TYPE_CSKY_INTC   "csky_intc"
#define CSKY_INTC(obj)   OBJECT_CHECK(csky_intc_state, (obj), TYPE_CSKY_INTC)

typedef struct csky_intc_state {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    SysBusDevice busdev;
    uint32_t int_icr;
    uint32_t int_isr;
    uint32_t int_source; /*irq from device*/
    uint32_t int_ifr;
    uint32_t int_ipr;
    uint32_t int_nier;
    uint32_t int_nipr;
    uint32_t int_fier;
    uint32_t int_fipr;
    uint32_t pr[32];
    qemu_irq irq;
} csky_intc_state;

static void csky_intc_update(csky_intc_state *s)
{
    uint32_t f;
    uint32_t i;
    uint32_t flag;

    f = s->int_source | s->int_ifr;
    if (f == 0) {
        s->int_ipr = 0;
    } else {
        i = __builtin_clz(f);
        s->int_ipr = 1 << s->pr[31 - i];
        f &= ~(1 << (31 - i));
        while (f != 0) {
            i = __builtin_clz(f);
            s->int_ipr |= 1 << s->pr[31 - i];
            f &= ~(1 << (31 - i));
        }
    }

    if (s->int_icr & INTC_ICR_ME) {
        if (s->int_icr & INTC_ICR_MFI) {
            s->int_nipr = 0;
            s->int_fipr = s->int_ipr & s->int_fier &
                (0xffffffff << ((s->int_icr & INTC_ICR_MASK) + 1));
        } else {
            s->int_fipr = s->int_ipr & s->int_fier;
            s->int_nipr = s->int_ipr & s->int_nier &
                (0xffffffff << ((s->int_icr & INTC_ICR_MASK) + 1));
        }
    } else {
        s->int_fipr = s->int_ipr & s->int_fier;
        s->int_nipr = s->int_ipr & s->int_nier;
    }

    s->int_isr = ((s->int_fipr != 0) << 8) | ((s->int_nipr != 0) << 9);

    if (s->int_icr & INTC_ICR_FVE) {
        if (s->int_fipr) {
            s->int_isr |= 63 - __builtin_clz(s->int_fipr);
        } else if (s->int_nipr) {
            s->int_isr |= 31 - __builtin_clz(s->int_nipr);
        }
    } else {
        if (s->int_fipr) {
            s->int_isr |= 31 - __builtin_clz(s->int_fipr);
        } else if (s->int_nipr) {
            s->int_isr |= 31 - __builtin_clz(s->int_nipr);
        }
    }

    flag = ((s->int_fipr || s->int_nipr) << 10) |
        (s->int_isr + 32) | ((s->int_icr & INTC_ICR_AVE) >> 8);
    qemu_set_irq(s->irq, flag);
}

static void csky_intc_set_irq(void *opaque, int irq, int level)
{
    csky_intc_state *s = (csky_intc_state *)opaque;
    if (level) {
        s->int_source |= 1 << irq;
    } else {
        s->int_source &= ~(1 << irq);
    }
    csky_intc_update(s);
}

static uint64_t csky_intc_read(void *opaque, hwaddr offset, unsigned size)
{
    csky_intc_state *s = (csky_intc_state *)opaque;

    if (size == 2) {
        switch (offset) {
        case 0x00: /*ISR*/
            return s->int_isr;
        case 0x02: /*ICR*/
            return s->int_icr;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "csky_intc_read: "
                          "Bad register offset 0x%x\n", (int)offset);
        }
    } else if (size == 4) {
        switch (offset) {
        case 0x00: /*ISR & ICR*/
            return s->int_isr | (s->int_icr << 16);
        case 0x08: /*IFR*/
            return s->int_ifr;
        case 0x0c: /*IPR*/
            return s->int_ipr;
        case 0x10: /*NIER*/
            return s->int_nier;
        case 0x14: /*NIPR*/
            return s->int_nipr;
        case 0x18: /*FIER*/
            return s->int_fier;
        case 0x1c: /*FIPR*/
            return s->int_fipr;
        case PR0 ... PR28: /*PR[32]*/
            return ((s->pr[offset - PR0] << 24) |
                    (s->pr[offset - PR0 + 1] << 16) |
                    (s->pr[offset - PR0 + 2] << 8) |
                    s->pr[offset - PR0 + 3]);
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "csky_intc_read: "
                          "Bad register offset 0x%x\n", (int)offset);
        }
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "csky_intc_read: "
                      "Bad size 0x%x\n", size);
    }

    return 0;
}

static void csky_intc_write(void *opaque, hwaddr offset,
                            uint64_t value, unsigned size)
{
    csky_intc_state *s = (csky_intc_state *)opaque;

    if (size == 2) {
        switch (offset) {
        case 0x00: /*ISR*/
            return;
        case 0x02: /*ICR*/
            s->int_icr = value;
            return;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "csky_intc_write: "
                          "Bad register offset 0x%x\n", (int)offset);
            return;
        }
    } else if (size == 4) {
        switch (offset) {
        case 0x00: /*ICR*/
            s->int_icr = value >> 16;
            break;
        case 0x08: /*IFR*/
            s->int_ifr = value;
            break;
        case 0x10: /*NIER*/
            s->int_nier = value;
            break;
        case 0x18: /*FIER*/
            s->int_fier = value;
            break;
        case PR0 ... PR28: /*PR[32]*/
            s->pr[offset - PR0] = (value >> 24) & 0xff;
            s->pr[offset - PR0 + 1] = (value >> 16) & 0xff;
            s->pr[offset - PR0 + 2] = (value >> 8) & 0xff;
            s->pr[offset - PR0 + 3] = value & 0xff;
            break;
        case 0x0c: /*IPR*/
        case 0x14: /*NIPR*/
        case 0x1c: /*FIPR*/
            return;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "csky_intc_write: "
                          "Bad register offset 0x%x\n", (int)offset);
            return;
        }
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "csky_intc_write: "
                      "Bad size 0x%x\n", size);
    }
    csky_intc_update(s);
}

static const MemoryRegionOps csky_intc_ops = {
    .read = csky_intc_read,
    .write = csky_intc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void csky_intc_cpu_handler(void *opaque, int irq, int level)
{
    CPUCSKYState *env = (CPUCSKYState *)opaque;
    CPUState *cs = CPU(csky_env_get_cpu(env));

    env->intc_signals.vec_b = level & INTC_ISR_VEC;
    env->intc_signals.avec_b = (level & 0x80) >> 7;
    env->intc_signals.fint_b = (level & INTC_ISR_FINT) >> 8;
    env->intc_signals.int_b = (level & INTC_ISR_INT) >> 9;

    if (level & INTC_LEVEL) {
        cpu_interrupt(cs, CPU_INTERRUPT_HARD);
    } else {
        cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
    }
}

qemu_irq *csky_intc_init_cpu(CPUCSKYState *env)
{
    return qemu_allocate_irqs(csky_intc_cpu_handler, env, 1);
}

static void csky_intc_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    csky_intc_state *s = CSKY_INTC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &csky_intc_ops,
                          s, TYPE_CSKY_INTC, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    qdev_init_gpio_in(dev, csky_intc_set_irq, 32);
    sysbus_init_irq(sbd, &s->irq);

    s->int_icr = INTC_ICR_AVE;
}

static const VMStateDescription vmstate_csky_intc = {
    .name = TYPE_CSKY_INTC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(int_icr, csky_intc_state),
        VMSTATE_UINT32(int_isr, csky_intc_state),
        VMSTATE_UINT32(int_source, csky_intc_state),
        VMSTATE_UINT32(int_ifr, csky_intc_state),
        VMSTATE_UINT32(int_ipr, csky_intc_state),
        VMSTATE_UINT32(int_nier, csky_intc_state),
        VMSTATE_UINT32(int_nipr, csky_intc_state),
        VMSTATE_UINT32(int_fier, csky_intc_state),
        VMSTATE_UINT32(int_fipr, csky_intc_state),
        VMSTATE_UINT32_ARRAY(pr, csky_intc_state, 32),
        VMSTATE_END_OF_LIST()
    }
};

static void csky_intc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_csky_intc;
}

static const TypeInfo csky_intc_info = {
    .name          = TYPE_CSKY_INTC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(csky_intc_state),
    .instance_init = csky_intc_init,
    .class_init    = csky_intc_class_init,
};

static void csky_register_types(void)
{
    type_register_static(&csky_intc_info);
}

type_init(csky_register_types)

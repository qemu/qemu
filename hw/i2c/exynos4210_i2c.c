/*
 *  Exynos4210 I2C Bus Serial Interface Emulation
 *
 *  Copyright (C) 2012 Samsung Electronics Co Ltd.
 *    Maksim Kozlov, <m.kozlov@samsung.com>
 *    Igor Mitsyanko, <i.mitsyanko@samsung.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/i2c/i2c.h"
#include "hw/irq.h"

#ifndef EXYNOS4_I2C_DEBUG
#define EXYNOS4_I2C_DEBUG                 0
#endif

#define TYPE_EXYNOS4_I2C                  "exynos4210.i2c"
#define EXYNOS4_I2C(obj)                  \
    OBJECT_CHECK(Exynos4210I2CState, (obj), TYPE_EXYNOS4_I2C)

/* Exynos4210 I2C memory map */
#define EXYNOS4_I2C_MEM_SIZE              0x14
#define I2CCON_ADDR                       0x00  /* control register */
#define I2CSTAT_ADDR                      0x04  /* control/status register */
#define I2CADD_ADDR                       0x08  /* address register */
#define I2CDS_ADDR                        0x0c  /* data shift register */
#define I2CLC_ADDR                        0x10  /* line control register */

#define I2CCON_ACK_GEN                    (1 << 7)
#define I2CCON_INTRS_EN                   (1 << 5)
#define I2CCON_INT_PEND                   (1 << 4)

#define EXYNOS4_I2C_MODE(reg)             (((reg) >> 6) & 3)
#define I2C_IN_MASTER_MODE(reg)           (((reg) >> 6) & 2)
#define I2CMODE_MASTER_Rx                 0x2
#define I2CMODE_MASTER_Tx                 0x3
#define I2CSTAT_LAST_BIT                  (1 << 0)
#define I2CSTAT_OUTPUT_EN                 (1 << 4)
#define I2CSTAT_START_BUSY                (1 << 5)


#if EXYNOS4_I2C_DEBUG
#define DPRINT(fmt, args...)              \
    do { fprintf(stderr, "QEMU I2C: "fmt, ## args); } while (0)

static const char *exynos4_i2c_get_regname(unsigned offset)
{
    switch (offset) {
    case I2CCON_ADDR:
        return "I2CCON";
    case I2CSTAT_ADDR:
        return "I2CSTAT";
    case I2CADD_ADDR:
        return "I2CADD";
    case I2CDS_ADDR:
        return "I2CDS";
    case I2CLC_ADDR:
        return "I2CLC";
    default:
        return "[?]";
    }
}

#else
#define DPRINT(fmt, args...)              do { } while (0)
#endif

typedef struct Exynos4210I2CState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    I2CBus *bus;
    qemu_irq irq;

    uint8_t i2ccon;
    uint8_t i2cstat;
    uint8_t i2cadd;
    uint8_t i2cds;
    uint8_t i2clc;
    bool scl_free;
} Exynos4210I2CState;

static inline void exynos4210_i2c_raise_interrupt(Exynos4210I2CState *s)
{
    if (s->i2ccon & I2CCON_INTRS_EN) {
        s->i2ccon |= I2CCON_INT_PEND;
        qemu_irq_raise(s->irq);
    }
}

static void exynos4210_i2c_data_receive(void *opaque)
{
    Exynos4210I2CState *s = (Exynos4210I2CState *)opaque;

    s->i2cstat &= ~I2CSTAT_LAST_BIT;
    s->scl_free = false;
    s->i2cds = i2c_recv(s->bus);
    exynos4210_i2c_raise_interrupt(s);
}

static void exynos4210_i2c_data_send(void *opaque)
{
    Exynos4210I2CState *s = (Exynos4210I2CState *)opaque;

    s->i2cstat &= ~I2CSTAT_LAST_BIT;
    s->scl_free = false;
    if (i2c_send(s->bus, s->i2cds) < 0 && (s->i2ccon & I2CCON_ACK_GEN)) {
        s->i2cstat |= I2CSTAT_LAST_BIT;
    }
    exynos4210_i2c_raise_interrupt(s);
}

static uint64_t exynos4210_i2c_read(void *opaque, hwaddr offset,
                                 unsigned size)
{
    Exynos4210I2CState *s = (Exynos4210I2CState *)opaque;
    uint8_t value;

    switch (offset) {
    case I2CCON_ADDR:
        value = s->i2ccon;
        break;
    case I2CSTAT_ADDR:
        value = s->i2cstat;
        break;
    case I2CADD_ADDR:
        value = s->i2cadd;
        break;
    case I2CDS_ADDR:
        value = s->i2cds;
        s->scl_free = true;
        if (EXYNOS4_I2C_MODE(s->i2cstat) == I2CMODE_MASTER_Rx &&
               (s->i2cstat & I2CSTAT_START_BUSY) &&
               !(s->i2ccon & I2CCON_INT_PEND)) {
            exynos4210_i2c_data_receive(s);
        }
        break;
    case I2CLC_ADDR:
        value = s->i2clc;
        break;
    default:
        value = 0;
        DPRINT("ERROR: Bad read offset 0x%x\n", (unsigned int)offset);
        break;
    }

    DPRINT("read %s [0x%02x] -> 0x%02x\n", exynos4_i2c_get_regname(offset),
            (unsigned int)offset, value);
    return value;
}

static void exynos4210_i2c_write(void *opaque, hwaddr offset,
                              uint64_t value, unsigned size)
{
    Exynos4210I2CState *s = (Exynos4210I2CState *)opaque;
    uint8_t v = value & 0xff;

    DPRINT("write %s [0x%02x] <- 0x%02x\n", exynos4_i2c_get_regname(offset),
            (unsigned int)offset, v);

    switch (offset) {
    case I2CCON_ADDR:
        s->i2ccon = (v & ~I2CCON_INT_PEND) | (s->i2ccon & I2CCON_INT_PEND);
        if ((s->i2ccon & I2CCON_INT_PEND) && !(v & I2CCON_INT_PEND)) {
            s->i2ccon &= ~I2CCON_INT_PEND;
            qemu_irq_lower(s->irq);
            if (!(s->i2ccon & I2CCON_INTRS_EN)) {
                s->i2cstat &= ~I2CSTAT_START_BUSY;
            }

            if (s->i2cstat & I2CSTAT_START_BUSY) {
                if (s->scl_free) {
                    if (EXYNOS4_I2C_MODE(s->i2cstat) == I2CMODE_MASTER_Tx) {
                        exynos4210_i2c_data_send(s);
                    } else if (EXYNOS4_I2C_MODE(s->i2cstat) ==
                            I2CMODE_MASTER_Rx) {
                        exynos4210_i2c_data_receive(s);
                    }
                } else {
                    s->i2ccon |= I2CCON_INT_PEND;
                    qemu_irq_raise(s->irq);
                }
            }
        }
        break;
    case I2CSTAT_ADDR:
        s->i2cstat =
                (s->i2cstat & I2CSTAT_START_BUSY) | (v & ~I2CSTAT_START_BUSY);

        if (!(s->i2cstat & I2CSTAT_OUTPUT_EN)) {
            s->i2cstat &= ~I2CSTAT_START_BUSY;
            s->scl_free = true;
            qemu_irq_lower(s->irq);
            break;
        }

        /* Nothing to do if in i2c slave mode */
        if (!I2C_IN_MASTER_MODE(s->i2cstat)) {
            break;
        }

        if (v & I2CSTAT_START_BUSY) {
            s->i2cstat &= ~I2CSTAT_LAST_BIT;
            s->i2cstat |= I2CSTAT_START_BUSY;    /* Line is busy */
            s->scl_free = false;

            /* Generate start bit and send slave address */
            if (i2c_start_transfer(s->bus, s->i2cds >> 1, s->i2cds & 0x1) &&
                    (s->i2ccon & I2CCON_ACK_GEN)) {
                s->i2cstat |= I2CSTAT_LAST_BIT;
            } else if (EXYNOS4_I2C_MODE(s->i2cstat) == I2CMODE_MASTER_Rx) {
                exynos4210_i2c_data_receive(s);
            }
            exynos4210_i2c_raise_interrupt(s);
        } else {
            i2c_end_transfer(s->bus);
            if (!(s->i2ccon & I2CCON_INT_PEND)) {
                s->i2cstat &= ~I2CSTAT_START_BUSY;
            }
            s->scl_free = true;
        }
        break;
    case I2CADD_ADDR:
        if ((s->i2cstat & I2CSTAT_OUTPUT_EN) == 0) {
            s->i2cadd = v;
        }
        break;
    case I2CDS_ADDR:
        if (s->i2cstat & I2CSTAT_OUTPUT_EN) {
            s->i2cds = v;
            s->scl_free = true;
            if (EXYNOS4_I2C_MODE(s->i2cstat) == I2CMODE_MASTER_Tx &&
                    (s->i2cstat & I2CSTAT_START_BUSY) &&
                    !(s->i2ccon & I2CCON_INT_PEND)) {
                exynos4210_i2c_data_send(s);
            }
        }
        break;
    case I2CLC_ADDR:
        s->i2clc = v;
        break;
    default:
        DPRINT("ERROR: Bad write offset 0x%x\n", (unsigned int)offset);
        break;
    }
}

static const MemoryRegionOps exynos4210_i2c_ops = {
    .read = exynos4210_i2c_read,
    .write = exynos4210_i2c_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription exynos4210_i2c_vmstate = {
    .name = "exynos4210.i2c",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(i2ccon, Exynos4210I2CState),
        VMSTATE_UINT8(i2cstat, Exynos4210I2CState),
        VMSTATE_UINT8(i2cds, Exynos4210I2CState),
        VMSTATE_UINT8(i2cadd, Exynos4210I2CState),
        VMSTATE_UINT8(i2clc, Exynos4210I2CState),
        VMSTATE_BOOL(scl_free, Exynos4210I2CState),
        VMSTATE_END_OF_LIST()
    }
};

static void exynos4210_i2c_reset(DeviceState *d)
{
    Exynos4210I2CState *s = EXYNOS4_I2C(d);

    s->i2ccon  = 0x00;
    s->i2cstat = 0x00;
    s->i2cds   = 0xFF;
    s->i2clc   = 0x00;
    s->i2cadd  = 0xFF;
    s->scl_free = true;
}

static void exynos4210_i2c_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    Exynos4210I2CState *s = EXYNOS4_I2C(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &exynos4210_i2c_ops, s,
                          TYPE_EXYNOS4_I2C, EXYNOS4_I2C_MEM_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
    s->bus = i2c_init_bus(dev, "i2c");
}

static void exynos4210_i2c_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &exynos4210_i2c_vmstate;
    dc->reset = exynos4210_i2c_reset;
}

static const TypeInfo exynos4210_i2c_type_info = {
    .name = TYPE_EXYNOS4_I2C,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Exynos4210I2CState),
    .instance_init = exynos4210_i2c_init,
    .class_init = exynos4210_i2c_class_init,
};

static void exynos4210_i2c_register_types(void)
{
    type_register_static(&exynos4210_i2c_type_info);
}

type_init(exynos4210_i2c_register_types)

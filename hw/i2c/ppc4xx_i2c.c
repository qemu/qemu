/*
 * PPC4xx I2C controller emulation
 *
 * Documentation: PPC405GP User's Manual, Chapter 22. IIC Bus Interface
 *
 * Copyright (c) 2007 Jocelyn Mayer
 * Copyright (c) 2012 François Revol
 * Copyright (c) 2016-2018 BALATON Zoltan
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
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/i2c/ppc4xx_i2c.h"
#include "hw/irq.h"

#define PPC4xx_I2C_MEM_SIZE 18

enum {
    IIC_MDBUF = 0,
    /* IIC_SDBUF = 2, */
    IIC_LMADR = 4,
    IIC_HMADR,
    IIC_CNTL,
    IIC_MDCNTL,
    IIC_STS,
    IIC_EXTSTS,
    IIC_LSADR,
    IIC_HSADR,
    IIC_CLKDIV,
    IIC_INTRMSK,
    IIC_XFRCNT,
    IIC_XTCNTLSS,
    IIC_DIRECTCNTL
    /* IIC_INTR */
};

#define IIC_CNTL_PT         (1 << 0)
#define IIC_CNTL_READ       (1 << 1)
#define IIC_CNTL_CHT        (1 << 2)
#define IIC_CNTL_RPST       (1 << 3)
#define IIC_CNTL_AMD        (1 << 6)
#define IIC_CNTL_HMT        (1 << 7)

#define IIC_MDCNTL_EINT     (1 << 2)
#define IIC_MDCNTL_ESM      (1 << 3)
#define IIC_MDCNTL_FMDB     (1 << 6)

#define IIC_STS_PT          (1 << 0)
#define IIC_STS_IRQA        (1 << 1)
#define IIC_STS_ERR         (1 << 2)
#define IIC_STS_MDBF        (1 << 4)
#define IIC_STS_MDBS        (1 << 5)

#define IIC_EXTSTS_XFRA     (1 << 0)
#define IIC_EXTSTS_BCS_FREE (4 << 4)
#define IIC_EXTSTS_BCS_BUSY (5 << 4)

#define IIC_INTRMSK_EIMTC   (1 << 0)
#define IIC_INTRMSK_EITA    (1 << 1)
#define IIC_INTRMSK_EIIC    (1 << 2)
#define IIC_INTRMSK_EIHE    (1 << 3)

#define IIC_XTCNTLSS_SRST   (1 << 0)

#define IIC_DIRECTCNTL_SDAC (1 << 3)
#define IIC_DIRECTCNTL_SCLC (1 << 2)
#define IIC_DIRECTCNTL_MSDA (1 << 1)
#define IIC_DIRECTCNTL_MSCL (1 << 0)

static void ppc4xx_i2c_reset(DeviceState *s)
{
    PPC4xxI2CState *i2c = PPC4xx_I2C(s);

    i2c->mdidx = -1;
    memset(i2c->mdata, 0, ARRAY_SIZE(i2c->mdata));
    /* [hl][ms]addr are not affected by reset */
    i2c->cntl = 0;
    i2c->mdcntl = 0;
    i2c->sts = 0;
    i2c->extsts = IIC_EXTSTS_BCS_FREE;
    i2c->clkdiv = 0;
    i2c->intrmsk = 0;
    i2c->xfrcnt = 0;
    i2c->xtcntlss = 0;
    i2c->directcntl = 0xf; /* all non-reserved bits set */
}

static uint64_t ppc4xx_i2c_readb(void *opaque, hwaddr addr, unsigned int size)
{
    PPC4xxI2CState *i2c = PPC4xx_I2C(opaque);
    uint64_t ret;
    int i;

    switch (addr) {
    case IIC_MDBUF:
        if (i2c->mdidx < 0) {
            ret = 0xff;
            break;
        }
        ret = i2c->mdata[0];
        if (i2c->mdidx == 3) {
            i2c->sts &= ~IIC_STS_MDBF;
        } else if (i2c->mdidx == 0) {
            i2c->sts &= ~IIC_STS_MDBS;
        }
        for (i = 0; i < i2c->mdidx; i++) {
            i2c->mdata[i] = i2c->mdata[i + 1];
        }
        if (i2c->mdidx >= 0) {
            i2c->mdidx--;
        }
        break;
    case IIC_LMADR:
        ret = i2c->lmadr;
        break;
    case IIC_HMADR:
        ret = i2c->hmadr;
        break;
    case IIC_CNTL:
        ret = i2c->cntl;
        break;
    case IIC_MDCNTL:
        ret = i2c->mdcntl;
        break;
    case IIC_STS:
        ret = i2c->sts;
        break;
    case IIC_EXTSTS:
        ret = i2c_bus_busy(i2c->bus) ?
              IIC_EXTSTS_BCS_BUSY : IIC_EXTSTS_BCS_FREE;
        break;
    case IIC_LSADR:
        ret = i2c->lsadr;
        break;
    case IIC_HSADR:
        ret = i2c->hsadr;
        break;
    case IIC_CLKDIV:
        ret = i2c->clkdiv;
        break;
    case IIC_INTRMSK:
        ret = i2c->intrmsk;
        break;
    case IIC_XFRCNT:
        ret = i2c->xfrcnt;
        break;
    case IIC_XTCNTLSS:
        ret = i2c->xtcntlss;
        break;
    case IIC_DIRECTCNTL:
        ret = i2c->directcntl;
        break;
    default:
        if (addr < PPC4xx_I2C_MEM_SIZE) {
            qemu_log_mask(LOG_UNIMP, "%s: Unimplemented register 0x%"
                          HWADDR_PRIx "\n", __func__, addr);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad address 0x%"
                          HWADDR_PRIx "\n", __func__, addr);
        }
        ret = 0;
        break;
    }
    return ret;
}

static void ppc4xx_i2c_writeb(void *opaque, hwaddr addr, uint64_t value,
                              unsigned int size)
{
    PPC4xxI2CState *i2c = opaque;

    switch (addr) {
    case IIC_MDBUF:
        if (i2c->mdidx >= 3) {
            break;
        }
        i2c->mdata[++i2c->mdidx] = value;
        if (i2c->mdidx == 3) {
            i2c->sts |= IIC_STS_MDBF;
        } else if (i2c->mdidx == 0) {
            i2c->sts |= IIC_STS_MDBS;
        }
        break;
    case IIC_LMADR:
        i2c->lmadr = value;
        break;
    case IIC_HMADR:
        i2c->hmadr = value;
        break;
    case IIC_CNTL:
        i2c->cntl = value & ~IIC_CNTL_PT;
        if (value & IIC_CNTL_AMD) {
            qemu_log_mask(LOG_UNIMP, "%s: only 7 bit addresses supported\n",
                          __func__);
        }
        if (value & IIC_CNTL_HMT && i2c_bus_busy(i2c->bus)) {
            i2c_end_transfer(i2c->bus);
            if (i2c->mdcntl & IIC_MDCNTL_EINT &&
                i2c->intrmsk & IIC_INTRMSK_EIHE) {
                i2c->sts |= IIC_STS_IRQA;
                qemu_irq_raise(i2c->irq);
            }
        } else if (value & IIC_CNTL_PT) {
            int recv = (value & IIC_CNTL_READ) >> 1;
            int tct = value >> 4 & 3;
            int i;

            if (recv && (i2c->lmadr >> 1) >= 0x50 && (i2c->lmadr >> 1) < 0x58) {
                /* smbus emulation does not like multi byte reads w/o restart */
                value |= IIC_CNTL_RPST;
            }

            for (i = 0; i <= tct; i++) {
                if (!i2c_bus_busy(i2c->bus)) {
                    i2c->extsts = IIC_EXTSTS_BCS_FREE;
                    if (i2c_start_transfer(i2c->bus, i2c->lmadr >> 1, recv)) {
                        i2c->sts |= IIC_STS_ERR;
                        i2c->extsts |= IIC_EXTSTS_XFRA;
                        break;
                    } else {
                        i2c->sts &= ~IIC_STS_ERR;
                    }
                }
                if (!(i2c->sts & IIC_STS_ERR)) {
                    if (recv) {
                        i2c->mdata[i] = i2c_recv(i2c->bus);
                    } else if (i2c_send(i2c->bus, i2c->mdata[i]) < 0) {
                        i2c->sts |= IIC_STS_ERR;
                        i2c->extsts |= IIC_EXTSTS_XFRA;
                        break;
                    }
                }
                if (value & IIC_CNTL_RPST || !(value & IIC_CNTL_CHT)) {
                    i2c_end_transfer(i2c->bus);
                }
            }
            i2c->xfrcnt = i;
            i2c->mdidx = i - 1;
            if (recv && i2c->mdidx >= 0) {
                i2c->sts |= IIC_STS_MDBS;
            }
            if (recv && i2c->mdidx == 3) {
                i2c->sts |= IIC_STS_MDBF;
            }
            if (i && i2c->mdcntl & IIC_MDCNTL_EINT &&
                i2c->intrmsk & IIC_INTRMSK_EIMTC) {
                i2c->sts |= IIC_STS_IRQA;
                qemu_irq_raise(i2c->irq);
            }
        }
        break;
    case IIC_MDCNTL:
        i2c->mdcntl = value & 0x3d;
        if (value & IIC_MDCNTL_ESM) {
            qemu_log_mask(LOG_UNIMP, "%s: slave mode not implemented\n",
                          __func__);
        }
        if (value & IIC_MDCNTL_FMDB) {
            i2c->mdidx = -1;
            memset(i2c->mdata, 0, ARRAY_SIZE(i2c->mdata));
            i2c->sts &= ~(IIC_STS_MDBF | IIC_STS_MDBS);
        }
        break;
    case IIC_STS:
        i2c->sts &= ~(value & 0x0a);
        if (value & IIC_STS_IRQA && i2c->mdcntl & IIC_MDCNTL_EINT) {
            qemu_irq_lower(i2c->irq);
        }
        break;
    case IIC_EXTSTS:
        i2c->extsts &= ~(value & 0x8f);
        break;
    case IIC_LSADR:
        i2c->lsadr = value;
        break;
    case IIC_HSADR:
        i2c->hsadr = value;
        break;
    case IIC_CLKDIV:
        i2c->clkdiv = value;
        break;
    case IIC_INTRMSK:
        i2c->intrmsk = value;
        break;
    case IIC_XFRCNT:
        i2c->xfrcnt = value & 0x77;
        break;
    case IIC_XTCNTLSS:
        i2c->xtcntlss &= ~(value & 0xf0);
        if (value & IIC_XTCNTLSS_SRST) {
            /* Is it actually a full reset? U-Boot sets some regs before */
            ppc4xx_i2c_reset(DEVICE(i2c));
            break;
        }
        break;
    case IIC_DIRECTCNTL:
        i2c->directcntl = value & (IIC_DIRECTCNTL_SDAC & IIC_DIRECTCNTL_SCLC);
        i2c->directcntl |= (value & IIC_DIRECTCNTL_SCLC ? 1 : 0);
        bitbang_i2c_set(&i2c->bitbang, BITBANG_I2C_SCL,
                        i2c->directcntl & IIC_DIRECTCNTL_MSCL);
        i2c->directcntl |= bitbang_i2c_set(&i2c->bitbang, BITBANG_I2C_SDA,
                               (value & IIC_DIRECTCNTL_SDAC) != 0) << 1;
        break;
    default:
        if (addr < PPC4xx_I2C_MEM_SIZE) {
            qemu_log_mask(LOG_UNIMP, "%s: Unimplemented register 0x%"
                          HWADDR_PRIx "\n", __func__, addr);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad address 0x%"
                          HWADDR_PRIx "\n", __func__, addr);
        }
        break;
    }
}

static const MemoryRegionOps ppc4xx_i2c_ops = {
    .read = ppc4xx_i2c_readb,
    .write = ppc4xx_i2c_writeb,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .impl.min_access_size = 1,
    .impl.max_access_size = 1,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void ppc4xx_i2c_init(Object *o)
{
    PPC4xxI2CState *s = PPC4xx_I2C(o);

    memory_region_init_io(&s->iomem, OBJECT(s), &ppc4xx_i2c_ops, s,
                          TYPE_PPC4xx_I2C, PPC4xx_I2C_MEM_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(s), &s->irq);
    s->bus = i2c_init_bus(DEVICE(s), "i2c");
    bitbang_i2c_init(&s->bitbang, s->bus);
}

static void ppc4xx_i2c_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, ppc4xx_i2c_reset);
}

static const TypeInfo ppc4xx_i2c_type_info = {
    .name = TYPE_PPC4xx_I2C,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PPC4xxI2CState),
    .instance_init = ppc4xx_i2c_init,
    .class_init = ppc4xx_i2c_class_init,
};

static void ppc4xx_i2c_register_types(void)
{
    type_register_static(&ppc4xx_i2c_type_info);
}

type_init(ppc4xx_i2c_register_types)

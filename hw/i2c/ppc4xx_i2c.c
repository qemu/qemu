/*
 * PPC4xx I2C controller emulation
 *
 * Copyright (c) 2007 Jocelyn Mayer
 * Copyright (c) 2012 François Revol
 * Copyright (c) 2016 BALATON Zoltan
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
#include "qemu-common.h"
#include "qemu/log.h"
#include "cpu.h"
#include "hw/hw.h"
#include "hw/i2c/ppc4xx_i2c.h"

#define PPC4xx_I2C_MEM_SIZE 0x12

#define IIC_CNTL_PT         (1 << 0)
#define IIC_CNTL_READ       (1 << 1)
#define IIC_CNTL_CHT        (1 << 2)
#define IIC_CNTL_RPST       (1 << 3)

#define IIC_STS_PT          (1 << 0)
#define IIC_STS_ERR         (1 << 2)
#define IIC_STS_MDBS        (1 << 5)

#define IIC_EXTSTS_XFRA     (1 << 0)

#define IIC_XTCNTLSS_SRST   (1 << 0)

static void ppc4xx_i2c_reset(DeviceState *s)
{
    PPC4xxI2CState *i2c = PPC4xx_I2C(s);

    /* FIXME: Should also reset bus?
     *if (s->address != ADDR_RESET) {
     *    i2c_end_transfer(s->bus);
     *}
     */

    i2c->mdata = 0;
    i2c->lmadr = 0;
    i2c->hmadr = 0;
    i2c->cntl = 0;
    i2c->mdcntl = 0;
    i2c->sts = 0;
    i2c->extsts = 0x8f;
    i2c->sdata = 0;
    i2c->lsadr = 0;
    i2c->hsadr = 0;
    i2c->clkdiv = 0;
    i2c->intrmsk = 0;
    i2c->xfrcnt = 0;
    i2c->xtcntlss = 0;
    i2c->directcntl = 0x0f;
    i2c->intr = 0;
}

static inline bool ppc4xx_i2c_is_master(PPC4xxI2CState *i2c)
{
    return true;
}

static uint64_t ppc4xx_i2c_readb(void *opaque, hwaddr addr, unsigned int size)
{
    PPC4xxI2CState *i2c = PPC4xx_I2C(opaque);
    uint64_t ret;

    switch (addr) {
    case 0x00:
        ret = i2c->mdata;
        if (ppc4xx_i2c_is_master(i2c)) {
            ret = 0xff;

            if (!(i2c->sts & IIC_STS_MDBS)) {
                qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Trying to read "
                              "without starting transfer\n",
                              TYPE_PPC4xx_I2C, __func__);
            } else {
                int pending = (i2c->cntl >> 4) & 3;

                /* get the next byte */
                int byte = i2c_recv(i2c->bus);

                if (byte < 0) {
                    qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: read failed "
                                  "for device 0x%02x\n", TYPE_PPC4xx_I2C,
                                  __func__, i2c->lmadr);
                    ret = 0xff;
                } else {
                    ret = byte;
                    /* Raise interrupt if enabled */
                    /*ppc4xx_i2c_raise_interrupt(i2c)*/;
                }

                if (!pending) {
                    i2c->sts &= ~IIC_STS_MDBS;
                    /*i2c_end_transfer(i2c->bus);*/
                /*} else if (i2c->cntl & (IIC_CNTL_RPST | IIC_CNTL_CHT)) {*/
                } else if (pending) {
                    /* current smbus implementation doesn't like
                       multibyte xfer repeated start */
                    i2c_end_transfer(i2c->bus);
                    if (i2c_start_transfer(i2c->bus, i2c->lmadr >> 1, 1)) {
                        /* if non zero is returned, the adress is not valid */
                        i2c->sts &= ~IIC_STS_PT;
                        i2c->sts |= IIC_STS_ERR;
                        i2c->extsts |= IIC_EXTSTS_XFRA;
                    } else {
                        /*i2c->sts |= IIC_STS_PT;*/
                        i2c->sts |= IIC_STS_MDBS;
                        i2c->sts &= ~IIC_STS_ERR;
                        i2c->extsts = 0;
                    }
                }
                pending--;
                i2c->cntl = (i2c->cntl & 0xcf) | (pending << 4);
            }
        } else {
            qemu_log_mask(LOG_UNIMP, "[%s]%s: slave mode not implemented\n",
                          TYPE_PPC4xx_I2C, __func__);
        }
        break;
    case 0x02:
        ret = i2c->sdata;
        break;
    case 0x04:
        ret = i2c->lmadr;
        break;
    case 0x05:
        ret = i2c->hmadr;
        break;
    case 0x06:
        ret = i2c->cntl;
        break;
    case 0x07:
        ret = i2c->mdcntl;
        break;
    case 0x08:
        ret = i2c->sts;
        break;
    case 0x09:
        ret = i2c->extsts;
        break;
    case 0x0A:
        ret = i2c->lsadr;
        break;
    case 0x0B:
        ret = i2c->hsadr;
        break;
    case 0x0C:
        ret = i2c->clkdiv;
        break;
    case 0x0D:
        ret = i2c->intrmsk;
        break;
    case 0x0E:
        ret = i2c->xfrcnt;
        break;
    case 0x0F:
        ret = i2c->xtcntlss;
        break;
    case 0x10:
        ret = i2c->directcntl;
        break;
    case 0x11:
        ret = i2c->intr;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad address at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_PPC4xx_I2C, __func__, addr);
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
    case 0x00:
        i2c->mdata = value;
        if (!i2c_bus_busy(i2c->bus)) {
            /* assume we start a write transfer */
            if (i2c_start_transfer(i2c->bus, i2c->lmadr >> 1, 0)) {
                /* if non zero is returned, the adress is not valid */
                i2c->sts &= ~IIC_STS_PT;
                i2c->sts |= IIC_STS_ERR;
                i2c->extsts |= IIC_EXTSTS_XFRA;
            } else {
                i2c->sts |= IIC_STS_PT;
                i2c->sts &= ~IIC_STS_ERR;
                i2c->extsts = 0;
            }
        }
        if (i2c_bus_busy(i2c->bus)) {
            if (i2c_send(i2c->bus, i2c->mdata)) {
                /* if the target return non zero then end the transfer */
                i2c->sts &= ~IIC_STS_PT;
                i2c->sts |= IIC_STS_ERR;
                i2c->extsts |= IIC_EXTSTS_XFRA;
                i2c_end_transfer(i2c->bus);
            }
        }
        break;
    case 0x02:
        i2c->sdata = value;
        break;
    case 0x04:
        i2c->lmadr = value;
        if (i2c_bus_busy(i2c->bus)) {
            i2c_end_transfer(i2c->bus);
        }
        break;
    case 0x05:
        i2c->hmadr = value;
        break;
    case 0x06:
        i2c->cntl = value;
        if (i2c->cntl & IIC_CNTL_PT) {
            if (i2c->cntl & IIC_CNTL_READ) {
                if (i2c_bus_busy(i2c->bus)) {
                    /* end previous transfer */
                    i2c->sts &= ~IIC_STS_PT;
                    i2c_end_transfer(i2c->bus);
                }
                if (i2c_start_transfer(i2c->bus, i2c->lmadr >> 1, 1)) {
                    /* if non zero is returned, the adress is not valid */
                    i2c->sts &= ~IIC_STS_PT;
                    i2c->sts |= IIC_STS_ERR;
                    i2c->extsts |= IIC_EXTSTS_XFRA;
                } else {
                    /*i2c->sts |= IIC_STS_PT;*/
                    i2c->sts |= IIC_STS_MDBS;
                    i2c->sts &= ~IIC_STS_ERR;
                    i2c->extsts = 0;
                }
            } else {
                /* we actually already did the write transfer... */
                i2c->sts &= ~IIC_STS_PT;
            }
        }
        break;
    case 0x07:
        i2c->mdcntl = value & 0xDF;
        break;
    case 0x08:
        i2c->sts &= ~(value & 0x0A);
        break;
    case 0x09:
        i2c->extsts &= ~(value & 0x8F);
        break;
    case 0x0A:
        i2c->lsadr = value;
        /*i2c_set_slave_address(i2c->bus, i2c->lsadr);*/
        break;
    case 0x0B:
        i2c->hsadr = value;
        break;
    case 0x0C:
        i2c->clkdiv = value;
        break;
    case 0x0D:
        i2c->intrmsk = value;
        break;
    case 0x0E:
        i2c->xfrcnt = value & 0x77;
        break;
    case 0x0F:
        if (value & IIC_XTCNTLSS_SRST) {
            /* Is it actually a full reset? U-Boot sets some regs before */
            ppc4xx_i2c_reset(DEVICE(i2c));
            break;
        }
        i2c->xtcntlss = value;
        break;
    case 0x10:
        i2c->directcntl = value & 0x7;
        break;
    case 0x11:
        i2c->intr = value;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad address at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_PPC4xx_I2C, __func__, addr);
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
}

static void ppc4xx_i2c_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = ppc4xx_i2c_reset;
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

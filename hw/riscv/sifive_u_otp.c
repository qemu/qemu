/*
 * QEMU SiFive U OTP (One-Time Programmable) Memory interface
 *
 * Copyright (c) 2019 Bin Meng <bmeng.cn@gmail.com>
 *
 * Simple model of the OTP to emulate register reads made by the SDK BSP
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/riscv/sifive_u_otp.h"

static uint64_t sifive_u_otp_read(void *opaque, hwaddr addr, unsigned int size)
{
    SiFiveUOTPState *s = opaque;

    switch (addr) {
    case SIFIVE_U_OTP_PA:
        return s->pa;
    case SIFIVE_U_OTP_PAIO:
        return s->paio;
    case SIFIVE_U_OTP_PAS:
        return s->pas;
    case SIFIVE_U_OTP_PCE:
        return s->pce;
    case SIFIVE_U_OTP_PCLK:
        return s->pclk;
    case SIFIVE_U_OTP_PDIN:
        return s->pdin;
    case SIFIVE_U_OTP_PDOUT:
        if ((s->pce & SIFIVE_U_OTP_PCE_EN) &&
            (s->pdstb & SIFIVE_U_OTP_PDSTB_EN) &&
            (s->ptrim & SIFIVE_U_OTP_PTRIM_EN)) {
            return s->fuse[s->pa & SIFIVE_U_OTP_PA_MASK];
        } else {
            return 0xff;
        }
    case SIFIVE_U_OTP_PDSTB:
        return s->pdstb;
    case SIFIVE_U_OTP_PPROG:
        return s->pprog;
    case SIFIVE_U_OTP_PTC:
        return s->ptc;
    case SIFIVE_U_OTP_PTM:
        return s->ptm;
    case SIFIVE_U_OTP_PTM_REP:
        return s->ptm_rep;
    case SIFIVE_U_OTP_PTR:
        return s->ptr;
    case SIFIVE_U_OTP_PTRIM:
        return s->ptrim;
    case SIFIVE_U_OTP_PWE:
        return s->pwe;
    }

    qemu_log_mask(LOG_GUEST_ERROR, "%s: read: addr=0x%" HWADDR_PRIx "\n",
                  __func__, addr);
    return 0;
}

static void sifive_u_otp_write(void *opaque, hwaddr addr,
                               uint64_t val64, unsigned int size)
{
    SiFiveUOTPState *s = opaque;
    uint32_t val32 = (uint32_t)val64;

    switch (addr) {
    case SIFIVE_U_OTP_PA:
        s->pa = val32 & SIFIVE_U_OTP_PA_MASK;
        break;
    case SIFIVE_U_OTP_PAIO:
        s->paio = val32;
        break;
    case SIFIVE_U_OTP_PAS:
        s->pas = val32;
        break;
    case SIFIVE_U_OTP_PCE:
        s->pce = val32;
        break;
    case SIFIVE_U_OTP_PCLK:
        s->pclk = val32;
        break;
    case SIFIVE_U_OTP_PDIN:
        s->pdin = val32;
        break;
    case SIFIVE_U_OTP_PDOUT:
        /* read-only */
        break;
    case SIFIVE_U_OTP_PDSTB:
        s->pdstb = val32;
        break;
    case SIFIVE_U_OTP_PPROG:
        s->pprog = val32;
        break;
    case SIFIVE_U_OTP_PTC:
        s->ptc = val32;
        break;
    case SIFIVE_U_OTP_PTM:
        s->ptm = val32;
        break;
    case SIFIVE_U_OTP_PTM_REP:
        s->ptm_rep = val32;
        break;
    case SIFIVE_U_OTP_PTR:
        s->ptr = val32;
        break;
    case SIFIVE_U_OTP_PTRIM:
        s->ptrim = val32;
        break;
    case SIFIVE_U_OTP_PWE:
        s->pwe = val32;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write: addr=0x%" HWADDR_PRIx
                      " v=0x%x\n", __func__, addr, val32);
    }
}

static const MemoryRegionOps sifive_u_otp_ops = {
    .read = sifive_u_otp_read,
    .write = sifive_u_otp_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static Property sifive_u_otp_properties[] = {
    DEFINE_PROP_UINT32("serial", SiFiveUOTPState, serial, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void sifive_u_otp_realize(DeviceState *dev, Error **errp)
{
    SiFiveUOTPState *s = SIFIVE_U_OTP(dev);

    memory_region_init_io(&s->mmio, OBJECT(dev), &sifive_u_otp_ops, s,
                          TYPE_SIFIVE_U_OTP, SIFIVE_U_OTP_REG_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);
}

static void sifive_u_otp_reset(DeviceState *dev)
{
    SiFiveUOTPState *s = SIFIVE_U_OTP(dev);

    /* Initialize all fuses' initial value to 0xFFs */
    memset(s->fuse, 0xff, sizeof(s->fuse));

    /* Make a valid content of serial number */
    s->fuse[SIFIVE_U_OTP_SERIAL_ADDR] = s->serial;
    s->fuse[SIFIVE_U_OTP_SERIAL_ADDR + 1] = ~(s->serial);
}

static void sifive_u_otp_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, sifive_u_otp_properties);
    dc->realize = sifive_u_otp_realize;
    dc->reset = sifive_u_otp_reset;
}

static const TypeInfo sifive_u_otp_info = {
    .name          = TYPE_SIFIVE_U_OTP,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SiFiveUOTPState),
    .class_init    = sifive_u_otp_class_init,
};

static void sifive_u_otp_register_types(void)
{
    type_register_static(&sifive_u_otp_info);
}

type_init(sifive_u_otp_register_types)

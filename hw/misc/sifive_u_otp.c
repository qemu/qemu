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
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/sysbus.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/misc/sifive_u_otp.h"
#include "system/blockdev.h"
#include "system/block-backend.h"

#define WRITTEN_BIT_ON 0x1

#define SET_FUSEARRAY_BIT(map, i, off, bit)    \
    map[i] = bit ? (map[i] | bit << off) : (map[i] & ~(0x1 << off))

#define GET_FUSEARRAY_BIT(map, i, off)    \
    ((map[i] >> off) & 0x1)

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

            /* read from backend */
            if (s->blk) {
                int32_t buf;

                if (blk_pread(s->blk, s->pa * SIFIVE_U_OTP_FUSE_WORD,
                              SIFIVE_U_OTP_FUSE_WORD, &buf, 0) < 0) {
                    error_report("read error index<%d>", s->pa);
                    return 0xff;
                }

                return buf;
            }

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
        s->pwe = val32 & SIFIVE_U_OTP_PWE_EN;

        /* PWE is enabled. Ignore PAS=1 (no redundancy cell) */
        if (s->pwe && !s->pas) {
            if (GET_FUSEARRAY_BIT(s->fuse_wo, s->pa, s->paio)) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "write once error: idx<%u>, bit<%u>\n",
                              s->pa, s->paio);
                break;
            }

            /* write bit data */
            SET_FUSEARRAY_BIT(s->fuse, s->pa, s->paio, s->pdin);

            /* write to backend */
            if (s->blk) {
                if (blk_pwrite(s->blk, s->pa * SIFIVE_U_OTP_FUSE_WORD,
                               SIFIVE_U_OTP_FUSE_WORD, &s->fuse[s->pa], 0)
                    < 0) {
                    error_report("write error index<%d>", s->pa);
                }
            }

            /* update written bit */
            SET_FUSEARRAY_BIT(s->fuse_wo, s->pa, s->paio, WRITTEN_BIT_ON);
        }

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

static const Property sifive_u_otp_properties[] = {
    DEFINE_PROP_UINT32("serial", SiFiveUOTPState, serial, 0),
    DEFINE_PROP_DRIVE("drive", SiFiveUOTPState, blk),
};

static void sifive_u_otp_realize(DeviceState *dev, Error **errp)
{
    SiFiveUOTPState *s = SIFIVE_U_OTP(dev);
    DriveInfo *dinfo;

    memory_region_init_io(&s->mmio, OBJECT(dev), &sifive_u_otp_ops, s,
                          TYPE_SIFIVE_U_OTP, SIFIVE_U_OTP_REG_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);

    dinfo = drive_get(IF_PFLASH, 0, 0);
    if (dinfo) {
        int ret;
        uint64_t perm;
        int filesize;
        BlockBackend *blk;

        blk = blk_by_legacy_dinfo(dinfo);
        filesize = SIFIVE_U_OTP_NUM_FUSES * SIFIVE_U_OTP_FUSE_WORD;
        if (blk_getlength(blk) < filesize) {
            error_setg(errp, "OTP drive size < 16K");
            return;
        }

        qdev_prop_set_drive_err(dev, "drive", blk, errp);

        if (s->blk) {
            perm = BLK_PERM_CONSISTENT_READ |
                   (blk_supports_write_perm(s->blk) ? BLK_PERM_WRITE : 0);
            ret = blk_set_perm(s->blk, perm, BLK_PERM_ALL, errp);
            if (ret < 0) {
                return;
            }

            if (blk_pread(s->blk, 0, filesize, s->fuse, 0) < 0) {
                error_setg(errp, "failed to read the initial flash content");
                return;
            }
        }
    }

    /* Initialize all fuses' initial value to 0xFFs */
    memset(s->fuse, 0xff, sizeof(s->fuse));

    /* Make a valid content of serial number */
    s->fuse[SIFIVE_U_OTP_SERIAL_ADDR] = s->serial;
    s->fuse[SIFIVE_U_OTP_SERIAL_ADDR + 1] = ~(s->serial);

    if (s->blk) {
        /* Put serial number to backend as well*/
        uint32_t serial_data;
        int index = SIFIVE_U_OTP_SERIAL_ADDR;

        serial_data = s->serial;
        if (blk_pwrite(s->blk, index * SIFIVE_U_OTP_FUSE_WORD,
                       SIFIVE_U_OTP_FUSE_WORD, &serial_data, 0) < 0) {
            error_setg(errp, "failed to write index<%d>", index);
            return;
        }

        serial_data = ~(s->serial);
        if (blk_pwrite(s->blk, (index + 1) * SIFIVE_U_OTP_FUSE_WORD,
                       SIFIVE_U_OTP_FUSE_WORD, &serial_data, 0) < 0) {
            error_setg(errp, "failed to write index<%d>", index + 1);
            return;
        }
    }

    /* Initialize write-once map */
    memset(s->fuse_wo, 0x00, sizeof(s->fuse_wo));
}

static void sifive_u_otp_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, sifive_u_otp_properties);
    dc->realize = sifive_u_otp_realize;
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

/*
 * Csky nand flash controller emulation.
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
#include "qapi/error.h"
#include "qemu-common.h"
#include "hw/hw.h"
#include "hw/block/flash.h"
#include "hw/irq.h"
#include "sysemu/block-backend.h"
#include "sysemu/blockdev.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "hw/sysbus.h"
#include "qemu/log.h"

#define NFC_EN 0x0
#define IMASK 0x4
#define DEVICE_CFG 0x8
#define IDR 0xc
#define COLAR 0x10
#define ROWAR 0x14
#define CMDR 0x18
#define SR 0x1c
#define ECC_CODE1 0x20
#define ECC_CODE2 0x24
#define WPR 0x28
#define TIMOUT 0x2c

#define TYPE_CSKY_NAND  "csky_nand"
#define CSKY_NAND(obj)  OBJECT_CHECK(csky_nand_state, (obj), TYPE_CSKY_NAND)

typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    DeviceState *nand;
    uint8_t buf0[0x880];
    uint8_t buf1[0x880];
    uint32_t nfc_en;
    uint32_t imask;
    uint32_t device_cfg;
    uint32_t idr;
    uint32_t colar;
    uint32_t rowar;
    uint32_t cmdr;
    uint32_t sr;
    uint32_t ecc_code1;
    uint32_t ecc_code2;
    uint32_t wpr;
    uint32_t timout;
    qemu_irq irq;
} csky_nand_state;

static uint64_t csky_nand_read(void *opaque, hwaddr addr, unsigned size)
{
    csky_nand_state *s = (csky_nand_state *) opaque;

    if (size == 1) {
        if (0x1000 <= addr && addr <= 0x187f) {
            return s->buf0[addr - 0x1000];
        } else if (0x2000 <= addr && addr <= 0x287f) {
            return s->buf0[addr - 0x2000];
        } else {
            printf("csky_nand_read: Bad offset %x\n", (int)addr);
            return 0;
        }
    } else if (size == 4) {
        switch (addr) {
        case NFC_EN:
            return s->nfc_en;
        case IMASK:
            return s->imask;
        case DEVICE_CFG:
            return s->device_cfg;
        case IDR:
            return s->idr;
        case COLAR:
            return s->colar;
        case ROWAR:
            return s->rowar;
        case CMDR:
            return s->cmdr;
        case SR:
            return s->sr;
        case ECC_CODE1:
            return s->ecc_code1;
        case ECC_CODE2:
            return s->ecc_code2;
        case WPR:
            return s->wpr;
        case TIMOUT:
            return s->timout;
        default:
            if (0x1000 <= addr && addr <= 0x187f) {
                return (int)s->buf0[addr - 0x1000] |
                    (s->buf0[addr - 0x1000 + 1] << 8) |
                    (s->buf0[addr - 0x1000 + 2] << 16) |
                    (s->buf0[addr - 0x1000 + 3] << 24);
            } else if (0x2000 <= addr && addr <= 0x287f) {
                return (int)s->buf1[addr - 0x2000] |
                    (s->buf1[addr - 0x2000 + 1] << 8) |
                    (s->buf1[addr - 0x2000 + 2] << 16) |
                    (s->buf1[addr - 0x2000 + 3] << 24);
            } else {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "csky_nand_read: Bad offset %x\n", (int)addr);
                return 0;
            }
        }
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_nand_read: Bad access size\n");
    }
    return 0;
}

static void csky_nand_write(void *opaque, hwaddr addr, uint64_t value,
                            unsigned size)
{
    csky_nand_state *s = (csky_nand_state *) opaque;
    int i;

    if (size == 1) {
        if (0x1000 <= addr && addr <= 0x187f) {
            s->buf0[addr - 0x1000] = value;
        } else if (0x2000 <= addr && addr <= 0x287f) {
            s->buf0[addr - 0x2000] = value;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "csky_nand_write: Bad offset %x\n", (int)addr);
        }
    } else if (size == 4) {
        switch (addr) {
        case NFC_EN:
            s->nfc_en = value;
            break;
        case IMASK:
            s->imask = value;
            break;
        case DEVICE_CFG:
            s->device_cfg = value;
            break;
        case IDR:  /* read only */
            break;
        case COLAR:
            s->colar = value;
            break;
        case ROWAR:
            s->rowar = value;
            break;
        case CMDR:
            s->cmdr = value;
            switch (s->cmdr & 0xf) {
            case 0x0: /* read page */
                nand_setpins(s->nand, 1, 0, 0, 1, 0); /* cmd */
                nand_setio(s->nand, 0x00);
                nand_setpins(s->nand, 0, 1, 0, 1, 0); /* addr */
                nand_setio(s->nand, s->colar & 0xff);
                nand_setio(s->nand, s->rowar & 0xff);
                nand_setio(s->nand, (s->rowar >> 8) & 0xff);
                nand_setio(s->nand, (s->rowar >> 16) & 0xff);
                if (s->cmdr & 0x40) {
                    for (i = 0; i < 528; i++) {
                        s->buf1[i] = nand_getio(s->nand);
                    }
                } else {
                    for (i = 0; i < 528; i++) {
                        s->buf0[i] = nand_getio(s->nand);
                    }
                }
                break;
            case 0x6: /* erase block */
                nand_setpins(s->nand, 1, 0, 0, 1, 0); /* cmd */
                nand_setio(s->nand, 0x60);
                nand_setpins(s->nand, 0, 1, 0, 1, 0); /* addr */
                nand_setio(s->nand, s->rowar & 0xff);
                nand_setio(s->nand, (s->rowar >> 8) & 0xff);
                nand_setio(s->nand, (s->rowar >> 16) & 0xff);
                nand_setpins(s->nand, 1, 0, 0, 1, 0); /* cmd */
                nand_setio(s->nand, 0xd0);
                break;
            case 0x7: /* read status */
                nand_setpins(s->nand, 1, 0, 0, 1, 0); /* cmd */
                nand_setio(s->nand, 0x70);
                i = nand_getio(s->nand);
                s->sr = ((i & 0x1) << 1) | ((i & 0x40) >> 6);
                s->wpr = (i & 0x80) >> 7;
                break;
            case 0x8:  /* program page */
                nand_setpins(s->nand, 1, 0, 0, 1, 0); /* cmd */
                nand_setio(s->nand, 0x80);
                nand_setpins(s->nand, 0, 1, 0, 1, 0); /* addr */
                nand_setio(s->nand, s->colar & 0xff);
                nand_setio(s->nand, s->rowar & 0xff);
                nand_setio(s->nand, (s->rowar >> 8) & 0xff);
                nand_setio(s->nand, (s->rowar >> 16) & 0xff);
                nand_setpins(s->nand, 0, 0, 0, 1, 0); /* data */
                if (s->cmdr & 0x40) {
                    for (i = 0; i < 528; i++) {
                        nand_setio(s->nand, s->buf1[i]);
                    }
                } else {
                    for (i = 0; i < 528; i++) {
                        nand_setio(s->nand, s->buf0[i]);
                    }
                }

                nand_setpins(s->nand, 1, 0, 0, 1, 0); /* cmd */
                nand_setio(s->nand, 0x10);
                break;
            case 0x9: /* read id */
                nand_setpins(s->nand, 1, 0, 0, 1, 0); /* cmd */
                nand_setio(s->nand, 0x90);
                nand_setpins(s->nand, 0, 1, 0, 1, 0); /* addr */
                nand_setio(s->nand, 0x00);
                for (i = 0; i < 4; i++) {
                    s->idr |= nand_getio(s->nand) << i * 8;
                }
                break;
            case 0xe: /* read parameter page */
                break;
            case 0xf:  /* reset */
                nand_setpins(s->nand, 1, 0, 0, 1, 0); /* cmd */
                nand_setio(s->nand, 0xff);
                break;
            default:
                qemu_log_mask(LOG_GUEST_ERROR,
                              "csky_nand_write: Bad offset %x\n", (int)addr);
            }
            break;
        case SR:
            break;
        case ECC_CODE1:
            break;
        case ECC_CODE2:
            break;
        case WPR:
            s->wpr = value;
            break;
        case TIMOUT:
            s->timout = value;
            break;
        default:
            if (0x1000 <= addr && addr <= 0x187f) {
                s->buf0[addr - 0x1000] = value & 0xff;
                s->buf0[addr - 0x1000 + 1] = (value >> 8) & 0xff;
                s->buf0[addr - 0x1000 + 2] = (value >> 16) & 0xff;
                s->buf0[addr - 0x1000 + 3] = (value >> 24) & 0xff;
            } else if (0x2000 <= addr && addr <= 0x287f) {
                s->buf1[addr - 0x2000] = value & 0xff;
                s->buf1[addr - 0x2000 + 1] = (value >> 8) & 0xff;
                s->buf1[addr - 0x2000 + 2] = (value >> 16) & 0xff;
                s->buf1[addr - 0x2000 + 3] = (value >> 24) & 0xff;
            } else {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "csky_nand_write: Bad offset %x\n", (int)addr);
            }
        }
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_nand_write: Bad access size\n");
    }
}

static const MemoryRegionOps csky_nand_ops = {
    .read = csky_nand_read,
    .write = csky_nand_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int csky_nand_init(SysBusDevice *sbd)
{
    DeviceState *dev = DEVICE(sbd);
    csky_nand_state *s = CSKY_NAND(dev);
    DriveInfo *nand = NULL;

    s->idr = 0;
    s->sr = 0;
    s->ecc_code1 = 0;
    s->ecc_code2 = 0;
    s->timout = 0xffffffff;
    s->nand = nand_init(nand ? blk_by_legacy_dinfo(nand) : NULL, 0xec, 0xa2);
    sysbus_init_irq(sbd, &s->irq);
    memory_region_init_io(&s->iomem, OBJECT(s), &csky_nand_ops, s,
                          TYPE_CSKY_NAND, 0x3000);
    sysbus_init_mmio(sbd, &s->iomem);

    return 0;
}

static void csky_nand_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = csky_nand_init;
}

static const TypeInfo onenand_info = {
    .name          = TYPE_CSKY_NAND,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(csky_nand_state),
    .class_init    = csky_nand_class_init,
};

static void csky_nand_register_types(void)
{
    type_register_static(&onenand_info);
}

type_init(csky_nand_register_types)

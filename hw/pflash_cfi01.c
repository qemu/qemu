/*
 *  CFI parallel flash with Intel command set emulation
 *
 *  Copyright (c) 2006 Thorsten Zitterell
 *  Copyright (c) 2005 Jocelyn Mayer
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

/*
 * For now, this code can emulate flashes of 1, 2 or 4 bytes width.
 * Supported commands/modes are:
 * - flash read
 * - flash write
 * - flash ID read
 * - sector erase
 * - CFI queries
 *
 * It does not support timings
 * It does not support flash interleaving
 * It does not implement software data protection as found in many real chips
 * It does not implement erase suspend/resume commands
 * It does not implement multiple sectors erase
 *
 * It does not implement much more ...
 */

#include "hw.h"
#include "flash.h"
#include "block.h"
#include "qemu-timer.h"
#include "exec-memory.h"
#include "host-utils.h"
#include "sysbus.h"

#define PFLASH_BUG(fmt, ...) \
do { \
    printf("PFLASH: Possible BUG - " fmt, ## __VA_ARGS__); \
    exit(1); \
} while(0)

/* #define PFLASH_DEBUG */
#ifdef PFLASH_DEBUG
#define DPRINTF(fmt, ...)                          \
do {                                               \
    printf("PFLASH: " fmt , ## __VA_ARGS__);       \
} while (0)
#else
#define DPRINTF(fmt, ...) do { } while (0)
#endif

struct pflash_t {
    SysBusDevice busdev;
    BlockDriverState *bs;
    uint32_t nb_blocs;
    uint64_t sector_len;
    uint8_t width;
    uint8_t be;
    int wcycle; /* if 0, the flash is read normally */
    int bypass;
    int ro;
    uint8_t cmd;
    uint8_t status;
    uint16_t ident0;
    uint16_t ident1;
    uint16_t ident2;
    uint16_t ident3;
    uint8_t cfi_len;
    uint8_t cfi_table[0x52];
    hwaddr counter;
    unsigned int writeblock_size;
    QEMUTimer *timer;
    MemoryRegion mem;
    char *name;
    void *storage;
};

static void pflash_timer (void *opaque)
{
    pflash_t *pfl = opaque;

    DPRINTF("%s: command %02x done\n", __func__, pfl->cmd);
    /* Reset flash */
    pfl->status ^= 0x80;
    if (pfl->bypass) {
        pfl->wcycle = 2;
    } else {
        memory_region_rom_device_set_readable(&pfl->mem, true);
        pfl->wcycle = 0;
    }
    pfl->cmd = 0;
}

static uint32_t pflash_read (pflash_t *pfl, hwaddr offset,
                             int width, int be)
{
    hwaddr boff;
    uint32_t ret;
    uint8_t *p;

    ret = -1;
    boff = offset & 0xFF; /* why this here ?? */

    if (pfl->width == 2)
        boff = boff >> 1;
    else if (pfl->width == 4)
        boff = boff >> 2;

#if 0
    DPRINTF("%s: reading offset " TARGET_FMT_plx " under cmd %02x width %d\n",
            __func__, offset, pfl->cmd, width);
#endif
    switch (pfl->cmd) {
    case 0x00:
        /* Flash area read */
        p = pfl->storage;
        switch (width) {
        case 1:
            ret = p[offset];
            DPRINTF("%s: data offset " TARGET_FMT_plx " %02x\n",
                    __func__, offset, ret);
            break;
        case 2:
            if (be) {
                ret = p[offset] << 8;
                ret |= p[offset + 1];
            } else {
                ret = p[offset];
                ret |= p[offset + 1] << 8;
            }
            DPRINTF("%s: data offset " TARGET_FMT_plx " %04x\n",
                    __func__, offset, ret);
            break;
        case 4:
            if (be) {
                ret = p[offset] << 24;
                ret |= p[offset + 1] << 16;
                ret |= p[offset + 2] << 8;
                ret |= p[offset + 3];
            } else {
                ret = p[offset];
                ret |= p[offset + 1] << 8;
                ret |= p[offset + 2] << 16;
                ret |= p[offset + 3] << 24;
            }
            DPRINTF("%s: data offset " TARGET_FMT_plx " %08x\n",
                    __func__, offset, ret);
            break;
        default:
            DPRINTF("BUG in %s\n", __func__);
        }

        break;
    case 0x20: /* Block erase */
    case 0x50: /* Clear status register */
    case 0x60: /* Block /un)lock */
    case 0x70: /* Status Register */
    case 0xe8: /* Write block */
        /* Status register read */
        ret = pfl->status;
        DPRINTF("%s: status %x\n", __func__, ret);
        break;
    case 0x90:
        switch (boff) {
        case 0:
            ret = pfl->ident0 << 8 | pfl->ident1;
            DPRINTF("%s: Manufacturer Code %04x\n", __func__, ret);
            break;
        case 1:
            ret = pfl->ident2 << 8 | pfl->ident3;
            DPRINTF("%s: Device ID Code %04x\n", __func__, ret);
            break;
        default:
            DPRINTF("%s: Read Device Information boff=%x\n", __func__,
                    (unsigned)boff);
            ret = 0;
            break;
        }
        break;
    case 0x98: /* Query mode */
        if (boff > pfl->cfi_len)
            ret = 0;
        else
            ret = pfl->cfi_table[boff];
        break;
    default:
        /* This should never happen : reset state & treat it as a read */
        DPRINTF("%s: unknown command state: %x\n", __func__, pfl->cmd);
        pfl->wcycle = 0;
        pfl->cmd = 0;
    }
    return ret;
}

/* update flash content on disk */
static void pflash_update(pflash_t *pfl, int offset,
                          int size)
{
    int offset_end;
    if (pfl->bs) {
        offset_end = offset + size;
        /* round to sectors */
        offset = offset >> 9;
        offset_end = (offset_end + 511) >> 9;
        bdrv_write(pfl->bs, offset, pfl->storage + (offset << 9),
                   offset_end - offset);
    }
}

static inline void pflash_data_write(pflash_t *pfl, hwaddr offset,
                                     uint32_t value, int width, int be)
{
    uint8_t *p = pfl->storage;

    DPRINTF("%s: block write offset " TARGET_FMT_plx
            " value %x counter " TARGET_FMT_plx "\n",
            __func__, offset, value, pfl->counter);
    switch (width) {
    case 1:
        p[offset] = value;
        break;
    case 2:
        if (be) {
            p[offset] = value >> 8;
            p[offset + 1] = value;
        } else {
            p[offset] = value;
            p[offset + 1] = value >> 8;
        }
        break;
    case 4:
        if (be) {
            p[offset] = value >> 24;
            p[offset + 1] = value >> 16;
            p[offset + 2] = value >> 8;
            p[offset + 3] = value;
        } else {
            p[offset] = value;
            p[offset + 1] = value >> 8;
            p[offset + 2] = value >> 16;
            p[offset + 3] = value >> 24;
        }
        break;
    }

}

static void pflash_write(pflash_t *pfl, hwaddr offset,
                         uint32_t value, int width, int be)
{
    uint8_t *p;
    uint8_t cmd;

    cmd = value;

    DPRINTF("%s: writing offset " TARGET_FMT_plx " value %08x width %d wcycle 0x%x\n",
            __func__, offset, value, width, pfl->wcycle);

    if (!pfl->wcycle) {
        /* Set the device in I/O access mode */
        memory_region_rom_device_set_readable(&pfl->mem, false);
    }

    switch (pfl->wcycle) {
    case 0:
        /* read mode */
        switch (cmd) {
        case 0x00: /* ??? */
            goto reset_flash;
        case 0x10: /* Single Byte Program */
        case 0x40: /* Single Byte Program */
            DPRINTF("%s: Single Byte Program\n", __func__);
            break;
        case 0x20: /* Block erase */
            p = pfl->storage;
            offset &= ~(pfl->sector_len - 1);

            DPRINTF("%s: block erase at " TARGET_FMT_plx " bytes %x\n",
                    __func__, offset, (unsigned)pfl->sector_len);

            if (!pfl->ro) {
                memset(p + offset, 0xff, pfl->sector_len);
                pflash_update(pfl, offset, pfl->sector_len);
            } else {
                pfl->status |= 0x20; /* Block erase error */
            }
            pfl->status |= 0x80; /* Ready! */
            break;
        case 0x50: /* Clear status bits */
            DPRINTF("%s: Clear status bits\n", __func__);
            pfl->status = 0x0;
            goto reset_flash;
        case 0x60: /* Block (un)lock */
            DPRINTF("%s: Block unlock\n", __func__);
            break;
        case 0x70: /* Status Register */
            DPRINTF("%s: Read status register\n", __func__);
            pfl->cmd = cmd;
            return;
        case 0x90: /* Read Device ID */
            DPRINTF("%s: Read Device information\n", __func__);
            pfl->cmd = cmd;
            return;
        case 0x98: /* CFI query */
            DPRINTF("%s: CFI query\n", __func__);
            break;
        case 0xe8: /* Write to buffer */
            DPRINTF("%s: Write to buffer\n", __func__);
            pfl->status |= 0x80; /* Ready! */
            break;
        case 0xff: /* Read array mode */
            DPRINTF("%s: Read array mode\n", __func__);
            goto reset_flash;
        default:
            goto error_flash;
        }
        pfl->wcycle++;
        pfl->cmd = cmd;
        break;
    case 1:
        switch (pfl->cmd) {
        case 0x10: /* Single Byte Program */
        case 0x40: /* Single Byte Program */
            DPRINTF("%s: Single Byte Program\n", __func__);
            if (!pfl->ro) {
                pflash_data_write(pfl, offset, value, width, be);
                pflash_update(pfl, offset, width);
            } else {
                pfl->status |= 0x10; /* Programming error */
            }
            pfl->status |= 0x80; /* Ready! */
            pfl->wcycle = 0;
        break;
        case 0x20: /* Block erase */
        case 0x28:
            if (cmd == 0xd0) { /* confirm */
                pfl->wcycle = 0;
                pfl->status |= 0x80;
            } else if (cmd == 0xff) { /* read array mode */
                goto reset_flash;
            } else
                goto error_flash;

            break;
        case 0xe8:
            DPRINTF("%s: block write of %x bytes\n", __func__, value);
            pfl->counter = value;
            pfl->wcycle++;
            break;
        case 0x60:
            if (cmd == 0xd0) {
                pfl->wcycle = 0;
                pfl->status |= 0x80;
            } else if (cmd == 0x01) {
                pfl->wcycle = 0;
                pfl->status |= 0x80;
            } else if (cmd == 0xff) {
                goto reset_flash;
            } else {
                DPRINTF("%s: Unknown (un)locking command\n", __func__);
                goto reset_flash;
            }
            break;
        case 0x98:
            if (cmd == 0xff) {
                goto reset_flash;
            } else {
                DPRINTF("%s: leaving query mode\n", __func__);
            }
            break;
        default:
            goto error_flash;
        }
        break;
    case 2:
        switch (pfl->cmd) {
        case 0xe8: /* Block write */
            if (!pfl->ro) {
                pflash_data_write(pfl, offset, value, width, be);
            } else {
                pfl->status |= 0x10; /* Programming error */
            }

            pfl->status |= 0x80;

            if (!pfl->counter) {
                hwaddr mask = pfl->writeblock_size - 1;
                mask = ~mask;

                DPRINTF("%s: block write finished\n", __func__);
                pfl->wcycle++;
                if (!pfl->ro) {
                    /* Flush the entire write buffer onto backing storage.  */
                    pflash_update(pfl, offset & mask, pfl->writeblock_size);
                } else {
                    pfl->status |= 0x10; /* Programming error */
                }
            }

            pfl->counter--;
            break;
        default:
            goto error_flash;
        }
        break;
    case 3: /* Confirm mode */
        switch (pfl->cmd) {
        case 0xe8: /* Block write */
            if (cmd == 0xd0) {
                pfl->wcycle = 0;
                pfl->status |= 0x80;
            } else {
                DPRINTF("%s: unknown command for \"write block\"\n", __func__);
                PFLASH_BUG("Write block confirm");
                goto reset_flash;
            }
            break;
        default:
            goto error_flash;
        }
        break;
    default:
        /* Should never happen */
        DPRINTF("%s: invalid write state\n",  __func__);
        goto reset_flash;
    }
    return;

 error_flash:
    printf("%s: Unimplemented flash cmd sequence "
           "(offset " TARGET_FMT_plx ", wcycle 0x%x cmd 0x%x value 0x%x)\n",
           __func__, offset, pfl->wcycle, pfl->cmd, value);

 reset_flash:
    memory_region_rom_device_set_readable(&pfl->mem, true);

    pfl->bypass = 0;
    pfl->wcycle = 0;
    pfl->cmd = 0;
}


static uint32_t pflash_readb_be(void *opaque, hwaddr addr)
{
    return pflash_read(opaque, addr, 1, 1);
}

static uint32_t pflash_readb_le(void *opaque, hwaddr addr)
{
    return pflash_read(opaque, addr, 1, 0);
}

static uint32_t pflash_readw_be(void *opaque, hwaddr addr)
{
    pflash_t *pfl = opaque;

    return pflash_read(pfl, addr, 2, 1);
}

static uint32_t pflash_readw_le(void *opaque, hwaddr addr)
{
    pflash_t *pfl = opaque;

    return pflash_read(pfl, addr, 2, 0);
}

static uint32_t pflash_readl_be(void *opaque, hwaddr addr)
{
    pflash_t *pfl = opaque;

    return pflash_read(pfl, addr, 4, 1);
}

static uint32_t pflash_readl_le(void *opaque, hwaddr addr)
{
    pflash_t *pfl = opaque;

    return pflash_read(pfl, addr, 4, 0);
}

static void pflash_writeb_be(void *opaque, hwaddr addr,
                             uint32_t value)
{
    pflash_write(opaque, addr, value, 1, 1);
}

static void pflash_writeb_le(void *opaque, hwaddr addr,
                             uint32_t value)
{
    pflash_write(opaque, addr, value, 1, 0);
}

static void pflash_writew_be(void *opaque, hwaddr addr,
                             uint32_t value)
{
    pflash_t *pfl = opaque;

    pflash_write(pfl, addr, value, 2, 1);
}

static void pflash_writew_le(void *opaque, hwaddr addr,
                             uint32_t value)
{
    pflash_t *pfl = opaque;

    pflash_write(pfl, addr, value, 2, 0);
}

static void pflash_writel_be(void *opaque, hwaddr addr,
                             uint32_t value)
{
    pflash_t *pfl = opaque;

    pflash_write(pfl, addr, value, 4, 1);
}

static void pflash_writel_le(void *opaque, hwaddr addr,
                             uint32_t value)
{
    pflash_t *pfl = opaque;

    pflash_write(pfl, addr, value, 4, 0);
}

static const MemoryRegionOps pflash_cfi01_ops_be = {
    .old_mmio = {
        .read = { pflash_readb_be, pflash_readw_be, pflash_readl_be, },
        .write = { pflash_writeb_be, pflash_writew_be, pflash_writel_be, },
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const MemoryRegionOps pflash_cfi01_ops_le = {
    .old_mmio = {
        .read = { pflash_readb_le, pflash_readw_le, pflash_readl_le, },
        .write = { pflash_writeb_le, pflash_writew_le, pflash_writel_le, },
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int pflash_cfi01_init(SysBusDevice *dev)
{
    pflash_t *pfl = FROM_SYSBUS(typeof(*pfl), dev);
    uint64_t total_len;
    int ret;

    total_len = pfl->sector_len * pfl->nb_blocs;

    /* XXX: to be fixed */
#if 0
    if (total_len != (8 * 1024 * 1024) && total_len != (16 * 1024 * 1024) &&
        total_len != (32 * 1024 * 1024) && total_len != (64 * 1024 * 1024))
        return NULL;
#endif

    memory_region_init_rom_device(
        &pfl->mem, pfl->be ? &pflash_cfi01_ops_be : &pflash_cfi01_ops_le, pfl,
        pfl->name, total_len);
    vmstate_register_ram(&pfl->mem, DEVICE(pfl));
    pfl->storage = memory_region_get_ram_ptr(&pfl->mem);
    sysbus_init_mmio(dev, &pfl->mem);

    if (pfl->bs) {
        /* read the initial flash content */
        ret = bdrv_read(pfl->bs, 0, pfl->storage, total_len >> 9);

        if (ret < 0) {
            vmstate_unregister_ram(&pfl->mem, DEVICE(pfl));
            memory_region_destroy(&pfl->mem);
            return 1;
        }
    }

    if (pfl->bs) {
        pfl->ro = bdrv_is_read_only(pfl->bs);
    } else {
        pfl->ro = 0;
    }

    pfl->timer = qemu_new_timer_ns(vm_clock, pflash_timer, pfl);
    pfl->wcycle = 0;
    pfl->cmd = 0;
    pfl->status = 0;
    /* Hardcoded CFI table */
    pfl->cfi_len = 0x52;
    /* Standard "QRY" string */
    pfl->cfi_table[0x10] = 'Q';
    pfl->cfi_table[0x11] = 'R';
    pfl->cfi_table[0x12] = 'Y';
    /* Command set (Intel) */
    pfl->cfi_table[0x13] = 0x01;
    pfl->cfi_table[0x14] = 0x00;
    /* Primary extended table address (none) */
    pfl->cfi_table[0x15] = 0x31;
    pfl->cfi_table[0x16] = 0x00;
    /* Alternate command set (none) */
    pfl->cfi_table[0x17] = 0x00;
    pfl->cfi_table[0x18] = 0x00;
    /* Alternate extended table (none) */
    pfl->cfi_table[0x19] = 0x00;
    pfl->cfi_table[0x1A] = 0x00;
    /* Vcc min */
    pfl->cfi_table[0x1B] = 0x45;
    /* Vcc max */
    pfl->cfi_table[0x1C] = 0x55;
    /* Vpp min (no Vpp pin) */
    pfl->cfi_table[0x1D] = 0x00;
    /* Vpp max (no Vpp pin) */
    pfl->cfi_table[0x1E] = 0x00;
    /* Reserved */
    pfl->cfi_table[0x1F] = 0x07;
    /* Timeout for min size buffer write */
    pfl->cfi_table[0x20] = 0x07;
    /* Typical timeout for block erase */
    pfl->cfi_table[0x21] = 0x0a;
    /* Typical timeout for full chip erase (4096 ms) */
    pfl->cfi_table[0x22] = 0x00;
    /* Reserved */
    pfl->cfi_table[0x23] = 0x04;
    /* Max timeout for buffer write */
    pfl->cfi_table[0x24] = 0x04;
    /* Max timeout for block erase */
    pfl->cfi_table[0x25] = 0x04;
    /* Max timeout for chip erase */
    pfl->cfi_table[0x26] = 0x00;
    /* Device size */
    pfl->cfi_table[0x27] = ctz32(total_len); // + 1;
    /* Flash device interface (8 & 16 bits) */
    pfl->cfi_table[0x28] = 0x02;
    pfl->cfi_table[0x29] = 0x00;
    /* Max number of bytes in multi-bytes write */
    if (pfl->width == 1) {
        pfl->cfi_table[0x2A] = 0x08;
    } else {
        pfl->cfi_table[0x2A] = 0x0B;
    }
    pfl->writeblock_size = 1 << pfl->cfi_table[0x2A];

    pfl->cfi_table[0x2B] = 0x00;
    /* Number of erase block regions (uniform) */
    pfl->cfi_table[0x2C] = 0x01;
    /* Erase block region 1 */
    pfl->cfi_table[0x2D] = pfl->nb_blocs - 1;
    pfl->cfi_table[0x2E] = (pfl->nb_blocs - 1) >> 8;
    pfl->cfi_table[0x2F] = pfl->sector_len >> 8;
    pfl->cfi_table[0x30] = pfl->sector_len >> 16;

    /* Extended */
    pfl->cfi_table[0x31] = 'P';
    pfl->cfi_table[0x32] = 'R';
    pfl->cfi_table[0x33] = 'I';

    pfl->cfi_table[0x34] = '1';
    pfl->cfi_table[0x35] = '0';

    pfl->cfi_table[0x36] = 0x00;
    pfl->cfi_table[0x37] = 0x00;
    pfl->cfi_table[0x38] = 0x00;
    pfl->cfi_table[0x39] = 0x00;

    pfl->cfi_table[0x3a] = 0x00;

    pfl->cfi_table[0x3b] = 0x00;
    pfl->cfi_table[0x3c] = 0x00;

    pfl->cfi_table[0x3f] = 0x01; /* Number of protection fields */

    return 0;
}

static Property pflash_cfi01_properties[] = {
    DEFINE_PROP_DRIVE("drive", struct pflash_t, bs),
    DEFINE_PROP_UINT32("num-blocks", struct pflash_t, nb_blocs, 0),
    DEFINE_PROP_UINT64("sector-length", struct pflash_t, sector_len, 0),
    DEFINE_PROP_UINT8("width", struct pflash_t, width, 0),
    DEFINE_PROP_UINT8("big-endian", struct pflash_t, be, 0),
    DEFINE_PROP_UINT16("id0", struct pflash_t, ident0, 0),
    DEFINE_PROP_UINT16("id1", struct pflash_t, ident1, 0),
    DEFINE_PROP_UINT16("id2", struct pflash_t, ident2, 0),
    DEFINE_PROP_UINT16("id3", struct pflash_t, ident3, 0),
    DEFINE_PROP_STRING("name", struct pflash_t, name),
    DEFINE_PROP_END_OF_LIST(),
};

static void pflash_cfi01_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = pflash_cfi01_init;
    dc->props = pflash_cfi01_properties;
}


static const TypeInfo pflash_cfi01_info = {
    .name           = "cfi.pflash01",
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(struct pflash_t),
    .class_init     = pflash_cfi01_class_init,
};

static void pflash_cfi01_register_types(void)
{
    type_register_static(&pflash_cfi01_info);
}

type_init(pflash_cfi01_register_types)

pflash_t *pflash_cfi01_register(hwaddr base,
                                DeviceState *qdev, const char *name,
                                hwaddr size,
                                BlockDriverState *bs,
                                uint32_t sector_len, int nb_blocs, int width,
                                uint16_t id0, uint16_t id1,
                                uint16_t id2, uint16_t id3, int be)
{
    DeviceState *dev = qdev_create(NULL, "cfi.pflash01");
    SysBusDevice *busdev = sysbus_from_qdev(dev);
    pflash_t *pfl = (pflash_t *)object_dynamic_cast(OBJECT(dev),
                                                    "cfi.pflash01");

    if (bs && qdev_prop_set_drive(dev, "drive", bs)) {
        abort();
    }
    qdev_prop_set_uint32(dev, "num-blocks", nb_blocs);
    qdev_prop_set_uint64(dev, "sector-length", sector_len);
    qdev_prop_set_uint8(dev, "width", width);
    qdev_prop_set_uint8(dev, "big-endian", !!be);
    qdev_prop_set_uint16(dev, "id0", id0);
    qdev_prop_set_uint16(dev, "id1", id1);
    qdev_prop_set_uint16(dev, "id2", id2);
    qdev_prop_set_uint16(dev, "id3", id3);
    qdev_prop_set_string(dev, "name", name);
    qdev_init_nofail(dev);

    sysbus_mmio_map(busdev, 0, base);
    return pfl;
}

MemoryRegion *pflash_cfi01_get_memory(pflash_t *fl)
{
    return &fl->mem;
}

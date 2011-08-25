/*
 *  CFI parallel flash with AMD command set emulation
 *
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
 * - chip erase
 * - unlock bypass command
 * - CFI queries
 *
 * It does not support flash interleaving.
 * It does not implement boot blocs with reduced size
 * It does not implement software data protection as found in many real chips
 * It does not implement erase suspend/resume commands
 * It does not implement multiple sectors erase
 */

#include "hw.h"
#include "flash.h"
#include "qemu-timer.h"
#include "block.h"

//#define PFLASH_DEBUG
#ifdef PFLASH_DEBUG
#define DPRINTF(fmt, ...)                          \
do {                                               \
    printf("PFLASH: " fmt , ## __VA_ARGS__);       \
} while (0)
#else
#define DPRINTF(fmt, ...) do { } while (0)
#endif

#define PFLASH_LAZY_ROMD_THRESHOLD 42

struct pflash_t {
    BlockDriverState *bs;
    target_phys_addr_t base;
    uint32_t sector_len;
    uint32_t chip_len;
    int mappings;
    int width;
    int wcycle; /* if 0, the flash is read normally */
    int bypass;
    int ro;
    uint8_t cmd;
    uint8_t status;
    uint16_t ident[4];
    uint16_t unlock_addr[2];
    uint8_t cfi_len;
    uint8_t cfi_table[0x52];
    QEMUTimer *timer;
    ram_addr_t off;
    int fl_mem;
    int rom_mode;
    int read_counter; /* used for lazy switch-back to rom mode */
    void *storage;
};

static void pflash_register_memory(pflash_t *pfl, int rom_mode)
{
    unsigned long phys_offset = pfl->fl_mem;
    int i;

    if (rom_mode)
        phys_offset |= pfl->off | IO_MEM_ROMD;
    pfl->rom_mode = rom_mode;

    for (i = 0; i < pfl->mappings; i++)
        cpu_register_physical_memory(pfl->base + i * pfl->chip_len,
                                     pfl->chip_len, phys_offset);
}

static void pflash_timer (void *opaque)
{
    pflash_t *pfl = opaque;

    DPRINTF("%s: command %02x done\n", __func__, pfl->cmd);
    /* Reset flash */
    pfl->status ^= 0x80;
    if (pfl->bypass) {
        pfl->wcycle = 2;
    } else {
        pflash_register_memory(pfl, 1);
        pfl->wcycle = 0;
    }
    pfl->cmd = 0;
}

static uint32_t pflash_read (pflash_t *pfl, target_phys_addr_t offset,
                             int width, int be)
{
    target_phys_addr_t boff;
    uint32_t ret;
    uint8_t *p;

    DPRINTF("%s: offset " TARGET_FMT_plx "\n", __func__, offset);
    ret = -1;
    /* Lazy reset to ROMD mode after a certain amount of read accesses */
    if (!pfl->rom_mode && pfl->wcycle == 0 &&
        ++pfl->read_counter > PFLASH_LAZY_ROMD_THRESHOLD) {
        pflash_register_memory(pfl, 1);
    }
    offset &= pfl->chip_len - 1;
    boff = offset & 0xFF;
    if (pfl->width == 2)
        boff = boff >> 1;
    else if (pfl->width == 4)
        boff = boff >> 2;
    switch (pfl->cmd) {
    default:
        /* This should never happen : reset state & treat it as a read*/
        DPRINTF("%s: unknown command state: %x\n", __func__, pfl->cmd);
        pfl->wcycle = 0;
        pfl->cmd = 0;
    case 0x80:
        /* We accept reads during second unlock sequence... */
    case 0x00:
    flash_read:
        /* Flash area read */
        p = pfl->storage;
        switch (width) {
        case 1:
            ret = p[offset];
//            DPRINTF("%s: data offset %08x %02x\n", __func__, offset, ret);
            break;
        case 2:
            if (be) {
                ret = p[offset] << 8;
                ret |= p[offset + 1];
            } else {
                ret = p[offset];
                ret |= p[offset + 1] << 8;
            }
//            DPRINTF("%s: data offset %08x %04x\n", __func__, offset, ret);
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
//            DPRINTF("%s: data offset %08x %08x\n", __func__, offset, ret);
            break;
        }
        break;
    case 0x90:
        /* flash ID read */
        switch (boff) {
        case 0x00:
        case 0x01:
            ret = pfl->ident[boff & 0x01];
            break;
        case 0x02:
            ret = 0x00; /* Pretend all sectors are unprotected */
            break;
        case 0x0E:
        case 0x0F:
            if (pfl->ident[2 + (boff & 0x01)] == (uint8_t)-1)
                goto flash_read;
            ret = pfl->ident[2 + (boff & 0x01)];
            break;
        default:
            goto flash_read;
        }
        DPRINTF("%s: ID " TARGET_FMT_plx " %x\n", __func__, boff, ret);
        break;
    case 0xA0:
    case 0x10:
    case 0x30:
        /* Status register read */
        ret = pfl->status;
        DPRINTF("%s: status %x\n", __func__, ret);
        /* Toggle bit 6 */
        pfl->status ^= 0x40;
        break;
    case 0x98:
        /* CFI query mode */
        if (boff > pfl->cfi_len)
            ret = 0;
        else
            ret = pfl->cfi_table[boff];
        break;
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

static void pflash_write (pflash_t *pfl, target_phys_addr_t offset,
                          uint32_t value, int width, int be)
{
    target_phys_addr_t boff;
    uint8_t *p;
    uint8_t cmd;

    cmd = value;
    if (pfl->cmd != 0xA0 && cmd == 0xF0) {
#if 0
        DPRINTF("%s: flash reset asked (%02x %02x)\n",
                __func__, pfl->cmd, cmd);
#endif
        goto reset_flash;
    }
    DPRINTF("%s: offset " TARGET_FMT_plx " %08x %d %d\n", __func__,
            offset, value, width, pfl->wcycle);
    offset &= pfl->chip_len - 1;

    DPRINTF("%s: offset " TARGET_FMT_plx " %08x %d\n", __func__,
            offset, value, width);
    boff = offset & (pfl->sector_len - 1);
    if (pfl->width == 2)
        boff = boff >> 1;
    else if (pfl->width == 4)
        boff = boff >> 2;
    switch (pfl->wcycle) {
    case 0:
        /* Set the device in I/O access mode if required */
        if (pfl->rom_mode)
            pflash_register_memory(pfl, 0);
        pfl->read_counter = 0;
        /* We're in read mode */
    check_unlock0:
        if (boff == 0x55 && cmd == 0x98) {
        enter_CFI_mode:
            /* Enter CFI query mode */
            pfl->wcycle = 7;
            pfl->cmd = 0x98;
            return;
        }
        if (boff != pfl->unlock_addr[0] || cmd != 0xAA) {
            DPRINTF("%s: unlock0 failed " TARGET_FMT_plx " %02x %04x\n",
                    __func__, boff, cmd, pfl->unlock_addr[0]);
            goto reset_flash;
        }
        DPRINTF("%s: unlock sequence started\n", __func__);
        break;
    case 1:
        /* We started an unlock sequence */
    check_unlock1:
        if (boff != pfl->unlock_addr[1] || cmd != 0x55) {
            DPRINTF("%s: unlock1 failed " TARGET_FMT_plx " %02x\n", __func__,
                    boff, cmd);
            goto reset_flash;
        }
        DPRINTF("%s: unlock sequence done\n", __func__);
        break;
    case 2:
        /* We finished an unlock sequence */
        if (!pfl->bypass && boff != pfl->unlock_addr[0]) {
            DPRINTF("%s: command failed " TARGET_FMT_plx " %02x\n", __func__,
                    boff, cmd);
            goto reset_flash;
        }
        switch (cmd) {
        case 0x20:
            pfl->bypass = 1;
            goto do_bypass;
        case 0x80:
        case 0x90:
        case 0xA0:
            pfl->cmd = cmd;
            DPRINTF("%s: starting command %02x\n", __func__, cmd);
            break;
        default:
            DPRINTF("%s: unknown command %02x\n", __func__, cmd);
            goto reset_flash;
        }
        break;
    case 3:
        switch (pfl->cmd) {
        case 0x80:
            /* We need another unlock sequence */
            goto check_unlock0;
        case 0xA0:
            DPRINTF("%s: write data offset " TARGET_FMT_plx " %08x %d\n",
                    __func__, offset, value, width);
            p = pfl->storage;
            switch (width) {
            case 1:
                p[offset] &= value;
                pflash_update(pfl, offset, 1);
                break;
            case 2:
                if (be) {
                    p[offset] &= value >> 8;
                    p[offset + 1] &= value;
                } else {
                    p[offset] &= value;
                    p[offset + 1] &= value >> 8;
                }
                pflash_update(pfl, offset, 2);
                break;
            case 4:
                if (be) {
                    p[offset] &= value >> 24;
                    p[offset + 1] &= value >> 16;
                    p[offset + 2] &= value >> 8;
                    p[offset + 3] &= value;
                } else {
                    p[offset] &= value;
                    p[offset + 1] &= value >> 8;
                    p[offset + 2] &= value >> 16;
                    p[offset + 3] &= value >> 24;
                }
                pflash_update(pfl, offset, 4);
                break;
            }
            pfl->status = 0x00 | ~(value & 0x80);
            /* Let's pretend write is immediate */
            if (pfl->bypass)
                goto do_bypass;
            goto reset_flash;
        case 0x90:
            if (pfl->bypass && cmd == 0x00) {
                /* Unlock bypass reset */
                goto reset_flash;
            }
            /* We can enter CFI query mode from autoselect mode */
            if (boff == 0x55 && cmd == 0x98)
                goto enter_CFI_mode;
            /* No break here */
        default:
            DPRINTF("%s: invalid write for command %02x\n",
                    __func__, pfl->cmd);
            goto reset_flash;
        }
    case 4:
        switch (pfl->cmd) {
        case 0xA0:
            /* Ignore writes while flash data write is occurring */
            /* As we suppose write is immediate, this should never happen */
            return;
        case 0x80:
            goto check_unlock1;
        default:
            /* Should never happen */
            DPRINTF("%s: invalid command state %02x (wc 4)\n",
                    __func__, pfl->cmd);
            goto reset_flash;
        }
        break;
    case 5:
        switch (cmd) {
        case 0x10:
            if (boff != pfl->unlock_addr[0]) {
                DPRINTF("%s: chip erase: invalid address " TARGET_FMT_plx "\n",
                        __func__, offset);
                goto reset_flash;
            }
            /* Chip erase */
            DPRINTF("%s: start chip erase\n", __func__);
            memset(pfl->storage, 0xFF, pfl->chip_len);
            pfl->status = 0x00;
            pflash_update(pfl, 0, pfl->chip_len);
            /* Let's wait 5 seconds before chip erase is done */
            qemu_mod_timer(pfl->timer,
                           qemu_get_clock_ns(vm_clock) + (get_ticks_per_sec() * 5));
            break;
        case 0x30:
            /* Sector erase */
            p = pfl->storage;
            offset &= ~(pfl->sector_len - 1);
            DPRINTF("%s: start sector erase at " TARGET_FMT_plx "\n", __func__,
                    offset);
            memset(p + offset, 0xFF, pfl->sector_len);
            pflash_update(pfl, offset, pfl->sector_len);
            pfl->status = 0x00;
            /* Let's wait 1/2 second before sector erase is done */
            qemu_mod_timer(pfl->timer,
                           qemu_get_clock_ns(vm_clock) + (get_ticks_per_sec() / 2));
            break;
        default:
            DPRINTF("%s: invalid command %02x (wc 5)\n", __func__, cmd);
            goto reset_flash;
        }
        pfl->cmd = cmd;
        break;
    case 6:
        switch (pfl->cmd) {
        case 0x10:
            /* Ignore writes during chip erase */
            return;
        case 0x30:
            /* Ignore writes during sector erase */
            return;
        default:
            /* Should never happen */
            DPRINTF("%s: invalid command state %02x (wc 6)\n",
                    __func__, pfl->cmd);
            goto reset_flash;
        }
        break;
    case 7: /* Special value for CFI queries */
        DPRINTF("%s: invalid write in CFI query mode\n", __func__);
        goto reset_flash;
    default:
        /* Should never happen */
        DPRINTF("%s: invalid write state (wc 7)\n",  __func__);
        goto reset_flash;
    }
    pfl->wcycle++;

    return;

    /* Reset flash */
 reset_flash:
    pfl->bypass = 0;
    pfl->wcycle = 0;
    pfl->cmd = 0;
    return;

 do_bypass:
    pfl->wcycle = 2;
    pfl->cmd = 0;
    return;
}


static uint32_t pflash_readb_be(void *opaque, target_phys_addr_t addr)
{
    return pflash_read(opaque, addr, 1, 1);
}

static uint32_t pflash_readb_le(void *opaque, target_phys_addr_t addr)
{
    return pflash_read(opaque, addr, 1, 0);
}

static uint32_t pflash_readw_be(void *opaque, target_phys_addr_t addr)
{
    pflash_t *pfl = opaque;

    return pflash_read(pfl, addr, 2, 1);
}

static uint32_t pflash_readw_le(void *opaque, target_phys_addr_t addr)
{
    pflash_t *pfl = opaque;

    return pflash_read(pfl, addr, 2, 0);
}

static uint32_t pflash_readl_be(void *opaque, target_phys_addr_t addr)
{
    pflash_t *pfl = opaque;

    return pflash_read(pfl, addr, 4, 1);
}

static uint32_t pflash_readl_le(void *opaque, target_phys_addr_t addr)
{
    pflash_t *pfl = opaque;

    return pflash_read(pfl, addr, 4, 0);
}

static void pflash_writeb_be(void *opaque, target_phys_addr_t addr,
                             uint32_t value)
{
    pflash_write(opaque, addr, value, 1, 1);
}

static void pflash_writeb_le(void *opaque, target_phys_addr_t addr,
                             uint32_t value)
{
    pflash_write(opaque, addr, value, 1, 0);
}

static void pflash_writew_be(void *opaque, target_phys_addr_t addr,
                             uint32_t value)
{
    pflash_t *pfl = opaque;

    pflash_write(pfl, addr, value, 2, 1);
}

static void pflash_writew_le(void *opaque, target_phys_addr_t addr,
                             uint32_t value)
{
    pflash_t *pfl = opaque;

    pflash_write(pfl, addr, value, 2, 0);
}

static void pflash_writel_be(void *opaque, target_phys_addr_t addr,
                             uint32_t value)
{
    pflash_t *pfl = opaque;

    pflash_write(pfl, addr, value, 4, 1);
}

static void pflash_writel_le(void *opaque, target_phys_addr_t addr,
                             uint32_t value)
{
    pflash_t *pfl = opaque;

    pflash_write(pfl, addr, value, 4, 0);
}

static CPUWriteMemoryFunc * const pflash_write_ops_be[] = {
    &pflash_writeb_be,
    &pflash_writew_be,
    &pflash_writel_be,
};

static CPUReadMemoryFunc * const pflash_read_ops_be[] = {
    &pflash_readb_be,
    &pflash_readw_be,
    &pflash_readl_be,
};

static CPUWriteMemoryFunc * const pflash_write_ops_le[] = {
    &pflash_writeb_le,
    &pflash_writew_le,
    &pflash_writel_le,
};

static CPUReadMemoryFunc * const pflash_read_ops_le[] = {
    &pflash_readb_le,
    &pflash_readw_le,
    &pflash_readl_le,
};

/* Count trailing zeroes of a 32 bits quantity */
static int ctz32 (uint32_t n)
{
    int ret;

    ret = 0;
    if (!(n & 0xFFFF)) {
        ret += 16;
        n = n >> 16;
    }
    if (!(n & 0xFF)) {
        ret += 8;
        n = n >> 8;
    }
    if (!(n & 0xF)) {
        ret += 4;
        n = n >> 4;
    }
    if (!(n & 0x3)) {
        ret += 2;
        n = n >> 2;
    }
    if (!(n & 0x1)) {
        ret++;
#if 0 /* This is not necessary as n is never 0 */
        n = n >> 1;
#endif
    }
#if 0 /* This is not necessary as n is never 0 */
    if (!n)
        ret++;
#endif

    return ret;
}

pflash_t *pflash_cfi02_register(target_phys_addr_t base, ram_addr_t off,
                                BlockDriverState *bs, uint32_t sector_len,
                                int nb_blocs, int nb_mappings, int width,
                                uint16_t id0, uint16_t id1,
                                uint16_t id2, uint16_t id3,
                                uint16_t unlock_addr0, uint16_t unlock_addr1,
                                int be)
{
    pflash_t *pfl;
    int32_t chip_len;
    int ret;

    chip_len = sector_len * nb_blocs;
    /* XXX: to be fixed */
#if 0
    if (total_len != (8 * 1024 * 1024) && total_len != (16 * 1024 * 1024) &&
        total_len != (32 * 1024 * 1024) && total_len != (64 * 1024 * 1024))
        return NULL;
#endif
    pfl = g_malloc0(sizeof(pflash_t));
    /* FIXME: Allocate ram ourselves.  */
    pfl->storage = qemu_get_ram_ptr(off);
    if (be) {
        pfl->fl_mem = cpu_register_io_memory(pflash_read_ops_be,
                                             pflash_write_ops_be,
                                             pfl, DEVICE_NATIVE_ENDIAN);
    } else {
        pfl->fl_mem = cpu_register_io_memory(pflash_read_ops_le,
                                             pflash_write_ops_le,
                                             pfl, DEVICE_NATIVE_ENDIAN);
    }
    pfl->off = off;
    pfl->base = base;
    pfl->chip_len = chip_len;
    pfl->mappings = nb_mappings;
    pflash_register_memory(pfl, 1);
    pfl->bs = bs;
    if (pfl->bs) {
        /* read the initial flash content */
        ret = bdrv_read(pfl->bs, 0, pfl->storage, chip_len >> 9);
        if (ret < 0) {
            cpu_unregister_io_memory(pfl->fl_mem);
            g_free(pfl);
            return NULL;
        }
    }
#if 0 /* XXX: there should be a bit to set up read-only,
       *      the same way the hardware does (with WP pin).
       */
    pfl->ro = 1;
#else
    pfl->ro = 0;
#endif
    pfl->timer = qemu_new_timer_ns(vm_clock, pflash_timer, pfl);
    pfl->sector_len = sector_len;
    pfl->width = width;
    pfl->wcycle = 0;
    pfl->cmd = 0;
    pfl->status = 0;
    pfl->ident[0] = id0;
    pfl->ident[1] = id1;
    pfl->ident[2] = id2;
    pfl->ident[3] = id3;
    pfl->unlock_addr[0] = unlock_addr0;
    pfl->unlock_addr[1] = unlock_addr1;
    /* Hardcoded CFI table (mostly from SG29 Spansion flash) */
    pfl->cfi_len = 0x52;
    /* Standard "QRY" string */
    pfl->cfi_table[0x10] = 'Q';
    pfl->cfi_table[0x11] = 'R';
    pfl->cfi_table[0x12] = 'Y';
    /* Command set (AMD/Fujitsu) */
    pfl->cfi_table[0x13] = 0x02;
    pfl->cfi_table[0x14] = 0x00;
    /* Primary extended table address */
    pfl->cfi_table[0x15] = 0x31;
    pfl->cfi_table[0x16] = 0x00;
    /* Alternate command set (none) */
    pfl->cfi_table[0x17] = 0x00;
    pfl->cfi_table[0x18] = 0x00;
    /* Alternate extended table (none) */
    pfl->cfi_table[0x19] = 0x00;
    pfl->cfi_table[0x1A] = 0x00;
    /* Vcc min */
    pfl->cfi_table[0x1B] = 0x27;
    /* Vcc max */
    pfl->cfi_table[0x1C] = 0x36;
    /* Vpp min (no Vpp pin) */
    pfl->cfi_table[0x1D] = 0x00;
    /* Vpp max (no Vpp pin) */
    pfl->cfi_table[0x1E] = 0x00;
    /* Reserved */
    pfl->cfi_table[0x1F] = 0x07;
    /* Timeout for min size buffer write (NA) */
    pfl->cfi_table[0x20] = 0x00;
    /* Typical timeout for block erase (512 ms) */
    pfl->cfi_table[0x21] = 0x09;
    /* Typical timeout for full chip erase (4096 ms) */
    pfl->cfi_table[0x22] = 0x0C;
    /* Reserved */
    pfl->cfi_table[0x23] = 0x01;
    /* Max timeout for buffer write (NA) */
    pfl->cfi_table[0x24] = 0x00;
    /* Max timeout for block erase */
    pfl->cfi_table[0x25] = 0x0A;
    /* Max timeout for chip erase */
    pfl->cfi_table[0x26] = 0x0D;
    /* Device size */
    pfl->cfi_table[0x27] = ctz32(chip_len);
    /* Flash device interface (8 & 16 bits) */
    pfl->cfi_table[0x28] = 0x02;
    pfl->cfi_table[0x29] = 0x00;
    /* Max number of bytes in multi-bytes write */
    /* XXX: disable buffered write as it's not supported */
    //    pfl->cfi_table[0x2A] = 0x05;
    pfl->cfi_table[0x2A] = 0x00;
    pfl->cfi_table[0x2B] = 0x00;
    /* Number of erase block regions (uniform) */
    pfl->cfi_table[0x2C] = 0x01;
    /* Erase block region 1 */
    pfl->cfi_table[0x2D] = nb_blocs - 1;
    pfl->cfi_table[0x2E] = (nb_blocs - 1) >> 8;
    pfl->cfi_table[0x2F] = sector_len >> 8;
    pfl->cfi_table[0x30] = sector_len >> 16;

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

    return pfl;
}

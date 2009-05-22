/*
 *  CFI parallel flash with AMD / Fujitsu command set emulation
 *
 *  Copyright (c) 2005 Jocelyn Mayer
 *  Copyright (c) 2006 Stefan Weil (ES29LV160DB emulation)
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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

#include <assert.h>     /* assert */
#include <stdio.h>      /* fprintf */

#include "hw.h"
#include "block.h"
#include "flash.h"
#include "pflash.h"     /* pflash_amd_register */
#include "qemu-timer.h"
#include "exec-all.h"

#ifdef PFLASH_DEBUG
static int traceflag;
#define loglevel 1
#define logfile  stderr
#define DPRINTF(fmt, ...) \
    ((loglevel) ? fprintf(logfile, "PFLASH\t%-24s" fmt , __func__, ##__VA_ARGS__) : (void)0)
#else
#define DPRINTF(fmt, ...) do { } while (0)
#endif

typedef enum {
  unknown_mode,
  io_mode,
  rom_mode
} flash_mode_t;

struct pflash_t {
    BlockDriverState *bs;
    target_phys_addr_t base;
    uint32_t sector_len;
    uint32_t total_len;
    int width;
    int wcycle; /* if 0, the flash is read normally */
    flash_mode_t mode;
    int bypass;
    int ro;
    uint8_t cmd;
    uint8_t status;
    uint16_t ident[4];
    uint8_t cfi_table[0x52];
    QEMUTimer *timer;
    ram_addr_t off;
    int fl_mem;
    void *storage;
};

static void pflash_io_mode(pflash_t *pfl)
{
  if (pfl->mode != io_mode) {
    DPRINTF("switch to i/o mode\n");
    cpu_register_physical_memory(pfl->base, pfl->total_len, pfl->fl_mem);
    //~ DPRINTF("0x%08x 0x%08x 0x%04x\n", pfl->base, pfl->total_len, pfl->fl_mem);
    pfl->mode = io_mode;
  }
}

static void pflash_rom_mode(pflash_t *pfl)
{
  if (pfl->mode != rom_mode) {
    DPRINTF("switch to rom mode\n");
    cpu_register_physical_memory(pfl->base, pfl->total_len,
                                 pfl->off | IO_MEM_ROMD | pfl->fl_mem);
    pfl->mode = rom_mode;
  }
}

static void pflash_timer (void *opaque)
{
    pflash_t *pfl = opaque;

    DPRINTF("command %02x done\n", pfl->cmd);
    /* Reset flash */
    pfl->status ^= 0x80;
    if (pfl->bypass) {
        pfl->wcycle = 2;
    } else {
        pflash_rom_mode(pfl);
        pfl->wcycle = 0;
    }
    //~ check!!!
    pfl->cmd = 0;
}

static uint32_t pflash_read (pflash_t *pfl, uint32_t offset, int width)
{
    uint32_t boff;
    uint32_t ret;
    uint8_t *p;

    ret = -1;
    offset -= pfl->base;
    DPRINTF("offset %08x\n", offset);
    boff = offset & 0xFF;
    if (pfl->width == 2)
        boff = boff >> 1;
    else if (pfl->width == 4)
        boff = boff >> 2;
    switch (pfl->cmd) {
    default:
        /* This should never happen : reset state & treat it as a read*/
        DPRINTF("unknown command state: %x\n", pfl->cmd);
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
//            DPRINTF("data offset %08x %02x\n", offset, ret);
            break;
        case 2:
#if defined(TARGET_WORDS_BIGENDIAN)
            ret = p[offset] << 8;
            ret |= p[offset + 1];
#else
            ret = p[offset];
            ret |= p[offset + 1] << 8;
#endif
//            DPRINTF("data offset %08x %04x\n", offset, ret);
            break;
        case 4:
#if defined(TARGET_WORDS_BIGENDIAN)
            ret = p[offset] << 24;
            ret |= p[offset + 1] << 16;
            ret |= p[offset + 2] << 8;
            ret |= p[offset + 3];
#else
            ret = p[offset];
            ret |= p[offset + 1] << 8;
            ret |= p[offset + 2] << 16;
            ret |= p[offset + 3] << 24;
#endif
//            DPRINTF("data offset %08x %08x\n", offset, ret);
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
        DPRINTF("ID %d %x\n", boff, ret);
        break;
    case 0xA0:
    case 0x10:
    case 0x30:
        /* Status register read */
        ret = pfl->status;
        DPRINTF("status %x\n", ret);
        /* Toggle bit 6 */
        pfl->status ^= 0x40;
        break;
    case 0x98:
        /* CFI query mode */
        if (boff < sizeof(pfl->cfi_table)) {
            ret = pfl->cfi_table[boff];
        } else {
            ret = 0;
        }
        break;
    }

    DPRINTF("offset %08x %08x %d\n", offset, ret, width);
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

static void pflash_write (pflash_t *pfl, uint32_t offset, uint32_t value,
                          int width)
{
    uint32_t boff;
    uint8_t *p;
    uint8_t cmd = value;
    uint32_t sector_len = pfl->sector_len;

    /* WARNING: when the memory area is in ROMD mode, the offset is a
       ram offset, not a physical address */
    if (pfl->mode == rom_mode) {
        // TODO: next line raises a compiler warning, needs fix.
        //~ offset -= (uint32_t)pfl->storage;
        assert(0);
    } else {
        offset -= pfl->base;
    }

    DPRINTF("offset %08x %08x %d\n", offset, value, width);
    if (pfl->cmd != 0xA0 && cmd == 0xf0) {
        DPRINTF("flash reset asked (%02x %02x)\n", pfl->cmd, cmd);
        goto reset_flash;
    }
    if (pfl->cmd != 0xA0 && cmd == 0xff) {
        /* Intel command (read array mode). */
        DPRINTF("read array asked (%02x %02x)\n", pfl->cmd, cmd);
        goto reset_flash;
    }
    //~ !!! next code only for certain flash chips
    if (offset < 0x004000) {
        sector_len = 0x4000;
    } else if (offset < 0x008000) {
        sector_len = 0x2000;
    } else if (offset < 0x010000) {
        sector_len = 0x8000;
    }
    boff = offset & (sector_len - 1);
    if (pfl->width == 2)
        boff = boff >> 1;
    else if (pfl->width == 4)
        boff = boff >> 2;
    switch (pfl->wcycle) {
    case 0:
        /* We're in read mode */
        if (boff == 0x55 && cmd == 0x98) {
        enter_CFI_mode:
            /* Enter CFI query mode */
            pfl->wcycle = 7;
            pfl->cmd = 0x98;
            /* Set the device in I/O access mode */
            pflash_io_mode(pfl);
            return;
        }
    check_unlock0:
        if ((boff != 0x555 && offset != 0xaaaa) || cmd != 0xAA) {
            DPRINTF("unlock0 failed %04x %02x %04x\n", boff, cmd, 0x555);
            goto reset_flash;
        }
        DPRINTF("unlock sequence started\n");
        /* Set the device in I/O access mode */
        pflash_io_mode(pfl);
        break;
    case 1:
        /* We started an unlock sequence */
    check_unlock1:
        if ((boff != 0x2AA && offset != 0x5554) || cmd != 0x55) {
            DPRINTF("unlock1 failed %04x %02x\n", boff, cmd);
            goto reset_flash;
        }
        DPRINTF("unlock sequence done\n");
        break;
    case 2:
        /* We finished an unlock sequence */
        if (!pfl->bypass && boff != 0x555 && offset != 0xaaaa) {
            DPRINTF("command failed %04x %02x\n", boff, cmd);
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
            DPRINTF("starting command %02x\n", cmd);
            break;
        default:
            DPRINTF("unknown command %02x\n", cmd);
            goto reset_flash;
        }
        break;
    case 3:
        switch (pfl->cmd) {
        case 0x80:
            /* We need another unlock sequence */
            goto check_unlock0;
        case 0xA0:
            DPRINTF("write data offset %08x %08x %d\n", offset, value, width);
            p = pfl->storage;
            switch (width) {
            case 1:
                p[offset] &= value;
                pflash_update(pfl, offset, 1);
                break;
            case 2:
#if defined(TARGET_WORDS_BIGENDIAN)
                p[offset] &= value >> 8;
                p[offset + 1] &= value;
#else
                p[offset] &= value;
                p[offset + 1] &= value >> 8;
#endif
                pflash_update(pfl, offset, 2);
                break;
            case 4:
#if defined(TARGET_WORDS_BIGENDIAN)
                p[offset] &= value >> 24;
                p[offset + 1] &= value >> 16;
                p[offset + 2] &= value >> 8;
                p[offset + 3] &= value;
#else
                p[offset] &= value;
                p[offset + 1] &= value >> 8;
                p[offset + 2] &= value >> 16;
                p[offset + 3] &= value >> 24;
#endif
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
            DPRINTF("invalid write for command %02x\n", pfl->cmd);
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
            DPRINTF("invalid command state %02x (wc 4)\n", pfl->cmd);
            goto reset_flash;
        }
        break;
    case 5:
        switch (cmd) {
        case 0x10:
            if (boff != 0x555) {
                DPRINTF("chip erase: invalid address %04x\n", offset);
                goto reset_flash;
            }
            /* Chip erase */
            DPRINTF("start chip erase\n");
            memset(pfl->storage, 0xFF, pfl->total_len);
            pfl->status = 0x00;
            pflash_update(pfl, 0, pfl->total_len);
            /* Let's wait 5 seconds before chip erase is done */
            qemu_mod_timer(pfl->timer,
                           qemu_get_clock(vm_clock) + (ticks_per_sec * 5));
            break;
        case 0x30:
            /* Sector erase */
            p = pfl->storage;
            offset &= ~(sector_len - 1);
            DPRINTF("start sector erase at %08x\n", offset);
            memset(p + offset, 0xFF, sector_len);
            pflash_update(pfl, offset, sector_len);
            pfl->status = 0x00;
            /* Let's wait 1/2 second before sector erase is done */
            qemu_mod_timer(pfl->timer,
                           qemu_get_clock(vm_clock) + (ticks_per_sec / 2));
            break;
        default:
            DPRINTF("invalid command %02x (wc 5)\n", cmd);
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
            DPRINTF("invalid command state %02x (wc 6)\n", pfl->cmd);
            goto reset_flash;
        }
        break;
    case 7: /* Special value for CFI queries */
        DPRINTF("invalid write in CFI query mode\n");
        goto reset_flash;
    default:
        /* Should never happen */
        DPRINTF("invalid write state (wc 7)\n");
        goto reset_flash;
    }
    pfl->wcycle++;

    return;

    /* Reset flash */
 reset_flash:
    pflash_rom_mode(pfl);
    pfl->bypass = 0;
    pfl->wcycle = 0;
    pfl->cmd = 0;
    return;

 do_bypass:
    pfl->wcycle = 2;
    pfl->cmd = 0;
    return;
}


static uint32_t pflash_readb (void *opaque, target_phys_addr_t addr)
{
    return pflash_read(opaque, addr, 1);
}

static uint32_t pflash_readw (void *opaque, target_phys_addr_t addr)
{
    pflash_t *pfl = opaque;

    return pflash_read(pfl, addr, 2);
}

static uint32_t pflash_readl (void *opaque, target_phys_addr_t addr)
{
    pflash_t *pfl = opaque;

    return pflash_read(pfl, addr, 4);
}

static void pflash_writeb (void *opaque, target_phys_addr_t addr,
                           uint32_t value)
{
    pflash_write(opaque, addr, value, 1);
}

static void pflash_writew (void *opaque, target_phys_addr_t addr,
                           uint32_t value)
{
    pflash_t *pfl = opaque;

    pflash_write(pfl, addr, value, 2);
}

static void pflash_writel (void *opaque, target_phys_addr_t addr,
                           uint32_t value)
{
    pflash_t *pfl = opaque;

    pflash_write(pfl, addr, value, 4);
}

static CPUWriteMemoryFunc * const pflash_write_ops[] = {
    pflash_writeb,
    pflash_writew,
    pflash_writel,
};

static CPUReadMemoryFunc * const pflash_read_ops[] = {
    pflash_readb,
    pflash_readw,
    pflash_readl,
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
        n = n >> 1;
    }
#if 0 /* This is not necessary as n is never 0 */
    if (!n)
        ret++;
#endif

    return ret;
}

static void flash_reset(void *opaque)
{
    pflash_t *pfl = (pflash_t *)opaque;
    DPRINTF("%s:%u\n", __FILE__, __LINE__);
    pflash_rom_mode(pfl);
    pfl->bypass = 0;
    pfl->wcycle = 0;
    pfl->cmd = 0;
}

pflash_t *pflash_amd_register (target_phys_addr_t base, ram_addr_t off,
                           BlockDriverState *bs,
                           uint32_t sector_len, int nb_blocs, int width,
                           uint16_t id0, uint16_t id1,
                           uint16_t id2, uint16_t id3)
{
    pflash_t *pfl;
    target_long total_len;

#ifdef PFLASH_DEBUG
    if (getenv("DEBUG_FLASH")) {
        traceflag = strtoul(getenv("DEBUG_FLASH"), 0, 0);
    }
    DPRINTF("Logging enabled for FLASH in %s\n", __func__);
#endif

    total_len = sector_len * nb_blocs;

    DPRINTF("flash size " TARGET_FMT_lu " MiB (" TARGET_FMT_lu " x %u bytes)\n",
            total_len / MiB, total_len / width, width);

    /* XXX: to be fixed */
    if (total_len != (2 * MiB) && total_len != (4 * MiB) &&
        total_len != (8 * MiB) && total_len != (16 * MiB) &&
        total_len != (32 * MiB) && total_len != (64 * MiB))
        return NULL;
    pfl = qemu_mallocz(sizeof(pflash_t));
    if (pfl == NULL)
        return NULL;
    pfl->storage = qemu_get_ram_ptr(off);
    pfl->fl_mem = cpu_register_io_memory(0, pflash_read_ops, pflash_write_ops, pfl);
    pfl->off = off;
    pfl->base = base;
    pfl->sector_len = sector_len;
    pfl->total_len = total_len;
    pflash_rom_mode(pfl);
    pfl->bs = bs;
    if (pfl->bs) {
        /* read the initial flash content */
        bdrv_read(pfl->bs, 0, pfl->storage, total_len >> 9);
    }
#if 0 /* XXX: there should be a bit to set up read-only,
       *      the same way the hardware does (with WP pin).
       */
    pfl->ro = 1;
#else
    pfl->ro = 0;
#endif
    pfl->timer = qemu_new_timer(vm_clock, pflash_timer, pfl);
    pfl->width = width;
    pfl->wcycle = 0;
    pfl->cmd = 0;
    pfl->status = 0;
    pfl->ident[0] = id0;
    pfl->ident[1] = id1;
    pfl->ident[2] = id2;
    pfl->ident[3] = id3;
#if 0
    /* Hardcoded CFI table (mostly from SG29 Spansion flash) */
    /* Standard "QRY" string */
    pfl->cfi_table[0x10] = 'Q';
    pfl->cfi_table[0x11] = 'R';
    pfl->cfi_table[0x12] = 'Y';
    /* Command set (AMD/Fujitsu) */
    pfl->cfi_table[0x13] = P_ID_AMD_STD;
    pfl->cfi_table[0x14] = 0x00;
    /* Primary extended table address (none) */
    pfl->cfi_table[0x15] = 0x00;
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
    /* Timeout for min size buffer write (16 µs) */
    pfl->cfi_table[0x20] = 0x04;
    /* Typical timeout for block erase (512 ms) */
    pfl->cfi_table[0x21] = 0x09;
    /* Typical timeout for full chip erase (4096 ms) */
    pfl->cfi_table[0x22] = 0x0C;
    /* Reserved */
    pfl->cfi_table[0x23] = 0x01;
    /* Max timeout for buffer write */
    pfl->cfi_table[0x24] = 0x04;
    /* Max timeout for block erase */
    pfl->cfi_table[0x25] = 0x0A;
    /* Max timeout for chip erase */
    pfl->cfi_table[0x26] = 0x0D;
    /* Device size */
    //~ pfl->cfi_table[0x27] = ctz32(total_len) + 1;
    pfl->cfi_table[0x27] = ctz32(total_len);	// !!! 0x15
    /* Flash device interface (8 & 16 bits) */
    pfl->cfi_table[0x28] = 0x02;
    pfl->cfi_table[0x29] = 0x00;
    /* Max number of bytes in multi-bytes write */
    pfl->cfi_table[0x2A] = 0x05;
    pfl->cfi_table[0x2B] = 0x00;
    /* Number of erase block regions (uniform) */
    pfl->cfi_table[0x2C] = 0x01;
    /* Erase block region 1 */
    pfl->cfi_table[0x2D] = nb_blocs - 1;
    pfl->cfi_table[0x2E] = (nb_blocs - 1) >> 8;
    pfl->cfi_table[0x2F] = sector_len >> 8;
    pfl->cfi_table[0x30] = sector_len >> 16;
#endif

    if (0) {
    } else if ((id0 == MANUFACTURER_AMD && id1 == AM29LV160DB) ||
               (id0 == MANUFACTURER_004A &&  id1 == ES29LV160DB) ||
               (id0 == MANUFACTURER_SPANSION && id1 == S29AL016DB)) {
        static const uint8_t data[] = {
          /* 0x10 */ 'Q',  'R',  'Y',  0x02, 0x00, 0x40, 0x00, 0x00,
          /* 0x18 */ 0x00, 0x00, 0x00, 0x27, 0x36, 0x00, 0x00, 0x04,
          /* 0x20 */ 0x00, 0x0a, 0x00, 0x05, 0x00, 0x04, 0x00, 0x15,
          /* 0x28 */ 0x02, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x40,
          /* 0x30 */ 0x00, 0x01, 0x00, 0x20, 0x00, 0x00, 0x00, 0x80,
          /* 0x38 */ 0x00, 0x1e, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
          /* 0x40 */ 'P',  'R',  'I',  '1',  '0',  0x00, 0x02, 0x01,
          /* 0x48 */ 0x01, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        };
        memcpy(&pfl->cfi_table[0x10], data, sizeof(data));
        //~ pfl->cfi_table[0x39] = nb_blocs - 2;        // 0x1e
        //~ pfl->cfi_table[0x3a] = (nb_blocs - 2) >> 8; // 0x00
        //~ pfl->cfi_table[0x3b] = (sector_len / 256);  // 0x01
        //~ pfl->cfi_table[0x3c] = (sector_len / 256) >> 8;     // 0x00
    } else if (id0 == MANUFACTURER_MACRONIX && (id1 == MX29LV320CB ||
          id1 == MX29LV320CT || id1 == MX29LV640BB || id1 == MX29LV640BT)) {
        static const uint8_t data[] = {
          /* 0x10 */ 'Q',  'R',  'Y',  0x02, 0x00, 0x40, 0x00, 0x00,
          /* 0x18 */ 0x00, 0x00, 0x00, 0x27, 0x36, 0x00, 0x00, 0x04,
          /* 0x20 */ 0x00, 0x0a, 0x00, 0x05, 0x00, 0x04, 0x00, 0x16,
          /* 0x28 */ 0x02, 0x00, 0x00, 0x00, 0x02, 0x07, 0x00, 0x20,
          /* 0x30 */ 0x00, 0x3e, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
          /* 0x38 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
          /* 0x40 */ 'P',  'R',  'I',  '1',  '1',  0x00, 0x02, 0x04,
          /* 0x48 */ 0x01, 0x04, 0x00, 0x00, 0x00, 0xb5, 0xc5, 0x02,
        };
        memcpy(&pfl->cfi_table[0x10], data, sizeof(data));
        if (id1 == MX29LV640BB || id1 == MX29LV640BT) {
          pfl->cfi_table[0x27] = 0x17;
          pfl->cfi_table[0x31] = 0x7e;
        }
        if (id1 == MX29LV320CT || id1 == MX29LV640BT) {
          pfl->cfi_table[0x4f] = 0x03;
        }
    } else {
        /* SG29 Spansion flash */
        static const uint8_t data[] = {
          /* 0x10 */ 'Q',  'R',  'Y',  0x02, 0x00, 0x00, 0x00, 0x00,
          /* 0x18 */ 0x00, 0x00, 0x00, 0x27, 0x36, 0x00, 0x00, 0x07,
          /* 0x20 */ 0x04, 0x09, 0x0c, 0x01, 0x04, 0x0a, 0x0d, 0x16,
          /* 0x28 */ 0x02, 0x00, 0x05, 0x00, 0x01, 0x00, 0x00, 0x40,
        };
        memcpy(&pfl->cfi_table[0x10], data, sizeof(data));
        pfl->cfi_table[0x27] = ctz32(total_len);
        pfl->cfi_table[0x2D] = nb_blocs - 1;
        pfl->cfi_table[0x2E] = (nb_blocs - 1) >> 8;
        pfl->cfi_table[0x2F] = sector_len >> 8;
        pfl->cfi_table[0x30] = sector_len >> 16;
    }

    qemu_register_reset(flash_reset, 0, pfl);

    return pfl;
}

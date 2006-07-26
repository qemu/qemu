/* id = 0x89a0 ... 0x89aa */
/* Flash-specific definitions.                                             */
/* These encompass all the commands and status checks the program makes.   */
#define FlashCommandReadID      0x90
#define FlashCommandRead        0xFF
#define FlashCommandErase       0x20
#define FlashCommandConfirm     0xD0
#define FlashCommandClear       0x50
#define FlashCommandWrite       0x40
#define FlashCommandLoadPB      0xE0
#define FlashCommandPBWrite     0x0C
#define FlashCommandStatus      0x70
#define FlashCommandSuspend     0xB0
#define FlashCommandResume      0xD0
#define FlashCommandReadESR     0x71
#define FlashCommandQueryCFI    0x98
#define FlashCommandSCSErase    0x28
#define FlashCommandSCSWrite    0xE8
#define FlashStatusReady        0x80
#define FlashStatusSuspended    0x40
#define FlashStatusError        0x3E
#define FlashStatusBlockError   0x3F

#if CFI_OPTIONAL
#define FlashCommandLock        0x60
#define FlashCommandUnlock      0x60
#define FlashCommandLockConfirm 0x01
#define FlashCommandChipErase 0x30
#define FlashCommandConfigureStatus 0xB8
#endif

#define FlashCommandPrefix1     0xAA
#define FlashCommandPrefix2     0x55

/*
 * CFI parallel flash with Intel command set emulation
 * 
 * Copyright (c) 2005 Jocelyn Mayer (CFI 2.0, see original cfi02.c)
 * Copyright (c) 2006 Stefan Weil (CFI 1.0, TE28F160C3-B emulation)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
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
 * - JEDEC ID (partially)
 *
 * It does not support flash interleaving.
 * It does not implement boot blocs with reduced size
 * It does not implement software data protection as found in many real chips
 * It does not implement erase suspend/resume commands
 * It does not implement multiple sectors erase
 */

#include <assert.h>
#include "vl.h"
#include "exec-all.h"

#include "pflash.h"

//~ #define PFLASH_DEBUG
#ifdef PFLASH_DEBUG
#define DPRINTF(fmt, args...)                      \
do {                                               \
    if (loglevel)                                  \
        fprintf(logfile, "PFLASH: " fmt , ##args); \
    else                                           \
        printf("PFLASH: " fmt , ##args);           \
} while (0)
#else
#define DPRINTF(fmt, args...) do { } while (0)
#endif

struct pflash_t {
    BlockDriverState *bs;
    target_ulong base;
    target_ulong sector_len;
    target_ulong total_len;
    int width;
    int wcycle; /* if 0, the flash is read normally */
    int bypass;
    int ro;
    uint8_t cmd;
    uint8_t status;
    uint16_t ident[4];
    uint8_t cfi_len;
    uint8_t cfi_table[0x52];
    QEMUTimer *timer;
    ram_addr_t off;
    int fl_mem;
    void *storage;
};

static enum {
  unknown_mode,
  io_mode,
  rom_mode
} flash_mode;

static void pflash_io_mode(pflash_t *pfl)
{
  if (flash_mode != io_mode) {
    DPRINTF("%s: switch to i/o mode\n", __func__);
    cpu_register_physical_memory(pfl->base, pfl->total_len, pfl->fl_mem);
    flash_mode = io_mode;
  }
}

static void pflash_rom_mode(pflash_t *pfl)
{
  if (flash_mode != rom_mode) {
    DPRINTF("%s: switch to rom mode\n", __func__);
    cpu_register_physical_memory(pfl->base, pfl->total_len,
                                 pfl->off | IO_MEM_ROMD | pfl->fl_mem);
    flash_mode = rom_mode;
  }
}

static uint32_t pflash_read_data(void *addr, target_ulong offset, int width)
{
    uint8_t *p = addr;
    uint32_t ret = (uint32_t)-1;
    switch (width) {
    case 1:
        ret = p[offset];
        break;
    case 2:
        assert((offset & 1) == 0);
#if defined(TARGET_WORDS_BIGENDIAN)
        ret = p[offset] << 8;
        ret |= p[offset + 1];
#else
        ret = p[offset];
        ret |= p[offset + 1] << 8;
#endif
//            DPRINTF("%s: data offset %08x %04x\n", __func__, offset, ret);
        break;
    case 4:
        assert((offset & 3) == 0);
#if defined(TARGET_WORDS_BIGENDIAN)
        ret = p[offset] << 24;
        ret |= p[offset + 1] << 16;
        ret |= p[offset + 2] << 8;
        ret |= p[offset + 3];
#else
        ret = p[offset];
        ret |= p[offset + 1] << 8;
        ret |= p[offset + 1] << 8;
        ret |= p[offset + 2] << 16;
        ret |= p[offset + 3] << 24;
#endif
//            DPRINTF("%s: data offset %08x %08x\n", __func__, offset, ret);
        break;
    }
    return ret;
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
        pflash_rom_mode(pfl);
        pfl->wcycle = 0;
    }
    pfl->cmd = 0;
}

static uint32_t pflash_read (pflash_t *pfl, target_ulong offset, int width)
{
    target_ulong boff;
    uint32_t ret;
    uint8_t *p;

    DPRINTF("%s: offset %08x width %d\n", __func__, offset, width);
    ret = -1;
    offset -= pfl->base;
    boff = (offset & 0xFF) / pfl->width;
    switch (pfl->cmd) {
    //~ case 0x80:
        //~ /* We accept reads during second unlock sequence... */
    case 0x00:
    flash_read:
        /* Flash area read */
        p = pfl->storage;
        ret = pflash_read_data(p, offset, width);
        break;
    case 0x90:
        /* flash ID read */
        switch (boff) {
        case 0x00:
        case 0x01:
            ret = pfl->ident[boff];
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
        DPRINTF("%s: ID %d %x\n", __func__, boff, ret);
        break;
    //~ case 0xA0:
    //~ case 0x10:
    //~ case 0x30:
    case 0x01:
    case 0x28:  // ??? unneeded
    case 0x70:
    case 0xd0:
    case 0x40:
        /* Status register read */
        ret = pfl->status;
        DPRINTF("%s: status %x\n", __func__, ret);
        //~ /* Toggle bit 6 */
        //~ pfl->status ^= 0x40;
        break;
    case 0x98:
        /* CFI query mode */
        if (boff > pfl->cfi_len)
            ret = 0;
        else
            ret = pfl->cfi_table[boff];
        break;
    default:
        /* This should never happen : reset state & treat it as a read*/
        DPRINTF("%s: unknown command state: %x\n", __func__, pfl->cmd);
        pfl->wcycle = 0;
        pfl->cmd = 0;
        goto flash_read;
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

static void pflash_write (pflash_t *pfl, target_ulong offset, uint32_t value,
                          int width)
{
    target_ulong boff;
    uint8_t *p;
    uint8_t cmd;

    /* WARNING: when the memory area is in ROMD mode, the offset is a
       ram offset, not a physical address */
    if (pfl->wcycle == 0)
        offset -= (target_ulong)(long)pfl->storage;
    else
        offset -= pfl->base;
        
    cmd = value;
    DPRINTF("%s: offset %08x %08x %d wc %d\n", __func__, offset, value, width, pfl->wcycle);
    if ((pfl->cmd != 0x40) && (pfl->cmd != 0xe8) && (cmd == 0xf0 || cmd == 0xff)) {
        DPRINTF("%s: flash read array asked (%02x %02x)\n",
                __func__, pfl->cmd, cmd);
        goto reset_flash;
    }
    boff = (offset & (pfl->sector_len - 1)) / pfl->width;
    switch (pfl->wcycle) {
    case 0:
        /* We're in read mode */
        /* Set the device in I/O access mode */
        pflash_io_mode(pfl);
    //~ check_unlock0:
        if (cmd == 0x10 || cmd == 0x40) {
            pfl->cmd = 0x40;
            DPRINTF("%s: Single Byte Program (%04x %02x)\n",
                    __func__, boff, cmd);
        } else if (cmd == 0x20 || cmd == 0x28) {
            DPRINTF("%s: Block Erase started (%04x %02x)\n",
                    __func__, boff, cmd);
            pfl->cmd = 0x28;
            pfl->status = 0x80;
        } else if (cmd == 0x50) {
            DPRINTF("%s: Clear Status Register (%04x %02x)\n",
                    __func__, boff, cmd);
            pfl->status = 0x80;
            return;
        } else if (/* boff == 0x55 && */ cmd == 0x98) {
        enter_CFI_mode:
            /* Enter CFI query mode */
            pfl->wcycle = 7;
            pfl->cmd = 0x98;
            return;
        } else if (cmd == 0x60) {
            DPRINTF("%s: Block Lock Bit Set / Reset started\n", __func__);
            pfl->cmd = cmd;
        } else if (cmd == 0x70) {
            DPRINTF("%s: Read Status\n", __func__);
            pfl->cmd = cmd;
            return;
        } else if (cmd == 0x90) {
            /* Read JEDEC ID. */
            DPRINTF("%s: Read JEDEC ID (%04x %02x)\n",
                    __func__, boff, cmd);
            pfl->cmd = cmd;
            //~ assert(offset == 0 && 0);
            //~ assert(offset == 0 && 0);
            //~ assert(offset == 0 && 0);
        } else if (cmd == 0xb0) {
            DPRINTF("%s: unimplemented Erase / Program Suspend / Resume (%04x %02x)\n",
                    __func__, boff, cmd);
            return;
        } else if (cmd == 0xe8) {
            DPRINTF("%s: unimplemented Write to Buffer (%04x %02x)\n",
                    __func__, boff, cmd);
            return;
        } else if (boff == 0x555 && cmd == 0xAA) {
            DPRINTF("%s: unlock sequence started\n", __func__);
        } else {
            DPRINTF("%s: unlock0 failed %04x %02x %04x\n",
                    __func__, boff, cmd, 0x555);
            goto reset_flash;
        }
        break;
    case 1:
        if (0) {
        } else if (pfl->cmd == 0x28 && cmd == 0xd0) {
            DPRINTF("%s: Block Erase confirmed (%08x %02x)\n",
                    __func__, offset, cmd);
            p = pfl->storage;
            offset &= ~(pfl->sector_len - 1);
            memset(p + offset, 0xFF, pfl->sector_len);
            pflash_update(pfl, offset, pfl->sector_len);
            //~ pfl->status = 0x00;
            //~ /* Let's wait 1/2 second before sector erase is done */
            //~ qemu_mod_timer(pfl->timer, 
                           //~ qemu_get_clock(vm_clock) + (ticks_per_sec / 2));
            pfl->cmd = 0x70;
            pfl->status = 0x80;
            pfl->wcycle = 0;
            return;
        } else if (pfl->cmd == 0x40) {
            DPRINTF("%s: Single Byte Program data offset %08x %08x %d\n",
                    __func__, offset, value, width);
            p = pfl->storage;
            switch (width) {
            case 1:
                p[offset] &= value;
                break;
            case 2:
#if defined(TARGET_WORDS_BIGENDIAN)
                p[offset] &= value >> 8;
                p[offset + 1] &= value;
#else
                p[offset] &= value;
                p[offset + 1] &= value >> 8;
#endif
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
                break;
            }
            pflash_update(pfl, offset, width);
            pfl->cmd = 0x70;
            pfl->status = 0x80;
            pfl->wcycle = 0;
            /* Let's pretend write is immediate */
            //~ if (pfl->bypass)
                //~ goto do_bypass;
            return;
        } else if (pfl->cmd == 0x60) {
            /* !!! */
            if (cmd == 0x01) {
                DPRINTF("%s: unimplemented Block Lock Bit Set done\n", __func__);
                pfl->cmd = cmd;
                pfl->status = 0x80;
            } else if (cmd == 0xd0) {
                DPRINTF("%s: unimplemented Block Lock Bit Reset done\n", __func__);
                pfl->cmd = cmd;
                pfl->status = 0x80;
            } else {
                DPRINTF("%s: block lock bit failed %04x %02x\n", __func__, boff, cmd);
                goto reset_flash;
            }
            pfl->cmd = cmd;
            pfl->wcycle = 0;
            return;
        } else {
            /* We started an unlock sequence */
        check_unlock1:
            if (boff != 0x2AA || cmd != 0x55) {
                DPRINTF("%s: unlock1 failed %04x %02x\n", __func__, boff, cmd);
                goto reset_flash;
            }
            DPRINTF("%s: unlock sequence done\n", __func__);
        }
        break;
    //~ case 2:
        //~ /* We finished an unlock sequence */
        //~ if (!pfl->bypass && boff != 0x555) {
            //~ DPRINTF("%s: command failed %04x %02x\n", __func__, boff, cmd);
            //~ goto reset_flash;
        //~ }
        //~ switch (cmd) {
        //~ case 0x20:
            //~ pfl->bypass = 1;
            //~ goto do_bypass;
        //~ case 0x80:
        //~ case 0x90:
        //~ case 0xA0:
            //~ pfl->cmd = cmd;
            //~ DPRINTF("%s: starting command %02x\n", __func__, cmd);
            //~ break;
        //~ default:
            //~ DPRINTF("%s: unknown command %02x\n", __func__, cmd);
            //~ goto reset_flash;
        //~ }
        //~ break;
    case 3:
        switch (pfl->cmd) {
        //~ case 0x80:
            //~ /* We need another unlock sequence */
            //~ goto check_unlock0;
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
            /* Ignore writes while flash data write is occuring */
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
            if (boff != 0x555) {
                DPRINTF("%s: chip erase: invalid address %04x\n",
                        __func__, offset);
                goto reset_flash;
            }
            /* Chip erase */
            DPRINTF("%s: start chip erase\n", __func__);
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
            offset &= ~(pfl->sector_len - 1);
            DPRINTF("%s: start sector erase at %08x\n", __func__, offset);
            memset(p + offset, 0xFF, pfl->sector_len);
            pflash_update(pfl, offset, pfl->sector_len);
            pfl->status = 0x00;
            /* Let's wait 1/2 second before sector erase is done */
            qemu_mod_timer(pfl->timer, 
                           qemu_get_clock(vm_clock) + (ticks_per_sec / 2));
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
        DPRINTF("%s: invalid write state (wc %d)\n",  __func__, pfl->wcycle);
        goto reset_flash;
    }
    pfl->wcycle++;

    return;

    /* Reset flash */
 reset_flash:
    //~ assert(pfl->wcycle != 0);
    pflash_rom_mode(pfl);
    pfl->bypass = 0;
    pfl->wcycle = 0;
    pfl->cmd = 0;
    return;

 //~ do_bypass:
    //~ pfl->wcycle = 2;
    //~ pfl->cmd = 0;
    //~ return;
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

static int flash_load(QEMUFile *f, void *opaque, int version_id)
{
    pflash_t *pfl = opaque;
    int result = 0;
    if (version_id == 0) {
        qemu_get_buffer(f, (uint8_t *)pfl, sizeof(*pfl));
    } else {
        result = -EINVAL;
    }
    return result;
}

static void flash_save(QEMUFile *f, void* opaque)
{
    /* TODO: fix */
    pflash_t *pfl = opaque;
    qemu_put_buffer(f, (uint8_t *)pfl, sizeof(*pfl));
}

static void flash_reset(void *opaque)
{
    pflash_t *pfl = opaque;
    printf("%s: %s:%u\n", __func__, __FILE__, __LINE__);
}

pflash_t *pflash_cfi01_register (target_ulong base, ram_addr_t off,
                                 BlockDriverState *bs,
                                 target_ulong sector_len, int nb_blocs, int width,
                                 uint16_t id0, uint16_t id1, 
                                 uint16_t id2, uint16_t id3)
{
    pflash_t *pfl;
    target_long total_len;

    id0 = MANUFACTURER_INTEL;
    id1 = I28F160C3B;

    total_len = sector_len * nb_blocs;
    /* XXX: to be fixed */
    if (total_len != (2 * 1024 * 1024) && total_len != (4 * 1024 * 1024) &&
        total_len != (8 * 1024 * 1024) && total_len != (16 * 1024 * 1024) &&
        total_len != (32 * 1024 * 1024) && total_len != (64 * 1024 * 1024))
        return NULL;
    pfl = qemu_mallocz(sizeof(pflash_t));
    if (pfl == NULL)
        return NULL;
    pfl->storage = phys_ram_base + off;
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
    /* Hardcoded CFI table */
    pfl->cfi_len = 0x47;
    /* Standard "QRY" string */
    pfl->cfi_table[0x10] = 'Q';
    pfl->cfi_table[0x11] = 'R';
    pfl->cfi_table[0x12] = 'Y';
    /* Primary Vendor Command Set (Intel) */
    pfl->cfi_table[0x13] = (P_ID_INTEL_STD & 0xff);
    pfl->cfi_table[0x14] = (P_ID_INTEL_STD >> 8);
    /* Primary extended table address */
    pfl->cfi_table[0x15] = 0x35;
    pfl->cfi_table[0x16] = 0x00;
    /* Alternate Vendor Command Set (none) */
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
    pfl->cfi_table[0x1D] = 0xb4;
    /* Vpp max (no Vpp pin) */
    pfl->cfi_table[0x1E] = 0xc6;
    /* Reserved */
    pfl->cfi_table[0x1F] = 0x05;
    /* Timeout for min size buffer write */
    pfl->cfi_table[0x20] = 0x00;
    /* Typical timeout for block erase */
    pfl->cfi_table[0x21] = 0x0a;
    /* Typical timeout for full chip erase */
    pfl->cfi_table[0x22] = 0x00;
    /* Reserved */
    pfl->cfi_table[0x23] = 0x04;
    /* Max timeout for buffer write */
    pfl->cfi_table[0x24] = 0x00;
    /* Max timeout for block erase */
    pfl->cfi_table[0x25] = 0x03;
    /* Max timeout for chip erase */
    pfl->cfi_table[0x26] = 0x00;
    /* Device size */
    pfl->cfi_table[0x27] = ctz32(total_len);	// !!! 0x15
    /* Flash device interface (8 & 16 bits) */
    pfl->cfi_table[0x28] = 0x01;
    pfl->cfi_table[0x29] = 0x00;
    /* Max number of bytes in multi-bytes write */
    pfl->cfi_table[0x2A] = 0x00;
    pfl->cfi_table[0x2B] = 0x00;
    /* Number of erase block regions */
    pfl->cfi_table[0x2C] = 0x02;
    /* Erase block region 1 */
    pfl->cfi_table[0x2D] = 0x07;
    pfl->cfi_table[0x2E] = 0x00;
    pfl->cfi_table[0x2F] = 0x20;
    pfl->cfi_table[0x30] = 0x00;
    /* Erase block region 2 */
    pfl->cfi_table[0x31] = nb_blocs - 2;	// 0x1e / 0x3e
    pfl->cfi_table[0x32] = (nb_blocs - 2) >> 8;	// 0x00
    pfl->cfi_table[0x33] = (sector_len >> 8);	// 0x00
    pfl->cfi_table[0x34] = (sector_len >> 16);	// 0x01
    /* Extended query table */
    pfl->cfi_table[0x35] = 'P';
    pfl->cfi_table[0x36] = 'R';
    pfl->cfi_table[0x37] = 'I';
    /* Major and minor version number */
    pfl->cfi_table[0x38] = 0x31;
    pfl->cfi_table[0x39] = 0x30;
    /* Optional Feature & Command Support */
    pfl->cfi_table[0x3a] = 0x6a;
    pfl->cfi_table[0x3b] = 0x00;
    pfl->cfi_table[0x3c] = 0x00;
    pfl->cfi_table[0x3d] = 0x00;
    /* Supported functions after suspend */
    pfl->cfi_table[0x3e] = 0x01;
    pfl->cfi_table[0x3f] = 0x03;
    pfl->cfi_table[0x40] = 0x00;
    pfl->cfi_table[0x41] = 0x33;
    pfl->cfi_table[0x42] = 0xc0;
    pfl->cfi_table[0x43] = 0x01;
    pfl->cfi_table[0x44] = 0x80;
    pfl->cfi_table[0x45] = 0x00;
    pfl->cfi_table[0x46] = 0x03;
    pfl->cfi_table[0x47] = 0x03;

#define flash_instance 0
#define flash_version 0
    qemu_register_reset(flash_reset, pfl);
    register_savevm("flash", flash_instance, flash_version, flash_save, flash_load, pfl);

    return pfl;
}

/*
PFLASH: pflash_write: offset 003c3810 00000060 2
PFLASH: pflash_write: unlock0 failed 1c08 60 0555
PFLASH: pflash_write: offset 6a088810 000000d0 2
PFLASH: pflash_write: unlock0 failed 4408 d0 0555
PFLASH: pflash_write: offset 6a088810 00000040 2
PFLASH: pflash_write: unlock0 failed 4408 40 0555
PFLASH: pflash_write: offset 6a088810 00000000 2
PFLASH: pflash_write: unlock0 failed 4408 00 0555
PFLASH: pflash_read: offset 103c3810
PFLASH: pflash_read: offset 103c3810
PFLASH: pflash_read: offset 103c3810
PFLASH: pflash_read: offset 103c3810
PFLASH: pflash_read: offset 103c3810
PFLASH: pflash_read: offset 103c3810

ERASE Flash
---------------------------------------
    Area            Address      Length
---------------------------------------
[0] Boot            0xB0000000     128K
[1] Configuration   0xB0020000     128K
[2] Web Image       0xB0040000     832K
[3] Code Image      0xB0110000     896K
[4] Boot Params     0xB01F0000      64K
[5] Flash Image     0xB0000000    2048K
---------------------------------------
Enter area to ERASE: 4
Erase area 4.  Are you sure? (Y/n) Yes
erase from location B01F0000tlb_set_page_exec: vaddr=b01f0000 paddr=0x101f0000 addr_write=0xb01f0050 prot=7 u=0 pd=0x021f0051
PFLASH: pflash_write: offset 001f0000 00006060 2 wc 0
PFLASH: pflash_io_mode: switch to i/o mode
PFLASH: pflash_write: Block Lock Bit Set / Reset started
PFLASH: pflash_write: offset 001f0000 0000d0d0 2 wc 1
PFLASH: pflash_write: unimplemented Block Lock Bit Reset done
PFLASH: pflash_write: offset 69e41000 0000ffff 2 wc 0
PFLASH: pflash_write: flash read array asked (d0 ff)
PFLASH: pflash_write: offset 69e41000 00002020 2 wc 0
PFLASH: pflash_io_mode: switch to i/o mode
PFLASH: pflash_write: unimplemented Block Erase (0800 20)
PFLASH: pflash_write: offset 001f0000 0000d0d0 2 wc 1
PFLASH: pflash_write: unlock1 failed 0000 d0
PFLASH: pflash_rom_mode: switch to rom mode
tlb_set_page_exec: vaddr=b01f0000 paddr=0x101f0000 addr_write=0xb01f0050 prot=7 u=0 pd=0x021f0051
PFLASH: pflash_write: offset 001f0000 0000ffff 2 wc 0
PFLASH: pflash_write: flash read array asked (00 ff)
PFLASH: pflash_write: offset 001f0000 00007070 2 wc 0
PFLASH: pflash_io_mode: switch to i/o mode
PFLASH: pflash_write: Read Status
PFLASH: pflash_write: offset 001f0000 00005050 2 wc 1
PFLASH: pflash_write: unlock1 failed 0000 50
PFLASH: pflash_rom_mode: switch to rom mode
 error
Erase fail!

*/

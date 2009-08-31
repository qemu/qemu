/*
 * CFI parallel flash emulation (Intel extended vendor command set / ID 0x0001)
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
 * - CFI queries
 * - JEDEC ID (partially)
 * - !!! check
 *
 * It does not support flash interleaving.
 * It does not implement boot blocks with reduced size
 * It does not implement software data protection as found in many real chips
 * It does not implement erase suspend/resume commands
 * It does not implement multiple sectors erase
 */

#include <assert.h>
#include <stdio.h>      /* fprintf */

#include "hw.h"
#include "block.h"
#include "flash.h"
#include "pflash.h"     /* pflash_cfi01_register */
#include "qemu-timer.h"
#include "exec-all.h"

#define PFLASH_DEBUG
#ifdef PFLASH_DEBUG
static int traceflag;
#define DPRINTF(fmt, ...) \
    (traceflag ? fprintf(stderr, "PFLASH\t%-24s" fmt, __func__, ##__VA_ARGS__) : (void)0)
#else
#define DPRINTF(fmt, ...) ((void)0)
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
    flash_mode_t mode;
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

static uint32_t pflash_read_data(const void *addr, uint32_t offset, int width)
{
    //~ check, maybe should use HOST_WORDS_BIGENDIAN
    const uint8_t *p = addr;
    uint32_t value = (uint32_t)-1;
    switch (width) {
    case 1:
        value = p[offset];
        break;
    case 2:
        assert((offset & 1) == 0);
#if defined(TARGET_WORDS_BIGENDIAN)
        value = p[offset] << 8;
        value |= p[offset + 1];
#else
        value = p[offset];
        value |= p[offset + 1] << 8;
#endif
        //~ DPRINTF("data offset " TARGET_FMT_lx " %04x\n", offset, value);
        break;
    case 4:
        assert((offset & 3) == 0);
#if defined(TARGET_WORDS_BIGENDIAN)
        value = p[offset] << 24;
        value |= p[offset + 1] << 16;
        value |= p[offset + 2] << 8;
        value |= p[offset + 3];
#else
        value = p[offset];
        value |= p[offset + 1] << 8;
        value |= p[offset + 1] << 8;
        value |= p[offset + 2] << 16;
        value |= p[offset + 3] << 24;
#endif
        //~ DPRINTF("data offset " TARGET_FMT_lx " %08x\n", offset, value);
        break;
    }
    return value;
}

static void pflash_write_data(void *addr, uint32_t offset, uint32_t value, int width)
{
    //~ check, maybe should use HOST_WORDS_BIGENDIAN
    uint8_t *p = addr;
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
}

static void pflash_timer (void *opaque)
{
    pflash_t *pfl = opaque;

    DPRINTF("command %02x done\n", pfl->cmd);
    /* Reset flash */
    pfl->status |= 0x80;
    //~ pflash_rom_mode(pfl);
    //~ pfl->cmd = 0;
}

static uint32_t pflash_read (pflash_t *pfl, uint32_t offset, int width)
{
    uint32_t boff;
    uint32_t ret = 0xffffffffU;

    offset -= pfl->base;
    boff = offset / pfl->width;
    switch (pfl->cmd) {
        case 0x00:
        flash_read:
            /* Flash area read */
            ret = pflash_read_data(pfl->storage, offset, width);
            break;
        case 0x90:
            /* flash ID read */
            switch (boff) {
                case 0x00:
                case 0x01:
                    ret = pfl->ident[boff];
                    break;
                default:
                    DPRINTF("??? offset %08" PRIx32 " %08x %d\n", offset, ret, width);
                    assert(0);
                    goto flash_read;
            }
            DPRINTF("ID %08" PRIx32 " 0x%04x\n", offset, ret);
            break;
        case 0x10:
        case 0x30:
        case 0x01:
        case 0x28:  // ??? unneeded
        case 0x70:
        case 0xd0:
        case 0x40:
            /* Status register read */
            ret = pfl->status;
            DPRINTF("status 0x%x\n", ret);
            break;
        case 0x98:
            /* CFI query mode */
            if (boff < sizeof(pfl->cfi_table)) {
                ret = pfl->cfi_table[boff];
                DPRINTF("CFI 0x%02x 0x%02x\n", (unsigned)boff, ret);
            } else {
                ret = 0;
            }
            break;
        default:
            /* This should never happen : reset state & treat it as a read*/
            DPRINTF("unknown command state: 0x%x\n", pfl->cmd);
            pfl->cmd = 0;
            goto flash_read;
    }

    DPRINTF("offset %08" PRIx32 " %08x %d\n", offset, ret, width);
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

#if 0

/* Status bits */

#define FlashStatusReady        0x80
#define FlashStatusSuspended    0x40
#define FlashStatusError        0x3E
#define FlashStatusBlockError   0x3F

/* Query commands */

#define FlashCommandReadID      0x90    /* Read JEDEC ID */
#define FlashCommandQueryCFI    0x98    /* CFI Query */

/* Basic Command Set */

#define FlashCommandRead        0xFF    /* Read Array */
#define FlashCommandClear       0x50    /* Clear Status Register */
#define FlashCommandErase       0x20    /* Block Erase */
#define FlashCommandSCSErase    0x28    /* Block Erase */
#define FlashCommandSuspend     0xB0    /* Erase Suspend */
#define FlashCommandResume      0xD0    /* Erase Resume */
#define FlashCommandWrite       0x10    /* Single Byte Program */
#define FlashCommandWrite       0x40    /* Single Byte Program */

#endif

/* Scaleable Command Set */

#define FlashCommandConfirm     0xD0
#define FlashCommandLoadPB      0xE0
#define FlashCommandPBWrite     0x0C
#define FlashCommandStatus      0x70
#define FlashCommandReadESR     0x71
#define FlashCommandSCSWrite    0xE8

#if defined(CFI_OPTIONAL) && CFI_OPTIONAL
#define FlashCommandLock        0x60
#define FlashCommandUnlock      0x60
#define FlashCommandLockConfirm 0x01
#define FlashCommandChipErase 0x30
#define FlashCommandConfigureStatus 0xB8
#endif

#define FlashCommandPrefix1     0xAA
#define FlashCommandPrefix2     0x55

static void pflash_write (pflash_t *pfl, uint32_t offset, uint32_t value,
                          int width)
{
    uint32_t boff;
    uint8_t cmd = value;
    uint32_t sector_len = pfl->sector_len;

    if (pfl->mode == rom_mode) {
        offset -= (uint32_t)(long)pfl->storage;
    } else {
        offset -= pfl->base;
    }

    DPRINTF("offset %08" PRIx32 " %08x %d\n", offset, value, width);

    if (pfl->cmd == 0x40) {
        DPRINTF("Single Byte Program offset %08" PRIx32 " data %08x %d\n",
                offset, value, width);
        pflash_write_data(pfl->storage, offset, value, width);
        pflash_update(pfl, offset, width);
        pfl->cmd = FlashCommandStatus;
        /* Let's pretend write is immediate. */
        pfl->status |= 0x80;
    //~ } else if (pfl->cmd == 0xe8) {
        //~ assert(0);
    } else if (cmd == 0xff) {
        DPRINTF("Read Array (%02x %02x)\n", pfl->cmd, cmd);
        goto reset_flash;
    } else if (cmd == 0x10 || cmd == 0x40) {
        pfl->cmd = 0x40;
        DPRINTF("Single Byte Program (%04x %02x)\n", (unsigned)offset, cmd);
        pflash_io_mode(pfl);
    } else if (cmd == 0x50) {
        DPRINTF("Clear Status Register (%04x %02x)\n", (unsigned)offset, cmd);
        pfl->status = 0x00;
        pflash_rom_mode(pfl);
    } else if (pfl->mode == rom_mode) {

        boff = (offset & (sector_len - 1)) / pfl->width;

        switch (cmd) {
            case 0x20:
            case 0x28:
                DPRINTF("Block Erase started (%04x %02x)\n", (unsigned)boff, cmd);
                pfl->cmd = 0x28;
                //~ pfl->status |= 0x80;
                break;
            case 0x60:
                DPRINTF("Block Lock Bit Set / Reset started\n");
                pfl->cmd = cmd;
                break;
            case 0x70:
                DPRINTF("Read Status\n");
                pfl->cmd = cmd;
                break;
            case 0x90:          /* Read JEDEC ID. */
                DPRINTF("Read JEDEC ID (%04x %02x)\n", (unsigned)boff, cmd);
                pfl->cmd = cmd;
                break;
            case 0x98:          /* Enter CFI query mode */
                /* Intel flash accepts CFI mode command on any address. */
                pfl->cmd = 0x98;
                break;
            case 0xb0:
                DPRINTF("unimplemented Erase / Program Suspend / Resume (%04x %02x)\n",
                        (unsigned)boff, cmd);
                return;
            case 0xe8:
                DPRINTF("unimplemented Write to Buffer (%04x %02x)\n",
                        (unsigned)boff, cmd);
                return;
            default:
                return;
        }
        pflash_io_mode(pfl);
        return;
    } else if (pfl->cmd == 0x28 && cmd == 0xd0) {
        DPRINTF("Block Erase confirmed (%08" PRIx32 " %02x)\n", offset, cmd);
        offset &= ~(sector_len - 1);
        memset(pfl->storage + offset, 0xFF, sector_len);
        pflash_update(pfl, offset, sector_len);
        pfl->status = 0x00;
        /* Let's wait 1/2 second before sector erase is done */
        qemu_mod_timer(pfl->timer, 
                       qemu_get_clock(vm_clock) + (ticks_per_sec / 2));
        pfl->cmd = FlashCommandStatus;
        return;
    } else if (pfl->cmd == 0x60) {
        /* !!! */
        if (cmd == 0x01) {
            DPRINTF("unimplemented Block Lock Bit Set done\n");
            pfl->cmd = cmd;
            pfl->status |= 0x80;
        } else if (cmd == 0xd0) {
            DPRINTF("Block Lock Bit Reset done\n");
            pfl->cmd = cmd;
            pfl->status |= 0x80;
        } else {
            DPRINTF("block lock bit failed %04x %02x\n", (unsigned)offset, cmd);
            goto reset_flash;
        }
        pfl->cmd = cmd;
    } else {
        /* Should never happen */
        DPRINTF("invalid write state\n");
        goto reset_flash;
    }

    return;

    /* Reset flash */
 reset_flash:
    pflash_rom_mode(pfl);
    pfl->cmd = 0;
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
    DPRINTF("%s:%u\n", __FILE__, __LINE__);
    pflash_rom_mode(pfl);
    pfl->cmd = 0;
}

pflash_t *pflash_cfi01_register (target_phys_addr_t base, ram_addr_t off,
                                 BlockDriverState *bs,
                                 uint32_t sector_len, int nb_blocs, int width,
                                 uint16_t id0, uint16_t id1, 
                                 uint16_t id2, uint16_t id3)
{
    pflash_t *pfl;
    target_phys_addr_t total_len;
    int ret;

#ifdef PFLASH_DEBUG
    if (getenv("DEBUG_FLASH")) {
        traceflag = strtoul(getenv("DEBUG_FLASH"), 0, 0);
    }
    DPRINTF("Logging enabled for FLASH in %s\n", __func__);
#endif

    /* Currently, only one flash chip is supported. */
    assert(id0 == MANUFACTURER_INTEL);

    total_len = sector_len * nb_blocs;

    DPRINTF("flash size %u MiB (%u x %u bytes)\n",
            (unsigned)(total_len / MiB),
            (unsigned)(total_len / width), width);

    /* XXX: to be fixed */
    if (total_len != (2 * MiB) && total_len != (4 * MiB) &&
        total_len != (8 * MiB) && total_len != (16 * MiB) &&
        total_len != (32 * MiB) && total_len != (64 * MiB))
        return NULL;
    pfl = qemu_mallocz(sizeof(pflash_t));
    /* FIXME: Allocate ram ourselves.  */
    pfl->storage = qemu_get_ram_ptr(off);
    pfl->fl_mem = cpu_register_io_memory(
                    pflash_read_ops, pflash_write_ops, pfl);
    pfl->off = off;
    pfl->base = base;
    pfl->sector_len = sector_len;
    pfl->total_len = total_len;
    pflash_rom_mode(pfl);
    pfl->bs = bs;
    if (pfl->bs) {
        /* read the initial flash content */
        ret = bdrv_read(pfl->bs, 0, pfl->storage, total_len >> 9);
        if (ret < 0) {
            cpu_unregister_io_memory(pfl->fl_mem);
            qemu_free(pfl);
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
    pfl->timer = qemu_new_timer(vm_clock, pflash_timer, pfl);
    pfl->width = width;
    pfl->cmd = 0;
    pfl->status = 0;
    pfl->ident[0] = id0;
    pfl->ident[1] = id1;
    pfl->ident[2] = id2;
    pfl->ident[3] = id3;

    /* Erase block region 1 */
    /* Erase block region 2 */
    /* Extended query table */
    /* Major and minor version number */
    /* Optional Feature & Command Support */
    /* Supported functions after suspend */

    if (0) {
    } else if (id0 == MANUFACTURER_INTEL && (id1 == I28F160C3B)) {
        static const uint8_t data[] = {
          /* 0x10 */ 'Q',  'R',  'Y',  0x03, 0x00, 0x35, 0x00, 0x00,
          /* 0x18 */ 0x00, 0x00, 0x00, 0x27, 0x36, 0xb4, 0xc6, 0x05,
          /* 0x20 */ 0x00, 0x0a, 0x00, 0x04, 0x00, 0x03, 0x00, 0x15,
          /* 0x28 */ 0x01, 0x00, 0x00, 0x00, 0x02, 0x07, 0x00, 0x20,
          /* 0x30 */ 0x00, 0x1e, 0x00, 0x00, 0x01, 'P',  'R',  'I',
          /* 0x38 */ '1',  '0',  0x6a, 0x00, 0x00, 0x00, 0x01, 0x03,
          /* 0x40 */ 0x00, 0x33, 0xc0, 0x01, 0x80, 0x00, 0x03, 0x03,
        };
        memcpy(&pfl->cfi_table[0x10], data, sizeof(data));
        /* Device size */
        pfl->cfi_table[0x27] = ctz32(total_len);	// !!! 0x15
        pfl->cfi_table[0x31] = nb_blocs - 2;	// 0x1e / 0x3e
        pfl->cfi_table[0x32] = (nb_blocs - 2) >> 8;	// 0x00
        pfl->cfi_table[0x33] = (sector_len >> 8);	// 0x00
        pfl->cfi_table[0x34] = (sector_len >> 16);	// 0x01
    } else if (id0 == MANUFACTURER_INTEL && (id1 == I28F160S5)) {
        static const uint8_t data[] = {
          /* 0x10 */ 'Q',  'R',  'Y',  0x01, 0x00, 0x31, 0x00, 0x00,
          /* 0x18 */ 0x00, 0x00, 0x00, 0x27, 0x55, 0x27, 0x55, 0x03,
          /* 0x20 */ 0x06, 0x0a, 0x0f, 0x04, 0x04, 0x04, 0x04, 0x15,
          /* 0x28 */ 0x02, 0x00, 0x05, 0x00, 0x01, 0x1f, 0x00, 0x00,
          /* 0x30 */ 0x01, 'P',  'R',  'I',  '1',  '0',  0x0f, 0x00,
          /* 0x38 */ 0x00, 0x00, 0x01, 0x03, 0x00, 0x50, 0x50, 0x00,
        };
        memcpy(&pfl->cfi_table[0x10], data, sizeof(data));
    } else {
        assert(0);
    }

#define flash_instance 0
#define flash_version 0
    qemu_register_reset(flash_reset, pfl);
    register_savevm("flash", flash_instance, flash_version, flash_save, flash_load, pfl);

    return pfl;
}

/*

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




flash_program error at B01F004C
PFLASH  pflash_read             offset 001f0050
PFLASH  pflash_read             status 0
PFLASH  pflash_read             offset 001f0050 00000000 4
PFLASH  pflash_write            offset 001f0052 0000ffff 2
PFLASH  pflash_write            Read Array (70 ff)
PFLASH  pflash_rom_mode         switch to rom mode
PFLASH  pflash_write            offset 001f0052 00006060 2
PFLASH  pflash_write            Block Lock Bit Set / Reset started
PFLASH  pflash_io_mode          switch to i/o mode
PFLASH  pflash_write            offset 001f0052 0000d0d0 2
PFLASH  pflash_write            Block Lock Bit Reset done
PFLASH  pflash_write            offset 001f0052 00004040 2
PFLASH  pflash_write            Single Byte Program (1f0052 40)
PFLASH  pflash_write            offset 001f0052 00004f42 2
PFLASH  pflash_write            Single Byte Program data offset 001f0052 00004f42 2
PFLASH  pflash_read             offset 001f0052
PFLASH  pflash_read             status 80
PFLASH  pflash_read             offset 001f0052 00000080 2
PFLASH  pflash_write            offset 001f0052 0000ffff 2
PFLASH  pflash_write            Read Array (70 ff)
PFLASH  pflash_rom_mode         switch to rom mode
PFLASH  pflash_write            offset 001f0052 0000ffff 2
PFLASH  pflash_write            Read Array (00 ff)
PFLASH  pflash_write            offset 001f0052 00007070 2
PFLASH  pflash_write            Read Status
PFLASH  pflash_io_mode          switch to i/o mode
PFLASH  pflash_write            offset 001f0052 00005050 2
PFLASH  pflash_write            Clear Status Register (1f0052 50)
PFLASH  pflash_write            offset 001f0050 0000ffff 2
PFLASH  pflash_write            Read Array (70 ff)
PFLASH  pflash_rom_mode         switch to rom mode
PFLASH  pflash_write            offset 001f0050 00006060 2
PFLASH  pflash_write            Block Lock Bit Set / Reset started
PFLASH  pflash_io_mode          switch to i/o mode
PFLASH  pflash_write            offset 001f0050 0000d0d0 2
PFLASH  pflash_write            Block Lock Bit Reset done
PFLASH  pflash_write            offset 001f0050 00004040 2
PFLASH  pflash_write            Single Byte Program (1f0050 40)
PFLASH  pflash_write            offset 001f0050 00002d4e 2
PFLASH  pflash_write            Single Byte Program data offset 001f0050 00002d4e 2
PFLASH  pflash_read             offset 001f0050
PFLASH  pflash_read             status 80
PFLASH  pflash_read             offset 001f0050 00000080 2
PFLASH  pflash_write            offset 001f0050 0000ffff 2
PFLASH  pflash_write            Read Array (70 ff)
PFLASH  pflash_rom_mode         switch to rom mode
PFLASH  pflash_write            offset 001f0050 0000ffff 2
PFLASH  pflash_write            Read Array (00 ff)
PFLASH  pflash_write            offset 001f0050 00007070 2
PFLASH  pflash_write            Read Status
PFLASH  pflash_io_mode          switch to i/o mode
PFLASH  pflash_write            offset 001f0050 00005050 2
PFLASH  pflash_write            Clear Status Register (1f0050 50)
PFLASH  pflash_read             offset 001f0050
PFLASH  pflash_read             status 0
PFLASH  pflash_read             offset 001f0050 00000000 4

flash_program error at B01F0050


*/

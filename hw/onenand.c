/*
 * OneNAND flash memories emulation.
 *
 * Copyright (C) 2008 Nokia Corporation
 * Written by Andrzej Zaborowski <andrew@openedhand.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu-common.h"
#include "hw.h"
#include "flash.h"
#include "irq.h"
#include "blockdev.h"

/* 11 for 2kB-page OneNAND ("2nd generation") and 10 for 1kB-page chips */
#define PAGE_SHIFT	11

/* Fixed */
#define BLOCK_SHIFT	(PAGE_SHIFT + 6)

typedef struct {
    uint32_t id;
    int shift;
    target_phys_addr_t base;
    qemu_irq intr;
    qemu_irq rdy;
    BlockDriverState *bdrv;
    BlockDriverState *bdrv_cur;
    uint8_t *image;
    uint8_t *otp;
    uint8_t *current;
    ram_addr_t ram;
    uint8_t *boot[2];
    uint8_t *data[2][2];
    int iomemtype;
    int cycle;
    int otpmode;

    uint16_t addr[8];
    uint16_t unladdr[8];
    int bufaddr;
    int count;
    uint16_t command;
    uint16_t config[2];
    uint16_t status;
    uint16_t intstatus;
    uint16_t wpstatus;

    ECCState ecc;

    int density_mask;
    int secs;
    int secs_cur;
    int blocks;
    uint8_t *blockwp;
} OneNANDState;

enum {
    ONEN_BUF_BLOCK = 0,
    ONEN_BUF_BLOCK2 = 1,
    ONEN_BUF_DEST_BLOCK = 2,
    ONEN_BUF_DEST_PAGE = 3,
    ONEN_BUF_PAGE = 7,
};

enum {
    ONEN_ERR_CMD = 1 << 10,
    ONEN_ERR_ERASE = 1 << 11,
    ONEN_ERR_PROG = 1 << 12,
    ONEN_ERR_LOAD = 1 << 13,
};

enum {
    ONEN_INT_RESET = 1 << 4,
    ONEN_INT_ERASE = 1 << 5,
    ONEN_INT_PROG = 1 << 6,
    ONEN_INT_LOAD = 1 << 7,
    ONEN_INT = 1 << 15,
};

enum {
    ONEN_LOCK_LOCKTIGHTEN = 1 << 0,
    ONEN_LOCK_LOCKED = 1 << 1,
    ONEN_LOCK_UNLOCKED = 1 << 2,
};

void onenand_base_update(void *opaque, target_phys_addr_t new)
{
    OneNANDState *s = (OneNANDState *) opaque;

    s->base = new;

    /* XXX: We should use IO_MEM_ROMD but we broke it earlier...
     * Both 0x0000 ... 0x01ff and 0x8000 ... 0x800f can be used to
     * write boot commands.  Also take note of the BWPS bit.  */
    cpu_register_physical_memory(s->base + (0x0000 << s->shift),
                    0x0200 << s->shift, s->iomemtype);
    cpu_register_physical_memory(s->base + (0x0200 << s->shift),
                    0xbe00 << s->shift,
                    (s->ram +(0x0200 << s->shift)) | IO_MEM_RAM);
    if (s->iomemtype)
        cpu_register_physical_memory_offset(s->base + (0xc000 << s->shift),
                    0x4000 << s->shift, s->iomemtype, (0xc000 << s->shift));
}

void onenand_base_unmap(void *opaque)
{
    OneNANDState *s = (OneNANDState *) opaque;

    cpu_register_physical_memory(s->base,
                    0x10000 << s->shift, IO_MEM_UNASSIGNED);
}

static void onenand_intr_update(OneNANDState *s)
{
    qemu_set_irq(s->intr, ((s->intstatus >> 15) ^ (~s->config[0] >> 6)) & 1);
}

/* Hot reset (Reset OneNAND command) or warm reset (RP pin low) */
static void onenand_reset(OneNANDState *s, int cold)
{
    memset(&s->addr, 0, sizeof(s->addr));
    s->command = 0;
    s->count = 1;
    s->bufaddr = 0;
    s->config[0] = 0x40c0;
    s->config[1] = 0x0000;
    onenand_intr_update(s);
    qemu_irq_raise(s->rdy);
    s->status = 0x0000;
    s->intstatus = cold ? 0x8080 : 0x8010;
    s->unladdr[0] = 0;
    s->unladdr[1] = 0;
    s->wpstatus = 0x0002;
    s->cycle = 0;
    s->otpmode = 0;
    s->bdrv_cur = s->bdrv;
    s->current = s->image;
    s->secs_cur = s->secs;

    if (cold) {
        /* Lock the whole flash */
        memset(s->blockwp, ONEN_LOCK_LOCKED, s->blocks);

        if (s->bdrv && bdrv_read(s->bdrv, 0, s->boot[0], 8) < 0)
            hw_error("%s: Loading the BootRAM failed.\n", __FUNCTION__);
    }
}

static inline int onenand_load_main(OneNANDState *s, int sec, int secn,
                void *dest)
{
    if (s->bdrv_cur)
        return bdrv_read(s->bdrv_cur, sec, dest, secn) < 0;
    else if (sec + secn > s->secs_cur)
        return 1;

    memcpy(dest, s->current + (sec << 9), secn << 9);

    return 0;
}

static inline int onenand_prog_main(OneNANDState *s, int sec, int secn,
                void *src)
{
    if (s->bdrv_cur)
        return bdrv_write(s->bdrv_cur, sec, src, secn) < 0;
    else if (sec + secn > s->secs_cur)
        return 1;

    memcpy(s->current + (sec << 9), src, secn << 9);

    return 0;
}

static inline int onenand_load_spare(OneNANDState *s, int sec, int secn,
                void *dest)
{
    uint8_t buf[512];

    if (s->bdrv_cur) {
        if (bdrv_read(s->bdrv_cur, s->secs_cur + (sec >> 5), buf, 1) < 0)
            return 1;
        memcpy(dest, buf + ((sec & 31) << 4), secn << 4);
    } else if (sec + secn > s->secs_cur)
        return 1;
    else
        memcpy(dest, s->current + (s->secs_cur << 9) + (sec << 4), secn << 4);
 
    return 0;
}

static inline int onenand_prog_spare(OneNANDState *s, int sec, int secn,
                void *src)
{
    uint8_t buf[512];

    if (s->bdrv_cur) {
        if (bdrv_read(s->bdrv_cur, s->secs_cur + (sec >> 5), buf, 1) < 0)
            return 1;
        memcpy(buf + ((sec & 31) << 4), src, secn << 4);
        return bdrv_write(s->bdrv_cur, s->secs_cur + (sec >> 5), buf, 1) < 0;
    } else if (sec + secn > s->secs_cur)
        return 1;

    memcpy(s->current + (s->secs_cur << 9) + (sec << 4), src, secn << 4);
 
    return 0;
}

static inline int onenand_erase(OneNANDState *s, int sec, int num)
{
    /* TODO: optimise */
    uint8_t buf[512];

    memset(buf, 0xff, sizeof(buf));
    for (; num > 0; num --, sec ++) {
        if (onenand_prog_main(s, sec, 1, buf))
            return 1;
        if (onenand_prog_spare(s, sec, 1, buf))
            return 1;
    }

    return 0;
}

static void onenand_command(OneNANDState *s, int cmd)
{
    int b;
    int sec;
    void *buf;
#define SETADDR(block, page)			\
    sec = (s->addr[page] & 3) +			\
            ((((s->addr[page] >> 2) & 0x3f) +	\
              (((s->addr[block] & 0xfff) |	\
                (s->addr[block] >> 15 ?		\
                 s->density_mask : 0)) << 6)) << (PAGE_SHIFT - 9));
#define SETBUF_M()				\
    buf = (s->bufaddr & 8) ?			\
            s->data[(s->bufaddr >> 2) & 1][0] : s->boot[0];	\
    buf += (s->bufaddr & 3) << 9;
#define SETBUF_S()				\
    buf = (s->bufaddr & 8) ?			\
            s->data[(s->bufaddr >> 2) & 1][1] : s->boot[1];	\
    buf += (s->bufaddr & 3) << 4;

    switch (cmd) {
    case 0x00:	/* Load single/multiple sector data unit into buffer */
        SETADDR(ONEN_BUF_BLOCK, ONEN_BUF_PAGE)

        SETBUF_M()
        if (onenand_load_main(s, sec, s->count, buf))
            s->status |= ONEN_ERR_CMD | ONEN_ERR_LOAD;

#if 0
        SETBUF_S()
        if (onenand_load_spare(s, sec, s->count, buf))
            s->status |= ONEN_ERR_CMD | ONEN_ERR_LOAD;
#endif

        /* TODO: if (s->bufaddr & 3) + s->count was > 4 (2k-pages)
         * or    if (s->bufaddr & 1) + s->count was > 2 (1k-pages)
         * then we need two split the read/write into two chunks.
         */
        s->intstatus |= ONEN_INT | ONEN_INT_LOAD;
        break;
    case 0x13:	/* Load single/multiple spare sector into buffer */
        SETADDR(ONEN_BUF_BLOCK, ONEN_BUF_PAGE)

        SETBUF_S()
        if (onenand_load_spare(s, sec, s->count, buf))
            s->status |= ONEN_ERR_CMD | ONEN_ERR_LOAD;

        /* TODO: if (s->bufaddr & 3) + s->count was > 4 (2k-pages)
         * or    if (s->bufaddr & 1) + s->count was > 2 (1k-pages)
         * then we need two split the read/write into two chunks.
         */
        s->intstatus |= ONEN_INT | ONEN_INT_LOAD;
        break;
    case 0x80:	/* Program single/multiple sector data unit from buffer */
        SETADDR(ONEN_BUF_BLOCK, ONEN_BUF_PAGE)

        SETBUF_M()
        if (onenand_prog_main(s, sec, s->count, buf))
            s->status |= ONEN_ERR_CMD | ONEN_ERR_PROG;

#if 0
        SETBUF_S()
        if (onenand_prog_spare(s, sec, s->count, buf))
            s->status |= ONEN_ERR_CMD | ONEN_ERR_PROG;
#endif

        /* TODO: if (s->bufaddr & 3) + s->count was > 4 (2k-pages)
         * or    if (s->bufaddr & 1) + s->count was > 2 (1k-pages)
         * then we need two split the read/write into two chunks.
         */
        s->intstatus |= ONEN_INT | ONEN_INT_PROG;
        break;
    case 0x1a:	/* Program single/multiple spare area sector from buffer */
        SETADDR(ONEN_BUF_BLOCK, ONEN_BUF_PAGE)

        SETBUF_S()
        if (onenand_prog_spare(s, sec, s->count, buf))
            s->status |= ONEN_ERR_CMD | ONEN_ERR_PROG;

        /* TODO: if (s->bufaddr & 3) + s->count was > 4 (2k-pages)
         * or    if (s->bufaddr & 1) + s->count was > 2 (1k-pages)
         * then we need two split the read/write into two chunks.
         */
        s->intstatus |= ONEN_INT | ONEN_INT_PROG;
        break;
    case 0x1b:	/* Copy-back program */
        SETBUF_S()

        SETADDR(ONEN_BUF_BLOCK, ONEN_BUF_PAGE)
        if (onenand_load_main(s, sec, s->count, buf))
            s->status |= ONEN_ERR_CMD | ONEN_ERR_PROG;

        SETADDR(ONEN_BUF_DEST_BLOCK, ONEN_BUF_DEST_PAGE)
        if (onenand_prog_main(s, sec, s->count, buf))
            s->status |= ONEN_ERR_CMD | ONEN_ERR_PROG;

        /* TODO: spare areas */

        s->intstatus |= ONEN_INT | ONEN_INT_PROG;
        break;

    case 0x23:	/* Unlock NAND array block(s) */
        s->intstatus |= ONEN_INT;

        /* XXX the previous (?) area should be locked automatically */
        for (b = s->unladdr[0]; b <= s->unladdr[1]; b ++) {
            if (b >= s->blocks) {
                s->status |= ONEN_ERR_CMD;
                break;
            }
            if (s->blockwp[b] == ONEN_LOCK_LOCKTIGHTEN)
                break;

            s->wpstatus = s->blockwp[b] = ONEN_LOCK_UNLOCKED;
        }
        break;
    case 0x27:	/* Unlock All NAND array blocks */
        s->intstatus |= ONEN_INT;

        for (b = 0; b < s->blocks; b ++) {
            if (b >= s->blocks) {
                s->status |= ONEN_ERR_CMD;
                break;
            }
            if (s->blockwp[b] == ONEN_LOCK_LOCKTIGHTEN)
                break;

            s->wpstatus = s->blockwp[b] = ONEN_LOCK_UNLOCKED;
        }
        break;

    case 0x2a:	/* Lock NAND array block(s) */
        s->intstatus |= ONEN_INT;

        for (b = s->unladdr[0]; b <= s->unladdr[1]; b ++) {
            if (b >= s->blocks) {
                s->status |= ONEN_ERR_CMD;
                break;
            }
            if (s->blockwp[b] == ONEN_LOCK_LOCKTIGHTEN)
                break;

            s->wpstatus = s->blockwp[b] = ONEN_LOCK_LOCKED;
        }
        break;
    case 0x2c:	/* Lock-tight NAND array block(s) */
        s->intstatus |= ONEN_INT;

        for (b = s->unladdr[0]; b <= s->unladdr[1]; b ++) {
            if (b >= s->blocks) {
                s->status |= ONEN_ERR_CMD;
                break;
            }
            if (s->blockwp[b] == ONEN_LOCK_UNLOCKED)
                continue;

            s->wpstatus = s->blockwp[b] = ONEN_LOCK_LOCKTIGHTEN;
        }
        break;

    case 0x71:	/* Erase-Verify-Read */
        s->intstatus |= ONEN_INT;
        break;
    case 0x95:	/* Multi-block erase */
        qemu_irq_pulse(s->intr);
        /* Fall through.  */
    case 0x94:	/* Block erase */
        sec = ((s->addr[ONEN_BUF_BLOCK] & 0xfff) |
                        (s->addr[ONEN_BUF_BLOCK] >> 15 ? s->density_mask : 0))
                << (BLOCK_SHIFT - 9);
        if (onenand_erase(s, sec, 1 << (BLOCK_SHIFT - 9)))
            s->status |= ONEN_ERR_CMD | ONEN_ERR_ERASE;

        s->intstatus |= ONEN_INT | ONEN_INT_ERASE;
        break;
    case 0xb0:	/* Erase suspend */
        break;
    case 0x30:	/* Erase resume */
        s->intstatus |= ONEN_INT | ONEN_INT_ERASE;
        break;

    case 0xf0:	/* Reset NAND Flash core */
        onenand_reset(s, 0);
        break;
    case 0xf3:	/* Reset OneNAND */
        onenand_reset(s, 0);
        break;

    case 0x65:	/* OTP Access */
        s->intstatus |= ONEN_INT;
        s->bdrv_cur = NULL;
        s->current = s->otp;
        s->secs_cur = 1 << (BLOCK_SHIFT - 9);
        s->addr[ONEN_BUF_BLOCK] = 0;
        s->otpmode = 1;
        break;

    default:
        s->status |= ONEN_ERR_CMD;
        s->intstatus |= ONEN_INT;
        fprintf(stderr, "%s: unknown OneNAND command %x\n",
                        __FUNCTION__, cmd);
    }

    onenand_intr_update(s);
}

static uint32_t onenand_read(void *opaque, target_phys_addr_t addr)
{
    OneNANDState *s = (OneNANDState *) opaque;
    int offset = addr >> s->shift;

    switch (offset) {
    case 0x0000 ... 0xc000:
        return lduw_le_p(s->boot[0] + addr);

    case 0xf000:	/* Manufacturer ID */
        return (s->id >> 16) & 0xff;
    case 0xf001:	/* Device ID */
        return (s->id >>  8) & 0xff;
    /* TODO: get the following values from a real chip!  */
    case 0xf002:	/* Version ID */
        return (s->id >>  0) & 0xff;
    case 0xf003:	/* Data Buffer size */
        return 1 << PAGE_SHIFT;
    case 0xf004:	/* Boot Buffer size */
        return 0x200;
    case 0xf005:	/* Amount of buffers */
        return 1 | (2 << 8);
    case 0xf006:	/* Technology */
        return 0;

    case 0xf100 ... 0xf107:	/* Start addresses */
        return s->addr[offset - 0xf100];

    case 0xf200:	/* Start buffer */
        return (s->bufaddr << 8) | ((s->count - 1) & (1 << (PAGE_SHIFT - 10)));

    case 0xf220:	/* Command */
        return s->command;
    case 0xf221:	/* System Configuration 1 */
        return s->config[0] & 0xffe0;
    case 0xf222:	/* System Configuration 2 */
        return s->config[1];

    case 0xf240:	/* Controller Status */
        return s->status;
    case 0xf241:	/* Interrupt */
        return s->intstatus;
    case 0xf24c:	/* Unlock Start Block Address */
        return s->unladdr[0];
    case 0xf24d:	/* Unlock End Block Address */
        return s->unladdr[1];
    case 0xf24e:	/* Write Protection Status */
        return s->wpstatus;

    case 0xff00:	/* ECC Status */
        return 0x00;
    case 0xff01:	/* ECC Result of main area data */
    case 0xff02:	/* ECC Result of spare area data */
    case 0xff03:	/* ECC Result of main area data */
    case 0xff04:	/* ECC Result of spare area data */
        hw_error("%s: imeplement ECC\n", __FUNCTION__);
        return 0x0000;
    }

    fprintf(stderr, "%s: unknown OneNAND register %x\n",
                    __FUNCTION__, offset);
    return 0;
}

static void onenand_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    OneNANDState *s = (OneNANDState *) opaque;
    int offset = addr >> s->shift;
    int sec;

    switch (offset) {
    case 0x0000 ... 0x01ff:
    case 0x8000 ... 0x800f:
        if (s->cycle) {
            s->cycle = 0;

            if (value == 0x0000) {
                SETADDR(ONEN_BUF_BLOCK, ONEN_BUF_PAGE)
                onenand_load_main(s, sec,
                                1 << (PAGE_SHIFT - 9), s->data[0][0]);
                s->addr[ONEN_BUF_PAGE] += 4;
                s->addr[ONEN_BUF_PAGE] &= 0xff;
            }
            break;
        }

        switch (value) {
        case 0x00f0:	/* Reset OneNAND */
            onenand_reset(s, 0);
            break;

        case 0x00e0:	/* Load Data into Buffer */
            s->cycle = 1;
            break;

        case 0x0090:	/* Read Identification Data */
            memset(s->boot[0], 0, 3 << s->shift);
            s->boot[0][0 << s->shift] = (s->id >> 16) & 0xff;
            s->boot[0][1 << s->shift] = (s->id >>  8) & 0xff;
            s->boot[0][2 << s->shift] = s->wpstatus & 0xff;
            break;

        default:
            fprintf(stderr, "%s: unknown OneNAND boot command %x\n",
                            __FUNCTION__, value);
        }
        break;

    case 0xf100 ... 0xf107:	/* Start addresses */
        s->addr[offset - 0xf100] = value;
        break;

    case 0xf200:	/* Start buffer */
        s->bufaddr = (value >> 8) & 0xf;
        if (PAGE_SHIFT == 11)
            s->count = (value & 3) ?: 4;
        else if (PAGE_SHIFT == 10)
            s->count = (value & 1) ?: 2;
        break;

    case 0xf220:	/* Command */
        if (s->intstatus & (1 << 15))
            break;
        s->command = value;
        onenand_command(s, s->command);
        break;
    case 0xf221:	/* System Configuration 1 */
        s->config[0] = value;
        onenand_intr_update(s);
        qemu_set_irq(s->rdy, (s->config[0] >> 7) & 1);
        break;
    case 0xf222:	/* System Configuration 2 */
        s->config[1] = value;
        break;

    case 0xf241:	/* Interrupt */
        s->intstatus &= value;
        if ((1 << 15) & ~s->intstatus)
            s->status &= ~(ONEN_ERR_CMD | ONEN_ERR_ERASE |
                            ONEN_ERR_PROG | ONEN_ERR_LOAD);
        onenand_intr_update(s);
        break;
    case 0xf24c:	/* Unlock Start Block Address */
        s->unladdr[0] = value & (s->blocks - 1);
        /* For some reason we have to set the end address to by default
         * be same as start because the software forgets to write anything
         * in there.  */
        s->unladdr[1] = value & (s->blocks - 1);
        break;
    case 0xf24d:	/* Unlock End Block Address */
        s->unladdr[1] = value & (s->blocks - 1);
        break;

    default:
        fprintf(stderr, "%s: unknown OneNAND register %x\n",
                        __FUNCTION__, offset);
    }
}

static CPUReadMemoryFunc * const onenand_readfn[] = {
    onenand_read,	/* TODO */
    onenand_read,
    onenand_read,
};

static CPUWriteMemoryFunc * const onenand_writefn[] = {
    onenand_write,	/* TODO */
    onenand_write,
    onenand_write,
};

void *onenand_init(uint32_t id, int regshift, qemu_irq irq)
{
    OneNANDState *s = (OneNANDState *) qemu_mallocz(sizeof(*s));
    DriveInfo *dinfo = drive_get(IF_MTD, 0, 0);
    uint32_t size = 1 << (24 + ((id >> 12) & 7));
    void *ram;

    s->shift = regshift;
    s->intr = irq;
    s->rdy = NULL;
    s->id = id;
    s->blocks = size >> BLOCK_SHIFT;
    s->secs = size >> 9;
    s->blockwp = qemu_malloc(s->blocks);
    s->density_mask = (id & (1 << 11)) ? (1 << (6 + ((id >> 12) & 7))) : 0;
    s->iomemtype = cpu_register_io_memory(onenand_readfn,
                    onenand_writefn, s, DEVICE_NATIVE_ENDIAN);
    if (!dinfo)
        s->image = memset(qemu_malloc(size + (size >> 5)),
                        0xff, size + (size >> 5));
    else
        s->bdrv = dinfo->bdrv;
    s->otp = memset(qemu_malloc((64 + 2) << PAGE_SHIFT),
                    0xff, (64 + 2) << PAGE_SHIFT);
    s->ram = qemu_ram_alloc(NULL, "onenand.ram", 0xc000 << s->shift);
    ram = qemu_get_ram_ptr(s->ram);
    s->boot[0] = ram + (0x0000 << s->shift);
    s->boot[1] = ram + (0x8000 << s->shift);
    s->data[0][0] = ram + ((0x0200 + (0 << (PAGE_SHIFT - 1))) << s->shift);
    s->data[0][1] = ram + ((0x8010 + (0 << (PAGE_SHIFT - 6))) << s->shift);
    s->data[1][0] = ram + ((0x0200 + (1 << (PAGE_SHIFT - 1))) << s->shift);
    s->data[1][1] = ram + ((0x8010 + (1 << (PAGE_SHIFT - 6))) << s->shift);

    onenand_reset(s, 1);

    return s;
}

void *onenand_raw_otp(void *opaque)
{
    OneNANDState *s = (OneNANDState *) opaque;

    return s->otp;
}

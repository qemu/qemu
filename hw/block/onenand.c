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
#include "hw/hw.h"
#include "hw/block/flash.h"
#include "hw/irq.h"
#include "sysemu/blockdev.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "hw/sysbus.h"
#include "qemu/error-report.h"

/* 11 for 2kB-page OneNAND ("2nd generation") and 10 for 1kB-page chips */
#define PAGE_SHIFT	11

/* Fixed */
#define BLOCK_SHIFT	(PAGE_SHIFT + 6)

#define TYPE_ONE_NAND "onenand"
#define ONE_NAND(obj) OBJECT_CHECK(OneNANDState, (obj), TYPE_ONE_NAND)

typedef struct OneNANDState {
    SysBusDevice parent_obj;

    struct {
        uint16_t man;
        uint16_t dev;
        uint16_t ver;
    } id;
    int shift;
    hwaddr base;
    qemu_irq intr;
    qemu_irq rdy;
    BlockDriverState *bdrv;
    BlockDriverState *bdrv_cur;
    uint8_t *image;
    uint8_t *otp;
    uint8_t *current;
    MemoryRegion ram;
    MemoryRegion mapped_ram;
    uint8_t current_direction;
    uint8_t *boot[2];
    uint8_t *data[2][2];
    MemoryRegion iomem;
    MemoryRegion container;
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

static void onenand_mem_setup(OneNANDState *s)
{
    /* XXX: We should use IO_MEM_ROMD but we broke it earlier...
     * Both 0x0000 ... 0x01ff and 0x8000 ... 0x800f can be used to
     * write boot commands.  Also take note of the BWPS bit.  */
    memory_region_init(&s->container, OBJECT(s), "onenand",
                       0x10000 << s->shift);
    memory_region_add_subregion(&s->container, 0, &s->iomem);
    memory_region_init_alias(&s->mapped_ram, OBJECT(s), "onenand-mapped-ram",
                             &s->ram, 0x0200 << s->shift,
                             0xbe00 << s->shift);
    memory_region_add_subregion_overlap(&s->container,
                                        0x0200 << s->shift,
                                        &s->mapped_ram,
                                        1);
}

static void onenand_intr_update(OneNANDState *s)
{
    qemu_set_irq(s->intr, ((s->intstatus >> 15) ^ (~s->config[0] >> 6)) & 1);
}

static void onenand_pre_save(void *opaque)
{
    OneNANDState *s = opaque;
    if (s->current == s->otp) {
        s->current_direction = 1;
    } else if (s->current == s->image) {
        s->current_direction = 2;
    } else {
        s->current_direction = 0;
    }
}

static int onenand_post_load(void *opaque, int version_id)
{
    OneNANDState *s = opaque;
    switch (s->current_direction) {
    case 0:
        break;
    case 1:
        s->current = s->otp;
        break;
    case 2:
        s->current = s->image;
        break;
    default:
        return -1;
    }
    onenand_intr_update(s);
    return 0;
}

static const VMStateDescription vmstate_onenand = {
    .name = "onenand",
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = onenand_pre_save,
    .post_load = onenand_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(current_direction, OneNANDState),
        VMSTATE_INT32(cycle, OneNANDState),
        VMSTATE_INT32(otpmode, OneNANDState),
        VMSTATE_UINT16_ARRAY(addr, OneNANDState, 8),
        VMSTATE_UINT16_ARRAY(unladdr, OneNANDState, 8),
        VMSTATE_INT32(bufaddr, OneNANDState),
        VMSTATE_INT32(count, OneNANDState),
        VMSTATE_UINT16(command, OneNANDState),
        VMSTATE_UINT16_ARRAY(config, OneNANDState, 2),
        VMSTATE_UINT16(status, OneNANDState),
        VMSTATE_UINT16(intstatus, OneNANDState),
        VMSTATE_UINT16(wpstatus, OneNANDState),
        VMSTATE_INT32(secs_cur, OneNANDState),
        VMSTATE_PARTIAL_VBUFFER(blockwp, OneNANDState, blocks),
        VMSTATE_UINT8(ecc.cp, OneNANDState),
        VMSTATE_UINT16_ARRAY(ecc.lp, OneNANDState, 2),
        VMSTATE_UINT16(ecc.count, OneNANDState),
        VMSTATE_BUFFER_POINTER_UNSAFE(otp, OneNANDState, 0,
            ((64 + 2) << PAGE_SHIFT)),
        VMSTATE_END_OF_LIST()
    }
};

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

        if (s->bdrv_cur && bdrv_read(s->bdrv_cur, 0, s->boot[0], 8) < 0) {
            hw_error("%s: Loading the BootRAM failed.\n", __func__);
        }
    }
}

static void onenand_system_reset(DeviceState *dev)
{
    OneNANDState *s = ONE_NAND(dev);

    onenand_reset(s, 1);
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
    int result = 0;

    if (secn > 0) {
        uint32_t size = (uint32_t)secn * 512;
        const uint8_t *sp = (const uint8_t *)src;
        uint8_t *dp = 0;
        if (s->bdrv_cur) {
            dp = g_malloc(size);
            if (!dp || bdrv_read(s->bdrv_cur, sec, dp, secn) < 0) {
                result = 1;
            }
        } else {
            if (sec + secn > s->secs_cur) {
                result = 1;
            } else {
                dp = (uint8_t *)s->current + (sec << 9);
            }
        }
        if (!result) {
            uint32_t i;
            for (i = 0; i < size; i++) {
                dp[i] &= sp[i];
            }
            if (s->bdrv_cur) {
                result = bdrv_write(s->bdrv_cur, sec, dp, secn) < 0;
            }
        }
        if (dp && s->bdrv_cur) {
            g_free(dp);
        }
    }

    return result;
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
    int result = 0;
    if (secn > 0) {
        const uint8_t *sp = (const uint8_t *)src;
        uint8_t *dp = 0, *dpp = 0;
        if (s->bdrv_cur) {
            dp = g_malloc(512);
            if (!dp || bdrv_read(s->bdrv_cur,
                                 s->secs_cur + (sec >> 5),
                                 dp, 1) < 0) {
                result = 1;
            } else {
                dpp = dp + ((sec & 31) << 4);
            }
        } else {
            if (sec + secn > s->secs_cur) {
                result = 1;
            } else {
                dpp = s->current + (s->secs_cur << 9) + (sec << 4);
            }
        }
        if (!result) {
            uint32_t i;
            for (i = 0; i < (secn << 4); i++) {
                dpp[i] &= sp[i];
            }
            if (s->bdrv_cur) {
                result = bdrv_write(s->bdrv_cur, s->secs_cur + (sec >> 5),
                                    dp, 1) < 0;
            }
        }
        g_free(dp);
    }
    return result;
}

static inline int onenand_erase(OneNANDState *s, int sec, int num)
{
    uint8_t *blankbuf, *tmpbuf;
    blankbuf = g_malloc(512);
    if (!blankbuf) {
        return 1;
    }
    tmpbuf = g_malloc(512);
    if (!tmpbuf) {
        g_free(blankbuf);
        return 1;
    }
    memset(blankbuf, 0xff, 512);
    for (; num > 0; num--, sec++) {
        if (s->bdrv_cur) {
            int erasesec = s->secs_cur + (sec >> 5);
            if (bdrv_write(s->bdrv_cur, sec, blankbuf, 1) < 0) {
                goto fail;
            }
            if (bdrv_read(s->bdrv_cur, erasesec, tmpbuf, 1) < 0) {
                goto fail;
            }
            memcpy(tmpbuf + ((sec & 31) << 4), blankbuf, 1 << 4);
            if (bdrv_write(s->bdrv_cur, erasesec, tmpbuf, 1) < 0) {
                goto fail;
            }
        } else {
            if (sec + 1 > s->secs_cur) {
                goto fail;
            }
            memcpy(s->current + (sec << 9), blankbuf, 512);
            memcpy(s->current + (s->secs_cur << 9) + (sec << 4),
                   blankbuf, 1 << 4);
        }
    }

    g_free(tmpbuf);
    g_free(blankbuf);
    return 0;

fail:
    g_free(tmpbuf);
    g_free(blankbuf);
    return 1;
}

static void onenand_command(OneNANDState *s)
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

    switch (s->command) {
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
                        __func__, s->command);
    }

    onenand_intr_update(s);
}

static uint64_t onenand_read(void *opaque, hwaddr addr,
                             unsigned size)
{
    OneNANDState *s = (OneNANDState *) opaque;
    int offset = addr >> s->shift;

    switch (offset) {
    case 0x0000 ... 0xc000:
        return lduw_le_p(s->boot[0] + addr);

    case 0xf000:	/* Manufacturer ID */
        return s->id.man;
    case 0xf001:	/* Device ID */
        return s->id.dev;
    case 0xf002:	/* Version ID */
        return s->id.ver;
    /* TODO: get the following values from a real chip!  */
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

static void onenand_write(void *opaque, hwaddr addr,
                          uint64_t value, unsigned size)
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
            s->boot[0][0 << s->shift] = s->id.man & 0xff;
            s->boot[0][1 << s->shift] = s->id.dev & 0xff;
            s->boot[0][2 << s->shift] = s->wpstatus & 0xff;
            break;

        default:
            fprintf(stderr, "%s: unknown OneNAND boot command %"PRIx64"\n",
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
        onenand_command(s);
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

static const MemoryRegionOps onenand_ops = {
    .read = onenand_read,
    .write = onenand_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int onenand_initfn(SysBusDevice *sbd)
{
    DeviceState *dev = DEVICE(sbd);
    OneNANDState *s = ONE_NAND(dev);
    uint32_t size = 1 << (24 + ((s->id.dev >> 4) & 7));
    void *ram;

    s->base = (hwaddr)-1;
    s->rdy = NULL;
    s->blocks = size >> BLOCK_SHIFT;
    s->secs = size >> 9;
    s->blockwp = g_malloc(s->blocks);
    s->density_mask = (s->id.dev & 0x08)
        ? (1 << (6 + ((s->id.dev >> 4) & 7))) : 0;
    memory_region_init_io(&s->iomem, OBJECT(s), &onenand_ops, s, "onenand",
                          0x10000 << s->shift);
    if (!s->bdrv) {
        s->image = memset(g_malloc(size + (size >> 5)),
                          0xff, size + (size >> 5));
    } else {
        if (bdrv_is_read_only(s->bdrv)) {
            error_report("Can't use a read-only drive");
            return -1;
        }
        s->bdrv_cur = s->bdrv;
    }
    s->otp = memset(g_malloc((64 + 2) << PAGE_SHIFT),
                    0xff, (64 + 2) << PAGE_SHIFT);
    memory_region_init_ram(&s->ram, OBJECT(s), "onenand.ram",
                           0xc000 << s->shift);
    vmstate_register_ram_global(&s->ram);
    ram = memory_region_get_ram_ptr(&s->ram);
    s->boot[0] = ram + (0x0000 << s->shift);
    s->boot[1] = ram + (0x8000 << s->shift);
    s->data[0][0] = ram + ((0x0200 + (0 << (PAGE_SHIFT - 1))) << s->shift);
    s->data[0][1] = ram + ((0x8010 + (0 << (PAGE_SHIFT - 6))) << s->shift);
    s->data[1][0] = ram + ((0x0200 + (1 << (PAGE_SHIFT - 1))) << s->shift);
    s->data[1][1] = ram + ((0x8010 + (1 << (PAGE_SHIFT - 6))) << s->shift);
    onenand_mem_setup(s);
    sysbus_init_irq(sbd, &s->intr);
    sysbus_init_mmio(sbd, &s->container);
    vmstate_register(dev,
                     ((s->shift & 0x7f) << 24)
                     | ((s->id.man & 0xff) << 16)
                     | ((s->id.dev & 0xff) << 8)
                     | (s->id.ver & 0xff),
                     &vmstate_onenand, s);
    return 0;
}

static Property onenand_properties[] = {
    DEFINE_PROP_UINT16("manufacturer_id", OneNANDState, id.man, 0),
    DEFINE_PROP_UINT16("device_id", OneNANDState, id.dev, 0),
    DEFINE_PROP_UINT16("version_id", OneNANDState, id.ver, 0),
    DEFINE_PROP_INT32("shift", OneNANDState, shift, 0),
    DEFINE_PROP_DRIVE("drive", OneNANDState, bdrv),
    DEFINE_PROP_END_OF_LIST(),
};

static void onenand_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = onenand_initfn;
    dc->reset = onenand_system_reset;
    dc->props = onenand_properties;
}

static const TypeInfo onenand_info = {
    .name          = TYPE_ONE_NAND,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(OneNANDState),
    .class_init    = onenand_class_init,
};

static void onenand_register_types(void)
{
    type_register_static(&onenand_info);
}

void *onenand_raw_otp(DeviceState *onenand_device)
{
    OneNANDState *s = ONE_NAND(onenand_device);

    return s->otp;
}

type_init(onenand_register_types)

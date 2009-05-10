/*
 * Flash NAND memory emulation.  Based on "16M x 8 Bit NAND Flash
 * Memory" datasheet for the KM29U128AT / K9F2808U0A chips from
 * Samsung Electronic.
 *
 * Copyright (c) 2006 Openedhand Ltd.
 * Written by Andrzej Zaborowski <balrog@zabor.org>
 *
 * This code is licensed under the GNU GPL v2.
 */

#ifndef NAND_IO

# include "hw.h"
# include "flash.h"
# include "block.h"
/* FIXME: Pass block device as an argument.  */
# include "sysemu.h"

# define NAND_CMD_READ0		0x00
# define NAND_CMD_READ1		0x01
# define NAND_CMD_READ2		0x50
# define NAND_CMD_LPREAD2	0x30
# define NAND_CMD_NOSERIALREAD2	0x35
# define NAND_CMD_RANDOMREAD1	0x05
# define NAND_CMD_RANDOMREAD2	0xe0
# define NAND_CMD_READID	0x90
# define NAND_CMD_RESET		0xff
# define NAND_CMD_PAGEPROGRAM1	0x80
# define NAND_CMD_PAGEPROGRAM2	0x10
# define NAND_CMD_CACHEPROGRAM2	0x15
# define NAND_CMD_BLOCKERASE1	0x60
# define NAND_CMD_BLOCKERASE2	0xd0
# define NAND_CMD_READSTATUS	0x70
# define NAND_CMD_COPYBACKPRG1	0x85

# define NAND_IOSTATUS_ERROR	(1 << 0)
# define NAND_IOSTATUS_PLANE0	(1 << 1)
# define NAND_IOSTATUS_PLANE1	(1 << 2)
# define NAND_IOSTATUS_PLANE2	(1 << 3)
# define NAND_IOSTATUS_PLANE3	(1 << 4)
# define NAND_IOSTATUS_BUSY	(1 << 6)
# define NAND_IOSTATUS_UNPROTCT	(1 << 7)

# define MAX_PAGE		0x800
# define MAX_OOB		0x40

struct NANDFlashState {
    uint8_t manf_id, chip_id;
    int size, pages;
    int page_shift, oob_shift, erase_shift, addr_shift;
    uint8_t *storage;
    BlockDriverState *bdrv;
    int mem_oob;

    int cle, ale, ce, wp, gnd;

    uint8_t io[MAX_PAGE + MAX_OOB + 0x400];
    uint8_t *ioaddr;
    int iolen;

    uint32_t cmd, addr;
    int addrlen;
    int status;
    int offset;

    void (*blk_write)(NANDFlashState *s);
    void (*blk_erase)(NANDFlashState *s);
    void (*blk_load)(NANDFlashState *s, uint32_t addr, int offset);
};

# define NAND_NO_AUTOINCR	0x00000001
# define NAND_BUSWIDTH_16	0x00000002
# define NAND_NO_PADDING	0x00000004
# define NAND_CACHEPRG		0x00000008
# define NAND_COPYBACK		0x00000010
# define NAND_IS_AND		0x00000020
# define NAND_4PAGE_ARRAY	0x00000040
# define NAND_NO_READRDY	0x00000100
# define NAND_SAMSUNG_LP	(NAND_NO_PADDING | NAND_COPYBACK)

# define NAND_IO

# define PAGE(addr)		((addr) >> ADDR_SHIFT)
# define PAGE_START(page)	(PAGE(page) * (PAGE_SIZE + OOB_SIZE))
# define PAGE_MASK		((1 << ADDR_SHIFT) - 1)
# define OOB_SHIFT		(PAGE_SHIFT - 5)
# define OOB_SIZE		(1 << OOB_SHIFT)
# define SECTOR(addr)		((addr) >> (9 + ADDR_SHIFT - PAGE_SHIFT))
# define SECTOR_OFFSET(addr)	((addr) & ((511 >> PAGE_SHIFT) << 8))

# define PAGE_SIZE		256
# define PAGE_SHIFT		8
# define PAGE_SECTORS		1
# define ADDR_SHIFT		8
# include "nand.c"
# define PAGE_SIZE		512
# define PAGE_SHIFT		9
# define PAGE_SECTORS		1
# define ADDR_SHIFT		8
# include "nand.c"
# define PAGE_SIZE		2048
# define PAGE_SHIFT		11
# define PAGE_SECTORS		4
# define ADDR_SHIFT		16
# include "nand.c"

/* Information based on Linux drivers/mtd/nand/nand_ids.c */
static const struct {
    int size;
    int width;
    int page_shift;
    int erase_shift;
    uint32_t options;
} nand_flash_ids[0x100] = {
    [0 ... 0xff] = { 0 },

    [0x6e] = { 1,	8,	8, 4, 0 },
    [0x64] = { 2,	8,	8, 4, 0 },
    [0x6b] = { 4,	8,	9, 4, 0 },
    [0xe8] = { 1,	8,	8, 4, 0 },
    [0xec] = { 1,	8,	8, 4, 0 },
    [0xea] = { 2,	8,	8, 4, 0 },
    [0xd5] = { 4,	8,	9, 4, 0 },
    [0xe3] = { 4,	8,	9, 4, 0 },
    [0xe5] = { 4,	8,	9, 4, 0 },
    [0xd6] = { 8,	8,	9, 4, 0 },

    [0x39] = { 8,	8,	9, 4, 0 },
    [0xe6] = { 8,	8,	9, 4, 0 },
    [0x49] = { 8,	16,	9, 4, NAND_BUSWIDTH_16 },
    [0x59] = { 8,	16,	9, 4, NAND_BUSWIDTH_16 },

    [0x33] = { 16,	8,	9, 5, 0 },
    [0x73] = { 16,	8,	9, 5, 0 },
    [0x43] = { 16,	16,	9, 5, NAND_BUSWIDTH_16 },
    [0x53] = { 16,	16,	9, 5, NAND_BUSWIDTH_16 },

    [0x35] = { 32,	8,	9, 5, 0 },
    [0x75] = { 32,	8,	9, 5, 0 },
    [0x45] = { 32,	16,	9, 5, NAND_BUSWIDTH_16 },
    [0x55] = { 32,	16,	9, 5, NAND_BUSWIDTH_16 },

    [0x36] = { 64,	8,	9, 5, 0 },
    [0x76] = { 64,	8,	9, 5, 0 },
    [0x46] = { 64,	16,	9, 5, NAND_BUSWIDTH_16 },
    [0x56] = { 64,	16,	9, 5, NAND_BUSWIDTH_16 },

    [0x78] = { 128,	8,	9, 5, 0 },
    [0x39] = { 128,	8,	9, 5, 0 },
    [0x79] = { 128,	8,	9, 5, 0 },
    [0x72] = { 128,	16,	9, 5, NAND_BUSWIDTH_16 },
    [0x49] = { 128,	16,	9, 5, NAND_BUSWIDTH_16 },
    [0x74] = { 128,	16,	9, 5, NAND_BUSWIDTH_16 },
    [0x59] = { 128,	16,	9, 5, NAND_BUSWIDTH_16 },

    [0x71] = { 256,	8,	9, 5, 0 },

    /*
     * These are the new chips with large page size. The pagesize and the
     * erasesize is determined from the extended id bytes
     */
# define LP_OPTIONS	(NAND_SAMSUNG_LP | NAND_NO_READRDY | NAND_NO_AUTOINCR)
# define LP_OPTIONS16	(LP_OPTIONS | NAND_BUSWIDTH_16)

    /* 512 Megabit */
    [0xa2] = { 64,	8,	0, 0, LP_OPTIONS },
    [0xf2] = { 64,	8,	0, 0, LP_OPTIONS },
    [0xb2] = { 64,	16,	0, 0, LP_OPTIONS16 },
    [0xc2] = { 64,	16,	0, 0, LP_OPTIONS16 },

    /* 1 Gigabit */
    [0xa1] = { 128,	8,	0, 0, LP_OPTIONS },
    [0xf1] = { 128,	8,	0, 0, LP_OPTIONS },
    [0xb1] = { 128,	16,	0, 0, LP_OPTIONS16 },
    [0xc1] = { 128,	16,	0, 0, LP_OPTIONS16 },

    /* 2 Gigabit */
    [0xaa] = { 256,	8,	0, 0, LP_OPTIONS },
    [0xda] = { 256,	8,	0, 0, LP_OPTIONS },
    [0xba] = { 256,	16,	0, 0, LP_OPTIONS16 },
    [0xca] = { 256,	16,	0, 0, LP_OPTIONS16 },

    /* 4 Gigabit */
    [0xac] = { 512,	8,	0, 0, LP_OPTIONS },
    [0xdc] = { 512,	8,	0, 0, LP_OPTIONS },
    [0xbc] = { 512,	16,	0, 0, LP_OPTIONS16 },
    [0xcc] = { 512,	16,	0, 0, LP_OPTIONS16 },

    /* 8 Gigabit */
    [0xa3] = { 1024,	8,	0, 0, LP_OPTIONS },
    [0xd3] = { 1024,	8,	0, 0, LP_OPTIONS },
    [0xb3] = { 1024,	16,	0, 0, LP_OPTIONS16 },
    [0xc3] = { 1024,	16,	0, 0, LP_OPTIONS16 },

    /* 16 Gigabit */
    [0xa5] = { 2048,	8,	0, 0, LP_OPTIONS },
    [0xd5] = { 2048,	8,	0, 0, LP_OPTIONS },
    [0xb5] = { 2048,	16,	0, 0, LP_OPTIONS16 },
    [0xc5] = { 2048,	16,	0, 0, LP_OPTIONS16 },
};

static void nand_reset(NANDFlashState *s)
{
    s->cmd = NAND_CMD_READ0;
    s->addr = 0;
    s->addrlen = 0;
    s->iolen = 0;
    s->offset = 0;
    s->status &= NAND_IOSTATUS_UNPROTCT;
}

static void nand_command(NANDFlashState *s)
{
    switch (s->cmd) {
    case NAND_CMD_READ0:
        s->iolen = 0;
        break;

    case NAND_CMD_READID:
        s->io[0] = s->manf_id;
        s->io[1] = s->chip_id;
        s->io[2] = 'Q';		/* Don't-care byte (often 0xa5) */
        if (nand_flash_ids[s->chip_id].options & NAND_SAMSUNG_LP)
            s->io[3] = 0x15;	/* Page Size, Block Size, Spare Size.. */
        else
            s->io[3] = 0xc0;	/* Multi-plane */
        s->ioaddr = s->io;
        s->iolen = 4;
        break;

    case NAND_CMD_RANDOMREAD2:
    case NAND_CMD_NOSERIALREAD2:
        if (!(nand_flash_ids[s->chip_id].options & NAND_SAMSUNG_LP))
            break;

        s->blk_load(s, s->addr, s->addr & ((1 << s->addr_shift) - 1));
        break;

    case NAND_CMD_RESET:
        nand_reset(s);
        break;

    case NAND_CMD_PAGEPROGRAM1:
        s->ioaddr = s->io;
        s->iolen = 0;
        break;

    case NAND_CMD_PAGEPROGRAM2:
        if (s->wp) {
            s->blk_write(s);
        }
        break;

    case NAND_CMD_BLOCKERASE1:
        break;

    case NAND_CMD_BLOCKERASE2:
        if (nand_flash_ids[s->chip_id].options & NAND_SAMSUNG_LP)
            s->addr <<= 16;
        else
            s->addr <<= 8;

        if (s->wp) {
            s->blk_erase(s);
        }
        break;

    case NAND_CMD_READSTATUS:
        s->io[0] = s->status;
        s->ioaddr = s->io;
        s->iolen = 1;
        break;

    default:
        printf("%s: Unknown NAND command 0x%02x\n", __FUNCTION__, s->cmd);
    }
}

static void nand_save(QEMUFile *f, void *opaque)
{
    NANDFlashState *s = (NANDFlashState *) opaque;
    qemu_put_byte(f, s->cle);
    qemu_put_byte(f, s->ale);
    qemu_put_byte(f, s->ce);
    qemu_put_byte(f, s->wp);
    qemu_put_byte(f, s->gnd);
    qemu_put_buffer(f, s->io, sizeof(s->io));
    qemu_put_be32(f, s->ioaddr - s->io);
    qemu_put_be32(f, s->iolen);

    qemu_put_be32s(f, &s->cmd);
    qemu_put_be32s(f, &s->addr);
    qemu_put_be32(f, s->addrlen);
    qemu_put_be32(f, s->status);
    qemu_put_be32(f, s->offset);
    /* XXX: do we want to save s->storage too? */
}

static int nand_load(QEMUFile *f, void *opaque, int version_id)
{
    NANDFlashState *s = (NANDFlashState *) opaque;
    s->cle = qemu_get_byte(f);
    s->ale = qemu_get_byte(f);
    s->ce = qemu_get_byte(f);
    s->wp = qemu_get_byte(f);
    s->gnd = qemu_get_byte(f);
    qemu_get_buffer(f, s->io, sizeof(s->io));
    s->ioaddr = s->io + qemu_get_be32(f);
    s->iolen = qemu_get_be32(f);
    if (s->ioaddr >= s->io + sizeof(s->io) || s->ioaddr < s->io)
        return -EINVAL;

    qemu_get_be32s(f, &s->cmd);
    qemu_get_be32s(f, &s->addr);
    s->addrlen = qemu_get_be32(f);
    s->status = qemu_get_be32(f);
    s->offset = qemu_get_be32(f);
    return 0;
}

/*
 * Chip inputs are CLE, ALE, CE, WP, GND and eight I/O pins.  Chip
 * outputs are R/B and eight I/O pins.
 *
 * CE, WP and R/B are active low.
 */
void nand_setpins(NANDFlashState *s,
                int cle, int ale, int ce, int wp, int gnd)
{
    s->cle = cle;
    s->ale = ale;
    s->ce = ce;
    s->wp = wp;
    s->gnd = gnd;
    if (wp)
        s->status |= NAND_IOSTATUS_UNPROTCT;
    else
        s->status &= ~NAND_IOSTATUS_UNPROTCT;
}

void nand_getpins(NANDFlashState *s, int *rb)
{
    *rb = 1;
}

void nand_setio(NANDFlashState *s, uint8_t value)
{
    if (!s->ce && s->cle) {
        if (nand_flash_ids[s->chip_id].options & NAND_SAMSUNG_LP) {
            if (s->cmd == NAND_CMD_READ0 && value == NAND_CMD_LPREAD2)
                return;
            if (value == NAND_CMD_RANDOMREAD1) {
                s->addr &= ~((1 << s->addr_shift) - 1);
                s->addrlen = 0;
                return;
            }
        }
        if (value == NAND_CMD_READ0)
            s->offset = 0;
	else if (value == NAND_CMD_READ1) {
            s->offset = 0x100;
            value = NAND_CMD_READ0;
        }
	else if (value == NAND_CMD_READ2) {
            s->offset = 1 << s->page_shift;
            value = NAND_CMD_READ0;
        }

        s->cmd = value;

        if (s->cmd == NAND_CMD_READSTATUS ||
                s->cmd == NAND_CMD_PAGEPROGRAM2 ||
                s->cmd == NAND_CMD_BLOCKERASE1 ||
                s->cmd == NAND_CMD_BLOCKERASE2 ||
                s->cmd == NAND_CMD_NOSERIALREAD2 ||
                s->cmd == NAND_CMD_RANDOMREAD2 ||
                s->cmd == NAND_CMD_RESET)
            nand_command(s);

        if (s->cmd != NAND_CMD_RANDOMREAD2) {
            s->addrlen = 0;
            s->addr = 0;
        }
    }

    if (s->ale) {
        s->addr |= value << (s->addrlen * 8);
        s->addrlen ++;

        if (s->addrlen == 1 && s->cmd == NAND_CMD_READID)
            nand_command(s);

        if (!(nand_flash_ids[s->chip_id].options & NAND_SAMSUNG_LP) &&
                s->addrlen == 3 && (
                    s->cmd == NAND_CMD_READ0 ||
                    s->cmd == NAND_CMD_PAGEPROGRAM1))
            nand_command(s);
        if ((nand_flash_ids[s->chip_id].options & NAND_SAMSUNG_LP) &&
               s->addrlen == 4 && (
                    s->cmd == NAND_CMD_READ0 ||
                    s->cmd == NAND_CMD_PAGEPROGRAM1))
            nand_command(s);
    }

    if (!s->cle && !s->ale && s->cmd == NAND_CMD_PAGEPROGRAM1) {
        if (s->iolen < (1 << s->page_shift) + (1 << s->oob_shift))
            s->io[s->iolen ++] = value;
    } else if (!s->cle && !s->ale && s->cmd == NAND_CMD_COPYBACKPRG1) {
        if ((s->addr & ((1 << s->addr_shift) - 1)) <
                (1 << s->page_shift) + (1 << s->oob_shift)) {
            s->io[s->iolen + (s->addr & ((1 << s->addr_shift) - 1))] = value;
            s->addr ++;
        }
    }
}

uint8_t nand_getio(NANDFlashState *s)
{
    int offset;

    /* Allow sequential reading */
    if (!s->iolen && s->cmd == NAND_CMD_READ0) {
        offset = (s->addr & ((1 << s->addr_shift) - 1)) + s->offset;
        s->offset = 0;

        s->blk_load(s, s->addr, offset);
        if (s->gnd)
            s->iolen = (1 << s->page_shift) - offset;
        else
            s->iolen = (1 << s->page_shift) + (1 << s->oob_shift) - offset;
    }

    if (s->ce || s->iolen <= 0)
        return 0;

    s->iolen --;
    return *(s->ioaddr ++);
}

NANDFlashState *nand_init(int manf_id, int chip_id)
{
    int pagesize;
    NANDFlashState *s;
    int index;

    if (nand_flash_ids[chip_id].size == 0) {
        hw_error("%s: Unsupported NAND chip ID.\n", __FUNCTION__);
    }

    s = (NANDFlashState *) qemu_mallocz(sizeof(NANDFlashState));
    index = drive_get_index(IF_MTD, 0, 0);
    if (index != -1)
        s->bdrv = drives_table[index].bdrv;
    s->manf_id = manf_id;
    s->chip_id = chip_id;
    s->size = nand_flash_ids[s->chip_id].size << 20;
    if (nand_flash_ids[s->chip_id].options & NAND_SAMSUNG_LP) {
        s->page_shift = 11;
        s->erase_shift = 6;
    } else {
        s->page_shift = nand_flash_ids[s->chip_id].page_shift;
        s->erase_shift = nand_flash_ids[s->chip_id].erase_shift;
    }

    switch (1 << s->page_shift) {
    case 256:
        nand_init_256(s);
        break;
    case 512:
        nand_init_512(s);
        break;
    case 2048:
        nand_init_2048(s);
        break;
    default:
        hw_error("%s: Unsupported NAND block size.\n", __FUNCTION__);
    }

    pagesize = 1 << s->oob_shift;
    s->mem_oob = 1;
    if (s->bdrv && bdrv_getlength(s->bdrv) >=
                    (s->pages << s->page_shift) + (s->pages << s->oob_shift)) {
        pagesize = 0;
        s->mem_oob = 0;
    }

    if (!s->bdrv)
        pagesize += 1 << s->page_shift;
    if (pagesize)
        s->storage = (uint8_t *) memset(qemu_malloc(s->pages * pagesize),
                        0xff, s->pages * pagesize);
    /* Give s->ioaddr a sane value in case we save state before it
       is used.  */
    s->ioaddr = s->io;

    register_savevm("nand", -1, 0, nand_save, nand_load, s);

    return s;
}

void nand_done(NANDFlashState *s)
{
    if (s->bdrv) {
        bdrv_close(s->bdrv);
        bdrv_delete(s->bdrv);
    }

    if (!s->bdrv || s->mem_oob)
        free(s->storage);

    free(s);
}

#else

/* Program a single page */
static void glue(nand_blk_write_, PAGE_SIZE)(NANDFlashState *s)
{
    uint32_t off, page, sector, soff;
    uint8_t iobuf[(PAGE_SECTORS + 2) * 0x200];
    if (PAGE(s->addr) >= s->pages)
        return;

    if (!s->bdrv) {
        memcpy(s->storage + PAGE_START(s->addr) + (s->addr & PAGE_MASK) +
                        s->offset, s->io, s->iolen);
    } else if (s->mem_oob) {
        sector = SECTOR(s->addr);
        off = (s->addr & PAGE_MASK) + s->offset;
        soff = SECTOR_OFFSET(s->addr);
        if (bdrv_read(s->bdrv, sector, iobuf, PAGE_SECTORS) == -1) {
            printf("%s: read error in sector %i\n", __FUNCTION__, sector);
            return;
        }

        memcpy(iobuf + (soff | off), s->io, MIN(s->iolen, PAGE_SIZE - off));
        if (off + s->iolen > PAGE_SIZE) {
            page = PAGE(s->addr);
            memcpy(s->storage + (page << OOB_SHIFT), s->io + PAGE_SIZE - off,
                            MIN(OOB_SIZE, off + s->iolen - PAGE_SIZE));
        }

        if (bdrv_write(s->bdrv, sector, iobuf, PAGE_SECTORS) == -1)
            printf("%s: write error in sector %i\n", __FUNCTION__, sector);
    } else {
        off = PAGE_START(s->addr) + (s->addr & PAGE_MASK) + s->offset;
        sector = off >> 9;
        soff = off & 0x1ff;
        if (bdrv_read(s->bdrv, sector, iobuf, PAGE_SECTORS + 2) == -1) {
            printf("%s: read error in sector %i\n", __FUNCTION__, sector);
            return;
        }

        memcpy(iobuf + soff, s->io, s->iolen);

        if (bdrv_write(s->bdrv, sector, iobuf, PAGE_SECTORS + 2) == -1)
            printf("%s: write error in sector %i\n", __FUNCTION__, sector);
    }
    s->offset = 0;
}

/* Erase a single block */
static void glue(nand_blk_erase_, PAGE_SIZE)(NANDFlashState *s)
{
    uint32_t i, page, addr;
    uint8_t iobuf[0x200] = { [0 ... 0x1ff] = 0xff, };
    addr = s->addr & ~((1 << (ADDR_SHIFT + s->erase_shift)) - 1);

    if (PAGE(addr) >= s->pages)
        return;

    if (!s->bdrv) {
        memset(s->storage + PAGE_START(addr),
                        0xff, (PAGE_SIZE + OOB_SIZE) << s->erase_shift);
    } else if (s->mem_oob) {
        memset(s->storage + (PAGE(addr) << OOB_SHIFT),
                        0xff, OOB_SIZE << s->erase_shift);
        i = SECTOR(addr);
        page = SECTOR(addr + (ADDR_SHIFT + s->erase_shift));
        for (; i < page; i ++)
            if (bdrv_write(s->bdrv, i, iobuf, 1) == -1)
                printf("%s: write error in sector %i\n", __FUNCTION__, i);
    } else {
        addr = PAGE_START(addr);
        page = addr >> 9;
        if (bdrv_read(s->bdrv, page, iobuf, 1) == -1)
            printf("%s: read error in sector %i\n", __FUNCTION__, page);
        memset(iobuf + (addr & 0x1ff), 0xff, (~addr & 0x1ff) + 1);
        if (bdrv_write(s->bdrv, page, iobuf, 1) == -1)
            printf("%s: write error in sector %i\n", __FUNCTION__, page);

        memset(iobuf, 0xff, 0x200);
        i = (addr & ~0x1ff) + 0x200;
        for (addr += ((PAGE_SIZE + OOB_SIZE) << s->erase_shift) - 0x200;
                        i < addr; i += 0x200)
            if (bdrv_write(s->bdrv, i >> 9, iobuf, 1) == -1)
                printf("%s: write error in sector %i\n", __FUNCTION__, i >> 9);

        page = i >> 9;
        if (bdrv_read(s->bdrv, page, iobuf, 1) == -1)
            printf("%s: read error in sector %i\n", __FUNCTION__, page);
        memset(iobuf, 0xff, ((addr - 1) & 0x1ff) + 1);
        if (bdrv_write(s->bdrv, page, iobuf, 1) == -1)
            printf("%s: write error in sector %i\n", __FUNCTION__, page);
    }
}

static void glue(nand_blk_load_, PAGE_SIZE)(NANDFlashState *s,
                uint32_t addr, int offset)
{
    if (PAGE(addr) >= s->pages)
        return;

    if (s->bdrv) {
        if (s->mem_oob) {
            if (bdrv_read(s->bdrv, SECTOR(addr), s->io, PAGE_SECTORS) == -1)
                printf("%s: read error in sector %i\n",
                                __FUNCTION__, SECTOR(addr));
            memcpy(s->io + SECTOR_OFFSET(s->addr) + PAGE_SIZE,
                            s->storage + (PAGE(s->addr) << OOB_SHIFT),
                            OOB_SIZE);
            s->ioaddr = s->io + SECTOR_OFFSET(s->addr) + offset;
        } else {
            if (bdrv_read(s->bdrv, PAGE_START(addr) >> 9,
                                    s->io, (PAGE_SECTORS + 2)) == -1)
                printf("%s: read error in sector %i\n",
                                __FUNCTION__, PAGE_START(addr) >> 9);
            s->ioaddr = s->io + (PAGE_START(addr) & 0x1ff) + offset;
        }
    } else {
        memcpy(s->io, s->storage + PAGE_START(s->addr) +
                        offset, PAGE_SIZE + OOB_SIZE - offset);
        s->ioaddr = s->io;
    }

    s->addr &= PAGE_SIZE - 1;
    s->addr += PAGE_SIZE;
}

static void glue(nand_init_, PAGE_SIZE)(NANDFlashState *s)
{
    s->oob_shift = PAGE_SHIFT - 5;
    s->pages = s->size >> PAGE_SHIFT;
    s->addr_shift = ADDR_SHIFT;

    s->blk_erase = glue(nand_blk_erase_, PAGE_SIZE);
    s->blk_write = glue(nand_blk_write_, PAGE_SIZE);
    s->blk_load = glue(nand_blk_load_, PAGE_SIZE);
}

# undef PAGE_SIZE
# undef PAGE_SHIFT
# undef PAGE_SECTORS
# undef ADDR_SHIFT
#endif	/* NAND_IO */

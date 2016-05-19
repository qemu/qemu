/*
 * Hard disk geometry utilities
 *
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 * Copyright (c) 2003 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "sysemu/block-backend.h"
#include "qemu/bswap.h"
#include "hw/block/block.h"
#include "trace.h"

struct partition {
        uint8_t boot_ind;           /* 0x80 - active */
        uint8_t head;               /* starting head */
        uint8_t sector;             /* starting sector */
        uint8_t cyl;                /* starting cylinder */
        uint8_t sys_ind;            /* What partition type */
        uint8_t end_head;           /* end head */
        uint8_t end_sector;         /* end sector */
        uint8_t end_cyl;            /* end cylinder */
        uint32_t start_sect;        /* starting sector counting from 0 */
        uint32_t nr_sects;          /* nr of sectors in partition */
} QEMU_PACKED;

/* try to guess the disk logical geometry from the MSDOS partition table.
   Return 0 if OK, -1 if could not guess */
static int guess_disk_lchs(BlockBackend *blk,
                           int *pcylinders, int *pheads, int *psectors)
{
    uint8_t buf[BDRV_SECTOR_SIZE];
    int i, heads, sectors, cylinders;
    struct partition *p;
    uint32_t nr_sects;
    uint64_t nb_sectors;

    blk_get_geometry(blk, &nb_sectors);

    /**
     * The function will be invoked during startup not only in sync I/O mode,
     * but also in async I/O mode. So the I/O throttling function has to
     * be disabled temporarily here, not permanently.
     */
    if (blk_pread_unthrottled(blk, 0, buf, BDRV_SECTOR_SIZE) < 0) {
        return -1;
    }
    /* test msdos magic */
    if (buf[510] != 0x55 || buf[511] != 0xaa) {
        return -1;
    }
    for (i = 0; i < 4; i++) {
        p = ((struct partition *)(buf + 0x1be)) + i;
        nr_sects = le32_to_cpu(p->nr_sects);
        if (nr_sects && p->end_head) {
            /* We make the assumption that the partition terminates on
               a cylinder boundary */
            heads = p->end_head + 1;
            sectors = p->end_sector & 63;
            if (sectors == 0) {
                continue;
            }
            cylinders = nb_sectors / (heads * sectors);
            if (cylinders < 1 || cylinders > 16383) {
                continue;
            }
            *pheads = heads;
            *psectors = sectors;
            *pcylinders = cylinders;
            trace_hd_geometry_lchs_guess(blk, cylinders, heads, sectors);
            return 0;
        }
    }
    return -1;
}

static void guess_chs_for_size(BlockBackend *blk,
                uint32_t *pcyls, uint32_t *pheads, uint32_t *psecs)
{
    uint64_t nb_sectors;
    int cylinders;

    blk_get_geometry(blk, &nb_sectors);

    cylinders = nb_sectors / (16 * 63);
    if (cylinders > 16383) {
        cylinders = 16383;
    } else if (cylinders < 2) {
        cylinders = 2;
    }
    *pcyls = cylinders;
    *pheads = 16;
    *psecs = 63;
}

void hd_geometry_guess(BlockBackend *blk,
                       uint32_t *pcyls, uint32_t *pheads, uint32_t *psecs,
                       int *ptrans)
{
    int cylinders, heads, secs, translation;
    HDGeometry geo;

    /* Try to probe the backing device geometry, otherwise fallback
       to the old logic. (as of 12/2014 probing only succeeds on DASDs) */
    if (blk_probe_geometry(blk, &geo) == 0) {
        *pcyls = geo.cylinders;
        *psecs = geo.sectors;
        *pheads = geo.heads;
        translation = BIOS_ATA_TRANSLATION_NONE;
    } else if (guess_disk_lchs(blk, &cylinders, &heads, &secs) < 0) {
        /* no LCHS guess: use a standard physical disk geometry  */
        guess_chs_for_size(blk, pcyls, pheads, psecs);
        translation = hd_bios_chs_auto_trans(*pcyls, *pheads, *psecs);
    } else if (heads > 16) {
        /* LCHS guess with heads > 16 means that a BIOS LBA
           translation was active, so a standard physical disk
           geometry is OK */
        guess_chs_for_size(blk, pcyls, pheads, psecs);
        translation = *pcyls * *pheads <= 131072
            ? BIOS_ATA_TRANSLATION_LARGE
            : BIOS_ATA_TRANSLATION_LBA;
    } else {
        /* LCHS guess with heads <= 16: use as physical geometry */
        *pcyls = cylinders;
        *pheads = heads;
        *psecs = secs;
        /* disable any translation to be in sync with
           the logical geometry */
        translation = BIOS_ATA_TRANSLATION_NONE;
    }
    if (ptrans) {
        *ptrans = translation;
    }
    trace_hd_geometry_guess(blk, *pcyls, *pheads, *psecs, translation);
}

int hd_bios_chs_auto_trans(uint32_t cyls, uint32_t heads, uint32_t secs)
{
    return cyls <= 1024 && heads <= 16 && secs <= 63
        ? BIOS_ATA_TRANSLATION_NONE
        : BIOS_ATA_TRANSLATION_LBA;
}

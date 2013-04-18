/*
 * MemoryRegion backed block driver
 *
 * Copyright (c) 2013 espes
 *
 * Based on "Add an in-memory block device" patch
 * Copyright IBM, Corp. 2007
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu-common.h"
#include "exec/memory.h"
#include "block/block_int.h"

#include "block/blkmemory.h"


typedef struct BDRVMemoryState {
    uint64_t size;
    AddressSpace *as;
} BDRVMemoryState;

static int memory_open(BlockDriverState *bs, QDict *options, int flags)
{
    /* We're kinda unique in that we're inited with a MemoryRegion instead
     * of a file. A MemoryRegion pointer can't be put in QDict, so we have
     * to be inited by hand. If something tries to init us normally, better
     * to fail than crash.
     */
    return -1;
}

static void memory_close(BlockDriverState *bs)
{
    /* nothing to do..? */
}

static int64_t memory_getlength(BlockDriverState *bs)
{
    BDRVMemoryState *s = bs->opaque;

    return s->size / BDRV_SECTOR_SIZE;
}

static int memory_read(BlockDriverState *bs, int64_t sector_num,
                       uint8_t *buf, int nb_sectors)
{
    BDRVMemoryState *s = bs->opaque;
    
    sector_num = MIN(sector_num, bs->total_sectors - 1);
    size_t size = MIN(s->size - sector_num * BDRV_SECTOR_SIZE,
                      nb_sectors * BDRV_SECTOR_SIZE);

    address_space_read(s->as, sector_num * BDRV_SECTOR_SIZE, buf, size);

    return 0;
}

static int memory_write(BlockDriverState *bs, int64_t sector_num,
                        const uint8_t *buf, int nb_sectors)
{
    BDRVMemoryState *s = bs->opaque;

    sector_num = MIN(sector_num, bs->total_sectors - 1);
    size_t size = MIN(s->size - sector_num * BDRV_SECTOR_SIZE,
                      nb_sectors * BDRV_SECTOR_SIZE);

    address_space_write(s->as, sector_num * BDRV_SECTOR_SIZE, buf, size);

    return 0;
}

static BlockDriver bdrv_memory = {
    .format_name = "memory",
    .instance_size = sizeof(BDRVMemoryState),
    .bdrv_open = memory_open,
    .bdrv_close = memory_close,
    .bdrv_getlength = memory_getlength,

    .bdrv_read = memory_read,
    .bdrv_write = memory_write,
};

static void bdrv_memory_init(void)
{
    bdrv_register(&bdrv_memory);
}

block_init(bdrv_memory_init);


int bdrv_memory_open(BlockDriverState *bs, AddressSpace *as, uint64_t size)
{
    bs->total_sectors = (size + BDRV_SECTOR_SIZE-1) / BDRV_SECTOR_SIZE;
    bs->read_only = false;
    bs->is_temporary = false;
    bs->encrypted = false;

    pstrcpy(bs->filename, sizeof(bs->filename), "<mem>");

    bs->drv = &bdrv_memory;
    bs->opaque = g_malloc0(bdrv_memory.instance_size);
    if (!bs->opaque) {
        return -1;
    }

    BDRVMemoryState *s = bs->opaque;
    s->as = as;
    s->size = size;

    return 0;
}
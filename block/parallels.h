/*
* Block driver for Parallels disk image format
*
* Copyright (c) 2015-2017 Virtuozzo, Inc.
* Authors:
*         2016-2017 Klim S. Kireev <klim.kireev@virtuozzo.com>
*         2015 Denis V. Lunev <den@openvz.org>
*
* This code was originally based on comparing different disk images created
* by Parallels. Currently it is based on opened OpenVZ sources
* available at
*     https://github.com/OpenVZ/ploop
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
#ifndef BLOCK_PARALLELS_H
#define BLOCK_PARALLELS_H
#include "qemu/coroutine.h"

#define HEADS_NUMBER 16
#define SEC_IN_CYL 32
#define DEFAULT_CLUSTER_SIZE 1048576        /* 1 MiB */

/* always little-endian */
typedef struct ParallelsHeader {
    char magic[16]; /* "WithoutFreeSpace" */
    uint32_t version;
    uint32_t heads;
    uint32_t cylinders;
    uint32_t tracks;
    uint32_t bat_entries;
    uint64_t nb_sectors;
    uint32_t inuse;
    uint32_t data_off;
    char padding[12];
} QEMU_PACKED ParallelsHeader;

typedef enum ParallelsPreallocMode {
    PRL_PREALLOC_MODE_FALLOCATE = 0,
    PRL_PREALLOC_MODE_TRUNCATE = 1,
    PRL_PREALLOC_MODE__MAX = 2,
} ParallelsPreallocMode;

typedef struct BDRVParallelsState {
    /** Locking is conservative, the lock protects
     *   - image file extending (truncate, fallocate)
     *   - any access to block allocation table
     */
    CoMutex lock;

    ParallelsHeader *header;
    uint32_t header_size;
    bool header_unclean;

    unsigned long *bat_dirty_bmap;
    unsigned int  bat_dirty_block;

    uint32_t *bat_bitmap;
    unsigned int bat_size;

    int64_t  data_end;
    uint64_t prealloc_size;
    ParallelsPreallocMode prealloc_mode;

    unsigned int tracks;

    unsigned int off_multiplier;
    Error *migration_blocker;
} BDRVParallelsState;

#endif

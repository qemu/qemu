/*
 * Common code for block device models
 *
 * Copyright (C) 2012 Red Hat, Inc.
 * Copyright (c) 2003-2008 Fabrice Bellard
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#ifndef HW_BLOCK_COMMON_H
#define HW_BLOCK_COMMON_H

#include "qemu-common.h"

/* Configuration */

typedef struct BlockConf {
    BlockDriverState *bs;
    uint16_t physical_block_size;
    uint16_t logical_block_size;
    uint16_t min_io_size;
    uint32_t opt_io_size;
    int32_t bootindex;
    uint32_t discard_granularity;
    /* geometry, not all devices use this */
    uint32_t cyls, heads, secs;
} BlockConf;

static inline unsigned int get_physical_block_exp(BlockConf *conf)
{
    unsigned int exp = 0, size;

    for (size = conf->physical_block_size;
        size > conf->logical_block_size;
        size >>= 1) {
        exp++;
    }

    return exp;
}

#define DEFINE_BLOCK_PROPERTIES(_state, _conf)                          \
    DEFINE_PROP_DRIVE("drive", _state, _conf.bs),                       \
    DEFINE_PROP_BLOCKSIZE("logical_block_size", _state,                 \
                          _conf.logical_block_size, 512),               \
    DEFINE_PROP_BLOCKSIZE("physical_block_size", _state,                \
                          _conf.physical_block_size, 512),              \
    DEFINE_PROP_UINT16("min_io_size", _state, _conf.min_io_size, 0),  \
    DEFINE_PROP_UINT32("opt_io_size", _state, _conf.opt_io_size, 0),    \
    DEFINE_PROP_INT32("bootindex", _state, _conf.bootindex, -1),        \
    DEFINE_PROP_UINT32("discard_granularity", _state, \
                       _conf.discard_granularity, -1)

#define DEFINE_BLOCK_CHS_PROPERTIES(_state, _conf)      \
    DEFINE_PROP_UINT32("cyls", _state, _conf.cyls, 0),  \
    DEFINE_PROP_UINT32("heads", _state, _conf.heads, 0), \
    DEFINE_PROP_UINT32("secs", _state, _conf.secs, 0)

/* Configuration helpers */

void blkconf_serial(BlockConf *conf, char **serial);
int blkconf_geometry(BlockConf *conf, int *trans,
                     unsigned cyls_max, unsigned heads_max, unsigned secs_max);

/* Hard disk geometry */

#define BIOS_ATA_TRANSLATION_AUTO   0
#define BIOS_ATA_TRANSLATION_NONE   1
#define BIOS_ATA_TRANSLATION_LBA    2
#define BIOS_ATA_TRANSLATION_LARGE  3
#define BIOS_ATA_TRANSLATION_RECHS  4

void hd_geometry_guess(BlockDriverState *bs,
                       uint32_t *pcyls, uint32_t *pheads, uint32_t *psecs,
                       int *ptrans);
int hd_bios_chs_auto_trans(uint32_t cyls, uint32_t heads, uint32_t secs);

#endif

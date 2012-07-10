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

/* Hard disk geometry */

#define BIOS_ATA_TRANSLATION_AUTO   0
#define BIOS_ATA_TRANSLATION_NONE   1
#define BIOS_ATA_TRANSLATION_LBA    2
#define BIOS_ATA_TRANSLATION_LARGE  3
#define BIOS_ATA_TRANSLATION_RECHS  4

void hd_geometry_guess(BlockDriverState *bs,
                       uint32_t *pcyls, uint32_t *pheads, uint32_t *psecs,
                       int *ptrans);

#endif

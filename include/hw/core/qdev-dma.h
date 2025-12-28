/*
 * Support for dma_addr_t typed properties
 *
 * Copyright (C) 2012 David Gibson, IBM Corporation.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_QDEV_DMA_H
#define HW_QDEV_DMA_H

#define DEFINE_PROP_DMAADDR(_n, _s, _f, _d)                               \
    DEFINE_PROP_UINT64(_n, _s, _f, _d)

#endif

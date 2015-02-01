/*
 * libqos malloc support for PC
 *
 * Copyright IBM, Corp. 2012-2013
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef LIBQOS_MALLOC_PC_H
#define LIBQOS_MALLOC_PC_H

#include "libqos/malloc.h"

QGuestAllocator *pc_alloc_init(void);
QGuestAllocator *pc_alloc_init_flags(QAllocOpts flags);
void pc_alloc_uninit(QGuestAllocator *allocator);

#endif

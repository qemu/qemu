/*
 * libqos malloc support for SPAPR
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef LIBQOS_MALLOC_SPAPR_H
#define LIBQOS_MALLOC_SPAPR_H

#include "libqos-malloc.h"

void spapr_alloc_init(QGuestAllocator *s, QTestState *qts, QAllocOpts flags);

#endif

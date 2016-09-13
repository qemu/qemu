/*
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef LIBQOS_RTAS_H
#define LIBQOS_RTAS_H
#include "libqos/malloc.h"

int qrtas_get_time_of_day(QGuestAllocator *alloc, struct tm *tm, uint32_t *ns);
#endif /* LIBQOS_RTAS_H */

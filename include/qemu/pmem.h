/*
 * QEMU header file for libpmem.
 *
 * Copyright (c) 2018 Intel Corporation.
 *
 * Author: Haozhong Zhang <address@hidden>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_PMEM_H
#define QEMU_PMEM_H

#ifdef CONFIG_LIBPMEM
#include <libpmem.h>
#else  /* !CONFIG_LIBPMEM */

static inline void *
pmem_memcpy_persist(void *pmemdest, const void *src, size_t len)
{
    /* If 'pmem' option is 'on', we should always have libpmem support,
       or qemu will report a error and exit, never come here. */
    g_assert_not_reached();
}

static inline void
pmem_persist(const void *addr, size_t len)
{
    g_assert_not_reached();
}

#endif /* CONFIG_LIBPMEM */

#endif /* QEMU_PMEM_H */

/*
 * pagesize.c - query the host about its page size
 *
 * Copyright (C) 2017, Emilio G. Cota <cota@braap.org>
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

uintptr_t qemu_real_host_page_size;
intptr_t qemu_real_host_page_mask;

static void __attribute__((constructor)) init_real_host_page_size(void)
{
    qemu_real_host_page_size = getpagesize();
    qemu_real_host_page_mask = -(intptr_t)qemu_real_host_page_size;
}

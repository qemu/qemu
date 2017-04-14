/*
 * Linux native AIO support.
 *
 * Copyright (C) 2009 IBM, Corp.
 * Copyright (C) 2009 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "block/aio.h"
#include "block/raw-aio.h"

void laio_detach_aio_context(LinuxAioState *s, AioContext *old_context)
{
    abort();
}

void laio_attach_aio_context(LinuxAioState *s, AioContext *new_context)
{
    abort();
}

LinuxAioState *laio_init(void)
{
    abort();
}

void laio_cleanup(LinuxAioState *s)
{
    abort();
}

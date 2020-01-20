/*
 * Linux io_uring support.
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

void luring_detach_aio_context(LuringState *s, AioContext *old_context)
{
    abort();
}

void luring_attach_aio_context(LuringState *s, AioContext *new_context)
{
    abort();
}

LuringState *luring_init(Error **errp)
{
    abort();
}

void luring_cleanup(LuringState *s)
{
    abort();
}

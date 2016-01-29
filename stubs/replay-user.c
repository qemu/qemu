/*
 * replay.c
 *
 * Copyright (c) 2010-2015 Institute for System Programming
 *                         of the Russian Academy of Sciences.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "sysemu/replay.h"

bool replay_exception(void)
{
    return true;
}

bool replay_has_exception(void)
{
    return false;
}

bool replay_interrupt(void)
{
    return true;
}

bool replay_has_interrupt(void)
{
    return false;
}

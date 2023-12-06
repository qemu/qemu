/*
 * Hosted file support for semihosting syscalls.
 *
 * Copyright (c) 2005, 2007 CodeSourcery.
 * Copyright (c) 2019 Linaro
 * Copyright © 2020 by Keith Packard <keithp@keithp.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "gdbstub/syscalls.h"
#include "semihosting/semihost.h"
#include "semihosting/guestfd.h"
#ifndef CONFIG_USER_ONLY
#include CONFIG_DEVICES
#endif

static GArray *guestfd_array;

#ifdef CONFIG_ARM_COMPATIBLE_SEMIHOSTING
GuestFD console_in_gf;
GuestFD console_out_gf;
#endif

void qemu_semihosting_guestfd_init(void)
{
    /* New entries zero-initialized, i.e. type GuestFDUnused */
    guestfd_array = g_array_new(FALSE, TRUE, sizeof(GuestFD));

#ifdef CONFIG_ARM_COMPATIBLE_SEMIHOSTING
    /* For ARM-compat, the console is in a separate namespace. */
    if (use_gdb_syscalls()) {
        console_in_gf.type = GuestFDGDB;
        console_in_gf.hostfd = 0;
        console_out_gf.type = GuestFDGDB;
        console_out_gf.hostfd = 2;
    } else {
        console_in_gf.type = GuestFDConsole;
        console_out_gf.type = GuestFDConsole;
    }
#else
    /* Otherwise, the stdio file descriptors apply. */
    guestfd_array = g_array_set_size(guestfd_array, 3);
#ifndef CONFIG_USER_ONLY
    if (!use_gdb_syscalls()) {
        GuestFD *gf = &g_array_index(guestfd_array, GuestFD, 0);
        gf[0].type = GuestFDConsole;
        gf[1].type = GuestFDConsole;
        gf[2].type = GuestFDConsole;
        return;
    }
#endif
    associate_guestfd(0, 0);
    associate_guestfd(1, 1);
    associate_guestfd(2, 2);
#endif
}

/*
 * Allocate a new guest file descriptor and return it; if we
 * couldn't allocate a new fd then return -1.
 * This is a fairly simplistic implementation because we don't
 * expect that most semihosting guest programs will make very
 * heavy use of opening and closing fds.
 */
int alloc_guestfd(void)
{
    guint i;

    /* SYS_OPEN should return nonzero handle on success. Start guestfd from 1 */
    for (i = 1; i < guestfd_array->len; i++) {
        GuestFD *gf = &g_array_index(guestfd_array, GuestFD, i);

        if (gf->type == GuestFDUnused) {
            return i;
        }
    }

    /* All elements already in use: expand the array */
    g_array_set_size(guestfd_array, i + 1);
    return i;
}

static void do_dealloc_guestfd(GuestFD *gf)
{
    gf->type = GuestFDUnused;
}

/*
 * Look up the guestfd in the data structure; return NULL
 * for out of bounds, but don't check whether the slot is unused.
 * This is used internally by the other guestfd functions.
 */
static GuestFD *do_get_guestfd(int guestfd)
{
    if (guestfd < 0 || guestfd >= guestfd_array->len) {
        return NULL;
    }

    return &g_array_index(guestfd_array, GuestFD, guestfd);
}

/*
 * Given a guest file descriptor, get the associated struct.
 * If the fd is not valid, return NULL. This is the function
 * used by the various semihosting calls to validate a handle
 * from the guest.
 * Note: calling alloc_guestfd() or dealloc_guestfd() will
 * invalidate any GuestFD* obtained by calling this function.
 */
GuestFD *get_guestfd(int guestfd)
{
    GuestFD *gf = do_get_guestfd(guestfd);

    if (!gf || gf->type == GuestFDUnused) {
        return NULL;
    }
    return gf;
}

/*
 * Associate the specified guest fd (which must have been
 * allocated via alloc_fd() and not previously used) with
 * the specified host/gdb fd.
 */
void associate_guestfd(int guestfd, int hostfd)
{
    GuestFD *gf = do_get_guestfd(guestfd);

    assert(gf);
    gf->type = use_gdb_syscalls() ? GuestFDGDB : GuestFDHost;
    gf->hostfd = hostfd;
}

void staticfile_guestfd(int guestfd, const uint8_t *data, size_t len)
{
    GuestFD *gf = do_get_guestfd(guestfd);

    assert(gf);
    gf->type = GuestFDStatic;
    gf->staticfile.data = data;
    gf->staticfile.len = len;
    gf->staticfile.off = 0;
}

/*
 * Deallocate the specified guest file descriptor. This doesn't
 * close the host fd, it merely undoes the work of alloc_fd().
 */
void dealloc_guestfd(int guestfd)
{
    GuestFD *gf = do_get_guestfd(guestfd);

    assert(gf);
    do_dealloc_guestfd(gf);
}

/*
 * FreeBSD file related system call shims and definitions
 *
 * Copyright (c) 2014 Stacey D. Son
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef FREEBSD_OS_FILE_H
#define FREEBSD_OS_FILE_H

#include <sys/specialfd.h>

/*
 * Asynchronous I/O.
 */

/* pipe2(2) */
static abi_long do_bsd_pipe2(CPUArchState *env, abi_ulong pipedes, int flags)
{
    int host_pipe[2];
    int host_ret = pipe2(host_pipe, flags);
    /* XXXss - flags should be translated from target to host. */

    if (host_ret == -1) {
        return get_errno(host_ret);
    }

    /*
     * pipe2() returns it's second FD by copying it back to userspace and not in
     * a second register like pipe(2): set_second_rval(env, host_pipe[1]);
     *
     * Copy the FD's back to userspace:
     */
    if (put_user_s32(host_pipe[0], pipedes) ||
        put_user_s32(host_pipe[1], pipedes + sizeof(host_pipe[0]))) {
        close(host_pipe[0]);
        close(host_pipe[1]);
        return -TARGET_EFAULT;
    }
    return 0;
}

/* chflagsat(2) */
static inline abi_long do_bsd_chflagsat(int fd, abi_ulong path,
        abi_ulong flags, int atflags)
{
    abi_long ret;
    void *p;

    LOCK_PATH(p, path);
    ret = get_errno(chflagsat(fd, p, flags, atflags)); /* XXX path(p)? */
    UNLOCK_PATH(p, path);

    return ret;
}

/* close_range(2) */
static inline abi_long do_freebsd_close_range(unsigned int lowfd,
    unsigned int highfd, int flags)
{

    return get_errno(close_range(lowfd, highfd, flags));
}

ssize_t safe_copy_file_range(int, off_t *, int, off_t *, size_t, unsigned int);

/* copy_file_range(2) */
static inline abi_long do_freebsd_copy_file_range(int infd,
    abi_ulong inofftp, int outfd, abi_ulong outofftp, size_t len,
    unsigned int flags)
{
    off_t inoff, outoff, *inp, *outp;
    abi_long ret;

    inp = outp = NULL;
    if (inofftp != 0 && !access_ok(VERIFY_WRITE, inofftp, sizeof(off_t))) {
        return -TARGET_EFAULT;
    } else if (inofftp != 0) {
        inoff = tswap64(*(off_t *)g2h_untagged(inofftp));
        inp = &inoff;
    }
    if (outofftp != 0 && !access_ok(VERIFY_WRITE, outofftp, sizeof(off_t))) {
        return -TARGET_EFAULT;
    } else if (outofftp != 0) {
        outoff = tswap64(*(off_t *)g2h_untagged(outofftp));
        outp = &outoff;
    }

    ret = get_errno(safe_copy_file_range(infd, inp, outfd, outp, len,
        flags));

    if (!is_error(ret)) {
        if (inofftp != 0) {
            *(off_t *)g2h_untagged(inofftp) = tswap64(inoff);
        }
        if (outofftp != 0) {
            *(off_t *)g2h_untagged(outofftp) = tswap64(outoff);
        }
    }
    return ret;
}

static inline abi_long do_freebsd___specialfd(int type, abi_ulong req,
    size_t len)
{
    abi_long ret;

    ret = -TARGET_EINVAL;
    switch (type) {
    case TARGET_SPECIALFD_EVENT: {
        struct specialfd_eventfd evfd;
        struct target_specialfd_eventfd *target_eventfd;

        if (!lock_user_struct(VERIFY_READ, target_eventfd, req, 1)) {
            return -TARGET_EFAULT;
        }

        evfd.initval = tswap32(target_eventfd->initval);
        evfd.flags = tswap32(target_eventfd->flags);
        ret = get_errno(syscall(SYS___specialfd, type, &evfd, sizeof(evfd)));
        unlock_user_struct(target_eventfd, req, 0);
        break;
    }
    }

    return ret;
}

#endif /* FREEBSD_OS_FILE_H */

/*
 * FreeBSD time related system call helpers
 *
 * Copyright (c) 2013-2015 Stacey D. Son
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"

#include <errno.h>
#include <time.h>
#include <sys/timex.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/umtx.h>

#include "qemu.h"
#include "qemu-os.h"

/*
 * FreeBSD time conversion functions
 */
abi_long t2h_freebsd_timeval(struct timeval *tv, abi_ulong target_tv_addr)
{
    struct target_freebsd_timeval *target_tv;

    if (!lock_user_struct(VERIFY_READ, target_tv, target_tv_addr, 1)) {
        return -TARGET_EFAULT;
    }
    __get_user(tv->tv_sec, &target_tv->tv_sec);
    __get_user(tv->tv_usec, &target_tv->tv_usec);
    unlock_user_struct(target_tv, target_tv_addr, 0);

    return 0;
}

abi_long h2t_freebsd_timeval(struct timeval *tv, abi_ulong target_tv_addr)
{
    struct target_freebsd_timeval *target_tv;

    if (!lock_user_struct(VERIFY_WRITE, target_tv, target_tv_addr, 0)) {
        return -TARGET_EFAULT;
    }
    __put_user(tv->tv_sec, &target_tv->tv_sec);
    __put_user(tv->tv_usec, &target_tv->tv_usec);
    unlock_user_struct(target_tv, target_tv_addr, 1);

    return 0;
}

abi_long t2h_freebsd_timespec(struct timespec *ts, abi_ulong target_ts_addr)
{
    struct target_freebsd_timespec *target_ts;

    if (!lock_user_struct(VERIFY_READ, target_ts, target_ts_addr, 1)) {
        return -TARGET_EFAULT;
    }
    __get_user(ts->tv_sec, &target_ts->tv_sec);
    __get_user(ts->tv_nsec, &target_ts->tv_nsec);
    unlock_user_struct(target_ts, target_ts_addr, 0);

    return 0;
}

abi_long h2t_freebsd_timespec(abi_ulong target_ts_addr, struct timespec *ts)
{
    struct target_freebsd_timespec *target_ts;

    if (!lock_user_struct(VERIFY_WRITE, target_ts, target_ts_addr, 0)) {
        return -TARGET_EFAULT;
    }
    __put_user(ts->tv_sec, &target_ts->tv_sec);
    __put_user(ts->tv_nsec, &target_ts->tv_nsec);
    unlock_user_struct(target_ts, target_ts_addr, 1);

    return 0;
}

abi_long t2h_freebsd_umtx_time(abi_ulong target_ut_addr,
        abi_ulong target_ut_size, void *host_t, size_t *host_tsz)
{
    abi_long ret;

    if (target_ut_size <= sizeof(struct target_freebsd_timespec)) {
        ret = t2h_freebsd_timespec((struct timespec *)host_t, target_ut_addr);
        if (ret == 0) {
            *host_tsz = sizeof(struct timespec);
        }
        return ret;
    } else {
        struct target_freebsd__umtx_time *target_ut;
        struct _umtx_time *ut = (struct _umtx_time *)host_t;

        if (!lock_user_struct(VERIFY_READ, target_ut, target_ut_addr, 1)) {
            return -TARGET_EFAULT;
        }
        if (t2h_freebsd_timespec(&ut->_timeout, h2g(&target_ut->_timeout))) {
            unlock_user_struct(target_ut, target_ut_addr, 0);
            return -TARGET_EFAULT;
        }
        __get_user(ut->_flags, &target_ut->_flags);
        __get_user(ut->_clockid, &target_ut->_clockid);
        unlock_user_struct(target_ut, target_ut_addr, 0);

        if (target_ut_size > sizeof(struct target_freebsd__umtx_time)) {
            *host_tsz = sizeof(struct _umtx_time) + sizeof(struct timespec);
        } else {
            *host_tsz = sizeof(struct _umtx_time);
        }

        return 0;
    }
}

abi_long t2h_freebsd_timex(struct timex *host_tx, abi_ulong target_tx_addr)
{
    struct target_freebsd_timex *target_tx;

    if (!lock_user_struct(VERIFY_READ, target_tx, target_tx_addr, 1)) {
        return -TARGET_EFAULT;
    }
    __get_user(host_tx->modes, &target_tx->modes);
    __get_user(host_tx->offset, &target_tx->offset);
    __get_user(host_tx->freq, &target_tx->freq);
    __get_user(host_tx->maxerror, &target_tx->maxerror);
    __get_user(host_tx->esterror, &target_tx->esterror);
    __get_user(host_tx->status, &target_tx->status);
    __get_user(host_tx->constant, &target_tx->constant);
    __get_user(host_tx->precision, &target_tx->precision);
    __get_user(host_tx->tolerance, &target_tx->tolerance);
    __get_user(host_tx->ppsfreq, &target_tx->ppsfreq);
    __get_user(host_tx->jitter, &target_tx->jitter);
    __get_user(host_tx->shift, &target_tx->shift);
    __get_user(host_tx->stabil, &target_tx->stabil);
    __get_user(host_tx->jitcnt, &target_tx->jitcnt);
    __get_user(host_tx->calcnt, &target_tx->calcnt);
    __get_user(host_tx->errcnt, &target_tx->errcnt);
    __get_user(host_tx->stbcnt, &target_tx->stbcnt);
    unlock_user_struct(target_tx, target_tx_addr, 0);

    return 0;
}

abi_long h2t_freebsd_timex(abi_ulong target_tx_addr, struct timex *host_tx)
{
    struct target_freebsd_timex *target_tx;

    if (!lock_user_struct(VERIFY_WRITE, target_tx, target_tx_addr, 0)) {
        return -TARGET_EFAULT;
    }
    __put_user(host_tx->modes, &target_tx->modes);
    __put_user(host_tx->offset, &target_tx->offset);
    __put_user(host_tx->freq, &target_tx->freq);
    __put_user(host_tx->maxerror, &target_tx->maxerror);
    __put_user(host_tx->esterror, &target_tx->esterror);
    __put_user(host_tx->status, &target_tx->status);
    __put_user(host_tx->constant, &target_tx->constant);
    __put_user(host_tx->precision, &target_tx->precision);
    __put_user(host_tx->tolerance, &target_tx->tolerance);
    __put_user(host_tx->ppsfreq, &target_tx->ppsfreq);
    __put_user(host_tx->jitter, &target_tx->jitter);
    __put_user(host_tx->shift, &target_tx->shift);
    __put_user(host_tx->stabil, &target_tx->stabil);
    __put_user(host_tx->jitcnt, &target_tx->jitcnt);
    __put_user(host_tx->calcnt, &target_tx->calcnt);
    __put_user(host_tx->errcnt, &target_tx->errcnt);
    __put_user(host_tx->stbcnt, &target_tx->stbcnt);
    unlock_user_struct(target_tx, target_tx_addr, 1);

    return 0;
}

abi_long h2t_freebsd_ntptimeval(abi_ulong target_ntv_addr,
        struct ntptimeval *ntv)
{
    struct target_freebsd_ntptimeval *target_ntv;

    if (!lock_user_struct(VERIFY_WRITE, target_ntv, target_ntv_addr, 0)) {
        return -TARGET_EFAULT;
    }
    __put_user(ntv->time.tv_sec, &target_ntv->time.tv_sec);
    __put_user(ntv->time.tv_nsec, &target_ntv->time.tv_nsec);
    __put_user(ntv->maxerror, &target_ntv->maxerror);
    __put_user(ntv->esterror, &target_ntv->esterror);
    __put_user(ntv->tai, &target_ntv->tai);
    __put_user(ntv->time_state, &target_ntv->time_state);
    unlock_user_struct(target_ntv, target_ntv_addr, 1);

    return 0;
}

/*
 * select(2) fdset copy functions
 */
abi_ulong copy_from_user_fdset(fd_set *fds, abi_ulong target_fds_addr, int n)
{
    int i, nw, j, k;
    abi_ulong b, *target_fds;

    nw = (n + TARGET_ABI_BITS - 1) / TARGET_ABI_BITS;
    target_fds = lock_user(VERIFY_READ, target_fds_addr,
            sizeof(abi_ulong) * nw, 1);
    if (target_fds == NULL) {
        return -TARGET_EFAULT;
    }
    FD_ZERO(fds);
    k = 0;
    for (i = 0; i < nw; i++) {
        /* grab the abi_ulong */
        __get_user(b, &target_fds[i]);
        for (j = 0; j < TARGET_ABI_BITS; j++) {
            /* check the bit inside the abi_ulong */
            if ((b >> j) & 1) {
                FD_SET(k, fds);
            }
            k++;
        }
    }
    unlock_user(target_fds, target_fds_addr, 0);

    return 0;
}

abi_ulong copy_from_user_fdset_ptr(fd_set *fds, fd_set **fds_ptr,
        abi_ulong target_fds_addr, int n)
{

    if (target_fds_addr) {
        if (copy_from_user_fdset(fds, target_fds_addr, n)) {
            return -TARGET_EFAULT;
        }
        *fds_ptr = fds;
    } else {
        *fds_ptr = NULL;
    }

    return 0;
}

abi_long copy_to_user_fdset(abi_ulong target_fds_addr, const fd_set *fds, int n)
{
    int i, nw, j, k;
    abi_long v;
    abi_ulong *target_fds;

    nw = (n + TARGET_ABI_BITS - 1) / TARGET_ABI_BITS;
    target_fds = lock_user(VERIFY_WRITE, target_fds_addr,
            sizeof(abi_ulong) * nw, 0);
    if (target_fds == NULL) {
        return -TARGET_EFAULT;
    }
    k = 0;
    for (i = 0; i < nw; i++) {
        v = 0;
        for (j = 0; j < TARGET_ABI_BITS; j++) {
            v |= ((FD_ISSET(k, fds) != 0) << j);
            k++;
        }
        __put_user(v, &target_fds[i]);
    }
    unlock_user(target_fds, target_fds_addr, sizeof(abi_ulong) * nw);

    return 0;
}

abi_int next_free_host_timer(void)
{
    int k ;
    /* FIXME: Does finding the next free slot require a lock? */
    for (k = 0; k < ARRAY_SIZE(g_posix_timers); k++) {
        if (g_posix_timers[k] == 0) {
            g_posix_timers[k] = 1;
            return k;
        }
    }
    return -1;
}

int host_to_target_timerid(int timerid)
{
    int k;

    for (k = 0; k < ARRAY_SIZE(g_posix_timers); k++) {
        if (g_posix_timers[k] == timerid) {
            return TIMER_MAGIC | k;
        }
    }

    return -1;
}

abi_long target_to_host_itimerspec(struct itimerspec *host_itspec,
                                                 abi_ulong target_addr)
{
    struct target_freebsd_itimerspec *target_itspec;

    if (!lock_user_struct(VERIFY_READ, target_itspec, target_addr, 1)) {
        return -TARGET_EFAULT;
    }

    host_itspec->it_interval.tv_sec =
                            tswapal(target_itspec->it_interval.tv_sec);
    host_itspec->it_interval.tv_nsec =
                            tswapal(target_itspec->it_interval.tv_nsec);
    host_itspec->it_value.tv_sec = tswapal(target_itspec->it_value.tv_sec);
    host_itspec->it_value.tv_nsec = tswapal(target_itspec->it_value.tv_nsec);

    unlock_user_struct(target_itspec, target_addr, 0);
    return 0;
}

abi_long host_to_target_itimerspec(abi_ulong target_addr,
                                               struct itimerspec *host_its)
{
    struct target_freebsd_itimerspec *target_itspec;

    if (!lock_user_struct(VERIFY_WRITE, target_itspec, target_addr, 0)) {
        return -TARGET_EFAULT;
    }

    target_itspec->it_interval.tv_sec = tswapal(host_its->it_interval.tv_sec);
    target_itspec->it_interval.tv_nsec = tswapal(host_its->it_interval.tv_nsec);

    target_itspec->it_value.tv_sec = tswapal(host_its->it_value.tv_sec);
    target_itspec->it_value.tv_nsec = tswapal(host_its->it_value.tv_nsec);

    unlock_user_struct(target_itspec, target_addr, 1);
    return 0;
}

/* Convert QEMU provided timer ID back to internal 16bit index format */
int get_timer_id(abi_long arg)
{
    int timerid = arg;

    if ((timerid & TIMER_MAGIC_MASK) != TIMER_MAGIC) {
        return -TARGET_EINVAL;
    }

    timerid &= 0xffff;

    if (timerid >= ARRAY_SIZE(g_posix_timers)) {
        return -TARGET_EINVAL;
    }

    return timerid;
}

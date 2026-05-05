/*
 * FreeBSD time related system call shims
 *
 * Copyright (c) 2013-2015 Stacey Son
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef FREEBSD_OS_TIME_H
#define FREEBSD_OS_TIME_H

#include <sys/types.h>
#include <sys/event.h>
#include <sys/select.h>
#include <sys/timex.h>
#include <poll.h>
#include <signal.h>
#include <time.h>

#include "qemu.h"
#include "qemu-os.h"

#include "bsd-socket.h"

int safe_clock_nanosleep(clockid_t clock_id, int flags,
     const struct timespec *rqtp, struct timespec *rmtp);
int safe_nanosleep(const struct timespec *rqtp, struct timespec *rmtp);
int safe_kevent(int, const struct kevent *, int, struct kevent *, int,
     const struct timespec *);

int __sys_ktimer_create(clockid_t, struct sigevent *restrict,
     int *restrict);
int __sys_ktimer_gettime(int, struct itimerspec *);
int __sys_ktimer_settime(int, int, const struct itimerspec *restrict,
     struct itimerspec *restrict);
int __sys_ktimer_delete(int);

/* nanosleep(2) */
static inline abi_long do_freebsd_nanosleep(abi_long arg1, abi_long arg2)
{
    abi_long ret;
    struct timespec req, rem;

    ret = t2h_freebsd_timespec(&req, arg1);
    if (!is_error(ret)) {
        ret = get_errno(safe_nanosleep(&req, &rem));
        if (ret == -TARGET_EINTR && arg2) {
            if (h2t_freebsd_timespec(arg2, &rem) != 0) {
                ret = -TARGET_EFAULT;
            }
        }
    }

    return ret;
}

/* clock_nanosleep(2) */
static inline abi_long do_freebsd_clock_nanosleep(abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4)
{
    struct timespec req, rem;
    abi_long ret;
    int clkid, flags;

    clkid = arg1;
    /* XXX Translate? */
    flags = arg2;
    ret = t2h_freebsd_timespec(&req, arg3);
    if (!is_error(ret)) {
        ret = get_errno(safe_clock_nanosleep(clkid, flags, &req, &rem));
        if (ret == -TARGET_EINTR && arg4) {
            if (h2t_freebsd_timespec(arg4, &rem) != 0) {
                ret = -TARGET_EFAULT;
            }
        }
    }

    return ret;
}

/* clock_gettime(2) */
static inline abi_long do_freebsd_clock_gettime(abi_long arg1, abi_long arg2)
{
    abi_long ret;
    struct timespec ts;

    ret = get_errno(clock_gettime(arg1, &ts));
    if (!is_error(ret)) {
        if (h2t_freebsd_timespec(arg2, &ts) != 0) {
            return -TARGET_EFAULT;
        }
    }

    return ret;
}

/* clock_settime(2) */
static inline abi_long do_freebsd_clock_settime(abi_long arg1, abi_long arg2)
{
    struct timespec ts;

    if (t2h_freebsd_timespec(&ts, arg2) != 0) {
        return -TARGET_EFAULT;
    }

    return get_errno(clock_settime(arg1, &ts));
}

/* clock_getres(2) */
static inline abi_long do_freebsd_clock_getres(abi_long arg1, abi_long arg2)
{
    abi_long ret;
    struct timespec ts;

    ret = get_errno(clock_getres(arg1, &ts));
    if (!is_error(ret)) {
        if (h2t_freebsd_timespec(arg2, &ts) != 0) {
            return -TARGET_EFAULT;
        }
    }

    return ret;
}

/* gettimeofday(2) */
static inline abi_long do_freebsd_gettimeofday(abi_ulong arg1, abi_ulong arg2)
{
    abi_long ret;
    struct timeval tv;
    struct timezone tz, *target_tz; /* XXX */

    if (arg2 != 0) {
        if (!lock_user_struct(VERIFY_READ, target_tz, arg2, 0)) {
            return -TARGET_EFAULT;
        }
        __get_user(tz.tz_minuteswest, &target_tz->tz_minuteswest);
        __get_user(tz.tz_dsttime, &target_tz->tz_dsttime);
        unlock_user_struct(target_tz, arg2, 1);
    }
    ret = get_errno(gettimeofday(&tv, arg2 != 0 ? &tz : NULL));
    if (!is_error(ret)) {
        if (h2t_freebsd_timeval(&tv, arg1)) {
            return -TARGET_EFAULT;
        }
    }

    return ret;
}

/* settimeofday(2) */
static inline abi_long do_freebsd_settimeofday(abi_long arg1, abi_long arg2)
{
    struct timeval tv;
    struct timezone tz, *target_tz; /* XXX */

    if (arg2 != 0) {
        if (!lock_user_struct(VERIFY_READ, target_tz, arg2, 0)) {
            return -TARGET_EFAULT;
        }
        __get_user(tz.tz_minuteswest, &target_tz->tz_minuteswest);
        __get_user(tz.tz_dsttime, &target_tz->tz_dsttime);
        unlock_user_struct(target_tz, arg2, 1);
    }
    if (t2h_freebsd_timeval(&tv, arg1)) {
        return -TARGET_EFAULT;
    }

    return get_errno(settimeofday(&tv, arg2 != 0 ? &tz : NULL));
}

/* adjtime(2) */
static inline abi_long do_freebsd_adjtime(abi_ulong target_delta_addr,
        abi_ulong target_old_addr)
{
    abi_long ret;
    struct timeval host_delta, host_old;

    ret = t2h_freebsd_timeval(&host_delta, target_delta_addr);
    if (is_error(ret)) {
        return ret;
    }

    if (target_old_addr) {
        ret = get_errno(adjtime(&host_delta, &host_old));
        if (is_error(ret)) {
            return ret;
        }
        ret = h2t_freebsd_timeval(&host_old, target_old_addr);
    } else {
        ret = get_errno(adjtime(&host_delta, NULL));
    }

    return ret;
}

/* ntp_adjtime(2) */
static inline abi_long do_freebsd_ntp_adjtime(abi_ulong target_tx_addr)
{
    abi_long ret;
    struct timex host_tx;

    ret = t2h_freebsd_timex(&host_tx, target_tx_addr);
    if (ret == 0) {
        ret = get_errno(ntp_adjtime(&host_tx));
    }

    return ret;
}

/* ntp_gettime(2) */
static inline abi_long do_freebsd_ntp_gettime(abi_ulong target_ntv_addr)
{
    abi_long ret;
    struct ntptimeval host_ntv;

    ret = get_errno(ntp_gettime(&host_ntv));
    if (!is_error(ret)) {
        ret = h2t_freebsd_ntptimeval(target_ntv_addr, &host_ntv);
    }

    return ret;
}


/* utimes(2) */
static inline abi_long do_freebsd_utimes(abi_long arg1, abi_long arg2)
{
    abi_long ret;
    void *p;
    struct timeval *tvp, tv[2];

    if (arg2 != 0) {
        if (t2h_freebsd_timeval(&tv[0], arg2) ||
                t2h_freebsd_timeval(&tv[1], arg2 +
                        sizeof(struct target_freebsd_timeval))) {
            return -TARGET_EFAULT;
        }
        tvp = tv;
    } else {
        tvp = NULL;
    }
    p = lock_user_string(arg1);
    if (p == NULL) {
        return -TARGET_EFAULT;
    }
    ret = get_errno(utimes(p, tvp));
    unlock_user(p, arg1, 0);

    return ret;
}

/* lutimes(2) */
static inline abi_long do_freebsd_lutimes(abi_long arg1, abi_long arg2)
{
    abi_long ret;
    void *p;
    struct timeval *tvp, tv[2];

    if (arg2 != 0) {
        if (t2h_freebsd_timeval(&tv[0], arg2) ||
                t2h_freebsd_timeval(&tv[1], arg2 +
                        sizeof(struct target_freebsd_timeval))) {
            return -TARGET_EFAULT;
        }
        tvp = tv;
    } else {
        tvp = NULL;
    }
    p = lock_user_string(arg1);
    if (p == NULL) {
        return -TARGET_EFAULT;
    }
    ret = get_errno(lutimes(p, tvp));
    unlock_user(p, arg1, 0);

    return ret;
}

/* futimes(2) */
static inline abi_long do_freebsd_futimes(abi_long arg1, abi_long arg2)
{
    struct timeval *tvp, tv[2];

    if (arg2 != 0) {
        if (t2h_freebsd_timeval(&tv[0], arg2) ||
                t2h_freebsd_timeval(&tv[1], arg2 +
                        sizeof(struct target_freebsd_timeval))) {
            return -TARGET_EFAULT;
        }
        tvp = tv;
    } else {
        tvp = NULL;
    }

    return get_errno(futimes(arg1, tvp));
}

/* futimesat(2) */
static inline abi_long do_freebsd_futimesat(abi_long arg1, abi_long arg2,
        abi_long arg3)
{
    abi_long ret;
    void *p;
    struct timeval *tvp, tv[2];

    if (arg3 != 0) {
        if (t2h_freebsd_timeval(&tv[0], arg3) ||
                t2h_freebsd_timeval(&tv[1], arg3 +
                        sizeof(struct target_freebsd_timeval))) {
            return -TARGET_EFAULT;
        }
        tvp = tv;
    } else {
        tvp = NULL;
    }

    p = lock_user_string(arg2);
    if (p == NULL) {
        return -TARGET_EFAULT;
    }
    ret = get_errno(futimesat(arg1, p, tvp));
    unlock_user(p, arg2, 0);

    return ret;
}

/* timer_create(2) */
static inline abi_long do_freebsd_ktimer_create(abi_long arg1, abi_long arg2,
        abi_long arg3)
{
    /* args: clockid_t clockid, struct sigevent *sevp, int *timerid */
    abi_long ret;

    struct sigevent host_sevp = { 0 }, *phost_sevp = NULL;

    int clkid = arg1;
    int timer_index = next_free_host_timer();

    if (timer_index < 0) {
        ret = -TARGET_EAGAIN;
    } else {
        int *phtimer = g_posix_timers  + timer_index;

        if (arg2) {
            phost_sevp = &host_sevp;
            ret = target_to_host_sigevent(phost_sevp, arg2);
            if (ret != 0) {
                g_posix_timers[timer_index] = 0;
                return -TARGET_EFAULT;
            }
        }

        ret = get_errno(__sys_ktimer_create(clkid, phost_sevp, phtimer));
        if (ret) {
            phtimer = NULL;
        } else {
            if (put_user(TIMER_MAGIC | timer_index, arg3, int)) {
                __sys_ktimer_delete(g_posix_timers[timer_index]);
                g_posix_timers[timer_index] = 0;
                ret = -TARGET_EFAULT;
            }
        }
    }
    return ret;
}

/* timer_delete(2) */
static inline abi_long do_freebsd_ktimer_delete(abi_long arg1)
{
    /* args: int timerid */
    abi_long ret;
    int timerid = get_timer_id(arg1);

    if (timerid < 0) {
        ret = timerid;
    } else {
        int htimer = g_posix_timers[timerid];
        ret = get_errno(__sys_ktimer_delete(htimer));
        g_posix_timers[timerid] = 0;
    }
    return ret;
}

/* timer_settime(2) */
static inline abi_long do_freebsd_ktimer_settime(abi_long arg1, abi_long arg2,
        abi_long arg3, abi_long arg4)
{
    /*
     * args: int timerid, int flags, const struct itimerspec *new_value,
     * struct itimerspec * old_value
     */
    abi_long ret;
    int timerid = get_timer_id(arg1);

    if (timerid < 0) {
        ret = timerid;
    } else if (arg3 == 0) {
        ret = -TARGET_EINVAL;
    } else {
        int htimer = g_posix_timers[timerid];
        struct itimerspec hspec_new = {{0},}, hspec_old = {{0},};

        if (target_to_host_itimerspec(&hspec_new, arg3)) {
            return -TARGET_EFAULT;
        }
        ret = get_errno(
            __sys_ktimer_settime(htimer, arg2, &hspec_new, &hspec_old));
        if (arg4 && host_to_target_itimerspec(arg4, &hspec_old)) {
            return -TARGET_EFAULT;
        }
    }
    return ret;
}

/* timer_gettime(2) */
static inline abi_long do_freebsd_ktimer_gettime(abi_long arg1, abi_long arg2)
{
    /* args: int timerid, struct itimerspec *curr_value */
    abi_long ret;
    int timerid = get_timer_id(arg1);

    if (timerid < 0) {
        ret = timerid;
    } else if (!arg2) {
        ret = -TARGET_EFAULT;
    } else {
        int htimer = g_posix_timers[timerid];
        struct itimerspec hspec;
        ret = get_errno(__sys_ktimer_gettime(htimer, &hspec));
        if (!is_error(ret)) {
            ret = host_to_target_itimerspec(arg2, &hspec);
        }
    }
    return ret;
}

/* select(2) */
static inline abi_long do_freebsd_select(CPUArchState *env, int n,
        abi_ulong rfd_addr, abi_ulong wfd_addr, abi_ulong efd_addr,
        abi_ulong target_tv_addr)
{
    fd_set rfds, wfds, efds;
    fd_set *rfds_ptr, *wfds_ptr, *efds_ptr;
    struct timeval tv, *tvp;
    abi_long ret, error;

    ret = copy_from_user_fdset_ptr(&rfds, &rfds_ptr, rfd_addr, n);
    if (ret != 0) {
        return ret;
    }
    ret = copy_from_user_fdset_ptr(&wfds, &wfds_ptr, wfd_addr, n);
    if (ret != 0) {
        return ret;
    }
    ret = copy_from_user_fdset_ptr(&efds, &efds_ptr, efd_addr, n);
    if (ret != 0) {
        return ret;
    }

    if (target_tv_addr != 0) {
        if (t2h_freebsd_timeval(&tv, target_tv_addr)) {
            return -TARGET_EFAULT;
        }
        tvp = &tv;
    } else {
        tvp = NULL;
    }

    ret = get_errno(safe_select(n, rfds_ptr, wfds_ptr, efds_ptr, tvp));

    if (!is_error(ret)) {
        if (rfd_addr != 0) {
            error = copy_to_user_fdset(rfd_addr, &rfds, n);
            if (error != 0) {
                return error;
            }
        }
        if (wfd_addr != 0) {
            error = copy_to_user_fdset(wfd_addr, &wfds, n);
            if (error != 0) {
                return error;
            }
        }
        if (efd_addr != 0) {
            error = copy_to_user_fdset(efd_addr, &efds, n);
            if (error != 0) {
                return error;
            }
        }
        if (target_tv_addr != 0) {
            error = h2t_freebsd_timeval(&tv, target_tv_addr);
            if (is_error(error)) {
                return error;
            }
        }
    }
    return ret;
}

/* pselect(2) */
static inline abi_long do_freebsd_pselect(CPUArchState *env, int n,
        abi_ulong rfd_addr, abi_ulong wfd_addr, abi_ulong efd_addr,
        abi_ulong ts_addr, abi_ulong set_addr)
{
    CPUState *cpu = env_cpu(env);
    TaskState *tstate = cpu->opaque;
    fd_set rfds, wfds, efds;
    fd_set *rfds_ptr, *wfds_ptr, *efds_ptr;
    sigset_t *set_ptr;
    struct timespec ts, *ts_ptr;
    void *p;
    abi_long ret, error;

    ret = copy_from_user_fdset_ptr(&rfds, &rfds_ptr, rfd_addr, n);
    if (is_error(ret)) {
        return ret;
    }
    ret = copy_from_user_fdset_ptr(&wfds, &wfds_ptr, wfd_addr, n);
    if (is_error(ret)) {
        return ret;
    }
    ret = copy_from_user_fdset_ptr(&efds, &efds_ptr, efd_addr, n);
    if (is_error(ret)) {
        return ret;
    }

    /* Unlike select(), pselect() uses struct timespec instead of timeval */
    if (ts_addr) {
        if (t2h_freebsd_timespec(&ts, ts_addr)) {
            return -TARGET_EFAULT;
        }
        ts_ptr = &ts;
    } else {
        ts_ptr = NULL;
    }

    if (set_addr != 0) {
        p = lock_user(VERIFY_READ, set_addr, sizeof(target_sigset_t), 1);
        if (p == NULL) {
            return -TARGET_EFAULT;
        }
        target_to_host_sigset(&tstate->sigsuspend_mask, p);
        unlock_user(p, set_addr, 0);
        set_ptr = &tstate->sigsuspend_mask;
    } else {
        set_ptr = NULL;
    }

    ret = get_errno(safe_pselect(n, rfds_ptr, wfds_ptr, efds_ptr, ts_ptr,
        set_ptr));
    if (ret != -TARGET_ERESTART)  {
        tstate->in_sigsuspend = true;
    }
    if (!is_error(ret)) {
        if (rfd_addr != 0) {
            error = copy_to_user_fdset(rfd_addr, &rfds, n);
            if (is_error(error)) {
                return error;
            }
        }
        if (wfd_addr != 0) {
            error = copy_to_user_fdset(wfd_addr, &wfds, n);
            if (is_error(error)) {
                return error;
            }
        }
        if (efd_addr != 0) {
            error = copy_to_user_fdset(efd_addr, &efds, n);
            if (is_error(error)) {
                return error;
            }
        }
    }
    return ret;
}

/* ppoll(2) */
static inline abi_long do_freebsd_ppoll(CPUArchState *env, abi_long arg1,
        abi_long arg2, abi_ulong arg3, abi_ulong arg4)
{
    CPUState *cpu = env_cpu(env);
    TaskState *tstate = cpu->opaque;
    abi_long ret;
    nfds_t i, nfds = arg2;
    struct pollfd *pfd;
    struct target_pollfd *target_pfd;
    struct timespec ts, *ts_ptr;
    sigset_t *set_ptr;
    void *p;

    target_pfd = lock_user(VERIFY_WRITE, arg1,
                           sizeof(struct target_pollfd) * nfds, 1);
    if (!target_pfd) {
        return -TARGET_EFAULT;
    }
    pfd = alloca(sizeof(struct pollfd) * nfds);
    for (i = 0; i < nfds; i++) {
        pfd[i].fd = tswap32(target_pfd[i].fd);
        pfd[i].events = tswap16(target_pfd[i].events);
    }

    /* Unlike poll(), ppoll() uses struct timespec. */
    if (arg3) {
        if (t2h_freebsd_timespec(&ts, arg3)) {
            return -TARGET_EFAULT;
        }
        ts_ptr = &ts;
    } else {
        ts_ptr = NULL;
    }

    if (arg4 != 0) {
        p = lock_user(VERIFY_READ, arg4, sizeof(target_sigset_t), 1);
        if (p == NULL) {
            return -TARGET_EFAULT;
        }
        target_to_host_sigset(&tstate->sigsuspend_mask, p);
        unlock_user(p, arg4, 0);
        set_ptr = &tstate->sigsuspend_mask;
    } else {
        set_ptr = NULL;
    }

    ret = get_errno(ppoll(pfd, nfds, ts_ptr, set_ptr));
    if (ret != -TARGET_ERESTART) {
        tstate->in_sigsuspend = true;
    }
    if (!is_error(ret)) {
        for (i = 0; i < nfds; i++) {
            target_pfd[i].revents = tswap16(pfd[i].revents);
        }
    }
    unlock_user(target_pfd, arg1, sizeof(struct target_pollfd) * nfds);

    return ret;
}

/* kqueue(2) */
static inline abi_long do_freebsd_kqueue(void)
{

    return get_errno(kqueue());
}

/* kevent(2) */
/* XXX Maybe some day, consolidate these two... */
static inline abi_long do_freebsd_freebsd11_kevent(abi_long arg1,
    abi_ulong arg2, abi_long arg3, abi_ulong arg4, abi_long arg5, abi_long arg6)
{
    abi_long ret;
    struct kevent *changelist = NULL, *eventlist = NULL;
    struct target_freebsd11_kevent *target_changelist, *target_eventlist;
    struct timespec ts;
    int i;

    if (arg3 != 0) {
        target_changelist = lock_user(VERIFY_READ, arg2,
                                      sizeof(*target_changelist) * arg3, 1);
        if (target_changelist == NULL) {
            return -TARGET_EFAULT;
        }

        changelist = alloca(sizeof(struct kevent) * arg3);
        memset(changelist, '\0', sizeof(struct kevent) * arg3);
        for (i = 0; i < arg3; i++) {
            __get_user(changelist[i].ident, &target_changelist[i].ident);
            __get_user(changelist[i].filter, &target_changelist[i].filter);
            __get_user(changelist[i].flags, &target_changelist[i].flags);
            __get_user(changelist[i].fflags, &target_changelist[i].fflags);
            __get_user(changelist[i].data, &target_changelist[i].data);
            /* __get_user(changelist[i].udata, &target_changelist[i].udata); */
#if TARGET_ABI_BITS == 32
            changelist[i].udata = (void *)(uintptr_t)target_changelist[i].udata;
            tswap32s((uint32_t *)&changelist[i].udata);
#else
            changelist[i].udata = (void *)(uintptr_t)target_changelist[i].udata;
            tswap64s((uint64_t *)&changelist[i].udata);
#endif
        }
        unlock_user(target_changelist, arg2, 0);
    }

    if (arg5 != 0) {
        eventlist = alloca(sizeof(struct kevent) * arg5);
    }
    if (arg6 != 0) {
        if (t2h_freebsd_timespec(&ts, arg6)) {
            return -TARGET_EFAULT;
        }
    }
    ret = get_errno(safe_kevent(arg1, changelist, arg3, eventlist, arg5,
                                arg6 != 0 ? &ts : NULL));

    if (arg5 == 0) {
        return ret;
    }

    if (!is_error(ret)) {
        target_eventlist = lock_user(VERIFY_WRITE, arg4,
                                     sizeof(*target_eventlist) * arg5, 0);
        if (target_eventlist == NULL) {
            return -TARGET_EFAULT;
        }
        for (i = 0; i < ret; i++) {
            __put_user(eventlist[i].ident, &target_eventlist[i].ident);
            __put_user(eventlist[i].filter, &target_eventlist[i].filter);
            __put_user(eventlist[i].flags, &target_eventlist[i].flags);
            __put_user(eventlist[i].fflags, &target_eventlist[i].fflags);
            __put_user(eventlist[i].data, &target_eventlist[i].data);
            /* __put_user(eventlist[i].udata, &target_eventlist[i].udata);*/
#if TARGET_ABI_BITS == 32
            tswap32s((uint32_t *)&eventlist[i].udata);
            target_eventlist[i].udata = (uintptr_t)eventlist[i].udata;
#else
            tswap64s((uint64_t *)&eventlist[i].udata);
            target_eventlist[i].udata = (uintptr_t)eventlist[i].udata;
#endif
        }
        unlock_user(target_eventlist, arg4,
                    sizeof(*target_eventlist) * ret);
    }
    return ret;
}

/* kevent(2) */
static inline abi_long do_freebsd_kevent(abi_long arg1, abi_ulong arg2,
        abi_long arg3, abi_ulong arg4, abi_long arg5, abi_long arg6)
{
    abi_long ret;
    struct kevent *changelist = NULL, *eventlist = NULL;
    struct target_freebsd_kevent *target_changelist, *target_eventlist;
    struct timespec ts;
    int i;

    if (arg3 != 0) {
        target_changelist = lock_user(VERIFY_READ, arg2,
                sizeof(struct target_freebsd_kevent) * arg3, 1);
        if (target_changelist == NULL) {
            return -TARGET_EFAULT;
        }

        changelist = alloca(sizeof(struct kevent) * arg3);
        for (i = 0; i < arg3; i++) {
            __get_user(changelist[i].ident, &target_changelist[i].ident);
            __get_user(changelist[i].filter, &target_changelist[i].filter);
            __get_user(changelist[i].flags, &target_changelist[i].flags);
            __get_user(changelist[i].fflags, &target_changelist[i].fflags);
            __get_user(changelist[i].data, &target_changelist[i].data);
            /* __get_user(changelist[i].udata, &target_changelist[i].udata); */
#if TARGET_ABI_BITS == 32
            changelist[i].udata = (void *)(uintptr_t)target_changelist[i].udata;
            tswap32s((uint32_t *)&changelist[i].udata);
#else
            changelist[i].udata = (void *)(uintptr_t)target_changelist[i].udata;
            tswap64s((uint64_t *)&changelist[i].udata);
#endif
            __get_user(changelist[i].ext[0], &target_changelist[i].ext[0]);
            __get_user(changelist[i].ext[1], &target_changelist[i].ext[1]);
            __get_user(changelist[i].ext[2], &target_changelist[i].ext[2]);
            __get_user(changelist[i].ext[3], &target_changelist[i].ext[3]);
        }
        unlock_user(target_changelist, arg2, 0);
    }

    if (arg5 != 0) {
        eventlist = alloca(sizeof(struct kevent) * arg5);
    }
    if (arg6 != 0) {
        if (t2h_freebsd_timespec(&ts, arg6)) {
            return -TARGET_EFAULT;
        }
    }
    ret = get_errno(safe_kevent(arg1, changelist, arg3, eventlist, arg5,
                                arg6 != 0 ? &ts : NULL));

    if (arg5 == 0) {
        return ret;
    }

    if (!is_error(ret)) {
        target_eventlist = lock_user(VERIFY_WRITE, arg4,
            sizeof(struct target_freebsd_kevent) * arg5, 0);
        if (target_eventlist == NULL) {
            return -TARGET_EFAULT;
        }
        for (i = 0; i < ret; i++) {
            __put_user(eventlist[i].ident, &target_eventlist[i].ident);
            __put_user(eventlist[i].filter, &target_eventlist[i].filter);
            __put_user(eventlist[i].flags, &target_eventlist[i].flags);
            __put_user(eventlist[i].fflags, &target_eventlist[i].fflags);
            __put_user(eventlist[i].data, &target_eventlist[i].data);
            /* __put_user(eventlist[i].udata, &target_eventlist[i].udata);*/
#if TARGET_ABI_BITS == 32
            tswap32s((uint32_t *)&eventlist[i].udata);
            target_eventlist[i].udata = (uintptr_t)eventlist[i].udata;
#else
            tswap64s((uint64_t *)&eventlist[i].udata);
            target_eventlist[i].udata = (uintptr_t)eventlist[i].udata;
#endif
            __put_user(eventlist[i].ext[0], &target_eventlist[i].ext[0]);
            __put_user(eventlist[i].ext[1], &target_eventlist[i].ext[1]);
            __put_user(eventlist[i].ext[2], &target_eventlist[i].ext[2]);
            __put_user(eventlist[i].ext[3], &target_eventlist[i].ext[3]);
        }
        unlock_user(target_eventlist, arg4,
                sizeof(struct target_freebsd_kevent) * ret);
    }
    return ret;
}

/* sigtimedwait(2) */

#endif /* FREEBSD_OS_TIME_H */

/*
 *  FreeBSD conversion extern declarations
 *
 *  Copyright (c) 2013 Stacey D. Son
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef QEMU_OS_H
#define QEMU_OS_H

/* qemu/osdep.h pulls in the rest */

#include <sys/acl.h>
#include <sys/mount.h>
#include <sys/timex.h>
#include <sys/rtprio.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>

struct freebsd11_stat;

/* os-time.c */
abi_long t2h_freebsd_timeval(struct timeval *tv, abi_ulong target_tv_addr);
abi_long h2t_freebsd_timeval(struct timeval *tv, abi_ulong target_tv_addr);

abi_long t2h_freebsd_timespec(struct timespec *ts, abi_ulong target_ts_addr);
abi_long h2t_freebsd_timespec(abi_ulong target_ts_addr, struct timespec *ts);

abi_long t2h_freebsd_umtx_time(abi_ulong target_ut_addr,
        abi_ulong target_ut_size, void *host_t, size_t *host_tsz);

abi_long t2h_freebsd_timex(struct timex *host_tx, abi_ulong target_tx_addr);
abi_long h2t_freebsd_timex(abi_ulong target_tx_addr, struct timex *host_tx);

abi_long h2t_freebsd_ntptimeval(abi_ulong target_ntv_addr,
        struct ntptimeval *ntv);

abi_ulong copy_from_user_fdset(fd_set *fds, abi_ulong target_fds_addr, int n);
abi_ulong copy_from_user_fdset_ptr(fd_set *fds, fd_set **fds_ptr,
        abi_ulong target_fds_addr, int n);
abi_long copy_to_user_fdset(abi_ulong target_fds_addr, const fd_set *fds,
        int n);

abi_int next_free_host_timer(void);
int host_to_target_timerid(int timerid);
abi_long target_to_host_itimerspec(struct itimerspec *host_itspec,
        abi_ulong target_addr);
abi_long host_to_target_itimerspec(abi_ulong target_addr,
        struct itimerspec *host_its);
int get_timer_id(abi_long arg);

/* os-socket.c */
abi_long t2h_freebsd_cmsg(struct msghdr *msgh,
                struct target_msghdr *target_msgh);
abi_long h2t_freebsd_cmsg(struct target_msghdr *target_msgh,
                struct msghdr *msgh);

/* os-thread.c */
abi_long t2h_freebsd_rtprio(struct rtprio *host_rtp, abi_ulong target_addr);
abi_long h2t_freebsd_rtprio(abi_ulong target_addr, struct rtprio *host_rtp);
abi_long do_freebsd_thr_new(CPUArchState *env, abi_ulong target_param_addr,
        int32_t param_size);

/* os-extattr.c */
struct acl;
abi_long t2h_freebsd_acl(struct acl *host_acl, abi_ulong target_addr);
abi_long h2t_freebsd_acl(abi_ulong target_addr, struct acl *host_acl);
abi_long t2h_freebsd_acl_type(acl_type_t *host_type, abi_long target_type);

/* os-stat.c */
abi_long h2t_freebsd11_stat(abi_ulong target_addr,
        struct freebsd11_stat *host_st);
abi_long h2t_freebsd11_nstat(abi_ulong target_addr,
        struct freebsd11_stat *host_st);
abi_long t2h_freebsd_fhandle(fhandle_t *host_fh, abi_ulong target_addr);
abi_long h2t_freebsd_fhandle(abi_ulong target_addr, fhandle_t *host_fh);
abi_long h2t_freebsd11_statfs(abi_ulong target_addr,
    struct freebsd11_statfs *host_statfs);
abi_long target_to_host_fcntl_cmd(int cmd);
abi_long h2t_freebsd_stat(abi_ulong target_addr,
        struct stat *host_st);
abi_long h2t_freebsd_statfs(abi_ulong target_addr,
    struct statfs *host_statfs);

#endif /* QEMU_OS_H */

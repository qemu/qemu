/*
 *  Linux syscalls
 * 
 *  Copyright (c) 2003 Fabrice Bellard
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
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <elf.h>
#include <endian.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <sys/swap.h>
#include <signal.h>
#include <sched.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/poll.h>
#include <sys/times.h>
#include <sys/shm.h>
#include <sys/statfs.h>
#include <utime.h>
#include <sys/sysinfo.h>
//#include <sys/user.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>

#define termios host_termios
#define winsize host_winsize
#define termio host_termio
#define sgttyb host_sgttyb /* same as target */
#define tchars host_tchars /* same as target */
#define ltchars host_ltchars /* same as target */

#include <linux/termios.h>
#include <linux/unistd.h>
#include <linux/utsname.h>
#include <linux/cdrom.h>
#include <linux/hdreg.h>
#include <linux/soundcard.h>
#include <linux/dirent.h>
#include <linux/kd.h>

#include "qemu.h"

//#define DEBUG

#if defined(TARGET_I386) || defined(TARGET_ARM) || defined(TARGET_SPARC)
/* 16 bit uid wrappers emulation */
#define USE_UID16
#endif

//#include <linux/msdos_fs.h>
#define	VFAT_IOCTL_READDIR_BOTH		_IOR('r', 1, struct dirent [2])
#define	VFAT_IOCTL_READDIR_SHORT	_IOR('r', 2, struct dirent [2])


#undef _syscall0
#undef _syscall1
#undef _syscall2
#undef _syscall3
#undef _syscall4
#undef _syscall5
#undef _syscall6

#define _syscall0(type,name)		\
type name (void)			\
{					\
	return syscall(__NR_##name);	\
}

#define _syscall1(type,name,type1,arg1)		\
type name (type1 arg1)				\
{						\
	return syscall(__NR_##name, arg1);	\
}

#define _syscall2(type,name,type1,arg1,type2,arg2)	\
type name (type1 arg1,type2 arg2)			\
{							\
	return syscall(__NR_##name, arg1, arg2);	\
}

#define _syscall3(type,name,type1,arg1,type2,arg2,type3,arg3)	\
type name (type1 arg1,type2 arg2,type3 arg3)			\
{								\
	return syscall(__NR_##name, arg1, arg2, arg3);		\
}

#define _syscall4(type,name,type1,arg1,type2,arg2,type3,arg3,type4,arg4)	\
type name (type1 arg1,type2 arg2,type3 arg3,type4 arg4)				\
{										\
	return syscall(__NR_##name, arg1, arg2, arg3, arg4);			\
}

#define _syscall5(type,name,type1,arg1,type2,arg2,type3,arg3,type4,arg4,	\
		  type5,arg5)							\
type name (type1 arg1,type2 arg2,type3 arg3,type4 arg4,type5 arg5)		\
{										\
	return syscall(__NR_##name, arg1, arg2, arg3, arg4, arg5);		\
}


#define _syscall6(type,name,type1,arg1,type2,arg2,type3,arg3,type4,arg4,	\
		  type5,arg5,type6,arg6)					\
type name (type1 arg1,type2 arg2,type3 arg3,type4 arg4,type5 arg5,type6 arg6)	\
{										\
	return syscall(__NR_##name, arg1, arg2, arg3, arg4, arg5, arg6);	\
}


#define __NR_sys_uname __NR_uname
#define __NR_sys_getcwd1 __NR_getcwd
#define __NR_sys_getdents __NR_getdents
#define __NR_sys_getdents64 __NR_getdents64
#define __NR_sys_rt_sigqueueinfo __NR_rt_sigqueueinfo

#if defined(__alpha__) || defined (__ia64__) || defined(__x86_64__)
#define __NR__llseek __NR_lseek
#endif

#ifdef __NR_gettid
_syscall0(int, gettid)
#else
static int gettid(void) {
    return -ENOSYS;
}
#endif
_syscall1(int,sys_uname,struct new_utsname *,buf)
_syscall2(int,sys_getcwd1,char *,buf,size_t,size)
_syscall3(int, sys_getdents, uint, fd, struct dirent *, dirp, uint, count);
_syscall3(int, sys_getdents64, uint, fd, struct dirent64 *, dirp, uint, count);
_syscall5(int, _llseek,  uint,  fd, ulong, hi, ulong, lo,
          loff_t *, res, uint, wh);
_syscall3(int,sys_rt_sigqueueinfo,int,pid,int,sig,siginfo_t *,uinfo)
#ifdef __NR_exit_group
_syscall1(int,exit_group,int,error_code)
#endif

extern int personality(int);
extern int flock(int, int);
extern int setfsuid(int);
extern int setfsgid(int);
extern int setresuid(uid_t, uid_t, uid_t);
extern int getresuid(uid_t *, uid_t *, uid_t *);
extern int setresgid(gid_t, gid_t, gid_t);
extern int getresgid(gid_t *, gid_t *, gid_t *);
extern int setgroups(int, gid_t *);

static inline long get_errno(long ret)
{
    if (ret == -1)
        return -errno;
    else
        return ret;
}

static inline int is_error(long ret)
{
    return (unsigned long)ret >= (unsigned long)(-4096);
}

static target_ulong target_brk;
static target_ulong target_original_brk;

void target_set_brk(target_ulong new_brk)
{
    target_original_brk = target_brk = new_brk;
}

long do_brk(target_ulong new_brk)
{
    target_ulong brk_page;
    long mapped_addr;
    int	new_alloc_size;

    if (!new_brk)
        return target_brk;
    if (new_brk < target_original_brk)
        return -ENOMEM;
    
    brk_page = HOST_PAGE_ALIGN(target_brk);

    /* If the new brk is less than this, set it and we're done... */
    if (new_brk < brk_page) {
	target_brk = new_brk;
    	return target_brk;
    }

    /* We need to allocate more memory after the brk... */
    new_alloc_size = HOST_PAGE_ALIGN(new_brk - brk_page + 1);
    mapped_addr = get_errno(target_mmap(brk_page, new_alloc_size, 
                                        PROT_READ|PROT_WRITE,
                                        MAP_ANON|MAP_FIXED|MAP_PRIVATE, 0, 0));
    if (is_error(mapped_addr)) {
	return mapped_addr;
    } else {
	target_brk = new_brk;
    	return target_brk;
    }
}

static inline fd_set *target_to_host_fds(fd_set *fds, 
                                         target_long *target_fds, int n)
{
#if !defined(BSWAP_NEEDED) && !defined(WORDS_BIGENDIAN)
    return (fd_set *)target_fds;
#else
    int i, b;
    if (target_fds) {
        FD_ZERO(fds);
        for(i = 0;i < n; i++) {
            b = (tswapl(target_fds[i / TARGET_LONG_BITS]) >>
                 (i & (TARGET_LONG_BITS - 1))) & 1;
            if (b)
                FD_SET(i, fds);
        }
        return fds;
    } else {
        return NULL;
    }
#endif
}

static inline void host_to_target_fds(target_long *target_fds, 
                                      fd_set *fds, int n)
{
#if !defined(BSWAP_NEEDED) && !defined(WORDS_BIGENDIAN)
    /* nothing to do */
#else
    int i, nw, j, k;
    target_long v;

    if (target_fds) {
        nw = (n + TARGET_LONG_BITS - 1) / TARGET_LONG_BITS;
        k = 0;
        for(i = 0;i < nw; i++) {
            v = 0;
            for(j = 0; j < TARGET_LONG_BITS; j++) {
                v |= ((FD_ISSET(k, fds) != 0) << j);
                k++;
            }
            target_fds[i] = tswapl(v);
        }
    }
#endif
}

#if defined(__alpha__)
#define HOST_HZ 1024
#else
#define HOST_HZ 100
#endif

static inline long host_to_target_clock_t(long ticks)
{
#if HOST_HZ == TARGET_HZ
    return ticks;
#else
    return ((int64_t)ticks * TARGET_HZ) / HOST_HZ;
#endif
}

static inline void host_to_target_rusage(target_ulong target_addr,
                                         const struct rusage *rusage)
{
    struct target_rusage *target_rusage;

    lock_user_struct(target_rusage, target_addr, 0);
    target_rusage->ru_utime.tv_sec = tswapl(rusage->ru_utime.tv_sec);
    target_rusage->ru_utime.tv_usec = tswapl(rusage->ru_utime.tv_usec);
    target_rusage->ru_stime.tv_sec = tswapl(rusage->ru_stime.tv_sec);
    target_rusage->ru_stime.tv_usec = tswapl(rusage->ru_stime.tv_usec);
    target_rusage->ru_maxrss = tswapl(rusage->ru_maxrss);
    target_rusage->ru_ixrss = tswapl(rusage->ru_ixrss);
    target_rusage->ru_idrss = tswapl(rusage->ru_idrss);
    target_rusage->ru_isrss = tswapl(rusage->ru_isrss);
    target_rusage->ru_minflt = tswapl(rusage->ru_minflt);
    target_rusage->ru_majflt = tswapl(rusage->ru_majflt);
    target_rusage->ru_nswap = tswapl(rusage->ru_nswap);
    target_rusage->ru_inblock = tswapl(rusage->ru_inblock);
    target_rusage->ru_oublock = tswapl(rusage->ru_oublock);
    target_rusage->ru_msgsnd = tswapl(rusage->ru_msgsnd);
    target_rusage->ru_msgrcv = tswapl(rusage->ru_msgrcv);
    target_rusage->ru_nsignals = tswapl(rusage->ru_nsignals);
    target_rusage->ru_nvcsw = tswapl(rusage->ru_nvcsw);
    target_rusage->ru_nivcsw = tswapl(rusage->ru_nivcsw);
    unlock_user_struct(target_rusage, target_addr, 1);
}

static inline void target_to_host_timeval(struct timeval *tv,
                                          target_ulong target_addr)
{
    struct target_timeval *target_tv;

    lock_user_struct(target_tv, target_addr, 1);
    tv->tv_sec = tswapl(target_tv->tv_sec);
    tv->tv_usec = tswapl(target_tv->tv_usec);
    unlock_user_struct(target_tv, target_addr, 0);
}

static inline void host_to_target_timeval(target_ulong target_addr,
                                          const struct timeval *tv)
{
    struct target_timeval *target_tv;

    lock_user_struct(target_tv, target_addr, 0);
    target_tv->tv_sec = tswapl(tv->tv_sec);
    target_tv->tv_usec = tswapl(tv->tv_usec);
    unlock_user_struct(target_tv, target_addr, 1);
}


static long do_select(long n, 
                      target_ulong rfd_p, target_ulong wfd_p, 
                      target_ulong efd_p, target_ulong target_tv)
{
    fd_set rfds, wfds, efds;
    fd_set *rfds_ptr, *wfds_ptr, *efds_ptr;
    target_long *target_rfds, *target_wfds, *target_efds;
    struct timeval tv, *tv_ptr;
    long ret;
    int ok;

    if (rfd_p) {
        target_rfds = lock_user(rfd_p, sizeof(target_long) * n, 1);
        rfds_ptr = target_to_host_fds(&rfds, target_rfds, n);
    } else {
        target_rfds = NULL;
        rfds_ptr = NULL;
    }
    if (wfd_p) {
        target_wfds = lock_user(wfd_p, sizeof(target_long) * n, 1);
        wfds_ptr = target_to_host_fds(&wfds, target_wfds, n);
    } else {
        target_wfds = NULL;
        wfds_ptr = NULL;
    }
    if (efd_p) {
        target_efds = lock_user(efd_p, sizeof(target_long) * n, 1);
        efds_ptr = target_to_host_fds(&efds, target_efds, n);
    } else {
        target_efds = NULL;
        efds_ptr = NULL;
    }
            
    if (target_tv) {
        target_to_host_timeval(&tv, target_tv);
        tv_ptr = &tv;
    } else {
        tv_ptr = NULL;
    }
    ret = get_errno(select(n, rfds_ptr, wfds_ptr, efds_ptr, tv_ptr));
    ok = !is_error(ret);

    if (ok) {
        host_to_target_fds(target_rfds, rfds_ptr, n);
        host_to_target_fds(target_wfds, wfds_ptr, n);
        host_to_target_fds(target_efds, efds_ptr, n);

        if (target_tv) {
            host_to_target_timeval(target_tv, &tv);
        }
    }
    if (target_rfds)
        unlock_user(target_rfds, rfd_p, ok ? sizeof(target_long) * n : 0);
    if (target_wfds)
        unlock_user(target_wfds, wfd_p, ok ? sizeof(target_long) * n : 0);
    if (target_efds)
        unlock_user(target_efds, efd_p, ok ? sizeof(target_long) * n : 0);

    return ret;
}

static inline void target_to_host_sockaddr(struct sockaddr *addr,
                                           target_ulong target_addr,
                                           socklen_t len)
{
    struct target_sockaddr *target_saddr;

    target_saddr = lock_user(target_addr, len, 1);
    memcpy(addr, target_saddr, len);
    addr->sa_family = tswap16(target_saddr->sa_family);
    unlock_user(target_saddr, target_addr, 0);
}

static inline void host_to_target_sockaddr(target_ulong target_addr,
                                           struct sockaddr *addr,
                                           socklen_t len)
{
    struct target_sockaddr *target_saddr;

    target_saddr = lock_user(target_addr, len, 0);
    memcpy(target_saddr, addr, len);
    target_saddr->sa_family = tswap16(addr->sa_family);
    unlock_user(target_saddr, target_addr, len);
}

/* ??? Should this also swap msgh->name?  */
static inline void target_to_host_cmsg(struct msghdr *msgh,
                                       struct target_msghdr *target_msgh)
{
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(msgh);
    struct target_cmsghdr *target_cmsg = TARGET_CMSG_FIRSTHDR(target_msgh);
    socklen_t space = 0;

    while (cmsg && target_cmsg) {
        void *data = CMSG_DATA(cmsg);
        void *target_data = TARGET_CMSG_DATA(target_cmsg);

        int len = tswapl(target_cmsg->cmsg_len) 
                  - TARGET_CMSG_ALIGN(sizeof (struct target_cmsghdr));

        space += CMSG_SPACE(len);
        if (space > msgh->msg_controllen) {
            space -= CMSG_SPACE(len);
            gemu_log("Host cmsg overflow\n");
            break;
        }

        cmsg->cmsg_level = tswap32(target_cmsg->cmsg_level);
        cmsg->cmsg_type = tswap32(target_cmsg->cmsg_type);
        cmsg->cmsg_len = CMSG_LEN(len);

        if (cmsg->cmsg_level != TARGET_SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) {
            gemu_log("Unsupported ancillary data: %d/%d\n", cmsg->cmsg_level, cmsg->cmsg_type);
            memcpy(data, target_data, len);
        } else {
            int *fd = (int *)data;
            int *target_fd = (int *)target_data;
            int i, numfds = len / sizeof(int);

            for (i = 0; i < numfds; i++)
                fd[i] = tswap32(target_fd[i]);
        }

        cmsg = CMSG_NXTHDR(msgh, cmsg);
        target_cmsg = TARGET_CMSG_NXTHDR(target_msgh, target_cmsg);
    }

    msgh->msg_controllen = space;
}

/* ??? Should this also swap msgh->name?  */
static inline void host_to_target_cmsg(struct target_msghdr *target_msgh,
                                       struct msghdr *msgh)
{
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(msgh);
    struct target_cmsghdr *target_cmsg = TARGET_CMSG_FIRSTHDR(target_msgh);
    socklen_t space = 0;

    while (cmsg && target_cmsg) {
        void *data = CMSG_DATA(cmsg);
        void *target_data = TARGET_CMSG_DATA(target_cmsg);

        int len = cmsg->cmsg_len - CMSG_ALIGN(sizeof (struct cmsghdr));

        space += TARGET_CMSG_SPACE(len);
        if (space > tswapl(target_msgh->msg_controllen)) {
            space -= TARGET_CMSG_SPACE(len);
            gemu_log("Target cmsg overflow\n");
            break;
        }

        target_cmsg->cmsg_level = tswap32(cmsg->cmsg_level);
        target_cmsg->cmsg_type = tswap32(cmsg->cmsg_type);
        target_cmsg->cmsg_len = tswapl(TARGET_CMSG_LEN(len));

        if (cmsg->cmsg_level != TARGET_SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) {
            gemu_log("Unsupported ancillary data: %d/%d\n", cmsg->cmsg_level, cmsg->cmsg_type);
            memcpy(target_data, data, len);
        } else {
            int *fd = (int *)data;
            int *target_fd = (int *)target_data;
            int i, numfds = len / sizeof(int);

            for (i = 0; i < numfds; i++)
                target_fd[i] = tswap32(fd[i]);
        }

        cmsg = CMSG_NXTHDR(msgh, cmsg);
        target_cmsg = TARGET_CMSG_NXTHDR(target_msgh, target_cmsg);
    }

    msgh->msg_controllen = tswapl(space);
}

static long do_setsockopt(int sockfd, int level, int optname, 
                          target_ulong optval, socklen_t optlen)
{
    int val, ret;
            
    switch(level) {
    case SOL_TCP:
        /* TCP options all take an 'int' value.  */
        if (optlen < sizeof(uint32_t))
            return -EINVAL;
        
        val = tget32(optval);
        ret = get_errno(setsockopt(sockfd, level, optname, &val, sizeof(val)));
        break;
    case SOL_IP:
        switch(optname) {
        case IP_TOS:
        case IP_TTL:
        case IP_HDRINCL:
        case IP_ROUTER_ALERT:
        case IP_RECVOPTS:
        case IP_RETOPTS:
        case IP_PKTINFO:
        case IP_MTU_DISCOVER:
        case IP_RECVERR:
        case IP_RECVTOS:
#ifdef IP_FREEBIND
        case IP_FREEBIND:
#endif
        case IP_MULTICAST_TTL:
        case IP_MULTICAST_LOOP:
            val = 0;
            if (optlen >= sizeof(uint32_t)) {
                val = tget32(optval);
            } else if (optlen >= 1) {
                val = tget8(optval);
            }
            ret = get_errno(setsockopt(sockfd, level, optname, &val, sizeof(val)));
            break;
        default:
            goto unimplemented;
        }
        break;
    case TARGET_SOL_SOCKET:
        switch (optname) {
            /* Options with 'int' argument.  */
        case TARGET_SO_DEBUG:
		optname = SO_DEBUG;
		break;
        case TARGET_SO_REUSEADDR:
		optname = SO_REUSEADDR;
		break;
        case TARGET_SO_TYPE:
		optname = SO_TYPE;
		break;
        case TARGET_SO_ERROR:
		optname = SO_ERROR;
		break;
        case TARGET_SO_DONTROUTE:
		optname = SO_DONTROUTE;
		break;
        case TARGET_SO_BROADCAST:
		optname = SO_BROADCAST;
		break;
        case TARGET_SO_SNDBUF:
		optname = SO_SNDBUF;
		break;
        case TARGET_SO_RCVBUF:
		optname = SO_RCVBUF;
		break;
        case TARGET_SO_KEEPALIVE:
		optname = SO_KEEPALIVE;
		break;
        case TARGET_SO_OOBINLINE:
		optname = SO_OOBINLINE;
		break;
        case TARGET_SO_NO_CHECK:
		optname = SO_NO_CHECK;
		break;
        case TARGET_SO_PRIORITY:
		optname = SO_PRIORITY;
		break;
#ifdef SO_BSDCOMPAT
        case TARGET_SO_BSDCOMPAT:
		optname = SO_BSDCOMPAT;
		break;
#endif
        case TARGET_SO_PASSCRED:
		optname = SO_PASSCRED;
		break;
        case TARGET_SO_TIMESTAMP:
		optname = SO_TIMESTAMP;
		break;
        case TARGET_SO_RCVLOWAT:
		optname = SO_RCVLOWAT;
		break;
        case TARGET_SO_RCVTIMEO:
		optname = SO_RCVTIMEO;
		break;
        case TARGET_SO_SNDTIMEO:
		optname = SO_SNDTIMEO;
		break;
            break;
        default:
            goto unimplemented;
        }
	if (optlen < sizeof(uint32_t))
	return -EINVAL;

	val = tget32(optval);
	ret = get_errno(setsockopt(sockfd, SOL_SOCKET, optname, &val, sizeof(val)));
        break;
    default:
    unimplemented:
        gemu_log("Unsupported setsockopt level=%d optname=%d \n", level, optname);
        ret = -ENOSYS;
    }
    return ret;
}

static long do_getsockopt(int sockfd, int level, int optname, 
                          target_ulong optval, target_ulong optlen)
{
    int len, lv, val, ret;

    switch(level) {
    case TARGET_SOL_SOCKET:
    	level = SOL_SOCKET;
	switch (optname) {
	case TARGET_SO_LINGER:
	case TARGET_SO_RCVTIMEO:
	case TARGET_SO_SNDTIMEO:
	case TARGET_SO_PEERCRED:
	case TARGET_SO_PEERNAME:
	    /* These don't just return a single integer */
	    goto unimplemented;
        default:
            goto int_case;
        }
        break;
    case SOL_TCP:
        /* TCP options all take an 'int' value.  */
    int_case:
        len = tget32(optlen);
        if (len < 0)
            return -EINVAL;
        lv = sizeof(int);
        ret = get_errno(getsockopt(sockfd, level, optname, &val, &lv));
        if (ret < 0)
            return ret;
        val = tswap32(val);
        if (len > lv)
            len = lv;
        if (len == 4)
            tput32(optval, val);
        else
            tput8(optval, val);
        tput32(optlen, len);
        break;
    case SOL_IP:
        switch(optname) {
        case IP_TOS:
        case IP_TTL:
        case IP_HDRINCL:
        case IP_ROUTER_ALERT:
        case IP_RECVOPTS:
        case IP_RETOPTS:
        case IP_PKTINFO:
        case IP_MTU_DISCOVER:
        case IP_RECVERR:
        case IP_RECVTOS:
#ifdef IP_FREEBIND
        case IP_FREEBIND:
#endif
        case IP_MULTICAST_TTL:
        case IP_MULTICAST_LOOP:
            len = tget32(optlen);
            if (len < 0)
                return -EINVAL;
            lv = sizeof(int);
            ret = get_errno(getsockopt(sockfd, level, optname, &val, &lv));
            if (ret < 0)
                return ret;
            if (len < sizeof(int) && len > 0 && val >= 0 && val < 255) {
                len = 1;
                tput32(optlen, len);
                tput8(optval, val);
            } else {
                if (len > sizeof(int))
                    len = sizeof(int);
                tput32(optlen, len);
                tput32(optval, val);
            }
            break;
        default:
            goto unimplemented;
        }
        break;
    default:
    unimplemented:
        gemu_log("getsockopt level=%d optname=%d not yet supported\n",
                 level, optname);
        ret = -ENOSYS;
        break;
    }
    return ret;
}

static void lock_iovec(struct iovec *vec, target_ulong target_addr,
                       int count, int copy)
{
    struct target_iovec *target_vec;
    target_ulong base;
    int i;

    target_vec = lock_user(target_addr, count * sizeof(struct target_iovec), 1);
    for(i = 0;i < count; i++) {
        base = tswapl(target_vec[i].iov_base);
        vec[i].iov_len = tswapl(target_vec[i].iov_len);
        vec[i].iov_base = lock_user(base, vec[i].iov_len, copy);
    }
    unlock_user (target_vec, target_addr, 0);
}

static void unlock_iovec(struct iovec *vec, target_ulong target_addr,
                         int count, int copy)
{
    struct target_iovec *target_vec;
    target_ulong base;
    int i;

    target_vec = lock_user(target_addr, count * sizeof(struct target_iovec), 1);
    for(i = 0;i < count; i++) {
        base = tswapl(target_vec[i].iov_base);
        unlock_user(vec[i].iov_base, base, copy ? vec[i].iov_len : 0);
    }
    unlock_user (target_vec, target_addr, 0);
}

static long do_socket(int domain, int type, int protocol)
{
#if defined(TARGET_MIPS)
    switch(type) {
    case TARGET_SOCK_DGRAM:
        type = SOCK_DGRAM;
        break;
    case TARGET_SOCK_STREAM:
        type = SOCK_STREAM;
        break;
    case TARGET_SOCK_RAW:
        type = SOCK_RAW;
        break;
    case TARGET_SOCK_RDM:
        type = SOCK_RDM;
        break;
    case TARGET_SOCK_SEQPACKET:
        type = SOCK_SEQPACKET;
        break;
    case TARGET_SOCK_PACKET:
        type = SOCK_PACKET;
        break;
    }
#endif
    return get_errno(socket(domain, type, protocol));
}

static long do_bind(int sockfd, target_ulong target_addr,
                    socklen_t addrlen)
{
    void *addr = alloca(addrlen);
    
    target_to_host_sockaddr(addr, target_addr, addrlen);
    return get_errno(bind(sockfd, addr, addrlen));
}

static long do_connect(int sockfd, target_ulong target_addr,
                    socklen_t addrlen)
{
    void *addr = alloca(addrlen);
    
    target_to_host_sockaddr(addr, target_addr, addrlen);
    return get_errno(connect(sockfd, addr, addrlen));
}

static long do_sendrecvmsg(int fd, target_ulong target_msg,
                           int flags, int send)
{
    long ret;
    struct target_msghdr *msgp;
    struct msghdr msg;
    int count;
    struct iovec *vec;
    target_ulong target_vec;

    lock_user_struct(msgp, target_msg, 1);
    if (msgp->msg_name) {
        msg.msg_namelen = tswap32(msgp->msg_namelen);
        msg.msg_name = alloca(msg.msg_namelen);
        target_to_host_sockaddr(msg.msg_name, tswapl(msgp->msg_name),
                                msg.msg_namelen);
    } else {
        msg.msg_name = NULL;
        msg.msg_namelen = 0;
    }
    msg.msg_controllen = 2 * tswapl(msgp->msg_controllen);
    msg.msg_control = alloca(msg.msg_controllen);
    msg.msg_flags = tswap32(msgp->msg_flags);
    
    count = tswapl(msgp->msg_iovlen);
    vec = alloca(count * sizeof(struct iovec));
    target_vec = tswapl(msgp->msg_iov);
    lock_iovec(vec, target_vec, count, send);
    msg.msg_iovlen = count;
    msg.msg_iov = vec;
    
    if (send) {
        target_to_host_cmsg(&msg, msgp);
        ret = get_errno(sendmsg(fd, &msg, flags));
    } else {
        ret = get_errno(recvmsg(fd, &msg, flags));
        if (!is_error(ret))
            host_to_target_cmsg(msgp, &msg);
    }
    unlock_iovec(vec, target_vec, count, !send);
    return ret;
}

static long do_socketcall(int num, target_ulong vptr)
{
    long ret;
    const int n = sizeof(target_ulong);

    switch(num) {
    case SOCKOP_socket:
	{
            int domain = tgetl(vptr);
            int type = tgetl(vptr + n);
            int protocol = tgetl(vptr + 2 * n);
            ret = do_socket(domain, type, protocol);
	}
        break;
    case SOCKOP_bind:
	{
            int sockfd = tgetl(vptr);
            target_ulong target_addr = tgetl(vptr + n);
            socklen_t addrlen = tgetl(vptr + 2 * n);
            ret = do_bind(sockfd, target_addr, addrlen);
        }
        break;
    case SOCKOP_connect:
        {
            int sockfd = tgetl(vptr);
            target_ulong target_addr = tgetl(vptr + n);
            socklen_t addrlen = tgetl(vptr + 2 * n);
            ret = do_connect(sockfd, target_addr, addrlen);
        }
        break;
    case SOCKOP_listen:
        {
            int sockfd = tgetl(vptr);
            int backlog = tgetl(vptr + n);
            ret = get_errno(listen(sockfd, backlog));
        }
        break;
    case SOCKOP_accept:
        {
            int sockfd = tgetl(vptr);
            target_ulong target_addr = tgetl(vptr + n);
            target_ulong target_addrlen = tgetl(vptr + 2 * n);
            socklen_t addrlen = tget32(target_addrlen);
            void *addr = alloca(addrlen);

            ret = get_errno(accept(sockfd, addr, &addrlen));
            if (!is_error(ret)) {
                host_to_target_sockaddr(target_addr, addr, addrlen);
                tput32(target_addrlen, addrlen);
            }
        }
        break;
    case SOCKOP_getsockname:
        {
            int sockfd = tgetl(vptr);
            target_ulong target_addr = tgetl(vptr + n);
            target_ulong target_addrlen = tgetl(vptr + 2 * n);
            socklen_t addrlen = tget32(target_addrlen);
            void *addr = alloca(addrlen);

            ret = get_errno(getsockname(sockfd, addr, &addrlen));
            if (!is_error(ret)) {
                host_to_target_sockaddr(target_addr, addr, addrlen);
                tput32(target_addrlen, addrlen);
            }
        }
        break;
    case SOCKOP_getpeername:
        {
            int sockfd = tgetl(vptr);
            target_ulong target_addr = tgetl(vptr + n);
            target_ulong target_addrlen = tgetl(vptr + 2 * n);
            socklen_t addrlen = tget32(target_addrlen);
            void *addr = alloca(addrlen);

            ret = get_errno(getpeername(sockfd, addr, &addrlen));
            if (!is_error(ret)) {
                host_to_target_sockaddr(target_addr, addr, addrlen);
                tput32(target_addrlen, addrlen);
            }
        }
        break;
    case SOCKOP_socketpair:
        {
            int domain = tgetl(vptr);
            int type = tgetl(vptr + n);
            int protocol = tgetl(vptr + 2 * n);
            target_ulong target_tab = tgetl(vptr + 3 * n);
            int tab[2];

            ret = get_errno(socketpair(domain, type, protocol, tab));
            if (!is_error(ret)) {
                tput32(target_tab, tab[0]);
                tput32(target_tab + 4, tab[1]);
            }
        }
        break;
    case SOCKOP_send:
        {
            int sockfd = tgetl(vptr);
            target_ulong msg = tgetl(vptr + n);
            size_t len = tgetl(vptr + 2 * n);
            int flags = tgetl(vptr + 3 * n);
            void *host_msg;

            host_msg = lock_user(msg, len, 1);
            ret = get_errno(send(sockfd, host_msg, len, flags));
            unlock_user(host_msg, msg, 0);
        }
        break;
    case SOCKOP_recv:
        {
            int sockfd = tgetl(vptr);
            target_ulong msg = tgetl(vptr + n);
            size_t len = tgetl(vptr + 2 * n);
            int flags = tgetl(vptr + 3 * n);
            void *host_msg;

            host_msg = lock_user(msg, len, 0);
            ret = get_errno(recv(sockfd, host_msg, len, flags));
            unlock_user(host_msg, msg, ret);
        }
        break;
    case SOCKOP_sendto:
        {
            int sockfd = tgetl(vptr);
            target_ulong msg = tgetl(vptr + n);
            size_t len = tgetl(vptr + 2 * n);
            int flags = tgetl(vptr + 3 * n);
            target_ulong target_addr = tgetl(vptr + 4 * n);
            socklen_t addrlen = tgetl(vptr + 5 * n);
            void *addr = alloca(addrlen);
            void *host_msg;

            host_msg = lock_user(msg, len, 1);
            target_to_host_sockaddr(addr, target_addr, addrlen);
            ret = get_errno(sendto(sockfd, host_msg, len, flags, addr, addrlen));
            unlock_user(host_msg, msg, 0);
        }
        break;
    case SOCKOP_recvfrom:
        {
            int sockfd = tgetl(vptr);
            target_ulong msg = tgetl(vptr + n);
            size_t len = tgetl(vptr + 2 * n);
            int flags = tgetl(vptr + 3 * n);
            target_ulong target_addr = tgetl(vptr + 4 * n);
            target_ulong target_addrlen = tgetl(vptr + 5 * n);
            socklen_t addrlen = tget32(target_addrlen);
            void *addr = alloca(addrlen);
            void *host_msg;

            host_msg = lock_user(msg, len, 0);
            ret = get_errno(recvfrom(sockfd, host_msg, len, flags, addr, &addrlen));
            if (!is_error(ret)) {
                host_to_target_sockaddr(target_addr, addr, addrlen);
                tput32(target_addrlen, addrlen);
                unlock_user(host_msg, msg, len);
            } else {
                unlock_user(host_msg, msg, 0);
            }
        }
        break;
    case SOCKOP_shutdown:
        {
            int sockfd = tgetl(vptr);
            int how = tgetl(vptr + n);

            ret = get_errno(shutdown(sockfd, how));
        }
        break;
    case SOCKOP_sendmsg:
    case SOCKOP_recvmsg:
        {
            int fd;
            target_ulong target_msg;
            int flags;

            fd = tgetl(vptr);
            target_msg = tgetl(vptr + n);
            flags = tgetl(vptr + 2 * n);

            ret = do_sendrecvmsg(fd, target_msg, flags, 
                                 (num == SOCKOP_sendmsg));
        }
        break;
    case SOCKOP_setsockopt:
        {
            int sockfd = tgetl(vptr);
            int level = tgetl(vptr + n);
            int optname = tgetl(vptr + 2 * n);
            target_ulong optval = tgetl(vptr + 3 * n);
            socklen_t optlen = tgetl(vptr + 4 * n);

            ret = do_setsockopt(sockfd, level, optname, optval, optlen);
        }
        break;
    case SOCKOP_getsockopt:
        {
            int sockfd = tgetl(vptr);
            int level = tgetl(vptr + n);
            int optname = tgetl(vptr + 2 * n);
            target_ulong optval = tgetl(vptr + 3 * n);
            target_ulong poptlen = tgetl(vptr + 4 * n);

            ret = do_getsockopt(sockfd, level, optname, optval, poptlen);
        }
        break;
    default:
        gemu_log("Unsupported socketcall: %d\n", num);
        ret = -ENOSYS;
        break;
    }
    return ret;
}

/* XXX: suppress this function and call directly the related socket
   functions */
static long do_socketcallwrapper(int num, long arg1, long arg2, long arg3,
				 long arg4, long arg5, long arg6)
{
    target_long args[6];

    tputl(args, arg1);
    tputl(args+1, arg2);
    tputl(args+2, arg3);
    tputl(args+3, arg4);
    tputl(args+4, arg5);
    tputl(args+5, arg6);

    return do_socketcall(num, (target_ulong) args);
}

#define N_SHM_REGIONS	32

static struct shm_region {
    uint32_t	start;
    uint32_t	size;
} shm_regions[N_SHM_REGIONS];

/* ??? This only works with linear mappings.  */
static long do_ipc(long call, long first, long second, long third,
		   long ptr, long fifth)
{
    int version;
    long ret = 0;
    unsigned long raddr;
    struct shmid_ds shm_info;
    int i;

    version = call >> 16;
    call &= 0xffff;

    switch (call) {
    case IPCOP_shmat:
	/* SHM_* flags are the same on all linux platforms */
	ret = get_errno((long) shmat(first, (void *) ptr, second));
        if (is_error(ret))
            break;
        raddr = ret;
	/* find out the length of the shared memory segment */
        
        ret = get_errno(shmctl(first, IPC_STAT, &shm_info));
        if (is_error(ret)) {
            /* can't get length, bail out */
            shmdt((void *) raddr);
	    break;
	}
	page_set_flags(raddr, raddr + shm_info.shm_segsz,
		       PAGE_VALID | PAGE_READ |
		       ((second & SHM_RDONLY)? 0: PAGE_WRITE));
	for (i = 0; i < N_SHM_REGIONS; ++i) {
	    if (shm_regions[i].start == 0) {
		shm_regions[i].start = raddr;
		shm_regions[i].size = shm_info.shm_segsz;
                break;
	    }
	}
	if (put_user(raddr, (uint32_t *)third))
            return -EFAULT;
        ret = 0;
	break;
    case IPCOP_shmdt:
	for (i = 0; i < N_SHM_REGIONS; ++i) {
	    if (shm_regions[i].start == ptr) {
		shm_regions[i].start = 0;
		page_set_flags(ptr, shm_regions[i].size, 0);
		break;
	    }
	}
	ret = get_errno(shmdt((void *) ptr));
	break;

    case IPCOP_shmget:
	/* IPC_* flag values are the same on all linux platforms */
	ret = get_errno(shmget(first, second, third));
	break;

	/* IPC_* and SHM_* command values are the same on all linux platforms */
    case IPCOP_shmctl:
        switch(second) {
        case IPC_RMID:
        case SHM_LOCK:
        case SHM_UNLOCK:
            ret = get_errno(shmctl(first, second, NULL));
            break;
        default:
            goto unimplemented;
        }
        break;
    default:
    unimplemented:
	gemu_log("Unsupported ipc call: %ld (version %d)\n", call, version);
	ret = -ENOSYS;
	break;
    }
    return ret;
}

/* kernel structure types definitions */
#define IFNAMSIZ        16

#define STRUCT(name, list...) STRUCT_ ## name,
#define STRUCT_SPECIAL(name) STRUCT_ ## name,
enum {
#include "syscall_types.h"
};
#undef STRUCT
#undef STRUCT_SPECIAL

#define STRUCT(name, list...) const argtype struct_ ## name ## _def[] = { list, TYPE_NULL };
#define STRUCT_SPECIAL(name)
#include "syscall_types.h"
#undef STRUCT
#undef STRUCT_SPECIAL

typedef struct IOCTLEntry {
    unsigned int target_cmd;
    unsigned int host_cmd;
    const char *name;
    int access;
    const argtype arg_type[5];
} IOCTLEntry;

#define IOC_R 0x0001
#define IOC_W 0x0002
#define IOC_RW (IOC_R | IOC_W)

#define MAX_STRUCT_SIZE 4096

IOCTLEntry ioctl_entries[] = {
#define IOCTL(cmd, access, types...) \
    { TARGET_ ## cmd, cmd, #cmd, access, { types } },
#include "ioctls.h"
    { 0, 0, },
};

/* ??? Implement proper locking for ioctls.  */
static long do_ioctl(long fd, long cmd, long arg)
{
    const IOCTLEntry *ie;
    const argtype *arg_type;
    long ret;
    uint8_t buf_temp[MAX_STRUCT_SIZE];
    int target_size;
    void *argptr;

    ie = ioctl_entries;
    for(;;) {
        if (ie->target_cmd == 0) {
            gemu_log("Unsupported ioctl: cmd=0x%04lx\n", cmd);
            return -ENOSYS;
        }
        if (ie->target_cmd == cmd)
            break;
        ie++;
    }
    arg_type = ie->arg_type;
#if defined(DEBUG)
    gemu_log("ioctl: cmd=0x%04lx (%s)\n", cmd, ie->name);
#endif
    switch(arg_type[0]) {
    case TYPE_NULL:
        /* no argument */
        ret = get_errno(ioctl(fd, ie->host_cmd));
        break;
    case TYPE_PTRVOID:
    case TYPE_INT:
        /* int argment */
        ret = get_errno(ioctl(fd, ie->host_cmd, arg));
        break;
    case TYPE_PTR:
        arg_type++;
        target_size = thunk_type_size(arg_type, 0);
        switch(ie->access) {
        case IOC_R:
            ret = get_errno(ioctl(fd, ie->host_cmd, buf_temp));
            if (!is_error(ret)) {
                argptr = lock_user(arg, target_size, 0);
                thunk_convert(argptr, buf_temp, arg_type, THUNK_TARGET);
                unlock_user(argptr, arg, target_size);
            }
            break;
        case IOC_W:
            argptr = lock_user(arg, target_size, 1);
            thunk_convert(buf_temp, argptr, arg_type, THUNK_HOST);
            unlock_user(argptr, arg, 0);
            ret = get_errno(ioctl(fd, ie->host_cmd, buf_temp));
            break;
        default:
        case IOC_RW:
            argptr = lock_user(arg, target_size, 1);
            thunk_convert(buf_temp, argptr, arg_type, THUNK_HOST);
            unlock_user(argptr, arg, 0);
            ret = get_errno(ioctl(fd, ie->host_cmd, buf_temp));
            if (!is_error(ret)) {
                argptr = lock_user(arg, target_size, 0);
                thunk_convert(argptr, buf_temp, arg_type, THUNK_TARGET);
                unlock_user(argptr, arg, target_size);
            }
            break;
        }
        break;
    default:
        gemu_log("Unsupported ioctl type: cmd=0x%04lx type=%d\n", cmd, arg_type[0]);
        ret = -ENOSYS;
        break;
    }
    return ret;
}

bitmask_transtbl iflag_tbl[] = {
        { TARGET_IGNBRK, TARGET_IGNBRK, IGNBRK, IGNBRK },
        { TARGET_BRKINT, TARGET_BRKINT, BRKINT, BRKINT },
        { TARGET_IGNPAR, TARGET_IGNPAR, IGNPAR, IGNPAR },
        { TARGET_PARMRK, TARGET_PARMRK, PARMRK, PARMRK },
        { TARGET_INPCK, TARGET_INPCK, INPCK, INPCK },
        { TARGET_ISTRIP, TARGET_ISTRIP, ISTRIP, ISTRIP },
        { TARGET_INLCR, TARGET_INLCR, INLCR, INLCR },
        { TARGET_IGNCR, TARGET_IGNCR, IGNCR, IGNCR },
        { TARGET_ICRNL, TARGET_ICRNL, ICRNL, ICRNL },
        { TARGET_IUCLC, TARGET_IUCLC, IUCLC, IUCLC },
        { TARGET_IXON, TARGET_IXON, IXON, IXON },
        { TARGET_IXANY, TARGET_IXANY, IXANY, IXANY },
        { TARGET_IXOFF, TARGET_IXOFF, IXOFF, IXOFF },
        { TARGET_IMAXBEL, TARGET_IMAXBEL, IMAXBEL, IMAXBEL },
        { 0, 0, 0, 0 }
};

bitmask_transtbl oflag_tbl[] = {
	{ TARGET_OPOST, TARGET_OPOST, OPOST, OPOST },
	{ TARGET_OLCUC, TARGET_OLCUC, OLCUC, OLCUC },
	{ TARGET_ONLCR, TARGET_ONLCR, ONLCR, ONLCR },
	{ TARGET_OCRNL, TARGET_OCRNL, OCRNL, OCRNL },
	{ TARGET_ONOCR, TARGET_ONOCR, ONOCR, ONOCR },
	{ TARGET_ONLRET, TARGET_ONLRET, ONLRET, ONLRET },
	{ TARGET_OFILL, TARGET_OFILL, OFILL, OFILL },
	{ TARGET_OFDEL, TARGET_OFDEL, OFDEL, OFDEL },
	{ TARGET_NLDLY, TARGET_NL0, NLDLY, NL0 },
	{ TARGET_NLDLY, TARGET_NL1, NLDLY, NL1 },
	{ TARGET_CRDLY, TARGET_CR0, CRDLY, CR0 },
	{ TARGET_CRDLY, TARGET_CR1, CRDLY, CR1 },
	{ TARGET_CRDLY, TARGET_CR2, CRDLY, CR2 },
	{ TARGET_CRDLY, TARGET_CR3, CRDLY, CR3 },
	{ TARGET_TABDLY, TARGET_TAB0, TABDLY, TAB0 },
	{ TARGET_TABDLY, TARGET_TAB1, TABDLY, TAB1 },
	{ TARGET_TABDLY, TARGET_TAB2, TABDLY, TAB2 },
	{ TARGET_TABDLY, TARGET_TAB3, TABDLY, TAB3 },
	{ TARGET_BSDLY, TARGET_BS0, BSDLY, BS0 },
	{ TARGET_BSDLY, TARGET_BS1, BSDLY, BS1 },
	{ TARGET_VTDLY, TARGET_VT0, VTDLY, VT0 },
	{ TARGET_VTDLY, TARGET_VT1, VTDLY, VT1 },
	{ TARGET_FFDLY, TARGET_FF0, FFDLY, FF0 },
	{ TARGET_FFDLY, TARGET_FF1, FFDLY, FF1 },
	{ 0, 0, 0, 0 }
};

bitmask_transtbl cflag_tbl[] = {
	{ TARGET_CBAUD, TARGET_B0, CBAUD, B0 },
	{ TARGET_CBAUD, TARGET_B50, CBAUD, B50 },
	{ TARGET_CBAUD, TARGET_B75, CBAUD, B75 },
	{ TARGET_CBAUD, TARGET_B110, CBAUD, B110 },
	{ TARGET_CBAUD, TARGET_B134, CBAUD, B134 },
	{ TARGET_CBAUD, TARGET_B150, CBAUD, B150 },
	{ TARGET_CBAUD, TARGET_B200, CBAUD, B200 },
	{ TARGET_CBAUD, TARGET_B300, CBAUD, B300 },
	{ TARGET_CBAUD, TARGET_B600, CBAUD, B600 },
	{ TARGET_CBAUD, TARGET_B1200, CBAUD, B1200 },
	{ TARGET_CBAUD, TARGET_B1800, CBAUD, B1800 },
	{ TARGET_CBAUD, TARGET_B2400, CBAUD, B2400 },
	{ TARGET_CBAUD, TARGET_B4800, CBAUD, B4800 },
	{ TARGET_CBAUD, TARGET_B9600, CBAUD, B9600 },
	{ TARGET_CBAUD, TARGET_B19200, CBAUD, B19200 },
	{ TARGET_CBAUD, TARGET_B38400, CBAUD, B38400 },
	{ TARGET_CBAUD, TARGET_B57600, CBAUD, B57600 },
	{ TARGET_CBAUD, TARGET_B115200, CBAUD, B115200 },
	{ TARGET_CBAUD, TARGET_B230400, CBAUD, B230400 },
	{ TARGET_CBAUD, TARGET_B460800, CBAUD, B460800 },
	{ TARGET_CSIZE, TARGET_CS5, CSIZE, CS5 },
	{ TARGET_CSIZE, TARGET_CS6, CSIZE, CS6 },
	{ TARGET_CSIZE, TARGET_CS7, CSIZE, CS7 },
	{ TARGET_CSIZE, TARGET_CS8, CSIZE, CS8 },
	{ TARGET_CSTOPB, TARGET_CSTOPB, CSTOPB, CSTOPB },
	{ TARGET_CREAD, TARGET_CREAD, CREAD, CREAD },
	{ TARGET_PARENB, TARGET_PARENB, PARENB, PARENB },
	{ TARGET_PARODD, TARGET_PARODD, PARODD, PARODD },
	{ TARGET_HUPCL, TARGET_HUPCL, HUPCL, HUPCL },
	{ TARGET_CLOCAL, TARGET_CLOCAL, CLOCAL, CLOCAL },
	{ TARGET_CRTSCTS, TARGET_CRTSCTS, CRTSCTS, CRTSCTS },
	{ 0, 0, 0, 0 }
};

bitmask_transtbl lflag_tbl[] = {
	{ TARGET_ISIG, TARGET_ISIG, ISIG, ISIG },
	{ TARGET_ICANON, TARGET_ICANON, ICANON, ICANON },
	{ TARGET_XCASE, TARGET_XCASE, XCASE, XCASE },
	{ TARGET_ECHO, TARGET_ECHO, ECHO, ECHO },
	{ TARGET_ECHOE, TARGET_ECHOE, ECHOE, ECHOE },
	{ TARGET_ECHOK, TARGET_ECHOK, ECHOK, ECHOK },
	{ TARGET_ECHONL, TARGET_ECHONL, ECHONL, ECHONL },
	{ TARGET_NOFLSH, TARGET_NOFLSH, NOFLSH, NOFLSH },
	{ TARGET_TOSTOP, TARGET_TOSTOP, TOSTOP, TOSTOP },
	{ TARGET_ECHOCTL, TARGET_ECHOCTL, ECHOCTL, ECHOCTL },
	{ TARGET_ECHOPRT, TARGET_ECHOPRT, ECHOPRT, ECHOPRT },
	{ TARGET_ECHOKE, TARGET_ECHOKE, ECHOKE, ECHOKE },
	{ TARGET_FLUSHO, TARGET_FLUSHO, FLUSHO, FLUSHO },
	{ TARGET_PENDIN, TARGET_PENDIN, PENDIN, PENDIN },
	{ TARGET_IEXTEN, TARGET_IEXTEN, IEXTEN, IEXTEN },
	{ 0, 0, 0, 0 }
};

static void target_to_host_termios (void *dst, const void *src)
{
    struct host_termios *host = dst;
    const struct target_termios *target = src;
    
    host->c_iflag = 
        target_to_host_bitmask(tswap32(target->c_iflag), iflag_tbl);
    host->c_oflag = 
        target_to_host_bitmask(tswap32(target->c_oflag), oflag_tbl);
    host->c_cflag = 
        target_to_host_bitmask(tswap32(target->c_cflag), cflag_tbl);
    host->c_lflag = 
        target_to_host_bitmask(tswap32(target->c_lflag), lflag_tbl);
    host->c_line = target->c_line;
    
    host->c_cc[VINTR] = target->c_cc[TARGET_VINTR]; 
    host->c_cc[VQUIT] = target->c_cc[TARGET_VQUIT]; 
    host->c_cc[VERASE] = target->c_cc[TARGET_VERASE];       
    host->c_cc[VKILL] = target->c_cc[TARGET_VKILL]; 
    host->c_cc[VEOF] = target->c_cc[TARGET_VEOF];   
    host->c_cc[VTIME] = target->c_cc[TARGET_VTIME]; 
    host->c_cc[VMIN] = target->c_cc[TARGET_VMIN];   
    host->c_cc[VSWTC] = target->c_cc[TARGET_VSWTC]; 
    host->c_cc[VSTART] = target->c_cc[TARGET_VSTART];       
    host->c_cc[VSTOP] = target->c_cc[TARGET_VSTOP]; 
    host->c_cc[VSUSP] = target->c_cc[TARGET_VSUSP]; 
    host->c_cc[VEOL] = target->c_cc[TARGET_VEOL];   
    host->c_cc[VREPRINT] = target->c_cc[TARGET_VREPRINT];   
    host->c_cc[VDISCARD] = target->c_cc[TARGET_VDISCARD];   
    host->c_cc[VWERASE] = target->c_cc[TARGET_VWERASE];     
    host->c_cc[VLNEXT] = target->c_cc[TARGET_VLNEXT];       
    host->c_cc[VEOL2] = target->c_cc[TARGET_VEOL2]; 
}
  
static void host_to_target_termios (void *dst, const void *src)
{
    struct target_termios *target = dst;
    const struct host_termios *host = src;

    target->c_iflag = 
        tswap32(host_to_target_bitmask(host->c_iflag, iflag_tbl));
    target->c_oflag = 
        tswap32(host_to_target_bitmask(host->c_oflag, oflag_tbl));
    target->c_cflag = 
        tswap32(host_to_target_bitmask(host->c_cflag, cflag_tbl));
    target->c_lflag = 
        tswap32(host_to_target_bitmask(host->c_lflag, lflag_tbl));
    target->c_line = host->c_line;
  
    target->c_cc[TARGET_VINTR] = host->c_cc[VINTR];
    target->c_cc[TARGET_VQUIT] = host->c_cc[VQUIT];
    target->c_cc[TARGET_VERASE] = host->c_cc[VERASE];
    target->c_cc[TARGET_VKILL] = host->c_cc[VKILL];
    target->c_cc[TARGET_VEOF] = host->c_cc[VEOF];
    target->c_cc[TARGET_VTIME] = host->c_cc[VTIME];
    target->c_cc[TARGET_VMIN] = host->c_cc[VMIN];
    target->c_cc[TARGET_VSWTC] = host->c_cc[VSWTC];
    target->c_cc[TARGET_VSTART] = host->c_cc[VSTART];
    target->c_cc[TARGET_VSTOP] = host->c_cc[VSTOP];
    target->c_cc[TARGET_VSUSP] = host->c_cc[VSUSP];
    target->c_cc[TARGET_VEOL] = host->c_cc[VEOL];
    target->c_cc[TARGET_VREPRINT] = host->c_cc[VREPRINT];
    target->c_cc[TARGET_VDISCARD] = host->c_cc[VDISCARD];
    target->c_cc[TARGET_VWERASE] = host->c_cc[VWERASE];
    target->c_cc[TARGET_VLNEXT] = host->c_cc[VLNEXT];
    target->c_cc[TARGET_VEOL2] = host->c_cc[VEOL2];
}

StructEntry struct_termios_def = {
    .convert = { host_to_target_termios, target_to_host_termios },
    .size = { sizeof(struct target_termios), sizeof(struct host_termios) },
    .align = { __alignof__(struct target_termios), __alignof__(struct host_termios) },
};

static bitmask_transtbl mmap_flags_tbl[] = {
	{ TARGET_MAP_SHARED, TARGET_MAP_SHARED, MAP_SHARED, MAP_SHARED },
	{ TARGET_MAP_PRIVATE, TARGET_MAP_PRIVATE, MAP_PRIVATE, MAP_PRIVATE },
	{ TARGET_MAP_FIXED, TARGET_MAP_FIXED, MAP_FIXED, MAP_FIXED },
	{ TARGET_MAP_ANONYMOUS, TARGET_MAP_ANONYMOUS, MAP_ANONYMOUS, MAP_ANONYMOUS },
	{ TARGET_MAP_GROWSDOWN, TARGET_MAP_GROWSDOWN, MAP_GROWSDOWN, MAP_GROWSDOWN },
	{ TARGET_MAP_DENYWRITE, TARGET_MAP_DENYWRITE, MAP_DENYWRITE, MAP_DENYWRITE },
	{ TARGET_MAP_EXECUTABLE, TARGET_MAP_EXECUTABLE, MAP_EXECUTABLE, MAP_EXECUTABLE },
	{ TARGET_MAP_LOCKED, TARGET_MAP_LOCKED, MAP_LOCKED, MAP_LOCKED },
	{ 0, 0, 0, 0 }
};

static bitmask_transtbl fcntl_flags_tbl[] = {
	{ TARGET_O_ACCMODE,   TARGET_O_WRONLY,    O_ACCMODE,   O_WRONLY,    },
	{ TARGET_O_ACCMODE,   TARGET_O_RDWR,      O_ACCMODE,   O_RDWR,      },
	{ TARGET_O_CREAT,     TARGET_O_CREAT,     O_CREAT,     O_CREAT,     },
	{ TARGET_O_EXCL,      TARGET_O_EXCL,      O_EXCL,      O_EXCL,      },
	{ TARGET_O_NOCTTY,    TARGET_O_NOCTTY,    O_NOCTTY,    O_NOCTTY,    },
	{ TARGET_O_TRUNC,     TARGET_O_TRUNC,     O_TRUNC,     O_TRUNC,     },
	{ TARGET_O_APPEND,    TARGET_O_APPEND,    O_APPEND,    O_APPEND,    },
	{ TARGET_O_NONBLOCK,  TARGET_O_NONBLOCK,  O_NONBLOCK,  O_NONBLOCK,  },
	{ TARGET_O_SYNC,      TARGET_O_SYNC,      O_SYNC,      O_SYNC,      },
	{ TARGET_FASYNC,      TARGET_FASYNC,      FASYNC,      FASYNC,      },
	{ TARGET_O_DIRECTORY, TARGET_O_DIRECTORY, O_DIRECTORY, O_DIRECTORY, },
	{ TARGET_O_NOFOLLOW,  TARGET_O_NOFOLLOW,  O_NOFOLLOW,  O_NOFOLLOW,  },
	{ TARGET_O_LARGEFILE, TARGET_O_LARGEFILE, O_LARGEFILE, O_LARGEFILE, },
#if defined(O_DIRECT)
	{ TARGET_O_DIRECT,    TARGET_O_DIRECT,    O_DIRECT,    O_DIRECT,    },
#endif
	{ 0, 0, 0, 0 }
};

#if defined(TARGET_I386)

/* NOTE: there is really one LDT for all the threads */
uint8_t *ldt_table;

static int read_ldt(target_ulong ptr, unsigned long bytecount)
{
    int size;
    void *p;

    if (!ldt_table)
        return 0;
    size = TARGET_LDT_ENTRIES * TARGET_LDT_ENTRY_SIZE;
    if (size > bytecount)
        size = bytecount;
    p = lock_user(ptr, size, 0);
    /* ??? Shoudl this by byteswapped?  */
    memcpy(p, ldt_table, size);
    unlock_user(p, ptr, size);
    return size;
}

/* XXX: add locking support */
static int write_ldt(CPUX86State *env, 
                     target_ulong ptr, unsigned long bytecount, int oldmode)
{
    struct target_modify_ldt_ldt_s ldt_info;
    struct target_modify_ldt_ldt_s *target_ldt_info;
    int seg_32bit, contents, read_exec_only, limit_in_pages;
    int seg_not_present, useable;
    uint32_t *lp, entry_1, entry_2;

    if (bytecount != sizeof(ldt_info))
        return -EINVAL;
    lock_user_struct(target_ldt_info, ptr, 1);
    ldt_info.entry_number = tswap32(target_ldt_info->entry_number);
    ldt_info.base_addr = tswapl(target_ldt_info->base_addr);
    ldt_info.limit = tswap32(target_ldt_info->limit);
    ldt_info.flags = tswap32(target_ldt_info->flags);
    unlock_user_struct(target_ldt_info, ptr, 0);
    
    if (ldt_info.entry_number >= TARGET_LDT_ENTRIES)
        return -EINVAL;
    seg_32bit = ldt_info.flags & 1;
    contents = (ldt_info.flags >> 1) & 3;
    read_exec_only = (ldt_info.flags >> 3) & 1;
    limit_in_pages = (ldt_info.flags >> 4) & 1;
    seg_not_present = (ldt_info.flags >> 5) & 1;
    useable = (ldt_info.flags >> 6) & 1;

    if (contents == 3) {
        if (oldmode)
            return -EINVAL;
        if (seg_not_present == 0)
            return -EINVAL;
    }
    /* allocate the LDT */
    if (!ldt_table) {
        ldt_table = malloc(TARGET_LDT_ENTRIES * TARGET_LDT_ENTRY_SIZE);
        if (!ldt_table)
            return -ENOMEM;
        memset(ldt_table, 0, TARGET_LDT_ENTRIES * TARGET_LDT_ENTRY_SIZE);
        env->ldt.base = h2g(ldt_table);
        env->ldt.limit = 0xffff;
    }

    /* NOTE: same code as Linux kernel */
    /* Allow LDTs to be cleared by the user. */
    if (ldt_info.base_addr == 0 && ldt_info.limit == 0) {
        if (oldmode ||
            (contents == 0		&&
             read_exec_only == 1	&&
             seg_32bit == 0		&&
             limit_in_pages == 0	&&
             seg_not_present == 1	&&
             useable == 0 )) {
            entry_1 = 0;
            entry_2 = 0;
            goto install;
        }
    }
    
    entry_1 = ((ldt_info.base_addr & 0x0000ffff) << 16) |
        (ldt_info.limit & 0x0ffff);
    entry_2 = (ldt_info.base_addr & 0xff000000) |
        ((ldt_info.base_addr & 0x00ff0000) >> 16) |
        (ldt_info.limit & 0xf0000) |
        ((read_exec_only ^ 1) << 9) |
        (contents << 10) |
        ((seg_not_present ^ 1) << 15) |
        (seg_32bit << 22) |
        (limit_in_pages << 23) |
        0x7000;
    if (!oldmode)
        entry_2 |= (useable << 20);

    /* Install the new entry ...  */
install:
    lp = (uint32_t *)(ldt_table + (ldt_info.entry_number << 3));
    lp[0] = tswap32(entry_1);
    lp[1] = tswap32(entry_2);
    return 0;
}

/* specific and weird i386 syscalls */
int do_modify_ldt(CPUX86State *env, int func, target_ulong ptr, unsigned long bytecount)
{
    int ret = -ENOSYS;
    
    switch (func) {
    case 0:
        ret = read_ldt(ptr, bytecount);
        break;
    case 1:
        ret = write_ldt(env, ptr, bytecount, 1);
        break;
    case 0x11:
        ret = write_ldt(env, ptr, bytecount, 0);
        break;
    }
    return ret;
}

#endif /* defined(TARGET_I386) */

/* this stack is the equivalent of the kernel stack associated with a
   thread/process */
#define NEW_STACK_SIZE 8192

static int clone_func(void *arg)
{
    CPUState *env = arg;
    cpu_loop(env);
    /* never exits */
    return 0;
}

int do_fork(CPUState *env, unsigned int flags, unsigned long newsp)
{
    int ret;
    TaskState *ts;
    uint8_t *new_stack;
    CPUState *new_env;
    
    if (flags & CLONE_VM) {
        ts = malloc(sizeof(TaskState) + NEW_STACK_SIZE);
        memset(ts, 0, sizeof(TaskState));
        new_stack = ts->stack;
        ts->used = 1;
        /* add in task state list */
        ts->next = first_task_state;
        first_task_state = ts;
        /* we create a new CPU instance. */
        new_env = cpu_init();
        memcpy(new_env, env, sizeof(CPUState));
#if defined(TARGET_I386)
        if (!newsp)
            newsp = env->regs[R_ESP];
        new_env->regs[R_ESP] = newsp;
        new_env->regs[R_EAX] = 0;
#elif defined(TARGET_ARM)
        if (!newsp)
            newsp = env->regs[13];
        new_env->regs[13] = newsp;
        new_env->regs[0] = 0;
#elif defined(TARGET_SPARC)
        if (!newsp)
            newsp = env->regwptr[22];
        new_env->regwptr[22] = newsp;
        new_env->regwptr[0] = 0;
	/* XXXXX */
        printf ("HELPME: %s:%d\n", __FILE__, __LINE__);
#elif defined(TARGET_MIPS)
        printf ("HELPME: %s:%d\n", __FILE__, __LINE__);
#elif defined(TARGET_PPC)
        if (!newsp)
            newsp = env->gpr[1];
        new_env->gpr[1] = newsp;
        { 
            int i;
            for (i = 7; i < 32; i++)
                new_env->gpr[i] = 0;
        }
#elif defined(TARGET_SH4)
	if (!newsp)
	  newsp = env->gregs[15];
	new_env->gregs[15] = newsp;
	/* XXXXX */
#else
#error unsupported target CPU
#endif
        new_env->opaque = ts;
#ifdef __ia64__
        ret = __clone2(clone_func, new_stack + NEW_STACK_SIZE, flags, new_env);
#else
	ret = clone(clone_func, new_stack + NEW_STACK_SIZE, flags, new_env);
#endif
    } else {
        /* if no CLONE_VM, we consider it is a fork */
        if ((flags & ~CSIGNAL) != 0)
            return -EINVAL;
        ret = fork();
    }
    return ret;
}

static long do_fcntl(int fd, int cmd, target_ulong arg)
{
    struct flock fl;
    struct target_flock *target_fl;
    long ret;

    switch(cmd) {
    case TARGET_F_GETLK:
        ret = fcntl(fd, cmd, &fl);
        if (ret == 0) {
            lock_user_struct(target_fl, arg, 0);
            target_fl->l_type = tswap16(fl.l_type);
            target_fl->l_whence = tswap16(fl.l_whence);
            target_fl->l_start = tswapl(fl.l_start);
            target_fl->l_len = tswapl(fl.l_len);
            target_fl->l_pid = tswapl(fl.l_pid);
            unlock_user_struct(target_fl, arg, 1);
        }
        break;
        
    case TARGET_F_SETLK:
    case TARGET_F_SETLKW:
        lock_user_struct(target_fl, arg, 1);
        fl.l_type = tswap16(target_fl->l_type);
        fl.l_whence = tswap16(target_fl->l_whence);
        fl.l_start = tswapl(target_fl->l_start);
        fl.l_len = tswapl(target_fl->l_len);
        fl.l_pid = tswapl(target_fl->l_pid);
        unlock_user_struct(target_fl, arg, 0);
        ret = fcntl(fd, cmd, &fl);
        break;
        
    case TARGET_F_GETLK64:
    case TARGET_F_SETLK64:
    case TARGET_F_SETLKW64:
        ret = -1;
        errno = EINVAL;
        break;

    case F_GETFL:
        ret = fcntl(fd, cmd, arg);
        ret = host_to_target_bitmask(ret, fcntl_flags_tbl);
        break;

    case F_SETFL:
        ret = fcntl(fd, cmd, target_to_host_bitmask(arg, fcntl_flags_tbl));
        break;

    default:
        ret = fcntl(fd, cmd, arg);
        break;
    }
    return ret;
}

#ifdef USE_UID16

static inline int high2lowuid(int uid)
{
    if (uid > 65535)
        return 65534;
    else
        return uid;
}

static inline int high2lowgid(int gid)
{
    if (gid > 65535)
        return 65534;
    else
        return gid;
}

static inline int low2highuid(int uid)
{
    if ((int16_t)uid == -1)
        return -1;
    else
        return uid;
}

static inline int low2highgid(int gid)
{
    if ((int16_t)gid == -1)
        return -1;
    else
        return gid;
}

#endif /* USE_UID16 */

void syscall_init(void)
{
    IOCTLEntry *ie;
    const argtype *arg_type;
    int size;

#define STRUCT(name, list...) thunk_register_struct(STRUCT_ ## name, #name, struct_ ## name ## _def); 
#define STRUCT_SPECIAL(name) thunk_register_struct_direct(STRUCT_ ## name, #name, &struct_ ## name ## _def); 
#include "syscall_types.h"
#undef STRUCT
#undef STRUCT_SPECIAL

    /* we patch the ioctl size if necessary. We rely on the fact that
       no ioctl has all the bits at '1' in the size field */
    ie = ioctl_entries;
    while (ie->target_cmd != 0) {
        if (((ie->target_cmd >> TARGET_IOC_SIZESHIFT) & TARGET_IOC_SIZEMASK) ==
            TARGET_IOC_SIZEMASK) {
            arg_type = ie->arg_type;
            if (arg_type[0] != TYPE_PTR) {
                fprintf(stderr, "cannot patch size for ioctl 0x%x\n", 
                        ie->target_cmd);
                exit(1);
            }
            arg_type++;
            size = thunk_type_size(arg_type, 0);
            ie->target_cmd = (ie->target_cmd & 
                              ~(TARGET_IOC_SIZEMASK << TARGET_IOC_SIZESHIFT)) |
                (size << TARGET_IOC_SIZESHIFT);
        }
        /* automatic consistency check if same arch */
#if defined(__i386__) && defined(TARGET_I386)
        if (ie->target_cmd != ie->host_cmd) {
            fprintf(stderr, "ERROR: ioctl: target=0x%x host=0x%x\n", 
                    ie->target_cmd, ie->host_cmd);
        }
#endif
        ie++;
    }
}

static inline uint64_t target_offset64(uint32_t word0, uint32_t word1)
{
#ifdef TARGET_WORDS_BIG_ENDIAN
    return ((uint64_t)word0 << 32) | word1;
#else
    return ((uint64_t)word1 << 32) | word0;
#endif
}

#ifdef TARGET_NR_truncate64
static inline long target_truncate64(void *cpu_env, const char *arg1,
                                     long arg2, long arg3, long arg4)
{
#ifdef TARGET_ARM
    if (((CPUARMState *)cpu_env)->eabi)
      {
        arg2 = arg3;
        arg3 = arg4;
      }
#endif
    return get_errno(truncate64(arg1, target_offset64(arg2, arg3)));
}
#endif

#ifdef TARGET_NR_ftruncate64
static inline long target_ftruncate64(void *cpu_env, long arg1, long arg2,
                                      long arg3, long arg4)
{
#ifdef TARGET_ARM
    if (((CPUARMState *)cpu_env)->eabi)
      {
        arg2 = arg3;
        arg3 = arg4;
      }
#endif
    return get_errno(ftruncate64(arg1, target_offset64(arg2, arg3)));
}
#endif

static inline void target_to_host_timespec(struct timespec *host_ts,
                                           target_ulong target_addr)
{
    struct target_timespec *target_ts;

    lock_user_struct(target_ts, target_addr, 1);
    host_ts->tv_sec = tswapl(target_ts->tv_sec);
    host_ts->tv_nsec = tswapl(target_ts->tv_nsec);
    unlock_user_struct(target_ts, target_addr, 0);
}

static inline void host_to_target_timespec(target_ulong target_addr,
                                           struct timespec *host_ts)
{
    struct target_timespec *target_ts;

    lock_user_struct(target_ts, target_addr, 0);
    target_ts->tv_sec = tswapl(host_ts->tv_sec);
    target_ts->tv_nsec = tswapl(host_ts->tv_nsec);
    unlock_user_struct(target_ts, target_addr, 1);
}

long do_syscall(void *cpu_env, int num, long arg1, long arg2, long arg3, 
                long arg4, long arg5, long arg6)
{
    long ret;
    struct stat st;
    struct statfs stfs;
    void *p;
    
#ifdef DEBUG
    gemu_log("syscall %d", num);
#endif
    switch(num) {
    case TARGET_NR_exit:
#ifdef HAVE_GPROF
        _mcleanup();
#endif
        gdb_exit(cpu_env, arg1);
        /* XXX: should free thread stack and CPU env */
        _exit(arg1);
        ret = 0; /* avoid warning */
        break;
    case TARGET_NR_read:
        page_unprotect_range(arg2, arg3);
        p = lock_user(arg2, arg3, 0);
        ret = get_errno(read(arg1, p, arg3));
        unlock_user(p, arg2, ret);
        break;
    case TARGET_NR_write:
        p = lock_user(arg2, arg3, 1);
        ret = get_errno(write(arg1, p, arg3));
        unlock_user(p, arg2, 0);
        break;
    case TARGET_NR_open:
        p = lock_user_string(arg1);
        ret = get_errno(open(path(p),
                             target_to_host_bitmask(arg2, fcntl_flags_tbl),
                             arg3));
        unlock_user(p, arg1, 0);
        break;
    case TARGET_NR_close:
        ret = get_errno(close(arg1));
        break;
    case TARGET_NR_brk:
        ret = do_brk(arg1);
        break;
    case TARGET_NR_fork:
        ret = get_errno(do_fork(cpu_env, SIGCHLD, 0));
        break;
    case TARGET_NR_waitpid:
        {
            int status;
            ret = get_errno(waitpid(arg1, &status, arg3));
            if (!is_error(ret) && arg2)
                tput32(arg2, status);
        }
        break;
    case TARGET_NR_creat:
        p = lock_user_string(arg1);
        ret = get_errno(creat(p, arg2));
        unlock_user(p, arg1, 0);
        break;
    case TARGET_NR_link:
        {
            void * p2;
            p = lock_user_string(arg1);
            p2 = lock_user_string(arg2);
            ret = get_errno(link(p, p2));
            unlock_user(p2, arg2, 0);
            unlock_user(p, arg1, 0);
        }
        break;
    case TARGET_NR_unlink:
        p = lock_user_string(arg1);
        ret = get_errno(unlink(p));
        unlock_user(p, arg1, 0);
        break;
    case TARGET_NR_execve:
        {
            char **argp, **envp;
            int argc, envc;
            target_ulong gp;
            target_ulong guest_argp;
            target_ulong guest_envp;
            target_ulong addr;
            char **q;

            argc = 0;
            guest_argp = arg2;
            for (gp = guest_argp; tgetl(gp); gp++)
                argc++;
            envc = 0;
            guest_envp = arg3;
            for (gp = guest_envp; tgetl(gp); gp++)
                envc++;

            argp = alloca((argc + 1) * sizeof(void *));
            envp = alloca((envc + 1) * sizeof(void *));

            for (gp = guest_argp, q = argp; ;
                  gp += sizeof(target_ulong), q++) {
                addr = tgetl(gp);
                if (!addr)
                    break;
                *q = lock_user_string(addr);
            }
            *q = NULL;

            for (gp = guest_envp, q = envp; ;
                  gp += sizeof(target_ulong), q++) {
                addr = tgetl(gp);
                if (!addr)
                    break;
                *q = lock_user_string(addr);
            }
            *q = NULL;

            p = lock_user_string(arg1);
            ret = get_errno(execve(p, argp, envp));
            unlock_user(p, arg1, 0);

            for (gp = guest_argp, q = argp; *q;
                  gp += sizeof(target_ulong), q++) {
                addr = tgetl(gp);
                unlock_user(*q, addr, 0);
            }
            for (gp = guest_envp, q = envp; *q;
                  gp += sizeof(target_ulong), q++) {
                addr = tgetl(gp);
                unlock_user(*q, addr, 0);
            }
        }
        break;
    case TARGET_NR_chdir:
        p = lock_user_string(arg1);
        ret = get_errno(chdir(p));
        unlock_user(p, arg1, 0);
        break;
#ifdef TARGET_NR_time
    case TARGET_NR_time:
        {
            time_t host_time;
            ret = get_errno(time(&host_time));
            if (!is_error(ret) && arg1)
                tputl(arg1, host_time);
        }
        break;
#endif
    case TARGET_NR_mknod:
        p = lock_user_string(arg1);
        ret = get_errno(mknod(p, arg2, arg3));
        unlock_user(p, arg1, 0);
        break;
    case TARGET_NR_chmod:
        p = lock_user_string(arg1);
        ret = get_errno(chmod(p, arg2));
        unlock_user(p, arg1, 0);
        break;
#ifdef TARGET_NR_break
    case TARGET_NR_break:
        goto unimplemented;
#endif
#ifdef TARGET_NR_oldstat
    case TARGET_NR_oldstat:
        goto unimplemented;
#endif
    case TARGET_NR_lseek:
        ret = get_errno(lseek(arg1, arg2, arg3));
        break;
    case TARGET_NR_getpid:
        ret = get_errno(getpid());
        break;
    case TARGET_NR_mount:
        /* need to look at the data field */
        goto unimplemented;
    case TARGET_NR_umount:
        p = lock_user_string(arg1);
        ret = get_errno(umount(p));
        unlock_user(p, arg1, 0);
        break;
    case TARGET_NR_stime:
        {
            time_t host_time;
            host_time = tgetl(arg1);
            ret = get_errno(stime(&host_time));
        }
        break;
    case TARGET_NR_ptrace:
        goto unimplemented;
    case TARGET_NR_alarm:
        ret = alarm(arg1);
        break;
#ifdef TARGET_NR_oldfstat
    case TARGET_NR_oldfstat:
        goto unimplemented;
#endif
    case TARGET_NR_pause:
        ret = get_errno(pause());
        break;
    case TARGET_NR_utime:
        {
            struct utimbuf tbuf, *host_tbuf;
            struct target_utimbuf *target_tbuf;
            if (arg2) {
                lock_user_struct(target_tbuf, arg2, 1);
                tbuf.actime = tswapl(target_tbuf->actime);
                tbuf.modtime = tswapl(target_tbuf->modtime);
                unlock_user_struct(target_tbuf, arg2, 0);
                host_tbuf = &tbuf;
            } else {
                host_tbuf = NULL;
            }
            p = lock_user_string(arg1);
            ret = get_errno(utime(p, host_tbuf));
            unlock_user(p, arg1, 0);
        }
        break;
    case TARGET_NR_utimes:
        {
            struct timeval *tvp, tv[2];
            if (arg2) {
                target_to_host_timeval(&tv[0], arg2);
                target_to_host_timeval(&tv[1],
                    arg2 + sizeof (struct target_timeval));
                tvp = tv;
            } else {
                tvp = NULL;
            }
            p = lock_user_string(arg1);
            ret = get_errno(utimes(p, tvp));
            unlock_user(p, arg1, 0);
        }
        break;
#ifdef TARGET_NR_stty
    case TARGET_NR_stty:
        goto unimplemented;
#endif
#ifdef TARGET_NR_gtty
    case TARGET_NR_gtty:
        goto unimplemented;
#endif
    case TARGET_NR_access:
        p = lock_user_string(arg1);
        ret = get_errno(access(p, arg2));
        unlock_user(p, arg1, 0);
        break;
    case TARGET_NR_nice:
        ret = get_errno(nice(arg1));
        break;
#ifdef TARGET_NR_ftime
    case TARGET_NR_ftime:
        goto unimplemented;
#endif
    case TARGET_NR_sync:
        sync();
        ret = 0;
        break;
    case TARGET_NR_kill:
        ret = get_errno(kill(arg1, arg2));
        break;
    case TARGET_NR_rename:
        {
            void *p2;
            p = lock_user_string(arg1);
            p2 = lock_user_string(arg2);
            ret = get_errno(rename(p, p2));
            unlock_user(p2, arg2, 0);
            unlock_user(p, arg1, 0);
        }
        break;
    case TARGET_NR_mkdir:
        p = lock_user_string(arg1);
        ret = get_errno(mkdir(p, arg2));
        unlock_user(p, arg1, 0);
        break;
    case TARGET_NR_rmdir:
        p = lock_user_string(arg1);
        ret = get_errno(rmdir(p));
        unlock_user(p, arg1, 0);
        break;
    case TARGET_NR_dup:
        ret = get_errno(dup(arg1));
        break;
    case TARGET_NR_pipe:
        {
            int host_pipe[2];
            ret = get_errno(pipe(host_pipe));
            if (!is_error(ret)) {
                tput32(arg1, host_pipe[0]);
                tput32(arg1 + 4, host_pipe[1]);
            }
        }
        break;
    case TARGET_NR_times:
        {
            struct target_tms *tmsp;
            struct tms tms;
            ret = get_errno(times(&tms));
            if (arg1) {
                tmsp = lock_user(arg1, sizeof(struct target_tms), 0);
                tmsp->tms_utime = tswapl(host_to_target_clock_t(tms.tms_utime));
                tmsp->tms_stime = tswapl(host_to_target_clock_t(tms.tms_stime));
                tmsp->tms_cutime = tswapl(host_to_target_clock_t(tms.tms_cutime));
                tmsp->tms_cstime = tswapl(host_to_target_clock_t(tms.tms_cstime));
            }
            if (!is_error(ret))
                ret = host_to_target_clock_t(ret);
        }
        break;
#ifdef TARGET_NR_prof
    case TARGET_NR_prof:
        goto unimplemented;
#endif
    case TARGET_NR_signal:
        goto unimplemented;

    case TARGET_NR_acct:
        p = lock_user_string(arg1);
        ret = get_errno(acct(path(p)));
        unlock_user(p, arg1, 0);
        break;
    case TARGET_NR_umount2:
        p = lock_user_string(arg1);
        ret = get_errno(umount2(p, arg2));
        unlock_user(p, arg1, 0);
        break;
#ifdef TARGET_NR_lock
    case TARGET_NR_lock:
        goto unimplemented;
#endif
    case TARGET_NR_ioctl:
        ret = do_ioctl(arg1, arg2, arg3);
        break;
    case TARGET_NR_fcntl:
        ret = get_errno(do_fcntl(arg1, arg2, arg3));
        break;
#ifdef TARGET_NR_mpx
    case TARGET_NR_mpx:
        goto unimplemented;
#endif
    case TARGET_NR_setpgid:
        ret = get_errno(setpgid(arg1, arg2));
        break;
#ifdef TARGET_NR_ulimit
    case TARGET_NR_ulimit:
        goto unimplemented;
#endif
#ifdef TARGET_NR_oldolduname
    case TARGET_NR_oldolduname:
        goto unimplemented;
#endif
    case TARGET_NR_umask:
        ret = get_errno(umask(arg1));
        break;
    case TARGET_NR_chroot:
        p = lock_user_string(arg1);
        ret = get_errno(chroot(p));
        unlock_user(p, arg1, 0);
        break;
    case TARGET_NR_ustat:
        goto unimplemented;
    case TARGET_NR_dup2:
        ret = get_errno(dup2(arg1, arg2));
        break;
    case TARGET_NR_getppid:
        ret = get_errno(getppid());
        break;
    case TARGET_NR_getpgrp:
        ret = get_errno(getpgrp());
        break;
    case TARGET_NR_setsid:
        ret = get_errno(setsid());
        break;
    case TARGET_NR_sigaction:
        {
	#if !defined(TARGET_MIPS)
            struct target_old_sigaction *old_act;
            struct target_sigaction act, oact, *pact;
            if (arg2) {
                lock_user_struct(old_act, arg2, 1);
                act._sa_handler = old_act->_sa_handler;
                target_siginitset(&act.sa_mask, old_act->sa_mask);
                act.sa_flags = old_act->sa_flags;
                act.sa_restorer = old_act->sa_restorer;
                unlock_user_struct(old_act, arg2, 0);
                pact = &act;
            } else {
                pact = NULL;
            }
            ret = get_errno(do_sigaction(arg1, pact, &oact));
            if (!is_error(ret) && arg3) {
                lock_user_struct(old_act, arg3, 0);
                old_act->_sa_handler = oact._sa_handler;
                old_act->sa_mask = oact.sa_mask.sig[0];
                old_act->sa_flags = oact.sa_flags;
                old_act->sa_restorer = oact.sa_restorer;
                unlock_user_struct(old_act, arg3, 1);
            }
	#else
	    struct target_sigaction act, oact, *pact, *old_act;

	    if (arg2) {
		lock_user_struct(old_act, arg2, 1);
		act._sa_handler = old_act->_sa_handler;
		target_siginitset(&act.sa_mask, old_act->sa_mask.sig[0]);
		act.sa_flags = old_act->sa_flags;
		unlock_user_struct(old_act, arg2, 0);
		pact = &act;
	    } else {
		pact = NULL;
	    }

	    ret = get_errno(do_sigaction(arg1, pact, &oact));

	    if (!is_error(ret) && arg3) {
		lock_user_struct(old_act, arg3, 0);
		old_act->_sa_handler = oact._sa_handler;
		old_act->sa_flags = oact.sa_flags;
		old_act->sa_mask.sig[0] = oact.sa_mask.sig[0];
		old_act->sa_mask.sig[1] = 0;
		old_act->sa_mask.sig[2] = 0;
		old_act->sa_mask.sig[3] = 0;
		unlock_user_struct(old_act, arg3, 1);
	    }
	#endif
        }
        break;
    case TARGET_NR_rt_sigaction:
        {
            struct target_sigaction *act;
            struct target_sigaction *oact;

            if (arg2)
                lock_user_struct(act, arg2, 1);
            else
                act = NULL;
            if (arg3)
                lock_user_struct(oact, arg3, 0);
            else
                oact = NULL;
            ret = get_errno(do_sigaction(arg1, act, oact));
            if (arg2)
                unlock_user_struct(act, arg2, 0);
            if (arg3)
                unlock_user_struct(oact, arg3, 1);
        }
        break;
    case TARGET_NR_sgetmask:
        {
            sigset_t cur_set;
            target_ulong target_set;
            sigprocmask(0, NULL, &cur_set);
            host_to_target_old_sigset(&target_set, &cur_set);
            ret = target_set;
        }
        break;
    case TARGET_NR_ssetmask:
        {
            sigset_t set, oset, cur_set;
            target_ulong target_set = arg1;
            sigprocmask(0, NULL, &cur_set);
            target_to_host_old_sigset(&set, &target_set);
            sigorset(&set, &set, &cur_set);
            sigprocmask(SIG_SETMASK, &set, &oset);
            host_to_target_old_sigset(&target_set, &oset);
            ret = target_set;
        }
        break;
    case TARGET_NR_sigprocmask:
        {
            int how = arg1;
            sigset_t set, oldset, *set_ptr;
            
            if (arg2) {
                switch(how) {
                case TARGET_SIG_BLOCK:
                    how = SIG_BLOCK;
                    break;
                case TARGET_SIG_UNBLOCK:
                    how = SIG_UNBLOCK;
                    break;
                case TARGET_SIG_SETMASK:
                    how = SIG_SETMASK;
                    break;
                default:
                    ret = -EINVAL;
                    goto fail;
                }
                p = lock_user(arg2, sizeof(target_sigset_t), 1);
                target_to_host_old_sigset(&set, p);
                unlock_user(p, arg2, 0);
                set_ptr = &set;
            } else {
                how = 0;
                set_ptr = NULL;
            }
            ret = get_errno(sigprocmask(arg1, set_ptr, &oldset));
            if (!is_error(ret) && arg3) {
                p = lock_user(arg3, sizeof(target_sigset_t), 0);
                host_to_target_old_sigset(p, &oldset);
                unlock_user(p, arg3, sizeof(target_sigset_t));
            }
        }
        break;
    case TARGET_NR_rt_sigprocmask:
        {
            int how = arg1;
            sigset_t set, oldset, *set_ptr;
            
            if (arg2) {
                switch(how) {
                case TARGET_SIG_BLOCK:
                    how = SIG_BLOCK;
                    break;
                case TARGET_SIG_UNBLOCK:
                    how = SIG_UNBLOCK;
                    break;
                case TARGET_SIG_SETMASK:
                    how = SIG_SETMASK;
                    break;
                default:
                    ret = -EINVAL;
                    goto fail;
                }
                p = lock_user(arg2, sizeof(target_sigset_t), 1);
                target_to_host_sigset(&set, p);
                unlock_user(p, arg2, 0);
                set_ptr = &set;
            } else {
                how = 0;
                set_ptr = NULL;
            }
            ret = get_errno(sigprocmask(how, set_ptr, &oldset));
            if (!is_error(ret) && arg3) {
                p = lock_user(arg3, sizeof(target_sigset_t), 0);
                host_to_target_sigset(p, &oldset);
                unlock_user(p, arg3, sizeof(target_sigset_t));
            }
        }
        break;
    case TARGET_NR_sigpending:
        {
            sigset_t set;
            ret = get_errno(sigpending(&set));
            if (!is_error(ret)) {
                p = lock_user(arg1, sizeof(target_sigset_t), 0);
                host_to_target_old_sigset(p, &set);
                unlock_user(p, arg1, sizeof(target_sigset_t));
            }
        }
        break;
    case TARGET_NR_rt_sigpending:
        {
            sigset_t set;
            ret = get_errno(sigpending(&set));
            if (!is_error(ret)) {
                p = lock_user(arg1, sizeof(target_sigset_t), 0);
                host_to_target_sigset(p, &set);
                unlock_user(p, arg1, sizeof(target_sigset_t));
            }
        }
        break;
    case TARGET_NR_sigsuspend:
        {
            sigset_t set;
            p = lock_user(arg1, sizeof(target_sigset_t), 1);
            target_to_host_old_sigset(&set, p);
            unlock_user(p, arg1, 0);
            ret = get_errno(sigsuspend(&set));
        }
        break;
    case TARGET_NR_rt_sigsuspend:
        {
            sigset_t set;
            p = lock_user(arg1, sizeof(target_sigset_t), 1);
            target_to_host_sigset(&set, p);
            unlock_user(p, arg1, 0);
            ret = get_errno(sigsuspend(&set));
        }
        break;
    case TARGET_NR_rt_sigtimedwait:
        {
            sigset_t set;
            struct timespec uts, *puts;
            siginfo_t uinfo;
            
            p = lock_user(arg1, sizeof(target_sigset_t), 1);
            target_to_host_sigset(&set, p);
            unlock_user(p, arg1, 0);
            if (arg3) {
                puts = &uts;
                target_to_host_timespec(puts, arg3);
            } else {
                puts = NULL;
            }
            ret = get_errno(sigtimedwait(&set, &uinfo, puts));
            if (!is_error(ret) && arg2) {
                p = lock_user(arg2, sizeof(target_sigset_t), 0);
                host_to_target_siginfo(p, &uinfo);
                unlock_user(p, arg2, sizeof(target_sigset_t));
            }
        }
        break;
    case TARGET_NR_rt_sigqueueinfo:
        {
            siginfo_t uinfo;
            p = lock_user(arg3, sizeof(target_sigset_t), 1);
            target_to_host_siginfo(&uinfo, p);
            unlock_user(p, arg1, 0);
            ret = get_errno(sys_rt_sigqueueinfo(arg1, arg2, &uinfo));
        }
        break;
    case TARGET_NR_sigreturn:
        /* NOTE: ret is eax, so not transcoding must be done */
        ret = do_sigreturn(cpu_env);
        break;
    case TARGET_NR_rt_sigreturn:
        /* NOTE: ret is eax, so not transcoding must be done */
        ret = do_rt_sigreturn(cpu_env);
        break;
    case TARGET_NR_sethostname:
        p = lock_user_string(arg1);
        ret = get_errno(sethostname(p, arg2));
        unlock_user(p, arg1, 0);
        break;
    case TARGET_NR_setrlimit:
        {
            /* XXX: convert resource ? */
            int resource = arg1;
            struct target_rlimit *target_rlim;
            struct rlimit rlim;
            lock_user_struct(target_rlim, arg2, 1);
            rlim.rlim_cur = tswapl(target_rlim->rlim_cur);
            rlim.rlim_max = tswapl(target_rlim->rlim_max);
            unlock_user_struct(target_rlim, arg2, 0);
            ret = get_errno(setrlimit(resource, &rlim));
        }
        break;
    case TARGET_NR_getrlimit:
        {
            /* XXX: convert resource ? */
            int resource = arg1;
            struct target_rlimit *target_rlim;
            struct rlimit rlim;
            
            ret = get_errno(getrlimit(resource, &rlim));
            if (!is_error(ret)) {
                lock_user_struct(target_rlim, arg2, 0);
                rlim.rlim_cur = tswapl(target_rlim->rlim_cur);
                rlim.rlim_max = tswapl(target_rlim->rlim_max);
                unlock_user_struct(target_rlim, arg2, 1);
            }
        }
        break;
    case TARGET_NR_getrusage:
        {
            struct rusage rusage;
            ret = get_errno(getrusage(arg1, &rusage));
            if (!is_error(ret)) {
                host_to_target_rusage(arg2, &rusage);
            }
        }
        break;
    case TARGET_NR_gettimeofday:
        {
            struct timeval tv;
            ret = get_errno(gettimeofday(&tv, NULL));
            if (!is_error(ret)) {
                host_to_target_timeval(arg1, &tv);
            }
        }
        break;
    case TARGET_NR_settimeofday:
        {
            struct timeval tv;
            target_to_host_timeval(&tv, arg1);
            ret = get_errno(settimeofday(&tv, NULL));
        }
        break;
#ifdef TARGET_NR_select
    case TARGET_NR_select:
        {
            struct target_sel_arg_struct *sel;
            target_ulong inp, outp, exp, tvp;
            long nsel;

            lock_user_struct(sel, arg1, 1);
            nsel = tswapl(sel->n);
            inp = tswapl(sel->inp);
            outp = tswapl(sel->outp);
            exp = tswapl(sel->exp);
            tvp = tswapl(sel->tvp);
            unlock_user_struct(sel, arg1, 0);
            ret = do_select(nsel, inp, outp, exp, tvp);
        }
        break;
#endif
    case TARGET_NR_symlink:
        {
            void *p2;
            p = lock_user_string(arg1);
            p2 = lock_user_string(arg2);
            ret = get_errno(symlink(p, p2));
            unlock_user(p2, arg2, 0);
            unlock_user(p, arg1, 0);
        }
        break;
#ifdef TARGET_NR_oldlstat
    case TARGET_NR_oldlstat:
        goto unimplemented;
#endif
    case TARGET_NR_readlink:
        {
            void *p2;
            p = lock_user_string(arg1);
            p2 = lock_user(arg2, arg3, 0);
            ret = get_errno(readlink(path(p), p2, arg3));
            unlock_user(p2, arg2, ret);
            unlock_user(p, arg1, 0);
        }
        break;
    case TARGET_NR_uselib:
        goto unimplemented;
    case TARGET_NR_swapon:
        p = lock_user_string(arg1);
        ret = get_errno(swapon(p, arg2));
        unlock_user(p, arg1, 0);
        break;
    case TARGET_NR_reboot:
        goto unimplemented;
    case TARGET_NR_readdir:
        goto unimplemented;
    case TARGET_NR_mmap:
#if defined(TARGET_I386) || defined(TARGET_ARM)
        {
            target_ulong *v;
            target_ulong v1, v2, v3, v4, v5, v6;
            v = lock_user(arg1, 6 * sizeof(target_ulong), 1);
            v1 = tswapl(v[0]);
            v2 = tswapl(v[1]);
            v3 = tswapl(v[2]);
            v4 = tswapl(v[3]);
            v5 = tswapl(v[4]);
            v6 = tswapl(v[5]);
            unlock_user(v, arg1, 0);
            ret = get_errno(target_mmap(v1, v2, v3, 
                                        target_to_host_bitmask(v4, mmap_flags_tbl),
                                        v5, v6));
        }
#else
        ret = get_errno(target_mmap(arg1, arg2, arg3, 
                                    target_to_host_bitmask(arg4, mmap_flags_tbl), 
                                    arg5,
                                    arg6));
#endif
        break;
#ifdef TARGET_NR_mmap2
    case TARGET_NR_mmap2:
#if defined(TARGET_SPARC)
#define MMAP_SHIFT 12
#else
#define MMAP_SHIFT TARGET_PAGE_BITS
#endif
        ret = get_errno(target_mmap(arg1, arg2, arg3, 
                                    target_to_host_bitmask(arg4, mmap_flags_tbl), 
                                    arg5,
                                    arg6 << MMAP_SHIFT));
        break;
#endif
    case TARGET_NR_munmap:
        ret = get_errno(target_munmap(arg1, arg2));
        break;
    case TARGET_NR_mprotect:
        ret = get_errno(target_mprotect(arg1, arg2, arg3));
        break;
    case TARGET_NR_mremap:
        ret = get_errno(target_mremap(arg1, arg2, arg3, arg4, arg5));
        break;
        /* ??? msync/mlock/munlock are broken for softmmu.  */
    case TARGET_NR_msync:
        ret = get_errno(msync(g2h(arg1), arg2, arg3));
        break;
    case TARGET_NR_mlock:
        ret = get_errno(mlock(g2h(arg1), arg2));
        break;
    case TARGET_NR_munlock:
        ret = get_errno(munlock(g2h(arg1), arg2));
        break;
    case TARGET_NR_mlockall:
        ret = get_errno(mlockall(arg1));
        break;
    case TARGET_NR_munlockall:
        ret = get_errno(munlockall());
        break;
    case TARGET_NR_truncate:
        p = lock_user_string(arg1);
        ret = get_errno(truncate(p, arg2));
        unlock_user(p, arg1, 0);
        break;
    case TARGET_NR_ftruncate:
        ret = get_errno(ftruncate(arg1, arg2));
        break;
    case TARGET_NR_fchmod:
        ret = get_errno(fchmod(arg1, arg2));
        break;
    case TARGET_NR_getpriority:
        ret = get_errno(getpriority(arg1, arg2));
        break;
    case TARGET_NR_setpriority:
        ret = get_errno(setpriority(arg1, arg2, arg3));
        break;
#ifdef TARGET_NR_profil
    case TARGET_NR_profil:
        goto unimplemented;
#endif
    case TARGET_NR_statfs:
        p = lock_user_string(arg1);
        ret = get_errno(statfs(path(p), &stfs));
        unlock_user(p, arg1, 0);
    convert_statfs:
        if (!is_error(ret)) {
            struct target_statfs *target_stfs;
            
            lock_user_struct(target_stfs, arg2, 0);
            /* ??? put_user is probably wrong.  */
            put_user(stfs.f_type, &target_stfs->f_type);
            put_user(stfs.f_bsize, &target_stfs->f_bsize);
            put_user(stfs.f_blocks, &target_stfs->f_blocks);
            put_user(stfs.f_bfree, &target_stfs->f_bfree);
            put_user(stfs.f_bavail, &target_stfs->f_bavail);
            put_user(stfs.f_files, &target_stfs->f_files);
            put_user(stfs.f_ffree, &target_stfs->f_ffree);
            put_user(stfs.f_fsid.__val[0], &target_stfs->f_fsid);
            put_user(stfs.f_namelen, &target_stfs->f_namelen);
            unlock_user_struct(target_stfs, arg2, 1);
        }
        break;
    case TARGET_NR_fstatfs:
        ret = get_errno(fstatfs(arg1, &stfs));
        goto convert_statfs;
#ifdef TARGET_NR_statfs64
    case TARGET_NR_statfs64:
        p = lock_user_string(arg1);
        ret = get_errno(statfs(path(p), &stfs));
        unlock_user(p, arg1, 0);
    convert_statfs64:
        if (!is_error(ret)) {
            struct target_statfs64 *target_stfs;
            
            lock_user_struct(target_stfs, arg3, 0);
            /* ??? put_user is probably wrong.  */
            put_user(stfs.f_type, &target_stfs->f_type);
            put_user(stfs.f_bsize, &target_stfs->f_bsize);
            put_user(stfs.f_blocks, &target_stfs->f_blocks);
            put_user(stfs.f_bfree, &target_stfs->f_bfree);
            put_user(stfs.f_bavail, &target_stfs->f_bavail);
            put_user(stfs.f_files, &target_stfs->f_files);
            put_user(stfs.f_ffree, &target_stfs->f_ffree);
            put_user(stfs.f_fsid.__val[0], &target_stfs->f_fsid);
            put_user(stfs.f_namelen, &target_stfs->f_namelen);
            unlock_user_struct(target_stfs, arg3, 0);
        }
        break;
    case TARGET_NR_fstatfs64:
        ret = get_errno(fstatfs(arg1, &stfs));
        goto convert_statfs64;
#endif
#ifdef TARGET_NR_ioperm
    case TARGET_NR_ioperm:
        goto unimplemented;
#endif
    case TARGET_NR_socketcall:
        ret = do_socketcall(arg1, arg2);
        break;

#ifdef TARGET_NR_accept
    case TARGET_NR_accept:
        ret = do_socketcallwrapper(SOCKOP_accept, arg1, arg2, arg3, arg4, arg5, arg6);
        break;
#endif
#ifdef TARGET_NR_bind
    case TARGET_NR_bind:
        ret = do_bind(arg1, arg2, arg3);
        break;
#endif
#ifdef TARGET_NR_connect
    case TARGET_NR_connect:
        ret = do_connect(arg1, arg2, arg3);
        break;
#endif
#ifdef TARGET_NR_getpeername
    case TARGET_NR_getpeername:
        ret = do_socketcallwrapper(SOCKOP_getpeername, arg1, arg2, arg3, arg4, arg5, arg6);
        break;
#endif
#ifdef TARGET_NR_getsockname
    case TARGET_NR_getsockname:
        ret = do_socketcallwrapper(SOCKOP_getsockname, arg1, arg2, arg3, arg4, arg5, arg6);
        break;
#endif
#ifdef TARGET_NR_getsockopt
    case TARGET_NR_getsockopt:
        ret = do_getsockopt(arg1, arg2, arg3, arg4, arg5);
        break;
#endif
#ifdef TARGET_NR_listen
    case TARGET_NR_listen:
        ret = do_socketcallwrapper(SOCKOP_listen, arg1, arg2, arg3, arg4, arg5, arg6);
        break;
#endif
#ifdef TARGET_NR_recv
    case TARGET_NR_recv:
        ret = do_socketcallwrapper(SOCKOP_recv, arg1, arg2, arg3, arg4, arg5, arg6);
        break;
#endif
#ifdef TARGET_NR_recvfrom
    case TARGET_NR_recvfrom:
        ret = do_socketcallwrapper(SOCKOP_recvfrom, arg1, arg2, arg3, arg4, arg5, arg6);
        break;
#endif
#ifdef TARGET_NR_recvmsg
    case TARGET_NR_recvmsg:
        ret = do_sendrecvmsg(arg1, arg2, arg3, 0);
        break;
#endif
#ifdef TARGET_NR_send
    case TARGET_NR_send:
        ret = do_socketcallwrapper(SOCKOP_send, arg1, arg2, arg3, arg4, arg5, arg6);
        break;
#endif
#ifdef TARGET_NR_sendmsg
    case TARGET_NR_sendmsg:
        ret = do_sendrecvmsg(arg1, arg2, arg3, 1);
        break;
#endif
#ifdef TARGET_NR_sendto
    case TARGET_NR_sendto:
        ret = do_socketcallwrapper(SOCKOP_sendto, arg1, arg2, arg3, arg4, arg5, arg6);
        break;
#endif
#ifdef TARGET_NR_shutdown
    case TARGET_NR_shutdown:
        ret = do_socketcallwrapper(SOCKOP_shutdown, arg1, arg2, arg3, arg4, arg5, arg6);
        break;
#endif
#ifdef TARGET_NR_socket
    case TARGET_NR_socket:
        ret = do_socket(arg1, arg2, arg3);
        break;
#endif
#ifdef TARGET_NR_socketpair
    case TARGET_NR_socketpair:
        ret = do_socketcallwrapper(SOCKOP_socketpair, arg1, arg2, arg3, arg4, arg5, arg6);
        break;
#endif
#ifdef TARGET_NR_setsockopt
    case TARGET_NR_setsockopt:
        ret = do_setsockopt(arg1, arg2, arg3, arg4, (socklen_t) arg5);
        break;
#endif
        
    case TARGET_NR_syslog:
        goto unimplemented;
    case TARGET_NR_setitimer:
        {
            struct itimerval value, ovalue, *pvalue;

            if (arg2) {
                pvalue = &value;
                target_to_host_timeval(&pvalue->it_interval, 
                                       arg2);
                target_to_host_timeval(&pvalue->it_value, 
                                       arg2 + sizeof(struct target_timeval));
            } else {
                pvalue = NULL;
            }
            ret = get_errno(setitimer(arg1, pvalue, &ovalue));
            if (!is_error(ret) && arg3) {
                host_to_target_timeval(arg3,
                                       &ovalue.it_interval);
                host_to_target_timeval(arg3 + sizeof(struct target_timeval),
                                       &ovalue.it_value);
            }
        }
        break;
    case TARGET_NR_getitimer:
        {
            struct itimerval value;
            
            ret = get_errno(getitimer(arg1, &value));
            if (!is_error(ret) && arg2) {
                host_to_target_timeval(arg2,
                                       &value.it_interval);
                host_to_target_timeval(arg2 + sizeof(struct target_timeval),
                                       &value.it_value);
            }
        }
        break;
    case TARGET_NR_stat:
        p = lock_user_string(arg1);
        ret = get_errno(stat(path(p), &st));
        unlock_user(p, arg1, 0);
        goto do_stat;
    case TARGET_NR_lstat:
        p = lock_user_string(arg1);
        ret = get_errno(lstat(path(p), &st));
        unlock_user(p, arg1, 0);
        goto do_stat;
    case TARGET_NR_fstat:
        {
            ret = get_errno(fstat(arg1, &st));
        do_stat:
            if (!is_error(ret)) {
                struct target_stat *target_st;
                
                lock_user_struct(target_st, arg2, 0);
                target_st->st_dev = tswap16(st.st_dev);
                target_st->st_ino = tswapl(st.st_ino);
#if defined(TARGET_PPC)
                target_st->st_mode = tswapl(st.st_mode); /* XXX: check this */
                target_st->st_uid = tswap32(st.st_uid);
                target_st->st_gid = tswap32(st.st_gid);
#else
                target_st->st_mode = tswap16(st.st_mode);
                target_st->st_uid = tswap16(st.st_uid);
                target_st->st_gid = tswap16(st.st_gid);
#endif
                target_st->st_nlink = tswap16(st.st_nlink);
                target_st->st_rdev = tswap16(st.st_rdev);
                target_st->st_size = tswapl(st.st_size);
                target_st->st_blksize = tswapl(st.st_blksize);
                target_st->st_blocks = tswapl(st.st_blocks);
                target_st->target_st_atime = tswapl(st.st_atime);
                target_st->target_st_mtime = tswapl(st.st_mtime);
                target_st->target_st_ctime = tswapl(st.st_ctime);
                unlock_user_struct(target_st, arg2, 1);
            }
        }
        break;
#ifdef TARGET_NR_olduname
    case TARGET_NR_olduname:
        goto unimplemented;
#endif
#ifdef TARGET_NR_iopl
    case TARGET_NR_iopl:
        goto unimplemented;
#endif
    case TARGET_NR_vhangup:
        ret = get_errno(vhangup());
        break;
#ifdef TARGET_NR_idle
    case TARGET_NR_idle:
        goto unimplemented;
#endif
#ifdef TARGET_NR_syscall
    case TARGET_NR_syscall:
    	ret = do_syscall(cpu_env,arg1 & 0xffff,arg2,arg3,arg4,arg5,arg6,0);
    	break;
#endif
    case TARGET_NR_wait4:
        {
            int status;
            target_long status_ptr = arg2;
            struct rusage rusage, *rusage_ptr;
            target_ulong target_rusage = arg4;
            if (target_rusage)
                rusage_ptr = &rusage;
            else
                rusage_ptr = NULL;
            ret = get_errno(wait4(arg1, &status, arg3, rusage_ptr));
            if (!is_error(ret)) {
                if (status_ptr)
                    tputl(status_ptr, status);
                if (target_rusage) {
                    host_to_target_rusage(target_rusage, &rusage);
                }
            }
        }
        break;
    case TARGET_NR_swapoff:
        p = lock_user_string(arg1);
        ret = get_errno(swapoff(p));
        unlock_user(p, arg1, 0);
        break;
    case TARGET_NR_sysinfo:
        {
            struct target_sysinfo *target_value;
            struct sysinfo value;
            ret = get_errno(sysinfo(&value));
            if (!is_error(ret) && arg1)
            {
                /* ??? __put_user is probably wrong.  */
                lock_user_struct(target_value, arg1, 0);
                __put_user(value.uptime, &target_value->uptime);
                __put_user(value.loads[0], &target_value->loads[0]);
                __put_user(value.loads[1], &target_value->loads[1]);
                __put_user(value.loads[2], &target_value->loads[2]);
                __put_user(value.totalram, &target_value->totalram);
                __put_user(value.freeram, &target_value->freeram);
                __put_user(value.sharedram, &target_value->sharedram);
                __put_user(value.bufferram, &target_value->bufferram);
                __put_user(value.totalswap, &target_value->totalswap);
                __put_user(value.freeswap, &target_value->freeswap);
                __put_user(value.procs, &target_value->procs);
                __put_user(value.totalhigh, &target_value->totalhigh);
                __put_user(value.freehigh, &target_value->freehigh);
                __put_user(value.mem_unit, &target_value->mem_unit);
                unlock_user_struct(target_value, arg1, 1);
            }
        }
        break;
    case TARGET_NR_ipc:
	ret = do_ipc(arg1, arg2, arg3, arg4, arg5, arg6);
	break;
    case TARGET_NR_fsync:
        ret = get_errno(fsync(arg1));
        break;
    case TARGET_NR_clone:
        ret = get_errno(do_fork(cpu_env, arg1, arg2));
        break;
#ifdef __NR_exit_group
        /* new thread calls */
    case TARGET_NR_exit_group:
        gdb_exit(cpu_env, arg1);
        ret = get_errno(exit_group(arg1));
        break;
#endif
    case TARGET_NR_setdomainname:
        p = lock_user_string(arg1);
        ret = get_errno(setdomainname(p, arg2));
        unlock_user(p, arg1, 0);
        break;
    case TARGET_NR_uname:
        /* no need to transcode because we use the linux syscall */
        {
            struct new_utsname * buf;
    
            lock_user_struct(buf, arg1, 0);
            ret = get_errno(sys_uname(buf));
            if (!is_error(ret)) {
                /* Overrite the native machine name with whatever is being
                   emulated. */
                strcpy (buf->machine, UNAME_MACHINE);
                /* Allow the user to override the reported release.  */
                if (qemu_uname_release && *qemu_uname_release)
                  strcpy (buf->release, qemu_uname_release);
            }
            unlock_user_struct(buf, arg1, 1);
        }
        break;
#ifdef TARGET_I386
    case TARGET_NR_modify_ldt:
        ret = get_errno(do_modify_ldt(cpu_env, arg1, arg2, arg3));
        break;
    case TARGET_NR_vm86old:
        goto unimplemented;
    case TARGET_NR_vm86:
        ret = do_vm86(cpu_env, arg1, arg2);
        break;
#endif
    case TARGET_NR_adjtimex:
        goto unimplemented;
    case TARGET_NR_create_module:
    case TARGET_NR_init_module:
    case TARGET_NR_delete_module:
    case TARGET_NR_get_kernel_syms:
        goto unimplemented;
    case TARGET_NR_quotactl:
        goto unimplemented;
    case TARGET_NR_getpgid:
        ret = get_errno(getpgid(arg1));
        break;
    case TARGET_NR_fchdir:
        ret = get_errno(fchdir(arg1));
        break;
    case TARGET_NR_bdflush:
        goto unimplemented;
    case TARGET_NR_sysfs:
        goto unimplemented;
    case TARGET_NR_personality:
        ret = get_errno(personality(arg1));
        break;
    case TARGET_NR_afs_syscall:
        goto unimplemented;
    case TARGET_NR__llseek:
        {
#if defined (__x86_64__)
            ret = get_errno(lseek(arg1, ((uint64_t )arg2 << 32) | arg3, arg5));
            tput64(arg4, ret);
#else
            int64_t res;
            ret = get_errno(_llseek(arg1, arg2, arg3, &res, arg5));
            tput64(arg4, res);
#endif
        }
        break;
    case TARGET_NR_getdents:
#if TARGET_LONG_SIZE != 4
        goto unimplemented;
#warning not supported
#elif TARGET_LONG_SIZE == 4 && HOST_LONG_SIZE == 8
        {
            struct target_dirent *target_dirp;
            struct dirent *dirp;
            long count = arg3;

	    dirp = malloc(count);
	    if (!dirp)
                return -ENOMEM;
            
            ret = get_errno(sys_getdents(arg1, dirp, count));
            if (!is_error(ret)) {
                struct dirent *de;
		struct target_dirent *tde;
                int len = ret;
                int reclen, treclen;
		int count1, tnamelen;

		count1 = 0;
                de = dirp;
                target_dirp = lock_user(arg2, count, 0);
		tde = target_dirp;
                while (len > 0) {
                    reclen = de->d_reclen;
		    treclen = reclen - (2 * (sizeof(long) - sizeof(target_long)));
                    tde->d_reclen = tswap16(treclen);
                    tde->d_ino = tswapl(de->d_ino);
                    tde->d_off = tswapl(de->d_off);
		    tnamelen = treclen - (2 * sizeof(target_long) + 2);
		    if (tnamelen > 256)
                        tnamelen = 256;
                    /* XXX: may not be correct */
		    strncpy(tde->d_name, de->d_name, tnamelen);
                    de = (struct dirent *)((char *)de + reclen);
                    len -= reclen;
                    tde = (struct dirent *)((char *)tde + treclen);
		    count1 += treclen;
                }
		ret = count1;
            }
            unlock_user(target_dirp, arg2, ret);
	    free(dirp);
        }
#else
        {
            struct dirent *dirp;
            long count = arg3;

            dirp = lock_user(arg2, count, 0);
            ret = get_errno(sys_getdents(arg1, dirp, count));
            if (!is_error(ret)) {
                struct dirent *de;
                int len = ret;
                int reclen;
                de = dirp;
                while (len > 0) {
                    reclen = de->d_reclen;
                    if (reclen > len)
                        break;
                    de->d_reclen = tswap16(reclen);
                    tswapls(&de->d_ino);
                    tswapls(&de->d_off);
                    de = (struct dirent *)((char *)de + reclen);
                    len -= reclen;
                }
            }
            unlock_user(dirp, arg2, ret);
        }
#endif
        break;
#ifdef TARGET_NR_getdents64
    case TARGET_NR_getdents64:
        {
            struct dirent64 *dirp;
            long count = arg3;
            dirp = lock_user(arg2, count, 0);
            ret = get_errno(sys_getdents64(arg1, dirp, count));
            if (!is_error(ret)) {
                struct dirent64 *de;
                int len = ret;
                int reclen;
                de = dirp;
                while (len > 0) {
                    reclen = de->d_reclen;
                    if (reclen > len)
                        break;
                    de->d_reclen = tswap16(reclen);
                    tswap64s(&de->d_ino);
                    tswap64s(&de->d_off);
                    de = (struct dirent64 *)((char *)de + reclen);
                    len -= reclen;
                }
            }
            unlock_user(dirp, arg2, ret);
        }
        break;
#endif /* TARGET_NR_getdents64 */
    case TARGET_NR__newselect:
        ret = do_select(arg1, arg2, arg3, arg4, arg5);
        break;
    case TARGET_NR_poll:
        {
            struct target_pollfd *target_pfd;
            unsigned int nfds = arg2;
            int timeout = arg3;
            struct pollfd *pfd;
            unsigned int i;

            target_pfd = lock_user(arg1, sizeof(struct target_pollfd) * nfds, 1);
            pfd = alloca(sizeof(struct pollfd) * nfds);
            for(i = 0; i < nfds; i++) {
                pfd[i].fd = tswap32(target_pfd[i].fd);
                pfd[i].events = tswap16(target_pfd[i].events);
            }
            ret = get_errno(poll(pfd, nfds, timeout));
            if (!is_error(ret)) {
                for(i = 0; i < nfds; i++) {
                    target_pfd[i].revents = tswap16(pfd[i].revents);
                }
                ret += nfds * (sizeof(struct target_pollfd)
                               - sizeof(struct pollfd));
            }
            unlock_user(target_pfd, arg1, ret);
        }
        break;
    case TARGET_NR_flock:
        /* NOTE: the flock constant seems to be the same for every
           Linux platform */
        ret = get_errno(flock(arg1, arg2));
        break;
    case TARGET_NR_readv:
        {
            int count = arg3;
            struct iovec *vec;

            vec = alloca(count * sizeof(struct iovec));
            lock_iovec(vec, arg2, count, 0);
            ret = get_errno(readv(arg1, vec, count));
            unlock_iovec(vec, arg2, count, 1);
        }
        break;
    case TARGET_NR_writev:
        {
            int count = arg3;
            struct iovec *vec;

            vec = alloca(count * sizeof(struct iovec));
            lock_iovec(vec, arg2, count, 1);
            ret = get_errno(writev(arg1, vec, count));
            unlock_iovec(vec, arg2, count, 0);
        }
        break;
    case TARGET_NR_getsid:
        ret = get_errno(getsid(arg1));
        break;
    case TARGET_NR_fdatasync:
        ret = get_errno(fdatasync(arg1));
        break;
    case TARGET_NR__sysctl:
        /* We don't implement this, but ENODIR is always a safe
           return value. */
        return -ENOTDIR;
    case TARGET_NR_sched_setparam:
        {
            struct sched_param *target_schp;
            struct sched_param schp;

            lock_user_struct(target_schp, arg2, 1);
            schp.sched_priority = tswap32(target_schp->sched_priority);
            unlock_user_struct(target_schp, arg2, 0);
            ret = get_errno(sched_setparam(arg1, &schp));
        }
        break;
    case TARGET_NR_sched_getparam:
        {
            struct sched_param *target_schp;
            struct sched_param schp;
            ret = get_errno(sched_getparam(arg1, &schp));
            if (!is_error(ret)) {
                lock_user_struct(target_schp, arg2, 0);
                target_schp->sched_priority = tswap32(schp.sched_priority);
                unlock_user_struct(target_schp, arg2, 1);
            }
        }
        break;
    case TARGET_NR_sched_setscheduler:
        {
            struct sched_param *target_schp;
            struct sched_param schp;
            lock_user_struct(target_schp, arg3, 1);
            schp.sched_priority = tswap32(target_schp->sched_priority);
            unlock_user_struct(target_schp, arg3, 0);
            ret = get_errno(sched_setscheduler(arg1, arg2, &schp));
        }
        break;
    case TARGET_NR_sched_getscheduler:
        ret = get_errno(sched_getscheduler(arg1));
        break;
    case TARGET_NR_sched_yield:
        ret = get_errno(sched_yield());
        break;
    case TARGET_NR_sched_get_priority_max:
        ret = get_errno(sched_get_priority_max(arg1));
        break;
    case TARGET_NR_sched_get_priority_min:
        ret = get_errno(sched_get_priority_min(arg1));
        break;
    case TARGET_NR_sched_rr_get_interval:
        {
            struct timespec ts;
            ret = get_errno(sched_rr_get_interval(arg1, &ts));
            if (!is_error(ret)) {
                host_to_target_timespec(arg2, &ts);
            }
        }
        break;
    case TARGET_NR_nanosleep:
        {
            struct timespec req, rem;
            target_to_host_timespec(&req, arg1);
            ret = get_errno(nanosleep(&req, &rem));
            if (is_error(ret) && arg2) {
                host_to_target_timespec(arg2, &rem);
            }
        }
        break;
    case TARGET_NR_query_module:
        goto unimplemented;
    case TARGET_NR_nfsservctl:
        goto unimplemented;
    case TARGET_NR_prctl:
        goto unimplemented;
#ifdef TARGET_NR_pread
    case TARGET_NR_pread:
        page_unprotect_range(arg2, arg3);
        p = lock_user(arg2, arg3, 0);
        ret = get_errno(pread(arg1, p, arg3, arg4));
        unlock_user(p, arg2, ret);
        break;
    case TARGET_NR_pwrite:
        p = lock_user(arg2, arg3, 1);
        ret = get_errno(pwrite(arg1, p, arg3, arg4));
        unlock_user(p, arg2, 0);
        break;
#endif
    case TARGET_NR_getcwd:
        p = lock_user(arg1, arg2, 0);
        ret = get_errno(sys_getcwd1(p, arg2));
        unlock_user(p, arg1, ret);
        break;
    case TARGET_NR_capget:
        goto unimplemented;
    case TARGET_NR_capset:
        goto unimplemented;
    case TARGET_NR_sigaltstack:
        goto unimplemented;
    case TARGET_NR_sendfile:
        goto unimplemented;
#ifdef TARGET_NR_getpmsg
    case TARGET_NR_getpmsg:
        goto unimplemented;
#endif
#ifdef TARGET_NR_putpmsg
    case TARGET_NR_putpmsg:
        goto unimplemented;
#endif
#ifdef TARGET_NR_vfork
    case TARGET_NR_vfork:
        ret = get_errno(do_fork(cpu_env, CLONE_VFORK | CLONE_VM | SIGCHLD, 0));
        break;
#endif
#ifdef TARGET_NR_ugetrlimit
    case TARGET_NR_ugetrlimit:
    {
	struct rlimit rlim;
	ret = get_errno(getrlimit(arg1, &rlim));
	if (!is_error(ret)) {
	    struct target_rlimit *target_rlim;
            lock_user_struct(target_rlim, arg2, 0);
	    target_rlim->rlim_cur = tswapl(rlim.rlim_cur);
	    target_rlim->rlim_max = tswapl(rlim.rlim_max);
            unlock_user_struct(target_rlim, arg2, 1);
	}
	break;
    }
#endif
#ifdef TARGET_NR_truncate64
    case TARGET_NR_truncate64:
        p = lock_user_string(arg1);
	ret = target_truncate64(cpu_env, p, arg2, arg3, arg4);
        unlock_user(p, arg1, 0);
	break;
#endif
#ifdef TARGET_NR_ftruncate64
    case TARGET_NR_ftruncate64:
	ret = target_ftruncate64(cpu_env, arg1, arg2, arg3, arg4);
	break;
#endif
#ifdef TARGET_NR_stat64
    case TARGET_NR_stat64:
        p = lock_user_string(arg1);
        ret = get_errno(stat(path(p), &st));
        unlock_user(p, arg1, 0);
        goto do_stat64;
#endif
#ifdef TARGET_NR_lstat64
    case TARGET_NR_lstat64:
        p = lock_user_string(arg1);
        ret = get_errno(lstat(path(p), &st));
        unlock_user(p, arg1, 0);
        goto do_stat64;
#endif
#ifdef TARGET_NR_fstat64
    case TARGET_NR_fstat64:
        {
            ret = get_errno(fstat(arg1, &st));
        do_stat64:
            if (!is_error(ret)) {
#ifdef TARGET_ARM
                if (((CPUARMState *)cpu_env)->eabi) {
                    struct target_eabi_stat64 *target_st;
                    lock_user_struct(target_st, arg2, 1);
                    memset(target_st, 0, sizeof(struct target_eabi_stat64));
                    /* put_user is probably wrong.  */
                    put_user(st.st_dev, &target_st->st_dev);
                    put_user(st.st_ino, &target_st->st_ino);
#ifdef TARGET_STAT64_HAS_BROKEN_ST_INO
                    put_user(st.st_ino, &target_st->__st_ino);
#endif
                    put_user(st.st_mode, &target_st->st_mode);
                    put_user(st.st_nlink, &target_st->st_nlink);
                    put_user(st.st_uid, &target_st->st_uid);
                    put_user(st.st_gid, &target_st->st_gid);
                    put_user(st.st_rdev, &target_st->st_rdev);
                    /* XXX: better use of kernel struct */
                    put_user(st.st_size, &target_st->st_size);
                    put_user(st.st_blksize, &target_st->st_blksize);
                    put_user(st.st_blocks, &target_st->st_blocks);
                    put_user(st.st_atime, &target_st->target_st_atime);
                    put_user(st.st_mtime, &target_st->target_st_mtime);
                    put_user(st.st_ctime, &target_st->target_st_ctime);
                    unlock_user_struct(target_st, arg2, 0);
                } else
#endif
                {
                    struct target_stat64 *target_st;
                    lock_user_struct(target_st, arg2, 1);
                    memset(target_st, 0, sizeof(struct target_stat64));
                    /* ??? put_user is probably wrong.  */
                    put_user(st.st_dev, &target_st->st_dev);
                    put_user(st.st_ino, &target_st->st_ino);
#ifdef TARGET_STAT64_HAS_BROKEN_ST_INO
                    put_user(st.st_ino, &target_st->__st_ino);
#endif
                    put_user(st.st_mode, &target_st->st_mode);
                    put_user(st.st_nlink, &target_st->st_nlink);
                    put_user(st.st_uid, &target_st->st_uid);
                    put_user(st.st_gid, &target_st->st_gid);
                    put_user(st.st_rdev, &target_st->st_rdev);
                    /* XXX: better use of kernel struct */
                    put_user(st.st_size, &target_st->st_size);
                    put_user(st.st_blksize, &target_st->st_blksize);
                    put_user(st.st_blocks, &target_st->st_blocks);
                    put_user(st.st_atime, &target_st->target_st_atime);
                    put_user(st.st_mtime, &target_st->target_st_mtime);
                    put_user(st.st_ctime, &target_st->target_st_ctime);
                    unlock_user_struct(target_st, arg2, 0);
                }
            }
        }
        break;
#endif
#ifdef USE_UID16
    case TARGET_NR_lchown:
        p = lock_user_string(arg1);
        ret = get_errno(lchown(p, low2highuid(arg2), low2highgid(arg3)));
        unlock_user(p, arg1, 0);
        break;
    case TARGET_NR_getuid:
        ret = get_errno(high2lowuid(getuid()));
        break;
    case TARGET_NR_getgid:
        ret = get_errno(high2lowgid(getgid()));
        break;
    case TARGET_NR_geteuid:
        ret = get_errno(high2lowuid(geteuid()));
        break;
    case TARGET_NR_getegid:
        ret = get_errno(high2lowgid(getegid()));
        break;
    case TARGET_NR_setreuid:
        ret = get_errno(setreuid(low2highuid(arg1), low2highuid(arg2)));
        break;
    case TARGET_NR_setregid:
        ret = get_errno(setregid(low2highgid(arg1), low2highgid(arg2)));
        break;
    case TARGET_NR_getgroups:
        {
            int gidsetsize = arg1;
            uint16_t *target_grouplist;
            gid_t *grouplist;
            int i;

            grouplist = alloca(gidsetsize * sizeof(gid_t));
            ret = get_errno(getgroups(gidsetsize, grouplist));
            if (!is_error(ret)) {
                target_grouplist = lock_user(arg2, gidsetsize * 2, 0);
                for(i = 0;i < gidsetsize; i++)
                    target_grouplist[i] = tswap16(grouplist[i]);
                unlock_user(target_grouplist, arg2, gidsetsize * 2);
            }
        }
        break;
    case TARGET_NR_setgroups:
        {
            int gidsetsize = arg1;
            uint16_t *target_grouplist;
            gid_t *grouplist;
            int i;

            grouplist = alloca(gidsetsize * sizeof(gid_t));
            target_grouplist = lock_user(arg2, gidsetsize * 2, 1);
            for(i = 0;i < gidsetsize; i++)
                grouplist[i] = tswap16(target_grouplist[i]);
            unlock_user(target_grouplist, arg2, 0);
            ret = get_errno(setgroups(gidsetsize, grouplist));
        }
        break;
    case TARGET_NR_fchown:
        ret = get_errno(fchown(arg1, low2highuid(arg2), low2highgid(arg3)));
        break;
#ifdef TARGET_NR_setresuid
    case TARGET_NR_setresuid:
        ret = get_errno(setresuid(low2highuid(arg1), 
                                  low2highuid(arg2), 
                                  low2highuid(arg3)));
        break;
#endif
#ifdef TARGET_NR_getresuid
    case TARGET_NR_getresuid:
        {
            uid_t ruid, euid, suid;
            ret = get_errno(getresuid(&ruid, &euid, &suid));
            if (!is_error(ret)) {
                tput16(arg1, tswap16(high2lowuid(ruid)));
                tput16(arg2, tswap16(high2lowuid(euid)));
                tput16(arg3, tswap16(high2lowuid(suid)));
            }
        }
        break;
#endif
#ifdef TARGET_NR_getresgid
    case TARGET_NR_setresgid:
        ret = get_errno(setresgid(low2highgid(arg1), 
                                  low2highgid(arg2), 
                                  low2highgid(arg3)));
        break;
#endif
#ifdef TARGET_NR_getresgid
    case TARGET_NR_getresgid:
        {
            gid_t rgid, egid, sgid;
            ret = get_errno(getresgid(&rgid, &egid, &sgid));
            if (!is_error(ret)) {
                tput16(arg1, tswap16(high2lowgid(rgid)));
                tput16(arg2, tswap16(high2lowgid(egid)));
                tput16(arg3, tswap16(high2lowgid(sgid)));
            }
        }
        break;
#endif
    case TARGET_NR_chown:
        p = lock_user_string(arg1);
        ret = get_errno(chown(p, low2highuid(arg2), low2highgid(arg3)));
        unlock_user(p, arg1, 0);
        break;
    case TARGET_NR_setuid:
        ret = get_errno(setuid(low2highuid(arg1)));
        break;
    case TARGET_NR_setgid:
        ret = get_errno(setgid(low2highgid(arg1)));
        break;
    case TARGET_NR_setfsuid:
        ret = get_errno(setfsuid(arg1));
        break;
    case TARGET_NR_setfsgid:
        ret = get_errno(setfsgid(arg1));
        break;
#endif /* USE_UID16 */

#ifdef TARGET_NR_lchown32
    case TARGET_NR_lchown32:
        p = lock_user_string(arg1);
        ret = get_errno(lchown(p, arg2, arg3));
        unlock_user(p, arg1, 0);
        break;
#endif
#ifdef TARGET_NR_getuid32
    case TARGET_NR_getuid32:
        ret = get_errno(getuid());
        break;
#endif
#ifdef TARGET_NR_getgid32
    case TARGET_NR_getgid32:
        ret = get_errno(getgid());
        break;
#endif
#ifdef TARGET_NR_geteuid32
    case TARGET_NR_geteuid32:
        ret = get_errno(geteuid());
        break;
#endif
#ifdef TARGET_NR_getegid32
    case TARGET_NR_getegid32:
        ret = get_errno(getegid());
        break;
#endif
#ifdef TARGET_NR_setreuid32
    case TARGET_NR_setreuid32:
        ret = get_errno(setreuid(arg1, arg2));
        break;
#endif
#ifdef TARGET_NR_setregid32
    case TARGET_NR_setregid32:
        ret = get_errno(setregid(arg1, arg2));
        break;
#endif
#ifdef TARGET_NR_getgroups32
    case TARGET_NR_getgroups32:
        {
            int gidsetsize = arg1;
            uint32_t *target_grouplist;
            gid_t *grouplist;
            int i;

            grouplist = alloca(gidsetsize * sizeof(gid_t));
            ret = get_errno(getgroups(gidsetsize, grouplist));
            if (!is_error(ret)) {
                target_grouplist = lock_user(arg2, gidsetsize * 4, 0);
                for(i = 0;i < gidsetsize; i++)
                    target_grouplist[i] = tswap32(grouplist[i]);
                unlock_user(target_grouplist, arg2, gidsetsize * 4);
            }
        }
        break;
#endif
#ifdef TARGET_NR_setgroups32
    case TARGET_NR_setgroups32:
        {
            int gidsetsize = arg1;
            uint32_t *target_grouplist;
            gid_t *grouplist;
            int i;
            
            grouplist = alloca(gidsetsize * sizeof(gid_t));
            target_grouplist = lock_user(arg2, gidsetsize * 4, 1);
            for(i = 0;i < gidsetsize; i++)
                grouplist[i] = tswap32(target_grouplist[i]);
            unlock_user(target_grouplist, arg2, 0);
            ret = get_errno(setgroups(gidsetsize, grouplist));
        }
        break;
#endif
#ifdef TARGET_NR_fchown32
    case TARGET_NR_fchown32:
        ret = get_errno(fchown(arg1, arg2, arg3));
        break;
#endif
#ifdef TARGET_NR_setresuid32
    case TARGET_NR_setresuid32:
        ret = get_errno(setresuid(arg1, arg2, arg3));
        break;
#endif
#ifdef TARGET_NR_getresuid32
    case TARGET_NR_getresuid32:
        {
            uid_t ruid, euid, suid;
            ret = get_errno(getresuid(&ruid, &euid, &suid));
            if (!is_error(ret)) {
                tput32(arg1, tswap32(ruid));
                tput32(arg2, tswap32(euid));
                tput32(arg3, tswap32(suid));
            }
        }
        break;
#endif
#ifdef TARGET_NR_setresgid32
    case TARGET_NR_setresgid32:
        ret = get_errno(setresgid(arg1, arg2, arg3));
        break;
#endif
#ifdef TARGET_NR_getresgid32
    case TARGET_NR_getresgid32:
        {
            gid_t rgid, egid, sgid;
            ret = get_errno(getresgid(&rgid, &egid, &sgid));
            if (!is_error(ret)) {
                tput32(arg1, tswap32(rgid));
                tput32(arg2, tswap32(egid));
                tput32(arg3, tswap32(sgid));
            }
        }
        break;
#endif
#ifdef TARGET_NR_chown32
    case TARGET_NR_chown32:
        p = lock_user_string(arg1);
        ret = get_errno(chown(p, arg2, arg3));
        unlock_user(p, arg1, 0);
        break;
#endif
#ifdef TARGET_NR_setuid32
    case TARGET_NR_setuid32:
        ret = get_errno(setuid(arg1));
        break;
#endif
#ifdef TARGET_NR_setgid32
    case TARGET_NR_setgid32:
        ret = get_errno(setgid(arg1));
        break;
#endif
#ifdef TARGET_NR_setfsuid32
    case TARGET_NR_setfsuid32:
        ret = get_errno(setfsuid(arg1));
        break;
#endif
#ifdef TARGET_NR_setfsgid32
    case TARGET_NR_setfsgid32:
        ret = get_errno(setfsgid(arg1));
        break;
#endif

    case TARGET_NR_pivot_root:
        goto unimplemented;
#ifdef TARGET_NR_mincore
    case TARGET_NR_mincore:
        goto unimplemented;
#endif
#ifdef TARGET_NR_madvise
    case TARGET_NR_madvise:
        /* A straight passthrough may not be safe because qemu sometimes
           turns private flie-backed mappings into anonymous mappings.
           This will break MADV_DONTNEED.
           This is a hint, so ignoring and returning success is ok.  */
        ret = get_errno(0);
        break;
#endif
#if TARGET_LONG_BITS == 32
    case TARGET_NR_fcntl64:
    {
	struct flock64 fl;
	struct target_flock64 *target_fl;
#ifdef TARGET_ARM
	struct target_eabi_flock64 *target_efl;
#endif

        switch(arg2) {
        case F_GETLK64:
            ret = get_errno(fcntl(arg1, arg2, &fl));
	    if (ret == 0) {
#ifdef TARGET_ARM
                if (((CPUARMState *)cpu_env)->eabi) {
                    lock_user_struct(target_efl, arg3, 0);
                    target_efl->l_type = tswap16(fl.l_type);
                    target_efl->l_whence = tswap16(fl.l_whence);
                    target_efl->l_start = tswap64(fl.l_start);
                    target_efl->l_len = tswap64(fl.l_len);
                    target_efl->l_pid = tswapl(fl.l_pid);
                    unlock_user_struct(target_efl, arg3, 1);
                } else
#endif
                {
                    lock_user_struct(target_fl, arg3, 0);
                    target_fl->l_type = tswap16(fl.l_type);
                    target_fl->l_whence = tswap16(fl.l_whence);
                    target_fl->l_start = tswap64(fl.l_start);
                    target_fl->l_len = tswap64(fl.l_len);
                    target_fl->l_pid = tswapl(fl.l_pid);
                    unlock_user_struct(target_fl, arg3, 1);
                }
	    }
	    break;

        case F_SETLK64:
        case F_SETLKW64:
#ifdef TARGET_ARM
            if (((CPUARMState *)cpu_env)->eabi) {
                lock_user_struct(target_efl, arg3, 1);
                fl.l_type = tswap16(target_efl->l_type);
                fl.l_whence = tswap16(target_efl->l_whence);
                fl.l_start = tswap64(target_efl->l_start);
                fl.l_len = tswap64(target_efl->l_len);
                fl.l_pid = tswapl(target_efl->l_pid);
                unlock_user_struct(target_efl, arg3, 0);
            } else
#endif
            {
                lock_user_struct(target_fl, arg3, 1);
                fl.l_type = tswap16(target_fl->l_type);
                fl.l_whence = tswap16(target_fl->l_whence);
                fl.l_start = tswap64(target_fl->l_start);
                fl.l_len = tswap64(target_fl->l_len);
                fl.l_pid = tswapl(target_fl->l_pid);
                unlock_user_struct(target_fl, arg3, 0);
            }
            ret = get_errno(fcntl(arg1, arg2, &fl));
	    break;
        default:
            ret = get_errno(do_fcntl(arg1, arg2, arg3));
            break;
        }
	break;
    }
#endif
#ifdef TARGET_NR_security
    case TARGET_NR_security:
        goto unimplemented;
#endif
#ifdef TARGET_NR_getpagesize
    case TARGET_NR_getpagesize:
        ret = TARGET_PAGE_SIZE;
        break;
#endif
    case TARGET_NR_gettid:
        ret = get_errno(gettid());
        break;
    case TARGET_NR_readahead:
        goto unimplemented;
#ifdef TARGET_NR_setxattr
    case TARGET_NR_setxattr:
    case TARGET_NR_lsetxattr:
    case TARGET_NR_fsetxattr:
    case TARGET_NR_getxattr:
    case TARGET_NR_lgetxattr:
    case TARGET_NR_fgetxattr:
    case TARGET_NR_listxattr:
    case TARGET_NR_llistxattr:
    case TARGET_NR_flistxattr:
    case TARGET_NR_removexattr:
    case TARGET_NR_lremovexattr:
    case TARGET_NR_fremovexattr:
        goto unimplemented_nowarn;
#endif
#ifdef TARGET_NR_set_thread_area
    case TARGET_NR_set_thread_area:
    case TARGET_NR_get_thread_area:
        goto unimplemented_nowarn;
#endif
#ifdef TARGET_NR_getdomainname
    case TARGET_NR_getdomainname:
        goto unimplemented_nowarn;
#endif
    default:
    unimplemented:
        gemu_log("qemu: Unsupported syscall: %d\n", num);
#if defined(TARGET_NR_setxattr) || defined(TARGET_NR_set_thread_area) || defined(TARGET_NR_getdomainname)
    unimplemented_nowarn:
#endif
        ret = -ENOSYS;
        break;
    }
 fail:
#ifdef DEBUG
    gemu_log(" = %ld\n", ret);
#endif
    return ret;
}


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
//#include <sys/user.h>
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

//#include <linux/msdos_fs.h>
#define	VFAT_IOCTL_READDIR_BOTH		_IOR('r', 1, struct dirent [2])
#define	VFAT_IOCTL_READDIR_SHORT	_IOR('r', 2, struct dirent [2])

#define __NR_sys_uname __NR_uname
#define __NR_sys_getcwd1 __NR_getcwd
#define __NR_sys_statfs __NR_statfs
#define __NR_sys_fstatfs __NR_fstatfs
#define __NR_sys_getdents __NR_getdents
#define __NR_sys_getdents64 __NR_getdents64
#define __NR_sys_rt_sigqueueinfo __NR_rt_sigqueueinfo

#if defined(__alpha__) || defined (__ia64__)
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
_syscall2(int,sys_statfs,const char *,path,struct kernel_statfs *,buf)
_syscall2(int,sys_fstatfs,int,fd,struct kernel_statfs *,buf)
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

static char *target_brk;
static char *target_original_brk;

void target_set_brk(char *new_brk)
{
    target_brk = new_brk;
    target_original_brk = new_brk;
}

static long do_brk(char *new_brk)
{
    char *brk_page;
    long mapped_addr;
    int	new_alloc_size;

    if (!new_brk)
        return (long)target_brk;
    if (new_brk < target_original_brk)
        return -ENOMEM;
    
    brk_page = (char *)HOST_PAGE_ALIGN((unsigned long)target_brk);

    /* If the new brk is less than this, set it and we're done... */
    if (new_brk < brk_page) {
	target_brk = new_brk;
    	return (long)target_brk;
    }

    /* We need to allocate more memory after the brk... */
    new_alloc_size = HOST_PAGE_ALIGN(new_brk - brk_page + 1);
    mapped_addr = get_errno(target_mmap((unsigned long)brk_page, new_alloc_size, 
                                        PROT_READ|PROT_WRITE,
                                        MAP_ANON|MAP_FIXED|MAP_PRIVATE, 0, 0));
    if (is_error(mapped_addr)) {
	return mapped_addr;
    } else {
	target_brk = new_brk;
    	return (long)target_brk;
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
        nw = n / TARGET_LONG_BITS;
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

static inline void host_to_target_rusage(struct target_rusage *target_rusage, 
                                         const struct rusage *rusage)
{
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
}

static inline void target_to_host_timeval(struct timeval *tv, 
                                          const struct target_timeval *target_tv)
{
    tv->tv_sec = tswapl(target_tv->tv_sec);
    tv->tv_usec = tswapl(target_tv->tv_usec);
}

static inline void host_to_target_timeval(struct target_timeval *target_tv, 
                                          const struct timeval *tv)
{
    target_tv->tv_sec = tswapl(tv->tv_sec);
    target_tv->tv_usec = tswapl(tv->tv_usec);
}


static long do_select(long n, 
                      target_long *target_rfds, target_long *target_wfds, 
                      target_long *target_efds, struct target_timeval *target_tv)
{
    fd_set rfds, wfds, efds;
    fd_set *rfds_ptr, *wfds_ptr, *efds_ptr;
    struct timeval tv, *tv_ptr;
    long ret;

    rfds_ptr = target_to_host_fds(&rfds, target_rfds, n);
    wfds_ptr = target_to_host_fds(&wfds, target_wfds, n);
    efds_ptr = target_to_host_fds(&efds, target_efds, n);
            
    if (target_tv) {
        target_to_host_timeval(&tv, target_tv);
        tv_ptr = &tv;
    } else {
        tv_ptr = NULL;
    }
    ret = get_errno(select(n, rfds_ptr, wfds_ptr, efds_ptr, tv_ptr));
    if (!is_error(ret)) {
        host_to_target_fds(target_rfds, rfds_ptr, n);
        host_to_target_fds(target_wfds, wfds_ptr, n);
        host_to_target_fds(target_efds, efds_ptr, n);

        if (target_tv) {
            host_to_target_timeval(target_tv, &tv);
        }
    }
    return ret;
}

static inline void target_to_host_sockaddr(struct sockaddr *addr,
                                           struct target_sockaddr *target_addr,
                                           socklen_t len)
{
    memcpy(addr, target_addr, len);
    addr->sa_family = tswap16(target_addr->sa_family);
}

static inline void host_to_target_sockaddr(struct target_sockaddr *target_addr,
                                           struct sockaddr *addr,
                                           socklen_t len)
{
    memcpy(target_addr, addr, len);
    target_addr->sa_family = tswap16(addr->sa_family);
}

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
            gemu_log("Host cmsg overflow");
            break;
        }

        cmsg->cmsg_level = tswap32(target_cmsg->cmsg_level);
        cmsg->cmsg_type = tswap32(target_cmsg->cmsg_type);
        cmsg->cmsg_len = CMSG_LEN(len);

        if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) {
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
            gemu_log("Target cmsg overflow");
            break;
        }

        target_cmsg->cmsg_level = tswap32(cmsg->cmsg_level);
        target_cmsg->cmsg_type = tswap32(cmsg->cmsg_type);
        target_cmsg->cmsg_len = tswapl(TARGET_CMSG_LEN(len));

        if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) {
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
                          void *optval, socklen_t optlen)
{
    if (level == SOL_TCP) {
        /* TCP options all take an 'int' value.  */
        int val;

        if (optlen < sizeof(uint32_t))
            return -EINVAL;

        val = tswap32(*(uint32_t *)optval);
        return get_errno(setsockopt(sockfd, level, optname, &val, sizeof(val)));
    }

    else if (level != SOL_SOCKET) {
        gemu_log("Unsupported setsockopt level: %d\n", level);
        return -ENOSYS;
    }

    switch (optname) {
    /* Options with 'int' argument.  */
    case SO_DEBUG:
    case SO_REUSEADDR:
    case SO_TYPE:
    case SO_ERROR:
    case SO_DONTROUTE:
    case SO_BROADCAST:
    case SO_SNDBUF:
    case SO_RCVBUF:
    case SO_KEEPALIVE:
    case SO_OOBINLINE:
    case SO_NO_CHECK:
    case SO_PRIORITY:
    case SO_BSDCOMPAT:
    case SO_PASSCRED:
    case SO_TIMESTAMP:
    case SO_RCVLOWAT:
    case SO_RCVTIMEO:
    case SO_SNDTIMEO:
    {
        int val;
        if (optlen < sizeof(uint32_t))
            return -EINVAL;
        val = tswap32(*(uint32_t *)optval);
        return get_errno(setsockopt(sockfd, level, optname, &val, sizeof(val)));
    }

    default:
        gemu_log("Unsupported setsockopt SOL_SOCKET option: %d\n", optname);
        return -ENOSYS;
    }
}

static long do_getsockopt(int sockfd, int level, int optname, 
                          void *optval, socklen_t *optlen)
{
    gemu_log("getsockopt not yet supported\n");
    return -ENOSYS;
}

static long do_socketcall(int num, int32_t *vptr)
{
    long ret;

    switch(num) {
    case SOCKOP_socket:
	{
            int domain = tswap32(vptr[0]);
            int type = tswap32(vptr[1]);
            int protocol = tswap32(vptr[2]);

            ret = get_errno(socket(domain, type, protocol));
	}
        break;
    case SOCKOP_bind:
	{
            int sockfd = tswap32(vptr[0]);
            void *target_addr = (void *)tswap32(vptr[1]);
            socklen_t addrlen = tswap32(vptr[2]);
            void *addr = alloca(addrlen);

            target_to_host_sockaddr(addr, target_addr, addrlen);
            ret = get_errno(bind(sockfd, addr, addrlen));
        }
        break;
    case SOCKOP_connect:
        {
            int sockfd = tswap32(vptr[0]);
            void *target_addr = (void *)tswap32(vptr[1]);
            socklen_t addrlen = tswap32(vptr[2]);
            void *addr = alloca(addrlen);

            target_to_host_sockaddr(addr, target_addr, addrlen);
            ret = get_errno(connect(sockfd, addr, addrlen));
        }
        break;
    case SOCKOP_listen:
        {
            int sockfd = tswap32(vptr[0]);
            int backlog = tswap32(vptr[1]);

            ret = get_errno(listen(sockfd, backlog));
        }
        break;
    case SOCKOP_accept:
        {
            int sockfd = tswap32(vptr[0]);
            void *target_addr = (void *)tswap32(vptr[1]);
            uint32_t *target_addrlen = (void *)tswap32(vptr[2]);
            socklen_t addrlen = tswap32(*target_addrlen);
            void *addr = alloca(addrlen);

            ret = get_errno(accept(sockfd, addr, &addrlen));
            if (!is_error(ret)) {
                host_to_target_sockaddr(target_addr, addr, addrlen);
                *target_addrlen = tswap32(addrlen);
            }
        }
        break;
    case SOCKOP_getsockname:
        {
            int sockfd = tswap32(vptr[0]);
            void *target_addr = (void *)tswap32(vptr[1]);
            uint32_t *target_addrlen = (void *)tswap32(vptr[2]);
            socklen_t addrlen = tswap32(*target_addrlen);
            void *addr = alloca(addrlen);

            ret = get_errno(getsockname(sockfd, addr, &addrlen));
            if (!is_error(ret)) {
                host_to_target_sockaddr(target_addr, addr, addrlen);
                *target_addrlen = tswap32(addrlen);
            }
        }
        break;
    case SOCKOP_getpeername:
        {
            int sockfd = tswap32(vptr[0]);
            void *target_addr = (void *)tswap32(vptr[1]);
            uint32_t *target_addrlen = (void *)tswap32(vptr[2]);
            socklen_t addrlen = tswap32(*target_addrlen);
            void *addr = alloca(addrlen);

            ret = get_errno(getpeername(sockfd, addr, &addrlen));
            if (!is_error(ret)) {
                host_to_target_sockaddr(target_addr, addr, addrlen);
                *target_addrlen = tswap32(addrlen);
            }
        }
        break;
    case SOCKOP_socketpair:
        {
            int domain = tswap32(vptr[0]);
            int type = tswap32(vptr[1]);
            int protocol = tswap32(vptr[2]);
            int32_t *target_tab = (void *)tswap32(vptr[3]);
            int tab[2];

            ret = get_errno(socketpair(domain, type, protocol, tab));
            if (!is_error(ret)) {
                target_tab[0] = tswap32(tab[0]);
                target_tab[1] = tswap32(tab[1]);
            }
        }
        break;
    case SOCKOP_send:
        {
            int sockfd = tswap32(vptr[0]);
            void *msg = (void *)tswap32(vptr[1]);
            size_t len = tswap32(vptr[2]);
            int flags = tswap32(vptr[3]);

            ret = get_errno(send(sockfd, msg, len, flags));
        }
        break;
    case SOCKOP_recv:
        {
            int sockfd = tswap32(vptr[0]);
            void *msg = (void *)tswap32(vptr[1]);
            size_t len = tswap32(vptr[2]);
            int flags = tswap32(vptr[3]);

            ret = get_errno(recv(sockfd, msg, len, flags));
        }
        break;
    case SOCKOP_sendto:
        {
            int sockfd = tswap32(vptr[0]);
            void *msg = (void *)tswap32(vptr[1]);
            size_t len = tswap32(vptr[2]);
            int flags = tswap32(vptr[3]);
            void *target_addr = (void *)tswap32(vptr[4]);
            socklen_t addrlen = tswap32(vptr[5]);
            void *addr = alloca(addrlen);

            target_to_host_sockaddr(addr, target_addr, addrlen);
            ret = get_errno(sendto(sockfd, msg, len, flags, addr, addrlen));
        }
        break;
    case SOCKOP_recvfrom:
        {
            int sockfd = tswap32(vptr[0]);
            void *msg = (void *)tswap32(vptr[1]);
            size_t len = tswap32(vptr[2]);
            int flags = tswap32(vptr[3]);
            void *target_addr = (void *)tswap32(vptr[4]);
            uint32_t *target_addrlen = (void *)tswap32(vptr[5]);
            socklen_t addrlen = tswap32(*target_addrlen);
            void *addr = alloca(addrlen);

            ret = get_errno(recvfrom(sockfd, msg, len, flags, addr, &addrlen));
            if (!is_error(ret)) {
                host_to_target_sockaddr(target_addr, addr, addrlen);
                *target_addrlen = tswap32(addrlen);
            }
        }
        break;
    case SOCKOP_shutdown:
        {
            int sockfd = tswap32(vptr[0]);
            int how = tswap32(vptr[1]);

            ret = get_errno(shutdown(sockfd, how));
        }
        break;
    case SOCKOP_sendmsg:
    case SOCKOP_recvmsg:
        {
            int fd;
            struct target_msghdr *msgp;
            struct msghdr msg;
            int flags, count, i;
            struct iovec *vec;
            struct target_iovec *target_vec;

            msgp = (void *)tswap32(vptr[1]);
            msg.msg_name = (void *)tswapl(msgp->msg_name);
            msg.msg_namelen = tswapl(msgp->msg_namelen);
            msg.msg_controllen = 2 * tswapl(msgp->msg_controllen);
            msg.msg_control = alloca(msg.msg_controllen);
            msg.msg_flags = tswap32(msgp->msg_flags);

            count = tswapl(msgp->msg_iovlen);
            vec = alloca(count * sizeof(struct iovec));
            target_vec = (void *)tswapl(msgp->msg_iov);
            for(i = 0;i < count; i++) {
                vec[i].iov_base = (void *)tswapl(target_vec[i].iov_base);
                vec[i].iov_len = tswapl(target_vec[i].iov_len);
            }
            msg.msg_iovlen = count;
            msg.msg_iov = vec;

            fd = tswap32(vptr[0]);
            flags = tswap32(vptr[2]);
            if (num == SOCKOP_sendmsg) {
                target_to_host_cmsg(&msg, msgp);
                ret = get_errno(sendmsg(fd, &msg, flags));
            } else {
                ret = get_errno(recvmsg(fd, &msg, flags));
                if (!is_error(ret))
                  host_to_target_cmsg(msgp, &msg);
            }
        }
        break;
    case SOCKOP_setsockopt:
        {
            int sockfd = tswap32(vptr[0]);
            int level = tswap32(vptr[1]);
            int optname = tswap32(vptr[2]);
            void *optval = (void *)tswap32(vptr[3]);
            socklen_t optlen = tswap32(vptr[4]);

            ret = do_setsockopt(sockfd, level, optname, optval, optlen);
        }
        break;
    case SOCKOP_getsockopt:
        {
            int sockfd = tswap32(vptr[0]);
            int level = tswap32(vptr[1]);
            int optname = tswap32(vptr[2]);
            void *optval = (void *)tswap32(vptr[3]);
            uint32_t *target_len = (void *)tswap32(vptr[4]);
            socklen_t optlen = tswap32(*target_len);

            ret = do_getsockopt(sockfd, level, optname, optval, &optlen);
            if (!is_error(ret))
                *target_len = tswap32(optlen);
        }
        break;
    default:
        gemu_log("Unsupported socketcall: %d\n", num);
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

static long do_ioctl(long fd, long cmd, long arg)
{
    const IOCTLEntry *ie;
    const argtype *arg_type;
    long ret;
    uint8_t buf_temp[MAX_STRUCT_SIZE];

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
        switch(ie->access) {
        case IOC_R:
            ret = get_errno(ioctl(fd, ie->host_cmd, buf_temp));
            if (!is_error(ret)) {
                thunk_convert((void *)arg, buf_temp, arg_type, THUNK_TARGET);
            }
            break;
        case IOC_W:
            thunk_convert(buf_temp, (void *)arg, arg_type, THUNK_HOST);
            ret = get_errno(ioctl(fd, ie->host_cmd, buf_temp));
            break;
        default:
        case IOC_RW:
            thunk_convert(buf_temp, (void *)arg, arg_type, THUNK_HOST);
            ret = get_errno(ioctl(fd, ie->host_cmd, buf_temp));
            if (!is_error(ret)) {
                thunk_convert((void *)arg, buf_temp, arg_type, THUNK_TARGET);
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

#if defined(TARGET_I386)

/* NOTE: there is really one LDT for all the threads */
uint8_t *ldt_table;

static int read_ldt(void *ptr, unsigned long bytecount)
{
    int size;

    if (!ldt_table)
        return 0;
    size = TARGET_LDT_ENTRIES * TARGET_LDT_ENTRY_SIZE;
    if (size > bytecount)
        size = bytecount;
    memcpy(ptr, ldt_table, size);
    return size;
}

/* XXX: add locking support */
static int write_ldt(CPUX86State *env, 
                     void *ptr, unsigned long bytecount, int oldmode)
{
    struct target_modify_ldt_ldt_s ldt_info;
    int seg_32bit, contents, read_exec_only, limit_in_pages;
    int seg_not_present, useable;
    uint32_t *lp, entry_1, entry_2;

    if (bytecount != sizeof(ldt_info))
        return -EINVAL;
    memcpy(&ldt_info, ptr, sizeof(ldt_info));
    tswap32s(&ldt_info.entry_number);
    tswapls((long *)&ldt_info.base_addr);
    tswap32s(&ldt_info.limit);
    tswap32s(&ldt_info.flags);
    
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
        env->ldt.base = ldt_table;
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
int do_modify_ldt(CPUX86State *env, int func, void *ptr, unsigned long bytecount)
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
#else
#error unsupported target CPU
#endif
        new_env->opaque = ts;
#ifdef __ia64__
        ret = clone2(clone_func, new_stack + NEW_STACK_SIZE, flags, new_env);
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

static long do_fcntl(int fd, int cmd, unsigned long arg)
{
    struct flock fl;
    struct target_flock *target_fl = (void *)arg;
    long ret;
    
    switch(cmd) {
    case TARGET_F_GETLK:
        ret = fcntl(fd, cmd, &fl);
        if (ret == 0) {
            target_fl->l_type = tswap16(fl.l_type);
            target_fl->l_whence = tswap16(fl.l_whence);
            target_fl->l_start = tswapl(fl.l_start);
            target_fl->l_len = tswapl(fl.l_len);
            target_fl->l_pid = tswapl(fl.l_pid);
        }
        break;
        
    case TARGET_F_SETLK:
    case TARGET_F_SETLKW:
        fl.l_type = tswap16(target_fl->l_type);
        fl.l_whence = tswap16(target_fl->l_whence);
        fl.l_start = tswapl(target_fl->l_start);
        fl.l_len = tswapl(target_fl->l_len);
        fl.l_pid = tswapl(target_fl->l_pid);
        ret = fcntl(fd, cmd, &fl);
        break;
        
    case TARGET_F_GETLK64:
    case TARGET_F_SETLK64:
    case TARGET_F_SETLKW64:
        ret = -1;
        errno = EINVAL;
        break;

    default:
        ret = fcntl(fd, cmd, arg);
        break;
    }
    return ret;
}


#define high2lowuid(x) (x)
#define high2lowgid(x) (x)
#define low2highuid(x) (x)
#define low2highgid(x) (x)

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
                                 
long do_syscall(void *cpu_env, int num, long arg1, long arg2, long arg3, 
                long arg4, long arg5, long arg6)
{
    long ret;
    struct stat st;
    struct kernel_statfs *stfs;
    
#ifdef DEBUG
    gemu_log("syscall %d\n", num);
#endif
    switch(num) {
    case TARGET_NR_exit:
#ifdef HAVE_GPROF
        _mcleanup();
#endif
        /* XXX: should free thread stack and CPU env */
        _exit(arg1);
        ret = 0; /* avoid warning */
        break;
    case TARGET_NR_read:
        page_unprotect_range((void *)arg2, arg3);
        ret = get_errno(read(arg1, (void *)arg2, arg3));
        break;
    case TARGET_NR_write:
        ret = get_errno(write(arg1, (void *)arg2, arg3));
        break;
    case TARGET_NR_open:
        ret = get_errno(open(path((const char *)arg1), arg2, arg3));
        break;
    case TARGET_NR_close:
        ret = get_errno(close(arg1));
        break;
    case TARGET_NR_brk:
        ret = do_brk((char *)arg1);
        break;
    case TARGET_NR_fork:
        ret = get_errno(do_fork(cpu_env, SIGCHLD, 0));
        break;
    case TARGET_NR_waitpid:
        {
            int *status = (int *)arg2;
            ret = get_errno(waitpid(arg1, status, arg3));
            if (!is_error(ret) && status)
                tswapls((long *)&status);
        }
        break;
    case TARGET_NR_creat:
        ret = get_errno(creat((const char *)arg1, arg2));
        break;
    case TARGET_NR_link:
        ret = get_errno(link((const char *)arg1, (const char *)arg2));
        break;
    case TARGET_NR_unlink:
        ret = get_errno(unlink((const char *)arg1));
        break;
    case TARGET_NR_execve:
        {
            char **argp, **envp;
            int argc, envc;
            uint32_t *p;
            char **q;

            argc = 0;
            for (p = (void *)arg2; *p; p++)
                argc++;
            envc = 0;
            for (p = (void *)arg3; *p; p++)
                envc++;

            argp = alloca((argc + 1) * sizeof(void *));
            envp = alloca((envc + 1) * sizeof(void *));

            for (p = (void *)arg2, q = argp; *p; p++, q++)
                *q = (void *)tswap32(*p);
            *q = NULL;

            for (p = (void *)arg3, q = envp; *p; p++, q++)
                *q = (void *)tswap32(*p);
            *q = NULL;

            ret = get_errno(execve((const char *)arg1, argp, envp));
        }
        break;
    case TARGET_NR_chdir:
        ret = get_errno(chdir((const char *)arg1));
        break;
    case TARGET_NR_time:
        {
            int *time_ptr = (int *)arg1;
            ret = get_errno(time((time_t *)time_ptr));
            if (!is_error(ret) && time_ptr)
                tswap32s(time_ptr);
        }
        break;
    case TARGET_NR_mknod:
        ret = get_errno(mknod((const char *)arg1, arg2, arg3));
        break;
    case TARGET_NR_chmod:
        ret = get_errno(chmod((const char *)arg1, arg2));
        break;
    case TARGET_NR_lchown:
        ret = get_errno(chown((const char *)arg1, arg2, arg3));
        break;
    case TARGET_NR_break:
        goto unimplemented;
    case TARGET_NR_oldstat:
        goto unimplemented;
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
        ret = get_errno(umount((const char *)arg1));
        break;
    case TARGET_NR_setuid:
        ret = get_errno(setuid(low2highuid(arg1)));
        break;
    case TARGET_NR_getuid:
        ret = get_errno(getuid());
        break;
    case TARGET_NR_stime:
        {
            int *time_ptr = (int *)arg1;
            if (time_ptr)
                tswap32s(time_ptr);
            ret = get_errno(stime((time_t *)time_ptr));
        }
        break;
    case TARGET_NR_ptrace:
        goto unimplemented;
    case TARGET_NR_alarm:
        ret = alarm(arg1);
        break;
    case TARGET_NR_oldfstat:
        goto unimplemented;
    case TARGET_NR_pause:
        ret = get_errno(pause());
        break;
    case TARGET_NR_utime:
        goto unimplemented;
    case TARGET_NR_stty:
        goto unimplemented;
    case TARGET_NR_gtty:
        goto unimplemented;
    case TARGET_NR_access:
        ret = get_errno(access((const char *)arg1, arg2));
        break;
    case TARGET_NR_nice:
        ret = get_errno(nice(arg1));
        break;
    case TARGET_NR_ftime:
        goto unimplemented;
    case TARGET_NR_sync:
        sync();
        ret = 0;
        break;
    case TARGET_NR_kill:
        ret = get_errno(kill(arg1, arg2));
        break;
    case TARGET_NR_rename:
        ret = get_errno(rename((const char *)arg1, (const char *)arg2));
        break;
    case TARGET_NR_mkdir:
        ret = get_errno(mkdir((const char *)arg1, arg2));
        break;
    case TARGET_NR_rmdir:
        ret = get_errno(rmdir((const char *)arg1));
        break;
    case TARGET_NR_dup:
        ret = get_errno(dup(arg1));
        break;
    case TARGET_NR_pipe:
        {
            int *pipe_ptr = (int *)arg1;
            ret = get_errno(pipe(pipe_ptr));
            if (!is_error(ret)) {
                tswap32s(&pipe_ptr[0]);
                tswap32s(&pipe_ptr[1]);
            }
        }
        break;
    case TARGET_NR_times:
        {
            struct target_tms *tmsp = (void *)arg1;
            struct tms tms;
            ret = get_errno(times(&tms));
            if (tmsp) {
                tmsp->tms_utime = tswapl(tms.tms_utime);
                tmsp->tms_stime = tswapl(tms.tms_stime);
                tmsp->tms_cutime = tswapl(tms.tms_cutime);
                tmsp->tms_cstime = tswapl(tms.tms_cstime);
            }
        }
        break;
    case TARGET_NR_prof:
        goto unimplemented;
    case TARGET_NR_setgid:
        ret = get_errno(setgid(low2highgid(arg1)));
        break;
    case TARGET_NR_getgid:
        ret = get_errno(getgid());
        break;
    case TARGET_NR_signal:
        goto unimplemented;
    case TARGET_NR_geteuid:
        ret = get_errno(geteuid());
        break;
    case TARGET_NR_getegid:
        ret = get_errno(getegid());
        break;
    case TARGET_NR_acct:
        goto unimplemented;
    case TARGET_NR_umount2:
        ret = get_errno(umount2((const char *)arg1, arg2));
        break;
    case TARGET_NR_lock:
        goto unimplemented;
    case TARGET_NR_ioctl:
        ret = do_ioctl(arg1, arg2, arg3);
        break;
    case TARGET_NR_fcntl:
        ret = get_errno(do_fcntl(arg1, arg2, arg3));
        break;
    case TARGET_NR_mpx:
        goto unimplemented;
    case TARGET_NR_setpgid:
        ret = get_errno(setpgid(arg1, arg2));
        break;
    case TARGET_NR_ulimit:
        goto unimplemented;
    case TARGET_NR_oldolduname:
        goto unimplemented;
    case TARGET_NR_umask:
        ret = get_errno(umask(arg1));
        break;
    case TARGET_NR_chroot:
        ret = get_errno(chroot((const char *)arg1));
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
            struct target_old_sigaction *old_act = (void *)arg2;
            struct target_old_sigaction *old_oact = (void *)arg3;
            struct target_sigaction act, oact, *pact;
            if (old_act) {
                act._sa_handler = old_act->_sa_handler;
                target_siginitset(&act.sa_mask, old_act->sa_mask);
                act.sa_flags = old_act->sa_flags;
                act.sa_restorer = old_act->sa_restorer;
                pact = &act;
            } else {
                pact = NULL;
            }
            ret = get_errno(do_sigaction(arg1, pact, &oact));
            if (!is_error(ret) && old_oact) {
                old_oact->_sa_handler = oact._sa_handler;
                old_oact->sa_mask = oact.sa_mask.sig[0];
                old_oact->sa_flags = oact.sa_flags;
                old_oact->sa_restorer = oact.sa_restorer;
            }
        }
        break;
    case TARGET_NR_rt_sigaction:
        ret = get_errno(do_sigaction(arg1, (void *)arg2, (void *)arg3));
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
            target_ulong *pset = (void *)arg2, *poldset = (void *)arg3;
            
            if (pset) {
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
                target_to_host_old_sigset(&set, pset);
                set_ptr = &set;
            } else {
                how = 0;
                set_ptr = NULL;
            }
            ret = get_errno(sigprocmask(arg1, set_ptr, &oldset));
            if (!is_error(ret) && poldset) {
                host_to_target_old_sigset(poldset, &oldset);
            }
        }
        break;
    case TARGET_NR_rt_sigprocmask:
        {
            int how = arg1;
            sigset_t set, oldset, *set_ptr;
            target_sigset_t *pset = (void *)arg2;
            target_sigset_t *poldset = (void *)arg3;
            
            if (pset) {
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
                target_to_host_sigset(&set, pset);
                set_ptr = &set;
            } else {
                how = 0;
                set_ptr = NULL;
            }
            ret = get_errno(sigprocmask(how, set_ptr, &oldset));
            if (!is_error(ret) && poldset) {
                host_to_target_sigset(poldset, &oldset);
            }
        }
        break;
    case TARGET_NR_sigpending:
        {
            sigset_t set;
            ret = get_errno(sigpending(&set));
            if (!is_error(ret)) {
                host_to_target_old_sigset((target_ulong *)arg1, &set);
            }
        }
        break;
    case TARGET_NR_rt_sigpending:
        {
            sigset_t set;
            ret = get_errno(sigpending(&set));
            if (!is_error(ret)) {
                host_to_target_sigset((target_sigset_t *)arg1, &set);
            }
        }
        break;
    case TARGET_NR_sigsuspend:
        {
            sigset_t set;
            target_to_host_old_sigset(&set, (target_ulong *)arg1);
            ret = get_errno(sigsuspend(&set));
        }
        break;
    case TARGET_NR_rt_sigsuspend:
        {
            sigset_t set;
            target_to_host_sigset(&set, (target_sigset_t *)arg1);
            ret = get_errno(sigsuspend(&set));
        }
        break;
    case TARGET_NR_rt_sigtimedwait:
        {
            target_sigset_t *target_set = (void *)arg1;
            target_siginfo_t *target_uinfo = (void *)arg2;
            struct target_timespec *target_uts = (void *)arg3;
            sigset_t set;
            struct timespec uts, *puts;
            siginfo_t uinfo;
            
            target_to_host_sigset(&set, target_set);
            if (target_uts) {
                puts = &uts;
                puts->tv_sec = tswapl(target_uts->tv_sec);
                puts->tv_nsec = tswapl(target_uts->tv_nsec);
            } else {
                puts = NULL;
            }
            ret = get_errno(sigtimedwait(&set, &uinfo, puts));
            if (!is_error(ret) && target_uinfo) {
                host_to_target_siginfo(target_uinfo, &uinfo);
            }
        }
        break;
    case TARGET_NR_rt_sigqueueinfo:
        {
            siginfo_t uinfo;
            target_to_host_siginfo(&uinfo, (target_siginfo_t *)arg3);
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
    case TARGET_NR_setreuid:
        ret = get_errno(setreuid(arg1, arg2));
        break;
    case TARGET_NR_setregid:
        ret = get_errno(setregid(arg1, arg2));
        break;
    case TARGET_NR_sethostname:
        ret = get_errno(sethostname((const char *)arg1, arg2));
        break;
    case TARGET_NR_setrlimit:
        {
            /* XXX: convert resource ? */
            int resource = arg1;
            struct target_rlimit *target_rlim = (void *)arg2;
            struct rlimit rlim;
            rlim.rlim_cur = tswapl(target_rlim->rlim_cur);
            rlim.rlim_max = tswapl(target_rlim->rlim_max);
            ret = get_errno(setrlimit(resource, &rlim));
        }
        break;
    case TARGET_NR_getrlimit:
        {
            /* XXX: convert resource ? */
            int resource = arg1;
            struct target_rlimit *target_rlim = (void *)arg2;
            struct rlimit rlim;
            
            ret = get_errno(getrlimit(resource, &rlim));
            if (!is_error(ret)) {
                target_rlim->rlim_cur = tswapl(rlim.rlim_cur);
                target_rlim->rlim_max = tswapl(rlim.rlim_max);
            }
        }
        break;
    case TARGET_NR_getrusage:
        {
            struct rusage rusage;
            struct target_rusage *target_rusage = (void *)arg2;
            ret = get_errno(getrusage(arg1, &rusage));
            if (!is_error(ret)) {
                host_to_target_rusage(target_rusage, &rusage);
            }
        }
        break;
    case TARGET_NR_gettimeofday:
        {
            struct target_timeval *target_tv = (void *)arg1;
            struct timeval tv;
            ret = get_errno(gettimeofday(&tv, NULL));
            if (!is_error(ret)) {
                host_to_target_timeval(target_tv, &tv);
            }
        }
        break;
    case TARGET_NR_settimeofday:
        {
            struct target_timeval *target_tv = (void *)arg1;
            struct timeval tv;
            target_to_host_timeval(&tv, target_tv);
            ret = get_errno(settimeofday(&tv, NULL));
        }
        break;
    case TARGET_NR_getgroups:
        {
            int gidsetsize = arg1;
            uint16_t *target_grouplist = (void *)arg2;
            gid_t *grouplist;
            int i;

            grouplist = alloca(gidsetsize * sizeof(gid_t));
            ret = get_errno(getgroups(gidsetsize, grouplist));
            if (!is_error(ret)) {
                for(i = 0;i < gidsetsize; i++)
                    target_grouplist[i] = tswap16(grouplist[i]);
            }
        }
        break;
    case TARGET_NR_setgroups:
        {
            int gidsetsize = arg1;
            uint16_t *target_grouplist = (void *)arg2;
            gid_t *grouplist;
            int i;

            grouplist = alloca(gidsetsize * sizeof(gid_t));
            for(i = 0;i < gidsetsize; i++)
                grouplist[i] = tswap16(target_grouplist[i]);
            ret = get_errno(setgroups(gidsetsize, grouplist));
        }
        break;
    case TARGET_NR_select:
        {
            struct target_sel_arg_struct *sel = (void *)arg1;
            sel->n = tswapl(sel->n);
            sel->inp = tswapl(sel->inp);
            sel->outp = tswapl(sel->outp);
            sel->exp = tswapl(sel->exp);
            sel->tvp = tswapl(sel->tvp);
            ret = do_select(sel->n, (void *)sel->inp, (void *)sel->outp,
                            (void *)sel->exp, (void *)sel->tvp);
        }
        break;
    case TARGET_NR_symlink:
        ret = get_errno(symlink((const char *)arg1, (const char *)arg2));
        break;
    case TARGET_NR_oldlstat:
        goto unimplemented;
    case TARGET_NR_readlink:
        ret = get_errno(readlink(path((const char *)arg1), (char *)arg2, arg3));
        break;
    case TARGET_NR_uselib:
        goto unimplemented;
    case TARGET_NR_swapon:
        ret = get_errno(swapon((const char *)arg1, arg2));
        break;
    case TARGET_NR_reboot:
        goto unimplemented;
    case TARGET_NR_readdir:
        goto unimplemented;
    case TARGET_NR_mmap:
#if defined(TARGET_I386) || defined(TARGET_ARM)
        {
            uint32_t v1, v2, v3, v4, v5, v6, *vptr;
            vptr = (uint32_t *)arg1;
            v1 = tswap32(vptr[0]);
            v2 = tswap32(vptr[1]);
            v3 = tswap32(vptr[2]);
            v4 = tswap32(vptr[3]);
            v5 = tswap32(vptr[4]);
            v6 = tswap32(vptr[5]);
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
    case TARGET_NR_mmap2:
        ret = get_errno(target_mmap(arg1, arg2, arg3, 
                                    target_to_host_bitmask(arg4, mmap_flags_tbl), 
                                    arg5,
                                    arg6 << TARGET_PAGE_BITS));
        break;
    case TARGET_NR_munmap:
        ret = get_errno(target_munmap(arg1, arg2));
        break;
    case TARGET_NR_mprotect:
        ret = get_errno(target_mprotect(arg1, arg2, arg3));
        break;
    case TARGET_NR_mremap:
        ret = get_errno(target_mremap(arg1, arg2, arg3, arg4, arg5));
        break;
    case TARGET_NR_msync:
        ret = get_errno(msync((void *)arg1, arg2, arg3));
        break;
    case TARGET_NR_mlock:
        ret = get_errno(mlock((void *)arg1, arg2));
        break;
    case TARGET_NR_munlock:
        ret = get_errno(munlock((void *)arg1, arg2));
        break;
    case TARGET_NR_mlockall:
        ret = get_errno(mlockall(arg1));
        break;
    case TARGET_NR_munlockall:
        ret = get_errno(munlockall());
        break;
    case TARGET_NR_truncate:
        ret = get_errno(truncate((const char *)arg1, arg2));
        break;
    case TARGET_NR_ftruncate:
        ret = get_errno(ftruncate(arg1, arg2));
        break;
    case TARGET_NR_fchmod:
        ret = get_errno(fchmod(arg1, arg2));
        break;
    case TARGET_NR_fchown:
        ret = get_errno(fchown(arg1, arg2, arg3));
        break;
    case TARGET_NR_getpriority:
        ret = get_errno(getpriority(arg1, arg2));
        break;
    case TARGET_NR_setpriority:
        ret = get_errno(setpriority(arg1, arg2, arg3));
        break;
    case TARGET_NR_profil:
        goto unimplemented;
    case TARGET_NR_statfs:
        stfs = (void *)arg2;
        ret = get_errno(sys_statfs(path((const char *)arg1), stfs));
    convert_statfs:
        if (!is_error(ret)) {
            tswap32s(&stfs->f_type);
            tswap32s(&stfs->f_bsize);
            tswap32s(&stfs->f_blocks);
            tswap32s(&stfs->f_bfree);
            tswap32s(&stfs->f_bavail);
            tswap32s(&stfs->f_files);
            tswap32s(&stfs->f_ffree);
            tswap32s(&stfs->f_fsid.val[0]);
            tswap32s(&stfs->f_fsid.val[1]);
            tswap32s(&stfs->f_namelen);
        }
        break;
    case TARGET_NR_fstatfs:
        stfs = (void *)arg2;
        ret = get_errno(sys_fstatfs(arg1, stfs));
        goto convert_statfs;
    case TARGET_NR_ioperm:
        goto unimplemented;
    case TARGET_NR_socketcall:
        ret = do_socketcall(arg1, (int32_t *)arg2);
        break;
    case TARGET_NR_syslog:
        goto unimplemented;
    case TARGET_NR_setitimer:
        {
            struct target_itimerval *target_value = (void *)arg2;
            struct target_itimerval *target_ovalue = (void *)arg3;
            struct itimerval value, ovalue, *pvalue;

            if (target_value) {
                pvalue = &value;
                target_to_host_timeval(&pvalue->it_interval, 
                                       &target_value->it_interval);
                target_to_host_timeval(&pvalue->it_value, 
                                       &target_value->it_value);
            } else {
                pvalue = NULL;
            }
            ret = get_errno(setitimer(arg1, pvalue, &ovalue));
            if (!is_error(ret) && target_ovalue) {
                host_to_target_timeval(&target_ovalue->it_interval, 
                                       &ovalue.it_interval);
                host_to_target_timeval(&target_ovalue->it_value, 
                                       &ovalue.it_value);
            }
        }
        break;
    case TARGET_NR_getitimer:
        {
            struct target_itimerval *target_value = (void *)arg2;
            struct itimerval value;
            
            ret = get_errno(getitimer(arg1, &value));
            if (!is_error(ret) && target_value) {
                host_to_target_timeval(&target_value->it_interval, 
                                       &value.it_interval);
                host_to_target_timeval(&target_value->it_value, 
                                       &value.it_value);
            }
        }
        break;
    case TARGET_NR_stat:
        ret = get_errno(stat(path((const char *)arg1), &st));
        goto do_stat;
    case TARGET_NR_lstat:
        ret = get_errno(lstat(path((const char *)arg1), &st));
        goto do_stat;
    case TARGET_NR_fstat:
        {
            ret = get_errno(fstat(arg1, &st));
        do_stat:
            if (!is_error(ret)) {
                struct target_stat *target_st = (void *)arg2;
                target_st->st_dev = tswap16(st.st_dev);
                target_st->st_ino = tswapl(st.st_ino);
                target_st->st_mode = tswap16(st.st_mode);
                target_st->st_nlink = tswap16(st.st_nlink);
                target_st->st_uid = tswap16(st.st_uid);
                target_st->st_gid = tswap16(st.st_gid);
                target_st->st_rdev = tswap16(st.st_rdev);
                target_st->st_size = tswapl(st.st_size);
                target_st->st_blksize = tswapl(st.st_blksize);
                target_st->st_blocks = tswapl(st.st_blocks);
                target_st->target_st_atime = tswapl(st.st_atime);
                target_st->target_st_mtime = tswapl(st.st_mtime);
                target_st->target_st_ctime = tswapl(st.st_ctime);
            }
        }
        break;
    case TARGET_NR_olduname:
        goto unimplemented;
    case TARGET_NR_iopl:
        goto unimplemented;
    case TARGET_NR_vhangup:
        ret = get_errno(vhangup());
        break;
    case TARGET_NR_idle:
        goto unimplemented;
    case TARGET_NR_wait4:
        {
            int status;
            target_long *status_ptr = (void *)arg2;
            struct rusage rusage, *rusage_ptr;
            struct target_rusage *target_rusage = (void *)arg4;
            if (target_rusage)
                rusage_ptr = &rusage;
            else
                rusage_ptr = NULL;
            ret = get_errno(wait4(arg1, &status, arg3, rusage_ptr));
            if (!is_error(ret)) {
                if (status_ptr)
                    *status_ptr = tswap32(status);
                if (target_rusage) {
                    host_to_target_rusage(target_rusage, &rusage);
                }
            }
        }
        break;
    case TARGET_NR_swapoff:
        ret = get_errno(swapoff((const char *)arg1));
        break;
    case TARGET_NR_sysinfo:
        goto unimplemented;
    case TARGET_NR_ipc:
        goto unimplemented;
    case TARGET_NR_fsync:
        ret = get_errno(fsync(arg1));
        break;
    case TARGET_NR_clone:
        ret = get_errno(do_fork(cpu_env, arg1, arg2));
        break;
#ifdef __NR_exit_group
        /* new thread calls */
    case TARGET_NR_exit_group:
        ret = get_errno(exit_group(arg1));
        break;
#endif
    case TARGET_NR_setdomainname:
        ret = get_errno(setdomainname((const char *)arg1, arg2));
        break;
    case TARGET_NR_uname:
        /* no need to transcode because we use the linux syscall */
        ret = get_errno(sys_uname((struct new_utsname *)arg1));
        break;
#ifdef TARGET_I386
    case TARGET_NR_modify_ldt:
        ret = get_errno(do_modify_ldt(cpu_env, arg1, (void *)arg2, arg3));
        break;
    case TARGET_NR_vm86old:
        goto unimplemented;
    case TARGET_NR_vm86:
        ret = do_vm86(cpu_env, arg1, (void *)arg2);
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
    case TARGET_NR_setfsuid:
        ret = get_errno(setfsuid(arg1));
        break;
    case TARGET_NR_setfsgid:
        ret = get_errno(setfsgid(arg1));
        break;
    case TARGET_NR__llseek:
        {
            int64_t res;
            ret = get_errno(_llseek(arg1, arg2, arg3, &res, arg5));
            *(int64_t *)arg4 = tswap64(res);
        }
        break;
    case TARGET_NR_getdents:
#if TARGET_LONG_SIZE != 4
#error not supported
#elif TARGET_LONG_SIZE == 4 && HOST_LONG_SIZE == 8
        {
            struct target_dirent *target_dirp = (void *)arg2;
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
		    strncpy(tde->d_name, de->d_name, tnamelen);
                    de = (struct dirent *)((char *)de + reclen);
                    len -= reclen;
                    tde = (struct dirent *)((char *)tde + treclen);
		    count1 += treclen;
                }
		ret = count1;
            }
	    free(dirp);
        }
#else
        {
            struct dirent *dirp = (void *)arg2;
            long count = arg3;

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
        }
#endif
        break;
    case TARGET_NR_getdents64:
        {
            struct dirent64 *dirp = (void *)arg2;
            long count = arg3;
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
        }
        break;
    case TARGET_NR__newselect:
        ret = do_select(arg1, (void *)arg2, (void *)arg3, (void *)arg4, 
                        (void *)arg5);
        break;
    case TARGET_NR_poll:
        {
            struct target_pollfd *target_pfd = (void *)arg1;
            unsigned int nfds = arg2;
            int timeout = arg3;
            struct pollfd *pfd;
            unsigned int i;

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
            }
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
            int i;
            struct iovec *vec;
            struct target_iovec *target_vec = (void *)arg2;

            vec = alloca(count * sizeof(struct iovec));
            for(i = 0;i < count; i++) {
                vec[i].iov_base = (void *)tswapl(target_vec[i].iov_base);
                vec[i].iov_len = tswapl(target_vec[i].iov_len);
            }
            ret = get_errno(readv(arg1, vec, count));
        }
        break;
    case TARGET_NR_writev:
        {
            int count = arg3;
            int i;
            struct iovec *vec;
            struct target_iovec *target_vec = (void *)arg2;

            vec = alloca(count * sizeof(struct iovec));
            for(i = 0;i < count; i++) {
                vec[i].iov_base = (void *)tswapl(target_vec[i].iov_base);
                vec[i].iov_len = tswapl(target_vec[i].iov_len);
            }
            ret = get_errno(writev(arg1, vec, count));
        }
        break;
    case TARGET_NR_getsid:
        ret = get_errno(getsid(arg1));
        break;
    case TARGET_NR_fdatasync:
        ret = get_errno(fdatasync(arg1));
        break;
    case TARGET_NR__sysctl:
        goto unimplemented;
    case TARGET_NR_sched_setparam:
        {
            struct sched_param *target_schp = (void *)arg2;
            struct sched_param schp;
            schp.sched_priority = tswap32(target_schp->sched_priority);
            ret = get_errno(sched_setparam(arg1, &schp));
        }
        break;
    case TARGET_NR_sched_getparam:
        {
            struct sched_param *target_schp = (void *)arg2;
            struct sched_param schp;
            ret = get_errno(sched_getparam(arg1, &schp));
            if (!is_error(ret)) {
                target_schp->sched_priority = tswap32(schp.sched_priority);
            }
        }
        break;
    case TARGET_NR_sched_setscheduler:
        {
            struct sched_param *target_schp = (void *)arg3;
            struct sched_param schp;
            schp.sched_priority = tswap32(target_schp->sched_priority);
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
            struct target_timespec *target_ts = (void *)arg2;
            struct timespec ts;
            ret = get_errno(sched_rr_get_interval(arg1, &ts));
            if (!is_error(ret)) {
                target_ts->tv_sec = tswapl(ts.tv_sec);
                target_ts->tv_nsec = tswapl(ts.tv_nsec);
            }
        }
        break;
    case TARGET_NR_nanosleep:
        {
            struct target_timespec *target_req = (void *)arg1;
            struct target_timespec *target_rem = (void *)arg2;
            struct timespec req, rem;
            req.tv_sec = tswapl(target_req->tv_sec);
            req.tv_nsec = tswapl(target_req->tv_nsec);
            ret = get_errno(nanosleep(&req, &rem));
            if (target_rem) {
                target_rem->tv_sec = tswapl(rem.tv_sec);
                target_rem->tv_nsec = tswapl(rem.tv_nsec);
            }
        }
        break;
    case TARGET_NR_setresuid:
        ret = get_errno(setresuid(low2highuid(arg1), 
                                  low2highuid(arg2), 
                                  low2highuid(arg3)));
        break;
    case TARGET_NR_getresuid:
        {
            int ruid, euid, suid;
            ret = get_errno(getresuid(&ruid, &euid, &suid));
            if (!is_error(ret)) {
                *(uint16_t *)arg1 = tswap16(high2lowuid(ruid));
                *(uint16_t *)arg2 = tswap16(high2lowuid(euid));
                *(uint16_t *)arg3 = tswap16(high2lowuid(suid));
            }
        }
        break;
    case TARGET_NR_setresgid:
        ret = get_errno(setresgid(low2highgid(arg1), 
                                  low2highgid(arg2), 
                                  low2highgid(arg3)));
        break;
    case TARGET_NR_getresgid:
        {
            int rgid, egid, sgid;
            ret = get_errno(getresgid(&rgid, &egid, &sgid));
            if (!is_error(ret)) {
                *(uint16_t *)arg1 = high2lowgid(tswap16(rgid));
                *(uint16_t *)arg2 = high2lowgid(tswap16(egid));
                *(uint16_t *)arg3 = high2lowgid(tswap16(sgid));
            }
        }
        break;
    case TARGET_NR_query_module:
        goto unimplemented;
    case TARGET_NR_nfsservctl:
        goto unimplemented;
    case TARGET_NR_prctl:
        goto unimplemented;
    case TARGET_NR_pread:
        page_unprotect_range((void *)arg2, arg3);
        ret = get_errno(pread(arg1, (void *)arg2, arg3, arg4));
        break;
    case TARGET_NR_pwrite:
        ret = get_errno(pwrite(arg1, (void *)arg2, arg3, arg4));
        break;
    case TARGET_NR_chown:
        ret = get_errno(chown((const char *)arg1, arg2, arg3));
        break;
    case TARGET_NR_getcwd:
        ret = get_errno(sys_getcwd1((char *)arg1, arg2));
        break;
    case TARGET_NR_capget:
        goto unimplemented;
    case TARGET_NR_capset:
        goto unimplemented;
    case TARGET_NR_sigaltstack:
        goto unimplemented;
    case TARGET_NR_sendfile:
        goto unimplemented;
    case TARGET_NR_getpmsg:
        goto unimplemented;
    case TARGET_NR_putpmsg:
        goto unimplemented;
    case TARGET_NR_vfork:
        ret = get_errno(do_fork(cpu_env, CLONE_VFORK | CLONE_VM | SIGCHLD, 0));
        break;
    case TARGET_NR_ugetrlimit:
    {
	struct rlimit rlim;
	ret = get_errno(getrlimit(arg1, &rlim));
	if (!is_error(ret)) {
	    struct target_rlimit *target_rlim = (void *)arg2;
	    target_rlim->rlim_cur = tswapl(rlim.rlim_cur);
	    target_rlim->rlim_max = tswapl(rlim.rlim_max);
	}
	break;
    }
    case TARGET_NR_truncate64:
        goto unimplemented;
    case TARGET_NR_ftruncate64:
        goto unimplemented;
    case TARGET_NR_stat64:
        ret = get_errno(stat(path((const char *)arg1), &st));
        goto do_stat64;
    case TARGET_NR_lstat64:
        ret = get_errno(lstat(path((const char *)arg1), &st));
        goto do_stat64;
    case TARGET_NR_fstat64:
        {
            ret = get_errno(fstat(arg1, &st));
        do_stat64:
            if (!is_error(ret)) {
                struct target_stat64 *target_st = (void *)arg2;
                memset(target_st, 0, sizeof(struct target_stat64));
                target_st->st_dev = tswap16(st.st_dev);
                target_st->st_ino = tswap64(st.st_ino);
#ifdef TARGET_STAT64_HAS_BROKEN_ST_INO
                target_st->__st_ino = tswapl(st.st_ino);
#endif
                target_st->st_mode = tswap32(st.st_mode);
                target_st->st_nlink = tswap32(st.st_nlink);
                target_st->st_uid = tswapl(st.st_uid);
                target_st->st_gid = tswapl(st.st_gid);
                target_st->st_rdev = tswap16(st.st_rdev);
                /* XXX: better use of kernel struct */
                target_st->st_size = tswap64(st.st_size);
                target_st->st_blksize = tswapl(st.st_blksize);
                target_st->st_blocks = tswapl(st.st_blocks);
                target_st->target_st_atime = tswapl(st.st_atime);
                target_st->target_st_mtime = tswapl(st.st_mtime);
                target_st->target_st_ctime = tswapl(st.st_ctime);
            }
        }
        break;

    case TARGET_NR_lchown32:
        ret = get_errno(lchown((const char *)arg1, arg2, arg3));
        break;
    case TARGET_NR_getuid32:
        ret = get_errno(getuid());
        break;
    case TARGET_NR_getgid32:
        ret = get_errno(getgid());
        break;
    case TARGET_NR_geteuid32:
        ret = get_errno(geteuid());
        break;
    case TARGET_NR_getegid32:
        ret = get_errno(getegid());
        break;
    case TARGET_NR_setreuid32:
        ret = get_errno(setreuid(arg1, arg2));
        break;
    case TARGET_NR_setregid32:
        ret = get_errno(setregid(arg1, arg2));
        break;
    case TARGET_NR_getgroups32:
        goto unimplemented;
    case TARGET_NR_setgroups32:
        goto unimplemented;
    case TARGET_NR_fchown32:
        ret = get_errno(fchown(arg1, arg2, arg3));
        break;
    case TARGET_NR_setresuid32:
        ret = get_errno(setresuid(arg1, arg2, arg3));
        break;
    case TARGET_NR_getresuid32:
        {
            int ruid, euid, suid;
            ret = get_errno(getresuid(&ruid, &euid, &suid));
            if (!is_error(ret)) {
                *(uint32_t *)arg1 = tswap32(ruid);
                *(uint32_t *)arg2 = tswap32(euid);
                *(uint32_t *)arg3 = tswap32(suid);
            }
        }
        break;
    case TARGET_NR_setresgid32:
        ret = get_errno(setresgid(arg1, arg2, arg3));
        break;
    case TARGET_NR_getresgid32:
        {
            int rgid, egid, sgid;
            ret = get_errno(getresgid(&rgid, &egid, &sgid));
            if (!is_error(ret)) {
                *(uint32_t *)arg1 = tswap32(rgid);
                *(uint32_t *)arg2 = tswap32(egid);
                *(uint32_t *)arg3 = tswap32(sgid);
            }
        }
        break;
    case TARGET_NR_chown32:
        ret = get_errno(chown((const char *)arg1, arg2, arg3));
        break;
    case TARGET_NR_setuid32:
        ret = get_errno(setuid(arg1));
        break;
    case TARGET_NR_setgid32:
        ret = get_errno(setgid(arg1));
        break;
    case TARGET_NR_setfsuid32:
        ret = get_errno(setfsuid(arg1));
        break;
    case TARGET_NR_setfsgid32:
        ret = get_errno(setfsgid(arg1));
        break;
    case TARGET_NR_pivot_root:
        goto unimplemented;
    case TARGET_NR_mincore:
        goto unimplemented;
    case TARGET_NR_madvise:
        goto unimplemented;
#if TARGET_LONG_BITS == 32
    case TARGET_NR_fcntl64:
    {
	struct flock64 fl;
	struct target_flock64 *target_fl = (void *)arg3;

        switch(arg2) {
        case F_GETLK64:
            ret = get_errno(fcntl(arg1, arg2, &fl));
	    if (ret == 0) {
		target_fl->l_type = tswap16(fl.l_type);
		target_fl->l_whence = tswap16(fl.l_whence);
		target_fl->l_start = tswap64(fl.l_start);
		target_fl->l_len = tswap64(fl.l_len);
		target_fl->l_pid = tswapl(fl.l_pid);
	    }
	    break;

        case F_SETLK64:
        case F_SETLKW64:
	    fl.l_type = tswap16(target_fl->l_type);
	    fl.l_whence = tswap16(target_fl->l_whence);
	    fl.l_start = tswap64(target_fl->l_start);
	    fl.l_len = tswap64(target_fl->l_len);
	    fl.l_pid = tswapl(target_fl->l_pid);
            ret = get_errno(fcntl(arg1, arg2, &fl));
	    break;
        default:
            ret = get_errno(do_fcntl(arg1, arg2, arg3));
            break;
        }
	break;
    }
#endif
    case TARGET_NR_security:
        goto unimplemented;
    case TARGET_NR_gettid:
        ret = get_errno(gettid());
        break;
    case TARGET_NR_readahead:
        goto unimplemented;
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
    case TARGET_NR_set_thread_area:
    case TARGET_NR_get_thread_area:
        goto unimplemented_nowarn;
    default:
    unimplemented:
        gemu_log("qemu: Unsupported syscall: %d\n", num);
    unimplemented_nowarn:
        ret = -ENOSYS;
        break;
    }
 fail:
    return ret;
}


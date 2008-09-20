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
#include <limits.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/prctl.h>
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
#include <sys/sem.h>
#include <sys/statfs.h>
#include <utime.h>
#include <sys/sysinfo.h>
//#include <sys/user.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <qemu-common.h>

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
#include "linux_loop.h"

#include "qemu.h"
#include "qemu-common.h"

#if defined(USE_NPTL)
#include <linux/futex.h>
#define CLONE_NPTL_FLAGS2 (CLONE_SETTLS | \
    CLONE_PARENT_SETTID | CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID)
#else
/* XXX: Hardcode the above values.  */
#define CLONE_NPTL_FLAGS2 0
#endif

//#define DEBUG

#if defined(TARGET_I386) || defined(TARGET_ARM) || defined(TARGET_SPARC) \
    || defined(TARGET_M68K) || defined(TARGET_SH4) || defined(TARGET_CRIS)
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
static type name (void)			\
{					\
	return syscall(__NR_##name);	\
}

#define _syscall1(type,name,type1,arg1)		\
static type name (type1 arg1)			\
{						\
	return syscall(__NR_##name, arg1);	\
}

#define _syscall2(type,name,type1,arg1,type2,arg2)	\
static type name (type1 arg1,type2 arg2)		\
{							\
	return syscall(__NR_##name, arg1, arg2);	\
}

#define _syscall3(type,name,type1,arg1,type2,arg2,type3,arg3)	\
static type name (type1 arg1,type2 arg2,type3 arg3)		\
{								\
	return syscall(__NR_##name, arg1, arg2, arg3);		\
}

#define _syscall4(type,name,type1,arg1,type2,arg2,type3,arg3,type4,arg4)	\
static type name (type1 arg1,type2 arg2,type3 arg3,type4 arg4)			\
{										\
	return syscall(__NR_##name, arg1, arg2, arg3, arg4);			\
}

#define _syscall5(type,name,type1,arg1,type2,arg2,type3,arg3,type4,arg4,	\
		  type5,arg5)							\
static type name (type1 arg1,type2 arg2,type3 arg3,type4 arg4,type5 arg5)	\
{										\
	return syscall(__NR_##name, arg1, arg2, arg3, arg4, arg5);		\
}


#define _syscall6(type,name,type1,arg1,type2,arg2,type3,arg3,type4,arg4,	\
		  type5,arg5,type6,arg6)					\
static type name (type1 arg1,type2 arg2,type3 arg3,type4 arg4,type5 arg5,	\
                  type6 arg6)							\
{										\
	return syscall(__NR_##name, arg1, arg2, arg3, arg4, arg5, arg6);	\
}


#define __NR_sys_uname __NR_uname
#define __NR_sys_faccessat __NR_faccessat
#define __NR_sys_fchmodat __NR_fchmodat
#define __NR_sys_fchownat __NR_fchownat
#define __NR_sys_getcwd1 __NR_getcwd
#define __NR_sys_getdents __NR_getdents
#define __NR_sys_getdents64 __NR_getdents64
#define __NR_sys_getpriority __NR_getpriority
#define __NR_sys_linkat __NR_linkat
#define __NR_sys_mkdirat __NR_mkdirat
#define __NR_sys_mknodat __NR_mknodat
#define __NR_sys_openat __NR_openat
#define __NR_sys_readlinkat __NR_readlinkat
#define __NR_sys_renameat __NR_renameat
#define __NR_sys_rt_sigqueueinfo __NR_rt_sigqueueinfo
#define __NR_sys_symlinkat __NR_symlinkat
#define __NR_sys_syslog __NR_syslog
#define __NR_sys_tgkill __NR_tgkill
#define __NR_sys_tkill __NR_tkill
#define __NR_sys_unlinkat __NR_unlinkat
#define __NR_sys_utimensat __NR_utimensat
#define __NR_sys_futex __NR_futex

#if defined(__alpha__) || defined (__ia64__) || defined(__x86_64__)
#define __NR__llseek __NR_lseek
#endif

#ifdef __NR_gettid
_syscall0(int, gettid)
#else
/* This is a replacement for the host gettid() and must return a host
   errno. */
static int gettid(void) {
    return -ENOSYS;
}
#endif
_syscall1(int,sys_uname,struct new_utsname *,buf)
#if defined(TARGET_NR_faccessat) && defined(__NR_faccessat)
_syscall4(int,sys_faccessat,int,dirfd,const char *,pathname,int,mode,int,flags)
#endif
#if defined(TARGET_NR_fchmodat) && defined(__NR_fchmodat)
_syscall4(int,sys_fchmodat,int,dirfd,const char *,pathname,
          mode_t,mode,int,flags)
#endif
#if defined(TARGET_NR_fchownat) && defined(__NR_fchownat) && defined(USE_UID16)
_syscall5(int,sys_fchownat,int,dirfd,const char *,pathname,
          uid_t,owner,gid_t,group,int,flags)
#endif
_syscall2(int,sys_getcwd1,char *,buf,size_t,size)
#if TARGET_ABI_BITS == 32
_syscall3(int, sys_getdents, uint, fd, struct dirent *, dirp, uint, count);
#endif
#if defined(TARGET_NR_getdents64) && defined(__NR_getdents64)
_syscall3(int, sys_getdents64, uint, fd, struct dirent64 *, dirp, uint, count);
#endif
_syscall2(int, sys_getpriority, int, which, int, who);
#if !defined (__x86_64__)
_syscall5(int, _llseek,  uint,  fd, ulong, hi, ulong, lo,
          loff_t *, res, uint, wh);
#endif
#if defined(TARGET_NR_linkat) && defined(__NR_linkat)
_syscall5(int,sys_linkat,int,olddirfd,const char *,oldpath,
	  int,newdirfd,const char *,newpath,int,flags)
#endif
#if defined(TARGET_NR_mkdirat) && defined(__NR_mkdirat)
_syscall3(int,sys_mkdirat,int,dirfd,const char *,pathname,mode_t,mode)
#endif
#if defined(TARGET_NR_mknodat) && defined(__NR_mknodat)
_syscall4(int,sys_mknodat,int,dirfd,const char *,pathname,
          mode_t,mode,dev_t,dev)
#endif
#if defined(TARGET_NR_openat) && defined(__NR_openat)
_syscall4(int,sys_openat,int,dirfd,const char *,pathname,int,flags,mode_t,mode)
#endif
#if defined(TARGET_NR_readlinkat) && defined(__NR_readlinkat)
_syscall4(int,sys_readlinkat,int,dirfd,const char *,pathname,
          char *,buf,size_t,bufsize)
#endif
#if defined(TARGET_NR_renameat) && defined(__NR_renameat)
_syscall4(int,sys_renameat,int,olddirfd,const char *,oldpath,
          int,newdirfd,const char *,newpath)
#endif
_syscall3(int,sys_rt_sigqueueinfo,int,pid,int,sig,siginfo_t *,uinfo)
#if defined(TARGET_NR_symlinkat) && defined(__NR_symlinkat)
_syscall3(int,sys_symlinkat,const char *,oldpath,
          int,newdirfd,const char *,newpath)
#endif
_syscall3(int,sys_syslog,int,type,char*,bufp,int,len)
#if defined(TARGET_NR_tgkill) && defined(__NR_tgkill)
_syscall3(int,sys_tgkill,int,tgid,int,pid,int,sig)
#endif
#if defined(TARGET_NR_tkill) && defined(__NR_tkill)
_syscall2(int,sys_tkill,int,tid,int,sig)
#endif
#ifdef __NR_exit_group
_syscall1(int,exit_group,int,error_code)
#endif
#if defined(TARGET_NR_set_tid_address) && defined(__NR_set_tid_address)
_syscall1(int,set_tid_address,int *,tidptr)
#endif
#if defined(TARGET_NR_unlinkat) && defined(__NR_unlinkat)
_syscall3(int,sys_unlinkat,int,dirfd,const char *,pathname,int,flags)
#endif
#if defined(TARGET_NR_utimensat) && defined(__NR_utimensat)
_syscall4(int,sys_utimensat,int,dirfd,const char *,pathname,
          const struct timespec *,tsp,int,flags)
#endif
#if defined(USE_NPTL)
#if defined(TARGET_NR_futex) && defined(__NR_futex)
_syscall6(int,sys_futex,int *,uaddr,int,op,int,val,
          const struct timespec *,timeout,int *,uaddr2,int,val3)
#endif
#endif

extern int personality(int);
extern int flock(int, int);
extern int setfsuid(int);
extern int setfsgid(int);
extern int setgroups(int, gid_t *);

#define ERRNO_TABLE_SIZE 1200

/* target_to_host_errno_table[] is initialized from
 * host_to_target_errno_table[] in syscall_init(). */
static uint16_t target_to_host_errno_table[ERRNO_TABLE_SIZE] = {
};

/*
 * This list is the union of errno values overridden in asm-<arch>/errno.h
 * minus the errnos that are not actually generic to all archs.
 */
static uint16_t host_to_target_errno_table[ERRNO_TABLE_SIZE] = {
    [EIDRM]		= TARGET_EIDRM,
    [ECHRNG]		= TARGET_ECHRNG,
    [EL2NSYNC]		= TARGET_EL2NSYNC,
    [EL3HLT]		= TARGET_EL3HLT,
    [EL3RST]		= TARGET_EL3RST,
    [ELNRNG]		= TARGET_ELNRNG,
    [EUNATCH]		= TARGET_EUNATCH,
    [ENOCSI]		= TARGET_ENOCSI,
    [EL2HLT]		= TARGET_EL2HLT,
    [EDEADLK]		= TARGET_EDEADLK,
    [ENOLCK]		= TARGET_ENOLCK,
    [EBADE]		= TARGET_EBADE,
    [EBADR]		= TARGET_EBADR,
    [EXFULL]		= TARGET_EXFULL,
    [ENOANO]		= TARGET_ENOANO,
    [EBADRQC]		= TARGET_EBADRQC,
    [EBADSLT]		= TARGET_EBADSLT,
    [EBFONT]		= TARGET_EBFONT,
    [ENOSTR]		= TARGET_ENOSTR,
    [ENODATA]		= TARGET_ENODATA,
    [ETIME]		= TARGET_ETIME,
    [ENOSR]		= TARGET_ENOSR,
    [ENONET]		= TARGET_ENONET,
    [ENOPKG]		= TARGET_ENOPKG,
    [EREMOTE]		= TARGET_EREMOTE,
    [ENOLINK]		= TARGET_ENOLINK,
    [EADV]		= TARGET_EADV,
    [ESRMNT]		= TARGET_ESRMNT,
    [ECOMM]		= TARGET_ECOMM,
    [EPROTO]		= TARGET_EPROTO,
    [EDOTDOT]		= TARGET_EDOTDOT,
    [EMULTIHOP]		= TARGET_EMULTIHOP,
    [EBADMSG]		= TARGET_EBADMSG,
    [ENAMETOOLONG]	= TARGET_ENAMETOOLONG,
    [EOVERFLOW]		= TARGET_EOVERFLOW,
    [ENOTUNIQ]		= TARGET_ENOTUNIQ,
    [EBADFD]		= TARGET_EBADFD,
    [EREMCHG]		= TARGET_EREMCHG,
    [ELIBACC]		= TARGET_ELIBACC,
    [ELIBBAD]		= TARGET_ELIBBAD,
    [ELIBSCN]		= TARGET_ELIBSCN,
    [ELIBMAX]		= TARGET_ELIBMAX,
    [ELIBEXEC]		= TARGET_ELIBEXEC,
    [EILSEQ]		= TARGET_EILSEQ,
    [ENOSYS]		= TARGET_ENOSYS,
    [ELOOP]		= TARGET_ELOOP,
    [ERESTART]		= TARGET_ERESTART,
    [ESTRPIPE]		= TARGET_ESTRPIPE,
    [ENOTEMPTY]		= TARGET_ENOTEMPTY,
    [EUSERS]		= TARGET_EUSERS,
    [ENOTSOCK]		= TARGET_ENOTSOCK,
    [EDESTADDRREQ]	= TARGET_EDESTADDRREQ,
    [EMSGSIZE]		= TARGET_EMSGSIZE,
    [EPROTOTYPE]	= TARGET_EPROTOTYPE,
    [ENOPROTOOPT]	= TARGET_ENOPROTOOPT,
    [EPROTONOSUPPORT]	= TARGET_EPROTONOSUPPORT,
    [ESOCKTNOSUPPORT]	= TARGET_ESOCKTNOSUPPORT,
    [EOPNOTSUPP]	= TARGET_EOPNOTSUPP,
    [EPFNOSUPPORT]	= TARGET_EPFNOSUPPORT,
    [EAFNOSUPPORT]	= TARGET_EAFNOSUPPORT,
    [EADDRINUSE]	= TARGET_EADDRINUSE,
    [EADDRNOTAVAIL]	= TARGET_EADDRNOTAVAIL,
    [ENETDOWN]		= TARGET_ENETDOWN,
    [ENETUNREACH]	= TARGET_ENETUNREACH,
    [ENETRESET]		= TARGET_ENETRESET,
    [ECONNABORTED]	= TARGET_ECONNABORTED,
    [ECONNRESET]	= TARGET_ECONNRESET,
    [ENOBUFS]		= TARGET_ENOBUFS,
    [EISCONN]		= TARGET_EISCONN,
    [ENOTCONN]		= TARGET_ENOTCONN,
    [EUCLEAN]		= TARGET_EUCLEAN,
    [ENOTNAM]		= TARGET_ENOTNAM,
    [ENAVAIL]		= TARGET_ENAVAIL,
    [EISNAM]		= TARGET_EISNAM,
    [EREMOTEIO]		= TARGET_EREMOTEIO,
    [ESHUTDOWN]		= TARGET_ESHUTDOWN,
    [ETOOMANYREFS]	= TARGET_ETOOMANYREFS,
    [ETIMEDOUT]		= TARGET_ETIMEDOUT,
    [ECONNREFUSED]	= TARGET_ECONNREFUSED,
    [EHOSTDOWN]		= TARGET_EHOSTDOWN,
    [EHOSTUNREACH]	= TARGET_EHOSTUNREACH,
    [EALREADY]		= TARGET_EALREADY,
    [EINPROGRESS]	= TARGET_EINPROGRESS,
    [ESTALE]		= TARGET_ESTALE,
    [ECANCELED]		= TARGET_ECANCELED,
    [ENOMEDIUM]		= TARGET_ENOMEDIUM,
    [EMEDIUMTYPE]	= TARGET_EMEDIUMTYPE,
#ifdef ENOKEY
    [ENOKEY]		= TARGET_ENOKEY,
#endif
#ifdef EKEYEXPIRED
    [EKEYEXPIRED]	= TARGET_EKEYEXPIRED,
#endif
#ifdef EKEYREVOKED
    [EKEYREVOKED]	= TARGET_EKEYREVOKED,
#endif
#ifdef EKEYREJECTED
    [EKEYREJECTED]	= TARGET_EKEYREJECTED,
#endif
#ifdef EOWNERDEAD
    [EOWNERDEAD]	= TARGET_EOWNERDEAD,
#endif
#ifdef ENOTRECOVERABLE
    [ENOTRECOVERABLE]	= TARGET_ENOTRECOVERABLE,
#endif
};

static inline int host_to_target_errno(int err)
{
    if(host_to_target_errno_table[err])
        return host_to_target_errno_table[err];
    return err;
}

static inline int target_to_host_errno(int err)
{
    if (target_to_host_errno_table[err])
        return target_to_host_errno_table[err];
    return err;
}

static inline abi_long get_errno(abi_long ret)
{
    if (ret == -1)
        return -host_to_target_errno(errno);
    else
        return ret;
}

static inline int is_error(abi_long ret)
{
    return (abi_ulong)ret >= (abi_ulong)(-4096);
}

char *target_strerror(int err)
{
    return strerror(target_to_host_errno(err));
}

static abi_ulong target_brk;
static abi_ulong target_original_brk;

void target_set_brk(abi_ulong new_brk)
{
    target_original_brk = target_brk = HOST_PAGE_ALIGN(new_brk);
}

/* do_brk() must return target values and target errnos. */
abi_long do_brk(abi_ulong new_brk)
{
    abi_ulong brk_page;
    abi_long mapped_addr;
    int	new_alloc_size;

    if (!new_brk)
        return target_brk;
    if (new_brk < target_original_brk)
        return target_brk;

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

    if (!is_error(mapped_addr))
	target_brk = new_brk;
    
    return target_brk;
}

static inline abi_long copy_from_user_fdset(fd_set *fds,
                                            abi_ulong target_fds_addr,
                                            int n)
{
    int i, nw, j, k;
    abi_ulong b, *target_fds;

    nw = (n + TARGET_ABI_BITS - 1) / TARGET_ABI_BITS;
    if (!(target_fds = lock_user(VERIFY_READ,
                                 target_fds_addr,
                                 sizeof(abi_ulong) * nw,
                                 1)))
        return -TARGET_EFAULT;

    FD_ZERO(fds);
    k = 0;
    for (i = 0; i < nw; i++) {
        /* grab the abi_ulong */
        __get_user(b, &target_fds[i]);
        for (j = 0; j < TARGET_ABI_BITS; j++) {
            /* check the bit inside the abi_ulong */
            if ((b >> j) & 1)
                FD_SET(k, fds);
            k++;
        }
    }

    unlock_user(target_fds, target_fds_addr, 0);

    return 0;
}

static inline abi_long copy_to_user_fdset(abi_ulong target_fds_addr,
                                          const fd_set *fds,
                                          int n)
{
    int i, nw, j, k;
    abi_long v;
    abi_ulong *target_fds;

    nw = (n + TARGET_ABI_BITS - 1) / TARGET_ABI_BITS;
    if (!(target_fds = lock_user(VERIFY_WRITE,
                                 target_fds_addr,
                                 sizeof(abi_ulong) * nw,
                                 0)))
        return -TARGET_EFAULT;

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

#if defined(__alpha__)
#define HOST_HZ 1024
#else
#define HOST_HZ 100
#endif

static inline abi_long host_to_target_clock_t(long ticks)
{
#if HOST_HZ == TARGET_HZ
    return ticks;
#else
    return ((int64_t)ticks * TARGET_HZ) / HOST_HZ;
#endif
}

static inline abi_long host_to_target_rusage(abi_ulong target_addr,
                                             const struct rusage *rusage)
{
    struct target_rusage *target_rusage;

    if (!lock_user_struct(VERIFY_WRITE, target_rusage, target_addr, 0))
        return -TARGET_EFAULT;
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

    return 0;
}

static inline abi_long copy_from_user_timeval(struct timeval *tv,
                                              abi_ulong target_tv_addr)
{
    struct target_timeval *target_tv;

    if (!lock_user_struct(VERIFY_READ, target_tv, target_tv_addr, 1))
        return -TARGET_EFAULT;

    __get_user(tv->tv_sec, &target_tv->tv_sec);
    __get_user(tv->tv_usec, &target_tv->tv_usec);

    unlock_user_struct(target_tv, target_tv_addr, 0);

    return 0;
}

static inline abi_long copy_to_user_timeval(abi_ulong target_tv_addr,
                                            const struct timeval *tv)
{
    struct target_timeval *target_tv;

    if (!lock_user_struct(VERIFY_WRITE, target_tv, target_tv_addr, 0))
        return -TARGET_EFAULT;

    __put_user(tv->tv_sec, &target_tv->tv_sec);
    __put_user(tv->tv_usec, &target_tv->tv_usec);

    unlock_user_struct(target_tv, target_tv_addr, 1);

    return 0;
}


/* do_select() must return target values and target errnos. */
static abi_long do_select(int n,
                          abi_ulong rfd_addr, abi_ulong wfd_addr,
                          abi_ulong efd_addr, abi_ulong target_tv_addr)
{
    fd_set rfds, wfds, efds;
    fd_set *rfds_ptr, *wfds_ptr, *efds_ptr;
    struct timeval tv, *tv_ptr;
    abi_long ret;

    if (rfd_addr) {
        if (copy_from_user_fdset(&rfds, rfd_addr, n))
            return -TARGET_EFAULT;
        rfds_ptr = &rfds;
    } else {
        rfds_ptr = NULL;
    }
    if (wfd_addr) {
        if (copy_from_user_fdset(&wfds, wfd_addr, n))
            return -TARGET_EFAULT;
        wfds_ptr = &wfds;
    } else {
        wfds_ptr = NULL;
    }
    if (efd_addr) {
        if (copy_from_user_fdset(&efds, efd_addr, n))
            return -TARGET_EFAULT;
        efds_ptr = &efds;
    } else {
        efds_ptr = NULL;
    }

    if (target_tv_addr) {
        if (copy_from_user_timeval(&tv, target_tv_addr))
            return -TARGET_EFAULT;
        tv_ptr = &tv;
    } else {
        tv_ptr = NULL;
    }

    ret = get_errno(select(n, rfds_ptr, wfds_ptr, efds_ptr, tv_ptr));

    if (!is_error(ret)) {
        if (rfd_addr && copy_to_user_fdset(rfd_addr, &rfds, n))
            return -TARGET_EFAULT;
        if (wfd_addr && copy_to_user_fdset(wfd_addr, &wfds, n))
            return -TARGET_EFAULT;
        if (efd_addr && copy_to_user_fdset(efd_addr, &efds, n))
            return -TARGET_EFAULT;

        if (target_tv_addr && copy_to_user_timeval(target_tv_addr, &tv))
            return -TARGET_EFAULT;
    }

    return ret;
}

static inline abi_long target_to_host_sockaddr(struct sockaddr *addr,
                                               abi_ulong target_addr,
                                               socklen_t len)
{
    struct target_sockaddr *target_saddr;

    target_saddr = lock_user(VERIFY_READ, target_addr, len, 1);
    if (!target_saddr)
        return -TARGET_EFAULT;
    memcpy(addr, target_saddr, len);
    addr->sa_family = tswap16(target_saddr->sa_family);
    unlock_user(target_saddr, target_addr, 0);

    return 0;
}

static inline abi_long host_to_target_sockaddr(abi_ulong target_addr,
                                               struct sockaddr *addr,
                                               socklen_t len)
{
    struct target_sockaddr *target_saddr;

    target_saddr = lock_user(VERIFY_WRITE, target_addr, len, 0);
    if (!target_saddr)
        return -TARGET_EFAULT;
    memcpy(target_saddr, addr, len);
    target_saddr->sa_family = tswap16(addr->sa_family);
    unlock_user(target_saddr, target_addr, len);

    return 0;
}

/* ??? Should this also swap msgh->name?  */
static inline abi_long target_to_host_cmsg(struct msghdr *msgh,
                                           struct target_msghdr *target_msgh)
{
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(msgh);
    abi_long msg_controllen;
    abi_ulong target_cmsg_addr;
    struct target_cmsghdr *target_cmsg;
    socklen_t space = 0;
    
    msg_controllen = tswapl(target_msgh->msg_controllen);
    if (msg_controllen < sizeof (struct target_cmsghdr)) 
        goto the_end;
    target_cmsg_addr = tswapl(target_msgh->msg_control);
    target_cmsg = lock_user(VERIFY_READ, target_cmsg_addr, msg_controllen, 1);
    if (!target_cmsg)
        return -TARGET_EFAULT;

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
    unlock_user(target_cmsg, target_cmsg_addr, 0);
 the_end:
    msgh->msg_controllen = space;
    return 0;
}

/* ??? Should this also swap msgh->name?  */
static inline abi_long host_to_target_cmsg(struct target_msghdr *target_msgh,
                                           struct msghdr *msgh)
{
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(msgh);
    abi_long msg_controllen;
    abi_ulong target_cmsg_addr;
    struct target_cmsghdr *target_cmsg;
    socklen_t space = 0;

    msg_controllen = tswapl(target_msgh->msg_controllen);
    if (msg_controllen < sizeof (struct target_cmsghdr)) 
        goto the_end;
    target_cmsg_addr = tswapl(target_msgh->msg_control);
    target_cmsg = lock_user(VERIFY_WRITE, target_cmsg_addr, msg_controllen, 0);
    if (!target_cmsg)
        return -TARGET_EFAULT;

    while (cmsg && target_cmsg) {
        void *data = CMSG_DATA(cmsg);
        void *target_data = TARGET_CMSG_DATA(target_cmsg);

        int len = cmsg->cmsg_len - CMSG_ALIGN(sizeof (struct cmsghdr));

        space += TARGET_CMSG_SPACE(len);
        if (space > msg_controllen) {
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
    unlock_user(target_cmsg, target_cmsg_addr, space);
 the_end:
    target_msgh->msg_controllen = tswapl(space);
    return 0;
}

/* do_setsockopt() Must return target values and target errnos. */
static abi_long do_setsockopt(int sockfd, int level, int optname,
                              abi_ulong optval_addr, socklen_t optlen)
{
    abi_long ret;
    int val;

    switch(level) {
    case SOL_TCP:
        /* TCP options all take an 'int' value.  */
        if (optlen < sizeof(uint32_t))
            return -TARGET_EINVAL;

        if (get_user_u32(val, optval_addr))
            return -TARGET_EFAULT;
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
                if (get_user_u32(val, optval_addr))
                    return -TARGET_EFAULT;
            } else if (optlen >= 1) {
                if (get_user_u8(val, optval_addr))
                    return -TARGET_EFAULT;
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
            return -TARGET_EINVAL;

	if (get_user_u32(val, optval_addr))
            return -TARGET_EFAULT;
	ret = get_errno(setsockopt(sockfd, SOL_SOCKET, optname, &val, sizeof(val)));
        break;
    default:
    unimplemented:
        gemu_log("Unsupported setsockopt level=%d optname=%d \n", level, optname);
        ret = -TARGET_ENOPROTOOPT;
    }
    return ret;
}

/* do_getsockopt() Must return target values and target errnos. */
static abi_long do_getsockopt(int sockfd, int level, int optname,
                              abi_ulong optval_addr, abi_ulong optlen)
{
    abi_long ret;
    int len, lv, val;

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
        if (get_user_u32(len, optlen))
            return -TARGET_EFAULT;
        if (len < 0)
            return -TARGET_EINVAL;
        lv = sizeof(int);
        ret = get_errno(getsockopt(sockfd, level, optname, &val, &lv));
        if (ret < 0)
            return ret;
        val = tswap32(val);
        if (len > lv)
            len = lv;
        if (len == 4) {
            if (put_user_u32(val, optval_addr))
                return -TARGET_EFAULT;
        } else {
            if (put_user_u8(val, optval_addr))
                return -TARGET_EFAULT;
	}
        if (put_user_u32(len, optlen))
            return -TARGET_EFAULT;
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
            if (get_user_u32(len, optlen))
                return -TARGET_EFAULT;
            if (len < 0)
                return -TARGET_EINVAL;
            lv = sizeof(int);
            ret = get_errno(getsockopt(sockfd, level, optname, &val, &lv));
            if (ret < 0)
                return ret;
            if (len < sizeof(int) && len > 0 && val >= 0 && val < 255) {
                len = 1;
                if (put_user_u32(len, optlen)
                    || put_user_u8(val, optval_addr))
                    return -TARGET_EFAULT;
            } else {
                if (len > sizeof(int))
                    len = sizeof(int);
                if (put_user_u32(len, optlen)
                    || put_user_u32(val, optval_addr))
                    return -TARGET_EFAULT;
            }
            break;
        default:
            ret = -TARGET_ENOPROTOOPT;
            break;
        }
        break;
    default:
    unimplemented:
        gemu_log("getsockopt level=%d optname=%d not yet supported\n",
                 level, optname);
        ret = -TARGET_EOPNOTSUPP;
        break;
    }
    return ret;
}

/* FIXME
 * lock_iovec()/unlock_iovec() have a return code of 0 for success where
 * other lock functions have a return code of 0 for failure.
 */
static abi_long lock_iovec(int type, struct iovec *vec, abi_ulong target_addr,
                           int count, int copy)
{
    struct target_iovec *target_vec;
    abi_ulong base;
    int i, j;

    target_vec = lock_user(VERIFY_READ, target_addr, count * sizeof(struct target_iovec), 1);
    if (!target_vec)
        return -TARGET_EFAULT;
    for(i = 0;i < count; i++) {
        base = tswapl(target_vec[i].iov_base);
        vec[i].iov_len = tswapl(target_vec[i].iov_len);
        if (vec[i].iov_len != 0) {
            vec[i].iov_base = lock_user(type, base, vec[i].iov_len, copy);
            if (!vec[i].iov_base && vec[i].iov_len) 
                goto fail;
        } else {
            /* zero length pointer is ignored */
            vec[i].iov_base = NULL;
        }
    }
    unlock_user (target_vec, target_addr, 0);
    return 0;
 fail:
    /* failure - unwind locks */
    for (j = 0; j < i; j++) {
        base = tswapl(target_vec[j].iov_base);
        unlock_user(vec[j].iov_base, base, 0);
    }
    unlock_user (target_vec, target_addr, 0);
    return -TARGET_EFAULT;
}

static abi_long unlock_iovec(struct iovec *vec, abi_ulong target_addr,
                             int count, int copy)
{
    struct target_iovec *target_vec;
    abi_ulong base;
    int i;

    target_vec = lock_user(VERIFY_READ, target_addr, count * sizeof(struct target_iovec), 1);
    if (!target_vec)
        return -TARGET_EFAULT;
    for(i = 0;i < count; i++) {
        base = tswapl(target_vec[i].iov_base);
        unlock_user(vec[i].iov_base, base, copy ? vec[i].iov_len : 0);
    }
    unlock_user (target_vec, target_addr, 0);

    return 0;
}

/* do_socket() Must return target values and target errnos. */
static abi_long do_socket(int domain, int type, int protocol)
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
    if (domain == PF_NETLINK)
        return -EAFNOSUPPORT; /* do not NETLINK socket connections possible */
    return get_errno(socket(domain, type, protocol));
}

/* do_bind() Must return target values and target errnos. */
static abi_long do_bind(int sockfd, abi_ulong target_addr,
                        socklen_t addrlen)
{
    void *addr = alloca(addrlen);

    target_to_host_sockaddr(addr, target_addr, addrlen);
    return get_errno(bind(sockfd, addr, addrlen));
}

/* do_connect() Must return target values and target errnos. */
static abi_long do_connect(int sockfd, abi_ulong target_addr,
                           socklen_t addrlen)
{
    void *addr = alloca(addrlen);

    target_to_host_sockaddr(addr, target_addr, addrlen);
    return get_errno(connect(sockfd, addr, addrlen));
}

/* do_sendrecvmsg() Must return target values and target errnos. */
static abi_long do_sendrecvmsg(int fd, abi_ulong target_msg,
                               int flags, int send)
{
    abi_long ret;
    struct target_msghdr *msgp;
    struct msghdr msg;
    int count;
    struct iovec *vec;
    abi_ulong target_vec;

    /* FIXME */
    if (!lock_user_struct(send ? VERIFY_READ : VERIFY_WRITE,
                          msgp,
                          target_msg,
                          send ? 1 : 0))
        return -TARGET_EFAULT;
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
    lock_iovec(send ? VERIFY_READ : VERIFY_WRITE, vec, target_vec, count, send);
    msg.msg_iovlen = count;
    msg.msg_iov = vec;

    if (send) {
        ret = target_to_host_cmsg(&msg, msgp);
        if (ret == 0)
            ret = get_errno(sendmsg(fd, &msg, flags));
    } else {
        ret = get_errno(recvmsg(fd, &msg, flags));
        if (!is_error(ret))
            ret = host_to_target_cmsg(msgp, &msg);
    }
    unlock_iovec(vec, target_vec, count, !send);
    unlock_user_struct(msgp, target_msg, send ? 0 : 1);
    return ret;
}

/* do_accept() Must return target values and target errnos. */
static abi_long do_accept(int fd, abi_ulong target_addr,
                          abi_ulong target_addrlen_addr)
{
    socklen_t addrlen;
    void *addr;
    abi_long ret;

    if (get_user_u32(addrlen, target_addrlen_addr))
        return -TARGET_EFAULT;

    addr = alloca(addrlen);

    ret = get_errno(accept(fd, addr, &addrlen));
    if (!is_error(ret)) {
        host_to_target_sockaddr(target_addr, addr, addrlen);
        if (put_user_u32(addrlen, target_addrlen_addr))
            ret = -TARGET_EFAULT;
    }
    return ret;
}

/* do_getpeername() Must return target values and target errnos. */
static abi_long do_getpeername(int fd, abi_ulong target_addr,
                               abi_ulong target_addrlen_addr)
{
    socklen_t addrlen;
    void *addr;
    abi_long ret;

    if (get_user_u32(addrlen, target_addrlen_addr))
        return -TARGET_EFAULT;

    addr = alloca(addrlen);

    ret = get_errno(getpeername(fd, addr, &addrlen));
    if (!is_error(ret)) {
        host_to_target_sockaddr(target_addr, addr, addrlen);
        if (put_user_u32(addrlen, target_addrlen_addr))
            ret = -TARGET_EFAULT;
    }
    return ret;
}

/* do_getsockname() Must return target values and target errnos. */
static abi_long do_getsockname(int fd, abi_ulong target_addr,
                               abi_ulong target_addrlen_addr)
{
    socklen_t addrlen;
    void *addr;
    abi_long ret;

    if (get_user_u32(addrlen, target_addrlen_addr))
        return -TARGET_EFAULT;

    addr = alloca(addrlen);

    ret = get_errno(getsockname(fd, addr, &addrlen));
    if (!is_error(ret)) {
        host_to_target_sockaddr(target_addr, addr, addrlen);
        if (put_user_u32(addrlen, target_addrlen_addr))
            ret = -TARGET_EFAULT;
    }
    return ret;
}

/* do_socketpair() Must return target values and target errnos. */
static abi_long do_socketpair(int domain, int type, int protocol,
                              abi_ulong target_tab_addr)
{
    int tab[2];
    abi_long ret;

    ret = get_errno(socketpair(domain, type, protocol, tab));
    if (!is_error(ret)) {
        if (put_user_s32(tab[0], target_tab_addr)
            || put_user_s32(tab[1], target_tab_addr + sizeof(tab[0])))
            ret = -TARGET_EFAULT;
    }
    return ret;
}

/* do_sendto() Must return target values and target errnos. */
static abi_long do_sendto(int fd, abi_ulong msg, size_t len, int flags,
                          abi_ulong target_addr, socklen_t addrlen)
{
    void *addr;
    void *host_msg;
    abi_long ret;

    host_msg = lock_user(VERIFY_READ, msg, len, 1);
    if (!host_msg)
        return -TARGET_EFAULT;
    if (target_addr) {
        addr = alloca(addrlen);
        target_to_host_sockaddr(addr, target_addr, addrlen);
        ret = get_errno(sendto(fd, host_msg, len, flags, addr, addrlen));
    } else {
        ret = get_errno(send(fd, host_msg, len, flags));
    }
    unlock_user(host_msg, msg, 0);
    return ret;
}

/* do_recvfrom() Must return target values and target errnos. */
static abi_long do_recvfrom(int fd, abi_ulong msg, size_t len, int flags,
                            abi_ulong target_addr,
                            abi_ulong target_addrlen)
{
    socklen_t addrlen;
    void *addr;
    void *host_msg;
    abi_long ret;

    host_msg = lock_user(VERIFY_WRITE, msg, len, 0);
    if (!host_msg)
        return -TARGET_EFAULT;
    if (target_addr) {
        if (get_user_u32(addrlen, target_addrlen)) {
            ret = -TARGET_EFAULT;
            goto fail;
        }
        addr = alloca(addrlen);
        ret = get_errno(recvfrom(fd, host_msg, len, flags, addr, &addrlen));
    } else {
        addr = NULL; /* To keep compiler quiet.  */
        ret = get_errno(recv(fd, host_msg, len, flags));
    }
    if (!is_error(ret)) {
        if (target_addr) {
            host_to_target_sockaddr(target_addr, addr, addrlen);
            if (put_user_u32(addrlen, target_addrlen)) {
                ret = -TARGET_EFAULT;
                goto fail;
            }
        }
        unlock_user(host_msg, msg, len);
    } else {
fail:
        unlock_user(host_msg, msg, 0);
    }
    return ret;
}

#ifdef TARGET_NR_socketcall
/* do_socketcall() Must return target values and target errnos. */
static abi_long do_socketcall(int num, abi_ulong vptr)
{
    abi_long ret;
    const int n = sizeof(abi_ulong);

    switch(num) {
    case SOCKOP_socket:
	{
            int domain, type, protocol;

            if (get_user_s32(domain, vptr)
                || get_user_s32(type, vptr + n)
                || get_user_s32(protocol, vptr + 2 * n))
                return -TARGET_EFAULT;

            ret = do_socket(domain, type, protocol);
	}
        break;
    case SOCKOP_bind:
	{
            int sockfd;
            abi_ulong target_addr;
            socklen_t addrlen;

            if (get_user_s32(sockfd, vptr)
                || get_user_ual(target_addr, vptr + n)
                || get_user_u32(addrlen, vptr + 2 * n))
                return -TARGET_EFAULT;

            ret = do_bind(sockfd, target_addr, addrlen);
        }
        break;
    case SOCKOP_connect:
        {
            int sockfd;
            abi_ulong target_addr;
            socklen_t addrlen;

            if (get_user_s32(sockfd, vptr)
                || get_user_ual(target_addr, vptr + n)
                || get_user_u32(addrlen, vptr + 2 * n))
                return -TARGET_EFAULT;

            ret = do_connect(sockfd, target_addr, addrlen);
        }
        break;
    case SOCKOP_listen:
        {
            int sockfd, backlog;

            if (get_user_s32(sockfd, vptr)
                || get_user_s32(backlog, vptr + n))
                return -TARGET_EFAULT;

            ret = get_errno(listen(sockfd, backlog));
        }
        break;
    case SOCKOP_accept:
        {
            int sockfd;
            abi_ulong target_addr, target_addrlen;

            if (get_user_s32(sockfd, vptr)
                || get_user_ual(target_addr, vptr + n)
                || get_user_u32(target_addrlen, vptr + 2 * n))
                return -TARGET_EFAULT;

            ret = do_accept(sockfd, target_addr, target_addrlen);
        }
        break;
    case SOCKOP_getsockname:
        {
            int sockfd;
            abi_ulong target_addr, target_addrlen;

            if (get_user_s32(sockfd, vptr)
                || get_user_ual(target_addr, vptr + n)
                || get_user_u32(target_addrlen, vptr + 2 * n))
                return -TARGET_EFAULT;

            ret = do_getsockname(sockfd, target_addr, target_addrlen);
        }
        break;
    case SOCKOP_getpeername:
        {
            int sockfd;
            abi_ulong target_addr, target_addrlen;

            if (get_user_s32(sockfd, vptr)
                || get_user_ual(target_addr, vptr + n)
                || get_user_u32(target_addrlen, vptr + 2 * n))
                return -TARGET_EFAULT;

            ret = do_getpeername(sockfd, target_addr, target_addrlen);
        }
        break;
    case SOCKOP_socketpair:
        {
            int domain, type, protocol;
            abi_ulong tab;

            if (get_user_s32(domain, vptr)
                || get_user_s32(type, vptr + n)
                || get_user_s32(protocol, vptr + 2 * n)
                || get_user_ual(tab, vptr + 3 * n))
                return -TARGET_EFAULT;

            ret = do_socketpair(domain, type, protocol, tab);
        }
        break;
    case SOCKOP_send:
        {
            int sockfd;
            abi_ulong msg;
            size_t len;
            int flags;

            if (get_user_s32(sockfd, vptr)
                || get_user_ual(msg, vptr + n)
                || get_user_ual(len, vptr + 2 * n)
                || get_user_s32(flags, vptr + 3 * n))
                return -TARGET_EFAULT;

            ret = do_sendto(sockfd, msg, len, flags, 0, 0);
        }
        break;
    case SOCKOP_recv:
        {
            int sockfd;
            abi_ulong msg;
            size_t len;
            int flags;

            if (get_user_s32(sockfd, vptr)
                || get_user_ual(msg, vptr + n)
                || get_user_ual(len, vptr + 2 * n)
                || get_user_s32(flags, vptr + 3 * n))
                return -TARGET_EFAULT;

            ret = do_recvfrom(sockfd, msg, len, flags, 0, 0);
        }
        break;
    case SOCKOP_sendto:
        {
            int sockfd;
            abi_ulong msg;
            size_t len;
            int flags;
            abi_ulong addr;
            socklen_t addrlen;

            if (get_user_s32(sockfd, vptr)
                || get_user_ual(msg, vptr + n)
                || get_user_ual(len, vptr + 2 * n)
                || get_user_s32(flags, vptr + 3 * n)
                || get_user_ual(addr, vptr + 4 * n)
                || get_user_u32(addrlen, vptr + 5 * n))
                return -TARGET_EFAULT;

            ret = do_sendto(sockfd, msg, len, flags, addr, addrlen);
        }
        break;
    case SOCKOP_recvfrom:
        {
            int sockfd;
            abi_ulong msg;
            size_t len;
            int flags;
            abi_ulong addr;
            socklen_t addrlen;

            if (get_user_s32(sockfd, vptr)
                || get_user_ual(msg, vptr + n)
                || get_user_ual(len, vptr + 2 * n)
                || get_user_s32(flags, vptr + 3 * n)
                || get_user_ual(addr, vptr + 4 * n)
                || get_user_u32(addrlen, vptr + 5 * n))
                return -TARGET_EFAULT;

            ret = do_recvfrom(sockfd, msg, len, flags, addr, addrlen);
        }
        break;
    case SOCKOP_shutdown:
        {
            int sockfd, how;

            if (get_user_s32(sockfd, vptr)
                || get_user_s32(how, vptr + n))
                return -TARGET_EFAULT;

            ret = get_errno(shutdown(sockfd, how));
        }
        break;
    case SOCKOP_sendmsg:
    case SOCKOP_recvmsg:
        {
            int fd;
            abi_ulong target_msg;
            int flags;

            if (get_user_s32(fd, vptr)
                || get_user_ual(target_msg, vptr + n)
                || get_user_s32(flags, vptr + 2 * n))
                return -TARGET_EFAULT;

            ret = do_sendrecvmsg(fd, target_msg, flags,
                                 (num == SOCKOP_sendmsg));
        }
        break;
    case SOCKOP_setsockopt:
        {
            int sockfd;
            int level;
            int optname;
            abi_ulong optval;
            socklen_t optlen;

            if (get_user_s32(sockfd, vptr)
                || get_user_s32(level, vptr + n)
                || get_user_s32(optname, vptr + 2 * n)
                || get_user_ual(optval, vptr + 3 * n)
                || get_user_u32(optlen, vptr + 4 * n))
                return -TARGET_EFAULT;

            ret = do_setsockopt(sockfd, level, optname, optval, optlen);
        }
        break;
    case SOCKOP_getsockopt:
        {
            int sockfd;
            int level;
            int optname;
            abi_ulong optval;
            socklen_t optlen;

            if (get_user_s32(sockfd, vptr)
                || get_user_s32(level, vptr + n)
                || get_user_s32(optname, vptr + 2 * n)
                || get_user_ual(optval, vptr + 3 * n)
                || get_user_u32(optlen, vptr + 4 * n))
                return -TARGET_EFAULT;

            ret = do_getsockopt(sockfd, level, optname, optval, optlen);
        }
        break;
    default:
        gemu_log("Unsupported socketcall: %d\n", num);
        ret = -TARGET_ENOSYS;
        break;
    }
    return ret;
}
#endif

#ifdef TARGET_NR_ipc
#define N_SHM_REGIONS	32

static struct shm_region {
    abi_ulong	start;
    abi_ulong	size;
} shm_regions[N_SHM_REGIONS];

struct target_ipc_perm
{
    abi_long __key;
    abi_ulong uid;
    abi_ulong gid;
    abi_ulong cuid;
    abi_ulong cgid;
    unsigned short int mode;
    unsigned short int __pad1;
    unsigned short int __seq;
    unsigned short int __pad2;
    abi_ulong __unused1;
    abi_ulong __unused2;
};

struct target_semid_ds
{
  struct target_ipc_perm sem_perm;
  abi_ulong sem_otime;
  abi_ulong __unused1;
  abi_ulong sem_ctime;
  abi_ulong __unused2;
  abi_ulong sem_nsems;
  abi_ulong __unused3;
  abi_ulong __unused4;
};

static inline abi_long target_to_host_ipc_perm(struct ipc_perm *host_ip,
                                               abi_ulong target_addr)
{
    struct target_ipc_perm *target_ip;
    struct target_semid_ds *target_sd;

    if (!lock_user_struct(VERIFY_READ, target_sd, target_addr, 1))
        return -TARGET_EFAULT;
    target_ip=&(target_sd->sem_perm);
    host_ip->__key = tswapl(target_ip->__key);
    host_ip->uid = tswapl(target_ip->uid);
    host_ip->gid = tswapl(target_ip->gid);
    host_ip->cuid = tswapl(target_ip->cuid);
    host_ip->cgid = tswapl(target_ip->cgid);
    host_ip->mode = tswapl(target_ip->mode);
    unlock_user_struct(target_sd, target_addr, 0);
    return 0;
}

static inline abi_long host_to_target_ipc_perm(abi_ulong target_addr,
                                               struct ipc_perm *host_ip)
{
    struct target_ipc_perm *target_ip;
    struct target_semid_ds *target_sd;

    if (!lock_user_struct(VERIFY_WRITE, target_sd, target_addr, 0))
        return -TARGET_EFAULT;
    target_ip = &(target_sd->sem_perm);
    target_ip->__key = tswapl(host_ip->__key);
    target_ip->uid = tswapl(host_ip->uid);
    target_ip->gid = tswapl(host_ip->gid);
    target_ip->cuid = tswapl(host_ip->cuid);
    target_ip->cgid = tswapl(host_ip->cgid);
    target_ip->mode = tswapl(host_ip->mode);
    unlock_user_struct(target_sd, target_addr, 1);
    return 0;
}

static inline abi_long target_to_host_semid_ds(struct semid_ds *host_sd,
                                               abi_ulong target_addr)
{
    struct target_semid_ds *target_sd;

    if (!lock_user_struct(VERIFY_READ, target_sd, target_addr, 1))
        return -TARGET_EFAULT;
    target_to_host_ipc_perm(&(host_sd->sem_perm),target_addr);
    host_sd->sem_nsems = tswapl(target_sd->sem_nsems);
    host_sd->sem_otime = tswapl(target_sd->sem_otime);
    host_sd->sem_ctime = tswapl(target_sd->sem_ctime);
    unlock_user_struct(target_sd, target_addr, 0);
    return 0;
}

static inline abi_long host_to_target_semid_ds(abi_ulong target_addr,
                                               struct semid_ds *host_sd)
{
    struct target_semid_ds *target_sd;

    if (!lock_user_struct(VERIFY_WRITE, target_sd, target_addr, 0))
        return -TARGET_EFAULT;
    host_to_target_ipc_perm(target_addr,&(host_sd->sem_perm));
    target_sd->sem_nsems = tswapl(host_sd->sem_nsems);
    target_sd->sem_otime = tswapl(host_sd->sem_otime);
    target_sd->sem_ctime = tswapl(host_sd->sem_ctime);
    unlock_user_struct(target_sd, target_addr, 1);
    return 0;
}

union semun {
	int val;
	struct semid_ds *buf;
	unsigned short *array;
};

union target_semun {
	int val;
	abi_long buf;
	unsigned short int *array;
};

static inline abi_long target_to_host_semun(int cmd,
                                            union semun *host_su,
                                            abi_ulong target_addr,
                                            struct semid_ds *ds)
{
    union target_semun *target_su;

    switch( cmd ) {
	case IPC_STAT:
	case IPC_SET:
           if (!lock_user_struct(VERIFY_READ, target_su, target_addr, 1))
               return -TARGET_EFAULT;
	   target_to_host_semid_ds(ds,target_su->buf);
	   host_su->buf = ds;
           unlock_user_struct(target_su, target_addr, 0);
	   break;
	case GETVAL:
	case SETVAL:
           if (!lock_user_struct(VERIFY_READ, target_su, target_addr, 1))
               return -TARGET_EFAULT;
	   host_su->val = tswapl(target_su->val);
           unlock_user_struct(target_su, target_addr, 0);
	   break;
	case GETALL:
	case SETALL:
           if (!lock_user_struct(VERIFY_READ, target_su, target_addr, 1))
               return -TARGET_EFAULT;
	   *host_su->array = tswap16(*target_su->array);
           unlock_user_struct(target_su, target_addr, 0);
	   break;
	default:
           gemu_log("semun operation not fully supported: %d\n", (int)cmd);
    }
    return 0;
}

static inline abi_long host_to_target_semun(int cmd,
                                            abi_ulong target_addr,
                                            union semun *host_su,
                                            struct semid_ds *ds)
{
    union target_semun *target_su;

    switch( cmd ) {
	case IPC_STAT:
	case IPC_SET:
           if (lock_user_struct(VERIFY_WRITE, target_su, target_addr, 0))
               return -TARGET_EFAULT;
	   host_to_target_semid_ds(target_su->buf,ds);
           unlock_user_struct(target_su, target_addr, 1);
	   break;
	case GETVAL:
	case SETVAL:
           if (lock_user_struct(VERIFY_WRITE, target_su, target_addr, 0))
               return -TARGET_EFAULT;
	   target_su->val = tswapl(host_su->val);
           unlock_user_struct(target_su, target_addr, 1);
	   break;
	case GETALL:
	case SETALL:
           if (lock_user_struct(VERIFY_WRITE, target_su, target_addr, 0))
               return -TARGET_EFAULT;
	   *target_su->array = tswap16(*host_su->array);
           unlock_user_struct(target_su, target_addr, 1);
	   break;
        default:
           gemu_log("semun operation not fully supported: %d\n", (int)cmd);
    }
    return 0;
}

static inline abi_long do_semctl(int first, int second, int third,
                                 abi_long ptr)
{
    union semun arg;
    struct semid_ds dsarg;
    int cmd = third&0xff;
    abi_long ret = 0;

    switch( cmd ) {
	case GETVAL:
            target_to_host_semun(cmd,&arg,ptr,&dsarg);
            ret = get_errno(semctl(first, second, cmd, arg));
            host_to_target_semun(cmd,ptr,&arg,&dsarg);
            break;
	case SETVAL:
            target_to_host_semun(cmd,&arg,ptr,&dsarg);
            ret = get_errno(semctl(first, second, cmd, arg));
            host_to_target_semun(cmd,ptr,&arg,&dsarg);
            break;
	case GETALL:
            target_to_host_semun(cmd,&arg,ptr,&dsarg);
            ret = get_errno(semctl(first, second, cmd, arg));
            host_to_target_semun(cmd,ptr,&arg,&dsarg);
            break;
	case SETALL:
            target_to_host_semun(cmd,&arg,ptr,&dsarg);
            ret = get_errno(semctl(first, second, cmd, arg));
            host_to_target_semun(cmd,ptr,&arg,&dsarg);
            break;
	case IPC_STAT:
            target_to_host_semun(cmd,&arg,ptr,&dsarg);
            ret = get_errno(semctl(first, second, cmd, arg));
            host_to_target_semun(cmd,ptr,&arg,&dsarg);
            break;
	case IPC_SET:
            target_to_host_semun(cmd,&arg,ptr,&dsarg);
            ret = get_errno(semctl(first, second, cmd, arg));
            host_to_target_semun(cmd,ptr,&arg,&dsarg);
            break;
    default:
            ret = get_errno(semctl(first, second, cmd, arg));
    }

    return ret;
}

struct target_msqid_ds
{
  struct target_ipc_perm msg_perm;
  abi_ulong msg_stime;
  abi_ulong __unused1;
  abi_ulong msg_rtime;
  abi_ulong __unused2;
  abi_ulong msg_ctime;
  abi_ulong __unused3;
  abi_ulong __msg_cbytes;
  abi_ulong msg_qnum;
  abi_ulong msg_qbytes;
  abi_ulong msg_lspid;
  abi_ulong msg_lrpid;
  abi_ulong __unused4;
  abi_ulong __unused5;
};

static inline abi_long target_to_host_msqid_ds(struct msqid_ds *host_md,
                                               abi_ulong target_addr)
{
    struct target_msqid_ds *target_md;

    if (!lock_user_struct(VERIFY_READ, target_md, target_addr, 1))
        return -TARGET_EFAULT;
    target_to_host_ipc_perm(&(host_md->msg_perm),target_addr);
    host_md->msg_stime = tswapl(target_md->msg_stime);
    host_md->msg_rtime = tswapl(target_md->msg_rtime);
    host_md->msg_ctime = tswapl(target_md->msg_ctime);
    host_md->__msg_cbytes = tswapl(target_md->__msg_cbytes);
    host_md->msg_qnum = tswapl(target_md->msg_qnum);
    host_md->msg_qbytes = tswapl(target_md->msg_qbytes);
    host_md->msg_lspid = tswapl(target_md->msg_lspid);
    host_md->msg_lrpid = tswapl(target_md->msg_lrpid);
    unlock_user_struct(target_md, target_addr, 0);
    return 0;
}

static inline abi_long host_to_target_msqid_ds(abi_ulong target_addr,
                                               struct msqid_ds *host_md)
{
    struct target_msqid_ds *target_md;

    if (!lock_user_struct(VERIFY_WRITE, target_md, target_addr, 0))
        return -TARGET_EFAULT;
    host_to_target_ipc_perm(target_addr,&(host_md->msg_perm));
    target_md->msg_stime = tswapl(host_md->msg_stime);
    target_md->msg_rtime = tswapl(host_md->msg_rtime);
    target_md->msg_ctime = tswapl(host_md->msg_ctime);
    target_md->__msg_cbytes = tswapl(host_md->__msg_cbytes);
    target_md->msg_qnum = tswapl(host_md->msg_qnum);
    target_md->msg_qbytes = tswapl(host_md->msg_qbytes);
    target_md->msg_lspid = tswapl(host_md->msg_lspid);
    target_md->msg_lrpid = tswapl(host_md->msg_lrpid);
    unlock_user_struct(target_md, target_addr, 1);
    return 0;
}

static inline abi_long do_msgctl(int first, int second, abi_long ptr)
{
    struct msqid_ds dsarg;
    int cmd = second&0xff;
    abi_long ret = 0;
    switch( cmd ) {
    case IPC_STAT:
    case IPC_SET:
        target_to_host_msqid_ds(&dsarg,ptr);
        ret = get_errno(msgctl(first, cmd, &dsarg));
        host_to_target_msqid_ds(ptr,&dsarg);
    default:
        ret = get_errno(msgctl(first, cmd, &dsarg));
    }
    return ret;
}

struct target_msgbuf {
	abi_ulong mtype;
	char	mtext[1];
};

static inline abi_long do_msgsnd(int msqid, abi_long msgp,
                                 unsigned int msgsz, int msgflg)
{
    struct target_msgbuf *target_mb;
    struct msgbuf *host_mb;
    abi_long ret = 0;

    if (!lock_user_struct(VERIFY_READ, target_mb, msgp, 0))
        return -TARGET_EFAULT;
    host_mb = malloc(msgsz+sizeof(long));
    host_mb->mtype = tswapl(target_mb->mtype);
    memcpy(host_mb->mtext,target_mb->mtext,msgsz);
    ret = get_errno(msgsnd(msqid, host_mb, msgsz, msgflg));
    free(host_mb);
    unlock_user_struct(target_mb, msgp, 0);

    return ret;
}

static inline abi_long do_msgrcv(int msqid, abi_long msgp,
                                 unsigned int msgsz, int msgtype,
                                 int msgflg)
{
    struct target_msgbuf *target_mb;
    char *target_mtext;
    struct msgbuf *host_mb;
    abi_long ret = 0;

    if (!lock_user_struct(VERIFY_WRITE, target_mb, msgp, 0))
        return -TARGET_EFAULT;
    host_mb = malloc(msgsz+sizeof(long));
    ret = get_errno(msgrcv(msqid, host_mb, msgsz, 1, msgflg));
    if (ret > 0) {
        abi_ulong target_mtext_addr = msgp + sizeof(abi_ulong);
        target_mtext = lock_user(VERIFY_WRITE, target_mtext_addr, ret, 0);
        if (!target_mtext) {
            ret = -TARGET_EFAULT;
            goto end;
        }
    	memcpy(target_mb->mtext, host_mb->mtext, ret);
        unlock_user(target_mtext, target_mtext_addr, ret);
    }
    target_mb->mtype = tswapl(host_mb->mtype);
    free(host_mb);

end:
    if (target_mb)
        unlock_user_struct(target_mb, msgp, 1);
    return ret;
}

/* ??? This only works with linear mappings.  */
/* do_ipc() must return target values and target errnos. */
static abi_long do_ipc(unsigned int call, int first,
                       int second, int third,
                       abi_long ptr, abi_long fifth)
{
    int version;
    abi_long ret = 0;
    struct shmid_ds shm_info;
    int i;

    version = call >> 16;
    call &= 0xffff;

    switch (call) {
    case IPCOP_semop:
        ret = get_errno(semop(first,(struct sembuf *)g2h(ptr), second));
        break;

    case IPCOP_semget:
        ret = get_errno(semget(first, second, third));
        break;

    case IPCOP_semctl:
        ret = do_semctl(first, second, third, ptr);
        break;

    case IPCOP_semtimedop:
        gemu_log("Unsupported ipc call: %d (version %d)\n", call, version);
        ret = -TARGET_ENOSYS;
        break;

	case IPCOP_msgget:
		ret = get_errno(msgget(first, second));
		break;

	case IPCOP_msgsnd:
		ret = do_msgsnd(first, ptr, second, third);
		break;

	case IPCOP_msgctl:
        	ret = do_msgctl(first, second, ptr);
		break;

	case IPCOP_msgrcv:
                {
                      /* XXX: this code is not correct */
                      struct ipc_kludge
                      {
                              void *__unbounded msgp;
                              long int msgtyp;
                      };

                      struct ipc_kludge *foo = (struct ipc_kludge *)g2h(ptr);
                      struct msgbuf *msgp = (struct msgbuf *) foo->msgp;

                      ret = do_msgrcv(first, (long)msgp, second, 0, third);

                }
		break;

    case IPCOP_shmat:
        {
            abi_ulong raddr;
            void *host_addr;
            /* SHM_* flags are the same on all linux platforms */
            host_addr = shmat(first, (void *)g2h(ptr), second);
            if (host_addr == (void *)-1) {
                ret = get_errno((long)host_addr);
                break;
            }
            raddr = h2g((unsigned long)host_addr);
            /* find out the length of the shared memory segment */
            
            ret = get_errno(shmctl(first, IPC_STAT, &shm_info));
            if (is_error(ret)) {
                /* can't get length, bail out */
                shmdt(host_addr);
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
            if (put_user_ual(raddr, third))
                return -TARGET_EFAULT;
            ret = 0;
        }
	break;
    case IPCOP_shmdt:
	for (i = 0; i < N_SHM_REGIONS; ++i) {
	    if (shm_regions[i].start == ptr) {
		shm_regions[i].start = 0;
		page_set_flags(ptr, shm_regions[i].size, 0);
		break;
	    }
	}
	ret = get_errno(shmdt((void *)g2h(ptr)));
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
	gemu_log("Unsupported ipc call: %d (version %d)\n", call, version);
	ret = -TARGET_ENOSYS;
	break;
    }
    return ret;
}
#endif

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
/* do_ioctl() Must return target values and target errnos. */
static abi_long do_ioctl(int fd, abi_long cmd, abi_long arg)
{
    const IOCTLEntry *ie;
    const argtype *arg_type;
    abi_long ret;
    uint8_t buf_temp[MAX_STRUCT_SIZE];
    int target_size;
    void *argptr;

    ie = ioctl_entries;
    for(;;) {
        if (ie->target_cmd == 0) {
            gemu_log("Unsupported ioctl: cmd=0x%04lx\n", (long)cmd);
            return -TARGET_ENOSYS;
        }
        if (ie->target_cmd == cmd)
            break;
        ie++;
    }
    arg_type = ie->arg_type;
#if defined(DEBUG)
    gemu_log("ioctl: cmd=0x%04lx (%s)\n", (long)cmd, ie->name);
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
                argptr = lock_user(VERIFY_WRITE, arg, target_size, 0);
                if (!argptr)
                    return -TARGET_EFAULT;
                thunk_convert(argptr, buf_temp, arg_type, THUNK_TARGET);
                unlock_user(argptr, arg, target_size);
            }
            break;
        case IOC_W:
            argptr = lock_user(VERIFY_READ, arg, target_size, 1);
            if (!argptr)
                return -TARGET_EFAULT;
            thunk_convert(buf_temp, argptr, arg_type, THUNK_HOST);
            unlock_user(argptr, arg, 0);
            ret = get_errno(ioctl(fd, ie->host_cmd, buf_temp));
            break;
        default:
        case IOC_RW:
            argptr = lock_user(VERIFY_READ, arg, target_size, 1);
            if (!argptr)
                return -TARGET_EFAULT;
            thunk_convert(buf_temp, argptr, arg_type, THUNK_HOST);
            unlock_user(argptr, arg, 0);
            ret = get_errno(ioctl(fd, ie->host_cmd, buf_temp));
            if (!is_error(ret)) {
                argptr = lock_user(VERIFY_WRITE, arg, target_size, 0);
                if (!argptr)
                    return -TARGET_EFAULT;
                thunk_convert(argptr, buf_temp, arg_type, THUNK_TARGET);
                unlock_user(argptr, arg, target_size);
            }
            break;
        }
        break;
    default:
        gemu_log("Unsupported ioctl type: cmd=0x%04lx type=%d\n",
                 (long)cmd, arg_type[0]);
        ret = -TARGET_ENOSYS;
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

static abi_long read_ldt(abi_ulong ptr, unsigned long bytecount)
{
    int size;
    void *p;

    if (!ldt_table)
        return 0;
    size = TARGET_LDT_ENTRIES * TARGET_LDT_ENTRY_SIZE;
    if (size > bytecount)
        size = bytecount;
    p = lock_user(VERIFY_WRITE, ptr, size, 0);
    if (!p)
        return -TARGET_EFAULT;
    /* ??? Should this by byteswapped?  */
    memcpy(p, ldt_table, size);
    unlock_user(p, ptr, size);
    return size;
}

/* XXX: add locking support */
static abi_long write_ldt(CPUX86State *env,
                          abi_ulong ptr, unsigned long bytecount, int oldmode)
{
    struct target_modify_ldt_ldt_s ldt_info;
    struct target_modify_ldt_ldt_s *target_ldt_info;
    int seg_32bit, contents, read_exec_only, limit_in_pages;
    int seg_not_present, useable, lm;
    uint32_t *lp, entry_1, entry_2;

    if (bytecount != sizeof(ldt_info))
        return -TARGET_EINVAL;
    if (!lock_user_struct(VERIFY_READ, target_ldt_info, ptr, 1))
        return -TARGET_EFAULT;
    ldt_info.entry_number = tswap32(target_ldt_info->entry_number);
    ldt_info.base_addr = tswapl(target_ldt_info->base_addr);
    ldt_info.limit = tswap32(target_ldt_info->limit);
    ldt_info.flags = tswap32(target_ldt_info->flags);
    unlock_user_struct(target_ldt_info, ptr, 0);

    if (ldt_info.entry_number >= TARGET_LDT_ENTRIES)
        return -TARGET_EINVAL;
    seg_32bit = ldt_info.flags & 1;
    contents = (ldt_info.flags >> 1) & 3;
    read_exec_only = (ldt_info.flags >> 3) & 1;
    limit_in_pages = (ldt_info.flags >> 4) & 1;
    seg_not_present = (ldt_info.flags >> 5) & 1;
    useable = (ldt_info.flags >> 6) & 1;
#ifdef TARGET_ABI32
    lm = 0;
#else
    lm = (ldt_info.flags >> 7) & 1;
#endif
    if (contents == 3) {
        if (oldmode)
            return -TARGET_EINVAL;
        if (seg_not_present == 0)
            return -TARGET_EINVAL;
    }
    /* allocate the LDT */
    if (!ldt_table) {
        ldt_table = malloc(TARGET_LDT_ENTRIES * TARGET_LDT_ENTRY_SIZE);
        if (!ldt_table)
            return -TARGET_ENOMEM;
        memset(ldt_table, 0, TARGET_LDT_ENTRIES * TARGET_LDT_ENTRY_SIZE);
        env->ldt.base = h2g((unsigned long)ldt_table);
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
        (lm << 21) |
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
static abi_long do_modify_ldt(CPUX86State *env, int func, abi_ulong ptr,
                              unsigned long bytecount)
{
    abi_long ret;

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
    default:
        ret = -TARGET_ENOSYS;
        break;
    }
    return ret;
}

#if defined(TARGET_I386) && defined(TARGET_ABI32)
static abi_long do_set_thread_area(CPUX86State *env, abi_ulong ptr)
{
    uint64_t *gdt_table = g2h(env->gdt.base);
    struct target_modify_ldt_ldt_s ldt_info;
    struct target_modify_ldt_ldt_s *target_ldt_info;
    int seg_32bit, contents, read_exec_only, limit_in_pages;
    int seg_not_present, useable, lm;
    uint32_t *lp, entry_1, entry_2;
    int i;

    lock_user_struct(VERIFY_WRITE, target_ldt_info, ptr, 1);
    if (!target_ldt_info)
        return -TARGET_EFAULT;
    ldt_info.entry_number = tswap32(target_ldt_info->entry_number);
    ldt_info.base_addr = tswapl(target_ldt_info->base_addr);
    ldt_info.limit = tswap32(target_ldt_info->limit);
    ldt_info.flags = tswap32(target_ldt_info->flags);
    if (ldt_info.entry_number == -1) {
        for (i=TARGET_GDT_ENTRY_TLS_MIN; i<=TARGET_GDT_ENTRY_TLS_MAX; i++) {
            if (gdt_table[i] == 0) {
                ldt_info.entry_number = i;
                target_ldt_info->entry_number = tswap32(i);
                break;
            }
        }
    }
    unlock_user_struct(target_ldt_info, ptr, 1);

    if (ldt_info.entry_number < TARGET_GDT_ENTRY_TLS_MIN || 
        ldt_info.entry_number > TARGET_GDT_ENTRY_TLS_MAX)
           return -TARGET_EINVAL;
    seg_32bit = ldt_info.flags & 1;
    contents = (ldt_info.flags >> 1) & 3;
    read_exec_only = (ldt_info.flags >> 3) & 1;
    limit_in_pages = (ldt_info.flags >> 4) & 1;
    seg_not_present = (ldt_info.flags >> 5) & 1;
    useable = (ldt_info.flags >> 6) & 1;
#ifdef TARGET_ABI32
    lm = 0;
#else
    lm = (ldt_info.flags >> 7) & 1;
#endif

    if (contents == 3) {
        if (seg_not_present == 0)
            return -TARGET_EINVAL;
    }

    /* NOTE: same code as Linux kernel */
    /* Allow LDTs to be cleared by the user. */
    if (ldt_info.base_addr == 0 && ldt_info.limit == 0) {
        if ((contents == 0             &&
             read_exec_only == 1       &&
             seg_32bit == 0            &&
             limit_in_pages == 0       &&
             seg_not_present == 1      &&
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
        (useable << 20) |
        (lm << 21) |
        0x7000;

    /* Install the new entry ...  */
install:
    lp = (uint32_t *)(gdt_table + ldt_info.entry_number);
    lp[0] = tswap32(entry_1);
    lp[1] = tswap32(entry_2);
    return 0;
}

static abi_long do_get_thread_area(CPUX86State *env, abi_ulong ptr)
{
    struct target_modify_ldt_ldt_s *target_ldt_info;
    uint64_t *gdt_table = g2h(env->gdt.base);
    uint32_t base_addr, limit, flags;
    int seg_32bit, contents, read_exec_only, limit_in_pages, idx;
    int seg_not_present, useable, lm;
    uint32_t *lp, entry_1, entry_2;

    lock_user_struct(VERIFY_WRITE, target_ldt_info, ptr, 1);
    if (!target_ldt_info)
        return -TARGET_EFAULT;
    idx = tswap32(target_ldt_info->entry_number);
    if (idx < TARGET_GDT_ENTRY_TLS_MIN ||
        idx > TARGET_GDT_ENTRY_TLS_MAX) {
        unlock_user_struct(target_ldt_info, ptr, 1);
        return -TARGET_EINVAL;
    }
    lp = (uint32_t *)(gdt_table + idx);
    entry_1 = tswap32(lp[0]);
    entry_2 = tswap32(lp[1]);
    
    read_exec_only = ((entry_2 >> 9) & 1) ^ 1;
    contents = (entry_2 >> 10) & 3;
    seg_not_present = ((entry_2 >> 15) & 1) ^ 1;
    seg_32bit = (entry_2 >> 22) & 1;
    limit_in_pages = (entry_2 >> 23) & 1;
    useable = (entry_2 >> 20) & 1;
#ifdef TARGET_ABI32
    lm = 0;
#else
    lm = (entry_2 >> 21) & 1;
#endif
    flags = (seg_32bit << 0) | (contents << 1) |
        (read_exec_only << 3) | (limit_in_pages << 4) |
        (seg_not_present << 5) | (useable << 6) | (lm << 7);
    limit = (entry_1 & 0xffff) | (entry_2  & 0xf0000);
    base_addr = (entry_1 >> 16) | 
        (entry_2 & 0xff000000) | 
        ((entry_2 & 0xff) << 16);
    target_ldt_info->base_addr = tswapl(base_addr);
    target_ldt_info->limit = tswap32(limit);
    target_ldt_info->flags = tswap32(flags);
    unlock_user_struct(target_ldt_info, ptr, 1);
    return 0;
}
#endif /* TARGET_I386 && TARGET_ABI32 */

#ifndef TARGET_ABI32
static abi_long do_arch_prctl(CPUX86State *env, int code, abi_ulong addr)
{
    abi_long ret;
    abi_ulong val;
    int idx;
    
    switch(code) {
    case TARGET_ARCH_SET_GS:
    case TARGET_ARCH_SET_FS:
        if (code == TARGET_ARCH_SET_GS)
            idx = R_GS;
        else
            idx = R_FS;
        cpu_x86_load_seg(env, idx, 0);
        env->segs[idx].base = addr;
        break;
    case TARGET_ARCH_GET_GS:
    case TARGET_ARCH_GET_FS:
        if (code == TARGET_ARCH_GET_GS)
            idx = R_GS;
        else
            idx = R_FS;
        val = env->segs[idx].base;
        if (put_user(val, addr, abi_ulong))
            return -TARGET_EFAULT;
        break;
    default:
        ret = -TARGET_EINVAL;
        break;
    }
    return 0;
}
#endif

#endif /* defined(TARGET_I386) */

#if defined(USE_NPTL)

#define NEW_STACK_SIZE PTHREAD_STACK_MIN

static pthread_mutex_t clone_lock = PTHREAD_MUTEX_INITIALIZER;
typedef struct {
    CPUState *env;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_t thread;
    uint32_t tid;
    abi_ulong child_tidptr;
    abi_ulong parent_tidptr;
    sigset_t sigmask;
} new_thread_info;

static void *clone_func(void *arg)
{
    new_thread_info *info = arg;
    CPUState *env;

    env = info->env;
    thread_env = env;
    info->tid = gettid();
    if (info->child_tidptr)
        put_user_u32(info->tid, info->child_tidptr);
    if (info->parent_tidptr)
        put_user_u32(info->tid, info->parent_tidptr);
    /* Enable signals.  */
    sigprocmask(SIG_SETMASK, &info->sigmask, NULL);
    /* Signal to the parent that we're ready.  */
    pthread_mutex_lock(&info->mutex);
    pthread_cond_broadcast(&info->cond);
    pthread_mutex_unlock(&info->mutex);
    /* Wait until the parent has finshed initializing the tls state.  */
    pthread_mutex_lock(&clone_lock);
    pthread_mutex_unlock(&clone_lock);
    cpu_loop(env);
    /* never exits */
    return NULL;
}
#else
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
#endif

/* do_fork() Must return host values and target errnos (unlike most
   do_*() functions). */
static int do_fork(CPUState *env, unsigned int flags, abi_ulong newsp,
                   abi_ulong parent_tidptr, target_ulong newtls,
                   abi_ulong child_tidptr)
{
    int ret;
    TaskState *ts;
    uint8_t *new_stack;
    CPUState *new_env;
#if defined(USE_NPTL)
    unsigned int nptl_flags;
    sigset_t sigmask;
#endif

    if (flags & CLONE_VM) {
#if defined(USE_NPTL)
        new_thread_info info;
        pthread_attr_t attr;
#endif
        ts = qemu_mallocz(sizeof(TaskState) + NEW_STACK_SIZE);
        init_task_state(ts);
        new_stack = ts->stack;
        /* we create a new CPU instance. */
        new_env = cpu_copy(env);
        /* Init regs that differ from the parent.  */
        cpu_clone_regs(new_env, newsp);
        new_env->opaque = ts;
#if defined(USE_NPTL)
        nptl_flags = flags;
        flags &= ~CLONE_NPTL_FLAGS2;

        /* TODO: Implement CLONE_CHILD_CLEARTID.  */
        if (nptl_flags & CLONE_SETTLS)
            cpu_set_tls (new_env, newtls);

        /* Grab a mutex so that thread setup appears atomic.  */
        pthread_mutex_lock(&clone_lock);

        memset(&info, 0, sizeof(info));
        pthread_mutex_init(&info.mutex, NULL);
        pthread_mutex_lock(&info.mutex);
        pthread_cond_init(&info.cond, NULL);
        info.env = new_env;
        if (nptl_flags & CLONE_CHILD_SETTID)
            info.child_tidptr = child_tidptr;
        if (nptl_flags & CLONE_PARENT_SETTID)
            info.parent_tidptr = parent_tidptr;

        ret = pthread_attr_init(&attr);
        ret = pthread_attr_setstack(&attr, new_stack, NEW_STACK_SIZE);
        /* It is not safe to deliver signals until the child has finished
           initializing, so temporarily block all signals.  */
        sigfillset(&sigmask);
        sigprocmask(SIG_BLOCK, &sigmask, &info.sigmask);

        ret = pthread_create(&info.thread, &attr, clone_func, &info);

        sigprocmask(SIG_SETMASK, &info.sigmask, NULL);
        pthread_attr_destroy(&attr);
        if (ret == 0) {
            /* Wait for the child to initialize.  */
            pthread_cond_wait(&info.cond, &info.mutex);
            ret = info.tid;
            if (flags & CLONE_PARENT_SETTID)
                put_user_u32(ret, parent_tidptr);
        } else {
            ret = -1;
        }
        pthread_mutex_unlock(&info.mutex);
        pthread_cond_destroy(&info.cond);
        pthread_mutex_destroy(&info.mutex);
        pthread_mutex_unlock(&clone_lock);
#else
        if (flags & CLONE_NPTL_FLAGS2)
            return -EINVAL;
        /* This is probably going to die very quickly, but do it anyway.  */
#ifdef __ia64__
        ret = __clone2(clone_func, new_stack + NEW_STACK_SIZE, flags, new_env);
#else
	ret = clone(clone_func, new_stack + NEW_STACK_SIZE, flags, new_env);
#endif
#endif
    } else {
        /* if no CLONE_VM, we consider it is a fork */
        if ((flags & ~(CSIGNAL | CLONE_NPTL_FLAGS2)) != 0)
            return -EINVAL;
        fork_start();
        ret = fork();
#if defined(USE_NPTL)
        /* There is a race condition here.  The parent process could
           theoretically read the TID in the child process before the child
           tid is set.  This would require using either ptrace
           (not implemented) or having *_tidptr to point at a shared memory
           mapping.  We can't repeat the spinlock hack used above because
           the child process gets its own copy of the lock.  */
        if (ret == 0) {
            cpu_clone_regs(env, newsp);
            fork_end(1);
            /* Child Process.  */
            if (flags & CLONE_CHILD_SETTID)
                put_user_u32(gettid(), child_tidptr);
            if (flags & CLONE_PARENT_SETTID)
                put_user_u32(gettid(), parent_tidptr);
            ts = (TaskState *)env->opaque;
            if (flags & CLONE_SETTLS)
                cpu_set_tls (env, newtls);
            /* TODO: Implement CLONE_CHILD_CLEARTID.  */
        } else {
            fork_end(0);
        }
#else
        if (ret == 0) {
            cpu_clone_regs(env, newsp);
        }
#endif
    }
    return ret;
}

static abi_long do_fcntl(int fd, int cmd, abi_ulong arg)
{
    struct flock fl;
    struct target_flock *target_fl;
    struct flock64 fl64;
    struct target_flock64 *target_fl64;
    abi_long ret;

    switch(cmd) {
    case TARGET_F_GETLK:
        if (!lock_user_struct(VERIFY_READ, target_fl, arg, 1))
            return -TARGET_EFAULT;
        fl.l_type = tswap16(target_fl->l_type);
        fl.l_whence = tswap16(target_fl->l_whence);
        fl.l_start = tswapl(target_fl->l_start);
        fl.l_len = tswapl(target_fl->l_len);
        fl.l_pid = tswapl(target_fl->l_pid);
        unlock_user_struct(target_fl, arg, 0);
        ret = get_errno(fcntl(fd, cmd, &fl));
        if (ret == 0) {
            if (!lock_user_struct(VERIFY_WRITE, target_fl, arg, 0))
                return -TARGET_EFAULT;
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
        if (!lock_user_struct(VERIFY_READ, target_fl, arg, 1))
            return -TARGET_EFAULT;
        fl.l_type = tswap16(target_fl->l_type);
        fl.l_whence = tswap16(target_fl->l_whence);
        fl.l_start = tswapl(target_fl->l_start);
        fl.l_len = tswapl(target_fl->l_len);
        fl.l_pid = tswapl(target_fl->l_pid);
        unlock_user_struct(target_fl, arg, 0);
        ret = get_errno(fcntl(fd, cmd, &fl));
        break;

    case TARGET_F_GETLK64:
        if (!lock_user_struct(VERIFY_READ, target_fl64, arg, 1))
            return -TARGET_EFAULT;
        fl64.l_type = tswap16(target_fl64->l_type) >> 1;
        fl64.l_whence = tswap16(target_fl64->l_whence);
        fl64.l_start = tswapl(target_fl64->l_start);
        fl64.l_len = tswapl(target_fl64->l_len);
        fl64.l_pid = tswap16(target_fl64->l_pid);
        unlock_user_struct(target_fl64, arg, 0);
        ret = get_errno(fcntl(fd, cmd >> 1, &fl64));
        if (ret == 0) {
            if (!lock_user_struct(VERIFY_WRITE, target_fl64, arg, 0))
                return -TARGET_EFAULT;
            target_fl64->l_type = tswap16(fl64.l_type) >> 1;
            target_fl64->l_whence = tswap16(fl64.l_whence);
            target_fl64->l_start = tswapl(fl64.l_start);
            target_fl64->l_len = tswapl(fl64.l_len);
            target_fl64->l_pid = tswapl(fl64.l_pid);
            unlock_user_struct(target_fl64, arg, 1);
        }
        break;
    case TARGET_F_SETLK64:
    case TARGET_F_SETLKW64:
        if (!lock_user_struct(VERIFY_READ, target_fl64, arg, 1))
            return -TARGET_EFAULT;
        fl64.l_type = tswap16(target_fl64->l_type) >> 1;
        fl64.l_whence = tswap16(target_fl64->l_whence);
        fl64.l_start = tswapl(target_fl64->l_start);
        fl64.l_len = tswapl(target_fl64->l_len);
        fl64.l_pid = tswap16(target_fl64->l_pid);
        unlock_user_struct(target_fl64, arg, 0);
        ret = get_errno(fcntl(fd, cmd >> 1, &fl64));
        break;

    case F_GETFL:
        ret = get_errno(fcntl(fd, cmd, arg));
        if (ret >= 0) {
            ret = host_to_target_bitmask(ret, fcntl_flags_tbl);
        }
        break;

    case F_SETFL:
        ret = get_errno(fcntl(fd, cmd, target_to_host_bitmask(arg, fcntl_flags_tbl)));
        break;

    default:
        ret = get_errno(fcntl(fd, cmd, arg));
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
    int i;

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

        /* Build target_to_host_errno_table[] table from
         * host_to_target_errno_table[]. */
        for (i=0; i < ERRNO_TABLE_SIZE; i++)
                target_to_host_errno_table[host_to_target_errno_table[i]] = i;

        /* automatic consistency check if same arch */
#if defined(__i386__) && defined(TARGET_I386) && defined(TARGET_ABI32)
        if (ie->target_cmd != ie->host_cmd) {
            fprintf(stderr, "ERROR: ioctl: target=0x%x host=0x%x\n",
                    ie->target_cmd, ie->host_cmd);
        }
#endif
        ie++;
    }
}

#if TARGET_ABI_BITS == 32
static inline uint64_t target_offset64(uint32_t word0, uint32_t word1)
{
#ifdef TARGET_WORDS_BIGENDIAN
    return ((uint64_t)word0 << 32) | word1;
#else
    return ((uint64_t)word1 << 32) | word0;
#endif
}
#else /* TARGET_ABI_BITS == 32 */
static inline uint64_t target_offset64(uint64_t word0, uint64_t word1)
{
    return word0;
}
#endif /* TARGET_ABI_BITS != 32 */

#ifdef TARGET_NR_truncate64
static inline abi_long target_truncate64(void *cpu_env, const char *arg1,
                                         abi_long arg2,
                                         abi_long arg3,
                                         abi_long arg4)
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
static inline abi_long target_ftruncate64(void *cpu_env, abi_long arg1,
                                          abi_long arg2,
                                          abi_long arg3,
                                          abi_long arg4)
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

static inline abi_long target_to_host_timespec(struct timespec *host_ts,
                                               abi_ulong target_addr)
{
    struct target_timespec *target_ts;

    if (!lock_user_struct(VERIFY_READ, target_ts, target_addr, 1))
        return -TARGET_EFAULT;
    host_ts->tv_sec = tswapl(target_ts->tv_sec);
    host_ts->tv_nsec = tswapl(target_ts->tv_nsec);
    unlock_user_struct(target_ts, target_addr, 0);
    return 0;
}

static inline abi_long host_to_target_timespec(abi_ulong target_addr,
                                               struct timespec *host_ts)
{
    struct target_timespec *target_ts;

    if (!lock_user_struct(VERIFY_WRITE, target_ts, target_addr, 0))
        return -TARGET_EFAULT;
    target_ts->tv_sec = tswapl(host_ts->tv_sec);
    target_ts->tv_nsec = tswapl(host_ts->tv_nsec);
    unlock_user_struct(target_ts, target_addr, 1);
    return 0;
}

#if defined(USE_NPTL)
/* ??? Using host futex calls even when target atomic operations
   are not really atomic probably breaks things.  However implementing
   futexes locally would make futexes shared between multiple processes
   tricky.  However they're probably useless because guest atomic
   operations won't work either.  */
static int do_futex(target_ulong uaddr, int op, int val, target_ulong timeout,
                    target_ulong uaddr2, int val3)
{
    struct timespec ts, *pts;

    /* ??? We assume FUTEX_* constants are the same on both host
       and target.  */
    switch (op) {
    case FUTEX_WAIT:
        if (timeout) {
            pts = &ts;
            target_to_host_timespec(pts, timeout);
        } else {
            pts = NULL;
        }
        return get_errno(sys_futex(g2h(uaddr), FUTEX_WAIT, tswap32(val),
                         pts, NULL, 0));
    case FUTEX_WAKE:
        return get_errno(sys_futex(g2h(uaddr), FUTEX_WAKE, val, NULL, NULL, 0));
    case FUTEX_FD:
        return get_errno(sys_futex(g2h(uaddr), FUTEX_FD, val, NULL, NULL, 0));
    case FUTEX_REQUEUE:
        return get_errno(sys_futex(g2h(uaddr), FUTEX_REQUEUE, val,
                         NULL, g2h(uaddr2), 0));
    case FUTEX_CMP_REQUEUE:
        return get_errno(sys_futex(g2h(uaddr), FUTEX_CMP_REQUEUE, val,
                         NULL, g2h(uaddr2), tswap32(val3)));
    default:
        return -TARGET_ENOSYS;
    }
}
#endif

int get_osversion(void)
{
    static int osversion;
    struct new_utsname buf;
    const char *s;
    int i, n, tmp;
    if (osversion)
        return osversion;
    if (qemu_uname_release && *qemu_uname_release) {
        s = qemu_uname_release;
    } else {
        if (sys_uname(&buf))
            return 0;
        s = buf.release;
    }
    tmp = 0;
    for (i = 0; i < 3; i++) {
        n = 0;
        while (*s >= '0' && *s <= '9') {
            n *= 10;
            n += *s - '0';
            s++;
        }
        tmp = (tmp << 8) + n;
        if (*s == '.')
            s++;
    }
    osversion = tmp;
    return osversion;
}

/* do_syscall() should always have a single exit point at the end so
   that actions, such as logging of syscall results, can be performed.
   All errnos that do_syscall() returns must be -TARGET_<errcode>. */
abi_long do_syscall(void *cpu_env, int num, abi_long arg1,
                    abi_long arg2, abi_long arg3, abi_long arg4,
                    abi_long arg5, abi_long arg6)
{
    abi_long ret;
    struct stat st;
    struct statfs stfs;
    void *p;

#ifdef DEBUG
    gemu_log("syscall %d", num);
#endif
    if(do_strace)
        print_syscall(num, arg1, arg2, arg3, arg4, arg5, arg6);

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
        if (!(p = lock_user(VERIFY_WRITE, arg2, arg3, 0)))
            goto efault;
        ret = get_errno(read(arg1, p, arg3));
        unlock_user(p, arg2, ret);
        break;
    case TARGET_NR_write:
        if (!(p = lock_user(VERIFY_READ, arg2, arg3, 1)))
            goto efault;
        ret = get_errno(write(arg1, p, arg3));
        unlock_user(p, arg2, 0);
        break;
    case TARGET_NR_open:
        if (!(p = lock_user_string(arg1)))
            goto efault;
        ret = get_errno(open(path(p),
                             target_to_host_bitmask(arg2, fcntl_flags_tbl),
                             arg3));
        unlock_user(p, arg1, 0);
        break;
#if defined(TARGET_NR_openat) && defined(__NR_openat)
    case TARGET_NR_openat:
        if (!(p = lock_user_string(arg2)))
            goto efault;
        ret = get_errno(sys_openat(arg1,
                                   path(p),
                                   target_to_host_bitmask(arg3, fcntl_flags_tbl),
                                   arg4));
        unlock_user(p, arg2, 0);
        break;
#endif
    case TARGET_NR_close:
        ret = get_errno(close(arg1));
        break;
    case TARGET_NR_brk:
        ret = do_brk(arg1);
        break;
    case TARGET_NR_fork:
        ret = get_errno(do_fork(cpu_env, SIGCHLD, 0, 0, 0, 0));
        break;
#ifdef TARGET_NR_waitpid
    case TARGET_NR_waitpid:
        {
            int status;
            ret = get_errno(waitpid(arg1, &status, arg3));
            if (!is_error(ret) && arg2
                && put_user_s32(status, arg2))
                goto efault;
        }
        break;
#endif
#ifdef TARGET_NR_waitid
    case TARGET_NR_waitid:
        {
            siginfo_t info;
            info.si_pid = 0;
            ret = get_errno(waitid(arg1, arg2, &info, arg4));
            if (!is_error(ret) && arg3 && info.si_pid != 0) {
                if (!(p = lock_user(VERIFY_WRITE, arg3, sizeof(target_siginfo_t), 0)))
                    goto efault;
                host_to_target_siginfo(p, &info);
                unlock_user(p, arg3, sizeof(target_siginfo_t));
            }
        }
        break;
#endif
#ifdef TARGET_NR_creat /* not on alpha */
    case TARGET_NR_creat:
        if (!(p = lock_user_string(arg1)))
            goto efault;
        ret = get_errno(creat(p, arg2));
        unlock_user(p, arg1, 0);
        break;
#endif
    case TARGET_NR_link:
        {
            void * p2;
            p = lock_user_string(arg1);
            p2 = lock_user_string(arg2);
            if (!p || !p2)
                ret = -TARGET_EFAULT;
            else
                ret = get_errno(link(p, p2));
            unlock_user(p2, arg2, 0);
            unlock_user(p, arg1, 0);
        }
        break;
#if defined(TARGET_NR_linkat) && defined(__NR_linkat)
    case TARGET_NR_linkat:
        {
            void * p2 = NULL;
            if (!arg2 || !arg4)
                goto efault;
            p  = lock_user_string(arg2);
            p2 = lock_user_string(arg4);
            if (!p || !p2)
                ret = -TARGET_EFAULT;
            else
                ret = get_errno(sys_linkat(arg1, p, arg3, p2, arg5));
            unlock_user(p, arg2, 0);
            unlock_user(p2, arg4, 0);
        }
        break;
#endif
    case TARGET_NR_unlink:
        if (!(p = lock_user_string(arg1)))
            goto efault;
        ret = get_errno(unlink(p));
        unlock_user(p, arg1, 0);
        break;
#if defined(TARGET_NR_unlinkat) && defined(__NR_unlinkat)
    case TARGET_NR_unlinkat:
        if (!(p = lock_user_string(arg2)))
            goto efault;
        ret = get_errno(sys_unlinkat(arg1, p, arg3));
        unlock_user(p, arg2, 0);
        break;
#endif
    case TARGET_NR_execve:
        {
            char **argp, **envp;
            int argc, envc;
            abi_ulong gp;
            abi_ulong guest_argp;
            abi_ulong guest_envp;
            abi_ulong addr;
            char **q;

            argc = 0;
            guest_argp = arg2;
            for (gp = guest_argp; gp; gp += sizeof(abi_ulong)) {
                if (get_user_ual(addr, gp))
                    goto efault;
                if (!addr)
                    break;
                argc++;
            }
            envc = 0;
            guest_envp = arg3;
            for (gp = guest_envp; gp; gp += sizeof(abi_ulong)) {
                if (get_user_ual(addr, gp))
                    goto efault;
                if (!addr)
                    break;
                envc++;
            }

            argp = alloca((argc + 1) * sizeof(void *));
            envp = alloca((envc + 1) * sizeof(void *));

            for (gp = guest_argp, q = argp; gp;
                  gp += sizeof(abi_ulong), q++) {
                if (get_user_ual(addr, gp))
                    goto execve_efault;
                if (!addr)
                    break;
                if (!(*q = lock_user_string(addr)))
                    goto execve_efault;
            }
            *q = NULL;

            for (gp = guest_envp, q = envp; gp;
                  gp += sizeof(abi_ulong), q++) {
                if (get_user_ual(addr, gp))
                    goto execve_efault;
                if (!addr)
                    break;
                if (!(*q = lock_user_string(addr)))
                    goto execve_efault;
            }
            *q = NULL;

            if (!(p = lock_user_string(arg1)))
                goto execve_efault;
            ret = get_errno(execve(p, argp, envp));
            unlock_user(p, arg1, 0);

            goto execve_end;

        execve_efault:
            ret = -TARGET_EFAULT;

        execve_end:
            for (gp = guest_argp, q = argp; *q;
                  gp += sizeof(abi_ulong), q++) {
                if (get_user_ual(addr, gp)
                    || !addr)
                    break;
                unlock_user(*q, addr, 0);
            }
            for (gp = guest_envp, q = envp; *q;
                  gp += sizeof(abi_ulong), q++) {
                if (get_user_ual(addr, gp)
                    || !addr)
                    break;
                unlock_user(*q, addr, 0);
            }
        }
        break;
    case TARGET_NR_chdir:
        if (!(p = lock_user_string(arg1)))
            goto efault;
        ret = get_errno(chdir(p));
        unlock_user(p, arg1, 0);
        break;
#ifdef TARGET_NR_time
    case TARGET_NR_time:
        {
            time_t host_time;
            ret = get_errno(time(&host_time));
            if (!is_error(ret)
                && arg1
                && put_user_sal(host_time, arg1))
                goto efault;
        }
        break;
#endif
    case TARGET_NR_mknod:
        if (!(p = lock_user_string(arg1)))
            goto efault;
        ret = get_errno(mknod(p, arg2, arg3));
        unlock_user(p, arg1, 0);
        break;
#if defined(TARGET_NR_mknodat) && defined(__NR_mknodat)
    case TARGET_NR_mknodat:
        if (!(p = lock_user_string(arg2)))
            goto efault;
        ret = get_errno(sys_mknodat(arg1, p, arg3, arg4));
        unlock_user(p, arg2, 0);
        break;
#endif
    case TARGET_NR_chmod:
        if (!(p = lock_user_string(arg1)))
            goto efault;
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
#ifdef TARGET_NR_getxpid
    case TARGET_NR_getxpid:
#else
    case TARGET_NR_getpid:
#endif
        ret = get_errno(getpid());
        break;
    case TARGET_NR_mount:
		{
			/* need to look at the data field */
			void *p2, *p3;
			p = lock_user_string(arg1);
			p2 = lock_user_string(arg2);
			p3 = lock_user_string(arg3);
                        if (!p || !p2 || !p3)
                            ret = -TARGET_EFAULT;
                        else
                            /* FIXME - arg5 should be locked, but it isn't clear how to
                             * do that since it's not guaranteed to be a NULL-terminated
                             * string.
                             */
                            ret = get_errno(mount(p, p2, p3, (unsigned long)arg4, g2h(arg5)));
                        unlock_user(p, arg1, 0);
                        unlock_user(p2, arg2, 0);
                        unlock_user(p3, arg3, 0);
			break;
		}
#ifdef TARGET_NR_umount
    case TARGET_NR_umount:
        if (!(p = lock_user_string(arg1)))
            goto efault;
        ret = get_errno(umount(p));
        unlock_user(p, arg1, 0);
        break;
#endif
#ifdef TARGET_NR_stime /* not on alpha */
    case TARGET_NR_stime:
        {
            time_t host_time;
            if (get_user_sal(host_time, arg1))
                goto efault;
            ret = get_errno(stime(&host_time));
        }
        break;
#endif
    case TARGET_NR_ptrace:
        goto unimplemented;
#ifdef TARGET_NR_alarm /* not on alpha */
    case TARGET_NR_alarm:
        ret = alarm(arg1);
        break;
#endif
#ifdef TARGET_NR_oldfstat
    case TARGET_NR_oldfstat:
        goto unimplemented;
#endif
#ifdef TARGET_NR_pause /* not on alpha */
    case TARGET_NR_pause:
        ret = get_errno(pause());
        break;
#endif
#ifdef TARGET_NR_utime
    case TARGET_NR_utime:
        {
            struct utimbuf tbuf, *host_tbuf;
            struct target_utimbuf *target_tbuf;
            if (arg2) {
                if (!lock_user_struct(VERIFY_READ, target_tbuf, arg2, 1))
                    goto efault;
                tbuf.actime = tswapl(target_tbuf->actime);
                tbuf.modtime = tswapl(target_tbuf->modtime);
                unlock_user_struct(target_tbuf, arg2, 0);
                host_tbuf = &tbuf;
            } else {
                host_tbuf = NULL;
            }
            if (!(p = lock_user_string(arg1)))
                goto efault;
            ret = get_errno(utime(p, host_tbuf));
            unlock_user(p, arg1, 0);
        }
        break;
#endif
    case TARGET_NR_utimes:
        {
            struct timeval *tvp, tv[2];
            if (arg2) {
                if (copy_from_user_timeval(&tv[0], arg2)
                    || copy_from_user_timeval(&tv[1],
                                              arg2 + sizeof(struct target_timeval)))
                    goto efault;
                tvp = tv;
            } else {
                tvp = NULL;
            }
            if (!(p = lock_user_string(arg1)))
                goto efault;
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
        if (!(p = lock_user_string(arg1)))
            goto efault;
        ret = get_errno(access(p, arg2));
        unlock_user(p, arg1, 0);
        break;
#if defined(TARGET_NR_faccessat) && defined(__NR_faccessat)
    case TARGET_NR_faccessat:
        if (!(p = lock_user_string(arg2)))
            goto efault;
        ret = get_errno(sys_faccessat(arg1, p, arg3, arg4));
        unlock_user(p, arg2, 0);
        break;
#endif
#ifdef TARGET_NR_nice /* not on alpha */
    case TARGET_NR_nice:
        ret = get_errno(nice(arg1));
        break;
#endif
#ifdef TARGET_NR_ftime
    case TARGET_NR_ftime:
        goto unimplemented;
#endif
    case TARGET_NR_sync:
        sync();
        ret = 0;
        break;
    case TARGET_NR_kill:
        ret = get_errno(kill(arg1, target_to_host_signal(arg2)));
        break;
    case TARGET_NR_rename:
        {
            void *p2;
            p = lock_user_string(arg1);
            p2 = lock_user_string(arg2);
            if (!p || !p2)
                ret = -TARGET_EFAULT;
            else
                ret = get_errno(rename(p, p2));
            unlock_user(p2, arg2, 0);
            unlock_user(p, arg1, 0);
        }
        break;
#if defined(TARGET_NR_renameat) && defined(__NR_renameat)
    case TARGET_NR_renameat:
        {
            void *p2;
            p  = lock_user_string(arg2);
            p2 = lock_user_string(arg4);
            if (!p || !p2)
                ret = -TARGET_EFAULT;
            else
                ret = get_errno(sys_renameat(arg1, p, arg3, p2));
            unlock_user(p2, arg4, 0);
            unlock_user(p, arg2, 0);
        }
        break;
#endif
    case TARGET_NR_mkdir:
        if (!(p = lock_user_string(arg1)))
            goto efault;
        ret = get_errno(mkdir(p, arg2));
        unlock_user(p, arg1, 0);
        break;
#if defined(TARGET_NR_mkdirat) && defined(__NR_mkdirat)
    case TARGET_NR_mkdirat:
        if (!(p = lock_user_string(arg2)))
            goto efault;
        ret = get_errno(sys_mkdirat(arg1, p, arg3));
        unlock_user(p, arg2, 0);
        break;
#endif
    case TARGET_NR_rmdir:
        if (!(p = lock_user_string(arg1)))
            goto efault;
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
#if defined(TARGET_MIPS)
                CPUMIPSState *env = (CPUMIPSState*)cpu_env;
		env->active_tc.gpr[3] = host_pipe[1];
		ret = host_pipe[0];
#elif defined(TARGET_SH4)
		((CPUSH4State*)cpu_env)->gregs[1] = host_pipe[1];
		ret = host_pipe[0];
#else
                if (put_user_s32(host_pipe[0], arg1)
                    || put_user_s32(host_pipe[1], arg1 + sizeof(host_pipe[0])))
                    goto efault;
#endif
            }
        }
        break;
    case TARGET_NR_times:
        {
            struct target_tms *tmsp;
            struct tms tms;
            ret = get_errno(times(&tms));
            if (arg1) {
                tmsp = lock_user(VERIFY_WRITE, arg1, sizeof(struct target_tms), 0);
                if (!tmsp)
                    goto efault;
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
#ifdef TARGET_NR_signal
    case TARGET_NR_signal:
        goto unimplemented;
#endif
    case TARGET_NR_acct:
        if (!(p = lock_user_string(arg1)))
            goto efault;
        ret = get_errno(acct(path(p)));
        unlock_user(p, arg1, 0);
        break;
#ifdef TARGET_NR_umount2 /* not on alpha */
    case TARGET_NR_umount2:
        if (!(p = lock_user_string(arg1)))
            goto efault;
        ret = get_errno(umount2(p, arg2));
        unlock_user(p, arg1, 0);
        break;
#endif
#ifdef TARGET_NR_lock
    case TARGET_NR_lock:
        goto unimplemented;
#endif
    case TARGET_NR_ioctl:
        ret = do_ioctl(arg1, arg2, arg3);
        break;
    case TARGET_NR_fcntl:
        ret = do_fcntl(arg1, arg2, arg3);
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
        if (!(p = lock_user_string(arg1)))
            goto efault;
        ret = get_errno(chroot(p));
        unlock_user(p, arg1, 0);
        break;
    case TARGET_NR_ustat:
        goto unimplemented;
    case TARGET_NR_dup2:
        ret = get_errno(dup2(arg1, arg2));
        break;
#ifdef TARGET_NR_getppid /* not on alpha */
    case TARGET_NR_getppid:
        ret = get_errno(getppid());
        break;
#endif
    case TARGET_NR_getpgrp:
        ret = get_errno(getpgrp());
        break;
    case TARGET_NR_setsid:
        ret = get_errno(setsid());
        break;
#ifdef TARGET_NR_sigaction
    case TARGET_NR_sigaction:
        {
#if !defined(TARGET_MIPS)
            struct target_old_sigaction *old_act;
            struct target_sigaction act, oact, *pact;
            if (arg2) {
                if (!lock_user_struct(VERIFY_READ, old_act, arg2, 1))
                    goto efault;
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
                if (!lock_user_struct(VERIFY_WRITE, old_act, arg3, 0))
                    goto efault;
                old_act->_sa_handler = oact._sa_handler;
                old_act->sa_mask = oact.sa_mask.sig[0];
                old_act->sa_flags = oact.sa_flags;
                old_act->sa_restorer = oact.sa_restorer;
                unlock_user_struct(old_act, arg3, 1);
            }
#else
	    struct target_sigaction act, oact, *pact, *old_act;

	    if (arg2) {
                if (!lock_user_struct(VERIFY_READ, old_act, arg2, 1))
                    goto efault;
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
                if (!lock_user_struct(VERIFY_WRITE, old_act, arg3, 0))
                    goto efault;
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
#endif
    case TARGET_NR_rt_sigaction:
        {
            struct target_sigaction *act;
            struct target_sigaction *oact;

            if (arg2) {
                if (!lock_user_struct(VERIFY_READ, act, arg2, 1))
                    goto efault;
            } else
                act = NULL;
            if (arg3) {
                if (!lock_user_struct(VERIFY_WRITE, oact, arg3, 0)) {
                    ret = -TARGET_EFAULT;
                    goto rt_sigaction_fail;
                }
            } else
                oact = NULL;
            ret = get_errno(do_sigaction(arg1, act, oact));
	rt_sigaction_fail:
            if (act)
                unlock_user_struct(act, arg2, 0);
            if (oact)
                unlock_user_struct(oact, arg3, 1);
        }
        break;
#ifdef TARGET_NR_sgetmask /* not on alpha */
    case TARGET_NR_sgetmask:
        {
            sigset_t cur_set;
            abi_ulong target_set;
            sigprocmask(0, NULL, &cur_set);
            host_to_target_old_sigset(&target_set, &cur_set);
            ret = target_set;
        }
        break;
#endif
#ifdef TARGET_NR_ssetmask /* not on alpha */
    case TARGET_NR_ssetmask:
        {
            sigset_t set, oset, cur_set;
            abi_ulong target_set = arg1;
            sigprocmask(0, NULL, &cur_set);
            target_to_host_old_sigset(&set, &target_set);
            sigorset(&set, &set, &cur_set);
            sigprocmask(SIG_SETMASK, &set, &oset);
            host_to_target_old_sigset(&target_set, &oset);
            ret = target_set;
        }
        break;
#endif
#ifdef TARGET_NR_sigprocmask
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
                    ret = -TARGET_EINVAL;
                    goto fail;
                }
                if (!(p = lock_user(VERIFY_READ, arg2, sizeof(target_sigset_t), 1)))
                    goto efault;
                target_to_host_old_sigset(&set, p);
                unlock_user(p, arg2, 0);
                set_ptr = &set;
            } else {
                how = 0;
                set_ptr = NULL;
            }
            ret = get_errno(sigprocmask(arg1, set_ptr, &oldset));
            if (!is_error(ret) && arg3) {
                if (!(p = lock_user(VERIFY_WRITE, arg3, sizeof(target_sigset_t), 0)))
                    goto efault;
                host_to_target_old_sigset(p, &oldset);
                unlock_user(p, arg3, sizeof(target_sigset_t));
            }
        }
        break;
#endif
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
                    ret = -TARGET_EINVAL;
                    goto fail;
                }
                if (!(p = lock_user(VERIFY_READ, arg2, sizeof(target_sigset_t), 1)))
                    goto efault;
                target_to_host_sigset(&set, p);
                unlock_user(p, arg2, 0);
                set_ptr = &set;
            } else {
                how = 0;
                set_ptr = NULL;
            }
            ret = get_errno(sigprocmask(how, set_ptr, &oldset));
            if (!is_error(ret) && arg3) {
                if (!(p = lock_user(VERIFY_WRITE, arg3, sizeof(target_sigset_t), 0)))
                    goto efault;
                host_to_target_sigset(p, &oldset);
                unlock_user(p, arg3, sizeof(target_sigset_t));
            }
        }
        break;
#ifdef TARGET_NR_sigpending
    case TARGET_NR_sigpending:
        {
            sigset_t set;
            ret = get_errno(sigpending(&set));
            if (!is_error(ret)) {
                if (!(p = lock_user(VERIFY_WRITE, arg1, sizeof(target_sigset_t), 0)))
                    goto efault;
                host_to_target_old_sigset(p, &set);
                unlock_user(p, arg1, sizeof(target_sigset_t));
            }
        }
        break;
#endif
    case TARGET_NR_rt_sigpending:
        {
            sigset_t set;
            ret = get_errno(sigpending(&set));
            if (!is_error(ret)) {
                if (!(p = lock_user(VERIFY_WRITE, arg1, sizeof(target_sigset_t), 0)))
                    goto efault;
                host_to_target_sigset(p, &set);
                unlock_user(p, arg1, sizeof(target_sigset_t));
            }
        }
        break;
#ifdef TARGET_NR_sigsuspend
    case TARGET_NR_sigsuspend:
        {
            sigset_t set;
            if (!(p = lock_user(VERIFY_READ, arg1, sizeof(target_sigset_t), 1)))
                goto efault;
            target_to_host_old_sigset(&set, p);
            unlock_user(p, arg1, 0);
            ret = get_errno(sigsuspend(&set));
        }
        break;
#endif
    case TARGET_NR_rt_sigsuspend:
        {
            sigset_t set;
            if (!(p = lock_user(VERIFY_READ, arg1, sizeof(target_sigset_t), 1)))
                goto efault;
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

            if (!(p = lock_user(VERIFY_READ, arg1, sizeof(target_sigset_t), 1)))
                goto efault;
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
                if (!(p = lock_user(VERIFY_WRITE, arg2, sizeof(target_siginfo_t), 0)))
                    goto efault;
                host_to_target_siginfo(p, &uinfo);
                unlock_user(p, arg2, sizeof(target_siginfo_t));
            }
        }
        break;
    case TARGET_NR_rt_sigqueueinfo:
        {
            siginfo_t uinfo;
            if (!(p = lock_user(VERIFY_READ, arg3, sizeof(target_sigset_t), 1)))
                goto efault;
            target_to_host_siginfo(&uinfo, p);
            unlock_user(p, arg1, 0);
            ret = get_errno(sys_rt_sigqueueinfo(arg1, arg2, &uinfo));
        }
        break;
#ifdef TARGET_NR_sigreturn
    case TARGET_NR_sigreturn:
        /* NOTE: ret is eax, so not transcoding must be done */
        ret = do_sigreturn(cpu_env);
        break;
#endif
    case TARGET_NR_rt_sigreturn:
        /* NOTE: ret is eax, so not transcoding must be done */
        ret = do_rt_sigreturn(cpu_env);
        break;
    case TARGET_NR_sethostname:
        if (!(p = lock_user_string(arg1)))
            goto efault;
        ret = get_errno(sethostname(p, arg2));
        unlock_user(p, arg1, 0);
        break;
    case TARGET_NR_setrlimit:
        {
            /* XXX: convert resource ? */
            int resource = arg1;
            struct target_rlimit *target_rlim;
            struct rlimit rlim;
            if (!lock_user_struct(VERIFY_READ, target_rlim, arg2, 1))
                goto efault;
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
                if (!lock_user_struct(VERIFY_WRITE, target_rlim, arg2, 0))
                    goto efault;
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
                if (copy_to_user_timeval(arg1, &tv))
                    goto efault;
            }
        }
        break;
    case TARGET_NR_settimeofday:
        {
            struct timeval tv;
            if (copy_from_user_timeval(&tv, arg1))
                goto efault;
            ret = get_errno(settimeofday(&tv, NULL));
        }
        break;
#ifdef TARGET_NR_select
    case TARGET_NR_select:
        {
            struct target_sel_arg_struct *sel;
            abi_ulong inp, outp, exp, tvp;
            long nsel;

            if (!lock_user_struct(VERIFY_READ, sel, arg1, 1))
                goto efault;
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
            if (!p || !p2)
                ret = -TARGET_EFAULT;
            else
                ret = get_errno(symlink(p, p2));
            unlock_user(p2, arg2, 0);
            unlock_user(p, arg1, 0);
        }
        break;
#if defined(TARGET_NR_symlinkat) && defined(__NR_symlinkat)
    case TARGET_NR_symlinkat:
        {
            void *p2;
            p  = lock_user_string(arg1);
            p2 = lock_user_string(arg3);
            if (!p || !p2)
                ret = -TARGET_EFAULT;
            else
                ret = get_errno(sys_symlinkat(p, arg2, p2));
            unlock_user(p2, arg3, 0);
            unlock_user(p, arg1, 0);
        }
        break;
#endif
#ifdef TARGET_NR_oldlstat
    case TARGET_NR_oldlstat:
        goto unimplemented;
#endif
    case TARGET_NR_readlink:
        {
            void *p2;
            p = lock_user_string(arg1);
            p2 = lock_user(VERIFY_WRITE, arg2, arg3, 0);
            if (!p || !p2)
                ret = -TARGET_EFAULT;
            else
                ret = get_errno(readlink(path(p), p2, arg3));
            unlock_user(p2, arg2, ret);
            unlock_user(p, arg1, 0);
        }
        break;
#if defined(TARGET_NR_readlinkat) && defined(__NR_readlinkat)
    case TARGET_NR_readlinkat:
        {
            void *p2;
            p  = lock_user_string(arg2);
            p2 = lock_user(VERIFY_WRITE, arg3, arg4, 0);
            if (!p || !p2)
        	ret = -TARGET_EFAULT;
            else
                ret = get_errno(sys_readlinkat(arg1, path(p), p2, arg4));
            unlock_user(p2, arg3, ret);
            unlock_user(p, arg2, 0);
        }
        break;
#endif
#ifdef TARGET_NR_uselib
    case TARGET_NR_uselib:
        goto unimplemented;
#endif
#ifdef TARGET_NR_swapon
    case TARGET_NR_swapon:
        if (!(p = lock_user_string(arg1)))
            goto efault;
        ret = get_errno(swapon(p, arg2));
        unlock_user(p, arg1, 0);
        break;
#endif
    case TARGET_NR_reboot:
        goto unimplemented;
#ifdef TARGET_NR_readdir
    case TARGET_NR_readdir:
        goto unimplemented;
#endif
#ifdef TARGET_NR_mmap
    case TARGET_NR_mmap:
#if (defined(TARGET_I386) && defined(TARGET_ABI32)) || defined(TARGET_ARM) || defined(TARGET_M68K) || defined(TARGET_CRIS)
        {
            abi_ulong *v;
            abi_ulong v1, v2, v3, v4, v5, v6;
            if (!(v = lock_user(VERIFY_READ, arg1, 6 * sizeof(abi_ulong), 1)))
                goto efault;
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
#endif
#ifdef TARGET_NR_mmap2
    case TARGET_NR_mmap2:
#ifndef MMAP_SHIFT
#define MMAP_SHIFT 12
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
#ifdef TARGET_NR_mremap
    case TARGET_NR_mremap:
        ret = get_errno(target_mremap(arg1, arg2, arg3, arg4, arg5));
        break;
#endif
        /* ??? msync/mlock/munlock are broken for softmmu.  */
#ifdef TARGET_NR_msync
    case TARGET_NR_msync:
        ret = get_errno(msync(g2h(arg1), arg2, arg3));
        break;
#endif
#ifdef TARGET_NR_mlock
    case TARGET_NR_mlock:
        ret = get_errno(mlock(g2h(arg1), arg2));
        break;
#endif
#ifdef TARGET_NR_munlock
    case TARGET_NR_munlock:
        ret = get_errno(munlock(g2h(arg1), arg2));
        break;
#endif
#ifdef TARGET_NR_mlockall
    case TARGET_NR_mlockall:
        ret = get_errno(mlockall(arg1));
        break;
#endif
#ifdef TARGET_NR_munlockall
    case TARGET_NR_munlockall:
        ret = get_errno(munlockall());
        break;
#endif
    case TARGET_NR_truncate:
        if (!(p = lock_user_string(arg1)))
            goto efault;
        ret = get_errno(truncate(p, arg2));
        unlock_user(p, arg1, 0);
        break;
    case TARGET_NR_ftruncate:
        ret = get_errno(ftruncate(arg1, arg2));
        break;
    case TARGET_NR_fchmod:
        ret = get_errno(fchmod(arg1, arg2));
        break;
#if defined(TARGET_NR_fchmodat) && defined(__NR_fchmodat)
    case TARGET_NR_fchmodat:
        if (!(p = lock_user_string(arg2)))
            goto efault;
        ret = get_errno(sys_fchmodat(arg1, p, arg3, arg4));
        unlock_user(p, arg2, 0);
        break;
#endif
    case TARGET_NR_getpriority:
        /* libc does special remapping of the return value of
         * sys_getpriority() so it's just easiest to call
         * sys_getpriority() directly rather than through libc. */
        ret = sys_getpriority(arg1, arg2);
        break;
    case TARGET_NR_setpriority:
        ret = get_errno(setpriority(arg1, arg2, arg3));
        break;
#ifdef TARGET_NR_profil
    case TARGET_NR_profil:
        goto unimplemented;
#endif
    case TARGET_NR_statfs:
        if (!(p = lock_user_string(arg1)))
            goto efault;
        ret = get_errno(statfs(path(p), &stfs));
        unlock_user(p, arg1, 0);
    convert_statfs:
        if (!is_error(ret)) {
            struct target_statfs *target_stfs;

            if (!lock_user_struct(VERIFY_WRITE, target_stfs, arg2, 0))
                goto efault;
            __put_user(stfs.f_type, &target_stfs->f_type);
            __put_user(stfs.f_bsize, &target_stfs->f_bsize);
            __put_user(stfs.f_blocks, &target_stfs->f_blocks);
            __put_user(stfs.f_bfree, &target_stfs->f_bfree);
            __put_user(stfs.f_bavail, &target_stfs->f_bavail);
            __put_user(stfs.f_files, &target_stfs->f_files);
            __put_user(stfs.f_ffree, &target_stfs->f_ffree);
            __put_user(stfs.f_fsid.__val[0], &target_stfs->f_fsid.val[0]);
            __put_user(stfs.f_fsid.__val[1], &target_stfs->f_fsid.val[1]);
            __put_user(stfs.f_namelen, &target_stfs->f_namelen);
            unlock_user_struct(target_stfs, arg2, 1);
        }
        break;
    case TARGET_NR_fstatfs:
        ret = get_errno(fstatfs(arg1, &stfs));
        goto convert_statfs;
#ifdef TARGET_NR_statfs64
    case TARGET_NR_statfs64:
        if (!(p = lock_user_string(arg1)))
            goto efault;
        ret = get_errno(statfs(path(p), &stfs));
        unlock_user(p, arg1, 0);
    convert_statfs64:
        if (!is_error(ret)) {
            struct target_statfs64 *target_stfs;

            if (!lock_user_struct(VERIFY_WRITE, target_stfs, arg3, 0))
                goto efault;
            __put_user(stfs.f_type, &target_stfs->f_type);
            __put_user(stfs.f_bsize, &target_stfs->f_bsize);
            __put_user(stfs.f_blocks, &target_stfs->f_blocks);
            __put_user(stfs.f_bfree, &target_stfs->f_bfree);
            __put_user(stfs.f_bavail, &target_stfs->f_bavail);
            __put_user(stfs.f_files, &target_stfs->f_files);
            __put_user(stfs.f_ffree, &target_stfs->f_ffree);
            __put_user(stfs.f_fsid.__val[0], &target_stfs->f_fsid.val[0]);
            __put_user(stfs.f_fsid.__val[1], &target_stfs->f_fsid.val[1]);
            __put_user(stfs.f_namelen, &target_stfs->f_namelen);
            unlock_user_struct(target_stfs, arg3, 1);
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
#ifdef TARGET_NR_socketcall
    case TARGET_NR_socketcall:
        ret = do_socketcall(arg1, arg2);
        break;
#endif
#ifdef TARGET_NR_accept
    case TARGET_NR_accept:
        ret = do_accept(arg1, arg2, arg3);
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
        ret = do_getpeername(arg1, arg2, arg3);
        break;
#endif
#ifdef TARGET_NR_getsockname
    case TARGET_NR_getsockname:
        ret = do_getsockname(arg1, arg2, arg3);
        break;
#endif
#ifdef TARGET_NR_getsockopt
    case TARGET_NR_getsockopt:
        ret = do_getsockopt(arg1, arg2, arg3, arg4, arg5);
        break;
#endif
#ifdef TARGET_NR_listen
    case TARGET_NR_listen:
        ret = get_errno(listen(arg1, arg2));
        break;
#endif
#ifdef TARGET_NR_recv
    case TARGET_NR_recv:
        ret = do_recvfrom(arg1, arg2, arg3, arg4, 0, 0);
        break;
#endif
#ifdef TARGET_NR_recvfrom
    case TARGET_NR_recvfrom:
        ret = do_recvfrom(arg1, arg2, arg3, arg4, arg5, arg6);
        break;
#endif
#ifdef TARGET_NR_recvmsg
    case TARGET_NR_recvmsg:
        ret = do_sendrecvmsg(arg1, arg2, arg3, 0);
        break;
#endif
#ifdef TARGET_NR_send
    case TARGET_NR_send:
        ret = do_sendto(arg1, arg2, arg3, arg4, 0, 0);
        break;
#endif
#ifdef TARGET_NR_sendmsg
    case TARGET_NR_sendmsg:
        ret = do_sendrecvmsg(arg1, arg2, arg3, 1);
        break;
#endif
#ifdef TARGET_NR_sendto
    case TARGET_NR_sendto:
        ret = do_sendto(arg1, arg2, arg3, arg4, arg5, arg6);
        break;
#endif
#ifdef TARGET_NR_shutdown
    case TARGET_NR_shutdown:
        ret = get_errno(shutdown(arg1, arg2));
        break;
#endif
#ifdef TARGET_NR_socket
    case TARGET_NR_socket:
        ret = do_socket(arg1, arg2, arg3);
        break;
#endif
#ifdef TARGET_NR_socketpair
    case TARGET_NR_socketpair:
        ret = do_socketpair(arg1, arg2, arg3, arg4);
        break;
#endif
#ifdef TARGET_NR_setsockopt
    case TARGET_NR_setsockopt:
        ret = do_setsockopt(arg1, arg2, arg3, arg4, (socklen_t) arg5);
        break;
#endif

    case TARGET_NR_syslog:
        if (!(p = lock_user_string(arg2)))
            goto efault;
        ret = get_errno(sys_syslog((int)arg1, p, (int)arg3));
        unlock_user(p, arg2, 0);
        break;

    case TARGET_NR_setitimer:
        {
            struct itimerval value, ovalue, *pvalue;

            if (arg2) {
                pvalue = &value;
                if (copy_from_user_timeval(&pvalue->it_interval, arg2)
                    || copy_from_user_timeval(&pvalue->it_value,
                                              arg2 + sizeof(struct target_timeval)))
                    goto efault;
            } else {
                pvalue = NULL;
            }
            ret = get_errno(setitimer(arg1, pvalue, &ovalue));
            if (!is_error(ret) && arg3) {
                if (copy_to_user_timeval(arg3,
                                         &ovalue.it_interval)
                    || copy_to_user_timeval(arg3 + sizeof(struct target_timeval),
                                            &ovalue.it_value))
                    goto efault;
            }
        }
        break;
    case TARGET_NR_getitimer:
        {
            struct itimerval value;

            ret = get_errno(getitimer(arg1, &value));
            if (!is_error(ret) && arg2) {
                if (copy_to_user_timeval(arg2,
                                         &value.it_interval)
                    || copy_to_user_timeval(arg2 + sizeof(struct target_timeval),
                                            &value.it_value))
                    goto efault;
            }
        }
        break;
    case TARGET_NR_stat:
        if (!(p = lock_user_string(arg1)))
            goto efault;
        ret = get_errno(stat(path(p), &st));
        unlock_user(p, arg1, 0);
        goto do_stat;
    case TARGET_NR_lstat:
        if (!(p = lock_user_string(arg1)))
            goto efault;
        ret = get_errno(lstat(path(p), &st));
        unlock_user(p, arg1, 0);
        goto do_stat;
    case TARGET_NR_fstat:
        {
            ret = get_errno(fstat(arg1, &st));
        do_stat:
            if (!is_error(ret)) {
                struct target_stat *target_st;

                if (!lock_user_struct(VERIFY_WRITE, target_st, arg2, 0))
                    goto efault;
                __put_user(st.st_dev, &target_st->st_dev);
                __put_user(st.st_ino, &target_st->st_ino);
                __put_user(st.st_mode, &target_st->st_mode);
                __put_user(st.st_uid, &target_st->st_uid);
                __put_user(st.st_gid, &target_st->st_gid);
                __put_user(st.st_nlink, &target_st->st_nlink);
                __put_user(st.st_rdev, &target_st->st_rdev);
                __put_user(st.st_size, &target_st->st_size);
                __put_user(st.st_blksize, &target_st->st_blksize);
                __put_user(st.st_blocks, &target_st->st_blocks);
                __put_user(st.st_atime, &target_st->target_st_atime);
                __put_user(st.st_mtime, &target_st->target_st_mtime);
                __put_user(st.st_ctime, &target_st->target_st_ctime);
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
            abi_long status_ptr = arg2;
            struct rusage rusage, *rusage_ptr;
            abi_ulong target_rusage = arg4;
            if (target_rusage)
                rusage_ptr = &rusage;
            else
                rusage_ptr = NULL;
            ret = get_errno(wait4(arg1, &status, arg3, rusage_ptr));
            if (!is_error(ret)) {
                if (status_ptr) {
                    if (put_user_s32(status, status_ptr))
                        goto efault;
                }
                if (target_rusage)
                    host_to_target_rusage(target_rusage, &rusage);
            }
        }
        break;
#ifdef TARGET_NR_swapoff
    case TARGET_NR_swapoff:
        if (!(p = lock_user_string(arg1)))
            goto efault;
        ret = get_errno(swapoff(p));
        unlock_user(p, arg1, 0);
        break;
#endif
    case TARGET_NR_sysinfo:
        {
            struct target_sysinfo *target_value;
            struct sysinfo value;
            ret = get_errno(sysinfo(&value));
            if (!is_error(ret) && arg1)
            {
                if (!lock_user_struct(VERIFY_WRITE, target_value, arg1, 0))
                    goto efault;
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
#ifdef TARGET_NR_ipc
    case TARGET_NR_ipc:
	ret = do_ipc(arg1, arg2, arg3, arg4, arg5, arg6);
	break;
#endif
    case TARGET_NR_fsync:
        ret = get_errno(fsync(arg1));
        break;
    case TARGET_NR_clone:
#if defined(TARGET_SH4)
        ret = get_errno(do_fork(cpu_env, arg1, arg2, arg3, arg5, arg4));
#else
        ret = get_errno(do_fork(cpu_env, arg1, arg2, arg3, arg4, arg5));
#endif
        break;
#ifdef __NR_exit_group
        /* new thread calls */
    case TARGET_NR_exit_group:
        gdb_exit(cpu_env, arg1);
        ret = get_errno(exit_group(arg1));
        break;
#endif
    case TARGET_NR_setdomainname:
        if (!(p = lock_user_string(arg1)))
            goto efault;
        ret = get_errno(setdomainname(p, arg2));
        unlock_user(p, arg1, 0);
        break;
    case TARGET_NR_uname:
        /* no need to transcode because we use the linux syscall */
        {
            struct new_utsname * buf;

            if (!lock_user_struct(VERIFY_WRITE, buf, arg1, 0))
                goto efault;
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
        ret = do_modify_ldt(cpu_env, arg1, arg2, arg3);
        break;
#if !defined(TARGET_X86_64)
    case TARGET_NR_vm86old:
        goto unimplemented;
    case TARGET_NR_vm86:
        ret = do_vm86(cpu_env, arg1, arg2);
        break;
#endif
#endif
    case TARGET_NR_adjtimex:
        goto unimplemented;
#ifdef TARGET_NR_create_module
    case TARGET_NR_create_module:
#endif
    case TARGET_NR_init_module:
    case TARGET_NR_delete_module:
#ifdef TARGET_NR_get_kernel_syms
    case TARGET_NR_get_kernel_syms:
#endif
        goto unimplemented;
    case TARGET_NR_quotactl:
        goto unimplemented;
    case TARGET_NR_getpgid:
        ret = get_errno(getpgid(arg1));
        break;
    case TARGET_NR_fchdir:
        ret = get_errno(fchdir(arg1));
        break;
#ifdef TARGET_NR_bdflush /* not on x86_64 */
    case TARGET_NR_bdflush:
        goto unimplemented;
#endif
#ifdef TARGET_NR_sysfs
    case TARGET_NR_sysfs:
        goto unimplemented;
#endif
    case TARGET_NR_personality:
        ret = get_errno(personality(arg1));
        break;
#ifdef TARGET_NR_afs_syscall
    case TARGET_NR_afs_syscall:
        goto unimplemented;
#endif
#ifdef TARGET_NR__llseek /* Not on alpha */
    case TARGET_NR__llseek:
        {
#if defined (__x86_64__)
            ret = get_errno(lseek(arg1, ((uint64_t )arg2 << 32) | arg3, arg5));
            if (put_user_s64(ret, arg4))
                goto efault;
#else
            int64_t res;
            ret = get_errno(_llseek(arg1, arg2, arg3, &res, arg5));
            if (put_user_s64(res, arg4))
                goto efault;
#endif
        }
        break;
#endif
    case TARGET_NR_getdents:
#if TARGET_ABI_BITS != 32
        goto unimplemented;
#elif TARGET_ABI_BITS == 32 && HOST_LONG_BITS == 64
        {
            struct target_dirent *target_dirp;
            struct dirent *dirp;
            abi_long count = arg3;

	    dirp = malloc(count);
	    if (!dirp) {
                ret = -TARGET_ENOMEM;
                goto fail;
            }

            ret = get_errno(sys_getdents(arg1, dirp, count));
            if (!is_error(ret)) {
                struct dirent *de;
		struct target_dirent *tde;
                int len = ret;
                int reclen, treclen;
		int count1, tnamelen;

		count1 = 0;
                de = dirp;
                if (!(target_dirp = lock_user(VERIFY_WRITE, arg2, count, 0)))
                    goto efault;
		tde = target_dirp;
                while (len > 0) {
                    reclen = de->d_reclen;
		    treclen = reclen - (2 * (sizeof(long) - sizeof(abi_long)));
                    tde->d_reclen = tswap16(treclen);
                    tde->d_ino = tswapl(de->d_ino);
                    tde->d_off = tswapl(de->d_off);
		    tnamelen = treclen - (2 * sizeof(abi_long) + 2);
		    if (tnamelen > 256)
                        tnamelen = 256;
                    /* XXX: may not be correct */
		    strncpy(tde->d_name, de->d_name, tnamelen);
                    de = (struct dirent *)((char *)de + reclen);
                    len -= reclen;
                    tde = (struct target_dirent *)((char *)tde + treclen);
		    count1 += treclen;
                }
		ret = count1;
                unlock_user(target_dirp, arg2, ret);
            }
	    free(dirp);
        }
#else
        {
            struct dirent *dirp;
            abi_long count = arg3;

            if (!(dirp = lock_user(VERIFY_WRITE, arg2, count, 0)))
                goto efault;
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
#if defined(TARGET_NR_getdents64) && defined(__NR_getdents64)
    case TARGET_NR_getdents64:
        {
            struct dirent64 *dirp;
            abi_long count = arg3;
            if (!(dirp = lock_user(VERIFY_WRITE, arg2, count, 0)))
                goto efault;
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
                    tswap64s((uint64_t *)&de->d_ino);
                    tswap64s((uint64_t *)&de->d_off);
                    de = (struct dirent64 *)((char *)de + reclen);
                    len -= reclen;
                }
            }
            unlock_user(dirp, arg2, ret);
        }
        break;
#endif /* TARGET_NR_getdents64 */
#ifdef TARGET_NR__newselect
    case TARGET_NR__newselect:
        ret = do_select(arg1, arg2, arg3, arg4, arg5);
        break;
#endif
#ifdef TARGET_NR_poll
    case TARGET_NR_poll:
        {
            struct target_pollfd *target_pfd;
            unsigned int nfds = arg2;
            int timeout = arg3;
            struct pollfd *pfd;
            unsigned int i;

            target_pfd = lock_user(VERIFY_WRITE, arg1, sizeof(struct target_pollfd) * nfds, 1);
            if (!target_pfd)
                goto efault;
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
#endif
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
            if (lock_iovec(VERIFY_WRITE, vec, arg2, count, 0) < 0)
                goto efault;
            ret = get_errno(readv(arg1, vec, count));
            unlock_iovec(vec, arg2, count, 1);
        }
        break;
    case TARGET_NR_writev:
        {
            int count = arg3;
            struct iovec *vec;

            vec = alloca(count * sizeof(struct iovec));
            if (lock_iovec(VERIFY_READ, vec, arg2, count, 1) < 0)
                goto efault;
            ret = get_errno(writev(arg1, vec, count));
            unlock_iovec(vec, arg2, count, 0);
        }
        break;
    case TARGET_NR_getsid:
        ret = get_errno(getsid(arg1));
        break;
#if defined(TARGET_NR_fdatasync) /* Not on alpha (osf_datasync ?) */
    case TARGET_NR_fdatasync:
        ret = get_errno(fdatasync(arg1));
        break;
#endif
    case TARGET_NR__sysctl:
        /* We don't implement this, but ENOTDIR is always a safe
           return value. */
        ret = -TARGET_ENOTDIR;
        break;
    case TARGET_NR_sched_setparam:
        {
            struct sched_param *target_schp;
            struct sched_param schp;

            if (!lock_user_struct(VERIFY_READ, target_schp, arg2, 1))
                goto efault;
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
                if (!lock_user_struct(VERIFY_WRITE, target_schp, arg2, 0))
                    goto efault;
                target_schp->sched_priority = tswap32(schp.sched_priority);
                unlock_user_struct(target_schp, arg2, 1);
            }
        }
        break;
    case TARGET_NR_sched_setscheduler:
        {
            struct sched_param *target_schp;
            struct sched_param schp;
            if (!lock_user_struct(VERIFY_READ, target_schp, arg3, 1))
                goto efault;
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
#ifdef TARGET_NR_query_module
    case TARGET_NR_query_module:
        goto unimplemented;
#endif
#ifdef TARGET_NR_nfsservctl
    case TARGET_NR_nfsservctl:
        goto unimplemented;
#endif
    case TARGET_NR_prctl:
        switch (arg1)
            {
            case PR_GET_PDEATHSIG:
                {
                    int deathsig;
                    ret = get_errno(prctl(arg1, &deathsig, arg3, arg4, arg5));
                    if (!is_error(ret) && arg2
                        && put_user_ual(deathsig, arg2))
                        goto efault;
                }
                break;
            default:
                ret = get_errno(prctl(arg1, arg2, arg3, arg4, arg5));
                break;
            }
        break;
#ifdef TARGET_NR_arch_prctl
    case TARGET_NR_arch_prctl:
#if defined(TARGET_I386) && !defined(TARGET_ABI32)
        ret = do_arch_prctl(cpu_env, arg1, arg2);
        break;
#else
        goto unimplemented;
#endif
#endif
#ifdef TARGET_NR_pread
    case TARGET_NR_pread:
        if (!(p = lock_user(VERIFY_WRITE, arg2, arg3, 0)))
            goto efault;
        ret = get_errno(pread(arg1, p, arg3, arg4));
        unlock_user(p, arg2, ret);
        break;
    case TARGET_NR_pwrite:
        if (!(p = lock_user(VERIFY_READ, arg2, arg3, 1)))
            goto efault;
        ret = get_errno(pwrite(arg1, p, arg3, arg4));
        unlock_user(p, arg2, 0);
        break;
#endif
#ifdef TARGET_NR_pread64
    case TARGET_NR_pread64:
        if (!(p = lock_user(VERIFY_WRITE, arg2, arg3, 0)))
            goto efault;
        ret = get_errno(pread64(arg1, p, arg3, target_offset64(arg4, arg5)));
        unlock_user(p, arg2, ret);
        break;
    case TARGET_NR_pwrite64:
        if (!(p = lock_user(VERIFY_READ, arg2, arg3, 1)))
            goto efault;
        ret = get_errno(pwrite64(arg1, p, arg3, target_offset64(arg4, arg5)));
        unlock_user(p, arg2, 0);
        break;
#endif
    case TARGET_NR_getcwd:
        if (!(p = lock_user(VERIFY_WRITE, arg1, arg2, 0)))
            goto efault;
        ret = get_errno(sys_getcwd1(p, arg2));
        unlock_user(p, arg1, ret);
        break;
    case TARGET_NR_capget:
        goto unimplemented;
    case TARGET_NR_capset:
        goto unimplemented;
    case TARGET_NR_sigaltstack:
#if defined(TARGET_I386) || defined(TARGET_ARM) || defined(TARGET_MIPS) || \
    defined(TARGET_SPARC) || defined(TARGET_PPC) || defined(TARGET_ALPHA)
        ret = do_sigaltstack(arg1, arg2, get_sp_from_cpustate((CPUState *)cpu_env));
        break;
#else
        goto unimplemented;
#endif
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
        ret = get_errno(do_fork(cpu_env, CLONE_VFORK | CLONE_VM | SIGCHLD,
                        0, 0, 0, 0));
        break;
#endif
#ifdef TARGET_NR_ugetrlimit
    case TARGET_NR_ugetrlimit:
    {
	struct rlimit rlim;
	ret = get_errno(getrlimit(arg1, &rlim));
	if (!is_error(ret)) {
	    struct target_rlimit *target_rlim;
            if (!lock_user_struct(VERIFY_WRITE, target_rlim, arg2, 0))
                goto efault;
	    target_rlim->rlim_cur = tswapl(rlim.rlim_cur);
	    target_rlim->rlim_max = tswapl(rlim.rlim_max);
            unlock_user_struct(target_rlim, arg2, 1);
	}
	break;
    }
#endif
#ifdef TARGET_NR_truncate64
    case TARGET_NR_truncate64:
        if (!(p = lock_user_string(arg1)))
            goto efault;
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
        if (!(p = lock_user_string(arg1)))
            goto efault;
        ret = get_errno(stat(path(p), &st));
        unlock_user(p, arg1, 0);
        goto do_stat64;
#endif
#ifdef TARGET_NR_lstat64
    case TARGET_NR_lstat64:
        if (!(p = lock_user_string(arg1)))
            goto efault;
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

                    if (!lock_user_struct(VERIFY_WRITE, target_st, arg2, 0))
                        goto efault;
                    memset(target_st, 0, sizeof(struct target_eabi_stat64));
                    __put_user(st.st_dev, &target_st->st_dev);
                    __put_user(st.st_ino, &target_st->st_ino);
#ifdef TARGET_STAT64_HAS_BROKEN_ST_INO
                    __put_user(st.st_ino, &target_st->__st_ino);
#endif
                    __put_user(st.st_mode, &target_st->st_mode);
                    __put_user(st.st_nlink, &target_st->st_nlink);
                    __put_user(st.st_uid, &target_st->st_uid);
                    __put_user(st.st_gid, &target_st->st_gid);
                    __put_user(st.st_rdev, &target_st->st_rdev);
                    __put_user(st.st_size, &target_st->st_size);
                    __put_user(st.st_blksize, &target_st->st_blksize);
                    __put_user(st.st_blocks, &target_st->st_blocks);
                    __put_user(st.st_atime, &target_st->target_st_atime);
                    __put_user(st.st_mtime, &target_st->target_st_mtime);
                    __put_user(st.st_ctime, &target_st->target_st_ctime);
                    unlock_user_struct(target_st, arg2, 1);
                } else
#endif
                {
                    struct target_stat64 *target_st;

                    if (!lock_user_struct(VERIFY_WRITE, target_st, arg2, 0))
                        goto efault;
                    memset(target_st, 0, sizeof(struct target_stat64));
                    __put_user(st.st_dev, &target_st->st_dev);
                    __put_user(st.st_ino, &target_st->st_ino);
#ifdef TARGET_STAT64_HAS_BROKEN_ST_INO
                    __put_user(st.st_ino, &target_st->__st_ino);
#endif
                    __put_user(st.st_mode, &target_st->st_mode);
                    __put_user(st.st_nlink, &target_st->st_nlink);
                    __put_user(st.st_uid, &target_st->st_uid);
                    __put_user(st.st_gid, &target_st->st_gid);
                    __put_user(st.st_rdev, &target_st->st_rdev);
                    /* XXX: better use of kernel struct */
                    __put_user(st.st_size, &target_st->st_size);
                    __put_user(st.st_blksize, &target_st->st_blksize);
                    __put_user(st.st_blocks, &target_st->st_blocks);
                    __put_user(st.st_atime, &target_st->target_st_atime);
                    __put_user(st.st_mtime, &target_st->target_st_mtime);
                    __put_user(st.st_ctime, &target_st->target_st_ctime);
                    unlock_user_struct(target_st, arg2, 1);
                }
            }
        }
        break;
#endif
#ifdef USE_UID16
    case TARGET_NR_lchown:
        if (!(p = lock_user_string(arg1)))
            goto efault;
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
            if (gidsetsize == 0)
                break;
            if (!is_error(ret)) {
                target_grouplist = lock_user(VERIFY_WRITE, arg2, gidsetsize * 2, 0);
                if (!target_grouplist)
                    goto efault;
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
            target_grouplist = lock_user(VERIFY_READ, arg2, gidsetsize * 2, 1);
            if (!target_grouplist) {
                ret = -TARGET_EFAULT;
                goto fail;
            }
            for(i = 0;i < gidsetsize; i++)
                grouplist[i] = tswap16(target_grouplist[i]);
            unlock_user(target_grouplist, arg2, 0);
            ret = get_errno(setgroups(gidsetsize, grouplist));
        }
        break;
    case TARGET_NR_fchown:
        ret = get_errno(fchown(arg1, low2highuid(arg2), low2highgid(arg3)));
        break;
#if defined(TARGET_NR_fchownat) && defined(__NR_fchownat)
    case TARGET_NR_fchownat:
        if (!(p = lock_user_string(arg2))) 
            goto efault;
        ret = get_errno(sys_fchownat(arg1, p, low2highuid(arg3), low2highgid(arg4), arg5));
        unlock_user(p, arg2, 0);
        break;
#endif
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
                if (put_user_u16(high2lowuid(ruid), arg1)
                    || put_user_u16(high2lowuid(euid), arg2)
                    || put_user_u16(high2lowuid(suid), arg3))
                    goto efault;
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
                if (put_user_u16(high2lowgid(rgid), arg1)
                    || put_user_u16(high2lowgid(egid), arg2)
                    || put_user_u16(high2lowgid(sgid), arg3))
                    goto efault;
            }
        }
        break;
#endif
    case TARGET_NR_chown:
        if (!(p = lock_user_string(arg1)))
            goto efault;
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
        if (!(p = lock_user_string(arg1)))
            goto efault;
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
            if (gidsetsize == 0)
                break;
            if (!is_error(ret)) {
                target_grouplist = lock_user(VERIFY_WRITE, arg2, gidsetsize * 4, 0);
                if (!target_grouplist) {
                    ret = -TARGET_EFAULT;
                    goto fail;
                }
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
            target_grouplist = lock_user(VERIFY_READ, arg2, gidsetsize * 4, 1);
            if (!target_grouplist) {
                ret = -TARGET_EFAULT;
                goto fail;
            }
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
                if (put_user_u32(ruid, arg1)
                    || put_user_u32(euid, arg2)
                    || put_user_u32(suid, arg3))
                    goto efault;
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
                if (put_user_u32(rgid, arg1)
                    || put_user_u32(egid, arg2)
                    || put_user_u32(sgid, arg3))
                    goto efault;
            }
        }
        break;
#endif
#ifdef TARGET_NR_chown32
    case TARGET_NR_chown32:
        if (!(p = lock_user_string(arg1)))
            goto efault;
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
#if TARGET_ABI_BITS == 32
    case TARGET_NR_fcntl64:
    {
	int cmd;
	struct flock64 fl;
	struct target_flock64 *target_fl;
#ifdef TARGET_ARM
	struct target_eabi_flock64 *target_efl;
#endif

        switch(arg2){
        case TARGET_F_GETLK64:
            cmd = F_GETLK64;
            break;
        case TARGET_F_SETLK64:
            cmd = F_SETLK64;
            break;
        case TARGET_F_SETLKW64:
            cmd = F_SETLK64;
            break;
        default:
            cmd = arg2;
            break;
        }

        switch(arg2) {
        case TARGET_F_GETLK64:
#ifdef TARGET_ARM
            if (((CPUARMState *)cpu_env)->eabi) {
                if (!lock_user_struct(VERIFY_READ, target_efl, arg3, 1)) 
                    goto efault;
                fl.l_type = tswap16(target_efl->l_type);
                fl.l_whence = tswap16(target_efl->l_whence);
                fl.l_start = tswap64(target_efl->l_start);
                fl.l_len = tswap64(target_efl->l_len);
                fl.l_pid = tswapl(target_efl->l_pid);
                unlock_user_struct(target_efl, arg3, 0);
            } else
#endif
            {
                if (!lock_user_struct(VERIFY_READ, target_fl, arg3, 1)) 
                    goto efault;
                fl.l_type = tswap16(target_fl->l_type);
                fl.l_whence = tswap16(target_fl->l_whence);
                fl.l_start = tswap64(target_fl->l_start);
                fl.l_len = tswap64(target_fl->l_len);
                fl.l_pid = tswapl(target_fl->l_pid);
                unlock_user_struct(target_fl, arg3, 0);
            }
            ret = get_errno(fcntl(arg1, cmd, &fl));
	    if (ret == 0) {
#ifdef TARGET_ARM
                if (((CPUARMState *)cpu_env)->eabi) {
                    if (!lock_user_struct(VERIFY_WRITE, target_efl, arg3, 0)) 
                        goto efault;
                    target_efl->l_type = tswap16(fl.l_type);
                    target_efl->l_whence = tswap16(fl.l_whence);
                    target_efl->l_start = tswap64(fl.l_start);
                    target_efl->l_len = tswap64(fl.l_len);
                    target_efl->l_pid = tswapl(fl.l_pid);
                    unlock_user_struct(target_efl, arg3, 1);
                } else
#endif
                {
                    if (!lock_user_struct(VERIFY_WRITE, target_fl, arg3, 0)) 
                        goto efault;
                    target_fl->l_type = tswap16(fl.l_type);
                    target_fl->l_whence = tswap16(fl.l_whence);
                    target_fl->l_start = tswap64(fl.l_start);
                    target_fl->l_len = tswap64(fl.l_len);
                    target_fl->l_pid = tswapl(fl.l_pid);
                    unlock_user_struct(target_fl, arg3, 1);
                }
	    }
	    break;

        case TARGET_F_SETLK64:
        case TARGET_F_SETLKW64:
#ifdef TARGET_ARM
            if (((CPUARMState *)cpu_env)->eabi) {
                if (!lock_user_struct(VERIFY_READ, target_efl, arg3, 1)) 
                    goto efault;
                fl.l_type = tswap16(target_efl->l_type);
                fl.l_whence = tswap16(target_efl->l_whence);
                fl.l_start = tswap64(target_efl->l_start);
                fl.l_len = tswap64(target_efl->l_len);
                fl.l_pid = tswapl(target_efl->l_pid);
                unlock_user_struct(target_efl, arg3, 0);
            } else
#endif
            {
                if (!lock_user_struct(VERIFY_READ, target_fl, arg3, 1)) 
                    goto efault;
                fl.l_type = tswap16(target_fl->l_type);
                fl.l_whence = tswap16(target_fl->l_whence);
                fl.l_start = tswap64(target_fl->l_start);
                fl.l_len = tswap64(target_fl->l_len);
                fl.l_pid = tswapl(target_fl->l_pid);
                unlock_user_struct(target_fl, arg3, 0);
            }
            ret = get_errno(fcntl(arg1, cmd, &fl));
	    break;
        default:
            ret = do_fcntl(arg1, cmd, arg3);
            break;
        }
	break;
    }
#endif
#ifdef TARGET_NR_cacheflush
    case TARGET_NR_cacheflush:
        /* self-modifying code is handled automatically, so nothing needed */
        ret = 0;
        break;
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
#ifdef TARGET_NR_readahead
    case TARGET_NR_readahead:
        goto unimplemented;
#endif
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
#if defined(TARGET_MIPS)
      ((CPUMIPSState *) cpu_env)->tls_value = arg1;
      ret = 0;
      break;
#elif defined(TARGET_I386) && defined(TARGET_ABI32)
      ret = do_set_thread_area(cpu_env, arg1);
      break;
#else
      goto unimplemented_nowarn;
#endif
#endif
#ifdef TARGET_NR_get_thread_area
    case TARGET_NR_get_thread_area:
#if defined(TARGET_I386) && defined(TARGET_ABI32)
        ret = do_get_thread_area(cpu_env, arg1);
#else
        goto unimplemented_nowarn;
#endif
#endif
#ifdef TARGET_NR_getdomainname
    case TARGET_NR_getdomainname:
        goto unimplemented_nowarn;
#endif

#ifdef TARGET_NR_clock_gettime
    case TARGET_NR_clock_gettime:
    {
        struct timespec ts;
        ret = get_errno(clock_gettime(arg1, &ts));
        if (!is_error(ret)) {
            host_to_target_timespec(arg2, &ts);
        }
        break;
    }
#endif
#ifdef TARGET_NR_clock_getres
    case TARGET_NR_clock_getres:
    {
        struct timespec ts;
        ret = get_errno(clock_getres(arg1, &ts));
        if (!is_error(ret)) {
            host_to_target_timespec(arg2, &ts);
        }
        break;
    }
#endif
#ifdef TARGET_NR_clock_nanosleep
    case TARGET_NR_clock_nanosleep:
    {
        struct timespec ts;
        target_to_host_timespec(&ts, arg3);
        ret = get_errno(clock_nanosleep(arg1, arg2, &ts, arg4 ? &ts : NULL));
        if (arg4)
            host_to_target_timespec(arg4, &ts);
        break;
    }
#endif

#if defined(TARGET_NR_set_tid_address) && defined(__NR_set_tid_address)
    case TARGET_NR_set_tid_address:
        ret = get_errno(set_tid_address((int *)g2h(arg1)));
        break;
#endif

#if defined(TARGET_NR_tkill) && defined(__NR_tkill)
    case TARGET_NR_tkill:
        ret = get_errno(sys_tkill((int)arg1, target_to_host_signal(arg2)));
        break;
#endif

#if defined(TARGET_NR_tgkill) && defined(__NR_tgkill)
    case TARGET_NR_tgkill:
	ret = get_errno(sys_tgkill((int)arg1, (int)arg2,
                        target_to_host_signal(arg3)));
	break;
#endif

#ifdef TARGET_NR_set_robust_list
    case TARGET_NR_set_robust_list:
	goto unimplemented_nowarn;
#endif

#if defined(TARGET_NR_utimensat) && defined(__NR_utimensat)
    case TARGET_NR_utimensat:
        {
            struct timespec ts[2];
            target_to_host_timespec(ts, arg3);
            target_to_host_timespec(ts+1, arg3+sizeof(struct target_timespec));
            if (!arg2)
                ret = get_errno(sys_utimensat(arg1, NULL, ts, arg4));
            else {
                if (!(p = lock_user_string(arg2))) {
                    ret = -TARGET_EFAULT;
                    goto fail;
                }
                ret = get_errno(sys_utimensat(arg1, path(p), ts, arg4));
                unlock_user(p, arg2, 0);
            }
        }
	break;
#endif
#if defined(USE_NPTL)
    case TARGET_NR_futex:
        ret = do_futex(arg1, arg2, arg3, arg4, arg5, arg6);
        break;
#endif

    default:
    unimplemented:
        gemu_log("qemu: Unsupported syscall: %d\n", num);
#if defined(TARGET_NR_setxattr) || defined(TARGET_NR_get_thread_area) || defined(TARGET_NR_getdomainname) || defined(TARGET_NR_set_robust_list)
    unimplemented_nowarn:
#endif
        ret = -TARGET_ENOSYS;
        break;
    }
fail:
#ifdef DEBUG
    gemu_log(" = %ld\n", ret);
#endif
    if(do_strace)
        print_syscall_ret(num, ret);
    return ret;
efault:
    ret = -TARGET_EFAULT;
    goto fail;
}

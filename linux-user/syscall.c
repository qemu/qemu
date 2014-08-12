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
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#define _ATFILE_SOURCE
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
#include <grp.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/file.h>
#include <sys/fsuid.h>
#include <sys/personality.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <sys/swap.h>
#include <linux/capability.h>
#include <signal.h>
#include <sched.h>
#ifdef __ia64__
int __clone2(int (*fn)(void *), void *child_stack_base,
             size_t stack_size, int flags, void *arg, ...);
#endif
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <sys/poll.h>
#include <sys/times.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/statfs.h>
#include <sys/timerfd.h>
#include <utime.h>
#include <sys/sysinfo.h>
//#include <sys/user.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <linux/wireless.h>
#include <linux/icmp.h>
#include "qemu-common.h"
#ifdef TARGET_GPROF
#include <sys/gmon.h>
#endif
#ifdef CONFIG_EVENTFD
#include <sys/eventfd.h>
#endif
#ifdef CONFIG_EPOLL
#include <sys/epoll.h>
#endif
#ifdef CONFIG_ATTR
#include "qemu/xattr.h"
#endif
#ifdef CONFIG_SENDFILE
#include <sys/sendfile.h>
#endif

#define termios host_termios
#define winsize host_winsize
#define termio host_termio
#define sgttyb host_sgttyb /* same as target */
#define tchars host_tchars /* same as target */
#define ltchars host_ltchars /* same as target */

#include <linux/termios.h>
#include <linux/unistd.h>
#include <linux/cdrom.h>
#include <linux/hdreg.h>
#include <linux/soundcard.h>
#include <linux/kd.h>
#include <linux/mtio.h>
#include <linux/fs.h>
#if defined(CONFIG_FIEMAP)
#include <linux/fiemap.h>
#endif
#include <linux/fb.h>
#include <linux/vt.h>
#include <linux/dm-ioctl.h>
#include <linux/reboot.h>
#include <linux/route.h>
#include <linux/filter.h>
#include <linux/blkpg.h>
#include "linux_loop.h"
#include "uname.h"

#include "qemu.h"

#define CLONE_NPTL_FLAGS2 (CLONE_SETTLS | \
    CLONE_PARENT_SETTID | CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID)

//#define DEBUG

//#include <linux/msdos_fs.h>
#define	VFAT_IOCTL_READDIR_BOTH		_IOR('r', 1, struct linux_dirent [2])
#define	VFAT_IOCTL_READDIR_SHORT	_IOR('r', 2, struct linux_dirent [2])


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
#define __NR_sys_getcwd1 __NR_getcwd
#define __NR_sys_getdents __NR_getdents
#define __NR_sys_getdents64 __NR_getdents64
#define __NR_sys_getpriority __NR_getpriority
#define __NR_sys_rt_sigqueueinfo __NR_rt_sigqueueinfo
#define __NR_sys_syslog __NR_syslog
#define __NR_sys_tgkill __NR_tgkill
#define __NR_sys_tkill __NR_tkill
#define __NR_sys_futex __NR_futex
#define __NR_sys_inotify_init __NR_inotify_init
#define __NR_sys_inotify_add_watch __NR_inotify_add_watch
#define __NR_sys_inotify_rm_watch __NR_inotify_rm_watch

#if defined(__alpha__) || defined (__ia64__) || defined(__x86_64__) || \
    defined(__s390x__)
#define __NR__llseek __NR_lseek
#endif

/* Newer kernel ports have llseek() instead of _llseek() */
#if defined(TARGET_NR_llseek) && !defined(TARGET_NR__llseek)
#define TARGET_NR__llseek TARGET_NR_llseek
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
#ifdef __NR_getdents
_syscall3(int, sys_getdents, uint, fd, struct linux_dirent *, dirp, uint, count);
#endif
#if !defined(__NR_getdents) || \
    (defined(TARGET_NR_getdents64) && defined(__NR_getdents64))
_syscall3(int, sys_getdents64, uint, fd, struct linux_dirent64 *, dirp, uint, count);
#endif
#if defined(TARGET_NR__llseek) && defined(__NR_llseek)
_syscall5(int, _llseek,  uint,  fd, ulong, hi, ulong, lo,
          loff_t *, res, uint, wh);
#endif
_syscall3(int,sys_rt_sigqueueinfo,int,pid,int,sig,siginfo_t *,uinfo)
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
#if defined(TARGET_NR_futex) && defined(__NR_futex)
_syscall6(int,sys_futex,int *,uaddr,int,op,int,val,
          const struct timespec *,timeout,int *,uaddr2,int,val3)
#endif
#define __NR_sys_sched_getaffinity __NR_sched_getaffinity
_syscall3(int, sys_sched_getaffinity, pid_t, pid, unsigned int, len,
          unsigned long *, user_mask_ptr);
#define __NR_sys_sched_setaffinity __NR_sched_setaffinity
_syscall3(int, sys_sched_setaffinity, pid_t, pid, unsigned int, len,
          unsigned long *, user_mask_ptr);
_syscall4(int, reboot, int, magic1, int, magic2, unsigned int, cmd,
          void *, arg);
_syscall2(int, capget, struct __user_cap_header_struct *, header,
          struct __user_cap_data_struct *, data);
_syscall2(int, capset, struct __user_cap_header_struct *, header,
          struct __user_cap_data_struct *, data);
#if defined(TARGET_NR_ioprio_get) && defined(__NR_ioprio_get)
_syscall2(int, ioprio_get, int, which, int, who)
#endif
#if defined(TARGET_NR_ioprio_set) && defined(__NR_ioprio_set)
_syscall3(int, ioprio_set, int, which, int, who, int, ioprio)
#endif

static bitmask_transtbl fcntl_flags_tbl[] = {
  { TARGET_O_ACCMODE,   TARGET_O_WRONLY,    O_ACCMODE,   O_WRONLY,    },
  { TARGET_O_ACCMODE,   TARGET_O_RDWR,      O_ACCMODE,   O_RDWR,      },
  { TARGET_O_CREAT,     TARGET_O_CREAT,     O_CREAT,     O_CREAT,     },
  { TARGET_O_EXCL,      TARGET_O_EXCL,      O_EXCL,      O_EXCL,      },
  { TARGET_O_NOCTTY,    TARGET_O_NOCTTY,    O_NOCTTY,    O_NOCTTY,    },
  { TARGET_O_TRUNC,     TARGET_O_TRUNC,     O_TRUNC,     O_TRUNC,     },
  { TARGET_O_APPEND,    TARGET_O_APPEND,    O_APPEND,    O_APPEND,    },
  { TARGET_O_NONBLOCK,  TARGET_O_NONBLOCK,  O_NONBLOCK,  O_NONBLOCK,  },
  { TARGET_O_SYNC,      TARGET_O_DSYNC,     O_SYNC,      O_DSYNC,     },
  { TARGET_O_SYNC,      TARGET_O_SYNC,      O_SYNC,      O_SYNC,      },
  { TARGET_FASYNC,      TARGET_FASYNC,      FASYNC,      FASYNC,      },
  { TARGET_O_DIRECTORY, TARGET_O_DIRECTORY, O_DIRECTORY, O_DIRECTORY, },
  { TARGET_O_NOFOLLOW,  TARGET_O_NOFOLLOW,  O_NOFOLLOW,  O_NOFOLLOW,  },
#if defined(O_DIRECT)
  { TARGET_O_DIRECT,    TARGET_O_DIRECT,    O_DIRECT,    O_DIRECT,    },
#endif
#if defined(O_NOATIME)
  { TARGET_O_NOATIME,   TARGET_O_NOATIME,   O_NOATIME,   O_NOATIME    },
#endif
#if defined(O_CLOEXEC)
  { TARGET_O_CLOEXEC,   TARGET_O_CLOEXEC,   O_CLOEXEC,   O_CLOEXEC    },
#endif
#if defined(O_PATH)
  { TARGET_O_PATH,      TARGET_O_PATH,      O_PATH,      O_PATH       },
#endif
  /* Don't terminate the list prematurely on 64-bit host+guest.  */
#if TARGET_O_LARGEFILE != 0 || O_LARGEFILE != 0
  { TARGET_O_LARGEFILE, TARGET_O_LARGEFILE, O_LARGEFILE, O_LARGEFILE, },
#endif
  { 0, 0, 0, 0 }
};

static int sys_getcwd1(char *buf, size_t size)
{
  if (getcwd(buf, size) == NULL) {
      /* getcwd() sets errno */
      return (-1);
  }
  return strlen(buf)+1;
}

static int sys_openat(int dirfd, const char *pathname, int flags, mode_t mode)
{
  /*
   * open(2) has extra parameter 'mode' when called with
   * flag O_CREAT.
   */
  if ((flags & O_CREAT) != 0) {
      return (openat(dirfd, pathname, flags, mode));
  }
  return (openat(dirfd, pathname, flags));
}

#ifdef TARGET_NR_utimensat
#ifdef CONFIG_UTIMENSAT
static int sys_utimensat(int dirfd, const char *pathname,
    const struct timespec times[2], int flags)
{
    if (pathname == NULL)
        return futimens(dirfd, times);
    else
        return utimensat(dirfd, pathname, times, flags);
}
#elif defined(__NR_utimensat)
#define __NR_sys_utimensat __NR_utimensat
_syscall4(int,sys_utimensat,int,dirfd,const char *,pathname,
          const struct timespec *,tsp,int,flags)
#else
static int sys_utimensat(int dirfd, const char *pathname,
                         const struct timespec times[2], int flags)
{
    errno = ENOSYS;
    return -1;
}
#endif
#endif /* TARGET_NR_utimensat */

#ifdef CONFIG_INOTIFY
#include <sys/inotify.h>

#if defined(TARGET_NR_inotify_init) && defined(__NR_inotify_init)
static int sys_inotify_init(void)
{
  return (inotify_init());
}
#endif
#if defined(TARGET_NR_inotify_add_watch) && defined(__NR_inotify_add_watch)
static int sys_inotify_add_watch(int fd,const char *pathname, int32_t mask)
{
  return (inotify_add_watch(fd, pathname, mask));
}
#endif
#if defined(TARGET_NR_inotify_rm_watch) && defined(__NR_inotify_rm_watch)
static int sys_inotify_rm_watch(int fd, int32_t wd)
{
  return (inotify_rm_watch(fd, wd));
}
#endif
#ifdef CONFIG_INOTIFY1
#if defined(TARGET_NR_inotify_init1) && defined(__NR_inotify_init1)
static int sys_inotify_init1(int flags)
{
  return (inotify_init1(flags));
}
#endif
#endif
#else
/* Userspace can usually survive runtime without inotify */
#undef TARGET_NR_inotify_init
#undef TARGET_NR_inotify_init1
#undef TARGET_NR_inotify_add_watch
#undef TARGET_NR_inotify_rm_watch
#endif /* CONFIG_INOTIFY  */

#if defined(TARGET_NR_ppoll)
#ifndef __NR_ppoll
# define __NR_ppoll -1
#endif
#define __NR_sys_ppoll __NR_ppoll
_syscall5(int, sys_ppoll, struct pollfd *, fds, nfds_t, nfds,
          struct timespec *, timeout, const sigset_t *, sigmask,
          size_t, sigsetsize)
#endif

#if defined(TARGET_NR_pselect6)
#ifndef __NR_pselect6
# define __NR_pselect6 -1
#endif
#define __NR_sys_pselect6 __NR_pselect6
_syscall6(int, sys_pselect6, int, nfds, fd_set *, readfds, fd_set *, writefds,
          fd_set *, exceptfds, struct timespec *, timeout, void *, sig);
#endif

#if defined(TARGET_NR_prlimit64)
#ifndef __NR_prlimit64
# define __NR_prlimit64 -1
#endif
#define __NR_sys_prlimit64 __NR_prlimit64
/* The glibc rlimit structure may not be that used by the underlying syscall */
struct host_rlimit64 {
    uint64_t rlim_cur;
    uint64_t rlim_max;
};
_syscall4(int, sys_prlimit64, pid_t, pid, int, resource,
          const struct host_rlimit64 *, new_limit,
          struct host_rlimit64 *, old_limit)
#endif


#if defined(TARGET_NR_timer_create)
/* Maxiumum of 32 active POSIX timers allowed at any one time. */
static timer_t g_posix_timers[32] = { 0, } ;

static inline int next_free_host_timer(void)
{
    int k ;
    /* FIXME: Does finding the next free slot require a lock? */
    for (k = 0; k < ARRAY_SIZE(g_posix_timers); k++) {
        if (g_posix_timers[k] == 0) {
            g_posix_timers[k] = (timer_t) 1;
            return k;
        }
    }
    return -1;
}
#endif

/* ARM EABI and MIPS expect 64bit types aligned even on pairs or registers */
#ifdef TARGET_ARM
static inline int regpairs_aligned(void *cpu_env) {
    return ((((CPUARMState *)cpu_env)->eabi) == 1) ;
}
#elif defined(TARGET_MIPS)
static inline int regpairs_aligned(void *cpu_env) { return 1; }
#elif defined(TARGET_PPC) && !defined(TARGET_PPC64)
/* SysV AVI for PPC32 expects 64bit parameters to be passed on odd/even pairs
 * of registers which translates to the same as ARM/MIPS, because we start with
 * r3 as arg1 */
static inline int regpairs_aligned(void *cpu_env) { return 1; }
#else
static inline int regpairs_aligned(void *cpu_env) { return 0; }
#endif

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
    if ((err >= ERRNO_TABLE_SIZE) || (err < 0)) {
        return NULL;
    }
    return strerror(target_to_host_errno(err));
}

static inline int host_to_target_sock_type(int host_type)
{
    int target_type;

    switch (host_type & 0xf /* SOCK_TYPE_MASK */) {
    case SOCK_DGRAM:
        target_type = TARGET_SOCK_DGRAM;
        break;
    case SOCK_STREAM:
        target_type = TARGET_SOCK_STREAM;
        break;
    default:
        target_type = host_type & 0xf /* SOCK_TYPE_MASK */;
        break;
    }

#if defined(SOCK_CLOEXEC)
    if (host_type & SOCK_CLOEXEC) {
        target_type |= TARGET_SOCK_CLOEXEC;
    }
#endif

#if defined(SOCK_NONBLOCK)
    if (host_type & SOCK_NONBLOCK) {
        target_type |= TARGET_SOCK_NONBLOCK;
    }
#endif

    return target_type;
}

static abi_ulong target_brk;
static abi_ulong target_original_brk;
static abi_ulong brk_page;

void target_set_brk(abi_ulong new_brk)
{
    target_original_brk = target_brk = HOST_PAGE_ALIGN(new_brk);
    brk_page = HOST_PAGE_ALIGN(target_brk);
}

//#define DEBUGF_BRK(message, args...) do { fprintf(stderr, (message), ## args); } while (0)
#define DEBUGF_BRK(message, args...)

/* do_brk() must return target values and target errnos. */
abi_long do_brk(abi_ulong new_brk)
{
    abi_long mapped_addr;
    int	new_alloc_size;

    DEBUGF_BRK("do_brk(" TARGET_ABI_FMT_lx ") -> ", new_brk);

    if (!new_brk) {
        DEBUGF_BRK(TARGET_ABI_FMT_lx " (!new_brk)\n", target_brk);
        return target_brk;
    }
    if (new_brk < target_original_brk) {
        DEBUGF_BRK(TARGET_ABI_FMT_lx " (new_brk < target_original_brk)\n",
                   target_brk);
        return target_brk;
    }

    /* If the new brk is less than the highest page reserved to the
     * target heap allocation, set it and we're almost done...  */
    if (new_brk <= brk_page) {
        /* Heap contents are initialized to zero, as for anonymous
         * mapped pages.  */
        if (new_brk > target_brk) {
            memset(g2h(target_brk), 0, new_brk - target_brk);
        }
	target_brk = new_brk;
        DEBUGF_BRK(TARGET_ABI_FMT_lx " (new_brk <= brk_page)\n", target_brk);
    	return target_brk;
    }

    /* We need to allocate more memory after the brk... Note that
     * we don't use MAP_FIXED because that will map over the top of
     * any existing mapping (like the one with the host libc or qemu
     * itself); instead we treat "mapped but at wrong address" as
     * a failure and unmap again.
     */
    new_alloc_size = HOST_PAGE_ALIGN(new_brk - brk_page);
    mapped_addr = get_errno(target_mmap(brk_page, new_alloc_size,
                                        PROT_READ|PROT_WRITE,
                                        MAP_ANON|MAP_PRIVATE, 0, 0));

    if (mapped_addr == brk_page) {
        /* Heap contents are initialized to zero, as for anonymous
         * mapped pages.  Technically the new pages are already
         * initialized to zero since they *are* anonymous mapped
         * pages, however we have to take care with the contents that
         * come from the remaining part of the previous page: it may
         * contains garbage data due to a previous heap usage (grown
         * then shrunken).  */
        memset(g2h(target_brk), 0, brk_page - target_brk);

        target_brk = new_brk;
        brk_page = HOST_PAGE_ALIGN(target_brk);
        DEBUGF_BRK(TARGET_ABI_FMT_lx " (mapped_addr == brk_page)\n",
            target_brk);
        return target_brk;
    } else if (mapped_addr != -1) {
        /* Mapped but at wrong address, meaning there wasn't actually
         * enough space for this brk.
         */
        target_munmap(mapped_addr, new_alloc_size);
        mapped_addr = -1;
        DEBUGF_BRK(TARGET_ABI_FMT_lx " (mapped_addr != -1)\n", target_brk);
    }
    else {
        DEBUGF_BRK(TARGET_ABI_FMT_lx " (otherwise)\n", target_brk);
    }

#if defined(TARGET_ALPHA)
    /* We (partially) emulate OSF/1 on Alpha, which requires we
       return a proper errno, not an unchanged brk value.  */
    return -TARGET_ENOMEM;
#endif
    /* For everything else, return the previous break. */
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

static inline abi_ulong copy_from_user_fdset_ptr(fd_set *fds, fd_set **fds_ptr,
                                                 abi_ulong target_fds_addr,
                                                 int n)
{
    if (target_fds_addr) {
        if (copy_from_user_fdset(fds, target_fds_addr, n))
            return -TARGET_EFAULT;
        *fds_ptr = fds;
    } else {
        *fds_ptr = NULL;
    }
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
            v |= ((abi_ulong)(FD_ISSET(k, fds) != 0) << j);
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
    target_rusage->ru_utime.tv_sec = tswapal(rusage->ru_utime.tv_sec);
    target_rusage->ru_utime.tv_usec = tswapal(rusage->ru_utime.tv_usec);
    target_rusage->ru_stime.tv_sec = tswapal(rusage->ru_stime.tv_sec);
    target_rusage->ru_stime.tv_usec = tswapal(rusage->ru_stime.tv_usec);
    target_rusage->ru_maxrss = tswapal(rusage->ru_maxrss);
    target_rusage->ru_ixrss = tswapal(rusage->ru_ixrss);
    target_rusage->ru_idrss = tswapal(rusage->ru_idrss);
    target_rusage->ru_isrss = tswapal(rusage->ru_isrss);
    target_rusage->ru_minflt = tswapal(rusage->ru_minflt);
    target_rusage->ru_majflt = tswapal(rusage->ru_majflt);
    target_rusage->ru_nswap = tswapal(rusage->ru_nswap);
    target_rusage->ru_inblock = tswapal(rusage->ru_inblock);
    target_rusage->ru_oublock = tswapal(rusage->ru_oublock);
    target_rusage->ru_msgsnd = tswapal(rusage->ru_msgsnd);
    target_rusage->ru_msgrcv = tswapal(rusage->ru_msgrcv);
    target_rusage->ru_nsignals = tswapal(rusage->ru_nsignals);
    target_rusage->ru_nvcsw = tswapal(rusage->ru_nvcsw);
    target_rusage->ru_nivcsw = tswapal(rusage->ru_nivcsw);
    unlock_user_struct(target_rusage, target_addr, 1);

    return 0;
}

static inline rlim_t target_to_host_rlim(abi_ulong target_rlim)
{
    abi_ulong target_rlim_swap;
    rlim_t result;
    
    target_rlim_swap = tswapal(target_rlim);
    if (target_rlim_swap == TARGET_RLIM_INFINITY)
        return RLIM_INFINITY;

    result = target_rlim_swap;
    if (target_rlim_swap != (rlim_t)result)
        return RLIM_INFINITY;
    
    return result;
}

static inline abi_ulong host_to_target_rlim(rlim_t rlim)
{
    abi_ulong target_rlim_swap;
    abi_ulong result;
    
    if (rlim == RLIM_INFINITY || rlim != (abi_long)rlim)
        target_rlim_swap = TARGET_RLIM_INFINITY;
    else
        target_rlim_swap = rlim;
    result = tswapal(target_rlim_swap);
    
    return result;
}

static inline int target_to_host_resource(int code)
{
    switch (code) {
    case TARGET_RLIMIT_AS:
        return RLIMIT_AS;
    case TARGET_RLIMIT_CORE:
        return RLIMIT_CORE;
    case TARGET_RLIMIT_CPU:
        return RLIMIT_CPU;
    case TARGET_RLIMIT_DATA:
        return RLIMIT_DATA;
    case TARGET_RLIMIT_FSIZE:
        return RLIMIT_FSIZE;
    case TARGET_RLIMIT_LOCKS:
        return RLIMIT_LOCKS;
    case TARGET_RLIMIT_MEMLOCK:
        return RLIMIT_MEMLOCK;
    case TARGET_RLIMIT_MSGQUEUE:
        return RLIMIT_MSGQUEUE;
    case TARGET_RLIMIT_NICE:
        return RLIMIT_NICE;
    case TARGET_RLIMIT_NOFILE:
        return RLIMIT_NOFILE;
    case TARGET_RLIMIT_NPROC:
        return RLIMIT_NPROC;
    case TARGET_RLIMIT_RSS:
        return RLIMIT_RSS;
    case TARGET_RLIMIT_RTPRIO:
        return RLIMIT_RTPRIO;
    case TARGET_RLIMIT_SIGPENDING:
        return RLIMIT_SIGPENDING;
    case TARGET_RLIMIT_STACK:
        return RLIMIT_STACK;
    default:
        return code;
    }
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

static inline abi_long copy_from_user_timezone(struct timezone *tz,
                                               abi_ulong target_tz_addr)
{
    struct target_timezone *target_tz;

    if (!lock_user_struct(VERIFY_READ, target_tz, target_tz_addr, 1)) {
        return -TARGET_EFAULT;
    }

    __get_user(tz->tz_minuteswest, &target_tz->tz_minuteswest);
    __get_user(tz->tz_dsttime, &target_tz->tz_dsttime);

    unlock_user_struct(target_tz, target_tz_addr, 0);

    return 0;
}

#if defined(TARGET_NR_mq_open) && defined(__NR_mq_open)
#include <mqueue.h>

static inline abi_long copy_from_user_mq_attr(struct mq_attr *attr,
                                              abi_ulong target_mq_attr_addr)
{
    struct target_mq_attr *target_mq_attr;

    if (!lock_user_struct(VERIFY_READ, target_mq_attr,
                          target_mq_attr_addr, 1))
        return -TARGET_EFAULT;

    __get_user(attr->mq_flags, &target_mq_attr->mq_flags);
    __get_user(attr->mq_maxmsg, &target_mq_attr->mq_maxmsg);
    __get_user(attr->mq_msgsize, &target_mq_attr->mq_msgsize);
    __get_user(attr->mq_curmsgs, &target_mq_attr->mq_curmsgs);

    unlock_user_struct(target_mq_attr, target_mq_attr_addr, 0);

    return 0;
}

static inline abi_long copy_to_user_mq_attr(abi_ulong target_mq_attr_addr,
                                            const struct mq_attr *attr)
{
    struct target_mq_attr *target_mq_attr;

    if (!lock_user_struct(VERIFY_WRITE, target_mq_attr,
                          target_mq_attr_addr, 0))
        return -TARGET_EFAULT;

    __put_user(attr->mq_flags, &target_mq_attr->mq_flags);
    __put_user(attr->mq_maxmsg, &target_mq_attr->mq_maxmsg);
    __put_user(attr->mq_msgsize, &target_mq_attr->mq_msgsize);
    __put_user(attr->mq_curmsgs, &target_mq_attr->mq_curmsgs);

    unlock_user_struct(target_mq_attr, target_mq_attr_addr, 1);

    return 0;
}
#endif

#if defined(TARGET_NR_select) || defined(TARGET_NR__newselect)
/* do_select() must return target values and target errnos. */
static abi_long do_select(int n,
                          abi_ulong rfd_addr, abi_ulong wfd_addr,
                          abi_ulong efd_addr, abi_ulong target_tv_addr)
{
    fd_set rfds, wfds, efds;
    fd_set *rfds_ptr, *wfds_ptr, *efds_ptr;
    struct timeval tv, *tv_ptr;
    abi_long ret;

    ret = copy_from_user_fdset_ptr(&rfds, &rfds_ptr, rfd_addr, n);
    if (ret) {
        return ret;
    }
    ret = copy_from_user_fdset_ptr(&wfds, &wfds_ptr, wfd_addr, n);
    if (ret) {
        return ret;
    }
    ret = copy_from_user_fdset_ptr(&efds, &efds_ptr, efd_addr, n);
    if (ret) {
        return ret;
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
#endif

static abi_long do_pipe2(int host_pipe[], int flags)
{
#ifdef CONFIG_PIPE2
    return pipe2(host_pipe, flags);
#else
    return -ENOSYS;
#endif
}

static abi_long do_pipe(void *cpu_env, abi_ulong pipedes,
                        int flags, int is_pipe2)
{
    int host_pipe[2];
    abi_long ret;
    ret = flags ? do_pipe2(host_pipe, flags) : pipe(host_pipe);

    if (is_error(ret))
        return get_errno(ret);

    /* Several targets have special calling conventions for the original
       pipe syscall, but didn't replicate this into the pipe2 syscall.  */
    if (!is_pipe2) {
#if defined(TARGET_ALPHA)
        ((CPUAlphaState *)cpu_env)->ir[IR_A4] = host_pipe[1];
        return host_pipe[0];
#elif defined(TARGET_MIPS)
        ((CPUMIPSState*)cpu_env)->active_tc.gpr[3] = host_pipe[1];
        return host_pipe[0];
#elif defined(TARGET_SH4)
        ((CPUSH4State*)cpu_env)->gregs[1] = host_pipe[1];
        return host_pipe[0];
#elif defined(TARGET_SPARC)
        ((CPUSPARCState*)cpu_env)->regwptr[1] = host_pipe[1];
        return host_pipe[0];
#endif
    }

    if (put_user_s32(host_pipe[0], pipedes)
        || put_user_s32(host_pipe[1], pipedes + sizeof(host_pipe[0])))
        return -TARGET_EFAULT;
    return get_errno(ret);
}

static inline abi_long target_to_host_ip_mreq(struct ip_mreqn *mreqn,
                                              abi_ulong target_addr,
                                              socklen_t len)
{
    struct target_ip_mreqn *target_smreqn;

    target_smreqn = lock_user(VERIFY_READ, target_addr, len, 1);
    if (!target_smreqn)
        return -TARGET_EFAULT;
    mreqn->imr_multiaddr.s_addr = target_smreqn->imr_multiaddr.s_addr;
    mreqn->imr_address.s_addr = target_smreqn->imr_address.s_addr;
    if (len == sizeof(struct target_ip_mreqn))
        mreqn->imr_ifindex = tswapal(target_smreqn->imr_ifindex);
    unlock_user(target_smreqn, target_addr, 0);

    return 0;
}

static inline abi_long target_to_host_sockaddr(struct sockaddr *addr,
                                               abi_ulong target_addr,
                                               socklen_t len)
{
    const socklen_t unix_maxlen = sizeof (struct sockaddr_un);
    sa_family_t sa_family;
    struct target_sockaddr *target_saddr;

    target_saddr = lock_user(VERIFY_READ, target_addr, len, 1);
    if (!target_saddr)
        return -TARGET_EFAULT;

    sa_family = tswap16(target_saddr->sa_family);

    /* Oops. The caller might send a incomplete sun_path; sun_path
     * must be terminated by \0 (see the manual page), but
     * unfortunately it is quite common to specify sockaddr_un
     * length as "strlen(x->sun_path)" while it should be
     * "strlen(...) + 1". We'll fix that here if needed.
     * Linux kernel has a similar feature.
     */

    if (sa_family == AF_UNIX) {
        if (len < unix_maxlen && len > 0) {
            char *cp = (char*)target_saddr;

            if ( cp[len-1] && !cp[len] )
                len++;
        }
        if (len > unix_maxlen)
            len = unix_maxlen;
    }

    memcpy(addr, target_saddr, len);
    addr->sa_family = sa_family;
    if (sa_family == AF_PACKET) {
	struct target_sockaddr_ll *lladdr;

	lladdr = (struct target_sockaddr_ll *)addr;
	lladdr->sll_ifindex = tswap32(lladdr->sll_ifindex);
	lladdr->sll_hatype = tswap16(lladdr->sll_hatype);
    }
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

static inline abi_long target_to_host_cmsg(struct msghdr *msgh,
                                           struct target_msghdr *target_msgh)
{
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(msgh);
    abi_long msg_controllen;
    abi_ulong target_cmsg_addr;
    struct target_cmsghdr *target_cmsg;
    socklen_t space = 0;
    
    msg_controllen = tswapal(target_msgh->msg_controllen);
    if (msg_controllen < sizeof (struct target_cmsghdr)) 
        goto the_end;
    target_cmsg_addr = tswapal(target_msgh->msg_control);
    target_cmsg = lock_user(VERIFY_READ, target_cmsg_addr, msg_controllen, 1);
    if (!target_cmsg)
        return -TARGET_EFAULT;

    while (cmsg && target_cmsg) {
        void *data = CMSG_DATA(cmsg);
        void *target_data = TARGET_CMSG_DATA(target_cmsg);

        int len = tswapal(target_cmsg->cmsg_len)
                  - TARGET_CMSG_ALIGN(sizeof (struct target_cmsghdr));

        space += CMSG_SPACE(len);
        if (space > msgh->msg_controllen) {
            space -= CMSG_SPACE(len);
            gemu_log("Host cmsg overflow\n");
            break;
        }

        if (tswap32(target_cmsg->cmsg_level) == TARGET_SOL_SOCKET) {
            cmsg->cmsg_level = SOL_SOCKET;
        } else {
            cmsg->cmsg_level = tswap32(target_cmsg->cmsg_level);
        }
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
    unlock_user(target_cmsg, target_cmsg_addr, 0);
 the_end:
    msgh->msg_controllen = space;
    return 0;
}

static inline abi_long host_to_target_cmsg(struct target_msghdr *target_msgh,
                                           struct msghdr *msgh)
{
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(msgh);
    abi_long msg_controllen;
    abi_ulong target_cmsg_addr;
    struct target_cmsghdr *target_cmsg;
    socklen_t space = 0;

    msg_controllen = tswapal(target_msgh->msg_controllen);
    if (msg_controllen < sizeof (struct target_cmsghdr)) 
        goto the_end;
    target_cmsg_addr = tswapal(target_msgh->msg_control);
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

        if (cmsg->cmsg_level == SOL_SOCKET) {
            target_cmsg->cmsg_level = tswap32(TARGET_SOL_SOCKET);
        } else {
            target_cmsg->cmsg_level = tswap32(cmsg->cmsg_level);
        }
        target_cmsg->cmsg_type = tswap32(cmsg->cmsg_type);
        target_cmsg->cmsg_len = tswapal(TARGET_CMSG_LEN(len));

        switch (cmsg->cmsg_level) {
        case SOL_SOCKET:
            switch (cmsg->cmsg_type) {
            case SCM_RIGHTS:
            {
                int *fd = (int *)data;
                int *target_fd = (int *)target_data;
                int i, numfds = len / sizeof(int);

                for (i = 0; i < numfds; i++)
                    target_fd[i] = tswap32(fd[i]);
                break;
            }
            case SO_TIMESTAMP:
            {
                struct timeval *tv = (struct timeval *)data;
                struct target_timeval *target_tv =
                    (struct target_timeval *)target_data;

                if (len != sizeof(struct timeval))
                    goto unimplemented;

                /* copy struct timeval to target */
                target_tv->tv_sec = tswapal(tv->tv_sec);
                target_tv->tv_usec = tswapal(tv->tv_usec);
                break;
            }
            case SCM_CREDENTIALS:
            {
                struct ucred *cred = (struct ucred *)data;
                struct target_ucred *target_cred =
                    (struct target_ucred *)target_data;

                __put_user(cred->pid, &target_cred->pid);
                __put_user(cred->uid, &target_cred->uid);
                __put_user(cred->gid, &target_cred->gid);
                break;
            }
            default:
                goto unimplemented;
            }
            break;

        default:
        unimplemented:
            gemu_log("Unsupported ancillary data: %d/%d\n",
                                        cmsg->cmsg_level, cmsg->cmsg_type);
            memcpy(target_data, data, len);
        }

        cmsg = CMSG_NXTHDR(msgh, cmsg);
        target_cmsg = TARGET_CMSG_NXTHDR(target_msgh, target_cmsg);
    }
    unlock_user(target_cmsg, target_cmsg_addr, space);
 the_end:
    target_msgh->msg_controllen = tswapal(space);
    return 0;
}

/* do_setsockopt() Must return target values and target errnos. */
static abi_long do_setsockopt(int sockfd, int level, int optname,
                              abi_ulong optval_addr, socklen_t optlen)
{
    abi_long ret;
    int val;
    struct ip_mreqn *ip_mreq;
    struct ip_mreq_source *ip_mreq_source;

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
        case IP_ADD_MEMBERSHIP:
        case IP_DROP_MEMBERSHIP:
            if (optlen < sizeof (struct target_ip_mreq) ||
                optlen > sizeof (struct target_ip_mreqn))
                return -TARGET_EINVAL;

            ip_mreq = (struct ip_mreqn *) alloca(optlen);
            target_to_host_ip_mreq(ip_mreq, optval_addr, optlen);
            ret = get_errno(setsockopt(sockfd, level, optname, ip_mreq, optlen));
            break;

        case IP_BLOCK_SOURCE:
        case IP_UNBLOCK_SOURCE:
        case IP_ADD_SOURCE_MEMBERSHIP:
        case IP_DROP_SOURCE_MEMBERSHIP:
            if (optlen != sizeof (struct target_ip_mreq_source))
                return -TARGET_EINVAL;

            ip_mreq_source = lock_user(VERIFY_READ, optval_addr, optlen, 1);
            ret = get_errno(setsockopt(sockfd, level, optname, ip_mreq_source, optlen));
            unlock_user (ip_mreq_source, optval_addr, 0);
            break;

        default:
            goto unimplemented;
        }
        break;
    case SOL_IPV6:
        switch (optname) {
        case IPV6_MTU_DISCOVER:
        case IPV6_MTU:
        case IPV6_V6ONLY:
        case IPV6_RECVPKTINFO:
            val = 0;
            if (optlen < sizeof(uint32_t)) {
                return -TARGET_EINVAL;
            }
            if (get_user_u32(val, optval_addr)) {
                return -TARGET_EFAULT;
            }
            ret = get_errno(setsockopt(sockfd, level, optname,
                                       &val, sizeof(val)));
            break;
        default:
            goto unimplemented;
        }
        break;
    case SOL_RAW:
        switch (optname) {
        case ICMP_FILTER:
            /* struct icmp_filter takes an u32 value */
            if (optlen < sizeof(uint32_t)) {
                return -TARGET_EINVAL;
            }

            if (get_user_u32(val, optval_addr)) {
                return -TARGET_EFAULT;
            }
            ret = get_errno(setsockopt(sockfd, level, optname,
                                       &val, sizeof(val)));
            break;

        default:
            goto unimplemented;
        }
        break;
    case TARGET_SOL_SOCKET:
        switch (optname) {
        case TARGET_SO_RCVTIMEO:
        {
                struct timeval tv;

                optname = SO_RCVTIMEO;

set_timeout:
                if (optlen != sizeof(struct target_timeval)) {
                    return -TARGET_EINVAL;
                }

                if (copy_from_user_timeval(&tv, optval_addr)) {
                    return -TARGET_EFAULT;
                }

                ret = get_errno(setsockopt(sockfd, SOL_SOCKET, optname,
                                &tv, sizeof(tv)));
                return ret;
        }
        case TARGET_SO_SNDTIMEO:
                optname = SO_SNDTIMEO;
                goto set_timeout;
        case TARGET_SO_ATTACH_FILTER:
        {
                struct target_sock_fprog *tfprog;
                struct target_sock_filter *tfilter;
                struct sock_fprog fprog;
                struct sock_filter *filter;
                int i;

                if (optlen != sizeof(*tfprog)) {
                    return -TARGET_EINVAL;
                }
                if (!lock_user_struct(VERIFY_READ, tfprog, optval_addr, 0)) {
                    return -TARGET_EFAULT;
                }
                if (!lock_user_struct(VERIFY_READ, tfilter,
                                      tswapal(tfprog->filter), 0)) {
                    unlock_user_struct(tfprog, optval_addr, 1);
                    return -TARGET_EFAULT;
                }

                fprog.len = tswap16(tfprog->len);
                filter = malloc(fprog.len * sizeof(*filter));
                if (filter == NULL) {
                    unlock_user_struct(tfilter, tfprog->filter, 1);
                    unlock_user_struct(tfprog, optval_addr, 1);
                    return -TARGET_ENOMEM;
                }
                for (i = 0; i < fprog.len; i++) {
                    filter[i].code = tswap16(tfilter[i].code);
                    filter[i].jt = tfilter[i].jt;
                    filter[i].jf = tfilter[i].jf;
                    filter[i].k = tswap32(tfilter[i].k);
                }
                fprog.filter = filter;

                ret = get_errno(setsockopt(sockfd, SOL_SOCKET,
                                SO_ATTACH_FILTER, &fprog, sizeof(fprog)));
                free(filter);

                unlock_user_struct(tfilter, tfprog->filter, 1);
                unlock_user_struct(tfprog, optval_addr, 1);
                return ret;
        }
	case TARGET_SO_BINDTODEVICE:
	{
		char *dev_ifname, *addr_ifname;

		if (optlen > IFNAMSIZ - 1) {
		    optlen = IFNAMSIZ - 1;
		}
		dev_ifname = lock_user(VERIFY_READ, optval_addr, optlen, 1);
		if (!dev_ifname) {
		    return -TARGET_EFAULT;
		}
		optname = SO_BINDTODEVICE;
		addr_ifname = alloca(IFNAMSIZ);
		memcpy(addr_ifname, dev_ifname, optlen);
		addr_ifname[optlen] = 0;
		ret = get_errno(setsockopt(sockfd, level, optname, addr_ifname, optlen));
		unlock_user (dev_ifname, optval_addr, 0);
		return ret;
	}
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
        case TARGET_SO_SNDBUFFORCE:
                optname = SO_SNDBUFFORCE;
                break;
        case TARGET_SO_RCVBUF:
		optname = SO_RCVBUF;
		break;
        case TARGET_SO_RCVBUFFORCE:
                optname = SO_RCVBUFFORCE;
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
        case TARGET_SO_PASSSEC:
                optname = SO_PASSSEC;
                break;
        case TARGET_SO_TIMESTAMP:
		optname = SO_TIMESTAMP;
		break;
        case TARGET_SO_RCVLOWAT:
		optname = SO_RCVLOWAT;
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
        gemu_log("Unsupported setsockopt level=%d optname=%d\n", level, optname);
        ret = -TARGET_ENOPROTOOPT;
    }
    return ret;
}

/* do_getsockopt() Must return target values and target errnos. */
static abi_long do_getsockopt(int sockfd, int level, int optname,
                              abi_ulong optval_addr, abi_ulong optlen)
{
    abi_long ret;
    int len, val;
    socklen_t lv;

    switch(level) {
    case TARGET_SOL_SOCKET:
        level = SOL_SOCKET;
        switch (optname) {
        /* These don't just return a single integer */
        case TARGET_SO_LINGER:
        case TARGET_SO_RCVTIMEO:
        case TARGET_SO_SNDTIMEO:
        case TARGET_SO_PEERNAME:
            goto unimplemented;
        case TARGET_SO_PEERCRED: {
            struct ucred cr;
            socklen_t crlen;
            struct target_ucred *tcr;

            if (get_user_u32(len, optlen)) {
                return -TARGET_EFAULT;
            }
            if (len < 0) {
                return -TARGET_EINVAL;
            }

            crlen = sizeof(cr);
            ret = get_errno(getsockopt(sockfd, level, SO_PEERCRED,
                                       &cr, &crlen));
            if (ret < 0) {
                return ret;
            }
            if (len > crlen) {
                len = crlen;
            }
            if (!lock_user_struct(VERIFY_WRITE, tcr, optval_addr, 0)) {
                return -TARGET_EFAULT;
            }
            __put_user(cr.pid, &tcr->pid);
            __put_user(cr.uid, &tcr->uid);
            __put_user(cr.gid, &tcr->gid);
            unlock_user_struct(tcr, optval_addr, 1);
            if (put_user_u32(len, optlen)) {
                return -TARGET_EFAULT;
            }
            break;
        }
        /* Options with 'int' argument.  */
        case TARGET_SO_DEBUG:
            optname = SO_DEBUG;
            goto int_case;
        case TARGET_SO_REUSEADDR:
            optname = SO_REUSEADDR;
            goto int_case;
        case TARGET_SO_TYPE:
            optname = SO_TYPE;
            goto int_case;
        case TARGET_SO_ERROR:
            optname = SO_ERROR;
            goto int_case;
        case TARGET_SO_DONTROUTE:
            optname = SO_DONTROUTE;
            goto int_case;
        case TARGET_SO_BROADCAST:
            optname = SO_BROADCAST;
            goto int_case;
        case TARGET_SO_SNDBUF:
            optname = SO_SNDBUF;
            goto int_case;
        case TARGET_SO_RCVBUF:
            optname = SO_RCVBUF;
            goto int_case;
        case TARGET_SO_KEEPALIVE:
            optname = SO_KEEPALIVE;
            goto int_case;
        case TARGET_SO_OOBINLINE:
            optname = SO_OOBINLINE;
            goto int_case;
        case TARGET_SO_NO_CHECK:
            optname = SO_NO_CHECK;
            goto int_case;
        case TARGET_SO_PRIORITY:
            optname = SO_PRIORITY;
            goto int_case;
#ifdef SO_BSDCOMPAT
        case TARGET_SO_BSDCOMPAT:
            optname = SO_BSDCOMPAT;
            goto int_case;
#endif
        case TARGET_SO_PASSCRED:
            optname = SO_PASSCRED;
            goto int_case;
        case TARGET_SO_TIMESTAMP:
            optname = SO_TIMESTAMP;
            goto int_case;
        case TARGET_SO_RCVLOWAT:
            optname = SO_RCVLOWAT;
            goto int_case;
        case TARGET_SO_ACCEPTCONN:
            optname = SO_ACCEPTCONN;
            goto int_case;
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
        lv = sizeof(lv);
        ret = get_errno(getsockopt(sockfd, level, optname, &val, &lv));
        if (ret < 0)
            return ret;
        if (optname == SO_TYPE) {
            val = host_to_target_sock_type(val);
        }
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
            lv = sizeof(lv);
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

static struct iovec *lock_iovec(int type, abi_ulong target_addr,
                                int count, int copy)
{
    struct target_iovec *target_vec;
    struct iovec *vec;
    abi_ulong total_len, max_len;
    int i;
    int err = 0;

    if (count == 0) {
        errno = 0;
        return NULL;
    }
    if (count < 0 || count > IOV_MAX) {
        errno = EINVAL;
        return NULL;
    }

    vec = calloc(count, sizeof(struct iovec));
    if (vec == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    target_vec = lock_user(VERIFY_READ, target_addr,
                           count * sizeof(struct target_iovec), 1);
    if (target_vec == NULL) {
        err = EFAULT;
        goto fail2;
    }

    /* ??? If host page size > target page size, this will result in a
       value larger than what we can actually support.  */
    max_len = 0x7fffffff & TARGET_PAGE_MASK;
    total_len = 0;

    for (i = 0; i < count; i++) {
        abi_ulong base = tswapal(target_vec[i].iov_base);
        abi_long len = tswapal(target_vec[i].iov_len);

        if (len < 0) {
            err = EINVAL;
            goto fail;
        } else if (len == 0) {
            /* Zero length pointer is ignored.  */
            vec[i].iov_base = 0;
        } else {
            vec[i].iov_base = lock_user(type, base, len, copy);
            if (!vec[i].iov_base) {
                err = EFAULT;
                goto fail;
            }
            if (len > max_len - total_len) {
                len = max_len - total_len;
            }
        }
        vec[i].iov_len = len;
        total_len += len;
    }

    unlock_user(target_vec, target_addr, 0);
    return vec;

 fail:
    unlock_user(target_vec, target_addr, 0);
 fail2:
    free(vec);
    errno = err;
    return NULL;
}

static void unlock_iovec(struct iovec *vec, abi_ulong target_addr,
                         int count, int copy)
{
    struct target_iovec *target_vec;
    int i;

    target_vec = lock_user(VERIFY_READ, target_addr,
                           count * sizeof(struct target_iovec), 1);
    if (target_vec) {
        for (i = 0; i < count; i++) {
            abi_ulong base = tswapal(target_vec[i].iov_base);
            abi_long len = tswapal(target_vec[i].iov_base);
            if (len < 0) {
                break;
            }
            unlock_user(vec[i].iov_base, base, copy ? vec[i].iov_len : 0);
        }
        unlock_user(target_vec, target_addr, 0);
    }

    free(vec);
}

static inline int target_to_host_sock_type(int *type)
{
    int host_type = 0;
    int target_type = *type;

    switch (target_type & TARGET_SOCK_TYPE_MASK) {
    case TARGET_SOCK_DGRAM:
        host_type = SOCK_DGRAM;
        break;
    case TARGET_SOCK_STREAM:
        host_type = SOCK_STREAM;
        break;
    default:
        host_type = target_type & TARGET_SOCK_TYPE_MASK;
        break;
    }
    if (target_type & TARGET_SOCK_CLOEXEC) {
#if defined(SOCK_CLOEXEC)
        host_type |= SOCK_CLOEXEC;
#else
        return -TARGET_EINVAL;
#endif
    }
    if (target_type & TARGET_SOCK_NONBLOCK) {
#if defined(SOCK_NONBLOCK)
        host_type |= SOCK_NONBLOCK;
#elif !defined(O_NONBLOCK)
        return -TARGET_EINVAL;
#endif
    }
    *type = host_type;
    return 0;
}

/* Try to emulate socket type flags after socket creation.  */
static int sock_flags_fixup(int fd, int target_type)
{
#if !defined(SOCK_NONBLOCK) && defined(O_NONBLOCK)
    if (target_type & TARGET_SOCK_NONBLOCK) {
        int flags = fcntl(fd, F_GETFL);
        if (fcntl(fd, F_SETFL, O_NONBLOCK | flags) == -1) {
            close(fd);
            return -TARGET_EINVAL;
        }
    }
#endif
    return fd;
}

/* do_socket() Must return target values and target errnos. */
static abi_long do_socket(int domain, int type, int protocol)
{
    int target_type = type;
    int ret;

    ret = target_to_host_sock_type(&type);
    if (ret) {
        return ret;
    }

    if (domain == PF_NETLINK)
        return -TARGET_EAFNOSUPPORT;
    ret = get_errno(socket(domain, type, protocol));
    if (ret >= 0) {
        ret = sock_flags_fixup(ret, target_type);
    }
    return ret;
}

/* do_bind() Must return target values and target errnos. */
static abi_long do_bind(int sockfd, abi_ulong target_addr,
                        socklen_t addrlen)
{
    void *addr;
    abi_long ret;

    if ((int)addrlen < 0) {
        return -TARGET_EINVAL;
    }

    addr = alloca(addrlen+1);

    ret = target_to_host_sockaddr(addr, target_addr, addrlen);
    if (ret)
        return ret;

    return get_errno(bind(sockfd, addr, addrlen));
}

/* do_connect() Must return target values and target errnos. */
static abi_long do_connect(int sockfd, abi_ulong target_addr,
                           socklen_t addrlen)
{
    void *addr;
    abi_long ret;

    if ((int)addrlen < 0) {
        return -TARGET_EINVAL;
    }

    addr = alloca(addrlen+1);

    ret = target_to_host_sockaddr(addr, target_addr, addrlen);
    if (ret)
        return ret;

    return get_errno(connect(sockfd, addr, addrlen));
}

/* do_sendrecvmsg_locked() Must return target values and target errnos. */
static abi_long do_sendrecvmsg_locked(int fd, struct target_msghdr *msgp,
                                      int flags, int send)
{
    abi_long ret, len;
    struct msghdr msg;
    int count;
    struct iovec *vec;
    abi_ulong target_vec;

    if (msgp->msg_name) {
        msg.msg_namelen = tswap32(msgp->msg_namelen);
        msg.msg_name = alloca(msg.msg_namelen+1);
        ret = target_to_host_sockaddr(msg.msg_name, tswapal(msgp->msg_name),
                                msg.msg_namelen);
        if (ret) {
            goto out2;
        }
    } else {
        msg.msg_name = NULL;
        msg.msg_namelen = 0;
    }
    msg.msg_controllen = 2 * tswapal(msgp->msg_controllen);
    msg.msg_control = alloca(msg.msg_controllen);
    msg.msg_flags = tswap32(msgp->msg_flags);

    count = tswapal(msgp->msg_iovlen);
    target_vec = tswapal(msgp->msg_iov);
    vec = lock_iovec(send ? VERIFY_READ : VERIFY_WRITE,
                     target_vec, count, send);
    if (vec == NULL) {
        ret = -host_to_target_errno(errno);
        goto out2;
    }
    msg.msg_iovlen = count;
    msg.msg_iov = vec;

    if (send) {
        ret = target_to_host_cmsg(&msg, msgp);
        if (ret == 0)
            ret = get_errno(sendmsg(fd, &msg, flags));
    } else {
        ret = get_errno(recvmsg(fd, &msg, flags));
        if (!is_error(ret)) {
            len = ret;
            ret = host_to_target_cmsg(msgp, &msg);
            if (!is_error(ret)) {
                msgp->msg_namelen = tswap32(msg.msg_namelen);
                if (msg.msg_name != NULL) {
                    ret = host_to_target_sockaddr(tswapal(msgp->msg_name),
                                    msg.msg_name, msg.msg_namelen);
                    if (ret) {
                        goto out;
                    }
                }

                ret = len;
            }
        }
    }

out:
    unlock_iovec(vec, target_vec, count, !send);
out2:
    return ret;
}

static abi_long do_sendrecvmsg(int fd, abi_ulong target_msg,
                               int flags, int send)
{
    abi_long ret;
    struct target_msghdr *msgp;

    if (!lock_user_struct(send ? VERIFY_READ : VERIFY_WRITE,
                          msgp,
                          target_msg,
                          send ? 1 : 0)) {
        return -TARGET_EFAULT;
    }
    ret = do_sendrecvmsg_locked(fd, msgp, flags, send);
    unlock_user_struct(msgp, target_msg, send ? 0 : 1);
    return ret;
}

#ifdef TARGET_NR_sendmmsg
/* We don't rely on the C library to have sendmmsg/recvmmsg support,
 * so it might not have this *mmsg-specific flag either.
 */
#ifndef MSG_WAITFORONE
#define MSG_WAITFORONE 0x10000
#endif

static abi_long do_sendrecvmmsg(int fd, abi_ulong target_msgvec,
                                unsigned int vlen, unsigned int flags,
                                int send)
{
    struct target_mmsghdr *mmsgp;
    abi_long ret = 0;
    int i;

    if (vlen > UIO_MAXIOV) {
        vlen = UIO_MAXIOV;
    }

    mmsgp = lock_user(VERIFY_WRITE, target_msgvec, sizeof(*mmsgp) * vlen, 1);
    if (!mmsgp) {
        return -TARGET_EFAULT;
    }

    for (i = 0; i < vlen; i++) {
        ret = do_sendrecvmsg_locked(fd, &mmsgp[i].msg_hdr, flags, send);
        if (is_error(ret)) {
            break;
        }
        mmsgp[i].msg_len = tswap32(ret);
        /* MSG_WAITFORONE turns on MSG_DONTWAIT after one packet */
        if (flags & MSG_WAITFORONE) {
            flags |= MSG_DONTWAIT;
        }
    }

    unlock_user(mmsgp, target_msgvec, sizeof(*mmsgp) * i);

    /* Return number of datagrams sent if we sent any at all;
     * otherwise return the error.
     */
    if (i) {
        return i;
    }
    return ret;
}
#endif

/* If we don't have a system accept4() then just call accept.
 * The callsites to do_accept4() will ensure that they don't
 * pass a non-zero flags argument in this config.
 */
#ifndef CONFIG_ACCEPT4
static inline int accept4(int sockfd, struct sockaddr *addr,
                          socklen_t *addrlen, int flags)
{
    assert(flags == 0);
    return accept(sockfd, addr, addrlen);
}
#endif

/* do_accept4() Must return target values and target errnos. */
static abi_long do_accept4(int fd, abi_ulong target_addr,
                           abi_ulong target_addrlen_addr, int flags)
{
    socklen_t addrlen;
    void *addr;
    abi_long ret;
    int host_flags;

    host_flags = target_to_host_bitmask(flags, fcntl_flags_tbl);

    if (target_addr == 0) {
        return get_errno(accept4(fd, NULL, NULL, host_flags));
    }

    /* linux returns EINVAL if addrlen pointer is invalid */
    if (get_user_u32(addrlen, target_addrlen_addr))
        return -TARGET_EINVAL;

    if ((int)addrlen < 0) {
        return -TARGET_EINVAL;
    }

    if (!access_ok(VERIFY_WRITE, target_addr, addrlen))
        return -TARGET_EINVAL;

    addr = alloca(addrlen);

    ret = get_errno(accept4(fd, addr, &addrlen, host_flags));
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

    if ((int)addrlen < 0) {
        return -TARGET_EINVAL;
    }

    if (!access_ok(VERIFY_WRITE, target_addr, addrlen))
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

    if ((int)addrlen < 0) {
        return -TARGET_EINVAL;
    }

    if (!access_ok(VERIFY_WRITE, target_addr, addrlen))
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

    target_to_host_sock_type(&type);

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

    if ((int)addrlen < 0) {
        return -TARGET_EINVAL;
    }

    host_msg = lock_user(VERIFY_READ, msg, len, 1);
    if (!host_msg)
        return -TARGET_EFAULT;
    if (target_addr) {
        addr = alloca(addrlen+1);
        ret = target_to_host_sockaddr(addr, target_addr, addrlen);
        if (ret) {
            unlock_user(host_msg, msg, 0);
            return ret;
        }
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
        if ((int)addrlen < 0) {
            ret = -TARGET_EINVAL;
            goto fail;
        }
        addr = alloca(addrlen);
        ret = get_errno(recvfrom(fd, host_msg, len, flags, addr, &addrlen));
    } else {
        addr = NULL; /* To keep compiler quiet.  */
        ret = get_errno(qemu_recv(fd, host_msg, len, flags));
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
    static const unsigned ac[] = { /* number of arguments per call */
        [SOCKOP_socket] = 3,      /* domain, type, protocol */
        [SOCKOP_bind] = 3,        /* sockfd, addr, addrlen */
        [SOCKOP_connect] = 3,     /* sockfd, addr, addrlen */
        [SOCKOP_listen] = 2,      /* sockfd, backlog */
        [SOCKOP_accept] = 3,      /* sockfd, addr, addrlen */
        [SOCKOP_accept4] = 4,     /* sockfd, addr, addrlen, flags */
        [SOCKOP_getsockname] = 3, /* sockfd, addr, addrlen */
        [SOCKOP_getpeername] = 3, /* sockfd, addr, addrlen */
        [SOCKOP_socketpair] = 4,  /* domain, type, protocol, tab */
        [SOCKOP_send] = 4,        /* sockfd, msg, len, flags */
        [SOCKOP_recv] = 4,        /* sockfd, msg, len, flags */
        [SOCKOP_sendto] = 6,      /* sockfd, msg, len, flags, addr, addrlen */
        [SOCKOP_recvfrom] = 6,    /* sockfd, msg, len, flags, addr, addrlen */
        [SOCKOP_shutdown] = 2,    /* sockfd, how */
        [SOCKOP_sendmsg] = 3,     /* sockfd, msg, flags */
        [SOCKOP_recvmsg] = 3,     /* sockfd, msg, flags */
        [SOCKOP_setsockopt] = 5,  /* sockfd, level, optname, optval, optlen */
        [SOCKOP_getsockopt] = 5,  /* sockfd, level, optname, optval, optlen */
    };
    abi_long a[6]; /* max 6 args */

    /* first, collect the arguments in a[] according to ac[] */
    if (num >= 0 && num < ARRAY_SIZE(ac)) {
        unsigned i;
        assert(ARRAY_SIZE(a) >= ac[num]); /* ensure we have space for args */
        for (i = 0; i < ac[num]; ++i) {
            if (get_user_ual(a[i], vptr + i * sizeof(abi_long)) != 0) {
                return -TARGET_EFAULT;
            }
        }
    }

    /* now when we have the args, actually handle the call */
    switch (num) {
    case SOCKOP_socket: /* domain, type, protocol */
        return do_socket(a[0], a[1], a[2]);
    case SOCKOP_bind: /* sockfd, addr, addrlen */
        return do_bind(a[0], a[1], a[2]);
    case SOCKOP_connect: /* sockfd, addr, addrlen */
        return do_connect(a[0], a[1], a[2]);
    case SOCKOP_listen: /* sockfd, backlog */
        return get_errno(listen(a[0], a[1]));
    case SOCKOP_accept: /* sockfd, addr, addrlen */
        return do_accept4(a[0], a[1], a[2], 0);
    case SOCKOP_accept4: /* sockfd, addr, addrlen, flags */
        return do_accept4(a[0], a[1], a[2], a[3]);
    case SOCKOP_getsockname: /* sockfd, addr, addrlen */
        return do_getsockname(a[0], a[1], a[2]);
    case SOCKOP_getpeername: /* sockfd, addr, addrlen */
        return do_getpeername(a[0], a[1], a[2]);
    case SOCKOP_socketpair: /* domain, type, protocol, tab */
        return do_socketpair(a[0], a[1], a[2], a[3]);
    case SOCKOP_send: /* sockfd, msg, len, flags */
        return do_sendto(a[0], a[1], a[2], a[3], 0, 0);
    case SOCKOP_recv: /* sockfd, msg, len, flags */
        return do_recvfrom(a[0], a[1], a[2], a[3], 0, 0);
    case SOCKOP_sendto: /* sockfd, msg, len, flags, addr, addrlen */
        return do_sendto(a[0], a[1], a[2], a[3], a[4], a[5]);
    case SOCKOP_recvfrom: /* sockfd, msg, len, flags, addr, addrlen */
        return do_recvfrom(a[0], a[1], a[2], a[3], a[4], a[5]);
    case SOCKOP_shutdown: /* sockfd, how */
        return get_errno(shutdown(a[0], a[1]));
    case SOCKOP_sendmsg: /* sockfd, msg, flags */
        return do_sendrecvmsg(a[0], a[1], a[2], 1);
    case SOCKOP_recvmsg: /* sockfd, msg, flags */
        return do_sendrecvmsg(a[0], a[1], a[2], 0);
    case SOCKOP_setsockopt: /* sockfd, level, optname, optval, optlen */
        return do_setsockopt(a[0], a[1], a[2], a[3], a[4]);
    case SOCKOP_getsockopt: /* sockfd, level, optname, optval, optlen */
        return do_getsockopt(a[0], a[1], a[2], a[3], a[4]);
    default:
        gemu_log("Unsupported socketcall: %d\n", num);
        return -TARGET_ENOSYS;
    }
}
#endif

#define N_SHM_REGIONS	32

static struct shm_region {
    abi_ulong	start;
    abi_ulong	size;
} shm_regions[N_SHM_REGIONS];

struct target_semid_ds
{
  struct target_ipc_perm sem_perm;
  abi_ulong sem_otime;
#if !defined(TARGET_PPC64)
  abi_ulong __unused1;
#endif
  abi_ulong sem_ctime;
#if !defined(TARGET_PPC64)
  abi_ulong __unused2;
#endif
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
    target_ip = &(target_sd->sem_perm);
    host_ip->__key = tswap32(target_ip->__key);
    host_ip->uid = tswap32(target_ip->uid);
    host_ip->gid = tswap32(target_ip->gid);
    host_ip->cuid = tswap32(target_ip->cuid);
    host_ip->cgid = tswap32(target_ip->cgid);
#if defined(TARGET_ALPHA) || defined(TARGET_MIPS) || defined(TARGET_PPC)
    host_ip->mode = tswap32(target_ip->mode);
#else
    host_ip->mode = tswap16(target_ip->mode);
#endif
#if defined(TARGET_PPC)
    host_ip->__seq = tswap32(target_ip->__seq);
#else
    host_ip->__seq = tswap16(target_ip->__seq);
#endif
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
    target_ip->__key = tswap32(host_ip->__key);
    target_ip->uid = tswap32(host_ip->uid);
    target_ip->gid = tswap32(host_ip->gid);
    target_ip->cuid = tswap32(host_ip->cuid);
    target_ip->cgid = tswap32(host_ip->cgid);
#if defined(TARGET_ALPHA) || defined(TARGET_MIPS) || defined(TARGET_PPC)
    target_ip->mode = tswap32(host_ip->mode);
#else
    target_ip->mode = tswap16(host_ip->mode);
#endif
#if defined(TARGET_PPC)
    target_ip->__seq = tswap32(host_ip->__seq);
#else
    target_ip->__seq = tswap16(host_ip->__seq);
#endif
    unlock_user_struct(target_sd, target_addr, 1);
    return 0;
}

static inline abi_long target_to_host_semid_ds(struct semid_ds *host_sd,
                                               abi_ulong target_addr)
{
    struct target_semid_ds *target_sd;

    if (!lock_user_struct(VERIFY_READ, target_sd, target_addr, 1))
        return -TARGET_EFAULT;
    if (target_to_host_ipc_perm(&(host_sd->sem_perm),target_addr))
        return -TARGET_EFAULT;
    host_sd->sem_nsems = tswapal(target_sd->sem_nsems);
    host_sd->sem_otime = tswapal(target_sd->sem_otime);
    host_sd->sem_ctime = tswapal(target_sd->sem_ctime);
    unlock_user_struct(target_sd, target_addr, 0);
    return 0;
}

static inline abi_long host_to_target_semid_ds(abi_ulong target_addr,
                                               struct semid_ds *host_sd)
{
    struct target_semid_ds *target_sd;

    if (!lock_user_struct(VERIFY_WRITE, target_sd, target_addr, 0))
        return -TARGET_EFAULT;
    if (host_to_target_ipc_perm(target_addr,&(host_sd->sem_perm)))
        return -TARGET_EFAULT;
    target_sd->sem_nsems = tswapal(host_sd->sem_nsems);
    target_sd->sem_otime = tswapal(host_sd->sem_otime);
    target_sd->sem_ctime = tswapal(host_sd->sem_ctime);
    unlock_user_struct(target_sd, target_addr, 1);
    return 0;
}

struct target_seminfo {
    int semmap;
    int semmni;
    int semmns;
    int semmnu;
    int semmsl;
    int semopm;
    int semume;
    int semusz;
    int semvmx;
    int semaem;
};

static inline abi_long host_to_target_seminfo(abi_ulong target_addr,
                                              struct seminfo *host_seminfo)
{
    struct target_seminfo *target_seminfo;
    if (!lock_user_struct(VERIFY_WRITE, target_seminfo, target_addr, 0))
        return -TARGET_EFAULT;
    __put_user(host_seminfo->semmap, &target_seminfo->semmap);
    __put_user(host_seminfo->semmni, &target_seminfo->semmni);
    __put_user(host_seminfo->semmns, &target_seminfo->semmns);
    __put_user(host_seminfo->semmnu, &target_seminfo->semmnu);
    __put_user(host_seminfo->semmsl, &target_seminfo->semmsl);
    __put_user(host_seminfo->semopm, &target_seminfo->semopm);
    __put_user(host_seminfo->semume, &target_seminfo->semume);
    __put_user(host_seminfo->semusz, &target_seminfo->semusz);
    __put_user(host_seminfo->semvmx, &target_seminfo->semvmx);
    __put_user(host_seminfo->semaem, &target_seminfo->semaem);
    unlock_user_struct(target_seminfo, target_addr, 1);
    return 0;
}

union semun {
	int val;
	struct semid_ds *buf;
	unsigned short *array;
	struct seminfo *__buf;
};

union target_semun {
	int val;
	abi_ulong buf;
	abi_ulong array;
	abi_ulong __buf;
};

static inline abi_long target_to_host_semarray(int semid, unsigned short **host_array,
                                               abi_ulong target_addr)
{
    int nsems;
    unsigned short *array;
    union semun semun;
    struct semid_ds semid_ds;
    int i, ret;

    semun.buf = &semid_ds;

    ret = semctl(semid, 0, IPC_STAT, semun);
    if (ret == -1)
        return get_errno(ret);

    nsems = semid_ds.sem_nsems;

    *host_array = malloc(nsems*sizeof(unsigned short));
    if (!*host_array) {
        return -TARGET_ENOMEM;
    }
    array = lock_user(VERIFY_READ, target_addr,
                      nsems*sizeof(unsigned short), 1);
    if (!array) {
        free(*host_array);
        return -TARGET_EFAULT;
    }

    for(i=0; i<nsems; i++) {
        __get_user((*host_array)[i], &array[i]);
    }
    unlock_user(array, target_addr, 0);

    return 0;
}

static inline abi_long host_to_target_semarray(int semid, abi_ulong target_addr,
                                               unsigned short **host_array)
{
    int nsems;
    unsigned short *array;
    union semun semun;
    struct semid_ds semid_ds;
    int i, ret;

    semun.buf = &semid_ds;

    ret = semctl(semid, 0, IPC_STAT, semun);
    if (ret == -1)
        return get_errno(ret);

    nsems = semid_ds.sem_nsems;

    array = lock_user(VERIFY_WRITE, target_addr,
                      nsems*sizeof(unsigned short), 0);
    if (!array)
        return -TARGET_EFAULT;

    for(i=0; i<nsems; i++) {
        __put_user((*host_array)[i], &array[i]);
    }
    free(*host_array);
    unlock_user(array, target_addr, 1);

    return 0;
}

static inline abi_long do_semctl(int semid, int semnum, int cmd,
                                 union target_semun target_su)
{
    union semun arg;
    struct semid_ds dsarg;
    unsigned short *array = NULL;
    struct seminfo seminfo;
    abi_long ret = -TARGET_EINVAL;
    abi_long err;
    cmd &= 0xff;

    switch( cmd ) {
	case GETVAL:
	case SETVAL:
            /* In 64 bit cross-endian situations, we will erroneously pick up
             * the wrong half of the union for the "val" element.  To rectify
             * this, the entire 8-byte structure is byteswapped, followed by
	     * a swap of the 4 byte val field. In other cases, the data is
	     * already in proper host byte order. */
	    if (sizeof(target_su.val) != (sizeof(target_su.buf))) {
		target_su.buf = tswapal(target_su.buf);
		arg.val = tswap32(target_su.val);
	    } else {
		arg.val = target_su.val;
	    }
            ret = get_errno(semctl(semid, semnum, cmd, arg));
            break;
	case GETALL:
	case SETALL:
            err = target_to_host_semarray(semid, &array, target_su.array);
            if (err)
                return err;
            arg.array = array;
            ret = get_errno(semctl(semid, semnum, cmd, arg));
            err = host_to_target_semarray(semid, target_su.array, &array);
            if (err)
                return err;
            break;
	case IPC_STAT:
	case IPC_SET:
	case SEM_STAT:
            err = target_to_host_semid_ds(&dsarg, target_su.buf);
            if (err)
                return err;
            arg.buf = &dsarg;
            ret = get_errno(semctl(semid, semnum, cmd, arg));
            err = host_to_target_semid_ds(target_su.buf, &dsarg);
            if (err)
                return err;
            break;
	case IPC_INFO:
	case SEM_INFO:
            arg.__buf = &seminfo;
            ret = get_errno(semctl(semid, semnum, cmd, arg));
            err = host_to_target_seminfo(target_su.__buf, &seminfo);
            if (err)
                return err;
            break;
	case IPC_RMID:
	case GETPID:
	case GETNCNT:
	case GETZCNT:
            ret = get_errno(semctl(semid, semnum, cmd, NULL));
            break;
    }

    return ret;
}

struct target_sembuf {
    unsigned short sem_num;
    short sem_op;
    short sem_flg;
};

static inline abi_long target_to_host_sembuf(struct sembuf *host_sembuf,
                                             abi_ulong target_addr,
                                             unsigned nsops)
{
    struct target_sembuf *target_sembuf;
    int i;

    target_sembuf = lock_user(VERIFY_READ, target_addr,
                              nsops*sizeof(struct target_sembuf), 1);
    if (!target_sembuf)
        return -TARGET_EFAULT;

    for(i=0; i<nsops; i++) {
        __get_user(host_sembuf[i].sem_num, &target_sembuf[i].sem_num);
        __get_user(host_sembuf[i].sem_op, &target_sembuf[i].sem_op);
        __get_user(host_sembuf[i].sem_flg, &target_sembuf[i].sem_flg);
    }

    unlock_user(target_sembuf, target_addr, 0);

    return 0;
}

static inline abi_long do_semop(int semid, abi_long ptr, unsigned nsops)
{
    struct sembuf sops[nsops];

    if (target_to_host_sembuf(sops, ptr, nsops))
        return -TARGET_EFAULT;

    return get_errno(semop(semid, sops, nsops));
}

struct target_msqid_ds
{
    struct target_ipc_perm msg_perm;
    abi_ulong msg_stime;
#if TARGET_ABI_BITS == 32
    abi_ulong __unused1;
#endif
    abi_ulong msg_rtime;
#if TARGET_ABI_BITS == 32
    abi_ulong __unused2;
#endif
    abi_ulong msg_ctime;
#if TARGET_ABI_BITS == 32
    abi_ulong __unused3;
#endif
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
    if (target_to_host_ipc_perm(&(host_md->msg_perm),target_addr))
        return -TARGET_EFAULT;
    host_md->msg_stime = tswapal(target_md->msg_stime);
    host_md->msg_rtime = tswapal(target_md->msg_rtime);
    host_md->msg_ctime = tswapal(target_md->msg_ctime);
    host_md->__msg_cbytes = tswapal(target_md->__msg_cbytes);
    host_md->msg_qnum = tswapal(target_md->msg_qnum);
    host_md->msg_qbytes = tswapal(target_md->msg_qbytes);
    host_md->msg_lspid = tswapal(target_md->msg_lspid);
    host_md->msg_lrpid = tswapal(target_md->msg_lrpid);
    unlock_user_struct(target_md, target_addr, 0);
    return 0;
}

static inline abi_long host_to_target_msqid_ds(abi_ulong target_addr,
                                               struct msqid_ds *host_md)
{
    struct target_msqid_ds *target_md;

    if (!lock_user_struct(VERIFY_WRITE, target_md, target_addr, 0))
        return -TARGET_EFAULT;
    if (host_to_target_ipc_perm(target_addr,&(host_md->msg_perm)))
        return -TARGET_EFAULT;
    target_md->msg_stime = tswapal(host_md->msg_stime);
    target_md->msg_rtime = tswapal(host_md->msg_rtime);
    target_md->msg_ctime = tswapal(host_md->msg_ctime);
    target_md->__msg_cbytes = tswapal(host_md->__msg_cbytes);
    target_md->msg_qnum = tswapal(host_md->msg_qnum);
    target_md->msg_qbytes = tswapal(host_md->msg_qbytes);
    target_md->msg_lspid = tswapal(host_md->msg_lspid);
    target_md->msg_lrpid = tswapal(host_md->msg_lrpid);
    unlock_user_struct(target_md, target_addr, 1);
    return 0;
}

struct target_msginfo {
    int msgpool;
    int msgmap;
    int msgmax;
    int msgmnb;
    int msgmni;
    int msgssz;
    int msgtql;
    unsigned short int msgseg;
};

static inline abi_long host_to_target_msginfo(abi_ulong target_addr,
                                              struct msginfo *host_msginfo)
{
    struct target_msginfo *target_msginfo;
    if (!lock_user_struct(VERIFY_WRITE, target_msginfo, target_addr, 0))
        return -TARGET_EFAULT;
    __put_user(host_msginfo->msgpool, &target_msginfo->msgpool);
    __put_user(host_msginfo->msgmap, &target_msginfo->msgmap);
    __put_user(host_msginfo->msgmax, &target_msginfo->msgmax);
    __put_user(host_msginfo->msgmnb, &target_msginfo->msgmnb);
    __put_user(host_msginfo->msgmni, &target_msginfo->msgmni);
    __put_user(host_msginfo->msgssz, &target_msginfo->msgssz);
    __put_user(host_msginfo->msgtql, &target_msginfo->msgtql);
    __put_user(host_msginfo->msgseg, &target_msginfo->msgseg);
    unlock_user_struct(target_msginfo, target_addr, 1);
    return 0;
}

static inline abi_long do_msgctl(int msgid, int cmd, abi_long ptr)
{
    struct msqid_ds dsarg;
    struct msginfo msginfo;
    abi_long ret = -TARGET_EINVAL;

    cmd &= 0xff;

    switch (cmd) {
    case IPC_STAT:
    case IPC_SET:
    case MSG_STAT:
        if (target_to_host_msqid_ds(&dsarg,ptr))
            return -TARGET_EFAULT;
        ret = get_errno(msgctl(msgid, cmd, &dsarg));
        if (host_to_target_msqid_ds(ptr,&dsarg))
            return -TARGET_EFAULT;
        break;
    case IPC_RMID:
        ret = get_errno(msgctl(msgid, cmd, NULL));
        break;
    case IPC_INFO:
    case MSG_INFO:
        ret = get_errno(msgctl(msgid, cmd, (struct msqid_ds *)&msginfo));
        if (host_to_target_msginfo(ptr, &msginfo))
            return -TARGET_EFAULT;
        break;
    }

    return ret;
}

struct target_msgbuf {
    abi_long mtype;
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
    host_mb->mtype = (abi_long) tswapal(target_mb->mtype);
    memcpy(host_mb->mtext, target_mb->mtext, msgsz);
    ret = get_errno(msgsnd(msqid, host_mb, msgsz, msgflg));
    free(host_mb);
    unlock_user_struct(target_mb, msgp, 0);

    return ret;
}

static inline abi_long do_msgrcv(int msqid, abi_long msgp,
                                 unsigned int msgsz, abi_long msgtyp,
                                 int msgflg)
{
    struct target_msgbuf *target_mb;
    char *target_mtext;
    struct msgbuf *host_mb;
    abi_long ret = 0;

    if (!lock_user_struct(VERIFY_WRITE, target_mb, msgp, 0))
        return -TARGET_EFAULT;

    host_mb = g_malloc(msgsz+sizeof(long));
    ret = get_errno(msgrcv(msqid, host_mb, msgsz, msgtyp, msgflg));

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

    target_mb->mtype = tswapal(host_mb->mtype);

end:
    if (target_mb)
        unlock_user_struct(target_mb, msgp, 1);
    g_free(host_mb);
    return ret;
}

static inline abi_long target_to_host_shmid_ds(struct shmid_ds *host_sd,
                                               abi_ulong target_addr)
{
    struct target_shmid_ds *target_sd;

    if (!lock_user_struct(VERIFY_READ, target_sd, target_addr, 1))
        return -TARGET_EFAULT;
    if (target_to_host_ipc_perm(&(host_sd->shm_perm), target_addr))
        return -TARGET_EFAULT;
    __get_user(host_sd->shm_segsz, &target_sd->shm_segsz);
    __get_user(host_sd->shm_atime, &target_sd->shm_atime);
    __get_user(host_sd->shm_dtime, &target_sd->shm_dtime);
    __get_user(host_sd->shm_ctime, &target_sd->shm_ctime);
    __get_user(host_sd->shm_cpid, &target_sd->shm_cpid);
    __get_user(host_sd->shm_lpid, &target_sd->shm_lpid);
    __get_user(host_sd->shm_nattch, &target_sd->shm_nattch);
    unlock_user_struct(target_sd, target_addr, 0);
    return 0;
}

static inline abi_long host_to_target_shmid_ds(abi_ulong target_addr,
                                               struct shmid_ds *host_sd)
{
    struct target_shmid_ds *target_sd;

    if (!lock_user_struct(VERIFY_WRITE, target_sd, target_addr, 0))
        return -TARGET_EFAULT;
    if (host_to_target_ipc_perm(target_addr, &(host_sd->shm_perm)))
        return -TARGET_EFAULT;
    __put_user(host_sd->shm_segsz, &target_sd->shm_segsz);
    __put_user(host_sd->shm_atime, &target_sd->shm_atime);
    __put_user(host_sd->shm_dtime, &target_sd->shm_dtime);
    __put_user(host_sd->shm_ctime, &target_sd->shm_ctime);
    __put_user(host_sd->shm_cpid, &target_sd->shm_cpid);
    __put_user(host_sd->shm_lpid, &target_sd->shm_lpid);
    __put_user(host_sd->shm_nattch, &target_sd->shm_nattch);
    unlock_user_struct(target_sd, target_addr, 1);
    return 0;
}

struct  target_shminfo {
    abi_ulong shmmax;
    abi_ulong shmmin;
    abi_ulong shmmni;
    abi_ulong shmseg;
    abi_ulong shmall;
};

static inline abi_long host_to_target_shminfo(abi_ulong target_addr,
                                              struct shminfo *host_shminfo)
{
    struct target_shminfo *target_shminfo;
    if (!lock_user_struct(VERIFY_WRITE, target_shminfo, target_addr, 0))
        return -TARGET_EFAULT;
    __put_user(host_shminfo->shmmax, &target_shminfo->shmmax);
    __put_user(host_shminfo->shmmin, &target_shminfo->shmmin);
    __put_user(host_shminfo->shmmni, &target_shminfo->shmmni);
    __put_user(host_shminfo->shmseg, &target_shminfo->shmseg);
    __put_user(host_shminfo->shmall, &target_shminfo->shmall);
    unlock_user_struct(target_shminfo, target_addr, 1);
    return 0;
}

struct target_shm_info {
    int used_ids;
    abi_ulong shm_tot;
    abi_ulong shm_rss;
    abi_ulong shm_swp;
    abi_ulong swap_attempts;
    abi_ulong swap_successes;
};

static inline abi_long host_to_target_shm_info(abi_ulong target_addr,
                                               struct shm_info *host_shm_info)
{
    struct target_shm_info *target_shm_info;
    if (!lock_user_struct(VERIFY_WRITE, target_shm_info, target_addr, 0))
        return -TARGET_EFAULT;
    __put_user(host_shm_info->used_ids, &target_shm_info->used_ids);
    __put_user(host_shm_info->shm_tot, &target_shm_info->shm_tot);
    __put_user(host_shm_info->shm_rss, &target_shm_info->shm_rss);
    __put_user(host_shm_info->shm_swp, &target_shm_info->shm_swp);
    __put_user(host_shm_info->swap_attempts, &target_shm_info->swap_attempts);
    __put_user(host_shm_info->swap_successes, &target_shm_info->swap_successes);
    unlock_user_struct(target_shm_info, target_addr, 1);
    return 0;
}

static inline abi_long do_shmctl(int shmid, int cmd, abi_long buf)
{
    struct shmid_ds dsarg;
    struct shminfo shminfo;
    struct shm_info shm_info;
    abi_long ret = -TARGET_EINVAL;

    cmd &= 0xff;

    switch(cmd) {
    case IPC_STAT:
    case IPC_SET:
    case SHM_STAT:
        if (target_to_host_shmid_ds(&dsarg, buf))
            return -TARGET_EFAULT;
        ret = get_errno(shmctl(shmid, cmd, &dsarg));
        if (host_to_target_shmid_ds(buf, &dsarg))
            return -TARGET_EFAULT;
        break;
    case IPC_INFO:
        ret = get_errno(shmctl(shmid, cmd, (struct shmid_ds *)&shminfo));
        if (host_to_target_shminfo(buf, &shminfo))
            return -TARGET_EFAULT;
        break;
    case SHM_INFO:
        ret = get_errno(shmctl(shmid, cmd, (struct shmid_ds *)&shm_info));
        if (host_to_target_shm_info(buf, &shm_info))
            return -TARGET_EFAULT;
        break;
    case IPC_RMID:
    case SHM_LOCK:
    case SHM_UNLOCK:
        ret = get_errno(shmctl(shmid, cmd, NULL));
        break;
    }

    return ret;
}

static inline abi_ulong do_shmat(int shmid, abi_ulong shmaddr, int shmflg)
{
    abi_long raddr;
    void *host_raddr;
    struct shmid_ds shm_info;
    int i,ret;

    /* find out the length of the shared memory segment */
    ret = get_errno(shmctl(shmid, IPC_STAT, &shm_info));
    if (is_error(ret)) {
        /* can't get length, bail out */
        return ret;
    }

    mmap_lock();

    if (shmaddr)
        host_raddr = shmat(shmid, (void *)g2h(shmaddr), shmflg);
    else {
        abi_ulong mmap_start;

        mmap_start = mmap_find_vma(0, shm_info.shm_segsz);

        if (mmap_start == -1) {
            errno = ENOMEM;
            host_raddr = (void *)-1;
        } else
            host_raddr = shmat(shmid, g2h(mmap_start), shmflg | SHM_REMAP);
    }

    if (host_raddr == (void *)-1) {
        mmap_unlock();
        return get_errno((long)host_raddr);
    }
    raddr=h2g((unsigned long)host_raddr);

    page_set_flags(raddr, raddr + shm_info.shm_segsz,
                   PAGE_VALID | PAGE_READ |
                   ((shmflg & SHM_RDONLY)? 0 : PAGE_WRITE));

    for (i = 0; i < N_SHM_REGIONS; i++) {
        if (shm_regions[i].start == 0) {
            shm_regions[i].start = raddr;
            shm_regions[i].size = shm_info.shm_segsz;
            break;
        }
    }

    mmap_unlock();
    return raddr;

}

static inline abi_long do_shmdt(abi_ulong shmaddr)
{
    int i;

    for (i = 0; i < N_SHM_REGIONS; ++i) {
        if (shm_regions[i].start == shmaddr) {
            shm_regions[i].start = 0;
            page_set_flags(shmaddr, shmaddr + shm_regions[i].size, 0);
            break;
        }
    }

    return get_errno(shmdt(g2h(shmaddr)));
}

#ifdef TARGET_NR_ipc
/* ??? This only works with linear mappings.  */
/* do_ipc() must return target values and target errnos. */
static abi_long do_ipc(unsigned int call, abi_long first,
                       abi_long second, abi_long third,
                       abi_long ptr, abi_long fifth)
{
    int version;
    abi_long ret = 0;

    version = call >> 16;
    call &= 0xffff;

    switch (call) {
    case IPCOP_semop:
        ret = do_semop(first, ptr, second);
        break;

    case IPCOP_semget:
        ret = get_errno(semget(first, second, third));
        break;

    case IPCOP_semctl: {
        /* The semun argument to semctl is passed by value, so dereference the
         * ptr argument. */
        abi_ulong atptr;
        get_user_ual(atptr, ptr);
        ret = do_semctl(first, second, third,
                (union target_semun) atptr);
        break;
    }

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
        switch (version) {
        case 0:
            {
                struct target_ipc_kludge {
                    abi_long msgp;
                    abi_long msgtyp;
                } *tmp;

                if (!lock_user_struct(VERIFY_READ, tmp, ptr, 1)) {
                    ret = -TARGET_EFAULT;
                    break;
                }

                ret = do_msgrcv(first, tswapal(tmp->msgp), second, tswapal(tmp->msgtyp), third);

                unlock_user_struct(tmp, ptr, 0);
                break;
            }
        default:
            ret = do_msgrcv(first, ptr, second, fifth, third);
        }
        break;

    case IPCOP_shmat:
        switch (version) {
        default:
        {
            abi_ulong raddr;
            raddr = do_shmat(first, ptr, second);
            if (is_error(raddr))
                return get_errno(raddr);
            if (put_user_ual(raddr, third))
                return -TARGET_EFAULT;
            break;
        }
        case 1:
            ret = -TARGET_EINVAL;
            break;
        }
	break;
    case IPCOP_shmdt:
        ret = do_shmdt(ptr);
	break;

    case IPCOP_shmget:
	/* IPC_* flag values are the same on all linux platforms */
	ret = get_errno(shmget(first, second, third));
	break;

	/* IPC_* and SHM_* command values are the same on all linux platforms */
    case IPCOP_shmctl:
        ret = do_shmctl(first, second, ptr);
        break;
    default:
	gemu_log("Unsupported ipc call: %d (version %d)\n", call, version);
	ret = -TARGET_ENOSYS;
	break;
    }
    return ret;
}
#endif

/* kernel structure types definitions */

#define STRUCT(name, ...) STRUCT_ ## name,
#define STRUCT_SPECIAL(name) STRUCT_ ## name,
enum {
#include "syscall_types.h"
};
#undef STRUCT
#undef STRUCT_SPECIAL

#define STRUCT(name, ...) static const argtype struct_ ## name ## _def[] = {  __VA_ARGS__, TYPE_NULL };
#define STRUCT_SPECIAL(name)
#include "syscall_types.h"
#undef STRUCT
#undef STRUCT_SPECIAL

typedef struct IOCTLEntry IOCTLEntry;

typedef abi_long do_ioctl_fn(const IOCTLEntry *ie, uint8_t *buf_temp,
                             int fd, abi_long cmd, abi_long arg);

struct IOCTLEntry {
    unsigned int target_cmd;
    unsigned int host_cmd;
    const char *name;
    int access;
    do_ioctl_fn *do_ioctl;
    const argtype arg_type[5];
};

#define IOC_R 0x0001
#define IOC_W 0x0002
#define IOC_RW (IOC_R | IOC_W)

#define MAX_STRUCT_SIZE 4096

#ifdef CONFIG_FIEMAP
/* So fiemap access checks don't overflow on 32 bit systems.
 * This is very slightly smaller than the limit imposed by
 * the underlying kernel.
 */
#define FIEMAP_MAX_EXTENTS ((UINT_MAX - sizeof(struct fiemap))  \
                            / sizeof(struct fiemap_extent))

static abi_long do_ioctl_fs_ioc_fiemap(const IOCTLEntry *ie, uint8_t *buf_temp,
                                       int fd, abi_long cmd, abi_long arg)
{
    /* The parameter for this ioctl is a struct fiemap followed
     * by an array of struct fiemap_extent whose size is set
     * in fiemap->fm_extent_count. The array is filled in by the
     * ioctl.
     */
    int target_size_in, target_size_out;
    struct fiemap *fm;
    const argtype *arg_type = ie->arg_type;
    const argtype extent_arg_type[] = { MK_STRUCT(STRUCT_fiemap_extent) };
    void *argptr, *p;
    abi_long ret;
    int i, extent_size = thunk_type_size(extent_arg_type, 0);
    uint32_t outbufsz;
    int free_fm = 0;

    assert(arg_type[0] == TYPE_PTR);
    assert(ie->access == IOC_RW);
    arg_type++;
    target_size_in = thunk_type_size(arg_type, 0);
    argptr = lock_user(VERIFY_READ, arg, target_size_in, 1);
    if (!argptr) {
        return -TARGET_EFAULT;
    }
    thunk_convert(buf_temp, argptr, arg_type, THUNK_HOST);
    unlock_user(argptr, arg, 0);
    fm = (struct fiemap *)buf_temp;
    if (fm->fm_extent_count > FIEMAP_MAX_EXTENTS) {
        return -TARGET_EINVAL;
    }

    outbufsz = sizeof (*fm) +
        (sizeof(struct fiemap_extent) * fm->fm_extent_count);

    if (outbufsz > MAX_STRUCT_SIZE) {
        /* We can't fit all the extents into the fixed size buffer.
         * Allocate one that is large enough and use it instead.
         */
        fm = malloc(outbufsz);
        if (!fm) {
            return -TARGET_ENOMEM;
        }
        memcpy(fm, buf_temp, sizeof(struct fiemap));
        free_fm = 1;
    }
    ret = get_errno(ioctl(fd, ie->host_cmd, fm));
    if (!is_error(ret)) {
        target_size_out = target_size_in;
        /* An extent_count of 0 means we were only counting the extents
         * so there are no structs to copy
         */
        if (fm->fm_extent_count != 0) {
            target_size_out += fm->fm_mapped_extents * extent_size;
        }
        argptr = lock_user(VERIFY_WRITE, arg, target_size_out, 0);
        if (!argptr) {
            ret = -TARGET_EFAULT;
        } else {
            /* Convert the struct fiemap */
            thunk_convert(argptr, fm, arg_type, THUNK_TARGET);
            if (fm->fm_extent_count != 0) {
                p = argptr + target_size_in;
                /* ...and then all the struct fiemap_extents */
                for (i = 0; i < fm->fm_mapped_extents; i++) {
                    thunk_convert(p, &fm->fm_extents[i], extent_arg_type,
                                  THUNK_TARGET);
                    p += extent_size;
                }
            }
            unlock_user(argptr, arg, target_size_out);
        }
    }
    if (free_fm) {
        free(fm);
    }
    return ret;
}
#endif

static abi_long do_ioctl_ifconf(const IOCTLEntry *ie, uint8_t *buf_temp,
                                int fd, abi_long cmd, abi_long arg)
{
    const argtype *arg_type = ie->arg_type;
    int target_size;
    void *argptr;
    int ret;
    struct ifconf *host_ifconf;
    uint32_t outbufsz;
    const argtype ifreq_arg_type[] = { MK_STRUCT(STRUCT_sockaddr_ifreq) };
    int target_ifreq_size;
    int nb_ifreq;
    int free_buf = 0;
    int i;
    int target_ifc_len;
    abi_long target_ifc_buf;
    int host_ifc_len;
    char *host_ifc_buf;

    assert(arg_type[0] == TYPE_PTR);
    assert(ie->access == IOC_RW);

    arg_type++;
    target_size = thunk_type_size(arg_type, 0);

    argptr = lock_user(VERIFY_READ, arg, target_size, 1);
    if (!argptr)
        return -TARGET_EFAULT;
    thunk_convert(buf_temp, argptr, arg_type, THUNK_HOST);
    unlock_user(argptr, arg, 0);

    host_ifconf = (struct ifconf *)(unsigned long)buf_temp;
    target_ifc_len = host_ifconf->ifc_len;
    target_ifc_buf = (abi_long)(unsigned long)host_ifconf->ifc_buf;

    target_ifreq_size = thunk_type_size(ifreq_arg_type, 0);
    nb_ifreq = target_ifc_len / target_ifreq_size;
    host_ifc_len = nb_ifreq * sizeof(struct ifreq);

    outbufsz = sizeof(*host_ifconf) + host_ifc_len;
    if (outbufsz > MAX_STRUCT_SIZE) {
        /* We can't fit all the extents into the fixed size buffer.
         * Allocate one that is large enough and use it instead.
         */
        host_ifconf = malloc(outbufsz);
        if (!host_ifconf) {
            return -TARGET_ENOMEM;
        }
        memcpy(host_ifconf, buf_temp, sizeof(*host_ifconf));
        free_buf = 1;
    }
    host_ifc_buf = (char*)host_ifconf + sizeof(*host_ifconf);

    host_ifconf->ifc_len = host_ifc_len;
    host_ifconf->ifc_buf = host_ifc_buf;

    ret = get_errno(ioctl(fd, ie->host_cmd, host_ifconf));
    if (!is_error(ret)) {
	/* convert host ifc_len to target ifc_len */

        nb_ifreq = host_ifconf->ifc_len / sizeof(struct ifreq);
        target_ifc_len = nb_ifreq * target_ifreq_size;
        host_ifconf->ifc_len = target_ifc_len;

	/* restore target ifc_buf */

        host_ifconf->ifc_buf = (char *)(unsigned long)target_ifc_buf;

	/* copy struct ifconf to target user */

        argptr = lock_user(VERIFY_WRITE, arg, target_size, 0);
        if (!argptr)
            return -TARGET_EFAULT;
        thunk_convert(argptr, host_ifconf, arg_type, THUNK_TARGET);
        unlock_user(argptr, arg, target_size);

	/* copy ifreq[] to target user */

        argptr = lock_user(VERIFY_WRITE, target_ifc_buf, target_ifc_len, 0);
        for (i = 0; i < nb_ifreq ; i++) {
            thunk_convert(argptr + i * target_ifreq_size,
                          host_ifc_buf + i * sizeof(struct ifreq),
                          ifreq_arg_type, THUNK_TARGET);
        }
        unlock_user(argptr, target_ifc_buf, target_ifc_len);
    }

    if (free_buf) {
        free(host_ifconf);
    }

    return ret;
}

static abi_long do_ioctl_dm(const IOCTLEntry *ie, uint8_t *buf_temp, int fd,
                            abi_long cmd, abi_long arg)
{
    void *argptr;
    struct dm_ioctl *host_dm;
    abi_long guest_data;
    uint32_t guest_data_size;
    int target_size;
    const argtype *arg_type = ie->arg_type;
    abi_long ret;
    void *big_buf = NULL;
    char *host_data;

    arg_type++;
    target_size = thunk_type_size(arg_type, 0);
    argptr = lock_user(VERIFY_READ, arg, target_size, 1);
    if (!argptr) {
        ret = -TARGET_EFAULT;
        goto out;
    }
    thunk_convert(buf_temp, argptr, arg_type, THUNK_HOST);
    unlock_user(argptr, arg, 0);

    /* buf_temp is too small, so fetch things into a bigger buffer */
    big_buf = g_malloc0(((struct dm_ioctl*)buf_temp)->data_size * 2);
    memcpy(big_buf, buf_temp, target_size);
    buf_temp = big_buf;
    host_dm = big_buf;

    guest_data = arg + host_dm->data_start;
    if ((guest_data - arg) < 0) {
        ret = -EINVAL;
        goto out;
    }
    guest_data_size = host_dm->data_size - host_dm->data_start;
    host_data = (char*)host_dm + host_dm->data_start;

    argptr = lock_user(VERIFY_READ, guest_data, guest_data_size, 1);
    switch (ie->host_cmd) {
    case DM_REMOVE_ALL:
    case DM_LIST_DEVICES:
    case DM_DEV_CREATE:
    case DM_DEV_REMOVE:
    case DM_DEV_SUSPEND:
    case DM_DEV_STATUS:
    case DM_DEV_WAIT:
    case DM_TABLE_STATUS:
    case DM_TABLE_CLEAR:
    case DM_TABLE_DEPS:
    case DM_LIST_VERSIONS:
        /* no input data */
        break;
    case DM_DEV_RENAME:
    case DM_DEV_SET_GEOMETRY:
        /* data contains only strings */
        memcpy(host_data, argptr, guest_data_size);
        break;
    case DM_TARGET_MSG:
        memcpy(host_data, argptr, guest_data_size);
        *(uint64_t*)host_data = tswap64(*(uint64_t*)argptr);
        break;
    case DM_TABLE_LOAD:
    {
        void *gspec = argptr;
        void *cur_data = host_data;
        const argtype arg_type[] = { MK_STRUCT(STRUCT_dm_target_spec) };
        int spec_size = thunk_type_size(arg_type, 0);
        int i;

        for (i = 0; i < host_dm->target_count; i++) {
            struct dm_target_spec *spec = cur_data;
            uint32_t next;
            int slen;

            thunk_convert(spec, gspec, arg_type, THUNK_HOST);
            slen = strlen((char*)gspec + spec_size) + 1;
            next = spec->next;
            spec->next = sizeof(*spec) + slen;
            strcpy((char*)&spec[1], gspec + spec_size);
            gspec += next;
            cur_data += spec->next;
        }
        break;
    }
    default:
        ret = -TARGET_EINVAL;
        goto out;
    }
    unlock_user(argptr, guest_data, 0);

    ret = get_errno(ioctl(fd, ie->host_cmd, buf_temp));
    if (!is_error(ret)) {
        guest_data = arg + host_dm->data_start;
        guest_data_size = host_dm->data_size - host_dm->data_start;
        argptr = lock_user(VERIFY_WRITE, guest_data, guest_data_size, 0);
        switch (ie->host_cmd) {
        case DM_REMOVE_ALL:
        case DM_DEV_CREATE:
        case DM_DEV_REMOVE:
        case DM_DEV_RENAME:
        case DM_DEV_SUSPEND:
        case DM_DEV_STATUS:
        case DM_TABLE_LOAD:
        case DM_TABLE_CLEAR:
        case DM_TARGET_MSG:
        case DM_DEV_SET_GEOMETRY:
            /* no return data */
            break;
        case DM_LIST_DEVICES:
        {
            struct dm_name_list *nl = (void*)host_dm + host_dm->data_start;
            uint32_t remaining_data = guest_data_size;
            void *cur_data = argptr;
            const argtype arg_type[] = { MK_STRUCT(STRUCT_dm_name_list) };
            int nl_size = 12; /* can't use thunk_size due to alignment */

            while (1) {
                uint32_t next = nl->next;
                if (next) {
                    nl->next = nl_size + (strlen(nl->name) + 1);
                }
                if (remaining_data < nl->next) {
                    host_dm->flags |= DM_BUFFER_FULL_FLAG;
                    break;
                }
                thunk_convert(cur_data, nl, arg_type, THUNK_TARGET);
                strcpy(cur_data + nl_size, nl->name);
                cur_data += nl->next;
                remaining_data -= nl->next;
                if (!next) {
                    break;
                }
                nl = (void*)nl + next;
            }
            break;
        }
        case DM_DEV_WAIT:
        case DM_TABLE_STATUS:
        {
            struct dm_target_spec *spec = (void*)host_dm + host_dm->data_start;
            void *cur_data = argptr;
            const argtype arg_type[] = { MK_STRUCT(STRUCT_dm_target_spec) };
            int spec_size = thunk_type_size(arg_type, 0);
            int i;

            for (i = 0; i < host_dm->target_count; i++) {
                uint32_t next = spec->next;
                int slen = strlen((char*)&spec[1]) + 1;
                spec->next = (cur_data - argptr) + spec_size + slen;
                if (guest_data_size < spec->next) {
                    host_dm->flags |= DM_BUFFER_FULL_FLAG;
                    break;
                }
                thunk_convert(cur_data, spec, arg_type, THUNK_TARGET);
                strcpy(cur_data + spec_size, (char*)&spec[1]);
                cur_data = argptr + spec->next;
                spec = (void*)host_dm + host_dm->data_start + next;
            }
            break;
        }
        case DM_TABLE_DEPS:
        {
            void *hdata = (void*)host_dm + host_dm->data_start;
            int count = *(uint32_t*)hdata;
            uint64_t *hdev = hdata + 8;
            uint64_t *gdev = argptr + 8;
            int i;

            *(uint32_t*)argptr = tswap32(count);
            for (i = 0; i < count; i++) {
                *gdev = tswap64(*hdev);
                gdev++;
                hdev++;
            }
            break;
        }
        case DM_LIST_VERSIONS:
        {
            struct dm_target_versions *vers = (void*)host_dm + host_dm->data_start;
            uint32_t remaining_data = guest_data_size;
            void *cur_data = argptr;
            const argtype arg_type[] = { MK_STRUCT(STRUCT_dm_target_versions) };
            int vers_size = thunk_type_size(arg_type, 0);

            while (1) {
                uint32_t next = vers->next;
                if (next) {
                    vers->next = vers_size + (strlen(vers->name) + 1);
                }
                if (remaining_data < vers->next) {
                    host_dm->flags |= DM_BUFFER_FULL_FLAG;
                    break;
                }
                thunk_convert(cur_data, vers, arg_type, THUNK_TARGET);
                strcpy(cur_data + vers_size, vers->name);
                cur_data += vers->next;
                remaining_data -= vers->next;
                if (!next) {
                    break;
                }
                vers = (void*)vers + next;
            }
            break;
        }
        default:
            ret = -TARGET_EINVAL;
            goto out;
        }
        unlock_user(argptr, guest_data, guest_data_size);

        argptr = lock_user(VERIFY_WRITE, arg, target_size, 0);
        if (!argptr) {
            ret = -TARGET_EFAULT;
            goto out;
        }
        thunk_convert(argptr, buf_temp, arg_type, THUNK_TARGET);
        unlock_user(argptr, arg, target_size);
    }
out:
    g_free(big_buf);
    return ret;
}

static abi_long do_ioctl_rt(const IOCTLEntry *ie, uint8_t *buf_temp,
                                int fd, abi_long cmd, abi_long arg)
{
    const argtype *arg_type = ie->arg_type;
    const StructEntry *se;
    const argtype *field_types;
    const int *dst_offsets, *src_offsets;
    int target_size;
    void *argptr;
    abi_ulong *target_rt_dev_ptr;
    unsigned long *host_rt_dev_ptr;
    abi_long ret;
    int i;

    assert(ie->access == IOC_W);
    assert(*arg_type == TYPE_PTR);
    arg_type++;
    assert(*arg_type == TYPE_STRUCT);
    target_size = thunk_type_size(arg_type, 0);
    argptr = lock_user(VERIFY_READ, arg, target_size, 1);
    if (!argptr) {
        return -TARGET_EFAULT;
    }
    arg_type++;
    assert(*arg_type == (int)STRUCT_rtentry);
    se = struct_entries + *arg_type++;
    assert(se->convert[0] == NULL);
    /* convert struct here to be able to catch rt_dev string */
    field_types = se->field_types;
    dst_offsets = se->field_offsets[THUNK_HOST];
    src_offsets = se->field_offsets[THUNK_TARGET];
    for (i = 0; i < se->nb_fields; i++) {
        if (dst_offsets[i] == offsetof(struct rtentry, rt_dev)) {
            assert(*field_types == TYPE_PTRVOID);
            target_rt_dev_ptr = (abi_ulong *)(argptr + src_offsets[i]);
            host_rt_dev_ptr = (unsigned long *)(buf_temp + dst_offsets[i]);
            if (*target_rt_dev_ptr != 0) {
                *host_rt_dev_ptr = (unsigned long)lock_user_string(
                                                  tswapal(*target_rt_dev_ptr));
                if (!*host_rt_dev_ptr) {
                    unlock_user(argptr, arg, 0);
                    return -TARGET_EFAULT;
                }
            } else {
                *host_rt_dev_ptr = 0;
            }
            field_types++;
            continue;
        }
        field_types = thunk_convert(buf_temp + dst_offsets[i],
                                    argptr + src_offsets[i],
                                    field_types, THUNK_HOST);
    }
    unlock_user(argptr, arg, 0);

    ret = get_errno(ioctl(fd, ie->host_cmd, buf_temp));
    if (*host_rt_dev_ptr != 0) {
        unlock_user((void *)*host_rt_dev_ptr,
                    *target_rt_dev_ptr, 0);
    }
    return ret;
}

static abi_long do_ioctl_kdsigaccept(const IOCTLEntry *ie, uint8_t *buf_temp,
                                     int fd, abi_long cmd, abi_long arg)
{
    int sig = target_to_host_signal(arg);
    return get_errno(ioctl(fd, ie->host_cmd, sig));
}

static IOCTLEntry ioctl_entries[] = {
#define IOCTL(cmd, access, ...) \
    { TARGET_ ## cmd, cmd, #cmd, access, 0, {  __VA_ARGS__ } },
#define IOCTL_SPECIAL(cmd, access, dofn, ...)                      \
    { TARGET_ ## cmd, cmd, #cmd, access, dofn, {  __VA_ARGS__ } },
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
    if (ie->do_ioctl) {
        return ie->do_ioctl(ie, buf_temp, fd, cmd, arg);
    }

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

static const bitmask_transtbl iflag_tbl[] = {
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

static const bitmask_transtbl oflag_tbl[] = {
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

static const bitmask_transtbl cflag_tbl[] = {
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

static const bitmask_transtbl lflag_tbl[] = {
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

    memset(host->c_cc, 0, sizeof(host->c_cc));
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

    memset(target->c_cc, 0, sizeof(target->c_cc));
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

static const StructEntry struct_termios_def = {
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
        { TARGET_MAP_NORESERVE, TARGET_MAP_NORESERVE, MAP_NORESERVE,
          MAP_NORESERVE },
	{ 0, 0, 0, 0 }
};

#if defined(TARGET_I386)

/* NOTE: there is really one LDT for all the threads */
static uint8_t *ldt_table;

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
    ldt_info.base_addr = tswapal(target_ldt_info->base_addr);
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
        env->ldt.base = target_mmap(0,
                                    TARGET_LDT_ENTRIES * TARGET_LDT_ENTRY_SIZE,
                                    PROT_READ|PROT_WRITE,
                                    MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
        if (env->ldt.base == -1)
            return -TARGET_ENOMEM;
        memset(g2h(env->ldt.base), 0,
               TARGET_LDT_ENTRIES * TARGET_LDT_ENTRY_SIZE);
        env->ldt.limit = 0xffff;
        ldt_table = g2h(env->ldt.base);
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
abi_long do_set_thread_area(CPUX86State *env, abi_ulong ptr)
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
    ldt_info.base_addr = tswapal(target_ldt_info->base_addr);
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
    target_ldt_info->base_addr = tswapal(base_addr);
    target_ldt_info->limit = tswap32(limit);
    target_ldt_info->flags = tswap32(flags);
    unlock_user_struct(target_ldt_info, ptr, 1);
    return 0;
}
#endif /* TARGET_I386 && TARGET_ABI32 */

#ifndef TARGET_ABI32
abi_long do_arch_prctl(CPUX86State *env, int code, abi_ulong addr)
{
    abi_long ret = 0;
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
            ret = -TARGET_EFAULT;
        break;
    default:
        ret = -TARGET_EINVAL;
        break;
    }
    return ret;
}
#endif

#endif /* defined(TARGET_I386) */

#define NEW_STACK_SIZE 0x40000


static pthread_mutex_t clone_lock = PTHREAD_MUTEX_INITIALIZER;
typedef struct {
    CPUArchState *env;
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
    CPUArchState *env;
    CPUState *cpu;
    TaskState *ts;

    env = info->env;
    cpu = ENV_GET_CPU(env);
    thread_cpu = cpu;
    ts = (TaskState *)cpu->opaque;
    info->tid = gettid();
    cpu->host_tid = info->tid;
    task_settid(ts);
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

/* do_fork() Must return host values and target errnos (unlike most
   do_*() functions). */
static int do_fork(CPUArchState *env, unsigned int flags, abi_ulong newsp,
                   abi_ulong parent_tidptr, target_ulong newtls,
                   abi_ulong child_tidptr)
{
    CPUState *cpu = ENV_GET_CPU(env);
    int ret;
    TaskState *ts;
    CPUState *new_cpu;
    CPUArchState *new_env;
    unsigned int nptl_flags;
    sigset_t sigmask;

    /* Emulate vfork() with fork() */
    if (flags & CLONE_VFORK)
        flags &= ~(CLONE_VFORK | CLONE_VM);

    if (flags & CLONE_VM) {
        TaskState *parent_ts = (TaskState *)cpu->opaque;
        new_thread_info info;
        pthread_attr_t attr;

        ts = g_malloc0(sizeof(TaskState));
        init_task_state(ts);
        /* we create a new CPU instance. */
        new_env = cpu_copy(env);
        /* Init regs that differ from the parent.  */
        cpu_clone_regs(new_env, newsp);
        new_cpu = ENV_GET_CPU(new_env);
        new_cpu->opaque = ts;
        ts->bprm = parent_ts->bprm;
        ts->info = parent_ts->info;
        nptl_flags = flags;
        flags &= ~CLONE_NPTL_FLAGS2;

        if (nptl_flags & CLONE_CHILD_CLEARTID) {
            ts->child_tidptr = child_tidptr;
        }

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
        ret = pthread_attr_setstacksize(&attr, NEW_STACK_SIZE);
        ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        /* It is not safe to deliver signals until the child has finished
           initializing, so temporarily block all signals.  */
        sigfillset(&sigmask);
        sigprocmask(SIG_BLOCK, &sigmask, &info.sigmask);

        ret = pthread_create(&info.thread, &attr, clone_func, &info);
        /* TODO: Free new CPU state if thread creation failed.  */

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
    } else {
        /* if no CLONE_VM, we consider it is a fork */
        if ((flags & ~(CSIGNAL | CLONE_NPTL_FLAGS2)) != 0)
            return -EINVAL;
        fork_start();
        ret = fork();
        if (ret == 0) {
            /* Child Process.  */
            cpu_clone_regs(env, newsp);
            fork_end(1);
            /* There is a race condition here.  The parent process could
               theoretically read the TID in the child process before the child
               tid is set.  This would require using either ptrace
               (not implemented) or having *_tidptr to point at a shared memory
               mapping.  We can't repeat the spinlock hack used above because
               the child process gets its own copy of the lock.  */
            if (flags & CLONE_CHILD_SETTID)
                put_user_u32(gettid(), child_tidptr);
            if (flags & CLONE_PARENT_SETTID)
                put_user_u32(gettid(), parent_tidptr);
            ts = (TaskState *)cpu->opaque;
            if (flags & CLONE_SETTLS)
                cpu_set_tls (env, newtls);
            if (flags & CLONE_CHILD_CLEARTID)
                ts->child_tidptr = child_tidptr;
        } else {
            fork_end(0);
        }
    }
    return ret;
}

/* warning : doesn't handle linux specific flags... */
static int target_to_host_fcntl_cmd(int cmd)
{
    switch(cmd) {
	case TARGET_F_DUPFD:
	case TARGET_F_GETFD:
	case TARGET_F_SETFD:
	case TARGET_F_GETFL:
	case TARGET_F_SETFL:
            return cmd;
        case TARGET_F_GETLK:
	    return F_GETLK;
	case TARGET_F_SETLK:
	    return F_SETLK;
	case TARGET_F_SETLKW:
	    return F_SETLKW;
	case TARGET_F_GETOWN:
	    return F_GETOWN;
	case TARGET_F_SETOWN:
	    return F_SETOWN;
	case TARGET_F_GETSIG:
	    return F_GETSIG;
	case TARGET_F_SETSIG:
	    return F_SETSIG;
#if TARGET_ABI_BITS == 32
        case TARGET_F_GETLK64:
	    return F_GETLK64;
	case TARGET_F_SETLK64:
	    return F_SETLK64;
	case TARGET_F_SETLKW64:
	    return F_SETLKW64;
#endif
        case TARGET_F_SETLEASE:
            return F_SETLEASE;
        case TARGET_F_GETLEASE:
            return F_GETLEASE;
#ifdef F_DUPFD_CLOEXEC
        case TARGET_F_DUPFD_CLOEXEC:
            return F_DUPFD_CLOEXEC;
#endif
        case TARGET_F_NOTIFY:
            return F_NOTIFY;
#ifdef F_GETOWN_EX
	case TARGET_F_GETOWN_EX:
	    return F_GETOWN_EX;
#endif
#ifdef F_SETOWN_EX
	case TARGET_F_SETOWN_EX:
	    return F_SETOWN_EX;
#endif
	default:
            return -TARGET_EINVAL;
    }
    return -TARGET_EINVAL;
}

#define TRANSTBL_CONVERT(a) { -1, TARGET_##a, -1, a }
static const bitmask_transtbl flock_tbl[] = {
    TRANSTBL_CONVERT(F_RDLCK),
    TRANSTBL_CONVERT(F_WRLCK),
    TRANSTBL_CONVERT(F_UNLCK),
    TRANSTBL_CONVERT(F_EXLCK),
    TRANSTBL_CONVERT(F_SHLCK),
    { 0, 0, 0, 0 }
};

static abi_long do_fcntl(int fd, int cmd, abi_ulong arg)
{
    struct flock fl;
    struct target_flock *target_fl;
    struct flock64 fl64;
    struct target_flock64 *target_fl64;
#ifdef F_GETOWN_EX
    struct f_owner_ex fox;
    struct target_f_owner_ex *target_fox;
#endif
    abi_long ret;
    int host_cmd = target_to_host_fcntl_cmd(cmd);

    if (host_cmd == -TARGET_EINVAL)
	    return host_cmd;

    switch(cmd) {
    case TARGET_F_GETLK:
        if (!lock_user_struct(VERIFY_READ, target_fl, arg, 1))
            return -TARGET_EFAULT;
        fl.l_type =
                  target_to_host_bitmask(tswap16(target_fl->l_type), flock_tbl);
        fl.l_whence = tswap16(target_fl->l_whence);
        fl.l_start = tswapal(target_fl->l_start);
        fl.l_len = tswapal(target_fl->l_len);
        fl.l_pid = tswap32(target_fl->l_pid);
        unlock_user_struct(target_fl, arg, 0);
        ret = get_errno(fcntl(fd, host_cmd, &fl));
        if (ret == 0) {
            if (!lock_user_struct(VERIFY_WRITE, target_fl, arg, 0))
                return -TARGET_EFAULT;
            target_fl->l_type =
                          host_to_target_bitmask(tswap16(fl.l_type), flock_tbl);
            target_fl->l_whence = tswap16(fl.l_whence);
            target_fl->l_start = tswapal(fl.l_start);
            target_fl->l_len = tswapal(fl.l_len);
            target_fl->l_pid = tswap32(fl.l_pid);
            unlock_user_struct(target_fl, arg, 1);
        }
        break;

    case TARGET_F_SETLK:
    case TARGET_F_SETLKW:
        if (!lock_user_struct(VERIFY_READ, target_fl, arg, 1))
            return -TARGET_EFAULT;
        fl.l_type =
                  target_to_host_bitmask(tswap16(target_fl->l_type), flock_tbl);
        fl.l_whence = tswap16(target_fl->l_whence);
        fl.l_start = tswapal(target_fl->l_start);
        fl.l_len = tswapal(target_fl->l_len);
        fl.l_pid = tswap32(target_fl->l_pid);
        unlock_user_struct(target_fl, arg, 0);
        ret = get_errno(fcntl(fd, host_cmd, &fl));
        break;

    case TARGET_F_GETLK64:
        if (!lock_user_struct(VERIFY_READ, target_fl64, arg, 1))
            return -TARGET_EFAULT;
        fl64.l_type =
           target_to_host_bitmask(tswap16(target_fl64->l_type), flock_tbl) >> 1;
        fl64.l_whence = tswap16(target_fl64->l_whence);
        fl64.l_start = tswap64(target_fl64->l_start);
        fl64.l_len = tswap64(target_fl64->l_len);
        fl64.l_pid = tswap32(target_fl64->l_pid);
        unlock_user_struct(target_fl64, arg, 0);
        ret = get_errno(fcntl(fd, host_cmd, &fl64));
        if (ret == 0) {
            if (!lock_user_struct(VERIFY_WRITE, target_fl64, arg, 0))
                return -TARGET_EFAULT;
            target_fl64->l_type =
                   host_to_target_bitmask(tswap16(fl64.l_type), flock_tbl) >> 1;
            target_fl64->l_whence = tswap16(fl64.l_whence);
            target_fl64->l_start = tswap64(fl64.l_start);
            target_fl64->l_len = tswap64(fl64.l_len);
            target_fl64->l_pid = tswap32(fl64.l_pid);
            unlock_user_struct(target_fl64, arg, 1);
        }
        break;
    case TARGET_F_SETLK64:
    case TARGET_F_SETLKW64:
        if (!lock_user_struct(VERIFY_READ, target_fl64, arg, 1))
            return -TARGET_EFAULT;
        fl64.l_type =
           target_to_host_bitmask(tswap16(target_fl64->l_type), flock_tbl) >> 1;
        fl64.l_whence = tswap16(target_fl64->l_whence);
        fl64.l_start = tswap64(target_fl64->l_start);
        fl64.l_len = tswap64(target_fl64->l_len);
        fl64.l_pid = tswap32(target_fl64->l_pid);
        unlock_user_struct(target_fl64, arg, 0);
        ret = get_errno(fcntl(fd, host_cmd, &fl64));
        break;

    case TARGET_F_GETFL:
        ret = get_errno(fcntl(fd, host_cmd, arg));
        if (ret >= 0) {
            ret = host_to_target_bitmask(ret, fcntl_flags_tbl);
        }
        break;

    case TARGET_F_SETFL:
        ret = get_errno(fcntl(fd, host_cmd, target_to_host_bitmask(arg, fcntl_flags_tbl)));
        break;

#ifdef F_GETOWN_EX
    case TARGET_F_GETOWN_EX:
        ret = get_errno(fcntl(fd, host_cmd, &fox));
        if (ret >= 0) {
            if (!lock_user_struct(VERIFY_WRITE, target_fox, arg, 0))
                return -TARGET_EFAULT;
            target_fox->type = tswap32(fox.type);
            target_fox->pid = tswap32(fox.pid);
            unlock_user_struct(target_fox, arg, 1);
        }
        break;
#endif

#ifdef F_SETOWN_EX
    case TARGET_F_SETOWN_EX:
        if (!lock_user_struct(VERIFY_READ, target_fox, arg, 1))
            return -TARGET_EFAULT;
        fox.type = tswap32(target_fox->type);
        fox.pid = tswap32(target_fox->pid);
        unlock_user_struct(target_fox, arg, 0);
        ret = get_errno(fcntl(fd, host_cmd, &fox));
        break;
#endif

    case TARGET_F_SETOWN:
    case TARGET_F_GETOWN:
    case TARGET_F_SETSIG:
    case TARGET_F_GETSIG:
    case TARGET_F_SETLEASE:
    case TARGET_F_GETLEASE:
        ret = get_errno(fcntl(fd, host_cmd, arg));
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
static inline int tswapid(int id)
{
    return tswap16(id);
}

#define put_user_id(x, gaddr) put_user_u16(x, gaddr)

#else /* !USE_UID16 */
static inline int high2lowuid(int uid)
{
    return uid;
}
static inline int high2lowgid(int gid)
{
    return gid;
}
static inline int low2highuid(int uid)
{
    return uid;
}
static inline int low2highgid(int gid)
{
    return gid;
}
static inline int tswapid(int id)
{
    return tswap32(id);
}

#define put_user_id(x, gaddr) put_user_u32(x, gaddr)

#endif /* USE_UID16 */

void syscall_init(void)
{
    IOCTLEntry *ie;
    const argtype *arg_type;
    int size;
    int i;

#define STRUCT(name, ...) thunk_register_struct(STRUCT_ ## name, #name, struct_ ## name ## _def);
#define STRUCT_SPECIAL(name) thunk_register_struct_direct(STRUCT_ ## name, #name, &struct_ ## name ## _def);
#include "syscall_types.h"
#undef STRUCT
#undef STRUCT_SPECIAL

    /* Build target_to_host_errno_table[] table from
     * host_to_target_errno_table[]. */
    for (i = 0; i < ERRNO_TABLE_SIZE; i++) {
        target_to_host_errno_table[host_to_target_errno_table[i]] = i;
    }

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
#if (defined(__i386__) && defined(TARGET_I386) && defined(TARGET_ABI32)) || \
    (defined(__x86_64__) && defined(TARGET_X86_64))
        if (unlikely(ie->target_cmd != ie->host_cmd)) {
            fprintf(stderr, "ERROR: ioctl(%s): target=0x%x host=0x%x\n",
                    ie->name, ie->target_cmd, ie->host_cmd);
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
    if (regpairs_aligned(cpu_env)) {
        arg2 = arg3;
        arg3 = arg4;
    }
    return get_errno(truncate64(arg1, target_offset64(arg2, arg3)));
}
#endif

#ifdef TARGET_NR_ftruncate64
static inline abi_long target_ftruncate64(void *cpu_env, abi_long arg1,
                                          abi_long arg2,
                                          abi_long arg3,
                                          abi_long arg4)
{
    if (regpairs_aligned(cpu_env)) {
        arg2 = arg3;
        arg3 = arg4;
    }
    return get_errno(ftruncate64(arg1, target_offset64(arg2, arg3)));
}
#endif

static inline abi_long target_to_host_timespec(struct timespec *host_ts,
                                               abi_ulong target_addr)
{
    struct target_timespec *target_ts;

    if (!lock_user_struct(VERIFY_READ, target_ts, target_addr, 1))
        return -TARGET_EFAULT;
    host_ts->tv_sec = tswapal(target_ts->tv_sec);
    host_ts->tv_nsec = tswapal(target_ts->tv_nsec);
    unlock_user_struct(target_ts, target_addr, 0);
    return 0;
}

static inline abi_long host_to_target_timespec(abi_ulong target_addr,
                                               struct timespec *host_ts)
{
    struct target_timespec *target_ts;

    if (!lock_user_struct(VERIFY_WRITE, target_ts, target_addr, 0))
        return -TARGET_EFAULT;
    target_ts->tv_sec = tswapal(host_ts->tv_sec);
    target_ts->tv_nsec = tswapal(host_ts->tv_nsec);
    unlock_user_struct(target_ts, target_addr, 1);
    return 0;
}

static inline abi_long target_to_host_itimerspec(struct itimerspec *host_itspec,
                                                 abi_ulong target_addr)
{
    struct target_itimerspec *target_itspec;

    if (!lock_user_struct(VERIFY_READ, target_itspec, target_addr, 1)) {
        return -TARGET_EFAULT;
    }

    host_itspec->it_interval.tv_sec =
                            tswapal(target_itspec->it_interval.tv_sec);
    host_itspec->it_interval.tv_nsec =
                            tswapal(target_itspec->it_interval.tv_nsec);
    host_itspec->it_value.tv_sec = tswapal(target_itspec->it_value.tv_sec);
    host_itspec->it_value.tv_nsec = tswapal(target_itspec->it_value.tv_nsec);

    unlock_user_struct(target_itspec, target_addr, 1);
    return 0;
}

static inline abi_long host_to_target_itimerspec(abi_ulong target_addr,
                                               struct itimerspec *host_its)
{
    struct target_itimerspec *target_itspec;

    if (!lock_user_struct(VERIFY_WRITE, target_itspec, target_addr, 0)) {
        return -TARGET_EFAULT;
    }

    target_itspec->it_interval.tv_sec = tswapal(host_its->it_interval.tv_sec);
    target_itspec->it_interval.tv_nsec = tswapal(host_its->it_interval.tv_nsec);

    target_itspec->it_value.tv_sec = tswapal(host_its->it_value.tv_sec);
    target_itspec->it_value.tv_nsec = tswapal(host_its->it_value.tv_nsec);

    unlock_user_struct(target_itspec, target_addr, 0);
    return 0;
}

static inline abi_long target_to_host_sigevent(struct sigevent *host_sevp,
                                               abi_ulong target_addr)
{
    struct target_sigevent *target_sevp;

    if (!lock_user_struct(VERIFY_READ, target_sevp, target_addr, 1)) {
        return -TARGET_EFAULT;
    }

    /* This union is awkward on 64 bit systems because it has a 32 bit
     * integer and a pointer in it; we follow the conversion approach
     * used for handling sigval types in signal.c so the guest should get
     * the correct value back even if we did a 64 bit byteswap and it's
     * using the 32 bit integer.
     */
    host_sevp->sigev_value.sival_ptr =
        (void *)(uintptr_t)tswapal(target_sevp->sigev_value.sival_ptr);
    host_sevp->sigev_signo =
        target_to_host_signal(tswap32(target_sevp->sigev_signo));
    host_sevp->sigev_notify = tswap32(target_sevp->sigev_notify);
    host_sevp->_sigev_un._tid = tswap32(target_sevp->_sigev_un._tid);

    unlock_user_struct(target_sevp, target_addr, 1);
    return 0;
}

#if defined(TARGET_NR_stat64) || defined(TARGET_NR_newfstatat)
static inline abi_long host_to_target_stat64(void *cpu_env,
                                             abi_ulong target_addr,
                                             struct stat *host_st)
{
#if defined(TARGET_ARM) && defined(TARGET_ABI32)
    if (((CPUARMState *)cpu_env)->eabi) {
        struct target_eabi_stat64 *target_st;

        if (!lock_user_struct(VERIFY_WRITE, target_st, target_addr, 0))
            return -TARGET_EFAULT;
        memset(target_st, 0, sizeof(struct target_eabi_stat64));
        __put_user(host_st->st_dev, &target_st->st_dev);
        __put_user(host_st->st_ino, &target_st->st_ino);
#ifdef TARGET_STAT64_HAS_BROKEN_ST_INO
        __put_user(host_st->st_ino, &target_st->__st_ino);
#endif
        __put_user(host_st->st_mode, &target_st->st_mode);
        __put_user(host_st->st_nlink, &target_st->st_nlink);
        __put_user(host_st->st_uid, &target_st->st_uid);
        __put_user(host_st->st_gid, &target_st->st_gid);
        __put_user(host_st->st_rdev, &target_st->st_rdev);
        __put_user(host_st->st_size, &target_st->st_size);
        __put_user(host_st->st_blksize, &target_st->st_blksize);
        __put_user(host_st->st_blocks, &target_st->st_blocks);
        __put_user(host_st->st_atime, &target_st->target_st_atime);
        __put_user(host_st->st_mtime, &target_st->target_st_mtime);
        __put_user(host_st->st_ctime, &target_st->target_st_ctime);
        unlock_user_struct(target_st, target_addr, 1);
    } else
#endif
    {
#if defined(TARGET_HAS_STRUCT_STAT64)
        struct target_stat64 *target_st;
#else
        struct target_stat *target_st;
#endif

        if (!lock_user_struct(VERIFY_WRITE, target_st, target_addr, 0))
            return -TARGET_EFAULT;
        memset(target_st, 0, sizeof(*target_st));
        __put_user(host_st->st_dev, &target_st->st_dev);
        __put_user(host_st->st_ino, &target_st->st_ino);
#ifdef TARGET_STAT64_HAS_BROKEN_ST_INO
        __put_user(host_st->st_ino, &target_st->__st_ino);
#endif
        __put_user(host_st->st_mode, &target_st->st_mode);
        __put_user(host_st->st_nlink, &target_st->st_nlink);
        __put_user(host_st->st_uid, &target_st->st_uid);
        __put_user(host_st->st_gid, &target_st->st_gid);
        __put_user(host_st->st_rdev, &target_st->st_rdev);
        /* XXX: better use of kernel struct */
        __put_user(host_st->st_size, &target_st->st_size);
        __put_user(host_st->st_blksize, &target_st->st_blksize);
        __put_user(host_st->st_blocks, &target_st->st_blocks);
        __put_user(host_st->st_atime, &target_st->target_st_atime);
        __put_user(host_st->st_mtime, &target_st->target_st_mtime);
        __put_user(host_st->st_ctime, &target_st->target_st_ctime);
        unlock_user_struct(target_st, target_addr, 1);
    }

    return 0;
}
#endif

/* ??? Using host futex calls even when target atomic operations
   are not really atomic probably breaks things.  However implementing
   futexes locally would make futexes shared between multiple processes
   tricky.  However they're probably useless because guest atomic
   operations won't work either.  */
static int do_futex(target_ulong uaddr, int op, int val, target_ulong timeout,
                    target_ulong uaddr2, int val3)
{
    struct timespec ts, *pts;
    int base_op;

    /* ??? We assume FUTEX_* constants are the same on both host
       and target.  */
#ifdef FUTEX_CMD_MASK
    base_op = op & FUTEX_CMD_MASK;
#else
    base_op = op;
#endif
    switch (base_op) {
    case FUTEX_WAIT:
    case FUTEX_WAIT_BITSET:
        if (timeout) {
            pts = &ts;
            target_to_host_timespec(pts, timeout);
        } else {
            pts = NULL;
        }
        return get_errno(sys_futex(g2h(uaddr), op, tswap32(val),
                         pts, NULL, val3));
    case FUTEX_WAKE:
        return get_errno(sys_futex(g2h(uaddr), op, val, NULL, NULL, 0));
    case FUTEX_FD:
        return get_errno(sys_futex(g2h(uaddr), op, val, NULL, NULL, 0));
    case FUTEX_REQUEUE:
    case FUTEX_CMP_REQUEUE:
    case FUTEX_WAKE_OP:
        /* For FUTEX_REQUEUE, FUTEX_CMP_REQUEUE, and FUTEX_WAKE_OP, the
           TIMEOUT parameter is interpreted as a uint32_t by the kernel.
           But the prototype takes a `struct timespec *'; insert casts
           to satisfy the compiler.  We do not need to tswap TIMEOUT
           since it's not compared to guest memory.  */
        pts = (struct timespec *)(uintptr_t) timeout;
        return get_errno(sys_futex(g2h(uaddr), op, val, pts,
                                   g2h(uaddr2),
                                   (base_op == FUTEX_CMP_REQUEUE
                                    ? tswap32(val3)
                                    : val3)));
    default:
        return -TARGET_ENOSYS;
    }
}

/* Map host to target signal numbers for the wait family of syscalls.
   Assume all other status bits are the same.  */
int host_to_target_waitstatus(int status)
{
    if (WIFSIGNALED(status)) {
        return host_to_target_signal(WTERMSIG(status)) | (status & ~0x7f);
    }
    if (WIFSTOPPED(status)) {
        return (host_to_target_signal(WSTOPSIG(status)) << 8)
               | (status & 0xff);
    }
    return status;
}

static int open_self_cmdline(void *cpu_env, int fd)
{
    int fd_orig = -1;
    bool word_skipped = false;

    fd_orig = open("/proc/self/cmdline", O_RDONLY);
    if (fd_orig < 0) {
        return fd_orig;
    }

    while (true) {
        ssize_t nb_read;
        char buf[128];
        char *cp_buf = buf;

        nb_read = read(fd_orig, buf, sizeof(buf));
        if (nb_read < 0) {
            fd_orig = close(fd_orig);
            return -1;
        } else if (nb_read == 0) {
            break;
        }

        if (!word_skipped) {
            /* Skip the first string, which is the path to qemu-*-static
               instead of the actual command. */
            cp_buf = memchr(buf, 0, sizeof(buf));
            if (cp_buf) {
                /* Null byte found, skip one string */
                cp_buf++;
                nb_read -= cp_buf - buf;
                word_skipped = true;
            }
        }

        if (word_skipped) {
            if (write(fd, cp_buf, nb_read) != nb_read) {
                return -1;
            }
        }
    }

    return close(fd_orig);
}

static int open_self_maps(void *cpu_env, int fd)
{
    CPUState *cpu = ENV_GET_CPU((CPUArchState *)cpu_env);
    TaskState *ts = cpu->opaque;
    FILE *fp;
    char *line = NULL;
    size_t len = 0;
    ssize_t read;

    fp = fopen("/proc/self/maps", "r");
    if (fp == NULL) {
        return -EACCES;
    }

    while ((read = getline(&line, &len, fp)) != -1) {
        int fields, dev_maj, dev_min, inode;
        uint64_t min, max, offset;
        char flag_r, flag_w, flag_x, flag_p;
        char path[512] = "";
        fields = sscanf(line, "%"PRIx64"-%"PRIx64" %c%c%c%c %"PRIx64" %x:%x %d"
                        " %512s", &min, &max, &flag_r, &flag_w, &flag_x,
                        &flag_p, &offset, &dev_maj, &dev_min, &inode, path);

        if ((fields < 10) || (fields > 11)) {
            continue;
        }
        if (h2g_valid(min)) {
            int flags = page_get_flags(h2g(min));
            max = h2g_valid(max - 1) ? max : (uintptr_t)g2h(GUEST_ADDR_MAX);
            if (page_check_range(h2g(min), max - min, flags) == -1) {
                continue;
            }
            if (h2g(min) == ts->info->stack_limit) {
                pstrcpy(path, sizeof(path), "      [stack]");
            }
            dprintf(fd, TARGET_ABI_FMT_lx "-" TARGET_ABI_FMT_lx
                    " %c%c%c%c %08" PRIx64 " %02x:%02x %d %s%s\n",
                    h2g(min), h2g(max - 1) + 1, flag_r, flag_w,
                    flag_x, flag_p, offset, dev_maj, dev_min, inode,
                    path[0] ? "         " : "", path);
        }
    }

    free(line);
    fclose(fp);

    return 0;
}

static int open_self_stat(void *cpu_env, int fd)
{
    CPUState *cpu = ENV_GET_CPU((CPUArchState *)cpu_env);
    TaskState *ts = cpu->opaque;
    abi_ulong start_stack = ts->info->start_stack;
    int i;

    for (i = 0; i < 44; i++) {
      char buf[128];
      int len;
      uint64_t val = 0;

      if (i == 0) {
        /* pid */
        val = getpid();
        snprintf(buf, sizeof(buf), "%"PRId64 " ", val);
      } else if (i == 1) {
        /* app name */
        snprintf(buf, sizeof(buf), "(%s) ", ts->bprm->argv[0]);
      } else if (i == 27) {
        /* stack bottom */
        val = start_stack;
        snprintf(buf, sizeof(buf), "%"PRId64 " ", val);
      } else {
        /* for the rest, there is MasterCard */
        snprintf(buf, sizeof(buf), "0%c", i == 43 ? '\n' : ' ');
      }

      len = strlen(buf);
      if (write(fd, buf, len) != len) {
          return -1;
      }
    }

    return 0;
}

static int open_self_auxv(void *cpu_env, int fd)
{
    CPUState *cpu = ENV_GET_CPU((CPUArchState *)cpu_env);
    TaskState *ts = cpu->opaque;
    abi_ulong auxv = ts->info->saved_auxv;
    abi_ulong len = ts->info->auxv_len;
    char *ptr;

    /*
     * Auxiliary vector is stored in target process stack.
     * read in whole auxv vector and copy it to file
     */
    ptr = lock_user(VERIFY_READ, auxv, len, 0);
    if (ptr != NULL) {
        while (len > 0) {
            ssize_t r;
            r = write(fd, ptr, len);
            if (r <= 0) {
                break;
            }
            len -= r;
            ptr += r;
        }
        lseek(fd, 0, SEEK_SET);
        unlock_user(ptr, auxv, len);
    }

    return 0;
}

static int is_proc_myself(const char *filename, const char *entry)
{
    if (!strncmp(filename, "/proc/", strlen("/proc/"))) {
        filename += strlen("/proc/");
        if (!strncmp(filename, "self/", strlen("self/"))) {
            filename += strlen("self/");
        } else if (*filename >= '1' && *filename <= '9') {
            char myself[80];
            snprintf(myself, sizeof(myself), "%d/", getpid());
            if (!strncmp(filename, myself, strlen(myself))) {
                filename += strlen(myself);
            } else {
                return 0;
            }
        } else {
            return 0;
        }
        if (!strcmp(filename, entry)) {
            return 1;
        }
    }
    return 0;
}

#if defined(HOST_WORDS_BIGENDIAN) != defined(TARGET_WORDS_BIGENDIAN)
static int is_proc(const char *filename, const char *entry)
{
    return strcmp(filename, entry) == 0;
}

static int open_net_route(void *cpu_env, int fd)
{
    FILE *fp;
    char *line = NULL;
    size_t len = 0;
    ssize_t read;

    fp = fopen("/proc/net/route", "r");
    if (fp == NULL) {
        return -EACCES;
    }

    /* read header */

    read = getline(&line, &len, fp);
    dprintf(fd, "%s", line);

    /* read routes */

    while ((read = getline(&line, &len, fp)) != -1) {
        char iface[16];
        uint32_t dest, gw, mask;
        unsigned int flags, refcnt, use, metric, mtu, window, irtt;
        sscanf(line, "%s\t%08x\t%08x\t%04x\t%d\t%d\t%d\t%08x\t%d\t%u\t%u\n",
                     iface, &dest, &gw, &flags, &refcnt, &use, &metric,
                     &mask, &mtu, &window, &irtt);
        dprintf(fd, "%s\t%08x\t%08x\t%04x\t%d\t%d\t%d\t%08x\t%d\t%u\t%u\n",
                iface, tswap32(dest), tswap32(gw), flags, refcnt, use,
                metric, tswap32(mask), mtu, window, irtt);
    }

    free(line);
    fclose(fp);

    return 0;
}
#endif

static int do_openat(void *cpu_env, int dirfd, const char *pathname, int flags, mode_t mode)
{
    struct fake_open {
        const char *filename;
        int (*fill)(void *cpu_env, int fd);
        int (*cmp)(const char *s1, const char *s2);
    };
    const struct fake_open *fake_open;
    static const struct fake_open fakes[] = {
        { "maps", open_self_maps, is_proc_myself },
        { "stat", open_self_stat, is_proc_myself },
        { "auxv", open_self_auxv, is_proc_myself },
        { "cmdline", open_self_cmdline, is_proc_myself },
#if defined(HOST_WORDS_BIGENDIAN) != defined(TARGET_WORDS_BIGENDIAN)
        { "/proc/net/route", open_net_route, is_proc },
#endif
        { NULL, NULL, NULL }
    };

    if (is_proc_myself(pathname, "exe")) {
        int execfd = qemu_getauxval(AT_EXECFD);
        return execfd ? execfd : get_errno(sys_openat(dirfd, exec_path, flags, mode));
    }

    for (fake_open = fakes; fake_open->filename; fake_open++) {
        if (fake_open->cmp(pathname, fake_open->filename)) {
            break;
        }
    }

    if (fake_open->filename) {
        const char *tmpdir;
        char filename[PATH_MAX];
        int fd, r;

        /* create temporary file to map stat to */
        tmpdir = getenv("TMPDIR");
        if (!tmpdir)
            tmpdir = "/tmp";
        snprintf(filename, sizeof(filename), "%s/qemu-open.XXXXXX", tmpdir);
        fd = mkstemp(filename);
        if (fd < 0) {
            return fd;
        }
        unlink(filename);

        if ((r = fake_open->fill(cpu_env, fd))) {
            close(fd);
            return r;
        }
        lseek(fd, 0, SEEK_SET);

        return fd;
    }

    return get_errno(sys_openat(dirfd, path(pathname), flags, mode));
}

/* do_syscall() should always have a single exit point at the end so
   that actions, such as logging of syscall results, can be performed.
   All errnos that do_syscall() returns must be -TARGET_<errcode>. */
abi_long do_syscall(void *cpu_env, int num, abi_long arg1,
                    abi_long arg2, abi_long arg3, abi_long arg4,
                    abi_long arg5, abi_long arg6, abi_long arg7,
                    abi_long arg8)
{
    CPUState *cpu = ENV_GET_CPU(cpu_env);
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
        /* In old applications this may be used to implement _exit(2).
           However in threaded applictions it is used for thread termination,
           and _exit_group is used for application termination.
           Do thread termination if we have more then one thread.  */
        /* FIXME: This probably breaks if a signal arrives.  We should probably
           be disabling signals.  */
        if (CPU_NEXT(first_cpu)) {
            TaskState *ts;

            cpu_list_lock();
            /* Remove the CPU from the list.  */
            QTAILQ_REMOVE(&cpus, cpu, node);
            cpu_list_unlock();
            ts = cpu->opaque;
            if (ts->child_tidptr) {
                put_user_u32(0, ts->child_tidptr);
                sys_futex(g2h(ts->child_tidptr), FUTEX_WAKE, INT_MAX,
                          NULL, NULL, 0);
            }
            thread_cpu = NULL;
            object_unref(OBJECT(cpu));
            g_free(ts);
            pthread_exit(NULL);
        }
#ifdef TARGET_GPROF
        _mcleanup();
#endif
        gdb_exit(cpu_env, arg1);
        _exit(arg1);
        ret = 0; /* avoid warning */
        break;
    case TARGET_NR_read:
        if (arg3 == 0)
            ret = 0;
        else {
            if (!(p = lock_user(VERIFY_WRITE, arg2, arg3, 0)))
                goto efault;
            ret = get_errno(read(arg1, p, arg3));
            unlock_user(p, arg2, ret);
        }
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
        ret = get_errno(do_openat(cpu_env, AT_FDCWD, p,
                                  target_to_host_bitmask(arg2, fcntl_flags_tbl),
                                  arg3));
        unlock_user(p, arg1, 0);
        break;
    case TARGET_NR_openat:
        if (!(p = lock_user_string(arg2)))
            goto efault;
        ret = get_errno(do_openat(cpu_env, arg1, p,
                                  target_to_host_bitmask(arg3, fcntl_flags_tbl),
                                  arg4));
        unlock_user(p, arg2, 0);
        break;
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
            if (!is_error(ret) && arg2 && ret
                && put_user_s32(host_to_target_waitstatus(status), arg2))
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
#if defined(TARGET_NR_linkat)
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
                ret = get_errno(linkat(arg1, p, arg3, p2, arg5));
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
#if defined(TARGET_NR_unlinkat)
    case TARGET_NR_unlinkat:
        if (!(p = lock_user_string(arg2)))
            goto efault;
        ret = get_errno(unlinkat(arg1, p, arg3));
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
            int total_size = 0;

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
                total_size += strlen(*q) + 1;
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
                total_size += strlen(*q) + 1;
            }
            *q = NULL;

            /* This case will not be caught by the host's execve() if its
               page size is bigger than the target's. */
            if (total_size > MAX_ARG_PAGES * TARGET_PAGE_SIZE) {
                ret = -TARGET_E2BIG;
                goto execve_end;
            }
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
#if defined(TARGET_NR_mknodat)
    case TARGET_NR_mknodat:
        if (!(p = lock_user_string(arg2)))
            goto efault;
        ret = get_errno(mknodat(arg1, p, arg3, arg4));
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
#if defined(TARGET_NR_getxpid) && defined(TARGET_ALPHA)
    /* Alpha specific */
    case TARGET_NR_getxpid:
        ((CPUAlphaState *)cpu_env)->ir[IR_A4] = getppid();
        ret = get_errno(getpid());
        break;
#endif
#ifdef TARGET_NR_getpid
    case TARGET_NR_getpid:
        ret = get_errno(getpid());
        break;
#endif
    case TARGET_NR_mount:
        {
            /* need to look at the data field */
            void *p2, *p3;

            if (arg1) {
                p = lock_user_string(arg1);
                if (!p) {
                    goto efault;
                }
            } else {
                p = NULL;
            }

            p2 = lock_user_string(arg2);
            if (!p2) {
                if (arg1) {
                    unlock_user(p, arg1, 0);
                }
                goto efault;
            }

            if (arg3) {
                p3 = lock_user_string(arg3);
                if (!p3) {
                    if (arg1) {
                        unlock_user(p, arg1, 0);
                    }
                    unlock_user(p2, arg2, 0);
                    goto efault;
                }
            } else {
                p3 = NULL;
            }

            /* FIXME - arg5 should be locked, but it isn't clear how to
             * do that since it's not guaranteed to be a NULL-terminated
             * string.
             */
            if (!arg5) {
                ret = mount(p, p2, p3, (unsigned long)arg4, NULL);
            } else {
                ret = mount(p, p2, p3, (unsigned long)arg4, g2h(arg5));
            }
            ret = get_errno(ret);

            if (arg1) {
                unlock_user(p, arg1, 0);
            }
            unlock_user(p2, arg2, 0);
            if (arg3) {
                unlock_user(p3, arg3, 0);
            }
        }
        break;
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
                tbuf.actime = tswapal(target_tbuf->actime);
                tbuf.modtime = tswapal(target_tbuf->modtime);
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
#if defined(TARGET_NR_futimesat)
    case TARGET_NR_futimesat:
        {
            struct timeval *tvp, tv[2];
            if (arg3) {
                if (copy_from_user_timeval(&tv[0], arg3)
                    || copy_from_user_timeval(&tv[1],
                                              arg3 + sizeof(struct target_timeval)))
                    goto efault;
                tvp = tv;
            } else {
                tvp = NULL;
            }
            if (!(p = lock_user_string(arg2)))
                goto efault;
            ret = get_errno(futimesat(arg1, path(p), tvp));
            unlock_user(p, arg2, 0);
        }
        break;
#endif
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
        ret = get_errno(access(path(p), arg2));
        unlock_user(p, arg1, 0);
        break;
#if defined(TARGET_NR_faccessat) && defined(__NR_faccessat)
    case TARGET_NR_faccessat:
        if (!(p = lock_user_string(arg2)))
            goto efault;
        ret = get_errno(faccessat(arg1, p, arg3, 0));
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
#if defined(TARGET_NR_renameat)
    case TARGET_NR_renameat:
        {
            void *p2;
            p  = lock_user_string(arg2);
            p2 = lock_user_string(arg4);
            if (!p || !p2)
                ret = -TARGET_EFAULT;
            else
                ret = get_errno(renameat(arg1, p, arg3, p2));
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
#if defined(TARGET_NR_mkdirat)
    case TARGET_NR_mkdirat:
        if (!(p = lock_user_string(arg2)))
            goto efault;
        ret = get_errno(mkdirat(arg1, p, arg3));
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
        ret = do_pipe(cpu_env, arg1, 0, 0);
        break;
#ifdef TARGET_NR_pipe2
    case TARGET_NR_pipe2:
        ret = do_pipe(cpu_env, arg1,
                      target_to_host_bitmask(arg2, fcntl_flags_tbl), 1);
        break;
#endif
    case TARGET_NR_times:
        {
            struct target_tms *tmsp;
            struct tms tms;
            ret = get_errno(times(&tms));
            if (arg1) {
                tmsp = lock_user(VERIFY_WRITE, arg1, sizeof(struct target_tms), 0);
                if (!tmsp)
                    goto efault;
                tmsp->tms_utime = tswapal(host_to_target_clock_t(tms.tms_utime));
                tmsp->tms_stime = tswapal(host_to_target_clock_t(tms.tms_stime));
                tmsp->tms_cutime = tswapal(host_to_target_clock_t(tms.tms_cutime));
                tmsp->tms_cstime = tswapal(host_to_target_clock_t(tms.tms_cstime));
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
        if (arg1 == 0) {
            ret = get_errno(acct(NULL));
        } else {
            if (!(p = lock_user_string(arg1)))
                goto efault;
            ret = get_errno(acct(path(p)));
            unlock_user(p, arg1, 0);
        }
        break;
#ifdef TARGET_NR_umount2
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
#if defined(CONFIG_DUP3) && defined(TARGET_NR_dup3)
    case TARGET_NR_dup3:
        ret = get_errno(dup3(arg1, arg2, arg3));
        break;
#endif
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
#if defined(TARGET_ALPHA)
            struct target_sigaction act, oact, *pact = 0;
            struct target_old_sigaction *old_act;
            if (arg2) {
                if (!lock_user_struct(VERIFY_READ, old_act, arg2, 1))
                    goto efault;
                act._sa_handler = old_act->_sa_handler;
                target_siginitset(&act.sa_mask, old_act->sa_mask);
                act.sa_flags = old_act->sa_flags;
                act.sa_restorer = 0;
                unlock_user_struct(old_act, arg2, 0);
                pact = &act;
            }
            ret = get_errno(do_sigaction(arg1, pact, &oact));
            if (!is_error(ret) && arg3) {
                if (!lock_user_struct(VERIFY_WRITE, old_act, arg3, 0))
                    goto efault;
                old_act->_sa_handler = oact._sa_handler;
                old_act->sa_mask = oact.sa_mask.sig[0];
                old_act->sa_flags = oact.sa_flags;
                unlock_user_struct(old_act, arg3, 1);
            }
#elif defined(TARGET_MIPS)
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
#else
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
#endif
        }
        break;
#endif
    case TARGET_NR_rt_sigaction:
        {
#if defined(TARGET_ALPHA)
            struct target_sigaction act, oact, *pact = 0;
            struct target_rt_sigaction *rt_act;
            /* ??? arg4 == sizeof(sigset_t).  */
            if (arg2) {
                if (!lock_user_struct(VERIFY_READ, rt_act, arg2, 1))
                    goto efault;
                act._sa_handler = rt_act->_sa_handler;
                act.sa_mask = rt_act->sa_mask;
                act.sa_flags = rt_act->sa_flags;
                act.sa_restorer = arg5;
                unlock_user_struct(rt_act, arg2, 0);
                pact = &act;
            }
            ret = get_errno(do_sigaction(arg1, pact, &oact));
            if (!is_error(ret) && arg3) {
                if (!lock_user_struct(VERIFY_WRITE, rt_act, arg3, 0))
                    goto efault;
                rt_act->_sa_handler = oact._sa_handler;
                rt_act->sa_mask = oact.sa_mask;
                rt_act->sa_flags = oact.sa_flags;
                unlock_user_struct(rt_act, arg3, 1);
            }
#else
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
#endif
        }
        break;
#ifdef TARGET_NR_sgetmask /* not on alpha */
    case TARGET_NR_sgetmask:
        {
            sigset_t cur_set;
            abi_ulong target_set;
            do_sigprocmask(0, NULL, &cur_set);
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
            do_sigprocmask(0, NULL, &cur_set);
            target_to_host_old_sigset(&set, &target_set);
            sigorset(&set, &set, &cur_set);
            do_sigprocmask(SIG_SETMASK, &set, &oset);
            host_to_target_old_sigset(&target_set, &oset);
            ret = target_set;
        }
        break;
#endif
#ifdef TARGET_NR_sigprocmask
    case TARGET_NR_sigprocmask:
        {
#if defined(TARGET_ALPHA)
            sigset_t set, oldset;
            abi_ulong mask;
            int how;

            switch (arg1) {
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
            mask = arg2;
            target_to_host_old_sigset(&set, &mask);

            ret = get_errno(do_sigprocmask(how, &set, &oldset));
            if (!is_error(ret)) {
                host_to_target_old_sigset(&mask, &oldset);
                ret = mask;
                ((CPUAlphaState *)cpu_env)->ir[IR_V0] = 0; /* force no error */
            }
#else
            sigset_t set, oldset, *set_ptr;
            int how;

            if (arg2) {
                switch (arg1) {
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
            ret = get_errno(do_sigprocmask(how, set_ptr, &oldset));
            if (!is_error(ret) && arg3) {
                if (!(p = lock_user(VERIFY_WRITE, arg3, sizeof(target_sigset_t), 0)))
                    goto efault;
                host_to_target_old_sigset(p, &oldset);
                unlock_user(p, arg3, sizeof(target_sigset_t));
            }
#endif
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
            ret = get_errno(do_sigprocmask(how, set_ptr, &oldset));
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
#if defined(TARGET_ALPHA)
            abi_ulong mask = arg1;
            target_to_host_old_sigset(&set, &mask);
#else
            if (!(p = lock_user(VERIFY_READ, arg1, sizeof(target_sigset_t), 1)))
                goto efault;
            target_to_host_old_sigset(&set, p);
            unlock_user(p, arg1, 0);
#endif
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
            if (!is_error(ret)) {
                if (arg2) {
                    p = lock_user(VERIFY_WRITE, arg2, sizeof(target_siginfo_t),
                                  0);
                    if (!p) {
                        goto efault;
                    }
                    host_to_target_siginfo(p, &uinfo);
                    unlock_user(p, arg2, sizeof(target_siginfo_t));
                }
                ret = host_to_target_signal(ret);
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
            int resource = target_to_host_resource(arg1);
            struct target_rlimit *target_rlim;
            struct rlimit rlim;
            if (!lock_user_struct(VERIFY_READ, target_rlim, arg2, 1))
                goto efault;
            rlim.rlim_cur = target_to_host_rlim(target_rlim->rlim_cur);
            rlim.rlim_max = target_to_host_rlim(target_rlim->rlim_max);
            unlock_user_struct(target_rlim, arg2, 0);
            ret = get_errno(setrlimit(resource, &rlim));
        }
        break;
    case TARGET_NR_getrlimit:
        {
            int resource = target_to_host_resource(arg1);
            struct target_rlimit *target_rlim;
            struct rlimit rlim;

            ret = get_errno(getrlimit(resource, &rlim));
            if (!is_error(ret)) {
                if (!lock_user_struct(VERIFY_WRITE, target_rlim, arg2, 0))
                    goto efault;
                target_rlim->rlim_cur = host_to_target_rlim(rlim.rlim_cur);
                target_rlim->rlim_max = host_to_target_rlim(rlim.rlim_max);
                unlock_user_struct(target_rlim, arg2, 1);
            }
        }
        break;
    case TARGET_NR_getrusage:
        {
            struct rusage rusage;
            ret = get_errno(getrusage(arg1, &rusage));
            if (!is_error(ret)) {
                ret = host_to_target_rusage(arg2, &rusage);
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
            struct timeval tv, *ptv = NULL;
            struct timezone tz, *ptz = NULL;

            if (arg1) {
                if (copy_from_user_timeval(&tv, arg1)) {
                    goto efault;
                }
                ptv = &tv;
            }

            if (arg2) {
                if (copy_from_user_timezone(&tz, arg2)) {
                    goto efault;
                }
                ptz = &tz;
            }

            ret = get_errno(settimeofday(ptv, ptz));
        }
        break;
#if defined(TARGET_NR_select)
    case TARGET_NR_select:
#if defined(TARGET_S390X) || defined(TARGET_ALPHA)
        ret = do_select(arg1, arg2, arg3, arg4, arg5);
#else
        {
            struct target_sel_arg_struct *sel;
            abi_ulong inp, outp, exp, tvp;
            long nsel;

            if (!lock_user_struct(VERIFY_READ, sel, arg1, 1))
                goto efault;
            nsel = tswapal(sel->n);
            inp = tswapal(sel->inp);
            outp = tswapal(sel->outp);
            exp = tswapal(sel->exp);
            tvp = tswapal(sel->tvp);
            unlock_user_struct(sel, arg1, 0);
            ret = do_select(nsel, inp, outp, exp, tvp);
        }
#endif
        break;
#endif
#ifdef TARGET_NR_pselect6
    case TARGET_NR_pselect6:
        {
            abi_long rfd_addr, wfd_addr, efd_addr, n, ts_addr;
            fd_set rfds, wfds, efds;
            fd_set *rfds_ptr, *wfds_ptr, *efds_ptr;
            struct timespec ts, *ts_ptr;

            /*
             * The 6th arg is actually two args smashed together,
             * so we cannot use the C library.
             */
            sigset_t set;
            struct {
                sigset_t *set;
                size_t size;
            } sig, *sig_ptr;

            abi_ulong arg_sigset, arg_sigsize, *arg7;
            target_sigset_t *target_sigset;

            n = arg1;
            rfd_addr = arg2;
            wfd_addr = arg3;
            efd_addr = arg4;
            ts_addr = arg5;

            ret = copy_from_user_fdset_ptr(&rfds, &rfds_ptr, rfd_addr, n);
            if (ret) {
                goto fail;
            }
            ret = copy_from_user_fdset_ptr(&wfds, &wfds_ptr, wfd_addr, n);
            if (ret) {
                goto fail;
            }
            ret = copy_from_user_fdset_ptr(&efds, &efds_ptr, efd_addr, n);
            if (ret) {
                goto fail;
            }

            /*
             * This takes a timespec, and not a timeval, so we cannot
             * use the do_select() helper ...
             */
            if (ts_addr) {
                if (target_to_host_timespec(&ts, ts_addr)) {
                    goto efault;
                }
                ts_ptr = &ts;
            } else {
                ts_ptr = NULL;
            }

            /* Extract the two packed args for the sigset */
            if (arg6) {
                sig_ptr = &sig;
                sig.size = _NSIG / 8;

                arg7 = lock_user(VERIFY_READ, arg6, sizeof(*arg7) * 2, 1);
                if (!arg7) {
                    goto efault;
                }
                arg_sigset = tswapal(arg7[0]);
                arg_sigsize = tswapal(arg7[1]);
                unlock_user(arg7, arg6, 0);

                if (arg_sigset) {
                    sig.set = &set;
                    if (arg_sigsize != sizeof(*target_sigset)) {
                        /* Like the kernel, we enforce correct size sigsets */
                        ret = -TARGET_EINVAL;
                        goto fail;
                    }
                    target_sigset = lock_user(VERIFY_READ, arg_sigset,
                                              sizeof(*target_sigset), 1);
                    if (!target_sigset) {
                        goto efault;
                    }
                    target_to_host_sigset(&set, target_sigset);
                    unlock_user(target_sigset, arg_sigset, 0);
                } else {
                    sig.set = NULL;
                }
            } else {
                sig_ptr = NULL;
            }

            ret = get_errno(sys_pselect6(n, rfds_ptr, wfds_ptr, efds_ptr,
                                         ts_ptr, sig_ptr));

            if (!is_error(ret)) {
                if (rfd_addr && copy_to_user_fdset(rfd_addr, &rfds, n))
                    goto efault;
                if (wfd_addr && copy_to_user_fdset(wfd_addr, &wfds, n))
                    goto efault;
                if (efd_addr && copy_to_user_fdset(efd_addr, &efds, n))
                    goto efault;

                if (ts_addr && host_to_target_timespec(ts_addr, &ts))
                    goto efault;
            }
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
#if defined(TARGET_NR_symlinkat)
    case TARGET_NR_symlinkat:
        {
            void *p2;
            p  = lock_user_string(arg1);
            p2 = lock_user_string(arg3);
            if (!p || !p2)
                ret = -TARGET_EFAULT;
            else
                ret = get_errno(symlinkat(p, arg2, p2));
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
            if (!p || !p2) {
                ret = -TARGET_EFAULT;
            } else if (!arg3) {
                /* Short circuit this for the magic exe check. */
                ret = -TARGET_EINVAL;
            } else if (is_proc_myself((const char *)p, "exe")) {
                char real[PATH_MAX], *temp;
                temp = realpath(exec_path, real);
                /* Return value is # of bytes that we wrote to the buffer. */
                if (temp == NULL) {
                    ret = get_errno(-1);
                } else {
                    /* Don't worry about sign mismatch as earlier mapping
                     * logic would have thrown a bad address error. */
                    ret = MIN(strlen(real), arg3);
                    /* We cannot NUL terminate the string. */
                    memcpy(p2, real, ret);
                }
            } else {
                ret = get_errno(readlink(path(p), p2, arg3));
            }
            unlock_user(p2, arg2, ret);
            unlock_user(p, arg1, 0);
        }
        break;
#if defined(TARGET_NR_readlinkat)
    case TARGET_NR_readlinkat:
        {
            void *p2;
            p  = lock_user_string(arg2);
            p2 = lock_user(VERIFY_WRITE, arg3, arg4, 0);
            if (!p || !p2) {
                ret = -TARGET_EFAULT;
            } else if (is_proc_myself((const char *)p, "exe")) {
                char real[PATH_MAX], *temp;
                temp = realpath(exec_path, real);
                ret = temp == NULL ? get_errno(-1) : strlen(real) ;
                snprintf((char *)p2, arg4, "%s", real);
            } else {
                ret = get_errno(readlinkat(arg1, path(p), p2, arg4));
            }
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
        if (arg3 == LINUX_REBOOT_CMD_RESTART2) {
           /* arg4 must be ignored in all other cases */
           p = lock_user_string(arg4);
           if (!p) {
              goto efault;
           }
           ret = get_errno(reboot(arg1, arg2, arg3, p));
           unlock_user(p, arg4, 0);
        } else {
           ret = get_errno(reboot(arg1, arg2, arg3, NULL));
        }
        break;
#ifdef TARGET_NR_readdir
    case TARGET_NR_readdir:
        goto unimplemented;
#endif
#ifdef TARGET_NR_mmap
    case TARGET_NR_mmap:
#if (defined(TARGET_I386) && defined(TARGET_ABI32)) || \
    (defined(TARGET_ARM) && defined(TARGET_ABI32)) || \
    defined(TARGET_M68K) || defined(TARGET_CRIS) || defined(TARGET_MICROBLAZE) \
    || defined(TARGET_S390X)
        {
            abi_ulong *v;
            abi_ulong v1, v2, v3, v4, v5, v6;
            if (!(v = lock_user(VERIFY_READ, arg1, 6 * sizeof(abi_ulong), 1)))
                goto efault;
            v1 = tswapal(v[0]);
            v2 = tswapal(v[1]);
            v3 = tswapal(v[2]);
            v4 = tswapal(v[3]);
            v5 = tswapal(v[4]);
            v6 = tswapal(v[5]);
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
        {
            TaskState *ts = cpu->opaque;
            /* Special hack to detect libc making the stack executable.  */
            if ((arg3 & PROT_GROWSDOWN)
                && arg1 >= ts->info->stack_limit
                && arg1 <= ts->info->start_stack) {
                arg3 &= ~PROT_GROWSDOWN;
                arg2 = arg2 + arg1 - ts->info->stack_limit;
                arg1 = ts->info->stack_limit;
            }
        }
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
#if defined(TARGET_NR_fchmodat)
    case TARGET_NR_fchmodat:
        if (!(p = lock_user_string(arg2)))
            goto efault;
        ret = get_errno(fchmodat(arg1, p, arg3, 0));
        unlock_user(p, arg2, 0);
        break;
#endif
    case TARGET_NR_getpriority:
        /* Note that negative values are valid for getpriority, so we must
           differentiate based on errno settings.  */
        errno = 0;
        ret = getpriority(arg1, arg2);
        if (ret == -1 && errno != 0) {
            ret = -host_to_target_errno(errno);
            break;
        }
#ifdef TARGET_ALPHA
        /* Return value is the unbiased priority.  Signal no error.  */
        ((CPUAlphaState *)cpu_env)->ir[IR_V0] = 0;
#else
        /* Return value is a biased priority to avoid negative numbers.  */
        ret = 20 - ret;
#endif
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
            __put_user(stfs.f_frsize, &target_stfs->f_frsize);
            memset(target_stfs->f_spare, 0, sizeof(target_stfs->f_spare));
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
            __put_user(stfs.f_frsize, &target_stfs->f_frsize);
            memset(target_stfs->f_spare, 0, sizeof(target_stfs->f_spare));
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
        ret = do_accept4(arg1, arg2, arg3, 0);
        break;
#endif
#ifdef TARGET_NR_accept4
    case TARGET_NR_accept4:
#ifdef CONFIG_ACCEPT4
        ret = do_accept4(arg1, arg2, arg3, arg4);
#else
        goto unimplemented;
#endif
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
#ifdef TARGET_NR_sendmmsg
    case TARGET_NR_sendmmsg:
        ret = do_sendrecvmmsg(arg1, arg2, arg3, arg4, 1);
        break;
    case TARGET_NR_recvmmsg:
        ret = do_sendrecvmmsg(arg1, arg2, arg3, arg4, 0);
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
                memset(target_st, 0, sizeof(*target_st));
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
        ret = do_syscall(cpu_env, arg1 & 0xffff, arg2, arg3, arg4, arg5,
                         arg6, arg7, arg8, 0);
        break;
#endif
    case TARGET_NR_wait4:
        {
            int status;
            abi_long status_ptr = arg2;
            struct rusage rusage, *rusage_ptr;
            abi_ulong target_rusage = arg4;
            abi_long rusage_err;
            if (target_rusage)
                rusage_ptr = &rusage;
            else
                rusage_ptr = NULL;
            ret = get_errno(wait4(arg1, &status, arg3, rusage_ptr));
            if (!is_error(ret)) {
                if (status_ptr && ret) {
                    status = host_to_target_waitstatus(status);
                    if (put_user_s32(status, status_ptr))
                        goto efault;
                }
                if (target_rusage) {
                    rusage_err = host_to_target_rusage(target_rusage, &rusage);
                    if (rusage_err) {
                        ret = rusage_err;
                    }
                }
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
#ifdef TARGET_NR_semget
    case TARGET_NR_semget:
        ret = get_errno(semget(arg1, arg2, arg3));
        break;
#endif
#ifdef TARGET_NR_semop
    case TARGET_NR_semop:
        ret = do_semop(arg1, arg2, arg3);
        break;
#endif
#ifdef TARGET_NR_semctl
    case TARGET_NR_semctl:
        ret = do_semctl(arg1, arg2, arg3, (union target_semun)(abi_ulong)arg4);
        break;
#endif
#ifdef TARGET_NR_msgctl
    case TARGET_NR_msgctl:
        ret = do_msgctl(arg1, arg2, arg3);
        break;
#endif
#ifdef TARGET_NR_msgget
    case TARGET_NR_msgget:
        ret = get_errno(msgget(arg1, arg2));
        break;
#endif
#ifdef TARGET_NR_msgrcv
    case TARGET_NR_msgrcv:
        ret = do_msgrcv(arg1, arg2, arg3, arg4, arg5);
        break;
#endif
#ifdef TARGET_NR_msgsnd
    case TARGET_NR_msgsnd:
        ret = do_msgsnd(arg1, arg2, arg3, arg4);
        break;
#endif
#ifdef TARGET_NR_shmget
    case TARGET_NR_shmget:
        ret = get_errno(shmget(arg1, arg2, arg3));
        break;
#endif
#ifdef TARGET_NR_shmctl
    case TARGET_NR_shmctl:
        ret = do_shmctl(arg1, arg2, arg3);
        break;
#endif
#ifdef TARGET_NR_shmat
    case TARGET_NR_shmat:
        ret = do_shmat(arg1, arg2, arg3);
        break;
#endif
#ifdef TARGET_NR_shmdt
    case TARGET_NR_shmdt:
        ret = do_shmdt(arg1);
        break;
#endif
    case TARGET_NR_fsync:
        ret = get_errno(fsync(arg1));
        break;
    case TARGET_NR_clone:
        /* Linux manages to have three different orderings for its
         * arguments to clone(); the BACKWARDS and BACKWARDS2 defines
         * match the kernel's CONFIG_CLONE_* settings.
         * Microblaze is further special in that it uses a sixth
         * implicit argument to clone for the TLS pointer.
         */
#if defined(TARGET_MICROBLAZE)
        ret = get_errno(do_fork(cpu_env, arg1, arg2, arg4, arg6, arg5));
#elif defined(TARGET_CLONE_BACKWARDS)
        ret = get_errno(do_fork(cpu_env, arg1, arg2, arg3, arg4, arg5));
#elif defined(TARGET_CLONE_BACKWARDS2)
        ret = get_errno(do_fork(cpu_env, arg2, arg1, arg3, arg5, arg4));
#else
        ret = get_errno(do_fork(cpu_env, arg1, arg2, arg3, arg5, arg4));
#endif
        break;
#ifdef __NR_exit_group
        /* new thread calls */
    case TARGET_NR_exit_group:
#ifdef TARGET_GPROF
        _mcleanup();
#endif
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
                strcpy (buf->machine, cpu_to_uname_machine(cpu_env));
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
            int64_t res;
#if !defined(__NR_llseek)
            res = lseek(arg1, ((uint64_t)arg2 << 32) | arg3, arg5);
            if (res == -1) {
                ret = get_errno(res);
            } else {
                ret = 0;
            }
#else
            ret = get_errno(_llseek(arg1, arg2, arg3, &res, arg5));
#endif
            if ((ret == 0) && put_user_s64(res, arg4)) {
                goto efault;
            }
        }
        break;
#endif
    case TARGET_NR_getdents:
#ifdef __NR_getdents
#if TARGET_ABI_BITS == 32 && HOST_LONG_BITS == 64
        {
            struct target_dirent *target_dirp;
            struct linux_dirent *dirp;
            abi_long count = arg3;

	    dirp = malloc(count);
	    if (!dirp) {
                ret = -TARGET_ENOMEM;
                goto fail;
            }

            ret = get_errno(sys_getdents(arg1, dirp, count));
            if (!is_error(ret)) {
                struct linux_dirent *de;
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
                    tnamelen = reclen - offsetof(struct linux_dirent, d_name);
                    assert(tnamelen >= 0);
                    treclen = tnamelen + offsetof(struct target_dirent, d_name);
                    assert(count1 + treclen <= count);
                    tde->d_reclen = tswap16(treclen);
                    tde->d_ino = tswapal(de->d_ino);
                    tde->d_off = tswapal(de->d_off);
                    memcpy(tde->d_name, de->d_name, tnamelen);
                    de = (struct linux_dirent *)((char *)de + reclen);
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
            struct linux_dirent *dirp;
            abi_long count = arg3;

            if (!(dirp = lock_user(VERIFY_WRITE, arg2, count, 0)))
                goto efault;
            ret = get_errno(sys_getdents(arg1, dirp, count));
            if (!is_error(ret)) {
                struct linux_dirent *de;
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
                    de = (struct linux_dirent *)((char *)de + reclen);
                    len -= reclen;
                }
            }
            unlock_user(dirp, arg2, ret);
        }
#endif
#else
        /* Implement getdents in terms of getdents64 */
        {
            struct linux_dirent64 *dirp;
            abi_long count = arg3;

            dirp = lock_user(VERIFY_WRITE, arg2, count, 0);
            if (!dirp) {
                goto efault;
            }
            ret = get_errno(sys_getdents64(arg1, dirp, count));
            if (!is_error(ret)) {
                /* Convert the dirent64 structs to target dirent.  We do this
                 * in-place, since we can guarantee that a target_dirent is no
                 * larger than a dirent64; however this means we have to be
                 * careful to read everything before writing in the new format.
                 */
                struct linux_dirent64 *de;
                struct target_dirent *tde;
                int len = ret;
                int tlen = 0;

                de = dirp;
                tde = (struct target_dirent *)dirp;
                while (len > 0) {
                    int namelen, treclen;
                    int reclen = de->d_reclen;
                    uint64_t ino = de->d_ino;
                    int64_t off = de->d_off;
                    uint8_t type = de->d_type;

                    namelen = strlen(de->d_name);
                    treclen = offsetof(struct target_dirent, d_name)
                        + namelen + 2;
                    treclen = QEMU_ALIGN_UP(treclen, sizeof(abi_long));

                    memmove(tde->d_name, de->d_name, namelen + 1);
                    tde->d_ino = tswapal(ino);
                    tde->d_off = tswapal(off);
                    tde->d_reclen = tswap16(treclen);
                    /* The target_dirent type is in what was formerly a padding
                     * byte at the end of the structure:
                     */
                    *(((char *)tde) + treclen - 1) = type;

                    de = (struct linux_dirent64 *)((char *)de + reclen);
                    tde = (struct target_dirent *)((char *)tde + treclen);
                    len -= reclen;
                    tlen += treclen;
                }
                ret = tlen;
            }
            unlock_user(dirp, arg2, ret);
        }
#endif
        break;
#if defined(TARGET_NR_getdents64) && defined(__NR_getdents64)
    case TARGET_NR_getdents64:
        {
            struct linux_dirent64 *dirp;
            abi_long count = arg3;
            if (!(dirp = lock_user(VERIFY_WRITE, arg2, count, 0)))
                goto efault;
            ret = get_errno(sys_getdents64(arg1, dirp, count));
            if (!is_error(ret)) {
                struct linux_dirent64 *de;
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
                    de = (struct linux_dirent64 *)((char *)de + reclen);
                    len -= reclen;
                }
            }
            unlock_user(dirp, arg2, ret);
        }
        break;
#endif /* TARGET_NR_getdents64 */
#if defined(TARGET_NR__newselect)
    case TARGET_NR__newselect:
        ret = do_select(arg1, arg2, arg3, arg4, arg5);
        break;
#endif
#if defined(TARGET_NR_poll) || defined(TARGET_NR_ppoll)
# ifdef TARGET_NR_poll
    case TARGET_NR_poll:
# endif
# ifdef TARGET_NR_ppoll
    case TARGET_NR_ppoll:
# endif
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

# ifdef TARGET_NR_ppoll
            if (num == TARGET_NR_ppoll) {
                struct timespec _timeout_ts, *timeout_ts = &_timeout_ts;
                target_sigset_t *target_set;
                sigset_t _set, *set = &_set;

                if (arg3) {
                    if (target_to_host_timespec(timeout_ts, arg3)) {
                        unlock_user(target_pfd, arg1, 0);
                        goto efault;
                    }
                } else {
                    timeout_ts = NULL;
                }

                if (arg4) {
                    target_set = lock_user(VERIFY_READ, arg4, sizeof(target_sigset_t), 1);
                    if (!target_set) {
                        unlock_user(target_pfd, arg1, 0);
                        goto efault;
                    }
                    target_to_host_sigset(set, target_set);
                } else {
                    set = NULL;
                }

                ret = get_errno(sys_ppoll(pfd, nfds, timeout_ts, set, _NSIG/8));

                if (!is_error(ret) && arg3) {
                    host_to_target_timespec(arg3, timeout_ts);
                }
                if (arg4) {
                    unlock_user(target_set, arg4, 0);
                }
            } else
# endif
                ret = get_errno(poll(pfd, nfds, timeout));

            if (!is_error(ret)) {
                for(i = 0; i < nfds; i++) {
                    target_pfd[i].revents = tswap16(pfd[i].revents);
                }
            }
            unlock_user(target_pfd, arg1, sizeof(struct target_pollfd) * nfds);
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
            struct iovec *vec = lock_iovec(VERIFY_WRITE, arg2, arg3, 0);
            if (vec != NULL) {
                ret = get_errno(readv(arg1, vec, arg3));
                unlock_iovec(vec, arg2, arg3, 1);
            } else {
                ret = -host_to_target_errno(errno);
            }
        }
        break;
    case TARGET_NR_writev:
        {
            struct iovec *vec = lock_iovec(VERIFY_READ, arg2, arg3, 1);
            if (vec != NULL) {
                ret = get_errno(writev(arg1, vec, arg3));
                unlock_iovec(vec, arg2, arg3, 0);
            } else {
                ret = -host_to_target_errno(errno);
            }
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
    case TARGET_NR_sched_getaffinity:
        {
            unsigned int mask_size;
            unsigned long *mask;

            /*
             * sched_getaffinity needs multiples of ulong, so need to take
             * care of mismatches between target ulong and host ulong sizes.
             */
            if (arg2 & (sizeof(abi_ulong) - 1)) {
                ret = -TARGET_EINVAL;
                break;
            }
            mask_size = (arg2 + (sizeof(*mask) - 1)) & ~(sizeof(*mask) - 1);

            mask = alloca(mask_size);
            ret = get_errno(sys_sched_getaffinity(arg1, mask_size, mask));

            if (!is_error(ret)) {
                if (ret > arg2) {
                    /* More data returned than the caller's buffer will fit.
                     * This only happens if sizeof(abi_long) < sizeof(long)
                     * and the caller passed us a buffer holding an odd number
                     * of abi_longs. If the host kernel is actually using the
                     * extra 4 bytes then fail EINVAL; otherwise we can just
                     * ignore them and only copy the interesting part.
                     */
                    int numcpus = sysconf(_SC_NPROCESSORS_CONF);
                    if (numcpus > arg2 * 8) {
                        ret = -TARGET_EINVAL;
                        break;
                    }
                    ret = arg2;
                }

                if (copy_to_user(arg3, mask, ret)) {
                    goto efault;
                }
            }
        }
        break;
    case TARGET_NR_sched_setaffinity:
        {
            unsigned int mask_size;
            unsigned long *mask;

            /*
             * sched_setaffinity needs multiples of ulong, so need to take
             * care of mismatches between target ulong and host ulong sizes.
             */
            if (arg2 & (sizeof(abi_ulong) - 1)) {
                ret = -TARGET_EINVAL;
                break;
            }
            mask_size = (arg2 + (sizeof(*mask) - 1)) & ~(sizeof(*mask) - 1);

            mask = alloca(mask_size);
            if (!lock_user_struct(VERIFY_READ, p, arg3, 1)) {
                goto efault;
            }
            memcpy(mask, p, arg2);
            unlock_user_struct(p, arg2, 0);

            ret = get_errno(sys_sched_setaffinity(arg1, mask_size, mask));
        }
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
        switch (arg1) {
        case PR_GET_PDEATHSIG:
        {
            int deathsig;
            ret = get_errno(prctl(arg1, &deathsig, arg3, arg4, arg5));
            if (!is_error(ret) && arg2
                && put_user_ual(deathsig, arg2)) {
                goto efault;
            }
            break;
        }
#ifdef PR_GET_NAME
        case PR_GET_NAME:
        {
            void *name = lock_user(VERIFY_WRITE, arg2, 16, 1);
            if (!name) {
                goto efault;
            }
            ret = get_errno(prctl(arg1, (unsigned long)name,
                                  arg3, arg4, arg5));
            unlock_user(name, arg2, 16);
            break;
        }
        case PR_SET_NAME:
        {
            void *name = lock_user(VERIFY_READ, arg2, 16, 1);
            if (!name) {
                goto efault;
            }
            ret = get_errno(prctl(arg1, (unsigned long)name,
                                  arg3, arg4, arg5));
            unlock_user(name, arg2, 0);
            break;
        }
#endif
        default:
            /* Most prctl options have no pointer arguments */
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
#ifdef TARGET_NR_pread64
    case TARGET_NR_pread64:
        if (regpairs_aligned(cpu_env)) {
            arg4 = arg5;
            arg5 = arg6;
        }
        if (!(p = lock_user(VERIFY_WRITE, arg2, arg3, 0)))
            goto efault;
        ret = get_errno(pread64(arg1, p, arg3, target_offset64(arg4, arg5)));
        unlock_user(p, arg2, ret);
        break;
    case TARGET_NR_pwrite64:
        if (regpairs_aligned(cpu_env)) {
            arg4 = arg5;
            arg5 = arg6;
        }
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
    case TARGET_NR_capset:
    {
        struct target_user_cap_header *target_header;
        struct target_user_cap_data *target_data = NULL;
        struct __user_cap_header_struct header;
        struct __user_cap_data_struct data[2];
        struct __user_cap_data_struct *dataptr = NULL;
        int i, target_datalen;
        int data_items = 1;

        if (!lock_user_struct(VERIFY_WRITE, target_header, arg1, 1)) {
            goto efault;
        }
        header.version = tswap32(target_header->version);
        header.pid = tswap32(target_header->pid);

        if (header.version != _LINUX_CAPABILITY_VERSION) {
            /* Version 2 and up takes pointer to two user_data structs */
            data_items = 2;
        }

        target_datalen = sizeof(*target_data) * data_items;

        if (arg2) {
            if (num == TARGET_NR_capget) {
                target_data = lock_user(VERIFY_WRITE, arg2, target_datalen, 0);
            } else {
                target_data = lock_user(VERIFY_READ, arg2, target_datalen, 1);
            }
            if (!target_data) {
                unlock_user_struct(target_header, arg1, 0);
                goto efault;
            }

            if (num == TARGET_NR_capset) {
                for (i = 0; i < data_items; i++) {
                    data[i].effective = tswap32(target_data[i].effective);
                    data[i].permitted = tswap32(target_data[i].permitted);
                    data[i].inheritable = tswap32(target_data[i].inheritable);
                }
            }

            dataptr = data;
        }

        if (num == TARGET_NR_capget) {
            ret = get_errno(capget(&header, dataptr));
        } else {
            ret = get_errno(capset(&header, dataptr));
        }

        /* The kernel always updates version for both capget and capset */
        target_header->version = tswap32(header.version);
        unlock_user_struct(target_header, arg1, 1);

        if (arg2) {
            if (num == TARGET_NR_capget) {
                for (i = 0; i < data_items; i++) {
                    target_data[i].effective = tswap32(data[i].effective);
                    target_data[i].permitted = tswap32(data[i].permitted);
                    target_data[i].inheritable = tswap32(data[i].inheritable);
                }
                unlock_user(target_data, arg2, target_datalen);
            } else {
                unlock_user(target_data, arg2, 0);
            }
        }
        break;
    }
    case TARGET_NR_sigaltstack:
#if defined(TARGET_I386) || defined(TARGET_ARM) || defined(TARGET_MIPS) || \
    defined(TARGET_SPARC) || defined(TARGET_PPC) || defined(TARGET_ALPHA) || \
    defined(TARGET_M68K) || defined(TARGET_S390X) || defined(TARGET_OPENRISC)
        ret = do_sigaltstack(arg1, arg2, get_sp_from_cpustate((CPUArchState *)cpu_env));
        break;
#else
        goto unimplemented;
#endif

#ifdef CONFIG_SENDFILE
    case TARGET_NR_sendfile:
    {
        off_t *offp = NULL;
        off_t off;
        if (arg3) {
            ret = get_user_sal(off, arg3);
            if (is_error(ret)) {
                break;
            }
            offp = &off;
        }
        ret = get_errno(sendfile(arg1, arg2, offp, arg4));
        if (!is_error(ret) && arg3) {
            abi_long ret2 = put_user_sal(off, arg3);
            if (is_error(ret2)) {
                ret = ret2;
            }
        }
        break;
    }
#ifdef TARGET_NR_sendfile64
    case TARGET_NR_sendfile64:
    {
        off_t *offp = NULL;
        off_t off;
        if (arg3) {
            ret = get_user_s64(off, arg3);
            if (is_error(ret)) {
                break;
            }
            offp = &off;
        }
        ret = get_errno(sendfile(arg1, arg2, offp, arg4));
        if (!is_error(ret) && arg3) {
            abi_long ret2 = put_user_s64(off, arg3);
            if (is_error(ret2)) {
                ret = ret2;
            }
        }
        break;
    }
#endif
#else
    case TARGET_NR_sendfile:
#ifdef TARGET_NR_sendfile64
    case TARGET_NR_sendfile64:
#endif
        goto unimplemented;
#endif

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
	int resource = target_to_host_resource(arg1);
	ret = get_errno(getrlimit(resource, &rlim));
	if (!is_error(ret)) {
	    struct target_rlimit *target_rlim;
            if (!lock_user_struct(VERIFY_WRITE, target_rlim, arg2, 0))
                goto efault;
	    target_rlim->rlim_cur = host_to_target_rlim(rlim.rlim_cur);
	    target_rlim->rlim_max = host_to_target_rlim(rlim.rlim_max);
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
        if (!is_error(ret))
            ret = host_to_target_stat64(cpu_env, arg2, &st);
        break;
#endif
#ifdef TARGET_NR_lstat64
    case TARGET_NR_lstat64:
        if (!(p = lock_user_string(arg1)))
            goto efault;
        ret = get_errno(lstat(path(p), &st));
        unlock_user(p, arg1, 0);
        if (!is_error(ret))
            ret = host_to_target_stat64(cpu_env, arg2, &st);
        break;
#endif
#ifdef TARGET_NR_fstat64
    case TARGET_NR_fstat64:
        ret = get_errno(fstat(arg1, &st));
        if (!is_error(ret))
            ret = host_to_target_stat64(cpu_env, arg2, &st);
        break;
#endif
#if (defined(TARGET_NR_fstatat64) || defined(TARGET_NR_newfstatat))
#ifdef TARGET_NR_fstatat64
    case TARGET_NR_fstatat64:
#endif
#ifdef TARGET_NR_newfstatat
    case TARGET_NR_newfstatat:
#endif
        if (!(p = lock_user_string(arg2)))
            goto efault;
        ret = get_errno(fstatat(arg1, path(p), &st, arg4));
        if (!is_error(ret))
            ret = host_to_target_stat64(cpu_env, arg3, &st);
        break;
#endif
    case TARGET_NR_lchown:
        if (!(p = lock_user_string(arg1)))
            goto efault;
        ret = get_errno(lchown(p, low2highuid(arg2), low2highgid(arg3)));
        unlock_user(p, arg1, 0);
        break;
#ifdef TARGET_NR_getuid
    case TARGET_NR_getuid:
        ret = get_errno(high2lowuid(getuid()));
        break;
#endif
#ifdef TARGET_NR_getgid
    case TARGET_NR_getgid:
        ret = get_errno(high2lowgid(getgid()));
        break;
#endif
#ifdef TARGET_NR_geteuid
    case TARGET_NR_geteuid:
        ret = get_errno(high2lowuid(geteuid()));
        break;
#endif
#ifdef TARGET_NR_getegid
    case TARGET_NR_getegid:
        ret = get_errno(high2lowgid(getegid()));
        break;
#endif
    case TARGET_NR_setreuid:
        ret = get_errno(setreuid(low2highuid(arg1), low2highuid(arg2)));
        break;
    case TARGET_NR_setregid:
        ret = get_errno(setregid(low2highgid(arg1), low2highgid(arg2)));
        break;
    case TARGET_NR_getgroups:
        {
            int gidsetsize = arg1;
            target_id *target_grouplist;
            gid_t *grouplist;
            int i;

            grouplist = alloca(gidsetsize * sizeof(gid_t));
            ret = get_errno(getgroups(gidsetsize, grouplist));
            if (gidsetsize == 0)
                break;
            if (!is_error(ret)) {
                target_grouplist = lock_user(VERIFY_WRITE, arg2, gidsetsize * sizeof(target_id), 0);
                if (!target_grouplist)
                    goto efault;
                for(i = 0;i < ret; i++)
                    target_grouplist[i] = tswapid(high2lowgid(grouplist[i]));
                unlock_user(target_grouplist, arg2, gidsetsize * sizeof(target_id));
            }
        }
        break;
    case TARGET_NR_setgroups:
        {
            int gidsetsize = arg1;
            target_id *target_grouplist;
            gid_t *grouplist = NULL;
            int i;
            if (gidsetsize) {
                grouplist = alloca(gidsetsize * sizeof(gid_t));
                target_grouplist = lock_user(VERIFY_READ, arg2, gidsetsize * sizeof(target_id), 1);
                if (!target_grouplist) {
                    ret = -TARGET_EFAULT;
                    goto fail;
                }
                for (i = 0; i < gidsetsize; i++) {
                    grouplist[i] = low2highgid(tswapid(target_grouplist[i]));
                }
                unlock_user(target_grouplist, arg2, 0);
            }
            ret = get_errno(setgroups(gidsetsize, grouplist));
        }
        break;
    case TARGET_NR_fchown:
        ret = get_errno(fchown(arg1, low2highuid(arg2), low2highgid(arg3)));
        break;
#if defined(TARGET_NR_fchownat)
    case TARGET_NR_fchownat:
        if (!(p = lock_user_string(arg2))) 
            goto efault;
        ret = get_errno(fchownat(arg1, p, low2highuid(arg3),
                                 low2highgid(arg4), arg5));
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
                if (put_user_id(high2lowuid(ruid), arg1)
                    || put_user_id(high2lowuid(euid), arg2)
                    || put_user_id(high2lowuid(suid), arg3))
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
                if (put_user_id(high2lowgid(rgid), arg1)
                    || put_user_id(high2lowgid(egid), arg2)
                    || put_user_id(high2lowgid(sgid), arg3))
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

#if defined(TARGET_NR_getxuid) && defined(TARGET_ALPHA)
   /* Alpha specific */
    case TARGET_NR_getxuid:
         {
            uid_t euid;
            euid=geteuid();
            ((CPUAlphaState *)cpu_env)->ir[IR_A4]=euid;
         }
        ret = get_errno(getuid());
        break;
#endif
#if defined(TARGET_NR_getxgid) && defined(TARGET_ALPHA)
   /* Alpha specific */
    case TARGET_NR_getxgid:
         {
            uid_t egid;
            egid=getegid();
            ((CPUAlphaState *)cpu_env)->ir[IR_A4]=egid;
         }
        ret = get_errno(getgid());
        break;
#endif
#if defined(TARGET_NR_osf_getsysinfo) && defined(TARGET_ALPHA)
    /* Alpha specific */
    case TARGET_NR_osf_getsysinfo:
        ret = -TARGET_EOPNOTSUPP;
        switch (arg1) {
          case TARGET_GSI_IEEE_FP_CONTROL:
            {
                uint64_t swcr, fpcr = cpu_alpha_load_fpcr (cpu_env);

                /* Copied from linux ieee_fpcr_to_swcr.  */
                swcr = (fpcr >> 35) & SWCR_STATUS_MASK;
                swcr |= (fpcr >> 36) & SWCR_MAP_DMZ;
                swcr |= (~fpcr >> 48) & (SWCR_TRAP_ENABLE_INV
                                        | SWCR_TRAP_ENABLE_DZE
                                        | SWCR_TRAP_ENABLE_OVF);
                swcr |= (~fpcr >> 57) & (SWCR_TRAP_ENABLE_UNF
                                        | SWCR_TRAP_ENABLE_INE);
                swcr |= (fpcr >> 47) & SWCR_MAP_UMZ;
                swcr |= (~fpcr >> 41) & SWCR_TRAP_ENABLE_DNO;

                if (put_user_u64 (swcr, arg2))
                        goto efault;
                ret = 0;
            }
            break;

          /* case GSI_IEEE_STATE_AT_SIGNAL:
             -- Not implemented in linux kernel.
             case GSI_UACPROC:
             -- Retrieves current unaligned access state; not much used.
             case GSI_PROC_TYPE:
             -- Retrieves implver information; surely not used.
             case GSI_GET_HWRPB:
             -- Grabs a copy of the HWRPB; surely not used.
          */
        }
        break;
#endif
#if defined(TARGET_NR_osf_setsysinfo) && defined(TARGET_ALPHA)
    /* Alpha specific */
    case TARGET_NR_osf_setsysinfo:
        ret = -TARGET_EOPNOTSUPP;
        switch (arg1) {
          case TARGET_SSI_IEEE_FP_CONTROL:
            {
                uint64_t swcr, fpcr, orig_fpcr;

                if (get_user_u64 (swcr, arg2)) {
                    goto efault;
                }
                orig_fpcr = cpu_alpha_load_fpcr(cpu_env);
                fpcr = orig_fpcr & FPCR_DYN_MASK;

                /* Copied from linux ieee_swcr_to_fpcr.  */
                fpcr |= (swcr & SWCR_STATUS_MASK) << 35;
                fpcr |= (swcr & SWCR_MAP_DMZ) << 36;
                fpcr |= (~swcr & (SWCR_TRAP_ENABLE_INV
                                  | SWCR_TRAP_ENABLE_DZE
                                  | SWCR_TRAP_ENABLE_OVF)) << 48;
                fpcr |= (~swcr & (SWCR_TRAP_ENABLE_UNF
                                  | SWCR_TRAP_ENABLE_INE)) << 57;
                fpcr |= (swcr & SWCR_MAP_UMZ ? FPCR_UNDZ | FPCR_UNFD : 0);
                fpcr |= (~swcr & SWCR_TRAP_ENABLE_DNO) << 41;

                cpu_alpha_store_fpcr(cpu_env, fpcr);
                ret = 0;
            }
            break;

          case TARGET_SSI_IEEE_RAISE_EXCEPTION:
            {
                uint64_t exc, fpcr, orig_fpcr;
                int si_code;

                if (get_user_u64(exc, arg2)) {
                    goto efault;
                }

                orig_fpcr = cpu_alpha_load_fpcr(cpu_env);

                /* We only add to the exception status here.  */
                fpcr = orig_fpcr | ((exc & SWCR_STATUS_MASK) << 35);

                cpu_alpha_store_fpcr(cpu_env, fpcr);
                ret = 0;

                /* Old exceptions are not signaled.  */
                fpcr &= ~(orig_fpcr & FPCR_STATUS_MASK);

                /* If any exceptions set by this call,
                   and are unmasked, send a signal.  */
                si_code = 0;
                if ((fpcr & (FPCR_INE | FPCR_INED)) == FPCR_INE) {
                    si_code = TARGET_FPE_FLTRES;
                }
                if ((fpcr & (FPCR_UNF | FPCR_UNFD)) == FPCR_UNF) {
                    si_code = TARGET_FPE_FLTUND;
                }
                if ((fpcr & (FPCR_OVF | FPCR_OVFD)) == FPCR_OVF) {
                    si_code = TARGET_FPE_FLTOVF;
                }
                if ((fpcr & (FPCR_DZE | FPCR_DZED)) == FPCR_DZE) {
                    si_code = TARGET_FPE_FLTDIV;
                }
                if ((fpcr & (FPCR_INV | FPCR_INVD)) == FPCR_INV) {
                    si_code = TARGET_FPE_FLTINV;
                }
                if (si_code != 0) {
                    target_siginfo_t info;
                    info.si_signo = SIGFPE;
                    info.si_errno = 0;
                    info.si_code = si_code;
                    info._sifields._sigfault._addr
                        = ((CPUArchState *)cpu_env)->pc;
                    queue_signal((CPUArchState *)cpu_env, info.si_signo, &info);
                }
            }
            break;

          /* case SSI_NVPAIRS:
             -- Used with SSIN_UACPROC to enable unaligned accesses.
             case SSI_IEEE_STATE_AT_SIGNAL:
             case SSI_IEEE_IGNORE_STATE_AT_SIGNAL:
             -- Not implemented in linux kernel
          */
        }
        break;
#endif
#ifdef TARGET_NR_osf_sigprocmask
    /* Alpha specific.  */
    case TARGET_NR_osf_sigprocmask:
        {
            abi_ulong mask;
            int how;
            sigset_t set, oldset;

            switch(arg1) {
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
            mask = arg2;
            target_to_host_old_sigset(&set, &mask);
            do_sigprocmask(how, &set, &oldset);
            host_to_target_old_sigset(&mask, &oldset);
            ret = mask;
        }
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
                for(i = 0;i < ret; i++)
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
        {
            void *a;
            ret = -TARGET_EFAULT;
            if (!(a = lock_user(VERIFY_READ, arg1,arg2, 0)))
                goto efault;
            if (!(p = lock_user_string(arg3)))
                goto mincore_fail;
            ret = get_errno(mincore(a, arg2, p));
            unlock_user(p, arg3, ret);
            mincore_fail:
            unlock_user(a, arg1, 0);
        }
        break;
#endif
#ifdef TARGET_NR_arm_fadvise64_64
    case TARGET_NR_arm_fadvise64_64:
	{
		/*
		 * arm_fadvise64_64 looks like fadvise64_64 but
		 * with different argument order
		 */
		abi_long temp;
		temp = arg3;
		arg3 = arg4;
		arg4 = temp;
	}
#endif
#if defined(TARGET_NR_fadvise64_64) || defined(TARGET_NR_arm_fadvise64_64) || defined(TARGET_NR_fadvise64)
#ifdef TARGET_NR_fadvise64_64
    case TARGET_NR_fadvise64_64:
#endif
#ifdef TARGET_NR_fadvise64
    case TARGET_NR_fadvise64:
#endif
#ifdef TARGET_S390X
        switch (arg4) {
        case 4: arg4 = POSIX_FADV_NOREUSE + 1; break; /* make sure it's an invalid value */
        case 5: arg4 = POSIX_FADV_NOREUSE + 2; break; /* ditto */
        case 6: arg4 = POSIX_FADV_DONTNEED; break;
        case 7: arg4 = POSIX_FADV_NOREUSE; break;
        default: break;
        }
#endif
        ret = -posix_fadvise(arg1, arg2, arg3, arg4);
	break;
#endif
#ifdef TARGET_NR_madvise
    case TARGET_NR_madvise:
        /* A straight passthrough may not be safe because qemu sometimes
           turns private file-backed mappings into anonymous mappings.
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

	cmd = target_to_host_fcntl_cmd(arg2);
        if (cmd == -TARGET_EINVAL) {
            ret = cmd;
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
                fl.l_pid = tswap32(target_efl->l_pid);
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
                fl.l_pid = tswap32(target_fl->l_pid);
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
                    target_efl->l_pid = tswap32(fl.l_pid);
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
                    target_fl->l_pid = tswap32(fl.l_pid);
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
                fl.l_pid = tswap32(target_efl->l_pid);
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
                fl.l_pid = tswap32(target_fl->l_pid);
                unlock_user_struct(target_fl, arg3, 0);
            }
            ret = get_errno(fcntl(arg1, cmd, &fl));
	    break;
        default:
            ret = do_fcntl(arg1, arg2, arg3);
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
#if TARGET_ABI_BITS == 32
        if (regpairs_aligned(cpu_env)) {
            arg2 = arg3;
            arg3 = arg4;
            arg4 = arg5;
        }
        ret = get_errno(readahead(arg1, ((off64_t)arg3 << 32) | arg2, arg4));
#else
        ret = get_errno(readahead(arg1, arg2, arg3));
#endif
        break;
#endif
#ifdef CONFIG_ATTR
#ifdef TARGET_NR_setxattr
    case TARGET_NR_listxattr:
    case TARGET_NR_llistxattr:
    {
        void *p, *b = 0;
        if (arg2) {
            b = lock_user(VERIFY_WRITE, arg2, arg3, 0);
            if (!b) {
                ret = -TARGET_EFAULT;
                break;
            }
        }
        p = lock_user_string(arg1);
        if (p) {
            if (num == TARGET_NR_listxattr) {
                ret = get_errno(listxattr(p, b, arg3));
            } else {
                ret = get_errno(llistxattr(p, b, arg3));
            }
        } else {
            ret = -TARGET_EFAULT;
        }
        unlock_user(p, arg1, 0);
        unlock_user(b, arg2, arg3);
        break;
    }
    case TARGET_NR_flistxattr:
    {
        void *b = 0;
        if (arg2) {
            b = lock_user(VERIFY_WRITE, arg2, arg3, 0);
            if (!b) {
                ret = -TARGET_EFAULT;
                break;
            }
        }
        ret = get_errno(flistxattr(arg1, b, arg3));
        unlock_user(b, arg2, arg3);
        break;
    }
    case TARGET_NR_setxattr:
    case TARGET_NR_lsetxattr:
        {
            void *p, *n, *v = 0;
            if (arg3) {
                v = lock_user(VERIFY_READ, arg3, arg4, 1);
                if (!v) {
                    ret = -TARGET_EFAULT;
                    break;
                }
            }
            p = lock_user_string(arg1);
            n = lock_user_string(arg2);
            if (p && n) {
                if (num == TARGET_NR_setxattr) {
                    ret = get_errno(setxattr(p, n, v, arg4, arg5));
                } else {
                    ret = get_errno(lsetxattr(p, n, v, arg4, arg5));
                }
            } else {
                ret = -TARGET_EFAULT;
            }
            unlock_user(p, arg1, 0);
            unlock_user(n, arg2, 0);
            unlock_user(v, arg3, 0);
        }
        break;
    case TARGET_NR_fsetxattr:
        {
            void *n, *v = 0;
            if (arg3) {
                v = lock_user(VERIFY_READ, arg3, arg4, 1);
                if (!v) {
                    ret = -TARGET_EFAULT;
                    break;
                }
            }
            n = lock_user_string(arg2);
            if (n) {
                ret = get_errno(fsetxattr(arg1, n, v, arg4, arg5));
            } else {
                ret = -TARGET_EFAULT;
            }
            unlock_user(n, arg2, 0);
            unlock_user(v, arg3, 0);
        }
        break;
    case TARGET_NR_getxattr:
    case TARGET_NR_lgetxattr:
        {
            void *p, *n, *v = 0;
            if (arg3) {
                v = lock_user(VERIFY_WRITE, arg3, arg4, 0);
                if (!v) {
                    ret = -TARGET_EFAULT;
                    break;
                }
            }
            p = lock_user_string(arg1);
            n = lock_user_string(arg2);
            if (p && n) {
                if (num == TARGET_NR_getxattr) {
                    ret = get_errno(getxattr(p, n, v, arg4));
                } else {
                    ret = get_errno(lgetxattr(p, n, v, arg4));
                }
            } else {
                ret = -TARGET_EFAULT;
            }
            unlock_user(p, arg1, 0);
            unlock_user(n, arg2, 0);
            unlock_user(v, arg3, arg4);
        }
        break;
    case TARGET_NR_fgetxattr:
        {
            void *n, *v = 0;
            if (arg3) {
                v = lock_user(VERIFY_WRITE, arg3, arg4, 0);
                if (!v) {
                    ret = -TARGET_EFAULT;
                    break;
                }
            }
            n = lock_user_string(arg2);
            if (n) {
                ret = get_errno(fgetxattr(arg1, n, v, arg4));
            } else {
                ret = -TARGET_EFAULT;
            }
            unlock_user(n, arg2, 0);
            unlock_user(v, arg3, arg4);
        }
        break;
    case TARGET_NR_removexattr:
    case TARGET_NR_lremovexattr:
        {
            void *p, *n;
            p = lock_user_string(arg1);
            n = lock_user_string(arg2);
            if (p && n) {
                if (num == TARGET_NR_removexattr) {
                    ret = get_errno(removexattr(p, n));
                } else {
                    ret = get_errno(lremovexattr(p, n));
                }
            } else {
                ret = -TARGET_EFAULT;
            }
            unlock_user(p, arg1, 0);
            unlock_user(n, arg2, 0);
        }
        break;
    case TARGET_NR_fremovexattr:
        {
            void *n;
            n = lock_user_string(arg2);
            if (n) {
                ret = get_errno(fremovexattr(arg1, n));
            } else {
                ret = -TARGET_EFAULT;
            }
            unlock_user(n, arg2, 0);
        }
        break;
#endif
#endif /* CONFIG_ATTR */
#ifdef TARGET_NR_set_thread_area
    case TARGET_NR_set_thread_area:
#if defined(TARGET_MIPS)
      ((CPUMIPSState *) cpu_env)->active_tc.CP0_UserLocal = arg1;
      ret = 0;
      break;
#elif defined(TARGET_CRIS)
      if (arg1 & 0xff)
          ret = -TARGET_EINVAL;
      else {
          ((CPUCRISState *) cpu_env)->pregs[PR_PID] = arg1;
          ret = 0;
      }
      break;
#elif defined(TARGET_I386) && defined(TARGET_ABI32)
      ret = do_set_thread_area(cpu_env, arg1);
      break;
#elif defined(TARGET_M68K)
      {
          TaskState *ts = cpu->opaque;
          ts->tp_value = arg1;
          ret = 0;
          break;
      }
#else
      goto unimplemented_nowarn;
#endif
#endif
#ifdef TARGET_NR_get_thread_area
    case TARGET_NR_get_thread_area:
#if defined(TARGET_I386) && defined(TARGET_ABI32)
        ret = do_get_thread_area(cpu_env, arg1);
        break;
#elif defined(TARGET_M68K)
        {
            TaskState *ts = cpu->opaque;
            ret = ts->tp_value;
            break;
        }
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
    case TARGET_NR_get_robust_list:
        /* The ABI for supporting robust futexes has userspace pass
         * the kernel a pointer to a linked list which is updated by
         * userspace after the syscall; the list is walked by the kernel
         * when the thread exits. Since the linked list in QEMU guest
         * memory isn't a valid linked list for the host and we have
         * no way to reliably intercept the thread-death event, we can't
         * support these. Silently return ENOSYS so that guest userspace
         * falls back to a non-robust futex implementation (which should
         * be OK except in the corner case of the guest crashing while
         * holding a mutex that is shared with another process via
         * shared memory).
         */
        goto unimplemented_nowarn;
#endif

#if defined(TARGET_NR_utimensat)
    case TARGET_NR_utimensat:
        {
            struct timespec *tsp, ts[2];
            if (!arg3) {
                tsp = NULL;
            } else {
                target_to_host_timespec(ts, arg3);
                target_to_host_timespec(ts+1, arg3+sizeof(struct target_timespec));
                tsp = ts;
            }
            if (!arg2)
                ret = get_errno(sys_utimensat(arg1, NULL, tsp, arg4));
            else {
                if (!(p = lock_user_string(arg2))) {
                    ret = -TARGET_EFAULT;
                    goto fail;
                }
                ret = get_errno(sys_utimensat(arg1, path(p), tsp, arg4));
                unlock_user(p, arg2, 0);
            }
        }
	break;
#endif
    case TARGET_NR_futex:
        ret = do_futex(arg1, arg2, arg3, arg4, arg5, arg6);
        break;
#if defined(TARGET_NR_inotify_init) && defined(__NR_inotify_init)
    case TARGET_NR_inotify_init:
        ret = get_errno(sys_inotify_init());
        break;
#endif
#ifdef CONFIG_INOTIFY1
#if defined(TARGET_NR_inotify_init1) && defined(__NR_inotify_init1)
    case TARGET_NR_inotify_init1:
        ret = get_errno(sys_inotify_init1(arg1));
        break;
#endif
#endif
#if defined(TARGET_NR_inotify_add_watch) && defined(__NR_inotify_add_watch)
    case TARGET_NR_inotify_add_watch:
        p = lock_user_string(arg2);
        ret = get_errno(sys_inotify_add_watch(arg1, path(p), arg3));
        unlock_user(p, arg2, 0);
        break;
#endif
#if defined(TARGET_NR_inotify_rm_watch) && defined(__NR_inotify_rm_watch)
    case TARGET_NR_inotify_rm_watch:
        ret = get_errno(sys_inotify_rm_watch(arg1, arg2));
        break;
#endif

#if defined(TARGET_NR_mq_open) && defined(__NR_mq_open)
    case TARGET_NR_mq_open:
        {
            struct mq_attr posix_mq_attr, *attrp;

            p = lock_user_string(arg1 - 1);
            if (arg4 != 0) {
                copy_from_user_mq_attr (&posix_mq_attr, arg4);
                attrp = &posix_mq_attr;
            } else {
                attrp = 0;
            }
            ret = get_errno(mq_open(p, arg2, arg3, attrp));
            unlock_user (p, arg1, 0);
        }
        break;

    case TARGET_NR_mq_unlink:
        p = lock_user_string(arg1 - 1);
        ret = get_errno(mq_unlink(p));
        unlock_user (p, arg1, 0);
        break;

    case TARGET_NR_mq_timedsend:
        {
            struct timespec ts;

            p = lock_user (VERIFY_READ, arg2, arg3, 1);
            if (arg5 != 0) {
                target_to_host_timespec(&ts, arg5);
                ret = get_errno(mq_timedsend(arg1, p, arg3, arg4, &ts));
                host_to_target_timespec(arg5, &ts);
            }
            else
                ret = get_errno(mq_send(arg1, p, arg3, arg4));
            unlock_user (p, arg2, arg3);
        }
        break;

    case TARGET_NR_mq_timedreceive:
        {
            struct timespec ts;
            unsigned int prio;

            p = lock_user (VERIFY_READ, arg2, arg3, 1);
            if (arg5 != 0) {
                target_to_host_timespec(&ts, arg5);
                ret = get_errno(mq_timedreceive(arg1, p, arg3, &prio, &ts));
                host_to_target_timespec(arg5, &ts);
            }
            else
                ret = get_errno(mq_receive(arg1, p, arg3, &prio));
            unlock_user (p, arg2, arg3);
            if (arg4 != 0)
                put_user_u32(prio, arg4);
        }
        break;

    /* Not implemented for now... */
/*     case TARGET_NR_mq_notify: */
/*         break; */

    case TARGET_NR_mq_getsetattr:
        {
            struct mq_attr posix_mq_attr_in, posix_mq_attr_out;
            ret = 0;
            if (arg3 != 0) {
                ret = mq_getattr(arg1, &posix_mq_attr_out);
                copy_to_user_mq_attr(arg3, &posix_mq_attr_out);
            }
            if (arg2 != 0) {
                copy_from_user_mq_attr(&posix_mq_attr_in, arg2);
                ret |= mq_setattr(arg1, &posix_mq_attr_in, &posix_mq_attr_out);
            }

        }
        break;
#endif

#ifdef CONFIG_SPLICE
#ifdef TARGET_NR_tee
    case TARGET_NR_tee:
        {
            ret = get_errno(tee(arg1,arg2,arg3,arg4));
        }
        break;
#endif
#ifdef TARGET_NR_splice
    case TARGET_NR_splice:
        {
            loff_t loff_in, loff_out;
            loff_t *ploff_in = NULL, *ploff_out = NULL;
            if(arg2) {
                get_user_u64(loff_in, arg2);
                ploff_in = &loff_in;
            }
            if(arg4) {
                get_user_u64(loff_out, arg2);
                ploff_out = &loff_out;
            }
            ret = get_errno(splice(arg1, ploff_in, arg3, ploff_out, arg5, arg6));
        }
        break;
#endif
#ifdef TARGET_NR_vmsplice
	case TARGET_NR_vmsplice:
        {
            struct iovec *vec = lock_iovec(VERIFY_READ, arg2, arg3, 1);
            if (vec != NULL) {
                ret = get_errno(vmsplice(arg1, vec, arg3, arg4));
                unlock_iovec(vec, arg2, arg3, 0);
            } else {
                ret = -host_to_target_errno(errno);
            }
        }
        break;
#endif
#endif /* CONFIG_SPLICE */
#ifdef CONFIG_EVENTFD
#if defined(TARGET_NR_eventfd)
    case TARGET_NR_eventfd:
        ret = get_errno(eventfd(arg1, 0));
        break;
#endif
#if defined(TARGET_NR_eventfd2)
    case TARGET_NR_eventfd2:
    {
        int host_flags = arg2 & (~(TARGET_O_NONBLOCK | TARGET_O_CLOEXEC));
        if (arg2 & TARGET_O_NONBLOCK) {
            host_flags |= O_NONBLOCK;
        }
        if (arg2 & TARGET_O_CLOEXEC) {
            host_flags |= O_CLOEXEC;
        }
        ret = get_errno(eventfd(arg1, host_flags));
        break;
    }
#endif
#endif /* CONFIG_EVENTFD  */
#if defined(CONFIG_FALLOCATE) && defined(TARGET_NR_fallocate)
    case TARGET_NR_fallocate:
#if TARGET_ABI_BITS == 32
        ret = get_errno(fallocate(arg1, arg2, target_offset64(arg3, arg4),
                                  target_offset64(arg5, arg6)));
#else
        ret = get_errno(fallocate(arg1, arg2, arg3, arg4));
#endif
        break;
#endif
#if defined(CONFIG_SYNC_FILE_RANGE)
#if defined(TARGET_NR_sync_file_range)
    case TARGET_NR_sync_file_range:
#if TARGET_ABI_BITS == 32
#if defined(TARGET_MIPS)
        ret = get_errno(sync_file_range(arg1, target_offset64(arg3, arg4),
                                        target_offset64(arg5, arg6), arg7));
#else
        ret = get_errno(sync_file_range(arg1, target_offset64(arg2, arg3),
                                        target_offset64(arg4, arg5), arg6));
#endif /* !TARGET_MIPS */
#else
        ret = get_errno(sync_file_range(arg1, arg2, arg3, arg4));
#endif
        break;
#endif
#if defined(TARGET_NR_sync_file_range2)
    case TARGET_NR_sync_file_range2:
        /* This is like sync_file_range but the arguments are reordered */
#if TARGET_ABI_BITS == 32
        ret = get_errno(sync_file_range(arg1, target_offset64(arg3, arg4),
                                        target_offset64(arg5, arg6), arg2));
#else
        ret = get_errno(sync_file_range(arg1, arg3, arg4, arg2));
#endif
        break;
#endif
#endif
#if defined(CONFIG_EPOLL)
#if defined(TARGET_NR_epoll_create)
    case TARGET_NR_epoll_create:
        ret = get_errno(epoll_create(arg1));
        break;
#endif
#if defined(TARGET_NR_epoll_create1) && defined(CONFIG_EPOLL_CREATE1)
    case TARGET_NR_epoll_create1:
        ret = get_errno(epoll_create1(arg1));
        break;
#endif
#if defined(TARGET_NR_epoll_ctl)
    case TARGET_NR_epoll_ctl:
    {
        struct epoll_event ep;
        struct epoll_event *epp = 0;
        if (arg4) {
            struct target_epoll_event *target_ep;
            if (!lock_user_struct(VERIFY_READ, target_ep, arg4, 1)) {
                goto efault;
            }
            ep.events = tswap32(target_ep->events);
            /* The epoll_data_t union is just opaque data to the kernel,
             * so we transfer all 64 bits across and need not worry what
             * actual data type it is.
             */
            ep.data.u64 = tswap64(target_ep->data.u64);
            unlock_user_struct(target_ep, arg4, 0);
            epp = &ep;
        }
        ret = get_errno(epoll_ctl(arg1, arg2, arg3, epp));
        break;
    }
#endif

#if defined(TARGET_NR_epoll_pwait) && defined(CONFIG_EPOLL_PWAIT)
#define IMPLEMENT_EPOLL_PWAIT
#endif
#if defined(TARGET_NR_epoll_wait) || defined(IMPLEMENT_EPOLL_PWAIT)
#if defined(TARGET_NR_epoll_wait)
    case TARGET_NR_epoll_wait:
#endif
#if defined(IMPLEMENT_EPOLL_PWAIT)
    case TARGET_NR_epoll_pwait:
#endif
    {
        struct target_epoll_event *target_ep;
        struct epoll_event *ep;
        int epfd = arg1;
        int maxevents = arg3;
        int timeout = arg4;

        target_ep = lock_user(VERIFY_WRITE, arg2,
                              maxevents * sizeof(struct target_epoll_event), 1);
        if (!target_ep) {
            goto efault;
        }

        ep = alloca(maxevents * sizeof(struct epoll_event));

        switch (num) {
#if defined(IMPLEMENT_EPOLL_PWAIT)
        case TARGET_NR_epoll_pwait:
        {
            target_sigset_t *target_set;
            sigset_t _set, *set = &_set;

            if (arg5) {
                target_set = lock_user(VERIFY_READ, arg5,
                                       sizeof(target_sigset_t), 1);
                if (!target_set) {
                    unlock_user(target_ep, arg2, 0);
                    goto efault;
                }
                target_to_host_sigset(set, target_set);
                unlock_user(target_set, arg5, 0);
            } else {
                set = NULL;
            }

            ret = get_errno(epoll_pwait(epfd, ep, maxevents, timeout, set));
            break;
        }
#endif
#if defined(TARGET_NR_epoll_wait)
        case TARGET_NR_epoll_wait:
            ret = get_errno(epoll_wait(epfd, ep, maxevents, timeout));
            break;
#endif
        default:
            ret = -TARGET_ENOSYS;
        }
        if (!is_error(ret)) {
            int i;
            for (i = 0; i < ret; i++) {
                target_ep[i].events = tswap32(ep[i].events);
                target_ep[i].data.u64 = tswap64(ep[i].data.u64);
            }
        }
        unlock_user(target_ep, arg2, ret * sizeof(struct target_epoll_event));
        break;
    }
#endif
#endif
#ifdef TARGET_NR_prlimit64
    case TARGET_NR_prlimit64:
    {
        /* args: pid, resource number, ptr to new rlimit, ptr to old rlimit */
        struct target_rlimit64 *target_rnew, *target_rold;
        struct host_rlimit64 rnew, rold, *rnewp = 0;
        if (arg3) {
            if (!lock_user_struct(VERIFY_READ, target_rnew, arg3, 1)) {
                goto efault;
            }
            rnew.rlim_cur = tswap64(target_rnew->rlim_cur);
            rnew.rlim_max = tswap64(target_rnew->rlim_max);
            unlock_user_struct(target_rnew, arg3, 0);
            rnewp = &rnew;
        }

        ret = get_errno(sys_prlimit64(arg1, arg2, rnewp, arg4 ? &rold : 0));
        if (!is_error(ret) && arg4) {
            if (!lock_user_struct(VERIFY_WRITE, target_rold, arg4, 1)) {
                goto efault;
            }
            target_rold->rlim_cur = tswap64(rold.rlim_cur);
            target_rold->rlim_max = tswap64(rold.rlim_max);
            unlock_user_struct(target_rold, arg4, 1);
        }
        break;
    }
#endif
#ifdef TARGET_NR_gethostname
    case TARGET_NR_gethostname:
    {
        char *name = lock_user(VERIFY_WRITE, arg1, arg2, 0);
        if (name) {
            ret = get_errno(gethostname(name, arg2));
            unlock_user(name, arg1, arg2);
        } else {
            ret = -TARGET_EFAULT;
        }
        break;
    }
#endif
#ifdef TARGET_NR_atomic_cmpxchg_32
    case TARGET_NR_atomic_cmpxchg_32:
    {
        /* should use start_exclusive from main.c */
        abi_ulong mem_value;
        if (get_user_u32(mem_value, arg6)) {
            target_siginfo_t info;
            info.si_signo = SIGSEGV;
            info.si_errno = 0;
            info.si_code = TARGET_SEGV_MAPERR;
            info._sifields._sigfault._addr = arg6;
            queue_signal((CPUArchState *)cpu_env, info.si_signo, &info);
            ret = 0xdeadbeef;

        }
        if (mem_value == arg2)
            put_user_u32(arg1, arg6);
        ret = mem_value;
        break;
    }
#endif
#ifdef TARGET_NR_atomic_barrier
    case TARGET_NR_atomic_barrier:
    {
        /* Like the kernel implementation and the qemu arm barrier, no-op this? */
        ret = 0;
        break;
    }
#endif

#ifdef TARGET_NR_timer_create
    case TARGET_NR_timer_create:
    {
        /* args: clockid_t clockid, struct sigevent *sevp, timer_t *timerid */

        struct sigevent host_sevp = { {0}, }, *phost_sevp = NULL;
        struct target_timer_t *ptarget_timer;

        int clkid = arg1;
        int timer_index = next_free_host_timer();

        if (timer_index < 0) {
            ret = -TARGET_EAGAIN;
        } else {
            timer_t *phtimer = g_posix_timers  + timer_index;

            if (arg2) {
                phost_sevp = &host_sevp;
                ret = target_to_host_sigevent(phost_sevp, arg2);
                if (ret != 0) {
                    break;
                }
            }

            ret = get_errno(timer_create(clkid, phost_sevp, phtimer));
            if (ret) {
                phtimer = NULL;
            } else {
                if (!lock_user_struct(VERIFY_WRITE, ptarget_timer, arg3, 1)) {
                    goto efault;
                }
                ptarget_timer->ptr = tswap32(0xcafe0000 | timer_index);
                unlock_user_struct(ptarget_timer, arg3, 1);
            }
        }
        break;
    }
#endif

#ifdef TARGET_NR_timer_settime
    case TARGET_NR_timer_settime:
    {
        /* args: timer_t timerid, int flags, const struct itimerspec *new_value,
         * struct itimerspec * old_value */
        arg1 &= 0xffff;
        if (arg3 == 0 || arg1 < 0 || arg1 >= ARRAY_SIZE(g_posix_timers)) {
            ret = -TARGET_EINVAL;
        } else {
            timer_t htimer = g_posix_timers[arg1];
            struct itimerspec hspec_new = {{0},}, hspec_old = {{0},};

            target_to_host_itimerspec(&hspec_new, arg3);
            ret = get_errno(
                          timer_settime(htimer, arg2, &hspec_new, &hspec_old));
            host_to_target_itimerspec(arg2, &hspec_old);
        }
        break;
    }
#endif

#ifdef TARGET_NR_timer_gettime
    case TARGET_NR_timer_gettime:
    {
        /* args: timer_t timerid, struct itimerspec *curr_value */
        arg1 &= 0xffff;
        if (!arg2) {
            return -TARGET_EFAULT;
        } else if (arg1 < 0 || arg1 >= ARRAY_SIZE(g_posix_timers)) {
            ret = -TARGET_EINVAL;
        } else {
            timer_t htimer = g_posix_timers[arg1];
            struct itimerspec hspec;
            ret = get_errno(timer_gettime(htimer, &hspec));

            if (host_to_target_itimerspec(arg2, &hspec)) {
                ret = -TARGET_EFAULT;
            }
        }
        break;
    }
#endif

#ifdef TARGET_NR_timer_getoverrun
    case TARGET_NR_timer_getoverrun:
    {
        /* args: timer_t timerid */
        arg1 &= 0xffff;
        if (arg1 < 0 || arg1 >= ARRAY_SIZE(g_posix_timers)) {
            ret = -TARGET_EINVAL;
        } else {
            timer_t htimer = g_posix_timers[arg1];
            ret = get_errno(timer_getoverrun(htimer));
        }
        break;
    }
#endif

#ifdef TARGET_NR_timer_delete
    case TARGET_NR_timer_delete:
    {
        /* args: timer_t timerid */
        arg1 &= 0xffff;
        if (arg1 < 0 || arg1 >= ARRAY_SIZE(g_posix_timers)) {
            ret = -TARGET_EINVAL;
        } else {
            timer_t htimer = g_posix_timers[arg1];
            ret = get_errno(timer_delete(htimer));
            g_posix_timers[arg1] = 0;
        }
        break;
    }
#endif

#if defined(TARGET_NR_timerfd_create) && defined(CONFIG_TIMERFD)
    case TARGET_NR_timerfd_create:
        ret = get_errno(timerfd_create(arg1,
                target_to_host_bitmask(arg2, fcntl_flags_tbl)));
        break;
#endif

#if defined(TARGET_NR_timerfd_gettime) && defined(CONFIG_TIMERFD)
    case TARGET_NR_timerfd_gettime:
        {
            struct itimerspec its_curr;

            ret = get_errno(timerfd_gettime(arg1, &its_curr));

            if (arg2 && host_to_target_itimerspec(arg2, &its_curr)) {
                goto efault;
            }
        }
        break;
#endif

#if defined(TARGET_NR_timerfd_settime) && defined(CONFIG_TIMERFD)
    case TARGET_NR_timerfd_settime:
        {
            struct itimerspec its_new, its_old, *p_new;

            if (arg3) {
                if (target_to_host_itimerspec(&its_new, arg3)) {
                    goto efault;
                }
                p_new = &its_new;
            } else {
                p_new = NULL;
            }

            ret = get_errno(timerfd_settime(arg1, arg2, p_new, &its_old));

            if (arg4 && host_to_target_itimerspec(arg4, &its_old)) {
                goto efault;
            }
        }
        break;
#endif

#if defined(TARGET_NR_ioprio_get) && defined(__NR_ioprio_get)
    case TARGET_NR_ioprio_get:
        ret = get_errno(ioprio_get(arg1, arg2));
        break;
#endif

#if defined(TARGET_NR_ioprio_set) && defined(__NR_ioprio_set)
    case TARGET_NR_ioprio_set:
        ret = get_errno(ioprio_set(arg1, arg2, arg3));
        break;
#endif

#if defined(TARGET_NR_setns) && defined(CONFIG_SETNS)
    case TARGET_NR_setns:
        ret = get_errno(setns(arg1, arg2));
        break;
#endif
#if defined(TARGET_NR_unshare) && defined(CONFIG_SETNS)
    case TARGET_NR_unshare:
        ret = get_errno(unshare(arg1));
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
    gemu_log(" = " TARGET_ABI_FMT_ld "\n", ret);
#endif
    if(do_strace)
        print_syscall_ret(num, ret);
    return ret;
efault:
    ret = -TARGET_EFAULT;
    goto fail;
}

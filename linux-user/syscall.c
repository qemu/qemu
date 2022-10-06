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
#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/path.h"
#include "qemu/memfd.h"
#include "qemu/queue.h"
#include <elf.h>
#include <endian.h>
#include <grp.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/file.h>
#include <sys/fsuid.h>
#include <sys/personality.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/swap.h>
#include <linux/capability.h>
#include <sched.h>
#include <sys/timex.h>
#include <sys/socket.h>
#include <linux/sockios.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <poll.h>
#include <sys/times.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/statfs.h>
#include <utime.h>
#include <sys/sysinfo.h>
#include <sys/signalfd.h>
//#include <sys/user.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <linux/wireless.h>
#include <linux/icmp.h>
#include <linux/icmpv6.h>
#include <linux/if_tun.h>
#include <linux/in6.h>
#include <linux/errqueue.h>
#include <linux/random.h>
#ifdef CONFIG_TIMERFD
#include <sys/timerfd.h>
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
#ifdef HAVE_SYS_KCOV_H
#include <sys/kcov.h>
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

#ifdef HAVE_SYS_MOUNT_FSCONFIG
/*
 * glibc >= 2.36 linux/mount.h conflicts with sys/mount.h,
 * which in turn prevents use of linux/fs.h. So we have to
 * define the constants ourselves for now.
 */
#define FS_IOC_GETFLAGS                _IOR('f', 1, long)
#define FS_IOC_SETFLAGS                _IOW('f', 2, long)
#define FS_IOC_GETVERSION              _IOR('v', 1, long)
#define FS_IOC_SETVERSION              _IOW('v', 2, long)
#define FS_IOC_FIEMAP                  _IOWR('f', 11, struct fiemap)
#define FS_IOC32_GETFLAGS              _IOR('f', 1, int)
#define FS_IOC32_SETFLAGS              _IOW('f', 2, int)
#define FS_IOC32_GETVERSION            _IOR('v', 1, int)
#define FS_IOC32_SETVERSION            _IOW('v', 2, int)
#else
#include <linux/fs.h>
#endif
#include <linux/fd.h>
#if defined(CONFIG_FIEMAP)
#include <linux/fiemap.h>
#endif
#include <linux/fb.h>
#if defined(CONFIG_USBFS)
#include <linux/usbdevice_fs.h>
#include <linux/usb/ch9.h>
#endif
#include <linux/vt.h>
#include <linux/dm-ioctl.h>
#include <linux/reboot.h>
#include <linux/route.h>
#include <linux/filter.h>
#include <linux/blkpg.h>
#include <netpacket/packet.h>
#include <linux/netlink.h>
#include <linux/if_alg.h>
#include <linux/rtc.h>
#include <sound/asound.h>
#ifdef HAVE_BTRFS_H
#include <linux/btrfs.h>
#endif
#ifdef HAVE_DRM_H
#include <libdrm/drm.h>
#include <libdrm/i915_drm.h>
#endif
#include "linux_loop.h"
#include "uname.h"

#include "qemu.h"
#include "user-internals.h"
#include "strace.h"
#include "signal-common.h"
#include "loader.h"
#include "user-mmap.h"
#include "user/safe-syscall.h"
#include "qemu/guest-random.h"
#include "qemu/selfmap.h"
#include "user/syscall-trace.h"
#include "special-errno.h"
#include "qapi/error.h"
#include "fd-trans.h"
#include "tcg/tcg.h"

#ifndef CLONE_IO
#define CLONE_IO                0x80000000      /* Clone io context */
#endif

/* We can't directly call the host clone syscall, because this will
 * badly confuse libc (breaking mutexes, for example). So we must
 * divide clone flags into:
 *  * flag combinations that look like pthread_create()
 *  * flag combinations that look like fork()
 *  * flags we can implement within QEMU itself
 *  * flags we can't support and will return an error for
 */
/* For thread creation, all these flags must be present; for
 * fork, none must be present.
 */
#define CLONE_THREAD_FLAGS                              \
    (CLONE_VM | CLONE_FS | CLONE_FILES |                \
     CLONE_SIGHAND | CLONE_THREAD | CLONE_SYSVSEM)

/* These flags are ignored:
 * CLONE_DETACHED is now ignored by the kernel;
 * CLONE_IO is just an optimisation hint to the I/O scheduler
 */
#define CLONE_IGNORED_FLAGS                     \
    (CLONE_DETACHED | CLONE_IO)

/* Flags for fork which we can implement within QEMU itself */
#define CLONE_OPTIONAL_FORK_FLAGS               \
    (CLONE_SETTLS | CLONE_PARENT_SETTID |       \
     CLONE_CHILD_CLEARTID | CLONE_CHILD_SETTID)

/* Flags for thread creation which we can implement within QEMU itself */
#define CLONE_OPTIONAL_THREAD_FLAGS                             \
    (CLONE_SETTLS | CLONE_PARENT_SETTID |                       \
     CLONE_CHILD_CLEARTID | CLONE_CHILD_SETTID | CLONE_PARENT)

#define CLONE_INVALID_FORK_FLAGS                                        \
    (~(CSIGNAL | CLONE_OPTIONAL_FORK_FLAGS | CLONE_IGNORED_FLAGS))

#define CLONE_INVALID_THREAD_FLAGS                                      \
    (~(CSIGNAL | CLONE_THREAD_FLAGS | CLONE_OPTIONAL_THREAD_FLAGS |     \
       CLONE_IGNORED_FLAGS))

/* CLONE_VFORK is special cased early in do_fork(). The other flag bits
 * have almost all been allocated. We cannot support any of
 * CLONE_NEWNS, CLONE_NEWCGROUP, CLONE_NEWUTS, CLONE_NEWIPC,
 * CLONE_NEWUSER, CLONE_NEWPID, CLONE_NEWNET, CLONE_PTRACE, CLONE_UNTRACED.
 * The checks against the invalid thread masks above will catch these.
 * (The one remaining unallocated bit is 0x1000 which used to be CLONE_PID.)
 */

/* Define DEBUG_ERESTARTSYS to force every syscall to be restarted
 * once. This exercises the codepaths for restart.
 */
//#define DEBUG_ERESTARTSYS

//#include <linux/msdos_fs.h>
#define VFAT_IOCTL_READDIR_BOTH \
    _IOC(_IOC_READ, 'r', 1, (sizeof(struct linux_dirent) + 256) * 2)
#define VFAT_IOCTL_READDIR_SHORT \
    _IOC(_IOC_READ, 'r', 2, (sizeof(struct linux_dirent) + 256) * 2)

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
#define __NR_sys_rt_tgsigqueueinfo __NR_rt_tgsigqueueinfo
#define __NR_sys_syslog __NR_syslog
#if defined(__NR_futex)
# define __NR_sys_futex __NR_futex
#endif
#if defined(__NR_futex_time64)
# define __NR_sys_futex_time64 __NR_futex_time64
#endif
#define __NR_sys_statx __NR_statx

#if defined(__alpha__) || defined(__x86_64__) || defined(__s390x__)
#define __NR__llseek __NR_lseek
#endif

/* Newer kernel ports have llseek() instead of _llseek() */
#if defined(TARGET_NR_llseek) && !defined(TARGET_NR__llseek)
#define TARGET_NR__llseek TARGET_NR_llseek
#endif

/* some platforms need to mask more bits than just TARGET_O_NONBLOCK */
#ifndef TARGET_O_NONBLOCK_MASK
#define TARGET_O_NONBLOCK_MASK TARGET_O_NONBLOCK
#endif

#define __NR_sys_gettid __NR_gettid
_syscall0(int, sys_gettid)

/* For the 64-bit guest on 32-bit host case we must emulate
 * getdents using getdents64, because otherwise the host
 * might hand us back more dirent records than we can fit
 * into the guest buffer after structure format conversion.
 * Otherwise we emulate getdents with getdents if the host has it.
 */
#if defined(__NR_getdents) && HOST_LONG_BITS >= TARGET_ABI_BITS
#define EMULATE_GETDENTS_WITH_GETDENTS
#endif

#if defined(TARGET_NR_getdents) && defined(EMULATE_GETDENTS_WITH_GETDENTS)
_syscall3(int, sys_getdents, uint, fd, struct linux_dirent *, dirp, uint, count);
#endif
#if (defined(TARGET_NR_getdents) && \
      !defined(EMULATE_GETDENTS_WITH_GETDENTS)) || \
    (defined(TARGET_NR_getdents64) && defined(__NR_getdents64))
_syscall3(int, sys_getdents64, uint, fd, struct linux_dirent64 *, dirp, uint, count);
#endif
#if defined(TARGET_NR__llseek) && defined(__NR_llseek)
_syscall5(int, _llseek,  uint,  fd, ulong, hi, ulong, lo,
          loff_t *, res, uint, wh);
#endif
_syscall3(int, sys_rt_sigqueueinfo, pid_t, pid, int, sig, siginfo_t *, uinfo)
_syscall4(int, sys_rt_tgsigqueueinfo, pid_t, pid, pid_t, tid, int, sig,
          siginfo_t *, uinfo)
_syscall3(int,sys_syslog,int,type,char*,bufp,int,len)
#ifdef __NR_exit_group
_syscall1(int,exit_group,int,error_code)
#endif
#if defined(__NR_futex)
_syscall6(int,sys_futex,int *,uaddr,int,op,int,val,
          const struct timespec *,timeout,int *,uaddr2,int,val3)
#endif
#if defined(__NR_futex_time64)
_syscall6(int,sys_futex_time64,int *,uaddr,int,op,int,val,
          const struct timespec *,timeout,int *,uaddr2,int,val3)
#endif
#if defined(__NR_pidfd_open) && defined(TARGET_NR_pidfd_open)
_syscall2(int, pidfd_open, pid_t, pid, unsigned int, flags);
#endif
#if defined(__NR_pidfd_send_signal) && defined(TARGET_NR_pidfd_send_signal)
_syscall4(int, pidfd_send_signal, int, pidfd, int, sig, siginfo_t *, info,
                             unsigned int, flags);
#endif
#if defined(__NR_pidfd_getfd) && defined(TARGET_NR_pidfd_getfd)
_syscall3(int, pidfd_getfd, int, pidfd, int, targetfd, unsigned int, flags);
#endif
#define __NR_sys_sched_getaffinity __NR_sched_getaffinity
_syscall3(int, sys_sched_getaffinity, pid_t, pid, unsigned int, len,
          unsigned long *, user_mask_ptr);
#define __NR_sys_sched_setaffinity __NR_sched_setaffinity
_syscall3(int, sys_sched_setaffinity, pid_t, pid, unsigned int, len,
          unsigned long *, user_mask_ptr);
/* sched_attr is not defined in glibc */
struct sched_attr {
    uint32_t size;
    uint32_t sched_policy;
    uint64_t sched_flags;
    int32_t sched_nice;
    uint32_t sched_priority;
    uint64_t sched_runtime;
    uint64_t sched_deadline;
    uint64_t sched_period;
    uint32_t sched_util_min;
    uint32_t sched_util_max;
};
#define __NR_sys_sched_getattr __NR_sched_getattr
_syscall4(int, sys_sched_getattr, pid_t, pid, struct sched_attr *, attr,
          unsigned int, size, unsigned int, flags);
#define __NR_sys_sched_setattr __NR_sched_setattr
_syscall3(int, sys_sched_setattr, pid_t, pid, struct sched_attr *, attr,
          unsigned int, flags);
#define __NR_sys_sched_getscheduler __NR_sched_getscheduler
_syscall1(int, sys_sched_getscheduler, pid_t, pid);
#define __NR_sys_sched_setscheduler __NR_sched_setscheduler
_syscall3(int, sys_sched_setscheduler, pid_t, pid, int, policy,
          const struct sched_param *, param);
#define __NR_sys_sched_getparam __NR_sched_getparam
_syscall2(int, sys_sched_getparam, pid_t, pid,
          struct sched_param *, param);
#define __NR_sys_sched_setparam __NR_sched_setparam
_syscall2(int, sys_sched_setparam, pid_t, pid,
          const struct sched_param *, param);
#define __NR_sys_getcpu __NR_getcpu
_syscall3(int, sys_getcpu, unsigned *, cpu, unsigned *, node, void *, tcache);
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
#if defined(TARGET_NR_getrandom) && defined(__NR_getrandom)
_syscall3(int, getrandom, void *, buf, size_t, buflen, unsigned int, flags)
#endif

#if defined(TARGET_NR_kcmp) && defined(__NR_kcmp)
_syscall5(int, kcmp, pid_t, pid1, pid_t, pid2, int, type,
          unsigned long, idx1, unsigned long, idx2)
#endif

/*
 * It is assumed that struct statx is architecture independent.
 */
#if defined(TARGET_NR_statx) && defined(__NR_statx)
_syscall5(int, sys_statx, int, dirfd, const char *, pathname, int, flags,
          unsigned int, mask, struct target_statx *, statxbuf)
#endif
#if defined(TARGET_NR_membarrier) && defined(__NR_membarrier)
_syscall2(int, membarrier, int, cmd, int, flags)
#endif

static const bitmask_transtbl fcntl_flags_tbl[] = {
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
#if defined(O_TMPFILE)
  { TARGET_O_TMPFILE,   TARGET_O_TMPFILE,   O_TMPFILE,   O_TMPFILE    },
#endif
  /* Don't terminate the list prematurely on 64-bit host+guest.  */
#if TARGET_O_LARGEFILE != 0 || O_LARGEFILE != 0
  { TARGET_O_LARGEFILE, TARGET_O_LARGEFILE, O_LARGEFILE, O_LARGEFILE, },
#endif
  { 0, 0, 0, 0 }
};

_syscall2(int, sys_getcwd1, char *, buf, size_t, size)

#if defined(TARGET_NR_utimensat) || defined(TARGET_NR_utimensat_time64)
#if defined(__NR_utimensat)
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

#ifdef TARGET_NR_renameat2
#if defined(__NR_renameat2)
#define __NR_sys_renameat2 __NR_renameat2
_syscall5(int, sys_renameat2, int, oldfd, const char *, old, int, newfd,
          const char *, new, unsigned int, flags)
#else
static int sys_renameat2(int oldfd, const char *old,
                         int newfd, const char *new, int flags)
{
    if (flags == 0) {
        return renameat(oldfd, old, newfd, new);
    }
    errno = ENOSYS;
    return -1;
}
#endif
#endif /* TARGET_NR_renameat2 */

#ifdef CONFIG_INOTIFY
#include <sys/inotify.h>
#else
/* Userspace can usually survive runtime without inotify */
#undef TARGET_NR_inotify_init
#undef TARGET_NR_inotify_init1
#undef TARGET_NR_inotify_add_watch
#undef TARGET_NR_inotify_rm_watch
#endif /* CONFIG_INOTIFY  */

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
/* Maximum of 32 active POSIX timers allowed at any one time. */
#define GUEST_TIMER_MAX 32
static timer_t g_posix_timers[GUEST_TIMER_MAX];
static int g_posix_timer_allocated[GUEST_TIMER_MAX];

static inline int next_free_host_timer(void)
{
    int k;
    for (k = 0; k < ARRAY_SIZE(g_posix_timer_allocated); k++) {
        if (qatomic_xchg(g_posix_timer_allocated + k, 1) == 0) {
            return k;
        }
    }
    return -1;
}

static inline void free_host_timer_slot(int id)
{
    qatomic_store_release(g_posix_timer_allocated + id, 0);
}
#endif

static inline int host_to_target_errno(int host_errno)
{
    switch (host_errno) {
#define E(X)  case X: return TARGET_##X;
#include "errnos.c.inc"
#undef E
    default:
        return host_errno;
    }
}

static inline int target_to_host_errno(int target_errno)
{
    switch (target_errno) {
#define E(X)  case TARGET_##X: return X;
#include "errnos.c.inc"
#undef E
    default:
        return target_errno;
    }
}

abi_long get_errno(abi_long ret)
{
    if (ret == -1)
        return -host_to_target_errno(errno);
    else
        return ret;
}

const char *target_strerror(int err)
{
    if (err == QEMU_ERESTARTSYS) {
        return "To be restarted";
    }
    if (err == QEMU_ESIGRETURN) {
        return "Successful exit from sigreturn";
    }

    return strerror(target_to_host_errno(err));
}

static int check_zeroed_user(abi_long addr, size_t ksize, size_t usize)
{
    int i;
    uint8_t b;
    if (usize <= ksize) {
        return 1;
    }
    for (i = ksize; i < usize; i++) {
        if (get_user_u8(b, addr + i)) {
            return -TARGET_EFAULT;
        }
        if (b != 0) {
            return 0;
        }
    }
    return 1;
}

#define safe_syscall0(type, name) \
static type safe_##name(void) \
{ \
    return safe_syscall(__NR_##name); \
}

#define safe_syscall1(type, name, type1, arg1) \
static type safe_##name(type1 arg1) \
{ \
    return safe_syscall(__NR_##name, arg1); \
}

#define safe_syscall2(type, name, type1, arg1, type2, arg2) \
static type safe_##name(type1 arg1, type2 arg2) \
{ \
    return safe_syscall(__NR_##name, arg1, arg2); \
}

#define safe_syscall3(type, name, type1, arg1, type2, arg2, type3, arg3) \
static type safe_##name(type1 arg1, type2 arg2, type3 arg3) \
{ \
    return safe_syscall(__NR_##name, arg1, arg2, arg3); \
}

#define safe_syscall4(type, name, type1, arg1, type2, arg2, type3, arg3, \
    type4, arg4) \
static type safe_##name(type1 arg1, type2 arg2, type3 arg3, type4 arg4) \
{ \
    return safe_syscall(__NR_##name, arg1, arg2, arg3, arg4); \
}

#define safe_syscall5(type, name, type1, arg1, type2, arg2, type3, arg3, \
    type4, arg4, type5, arg5) \
static type safe_##name(type1 arg1, type2 arg2, type3 arg3, type4 arg4, \
    type5 arg5) \
{ \
    return safe_syscall(__NR_##name, arg1, arg2, arg3, arg4, arg5); \
}

#define safe_syscall6(type, name, type1, arg1, type2, arg2, type3, arg3, \
    type4, arg4, type5, arg5, type6, arg6) \
static type safe_##name(type1 arg1, type2 arg2, type3 arg3, type4 arg4, \
    type5 arg5, type6 arg6) \
{ \
    return safe_syscall(__NR_##name, arg1, arg2, arg3, arg4, arg5, arg6); \
}

safe_syscall3(ssize_t, read, int, fd, void *, buff, size_t, count)
safe_syscall3(ssize_t, write, int, fd, const void *, buff, size_t, count)
safe_syscall4(int, openat, int, dirfd, const char *, pathname, \
              int, flags, mode_t, mode)
#if defined(TARGET_NR_wait4) || defined(TARGET_NR_waitpid)
safe_syscall4(pid_t, wait4, pid_t, pid, int *, status, int, options, \
              struct rusage *, rusage)
#endif
safe_syscall5(int, waitid, idtype_t, idtype, id_t, id, siginfo_t *, infop, \
              int, options, struct rusage *, rusage)
safe_syscall3(int, execve, const char *, filename, char **, argv, char **, envp)
#if defined(TARGET_NR_select) || defined(TARGET_NR__newselect) || \
    defined(TARGET_NR_pselect6) || defined(TARGET_NR_pselect6_time64)
safe_syscall6(int, pselect6, int, nfds, fd_set *, readfds, fd_set *, writefds, \
              fd_set *, exceptfds, struct timespec *, timeout, void *, sig)
#endif
#if defined(TARGET_NR_ppoll) || defined(TARGET_NR_ppoll_time64)
safe_syscall5(int, ppoll, struct pollfd *, ufds, unsigned int, nfds,
              struct timespec *, tsp, const sigset_t *, sigmask,
              size_t, sigsetsize)
#endif
safe_syscall6(int, epoll_pwait, int, epfd, struct epoll_event *, events,
              int, maxevents, int, timeout, const sigset_t *, sigmask,
              size_t, sigsetsize)
#if defined(__NR_futex)
safe_syscall6(int,futex,int *,uaddr,int,op,int,val, \
              const struct timespec *,timeout,int *,uaddr2,int,val3)
#endif
#if defined(__NR_futex_time64)
safe_syscall6(int,futex_time64,int *,uaddr,int,op,int,val, \
              const struct timespec *,timeout,int *,uaddr2,int,val3)
#endif
safe_syscall2(int, rt_sigsuspend, sigset_t *, newset, size_t, sigsetsize)
safe_syscall2(int, kill, pid_t, pid, int, sig)
safe_syscall2(int, tkill, int, tid, int, sig)
safe_syscall3(int, tgkill, int, tgid, int, pid, int, sig)
safe_syscall3(ssize_t, readv, int, fd, const struct iovec *, iov, int, iovcnt)
safe_syscall3(ssize_t, writev, int, fd, const struct iovec *, iov, int, iovcnt)
safe_syscall5(ssize_t, preadv, int, fd, const struct iovec *, iov, int, iovcnt,
              unsigned long, pos_l, unsigned long, pos_h)
safe_syscall5(ssize_t, pwritev, int, fd, const struct iovec *, iov, int, iovcnt,
              unsigned long, pos_l, unsigned long, pos_h)
safe_syscall3(int, connect, int, fd, const struct sockaddr *, addr,
              socklen_t, addrlen)
safe_syscall6(ssize_t, sendto, int, fd, const void *, buf, size_t, len,
              int, flags, const struct sockaddr *, addr, socklen_t, addrlen)
safe_syscall6(ssize_t, recvfrom, int, fd, void *, buf, size_t, len,
              int, flags, struct sockaddr *, addr, socklen_t *, addrlen)
safe_syscall3(ssize_t, sendmsg, int, fd, const struct msghdr *, msg, int, flags)
safe_syscall3(ssize_t, recvmsg, int, fd, struct msghdr *, msg, int, flags)
safe_syscall2(int, flock, int, fd, int, operation)
#if defined(TARGET_NR_rt_sigtimedwait) || defined(TARGET_NR_rt_sigtimedwait_time64)
safe_syscall4(int, rt_sigtimedwait, const sigset_t *, these, siginfo_t *, uinfo,
              const struct timespec *, uts, size_t, sigsetsize)
#endif
safe_syscall4(int, accept4, int, fd, struct sockaddr *, addr, socklen_t *, len,
              int, flags)
#if defined(TARGET_NR_nanosleep)
safe_syscall2(int, nanosleep, const struct timespec *, req,
              struct timespec *, rem)
#endif
#if defined(TARGET_NR_clock_nanosleep) || \
    defined(TARGET_NR_clock_nanosleep_time64)
safe_syscall4(int, clock_nanosleep, const clockid_t, clock, int, flags,
              const struct timespec *, req, struct timespec *, rem)
#endif
#ifdef __NR_ipc
#ifdef __s390x__
safe_syscall5(int, ipc, int, call, long, first, long, second, long, third,
              void *, ptr)
#else
safe_syscall6(int, ipc, int, call, long, first, long, second, long, third,
              void *, ptr, long, fifth)
#endif
#endif
#ifdef __NR_msgsnd
safe_syscall4(int, msgsnd, int, msgid, const void *, msgp, size_t, sz,
              int, flags)
#endif
#ifdef __NR_msgrcv
safe_syscall5(int, msgrcv, int, msgid, void *, msgp, size_t, sz,
              long, msgtype, int, flags)
#endif
#ifdef __NR_semtimedop
safe_syscall4(int, semtimedop, int, semid, struct sembuf *, tsops,
              unsigned, nsops, const struct timespec *, timeout)
#endif
#if defined(TARGET_NR_mq_timedsend) || \
    defined(TARGET_NR_mq_timedsend_time64)
safe_syscall5(int, mq_timedsend, int, mqdes, const char *, msg_ptr,
              size_t, len, unsigned, prio, const struct timespec *, timeout)
#endif
#if defined(TARGET_NR_mq_timedreceive) || \
    defined(TARGET_NR_mq_timedreceive_time64)
safe_syscall5(int, mq_timedreceive, int, mqdes, char *, msg_ptr,
              size_t, len, unsigned *, prio, const struct timespec *, timeout)
#endif
#if defined(TARGET_NR_copy_file_range) && defined(__NR_copy_file_range)
safe_syscall6(ssize_t, copy_file_range, int, infd, loff_t *, pinoff,
              int, outfd, loff_t *, poutoff, size_t, length,
              unsigned int, flags)
#endif

/* We do ioctl like this rather than via safe_syscall3 to preserve the
 * "third argument might be integer or pointer or not present" behaviour of
 * the libc function.
 */
#define safe_ioctl(...) safe_syscall(__NR_ioctl, __VA_ARGS__)
/* Similarly for fcntl. Note that callers must always:
 *  pass the F_GETLK64 etc constants rather than the unsuffixed F_GETLK
 *  use the flock64 struct rather than unsuffixed flock
 * This will then work and use a 64-bit offset for both 32-bit and 64-bit hosts.
 */
#ifdef __NR_fcntl64
#define safe_fcntl(...) safe_syscall(__NR_fcntl64, __VA_ARGS__)
#else
#define safe_fcntl(...) safe_syscall(__NR_fcntl, __VA_ARGS__)
#endif

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
    abi_ulong new_alloc_size;

    /* brk pointers are always untagged */

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
            memset(g2h_untagged(target_brk), 0, new_brk - target_brk);
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
        memset(g2h_untagged(target_brk), 0, brk_page - target_brk);

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

#if defined(TARGET_NR_select) || defined(TARGET_NR__newselect) || \
    defined(TARGET_NR_pselect6) || defined(TARGET_NR_pselect6_time64)
static inline abi_long copy_from_user_fdset(fd_set *fds,
                                            abi_ulong target_fds_addr,
                                            int n)
{
    int i, nw, j, k;
    abi_ulong b, *target_fds;

    nw = DIV_ROUND_UP(n, TARGET_ABI_BITS);
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

    nw = DIV_ROUND_UP(n, TARGET_ABI_BITS);
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
#endif

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

#ifdef TARGET_NR_setrlimit
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
#endif

#if defined(TARGET_NR_getrlimit) || defined(TARGET_NR_ugetrlimit)
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
#endif

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
#ifdef RLIMIT_RTTIME
    case TARGET_RLIMIT_RTTIME:
        return RLIMIT_RTTIME;
#endif
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

    if (!lock_user_struct(VERIFY_READ, target_tv, target_tv_addr, 1)) {
        return -TARGET_EFAULT;
    }

    __get_user(tv->tv_sec, &target_tv->tv_sec);
    __get_user(tv->tv_usec, &target_tv->tv_usec);

    unlock_user_struct(target_tv, target_tv_addr, 0);

    return 0;
}

static inline abi_long copy_to_user_timeval(abi_ulong target_tv_addr,
                                            const struct timeval *tv)
{
    struct target_timeval *target_tv;

    if (!lock_user_struct(VERIFY_WRITE, target_tv, target_tv_addr, 0)) {
        return -TARGET_EFAULT;
    }

    __put_user(tv->tv_sec, &target_tv->tv_sec);
    __put_user(tv->tv_usec, &target_tv->tv_usec);

    unlock_user_struct(target_tv, target_tv_addr, 1);

    return 0;
}

#if defined(TARGET_NR_clock_adjtime64) && defined(CONFIG_CLOCK_ADJTIME)
static inline abi_long copy_from_user_timeval64(struct timeval *tv,
                                                abi_ulong target_tv_addr)
{
    struct target__kernel_sock_timeval *target_tv;

    if (!lock_user_struct(VERIFY_READ, target_tv, target_tv_addr, 1)) {
        return -TARGET_EFAULT;
    }

    __get_user(tv->tv_sec, &target_tv->tv_sec);
    __get_user(tv->tv_usec, &target_tv->tv_usec);

    unlock_user_struct(target_tv, target_tv_addr, 0);

    return 0;
}
#endif

static inline abi_long copy_to_user_timeval64(abi_ulong target_tv_addr,
                                              const struct timeval *tv)
{
    struct target__kernel_sock_timeval *target_tv;

    if (!lock_user_struct(VERIFY_WRITE, target_tv, target_tv_addr, 0)) {
        return -TARGET_EFAULT;
    }

    __put_user(tv->tv_sec, &target_tv->tv_sec);
    __put_user(tv->tv_usec, &target_tv->tv_usec);

    unlock_user_struct(target_tv, target_tv_addr, 1);

    return 0;
}

#if defined(TARGET_NR_futex) || \
    defined(TARGET_NR_rt_sigtimedwait) || \
    defined(TARGET_NR_pselect6) || defined(TARGET_NR_pselect6) || \
    defined(TARGET_NR_nanosleep) || defined(TARGET_NR_clock_settime) || \
    defined(TARGET_NR_utimensat) || defined(TARGET_NR_mq_timedsend) || \
    defined(TARGET_NR_mq_timedreceive) || defined(TARGET_NR_ipc) || \
    defined(TARGET_NR_semop) || defined(TARGET_NR_semtimedop) || \
    defined(TARGET_NR_timer_settime) || \
    (defined(TARGET_NR_timerfd_settime) && defined(CONFIG_TIMERFD))
static inline abi_long target_to_host_timespec(struct timespec *host_ts,
                                               abi_ulong target_addr)
{
    struct target_timespec *target_ts;

    if (!lock_user_struct(VERIFY_READ, target_ts, target_addr, 1)) {
        return -TARGET_EFAULT;
    }
    __get_user(host_ts->tv_sec, &target_ts->tv_sec);
    __get_user(host_ts->tv_nsec, &target_ts->tv_nsec);
    unlock_user_struct(target_ts, target_addr, 0);
    return 0;
}
#endif

#if defined(TARGET_NR_clock_settime64) || defined(TARGET_NR_futex_time64) || \
    defined(TARGET_NR_timer_settime64) || \
    defined(TARGET_NR_mq_timedsend_time64) || \
    defined(TARGET_NR_mq_timedreceive_time64) || \
    (defined(TARGET_NR_timerfd_settime64) && defined(CONFIG_TIMERFD)) || \
    defined(TARGET_NR_clock_nanosleep_time64) || \
    defined(TARGET_NR_rt_sigtimedwait_time64) || \
    defined(TARGET_NR_utimensat) || \
    defined(TARGET_NR_utimensat_time64) || \
    defined(TARGET_NR_semtimedop_time64) || \
    defined(TARGET_NR_pselect6_time64) || defined(TARGET_NR_ppoll_time64)
static inline abi_long target_to_host_timespec64(struct timespec *host_ts,
                                                 abi_ulong target_addr)
{
    struct target__kernel_timespec *target_ts;

    if (!lock_user_struct(VERIFY_READ, target_ts, target_addr, 1)) {
        return -TARGET_EFAULT;
    }
    __get_user(host_ts->tv_sec, &target_ts->tv_sec);
    __get_user(host_ts->tv_nsec, &target_ts->tv_nsec);
    /* in 32bit mode, this drops the padding */
    host_ts->tv_nsec = (long)(abi_long)host_ts->tv_nsec;
    unlock_user_struct(target_ts, target_addr, 0);
    return 0;
}
#endif

static inline abi_long host_to_target_timespec(abi_ulong target_addr,
                                               struct timespec *host_ts)
{
    struct target_timespec *target_ts;

    if (!lock_user_struct(VERIFY_WRITE, target_ts, target_addr, 0)) {
        return -TARGET_EFAULT;
    }
    __put_user(host_ts->tv_sec, &target_ts->tv_sec);
    __put_user(host_ts->tv_nsec, &target_ts->tv_nsec);
    unlock_user_struct(target_ts, target_addr, 1);
    return 0;
}

static inline abi_long host_to_target_timespec64(abi_ulong target_addr,
                                                 struct timespec *host_ts)
{
    struct target__kernel_timespec *target_ts;

    if (!lock_user_struct(VERIFY_WRITE, target_ts, target_addr, 0)) {
        return -TARGET_EFAULT;
    }
    __put_user(host_ts->tv_sec, &target_ts->tv_sec);
    __put_user(host_ts->tv_nsec, &target_ts->tv_nsec);
    unlock_user_struct(target_ts, target_addr, 1);
    return 0;
}

#if defined(TARGET_NR_gettimeofday)
static inline abi_long copy_to_user_timezone(abi_ulong target_tz_addr,
                                             struct timezone *tz)
{
    struct target_timezone *target_tz;

    if (!lock_user_struct(VERIFY_WRITE, target_tz, target_tz_addr, 1)) {
        return -TARGET_EFAULT;
    }

    __put_user(tz->tz_minuteswest, &target_tz->tz_minuteswest);
    __put_user(tz->tz_dsttime, &target_tz->tz_dsttime);

    unlock_user_struct(target_tz, target_tz_addr, 1);

    return 0;
}
#endif

#if defined(TARGET_NR_settimeofday)
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
#endif

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
    struct timeval tv;
    struct timespec ts, *ts_ptr;
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
        ts.tv_sec = tv.tv_sec;
        ts.tv_nsec = tv.tv_usec * 1000;
        ts_ptr = &ts;
    } else {
        ts_ptr = NULL;
    }

    ret = get_errno(safe_pselect6(n, rfds_ptr, wfds_ptr, efds_ptr,
                                  ts_ptr, NULL));

    if (!is_error(ret)) {
        if (rfd_addr && copy_to_user_fdset(rfd_addr, &rfds, n))
            return -TARGET_EFAULT;
        if (wfd_addr && copy_to_user_fdset(wfd_addr, &wfds, n))
            return -TARGET_EFAULT;
        if (efd_addr && copy_to_user_fdset(efd_addr, &efds, n))
            return -TARGET_EFAULT;

        if (target_tv_addr) {
            tv.tv_sec = ts.tv_sec;
            tv.tv_usec = ts.tv_nsec / 1000;
            if (copy_to_user_timeval(target_tv_addr, &tv)) {
                return -TARGET_EFAULT;
            }
        }
    }

    return ret;
}

#if defined(TARGET_WANT_OLD_SYS_SELECT)
static abi_long do_old_select(abi_ulong arg1)
{
    struct target_sel_arg_struct *sel;
    abi_ulong inp, outp, exp, tvp;
    long nsel;

    if (!lock_user_struct(VERIFY_READ, sel, arg1, 1)) {
        return -TARGET_EFAULT;
    }

    nsel = tswapal(sel->n);
    inp = tswapal(sel->inp);
    outp = tswapal(sel->outp);
    exp = tswapal(sel->exp);
    tvp = tswapal(sel->tvp);

    unlock_user_struct(sel, arg1, 0);

    return do_select(nsel, inp, outp, exp, tvp);
}
#endif
#endif

#if defined(TARGET_NR_pselect6) || defined(TARGET_NR_pselect6_time64)
static abi_long do_pselect6(abi_long arg1, abi_long arg2, abi_long arg3,
                            abi_long arg4, abi_long arg5, abi_long arg6,
                            bool time64)
{
    abi_long rfd_addr, wfd_addr, efd_addr, n, ts_addr;
    fd_set rfds, wfds, efds;
    fd_set *rfds_ptr, *wfds_ptr, *efds_ptr;
    struct timespec ts, *ts_ptr;
    abi_long ret;

    /*
     * The 6th arg is actually two args smashed together,
     * so we cannot use the C library.
     */
    struct {
        sigset_t *set;
        size_t size;
    } sig, *sig_ptr;

    abi_ulong arg_sigset, arg_sigsize, *arg7;

    n = arg1;
    rfd_addr = arg2;
    wfd_addr = arg3;
    efd_addr = arg4;
    ts_addr = arg5;

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

    /*
     * This takes a timespec, and not a timeval, so we cannot
     * use the do_select() helper ...
     */
    if (ts_addr) {
        if (time64) {
            if (target_to_host_timespec64(&ts, ts_addr)) {
                return -TARGET_EFAULT;
            }
        } else {
            if (target_to_host_timespec(&ts, ts_addr)) {
                return -TARGET_EFAULT;
            }
        }
            ts_ptr = &ts;
    } else {
        ts_ptr = NULL;
    }

    /* Extract the two packed args for the sigset */
    sig_ptr = NULL;
    if (arg6) {
        arg7 = lock_user(VERIFY_READ, arg6, sizeof(*arg7) * 2, 1);
        if (!arg7) {
            return -TARGET_EFAULT;
        }
        arg_sigset = tswapal(arg7[0]);
        arg_sigsize = tswapal(arg7[1]);
        unlock_user(arg7, arg6, 0);

        if (arg_sigset) {
            ret = process_sigsuspend_mask(&sig.set, arg_sigset, arg_sigsize);
            if (ret != 0) {
                return ret;
            }
            sig_ptr = &sig;
            sig.size = SIGSET_T_SIZE;
        }
    }

    ret = get_errno(safe_pselect6(n, rfds_ptr, wfds_ptr, efds_ptr,
                                  ts_ptr, sig_ptr));

    if (sig_ptr) {
        finish_sigsuspend_mask(ret);
    }

    if (!is_error(ret)) {
        if (rfd_addr && copy_to_user_fdset(rfd_addr, &rfds, n)) {
            return -TARGET_EFAULT;
        }
        if (wfd_addr && copy_to_user_fdset(wfd_addr, &wfds, n)) {
            return -TARGET_EFAULT;
        }
        if (efd_addr && copy_to_user_fdset(efd_addr, &efds, n)) {
            return -TARGET_EFAULT;
        }
        if (time64) {
            if (ts_addr && host_to_target_timespec64(ts_addr, &ts)) {
                return -TARGET_EFAULT;
            }
        } else {
            if (ts_addr && host_to_target_timespec(ts_addr, &ts)) {
                return -TARGET_EFAULT;
            }
        }
    }
    return ret;
}
#endif

#if defined(TARGET_NR_poll) || defined(TARGET_NR_ppoll) || \
    defined(TARGET_NR_ppoll_time64)
static abi_long do_ppoll(abi_long arg1, abi_long arg2, abi_long arg3,
                         abi_long arg4, abi_long arg5, bool ppoll, bool time64)
{
    struct target_pollfd *target_pfd;
    unsigned int nfds = arg2;
    struct pollfd *pfd;
    unsigned int i;
    abi_long ret;

    pfd = NULL;
    target_pfd = NULL;
    if (nfds) {
        if (nfds > (INT_MAX / sizeof(struct target_pollfd))) {
            return -TARGET_EINVAL;
        }
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
    }
    if (ppoll) {
        struct timespec _timeout_ts, *timeout_ts = &_timeout_ts;
        sigset_t *set = NULL;

        if (arg3) {
            if (time64) {
                if (target_to_host_timespec64(timeout_ts, arg3)) {
                    unlock_user(target_pfd, arg1, 0);
                    return -TARGET_EFAULT;
                }
            } else {
                if (target_to_host_timespec(timeout_ts, arg3)) {
                    unlock_user(target_pfd, arg1, 0);
                    return -TARGET_EFAULT;
                }
            }
        } else {
            timeout_ts = NULL;
        }

        if (arg4) {
            ret = process_sigsuspend_mask(&set, arg4, arg5);
            if (ret != 0) {
                unlock_user(target_pfd, arg1, 0);
                return ret;
            }
        }

        ret = get_errno(safe_ppoll(pfd, nfds, timeout_ts,
                                   set, SIGSET_T_SIZE));

        if (set) {
            finish_sigsuspend_mask(ret);
        }
        if (!is_error(ret) && arg3) {
            if (time64) {
                if (host_to_target_timespec64(arg3, timeout_ts)) {
                    return -TARGET_EFAULT;
                }
            } else {
                if (host_to_target_timespec(arg3, timeout_ts)) {
                    return -TARGET_EFAULT;
                }
            }
        }
    } else {
          struct timespec ts, *pts;

          if (arg3 >= 0) {
              /* Convert ms to secs, ns */
              ts.tv_sec = arg3 / 1000;
              ts.tv_nsec = (arg3 % 1000) * 1000000LL;
              pts = &ts;
          } else {
              /* -ve poll() timeout means "infinite" */
              pts = NULL;
          }
          ret = get_errno(safe_ppoll(pfd, nfds, pts, NULL, 0));
    }

    if (!is_error(ret)) {
        for (i = 0; i < nfds; i++) {
            target_pfd[i].revents = tswap16(pfd[i].revents);
        }
    }
    unlock_user(target_pfd, arg1, sizeof(struct target_pollfd) * nfds);
    return ret;
}
#endif

static abi_long do_pipe(CPUArchState *cpu_env, abi_ulong pipedes,
                        int flags, int is_pipe2)
{
    int host_pipe[2];
    abi_long ret;
    ret = pipe2(host_pipe, flags);

    if (is_error(ret))
        return get_errno(ret);

    /* Several targets have special calling conventions for the original
       pipe syscall, but didn't replicate this into the pipe2 syscall.  */
    if (!is_pipe2) {
#if defined(TARGET_ALPHA)
        cpu_env->ir[IR_A4] = host_pipe[1];
        return host_pipe[0];
#elif defined(TARGET_MIPS)
        cpu_env->active_tc.gpr[3] = host_pipe[1];
        return host_pipe[0];
#elif defined(TARGET_SH4)
        cpu_env->gregs[1] = host_pipe[1];
        return host_pipe[0];
#elif defined(TARGET_SPARC)
        cpu_env->regwptr[1] = host_pipe[1];
        return host_pipe[0];
#endif
    }

    if (put_user_s32(host_pipe[0], pipedes)
        || put_user_s32(host_pipe[1], pipedes + sizeof(abi_int)))
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

static inline abi_long target_to_host_sockaddr(int fd, struct sockaddr *addr,
                                               abi_ulong target_addr,
                                               socklen_t len)
{
    const socklen_t unix_maxlen = sizeof (struct sockaddr_un);
    sa_family_t sa_family;
    struct target_sockaddr *target_saddr;

    if (fd_trans_target_to_host_addr(fd)) {
        return fd_trans_target_to_host_addr(fd)(addr, target_addr, len);
    }

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
    if (sa_family == AF_NETLINK) {
        struct sockaddr_nl *nladdr;

        nladdr = (struct sockaddr_nl *)addr;
        nladdr->nl_pid = tswap32(nladdr->nl_pid);
        nladdr->nl_groups = tswap32(nladdr->nl_groups);
    } else if (sa_family == AF_PACKET) {
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

    if (len == 0) {
        return 0;
    }
    assert(addr);

    target_saddr = lock_user(VERIFY_WRITE, target_addr, len, 0);
    if (!target_saddr)
        return -TARGET_EFAULT;
    memcpy(target_saddr, addr, len);
    if (len >= offsetof(struct target_sockaddr, sa_family) +
        sizeof(target_saddr->sa_family)) {
        target_saddr->sa_family = tswap16(addr->sa_family);
    }
    if (addr->sa_family == AF_NETLINK &&
        len >= sizeof(struct target_sockaddr_nl)) {
        struct target_sockaddr_nl *target_nl =
               (struct target_sockaddr_nl *)target_saddr;
        target_nl->nl_pid = tswap32(target_nl->nl_pid);
        target_nl->nl_groups = tswap32(target_nl->nl_groups);
    } else if (addr->sa_family == AF_PACKET) {
        struct sockaddr_ll *target_ll = (struct sockaddr_ll *)target_saddr;
        target_ll->sll_ifindex = tswap32(target_ll->sll_ifindex);
        target_ll->sll_hatype = tswap16(target_ll->sll_hatype);
    } else if (addr->sa_family == AF_INET6 &&
               len >= sizeof(struct target_sockaddr_in6)) {
        struct target_sockaddr_in6 *target_in6 =
               (struct target_sockaddr_in6 *)target_saddr;
        target_in6->sin6_scope_id = tswap16(target_in6->sin6_scope_id);
    }
    unlock_user(target_saddr, target_addr, len);

    return 0;
}

static inline abi_long target_to_host_cmsg(struct msghdr *msgh,
                                           struct target_msghdr *target_msgh)
{
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(msgh);
    abi_long msg_controllen;
    abi_ulong target_cmsg_addr;
    struct target_cmsghdr *target_cmsg, *target_cmsg_start;
    socklen_t space = 0;
    
    msg_controllen = tswapal(target_msgh->msg_controllen);
    if (msg_controllen < sizeof (struct target_cmsghdr)) 
        goto the_end;
    target_cmsg_addr = tswapal(target_msgh->msg_control);
    target_cmsg = lock_user(VERIFY_READ, target_cmsg_addr, msg_controllen, 1);
    target_cmsg_start = target_cmsg;
    if (!target_cmsg)
        return -TARGET_EFAULT;

    while (cmsg && target_cmsg) {
        void *data = CMSG_DATA(cmsg);
        void *target_data = TARGET_CMSG_DATA(target_cmsg);

        int len = tswapal(target_cmsg->cmsg_len)
            - sizeof(struct target_cmsghdr);

        space += CMSG_SPACE(len);
        if (space > msgh->msg_controllen) {
            space -= CMSG_SPACE(len);
            /* This is a QEMU bug, since we allocated the payload
             * area ourselves (unlike overflow in host-to-target
             * conversion, which is just the guest giving us a buffer
             * that's too small). It can't happen for the payload types
             * we currently support; if it becomes an issue in future
             * we would need to improve our allocation strategy to
             * something more intelligent than "twice the size of the
             * target buffer we're reading from".
             */
            qemu_log_mask(LOG_UNIMP,
                          ("Unsupported ancillary data %d/%d: "
                           "unhandled msg size\n"),
                          tswap32(target_cmsg->cmsg_level),
                          tswap32(target_cmsg->cmsg_type));
            break;
        }

        if (tswap32(target_cmsg->cmsg_level) == TARGET_SOL_SOCKET) {
            cmsg->cmsg_level = SOL_SOCKET;
        } else {
            cmsg->cmsg_level = tswap32(target_cmsg->cmsg_level);
        }
        cmsg->cmsg_type = tswap32(target_cmsg->cmsg_type);
        cmsg->cmsg_len = CMSG_LEN(len);

        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
            int *fd = (int *)data;
            int *target_fd = (int *)target_data;
            int i, numfds = len / sizeof(int);

            for (i = 0; i < numfds; i++) {
                __get_user(fd[i], target_fd + i);
            }
        } else if (cmsg->cmsg_level == SOL_SOCKET
               &&  cmsg->cmsg_type == SCM_CREDENTIALS) {
            struct ucred *cred = (struct ucred *)data;
            struct target_ucred *target_cred =
                (struct target_ucred *)target_data;

            __get_user(cred->pid, &target_cred->pid);
            __get_user(cred->uid, &target_cred->uid);
            __get_user(cred->gid, &target_cred->gid);
        } else {
            qemu_log_mask(LOG_UNIMP, "Unsupported ancillary data: %d/%d\n",
                          cmsg->cmsg_level, cmsg->cmsg_type);
            memcpy(data, target_data, len);
        }

        cmsg = CMSG_NXTHDR(msgh, cmsg);
        target_cmsg = TARGET_CMSG_NXTHDR(target_msgh, target_cmsg,
                                         target_cmsg_start);
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
    struct target_cmsghdr *target_cmsg, *target_cmsg_start;
    socklen_t space = 0;

    msg_controllen = tswapal(target_msgh->msg_controllen);
    if (msg_controllen < sizeof (struct target_cmsghdr)) 
        goto the_end;
    target_cmsg_addr = tswapal(target_msgh->msg_control);
    target_cmsg = lock_user(VERIFY_WRITE, target_cmsg_addr, msg_controllen, 0);
    target_cmsg_start = target_cmsg;
    if (!target_cmsg)
        return -TARGET_EFAULT;

    while (cmsg && target_cmsg) {
        void *data = CMSG_DATA(cmsg);
        void *target_data = TARGET_CMSG_DATA(target_cmsg);

        int len = cmsg->cmsg_len - sizeof(struct cmsghdr);
        int tgt_len, tgt_space;

        /* We never copy a half-header but may copy half-data;
         * this is Linux's behaviour in put_cmsg(). Note that
         * truncation here is a guest problem (which we report
         * to the guest via the CTRUNC bit), unlike truncation
         * in target_to_host_cmsg, which is a QEMU bug.
         */
        if (msg_controllen < sizeof(struct target_cmsghdr)) {
            target_msgh->msg_flags |= tswap32(MSG_CTRUNC);
            break;
        }

        if (cmsg->cmsg_level == SOL_SOCKET) {
            target_cmsg->cmsg_level = tswap32(TARGET_SOL_SOCKET);
        } else {
            target_cmsg->cmsg_level = tswap32(cmsg->cmsg_level);
        }
        target_cmsg->cmsg_type = tswap32(cmsg->cmsg_type);

        /* Payload types which need a different size of payload on
         * the target must adjust tgt_len here.
         */
        tgt_len = len;
        switch (cmsg->cmsg_level) {
        case SOL_SOCKET:
            switch (cmsg->cmsg_type) {
            case SO_TIMESTAMP:
                tgt_len = sizeof(struct target_timeval);
                break;
            default:
                break;
            }
            break;
        default:
            break;
        }

        if (msg_controllen < TARGET_CMSG_LEN(tgt_len)) {
            target_msgh->msg_flags |= tswap32(MSG_CTRUNC);
            tgt_len = msg_controllen - sizeof(struct target_cmsghdr);
        }

        /* We must now copy-and-convert len bytes of payload
         * into tgt_len bytes of destination space. Bear in mind
         * that in both source and destination we may be dealing
         * with a truncated value!
         */
        switch (cmsg->cmsg_level) {
        case SOL_SOCKET:
            switch (cmsg->cmsg_type) {
            case SCM_RIGHTS:
            {
                int *fd = (int *)data;
                int *target_fd = (int *)target_data;
                int i, numfds = tgt_len / sizeof(int);

                for (i = 0; i < numfds; i++) {
                    __put_user(fd[i], target_fd + i);
                }
                break;
            }
            case SO_TIMESTAMP:
            {
                struct timeval *tv = (struct timeval *)data;
                struct target_timeval *target_tv =
                    (struct target_timeval *)target_data;

                if (len != sizeof(struct timeval) ||
                    tgt_len != sizeof(struct target_timeval)) {
                    goto unimplemented;
                }

                /* copy struct timeval to target */
                __put_user(tv->tv_sec, &target_tv->tv_sec);
                __put_user(tv->tv_usec, &target_tv->tv_usec);
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

        case SOL_IP:
            switch (cmsg->cmsg_type) {
            case IP_TTL:
            {
                uint32_t *v = (uint32_t *)data;
                uint32_t *t_int = (uint32_t *)target_data;

                if (len != sizeof(uint32_t) ||
                    tgt_len != sizeof(uint32_t)) {
                    goto unimplemented;
                }
                __put_user(*v, t_int);
                break;
            }
            case IP_RECVERR:
            {
                struct errhdr_t {
                   struct sock_extended_err ee;
                   struct sockaddr_in offender;
                };
                struct errhdr_t *errh = (struct errhdr_t *)data;
                struct errhdr_t *target_errh =
                    (struct errhdr_t *)target_data;

                if (len != sizeof(struct errhdr_t) ||
                    tgt_len != sizeof(struct errhdr_t)) {
                    goto unimplemented;
                }
                __put_user(errh->ee.ee_errno, &target_errh->ee.ee_errno);
                __put_user(errh->ee.ee_origin, &target_errh->ee.ee_origin);
                __put_user(errh->ee.ee_type,  &target_errh->ee.ee_type);
                __put_user(errh->ee.ee_code, &target_errh->ee.ee_code);
                __put_user(errh->ee.ee_pad, &target_errh->ee.ee_pad);
                __put_user(errh->ee.ee_info, &target_errh->ee.ee_info);
                __put_user(errh->ee.ee_data, &target_errh->ee.ee_data);
                host_to_target_sockaddr((unsigned long) &target_errh->offender,
                    (void *) &errh->offender, sizeof(errh->offender));
                break;
            }
            default:
                goto unimplemented;
            }
            break;

        case SOL_IPV6:
            switch (cmsg->cmsg_type) {
            case IPV6_HOPLIMIT:
            {
                uint32_t *v = (uint32_t *)data;
                uint32_t *t_int = (uint32_t *)target_data;

                if (len != sizeof(uint32_t) ||
                    tgt_len != sizeof(uint32_t)) {
                    goto unimplemented;
                }
                __put_user(*v, t_int);
                break;
            }
            case IPV6_RECVERR:
            {
                struct errhdr6_t {
                   struct sock_extended_err ee;
                   struct sockaddr_in6 offender;
                };
                struct errhdr6_t *errh = (struct errhdr6_t *)data;
                struct errhdr6_t *target_errh =
                    (struct errhdr6_t *)target_data;

                if (len != sizeof(struct errhdr6_t) ||
                    tgt_len != sizeof(struct errhdr6_t)) {
                    goto unimplemented;
                }
                __put_user(errh->ee.ee_errno, &target_errh->ee.ee_errno);
                __put_user(errh->ee.ee_origin, &target_errh->ee.ee_origin);
                __put_user(errh->ee.ee_type,  &target_errh->ee.ee_type);
                __put_user(errh->ee.ee_code, &target_errh->ee.ee_code);
                __put_user(errh->ee.ee_pad, &target_errh->ee.ee_pad);
                __put_user(errh->ee.ee_info, &target_errh->ee.ee_info);
                __put_user(errh->ee.ee_data, &target_errh->ee.ee_data);
                host_to_target_sockaddr((unsigned long) &target_errh->offender,
                    (void *) &errh->offender, sizeof(errh->offender));
                break;
            }
            default:
                goto unimplemented;
            }
            break;

        default:
        unimplemented:
            qemu_log_mask(LOG_UNIMP, "Unsupported ancillary data: %d/%d\n",
                          cmsg->cmsg_level, cmsg->cmsg_type);
            memcpy(target_data, data, MIN(len, tgt_len));
            if (tgt_len > len) {
                memset(target_data + len, 0, tgt_len - len);
            }
        }

        target_cmsg->cmsg_len = tswapal(TARGET_CMSG_LEN(tgt_len));
        tgt_space = TARGET_CMSG_SPACE(tgt_len);
        if (msg_controllen < tgt_space) {
            tgt_space = msg_controllen;
        }
        msg_controllen -= tgt_space;
        space += tgt_space;
        cmsg = CMSG_NXTHDR(msgh, cmsg);
        target_cmsg = TARGET_CMSG_NXTHDR(target_msgh, target_cmsg,
                                         target_cmsg_start);
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
    case SOL_UDP:
        /* TCP and UDP options all take an 'int' value.  */
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
        case IP_RECVTTL:
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
            if (!ip_mreq_source) {
                return -TARGET_EFAULT;
            }
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
        case IPV6_UNICAST_HOPS:
        case IPV6_MULTICAST_HOPS:
        case IPV6_MULTICAST_LOOP:
        case IPV6_RECVERR:
        case IPV6_RECVHOPLIMIT:
        case IPV6_2292HOPLIMIT:
        case IPV6_CHECKSUM:
        case IPV6_ADDRFORM:
        case IPV6_2292PKTINFO:
        case IPV6_RECVTCLASS:
        case IPV6_RECVRTHDR:
        case IPV6_2292RTHDR:
        case IPV6_RECVHOPOPTS:
        case IPV6_2292HOPOPTS:
        case IPV6_RECVDSTOPTS:
        case IPV6_2292DSTOPTS:
        case IPV6_TCLASS:
        case IPV6_ADDR_PREFERENCES:
#ifdef IPV6_RECVPATHMTU
        case IPV6_RECVPATHMTU:
#endif
#ifdef IPV6_TRANSPARENT
        case IPV6_TRANSPARENT:
#endif
#ifdef IPV6_FREEBIND
        case IPV6_FREEBIND:
#endif
#ifdef IPV6_RECVORIGDSTADDR
        case IPV6_RECVORIGDSTADDR:
#endif
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
        case IPV6_PKTINFO:
        {
            struct in6_pktinfo pki;

            if (optlen < sizeof(pki)) {
                return -TARGET_EINVAL;
            }

            if (copy_from_user(&pki, optval_addr, sizeof(pki))) {
                return -TARGET_EFAULT;
            }

            pki.ipi6_ifindex = tswap32(pki.ipi6_ifindex);

            ret = get_errno(setsockopt(sockfd, level, optname,
                                       &pki, sizeof(pki)));
            break;
        }
        case IPV6_ADD_MEMBERSHIP:
        case IPV6_DROP_MEMBERSHIP:
        {
            struct ipv6_mreq ipv6mreq;

            if (optlen < sizeof(ipv6mreq)) {
                return -TARGET_EINVAL;
            }

            if (copy_from_user(&ipv6mreq, optval_addr, sizeof(ipv6mreq))) {
                return -TARGET_EFAULT;
            }

            ipv6mreq.ipv6mr_interface = tswap32(ipv6mreq.ipv6mr_interface);

            ret = get_errno(setsockopt(sockfd, level, optname,
                                       &ipv6mreq, sizeof(ipv6mreq)));
            break;
        }
        default:
            goto unimplemented;
        }
        break;
    case SOL_ICMPV6:
        switch (optname) {
        case ICMPV6_FILTER:
        {
            struct icmp6_filter icmp6f;

            if (optlen > sizeof(icmp6f)) {
                optlen = sizeof(icmp6f);
            }

            if (copy_from_user(&icmp6f, optval_addr, optlen)) {
                return -TARGET_EFAULT;
            }

            for (val = 0; val < 8; val++) {
                icmp6f.data[val] = tswap32(icmp6f.data[val]);
            }

            ret = get_errno(setsockopt(sockfd, level, optname,
                                       &icmp6f, optlen));
            break;
        }
        default:
            goto unimplemented;
        }
        break;
    case SOL_RAW:
        switch (optname) {
        case ICMP_FILTER:
        case IPV6_CHECKSUM:
            /* those take an u32 value */
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
#if defined(SOL_ALG) && defined(ALG_SET_KEY) && defined(ALG_SET_AEAD_AUTHSIZE)
    case SOL_ALG:
        switch (optname) {
        case ALG_SET_KEY:
        {
            char *alg_key = g_malloc(optlen);

            if (!alg_key) {
                return -TARGET_ENOMEM;
            }
            if (copy_from_user(alg_key, optval_addr, optlen)) {
                g_free(alg_key);
                return -TARGET_EFAULT;
            }
            ret = get_errno(setsockopt(sockfd, level, optname,
                                       alg_key, optlen));
            g_free(alg_key);
            break;
        }
        case ALG_SET_AEAD_AUTHSIZE:
        {
            ret = get_errno(setsockopt(sockfd, level, optname,
                                       NULL, optlen));
            break;
        }
        default:
            goto unimplemented;
        }
        break;
#endif
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
                filter = g_try_new(struct sock_filter, fprog.len);
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
                g_free(filter);

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
		ret = get_errno(setsockopt(sockfd, SOL_SOCKET, optname,
                                           addr_ifname, optlen));
		unlock_user (dev_ifname, optval_addr, 0);
		return ret;
	}
        case TARGET_SO_LINGER:
        {
                struct linger lg;
                struct target_linger *tlg;

                if (optlen != sizeof(struct target_linger)) {
                    return -TARGET_EINVAL;
                }
                if (!lock_user_struct(VERIFY_READ, tlg, optval_addr, 1)) {
                    return -TARGET_EFAULT;
                }
                __get_user(lg.l_onoff, &tlg->l_onoff);
                __get_user(lg.l_linger, &tlg->l_linger);
                ret = get_errno(setsockopt(sockfd, SOL_SOCKET, SO_LINGER,
                                &lg, sizeof(lg)));
                unlock_user_struct(tlg, optval_addr, 0);
                return ret;
        }
            /* Options with 'int' argument.  */
        case TARGET_SO_DEBUG:
		optname = SO_DEBUG;
		break;
        case TARGET_SO_REUSEADDR:
		optname = SO_REUSEADDR;
		break;
#ifdef SO_REUSEPORT
        case TARGET_SO_REUSEPORT:
                optname = SO_REUSEPORT;
                break;
#endif
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
        default:
            goto unimplemented;
        }
	if (optlen < sizeof(uint32_t))
            return -TARGET_EINVAL;

	if (get_user_u32(val, optval_addr))
            return -TARGET_EFAULT;
	ret = get_errno(setsockopt(sockfd, SOL_SOCKET, optname, &val, sizeof(val)));
        break;
#ifdef SOL_NETLINK
    case SOL_NETLINK:
        switch (optname) {
        case NETLINK_PKTINFO:
        case NETLINK_ADD_MEMBERSHIP:
        case NETLINK_DROP_MEMBERSHIP:
        case NETLINK_BROADCAST_ERROR:
        case NETLINK_NO_ENOBUFS:
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 2, 0)
        case NETLINK_LISTEN_ALL_NSID:
        case NETLINK_CAP_ACK:
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4, 2, 0) */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0)
        case NETLINK_EXT_ACK:
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0) */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0)
        case NETLINK_GET_STRICT_CHK:
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0) */
            break;
        default:
            goto unimplemented;
        }
        val = 0;
        if (optlen < sizeof(uint32_t)) {
            return -TARGET_EINVAL;
        }
        if (get_user_u32(val, optval_addr)) {
            return -TARGET_EFAULT;
        }
        ret = get_errno(setsockopt(sockfd, SOL_NETLINK, optname, &val,
                                   sizeof(val)));
        break;
#endif /* SOL_NETLINK */
    default:
    unimplemented:
        qemu_log_mask(LOG_UNIMP, "Unsupported setsockopt level=%d optname=%d\n",
                      level, optname);
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
        case TARGET_SO_PEERNAME:
            goto unimplemented;
        case TARGET_SO_RCVTIMEO: {
            struct timeval tv;
            socklen_t tvlen;

            optname = SO_RCVTIMEO;

get_timeout:
            if (get_user_u32(len, optlen)) {
                return -TARGET_EFAULT;
            }
            if (len < 0) {
                return -TARGET_EINVAL;
            }

            tvlen = sizeof(tv);
            ret = get_errno(getsockopt(sockfd, level, optname,
                                       &tv, &tvlen));
            if (ret < 0) {
                return ret;
            }
            if (len > sizeof(struct target_timeval)) {
                len = sizeof(struct target_timeval);
            }
            if (copy_to_user_timeval(optval_addr, &tv)) {
                return -TARGET_EFAULT;
            }
            if (put_user_u32(len, optlen)) {
                return -TARGET_EFAULT;
            }
            break;
        }
        case TARGET_SO_SNDTIMEO:
            optname = SO_SNDTIMEO;
            goto get_timeout;
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
        case TARGET_SO_PEERSEC: {
            char *name;

            if (get_user_u32(len, optlen)) {
                return -TARGET_EFAULT;
            }
            if (len < 0) {
                return -TARGET_EINVAL;
            }
            name = lock_user(VERIFY_WRITE, optval_addr, len, 0);
            if (!name) {
                return -TARGET_EFAULT;
            }
            lv = len;
            ret = get_errno(getsockopt(sockfd, level, SO_PEERSEC,
                                       name, &lv));
            if (put_user_u32(lv, optlen)) {
                ret = -TARGET_EFAULT;
            }
            unlock_user(name, optval_addr, lv);
            break;
        }
        case TARGET_SO_LINGER:
        {
            struct linger lg;
            socklen_t lglen;
            struct target_linger *tlg;

            if (get_user_u32(len, optlen)) {
                return -TARGET_EFAULT;
            }
            if (len < 0) {
                return -TARGET_EINVAL;
            }

            lglen = sizeof(lg);
            ret = get_errno(getsockopt(sockfd, level, SO_LINGER,
                                       &lg, &lglen));
            if (ret < 0) {
                return ret;
            }
            if (len > lglen) {
                len = lglen;
            }
            if (!lock_user_struct(VERIFY_WRITE, tlg, optval_addr, 0)) {
                return -TARGET_EFAULT;
            }
            __put_user(lg.l_onoff, &tlg->l_onoff);
            __put_user(lg.l_linger, &tlg->l_linger);
            unlock_user_struct(tlg, optval_addr, 1);
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
#ifdef SO_REUSEPORT
        case TARGET_SO_REUSEPORT:
            optname = SO_REUSEPORT;
            goto int_case;
#endif
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
        case TARGET_SO_PROTOCOL:
            optname = SO_PROTOCOL;
            goto int_case;
        case TARGET_SO_DOMAIN:
            optname = SO_DOMAIN;
            goto int_case;
        default:
            goto int_case;
        }
        break;
    case SOL_TCP:
    case SOL_UDP:
        /* TCP and UDP options all take an 'int' value.  */
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
    case SOL_IPV6:
        switch (optname) {
        case IPV6_MTU_DISCOVER:
        case IPV6_MTU:
        case IPV6_V6ONLY:
        case IPV6_RECVPKTINFO:
        case IPV6_UNICAST_HOPS:
        case IPV6_MULTICAST_HOPS:
        case IPV6_MULTICAST_LOOP:
        case IPV6_RECVERR:
        case IPV6_RECVHOPLIMIT:
        case IPV6_2292HOPLIMIT:
        case IPV6_CHECKSUM:
        case IPV6_ADDRFORM:
        case IPV6_2292PKTINFO:
        case IPV6_RECVTCLASS:
        case IPV6_RECVRTHDR:
        case IPV6_2292RTHDR:
        case IPV6_RECVHOPOPTS:
        case IPV6_2292HOPOPTS:
        case IPV6_RECVDSTOPTS:
        case IPV6_2292DSTOPTS:
        case IPV6_TCLASS:
        case IPV6_ADDR_PREFERENCES:
#ifdef IPV6_RECVPATHMTU
        case IPV6_RECVPATHMTU:
#endif
#ifdef IPV6_TRANSPARENT
        case IPV6_TRANSPARENT:
#endif
#ifdef IPV6_FREEBIND
        case IPV6_FREEBIND:
#endif
#ifdef IPV6_RECVORIGDSTADDR
        case IPV6_RECVORIGDSTADDR:
#endif
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
#ifdef SOL_NETLINK
    case SOL_NETLINK:
        switch (optname) {
        case NETLINK_PKTINFO:
        case NETLINK_BROADCAST_ERROR:
        case NETLINK_NO_ENOBUFS:
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 2, 0)
        case NETLINK_LISTEN_ALL_NSID:
        case NETLINK_CAP_ACK:
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4, 2, 0) */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0)
        case NETLINK_EXT_ACK:
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0) */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0)
        case NETLINK_GET_STRICT_CHK:
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0) */
            if (get_user_u32(len, optlen)) {
                return -TARGET_EFAULT;
            }
            if (len != sizeof(val)) {
                return -TARGET_EINVAL;
            }
            lv = len;
            ret = get_errno(getsockopt(sockfd, level, optname, &val, &lv));
            if (ret < 0) {
                return ret;
            }
            if (put_user_u32(lv, optlen)
                || put_user_u32(val, optval_addr)) {
                return -TARGET_EFAULT;
            }
            break;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 2, 0)
        case NETLINK_LIST_MEMBERSHIPS:
        {
            uint32_t *results;
            int i;
            if (get_user_u32(len, optlen)) {
                return -TARGET_EFAULT;
            }
            if (len < 0) {
                return -TARGET_EINVAL;
            }
            results = lock_user(VERIFY_WRITE, optval_addr, len, 1);
            if (!results && len > 0) {
                return -TARGET_EFAULT;
            }
            lv = len;
            ret = get_errno(getsockopt(sockfd, level, optname, results, &lv));
            if (ret < 0) {
                unlock_user(results, optval_addr, 0);
                return ret;
            }
            /* swap host endianess to target endianess. */
            for (i = 0; i < (len / sizeof(uint32_t)); i++) {
                results[i] = tswap32(results[i]);
            }
            if (put_user_u32(lv, optlen)) {
                return -TARGET_EFAULT;
            }
            unlock_user(results, optval_addr, 0);
            break;
        }
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4, 2, 0) */
        default:
            goto unimplemented;
        }
        break;
#endif /* SOL_NETLINK */
    default:
    unimplemented:
        qemu_log_mask(LOG_UNIMP,
                      "getsockopt level=%d optname=%d not yet supported\n",
                      level, optname);
        ret = -TARGET_EOPNOTSUPP;
        break;
    }
    return ret;
}

/* Convert target low/high pair representing file offset into the host
 * low/high pair. This function doesn't handle offsets bigger than 64 bits
 * as the kernel doesn't handle them either.
 */
static void target_to_host_low_high(abi_ulong tlow,
                                    abi_ulong thigh,
                                    unsigned long *hlow,
                                    unsigned long *hhigh)
{
    uint64_t off = tlow |
        ((unsigned long long)thigh << TARGET_LONG_BITS / 2) <<
        TARGET_LONG_BITS / 2;

    *hlow = off;
    *hhigh = (off >> HOST_LONG_BITS / 2) >> HOST_LONG_BITS / 2;
}

static struct iovec *lock_iovec(int type, abi_ulong target_addr,
                                abi_ulong count, int copy)
{
    struct target_iovec *target_vec;
    struct iovec *vec;
    abi_ulong total_len, max_len;
    int i;
    int err = 0;
    bool bad_address = false;

    if (count == 0) {
        errno = 0;
        return NULL;
    }
    if (count > IOV_MAX) {
        errno = EINVAL;
        return NULL;
    }

    vec = g_try_new0(struct iovec, count);
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
            /* If the first buffer pointer is bad, this is a fault.  But
             * subsequent bad buffers will result in a partial write; this
             * is realized by filling the vector with null pointers and
             * zero lengths. */
            if (!vec[i].iov_base) {
                if (i == 0) {
                    err = EFAULT;
                    goto fail;
                } else {
                    bad_address = true;
                }
            }
            if (bad_address) {
                len = 0;
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
    while (--i >= 0) {
        if (tswapal(target_vec[i].iov_len) > 0) {
            unlock_user(vec[i].iov_base, tswapal(target_vec[i].iov_base), 0);
        }
    }
    unlock_user(target_vec, target_addr, 0);
 fail2:
    g_free(vec);
    errno = err;
    return NULL;
}

static void unlock_iovec(struct iovec *vec, abi_ulong target_addr,
                         abi_ulong count, int copy)
{
    struct target_iovec *target_vec;
    int i;

    target_vec = lock_user(VERIFY_READ, target_addr,
                           count * sizeof(struct target_iovec), 1);
    if (target_vec) {
        for (i = 0; i < count; i++) {
            abi_ulong base = tswapal(target_vec[i].iov_base);
            abi_long len = tswapal(target_vec[i].iov_len);
            if (len < 0) {
                break;
            }
            unlock_user(vec[i].iov_base, base, copy ? vec[i].iov_len : 0);
        }
        unlock_user(target_vec, target_addr, 0);
    }

    g_free(vec);
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

    if (domain == PF_NETLINK && !(
#ifdef CONFIG_RTNETLINK
         protocol == NETLINK_ROUTE ||
#endif
         protocol == NETLINK_KOBJECT_UEVENT ||
         protocol == NETLINK_AUDIT)) {
        return -TARGET_EPROTONOSUPPORT;
    }

    if (domain == AF_PACKET ||
        (domain == AF_INET && type == SOCK_PACKET)) {
        protocol = tswap16(protocol);
    }

    ret = get_errno(socket(domain, type, protocol));
    if (ret >= 0) {
        ret = sock_flags_fixup(ret, target_type);
        if (type == SOCK_PACKET) {
            /* Manage an obsolete case :
             * if socket type is SOCK_PACKET, bind by name
             */
            fd_trans_register(ret, &target_packet_trans);
        } else if (domain == PF_NETLINK) {
            switch (protocol) {
#ifdef CONFIG_RTNETLINK
            case NETLINK_ROUTE:
                fd_trans_register(ret, &target_netlink_route_trans);
                break;
#endif
            case NETLINK_KOBJECT_UEVENT:
                /* nothing to do: messages are strings */
                break;
            case NETLINK_AUDIT:
                fd_trans_register(ret, &target_netlink_audit_trans);
                break;
            default:
                g_assert_not_reached();
            }
        }
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

    ret = target_to_host_sockaddr(sockfd, addr, target_addr, addrlen);
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

    ret = target_to_host_sockaddr(sockfd, addr, target_addr, addrlen);
    if (ret)
        return ret;

    return get_errno(safe_connect(sockfd, addr, addrlen));
}

/* do_sendrecvmsg_locked() Must return target values and target errnos. */
static abi_long do_sendrecvmsg_locked(int fd, struct target_msghdr *msgp,
                                      int flags, int send)
{
    abi_long ret, len;
    struct msghdr msg;
    abi_ulong count;
    struct iovec *vec;
    abi_ulong target_vec;

    if (msgp->msg_name) {
        msg.msg_namelen = tswap32(msgp->msg_namelen);
        msg.msg_name = alloca(msg.msg_namelen+1);
        ret = target_to_host_sockaddr(fd, msg.msg_name,
                                      tswapal(msgp->msg_name),
                                      msg.msg_namelen);
        if (ret == -TARGET_EFAULT) {
            /* For connected sockets msg_name and msg_namelen must
             * be ignored, so returning EFAULT immediately is wrong.
             * Instead, pass a bad msg_name to the host kernel, and
             * let it decide whether to return EFAULT or not.
             */
            msg.msg_name = (void *)-1;
        } else if (ret) {
            goto out2;
        }
    } else {
        msg.msg_name = NULL;
        msg.msg_namelen = 0;
    }
    msg.msg_controllen = 2 * tswapal(msgp->msg_controllen);
    msg.msg_control = alloca(msg.msg_controllen);
    memset(msg.msg_control, 0, msg.msg_controllen);

    msg.msg_flags = tswap32(msgp->msg_flags);

    count = tswapal(msgp->msg_iovlen);
    target_vec = tswapal(msgp->msg_iov);

    if (count > IOV_MAX) {
        /* sendrcvmsg returns a different errno for this condition than
         * readv/writev, so we must catch it here before lock_iovec() does.
         */
        ret = -TARGET_EMSGSIZE;
        goto out2;
    }

    vec = lock_iovec(send ? VERIFY_READ : VERIFY_WRITE,
                     target_vec, count, send);
    if (vec == NULL) {
        ret = -host_to_target_errno(errno);
        goto out2;
    }
    msg.msg_iovlen = count;
    msg.msg_iov = vec;

    if (send) {
        if (fd_trans_target_to_host_data(fd)) {
            void *host_msg;

            host_msg = g_malloc(msg.msg_iov->iov_len);
            memcpy(host_msg, msg.msg_iov->iov_base, msg.msg_iov->iov_len);
            ret = fd_trans_target_to_host_data(fd)(host_msg,
                                                   msg.msg_iov->iov_len);
            if (ret >= 0) {
                msg.msg_iov->iov_base = host_msg;
                ret = get_errno(safe_sendmsg(fd, &msg, flags));
            }
            g_free(host_msg);
        } else {
            ret = target_to_host_cmsg(&msg, msgp);
            if (ret == 0) {
                ret = get_errno(safe_sendmsg(fd, &msg, flags));
            }
        }
    } else {
        ret = get_errno(safe_recvmsg(fd, &msg, flags));
        if (!is_error(ret)) {
            len = ret;
            if (fd_trans_host_to_target_data(fd)) {
                ret = fd_trans_host_to_target_data(fd)(msg.msg_iov->iov_base,
                                               MIN(msg.msg_iov->iov_len, len));
            } else {
                ret = host_to_target_cmsg(msgp, &msg);
            }
            if (!is_error(ret)) {
                msgp->msg_namelen = tswap32(msg.msg_namelen);
                msgp->msg_flags = tswap32(msg.msg_flags);
                if (msg.msg_name != NULL && msg.msg_name != (void *)-1) {
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

/* do_accept4() Must return target values and target errnos. */
static abi_long do_accept4(int fd, abi_ulong target_addr,
                           abi_ulong target_addrlen_addr, int flags)
{
    socklen_t addrlen, ret_addrlen;
    void *addr;
    abi_long ret;
    int host_flags;

    host_flags = target_to_host_bitmask(flags, fcntl_flags_tbl);

    if (target_addr == 0) {
        return get_errno(safe_accept4(fd, NULL, NULL, host_flags));
    }

    /* linux returns EFAULT if addrlen pointer is invalid */
    if (get_user_u32(addrlen, target_addrlen_addr))
        return -TARGET_EFAULT;

    if ((int)addrlen < 0) {
        return -TARGET_EINVAL;
    }

    if (!access_ok(thread_cpu, VERIFY_WRITE, target_addr, addrlen)) {
        return -TARGET_EFAULT;
    }

    addr = alloca(addrlen);

    ret_addrlen = addrlen;
    ret = get_errno(safe_accept4(fd, addr, &ret_addrlen, host_flags));
    if (!is_error(ret)) {
        host_to_target_sockaddr(target_addr, addr, MIN(addrlen, ret_addrlen));
        if (put_user_u32(ret_addrlen, target_addrlen_addr)) {
            ret = -TARGET_EFAULT;
        }
    }
    return ret;
}

/* do_getpeername() Must return target values and target errnos. */
static abi_long do_getpeername(int fd, abi_ulong target_addr,
                               abi_ulong target_addrlen_addr)
{
    socklen_t addrlen, ret_addrlen;
    void *addr;
    abi_long ret;

    if (get_user_u32(addrlen, target_addrlen_addr))
        return -TARGET_EFAULT;

    if ((int)addrlen < 0) {
        return -TARGET_EINVAL;
    }

    if (!access_ok(thread_cpu, VERIFY_WRITE, target_addr, addrlen)) {
        return -TARGET_EFAULT;
    }

    addr = alloca(addrlen);

    ret_addrlen = addrlen;
    ret = get_errno(getpeername(fd, addr, &ret_addrlen));
    if (!is_error(ret)) {
        host_to_target_sockaddr(target_addr, addr, MIN(addrlen, ret_addrlen));
        if (put_user_u32(ret_addrlen, target_addrlen_addr)) {
            ret = -TARGET_EFAULT;
        }
    }
    return ret;
}

/* do_getsockname() Must return target values and target errnos. */
static abi_long do_getsockname(int fd, abi_ulong target_addr,
                               abi_ulong target_addrlen_addr)
{
    socklen_t addrlen, ret_addrlen;
    void *addr;
    abi_long ret;

    if (get_user_u32(addrlen, target_addrlen_addr))
        return -TARGET_EFAULT;

    if ((int)addrlen < 0) {
        return -TARGET_EINVAL;
    }

    if (!access_ok(thread_cpu, VERIFY_WRITE, target_addr, addrlen)) {
        return -TARGET_EFAULT;
    }

    addr = alloca(addrlen);

    ret_addrlen = addrlen;
    ret = get_errno(getsockname(fd, addr, &ret_addrlen));
    if (!is_error(ret)) {
        host_to_target_sockaddr(target_addr, addr, MIN(addrlen, ret_addrlen));
        if (put_user_u32(ret_addrlen, target_addrlen_addr)) {
            ret = -TARGET_EFAULT;
        }
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
    void *copy_msg = NULL;
    abi_long ret;

    if ((int)addrlen < 0) {
        return -TARGET_EINVAL;
    }

    host_msg = lock_user(VERIFY_READ, msg, len, 1);
    if (!host_msg)
        return -TARGET_EFAULT;
    if (fd_trans_target_to_host_data(fd)) {
        copy_msg = host_msg;
        host_msg = g_malloc(len);
        memcpy(host_msg, copy_msg, len);
        ret = fd_trans_target_to_host_data(fd)(host_msg, len);
        if (ret < 0) {
            goto fail;
        }
    }
    if (target_addr) {
        addr = alloca(addrlen+1);
        ret = target_to_host_sockaddr(fd, addr, target_addr, addrlen);
        if (ret) {
            goto fail;
        }
        ret = get_errno(safe_sendto(fd, host_msg, len, flags, addr, addrlen));
    } else {
        ret = get_errno(safe_sendto(fd, host_msg, len, flags, NULL, 0));
    }
fail:
    if (copy_msg) {
        g_free(host_msg);
        host_msg = copy_msg;
    }
    unlock_user(host_msg, msg, 0);
    return ret;
}

/* do_recvfrom() Must return target values and target errnos. */
static abi_long do_recvfrom(int fd, abi_ulong msg, size_t len, int flags,
                            abi_ulong target_addr,
                            abi_ulong target_addrlen)
{
    socklen_t addrlen, ret_addrlen;
    void *addr;
    void *host_msg;
    abi_long ret;

    if (!msg) {
        host_msg = NULL;
    } else {
        host_msg = lock_user(VERIFY_WRITE, msg, len, 0);
        if (!host_msg) {
            return -TARGET_EFAULT;
        }
    }
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
        ret_addrlen = addrlen;
        ret = get_errno(safe_recvfrom(fd, host_msg, len, flags,
                                      addr, &ret_addrlen));
    } else {
        addr = NULL; /* To keep compiler quiet.  */
        addrlen = 0; /* To keep compiler quiet.  */
        ret = get_errno(safe_recvfrom(fd, host_msg, len, flags, NULL, 0));
    }
    if (!is_error(ret)) {
        if (fd_trans_host_to_target_data(fd)) {
            abi_long trans;
            trans = fd_trans_host_to_target_data(fd)(host_msg, MIN(ret, len));
            if (is_error(trans)) {
                ret = trans;
                goto fail;
            }
        }
        if (target_addr) {
            host_to_target_sockaddr(target_addr, addr,
                                    MIN(addrlen, ret_addrlen));
            if (put_user_u32(ret_addrlen, target_addrlen)) {
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
/* do_socketcall() must return target values and target errnos. */
static abi_long do_socketcall(int num, abi_ulong vptr)
{
    static const unsigned nargs[] = { /* number of arguments per operation */
        [TARGET_SYS_SOCKET] = 3,      /* domain, type, protocol */
        [TARGET_SYS_BIND] = 3,        /* fd, addr, addrlen */
        [TARGET_SYS_CONNECT] = 3,     /* fd, addr, addrlen */
        [TARGET_SYS_LISTEN] = 2,      /* fd, backlog */
        [TARGET_SYS_ACCEPT] = 3,      /* fd, addr, addrlen */
        [TARGET_SYS_GETSOCKNAME] = 3, /* fd, addr, addrlen */
        [TARGET_SYS_GETPEERNAME] = 3, /* fd, addr, addrlen */
        [TARGET_SYS_SOCKETPAIR] = 4,  /* domain, type, protocol, tab */
        [TARGET_SYS_SEND] = 4,        /* fd, msg, len, flags */
        [TARGET_SYS_RECV] = 4,        /* fd, msg, len, flags */
        [TARGET_SYS_SENDTO] = 6,      /* fd, msg, len, flags, addr, addrlen */
        [TARGET_SYS_RECVFROM] = 6,    /* fd, msg, len, flags, addr, addrlen */
        [TARGET_SYS_SHUTDOWN] = 2,    /* fd, how */
        [TARGET_SYS_SETSOCKOPT] = 5,  /* fd, level, optname, optval, optlen */
        [TARGET_SYS_GETSOCKOPT] = 5,  /* fd, level, optname, optval, optlen */
        [TARGET_SYS_SENDMSG] = 3,     /* fd, msg, flags */
        [TARGET_SYS_RECVMSG] = 3,     /* fd, msg, flags */
        [TARGET_SYS_ACCEPT4] = 4,     /* fd, addr, addrlen, flags */
        [TARGET_SYS_RECVMMSG] = 4,    /* fd, msgvec, vlen, flags */
        [TARGET_SYS_SENDMMSG] = 4,    /* fd, msgvec, vlen, flags */
    };
    abi_long a[6]; /* max 6 args */
    unsigned i;

    /* check the range of the first argument num */
    /* (TARGET_SYS_SENDMMSG is the highest among TARGET_SYS_xxx) */
    if (num < 1 || num > TARGET_SYS_SENDMMSG) {
        return -TARGET_EINVAL;
    }
    /* ensure we have space for args */
    if (nargs[num] > ARRAY_SIZE(a)) {
        return -TARGET_EINVAL;
    }
    /* collect the arguments in a[] according to nargs[] */
    for (i = 0; i < nargs[num]; ++i) {
        if (get_user_ual(a[i], vptr + i * sizeof(abi_long)) != 0) {
            return -TARGET_EFAULT;
        }
    }
    /* now when we have the args, invoke the appropriate underlying function */
    switch (num) {
    case TARGET_SYS_SOCKET: /* domain, type, protocol */
        return do_socket(a[0], a[1], a[2]);
    case TARGET_SYS_BIND: /* sockfd, addr, addrlen */
        return do_bind(a[0], a[1], a[2]);
    case TARGET_SYS_CONNECT: /* sockfd, addr, addrlen */
        return do_connect(a[0], a[1], a[2]);
    case TARGET_SYS_LISTEN: /* sockfd, backlog */
        return get_errno(listen(a[0], a[1]));
    case TARGET_SYS_ACCEPT: /* sockfd, addr, addrlen */
        return do_accept4(a[0], a[1], a[2], 0);
    case TARGET_SYS_GETSOCKNAME: /* sockfd, addr, addrlen */
        return do_getsockname(a[0], a[1], a[2]);
    case TARGET_SYS_GETPEERNAME: /* sockfd, addr, addrlen */
        return do_getpeername(a[0], a[1], a[2]);
    case TARGET_SYS_SOCKETPAIR: /* domain, type, protocol, tab */
        return do_socketpair(a[0], a[1], a[2], a[3]);
    case TARGET_SYS_SEND: /* sockfd, msg, len, flags */
        return do_sendto(a[0], a[1], a[2], a[3], 0, 0);
    case TARGET_SYS_RECV: /* sockfd, msg, len, flags */
        return do_recvfrom(a[0], a[1], a[2], a[3], 0, 0);
    case TARGET_SYS_SENDTO: /* sockfd, msg, len, flags, addr, addrlen */
        return do_sendto(a[0], a[1], a[2], a[3], a[4], a[5]);
    case TARGET_SYS_RECVFROM: /* sockfd, msg, len, flags, addr, addrlen */
        return do_recvfrom(a[0], a[1], a[2], a[3], a[4], a[5]);
    case TARGET_SYS_SHUTDOWN: /* sockfd, how */
        return get_errno(shutdown(a[0], a[1]));
    case TARGET_SYS_SETSOCKOPT: /* sockfd, level, optname, optval, optlen */
        return do_setsockopt(a[0], a[1], a[2], a[3], a[4]);
    case TARGET_SYS_GETSOCKOPT: /* sockfd, level, optname, optval, optlen */
        return do_getsockopt(a[0], a[1], a[2], a[3], a[4]);
    case TARGET_SYS_SENDMSG: /* sockfd, msg, flags */
        return do_sendrecvmsg(a[0], a[1], a[2], 1);
    case TARGET_SYS_RECVMSG: /* sockfd, msg, flags */
        return do_sendrecvmsg(a[0], a[1], a[2], 0);
    case TARGET_SYS_ACCEPT4: /* sockfd, addr, addrlen, flags */
        return do_accept4(a[0], a[1], a[2], a[3]);
    case TARGET_SYS_RECVMMSG: /* sockfd, msgvec, vlen, flags */
        return do_sendrecvmmsg(a[0], a[1], a[2], a[3], 0);
    case TARGET_SYS_SENDMMSG: /* sockfd, msgvec, vlen, flags */
        return do_sendrecvmmsg(a[0], a[1], a[2], a[3], 1);
    default:
        qemu_log_mask(LOG_UNIMP, "Unsupported socketcall: %d\n", num);
        return -TARGET_EINVAL;
    }
}
#endif

#define N_SHM_REGIONS	32

static struct shm_region {
    abi_ulong start;
    abi_ulong size;
    bool in_use;
} shm_regions[N_SHM_REGIONS];

#ifndef TARGET_SEMID64_DS
/* asm-generic version of this struct */
struct target_semid64_ds
{
  struct target_ipc_perm sem_perm;
  abi_ulong sem_otime;
#if TARGET_ABI_BITS == 32
  abi_ulong __unused1;
#endif
  abi_ulong sem_ctime;
#if TARGET_ABI_BITS == 32
  abi_ulong __unused2;
#endif
  abi_ulong sem_nsems;
  abi_ulong __unused3;
  abi_ulong __unused4;
};
#endif

static inline abi_long target_to_host_ipc_perm(struct ipc_perm *host_ip,
                                               abi_ulong target_addr)
{
    struct target_ipc_perm *target_ip;
    struct target_semid64_ds *target_sd;

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
    struct target_semid64_ds *target_sd;

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
    struct target_semid64_ds *target_sd;

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
    struct target_semid64_ds *target_sd;

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

    *host_array = g_try_new(unsigned short, nsems);
    if (!*host_array) {
        return -TARGET_ENOMEM;
    }
    array = lock_user(VERIFY_READ, target_addr,
                      nsems*sizeof(unsigned short), 1);
    if (!array) {
        g_free(*host_array);
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
    g_free(*host_array);
    unlock_user(array, target_addr, 1);

    return 0;
}

static inline abi_long do_semctl(int semid, int semnum, int cmd,
                                 abi_ulong target_arg)
{
    union target_semun target_su = { .buf = target_arg };
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

#if defined(TARGET_NR_ipc) || defined(TARGET_NR_semop) || \
    defined(TARGET_NR_semtimedop) || defined(TARGET_NR_semtimedop_time64)

/*
 * This macro is required to handle the s390 variants, which passes the
 * arguments in a different order than default.
 */
#ifdef __s390x__
#define SEMTIMEDOP_IPC_ARGS(__nsops, __sops, __timeout) \
  (__nsops), (__timeout), (__sops)
#else
#define SEMTIMEDOP_IPC_ARGS(__nsops, __sops, __timeout) \
  (__nsops), 0, (__sops), (__timeout)
#endif

static inline abi_long do_semtimedop(int semid,
                                     abi_long ptr,
                                     unsigned nsops,
                                     abi_long timeout, bool time64)
{
    struct sembuf *sops;
    struct timespec ts, *pts = NULL;
    abi_long ret;

    if (timeout) {
        pts = &ts;
        if (time64) {
            if (target_to_host_timespec64(pts, timeout)) {
                return -TARGET_EFAULT;
            }
        } else {
            if (target_to_host_timespec(pts, timeout)) {
                return -TARGET_EFAULT;
            }
        }
    }

    if (nsops > TARGET_SEMOPM) {
        return -TARGET_E2BIG;
    }

    sops = g_new(struct sembuf, nsops);

    if (target_to_host_sembuf(sops, ptr, nsops)) {
        g_free(sops);
        return -TARGET_EFAULT;
    }

    ret = -TARGET_ENOSYS;
#ifdef __NR_semtimedop
    ret = get_errno(safe_semtimedop(semid, sops, nsops, pts));
#endif
#ifdef __NR_ipc
    if (ret == -TARGET_ENOSYS) {
        ret = get_errno(safe_ipc(IPCOP_semtimedop, semid,
                                 SEMTIMEDOP_IPC_ARGS(nsops, sops, (long)pts)));
    }
#endif
    g_free(sops);
    return ret;
}
#endif

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
                                 ssize_t msgsz, int msgflg)
{
    struct target_msgbuf *target_mb;
    struct msgbuf *host_mb;
    abi_long ret = 0;

    if (msgsz < 0) {
        return -TARGET_EINVAL;
    }

    if (!lock_user_struct(VERIFY_READ, target_mb, msgp, 0))
        return -TARGET_EFAULT;
    host_mb = g_try_malloc(msgsz + sizeof(long));
    if (!host_mb) {
        unlock_user_struct(target_mb, msgp, 0);
        return -TARGET_ENOMEM;
    }
    host_mb->mtype = (abi_long) tswapal(target_mb->mtype);
    memcpy(host_mb->mtext, target_mb->mtext, msgsz);
    ret = -TARGET_ENOSYS;
#ifdef __NR_msgsnd
    ret = get_errno(safe_msgsnd(msqid, host_mb, msgsz, msgflg));
#endif
#ifdef __NR_ipc
    if (ret == -TARGET_ENOSYS) {
#ifdef __s390x__
        ret = get_errno(safe_ipc(IPCOP_msgsnd, msqid, msgsz, msgflg,
                                 host_mb));
#else
        ret = get_errno(safe_ipc(IPCOP_msgsnd, msqid, msgsz, msgflg,
                                 host_mb, 0));
#endif
    }
#endif
    g_free(host_mb);
    unlock_user_struct(target_mb, msgp, 0);

    return ret;
}

#ifdef __NR_ipc
#if defined(__sparc__)
/* SPARC for msgrcv it does not use the kludge on final 2 arguments.  */
#define MSGRCV_ARGS(__msgp, __msgtyp) __msgp, __msgtyp
#elif defined(__s390x__)
/* The s390 sys_ipc variant has only five parameters.  */
#define MSGRCV_ARGS(__msgp, __msgtyp) \
    ((long int[]){(long int)__msgp, __msgtyp})
#else
#define MSGRCV_ARGS(__msgp, __msgtyp) \
    ((long int[]){(long int)__msgp, __msgtyp}), 0
#endif
#endif

static inline abi_long do_msgrcv(int msqid, abi_long msgp,
                                 ssize_t msgsz, abi_long msgtyp,
                                 int msgflg)
{
    struct target_msgbuf *target_mb;
    char *target_mtext;
    struct msgbuf *host_mb;
    abi_long ret = 0;

    if (msgsz < 0) {
        return -TARGET_EINVAL;
    }

    if (!lock_user_struct(VERIFY_WRITE, target_mb, msgp, 0))
        return -TARGET_EFAULT;

    host_mb = g_try_malloc(msgsz + sizeof(long));
    if (!host_mb) {
        ret = -TARGET_ENOMEM;
        goto end;
    }
    ret = -TARGET_ENOSYS;
#ifdef __NR_msgrcv
    ret = get_errno(safe_msgrcv(msqid, host_mb, msgsz, msgtyp, msgflg));
#endif
#ifdef __NR_ipc
    if (ret == -TARGET_ENOSYS) {
        ret = get_errno(safe_ipc(IPCOP_CALL(1, IPCOP_msgrcv), msqid, msgsz,
                        msgflg, MSGRCV_ARGS(host_mb, msgtyp)));
    }
#endif

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

#ifndef TARGET_FORCE_SHMLBA
/* For most architectures, SHMLBA is the same as the page size;
 * some architectures have larger values, in which case they should
 * define TARGET_FORCE_SHMLBA and provide a target_shmlba() function.
 * This corresponds to the kernel arch code defining __ARCH_FORCE_SHMLBA
 * and defining its own value for SHMLBA.
 *
 * The kernel also permits SHMLBA to be set by the architecture to a
 * value larger than the page size without setting __ARCH_FORCE_SHMLBA;
 * this means that addresses are rounded to the large size if
 * SHM_RND is set but addresses not aligned to that size are not rejected
 * as long as they are at least page-aligned. Since the only architecture
 * which uses this is ia64 this code doesn't provide for that oddity.
 */
static inline abi_ulong target_shmlba(CPUArchState *cpu_env)
{
    return TARGET_PAGE_SIZE;
}
#endif

static inline abi_ulong do_shmat(CPUArchState *cpu_env,
                                 int shmid, abi_ulong shmaddr, int shmflg)
{
    CPUState *cpu = env_cpu(cpu_env);
    abi_long raddr;
    void *host_raddr;
    struct shmid_ds shm_info;
    int i,ret;
    abi_ulong shmlba;

    /* shmat pointers are always untagged */

    /* find out the length of the shared memory segment */
    ret = get_errno(shmctl(shmid, IPC_STAT, &shm_info));
    if (is_error(ret)) {
        /* can't get length, bail out */
        return ret;
    }

    shmlba = target_shmlba(cpu_env);

    if (shmaddr & (shmlba - 1)) {
        if (shmflg & SHM_RND) {
            shmaddr &= ~(shmlba - 1);
        } else {
            return -TARGET_EINVAL;
        }
    }
    if (!guest_range_valid_untagged(shmaddr, shm_info.shm_segsz)) {
        return -TARGET_EINVAL;
    }

    mmap_lock();

    /*
     * We're mapping shared memory, so ensure we generate code for parallel
     * execution and flush old translations.  This will work up to the level
     * supported by the host -- anything that requires EXCP_ATOMIC will not
     * be atomic with respect to an external process.
     */
    if (!(cpu->tcg_cflags & CF_PARALLEL)) {
        cpu->tcg_cflags |= CF_PARALLEL;
        tb_flush(cpu);
    }

    if (shmaddr)
        host_raddr = shmat(shmid, (void *)g2h_untagged(shmaddr), shmflg);
    else {
        abi_ulong mmap_start;

        /* In order to use the host shmat, we need to honor host SHMLBA.  */
        mmap_start = mmap_find_vma(0, shm_info.shm_segsz, MAX(SHMLBA, shmlba));

        if (mmap_start == -1) {
            errno = ENOMEM;
            host_raddr = (void *)-1;
        } else
            host_raddr = shmat(shmid, g2h_untagged(mmap_start),
                               shmflg | SHM_REMAP);
    }

    if (host_raddr == (void *)-1) {
        mmap_unlock();
        return get_errno((long)host_raddr);
    }
    raddr=h2g((unsigned long)host_raddr);

    page_set_flags(raddr, raddr + shm_info.shm_segsz,
                   PAGE_VALID | PAGE_RESET | PAGE_READ |
                   (shmflg & SHM_RDONLY ? 0 : PAGE_WRITE));

    for (i = 0; i < N_SHM_REGIONS; i++) {
        if (!shm_regions[i].in_use) {
            shm_regions[i].in_use = true;
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
    abi_long rv;

    /* shmdt pointers are always untagged */

    mmap_lock();

    for (i = 0; i < N_SHM_REGIONS; ++i) {
        if (shm_regions[i].in_use && shm_regions[i].start == shmaddr) {
            shm_regions[i].in_use = false;
            page_set_flags(shmaddr, shmaddr + shm_regions[i].size, 0);
            break;
        }
    }
    rv = get_errno(shmdt(g2h_untagged(shmaddr)));

    mmap_unlock();

    return rv;
}

#ifdef TARGET_NR_ipc
/* ??? This only works with linear mappings.  */
/* do_ipc() must return target values and target errnos. */
static abi_long do_ipc(CPUArchState *cpu_env,
                       unsigned int call, abi_long first,
                       abi_long second, abi_long third,
                       abi_long ptr, abi_long fifth)
{
    int version;
    abi_long ret = 0;

    version = call >> 16;
    call &= 0xffff;

    switch (call) {
    case IPCOP_semop:
        ret = do_semtimedop(first, ptr, second, 0, false);
        break;
    case IPCOP_semtimedop:
    /*
     * The s390 sys_ipc variant has only five parameters instead of six
     * (as for default variant) and the only difference is the handling of
     * SEMTIMEDOP where on s390 the third parameter is used as a pointer
     * to a struct timespec where the generic variant uses fifth parameter.
     */
#if defined(TARGET_S390X)
        ret = do_semtimedop(first, ptr, second, third, TARGET_ABI_BITS == 64);
#else
        ret = do_semtimedop(first, ptr, second, fifth, TARGET_ABI_BITS == 64);
#endif
        break;

    case IPCOP_semget:
        ret = get_errno(semget(first, second, third));
        break;

    case IPCOP_semctl: {
        /* The semun argument to semctl is passed by value, so dereference the
         * ptr argument. */
        abi_ulong atptr;
        get_user_ual(atptr, ptr);
        ret = do_semctl(first, second, third, atptr);
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
            raddr = do_shmat(cpu_env, first, ptr, second);
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
        qemu_log_mask(LOG_UNIMP, "Unsupported ipc call: %d (version %d)\n",
                      call, version);
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
STRUCT_MAX
};
#undef STRUCT
#undef STRUCT_SPECIAL

#define STRUCT(name, ...) static const argtype struct_ ## name ## _def[] = {  __VA_ARGS__, TYPE_NULL };
#define STRUCT_SPECIAL(name)
#include "syscall_types.h"
#undef STRUCT
#undef STRUCT_SPECIAL

#define MAX_STRUCT_SIZE 4096

#ifdef CONFIG_FIEMAP
/* So fiemap access checks don't overflow on 32 bit systems.
 * This is very slightly smaller than the limit imposed by
 * the underlying kernel.
 */
#define FIEMAP_MAX_EXTENTS ((UINT_MAX - sizeof(struct fiemap))  \
                            / sizeof(struct fiemap_extent))

static abi_long do_ioctl_fs_ioc_fiemap(const IOCTLEntry *ie, uint8_t *buf_temp,
                                       int fd, int cmd, abi_long arg)
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
        fm = g_try_malloc(outbufsz);
        if (!fm) {
            return -TARGET_ENOMEM;
        }
        memcpy(fm, buf_temp, sizeof(struct fiemap));
        free_fm = 1;
    }
    ret = get_errno(safe_ioctl(fd, ie->host_cmd, fm));
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
        g_free(fm);
    }
    return ret;
}
#endif

static abi_long do_ioctl_ifconf(const IOCTLEntry *ie, uint8_t *buf_temp,
                                int fd, int cmd, abi_long arg)
{
    const argtype *arg_type = ie->arg_type;
    int target_size;
    void *argptr;
    int ret;
    struct ifconf *host_ifconf;
    uint32_t outbufsz;
    const argtype ifreq_arg_type[] = { MK_STRUCT(STRUCT_sockaddr_ifreq) };
    const argtype ifreq_max_type[] = { MK_STRUCT(STRUCT_ifmap_ifreq) };
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
    target_ifc_buf = (abi_long)(unsigned long)host_ifconf->ifc_buf;
    target_ifreq_size = thunk_type_size(ifreq_max_type, 0);

    if (target_ifc_buf != 0) {
        target_ifc_len = host_ifconf->ifc_len;
        nb_ifreq = target_ifc_len / target_ifreq_size;
        host_ifc_len = nb_ifreq * sizeof(struct ifreq);

        outbufsz = sizeof(*host_ifconf) + host_ifc_len;
        if (outbufsz > MAX_STRUCT_SIZE) {
            /*
             * We can't fit all the extents into the fixed size buffer.
             * Allocate one that is large enough and use it instead.
             */
            host_ifconf = g_try_malloc(outbufsz);
            if (!host_ifconf) {
                return -TARGET_ENOMEM;
            }
            memcpy(host_ifconf, buf_temp, sizeof(*host_ifconf));
            free_buf = 1;
        }
        host_ifc_buf = (char *)host_ifconf + sizeof(*host_ifconf);

        host_ifconf->ifc_len = host_ifc_len;
    } else {
      host_ifc_buf = NULL;
    }
    host_ifconf->ifc_buf = host_ifc_buf;

    ret = get_errno(safe_ioctl(fd, ie->host_cmd, host_ifconf));
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

        if (target_ifc_buf != 0) {
            /* copy ifreq[] to target user */
            argptr = lock_user(VERIFY_WRITE, target_ifc_buf, target_ifc_len, 0);
            for (i = 0; i < nb_ifreq ; i++) {
                thunk_convert(argptr + i * target_ifreq_size,
                              host_ifc_buf + i * sizeof(struct ifreq),
                              ifreq_arg_type, THUNK_TARGET);
            }
            unlock_user(argptr, target_ifc_buf, target_ifc_len);
        }
    }

    if (free_buf) {
        g_free(host_ifconf);
    }

    return ret;
}

#if defined(CONFIG_USBFS)
#if HOST_LONG_BITS > 64
#error USBDEVFS thunks do not support >64 bit hosts yet.
#endif
struct live_urb {
    uint64_t target_urb_adr;
    uint64_t target_buf_adr;
    char *target_buf_ptr;
    struct usbdevfs_urb host_urb;
};

static GHashTable *usbdevfs_urb_hashtable(void)
{
    static GHashTable *urb_hashtable;

    if (!urb_hashtable) {
        urb_hashtable = g_hash_table_new(g_int64_hash, g_int64_equal);
    }
    return urb_hashtable;
}

static void urb_hashtable_insert(struct live_urb *urb)
{
    GHashTable *urb_hashtable = usbdevfs_urb_hashtable();
    g_hash_table_insert(urb_hashtable, urb, urb);
}

static struct live_urb *urb_hashtable_lookup(uint64_t target_urb_adr)
{
    GHashTable *urb_hashtable = usbdevfs_urb_hashtable();
    return g_hash_table_lookup(urb_hashtable, &target_urb_adr);
}

static void urb_hashtable_remove(struct live_urb *urb)
{
    GHashTable *urb_hashtable = usbdevfs_urb_hashtable();
    g_hash_table_remove(urb_hashtable, urb);
}

static abi_long
do_ioctl_usbdevfs_reapurb(const IOCTLEntry *ie, uint8_t *buf_temp,
                          int fd, int cmd, abi_long arg)
{
    const argtype usbfsurb_arg_type[] = { MK_STRUCT(STRUCT_usbdevfs_urb) };
    const argtype ptrvoid_arg_type[] = { TYPE_PTRVOID, 0, 0 };
    struct live_urb *lurb;
    void *argptr;
    uint64_t hurb;
    int target_size;
    uintptr_t target_urb_adr;
    abi_long ret;

    target_size = thunk_type_size(usbfsurb_arg_type, THUNK_TARGET);

    memset(buf_temp, 0, sizeof(uint64_t));
    ret = get_errno(safe_ioctl(fd, ie->host_cmd, buf_temp));
    if (is_error(ret)) {
        return ret;
    }

    memcpy(&hurb, buf_temp, sizeof(uint64_t));
    lurb = (void *)((uintptr_t)hurb - offsetof(struct live_urb, host_urb));
    if (!lurb->target_urb_adr) {
        return -TARGET_EFAULT;
    }
    urb_hashtable_remove(lurb);
    unlock_user(lurb->target_buf_ptr, lurb->target_buf_adr,
        lurb->host_urb.buffer_length);
    lurb->target_buf_ptr = NULL;

    /* restore the guest buffer pointer */
    lurb->host_urb.buffer = (void *)(uintptr_t)lurb->target_buf_adr;

    /* update the guest urb struct */
    argptr = lock_user(VERIFY_WRITE, lurb->target_urb_adr, target_size, 0);
    if (!argptr) {
        g_free(lurb);
        return -TARGET_EFAULT;
    }
    thunk_convert(argptr, &lurb->host_urb, usbfsurb_arg_type, THUNK_TARGET);
    unlock_user(argptr, lurb->target_urb_adr, target_size);

    target_size = thunk_type_size(ptrvoid_arg_type, THUNK_TARGET);
    /* write back the urb handle */
    argptr = lock_user(VERIFY_WRITE, arg, target_size, 0);
    if (!argptr) {
        g_free(lurb);
        return -TARGET_EFAULT;
    }

    /* GHashTable uses 64-bit keys but thunk_convert expects uintptr_t */
    target_urb_adr = lurb->target_urb_adr;
    thunk_convert(argptr, &target_urb_adr, ptrvoid_arg_type, THUNK_TARGET);
    unlock_user(argptr, arg, target_size);

    g_free(lurb);
    return ret;
}

static abi_long
do_ioctl_usbdevfs_discardurb(const IOCTLEntry *ie,
                             uint8_t *buf_temp __attribute__((unused)),
                             int fd, int cmd, abi_long arg)
{
    struct live_urb *lurb;

    /* map target address back to host URB with metadata. */
    lurb = urb_hashtable_lookup(arg);
    if (!lurb) {
        return -TARGET_EFAULT;
    }
    return get_errno(safe_ioctl(fd, ie->host_cmd, &lurb->host_urb));
}

static abi_long
do_ioctl_usbdevfs_submiturb(const IOCTLEntry *ie, uint8_t *buf_temp,
                            int fd, int cmd, abi_long arg)
{
    const argtype *arg_type = ie->arg_type;
    int target_size;
    abi_long ret;
    void *argptr;
    int rw_dir;
    struct live_urb *lurb;

    /*
     * each submitted URB needs to map to a unique ID for the
     * kernel, and that unique ID needs to be a pointer to
     * host memory.  hence, we need to malloc for each URB.
     * isochronous transfers have a variable length struct.
     */
    arg_type++;
    target_size = thunk_type_size(arg_type, THUNK_TARGET);

    /* construct host copy of urb and metadata */
    lurb = g_try_new0(struct live_urb, 1);
    if (!lurb) {
        return -TARGET_ENOMEM;
    }

    argptr = lock_user(VERIFY_READ, arg, target_size, 1);
    if (!argptr) {
        g_free(lurb);
        return -TARGET_EFAULT;
    }
    thunk_convert(&lurb->host_urb, argptr, arg_type, THUNK_HOST);
    unlock_user(argptr, arg, 0);

    lurb->target_urb_adr = arg;
    lurb->target_buf_adr = (uintptr_t)lurb->host_urb.buffer;

    /* buffer space used depends on endpoint type so lock the entire buffer */
    /* control type urbs should check the buffer contents for true direction */
    rw_dir = lurb->host_urb.endpoint & USB_DIR_IN ? VERIFY_WRITE : VERIFY_READ;
    lurb->target_buf_ptr = lock_user(rw_dir, lurb->target_buf_adr,
        lurb->host_urb.buffer_length, 1);
    if (lurb->target_buf_ptr == NULL) {
        g_free(lurb);
        return -TARGET_EFAULT;
    }

    /* update buffer pointer in host copy */
    lurb->host_urb.buffer = lurb->target_buf_ptr;

    ret = get_errno(safe_ioctl(fd, ie->host_cmd, &lurb->host_urb));
    if (is_error(ret)) {
        unlock_user(lurb->target_buf_ptr, lurb->target_buf_adr, 0);
        g_free(lurb);
    } else {
        urb_hashtable_insert(lurb);
    }

    return ret;
}
#endif /* CONFIG_USBFS */

static abi_long do_ioctl_dm(const IOCTLEntry *ie, uint8_t *buf_temp, int fd,
                            int cmd, abi_long arg)
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
        ret = -TARGET_EINVAL;
        goto out;
    }
    guest_data_size = host_dm->data_size - host_dm->data_start;
    host_data = (char*)host_dm + host_dm->data_start;

    argptr = lock_user(VERIFY_READ, guest_data, guest_data_size, 1);
    if (!argptr) {
        ret = -TARGET_EFAULT;
        goto out;
    }

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
        unlock_user(argptr, guest_data, 0);
        goto out;
    }
    unlock_user(argptr, guest_data, 0);

    ret = get_errno(safe_ioctl(fd, ie->host_cmd, buf_temp));
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
            unlock_user(argptr, guest_data, 0);
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

static abi_long do_ioctl_blkpg(const IOCTLEntry *ie, uint8_t *buf_temp, int fd,
                               int cmd, abi_long arg)
{
    void *argptr;
    int target_size;
    const argtype *arg_type = ie->arg_type;
    const argtype part_arg_type[] = { MK_STRUCT(STRUCT_blkpg_partition) };
    abi_long ret;

    struct blkpg_ioctl_arg *host_blkpg = (void*)buf_temp;
    struct blkpg_partition host_part;

    /* Read and convert blkpg */
    arg_type++;
    target_size = thunk_type_size(arg_type, 0);
    argptr = lock_user(VERIFY_READ, arg, target_size, 1);
    if (!argptr) {
        ret = -TARGET_EFAULT;
        goto out;
    }
    thunk_convert(buf_temp, argptr, arg_type, THUNK_HOST);
    unlock_user(argptr, arg, 0);

    switch (host_blkpg->op) {
    case BLKPG_ADD_PARTITION:
    case BLKPG_DEL_PARTITION:
        /* payload is struct blkpg_partition */
        break;
    default:
        /* Unknown opcode */
        ret = -TARGET_EINVAL;
        goto out;
    }

    /* Read and convert blkpg->data */
    arg = (abi_long)(uintptr_t)host_blkpg->data;
    target_size = thunk_type_size(part_arg_type, 0);
    argptr = lock_user(VERIFY_READ, arg, target_size, 1);
    if (!argptr) {
        ret = -TARGET_EFAULT;
        goto out;
    }
    thunk_convert(&host_part, argptr, part_arg_type, THUNK_HOST);
    unlock_user(argptr, arg, 0);

    /* Swizzle the data pointer to our local copy and call! */
    host_blkpg->data = &host_part;
    ret = get_errno(safe_ioctl(fd, ie->host_cmd, host_blkpg));

out:
    return ret;
}

static abi_long do_ioctl_rt(const IOCTLEntry *ie, uint8_t *buf_temp,
                                int fd, int cmd, abi_long arg)
{
    const argtype *arg_type = ie->arg_type;
    const StructEntry *se;
    const argtype *field_types;
    const int *dst_offsets, *src_offsets;
    int target_size;
    void *argptr;
    abi_ulong *target_rt_dev_ptr = NULL;
    unsigned long *host_rt_dev_ptr = NULL;
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

    ret = get_errno(safe_ioctl(fd, ie->host_cmd, buf_temp));

    assert(host_rt_dev_ptr != NULL);
    assert(target_rt_dev_ptr != NULL);
    if (*host_rt_dev_ptr != 0) {
        unlock_user((void *)*host_rt_dev_ptr,
                    *target_rt_dev_ptr, 0);
    }
    return ret;
}

static abi_long do_ioctl_kdsigaccept(const IOCTLEntry *ie, uint8_t *buf_temp,
                                     int fd, int cmd, abi_long arg)
{
    int sig = target_to_host_signal(arg);
    return get_errno(safe_ioctl(fd, ie->host_cmd, sig));
}

static abi_long do_ioctl_SIOCGSTAMP(const IOCTLEntry *ie, uint8_t *buf_temp,
                                    int fd, int cmd, abi_long arg)
{
    struct timeval tv;
    abi_long ret;

    ret = get_errno(safe_ioctl(fd, SIOCGSTAMP, &tv));
    if (is_error(ret)) {
        return ret;
    }

    if (cmd == (int)TARGET_SIOCGSTAMP_OLD) {
        if (copy_to_user_timeval(arg, &tv)) {
            return -TARGET_EFAULT;
        }
    } else {
        if (copy_to_user_timeval64(arg, &tv)) {
            return -TARGET_EFAULT;
        }
    }

    return ret;
}

static abi_long do_ioctl_SIOCGSTAMPNS(const IOCTLEntry *ie, uint8_t *buf_temp,
                                      int fd, int cmd, abi_long arg)
{
    struct timespec ts;
    abi_long ret;

    ret = get_errno(safe_ioctl(fd, SIOCGSTAMPNS, &ts));
    if (is_error(ret)) {
        return ret;
    }

    if (cmd == (int)TARGET_SIOCGSTAMPNS_OLD) {
        if (host_to_target_timespec(arg, &ts)) {
            return -TARGET_EFAULT;
        }
    } else{
        if (host_to_target_timespec64(arg, &ts)) {
            return -TARGET_EFAULT;
        }
    }

    return ret;
}

#ifdef TIOCGPTPEER
static abi_long do_ioctl_tiocgptpeer(const IOCTLEntry *ie, uint8_t *buf_temp,
                                     int fd, int cmd, abi_long arg)
{
    int flags = target_to_host_bitmask(arg, fcntl_flags_tbl);
    return get_errno(safe_ioctl(fd, ie->host_cmd, flags));
}
#endif

#ifdef HAVE_DRM_H

static void unlock_drm_version(struct drm_version *host_ver,
                               struct target_drm_version *target_ver,
                               bool copy)
{
    unlock_user(host_ver->name, target_ver->name,
                                copy ? host_ver->name_len : 0);
    unlock_user(host_ver->date, target_ver->date,
                                copy ? host_ver->date_len : 0);
    unlock_user(host_ver->desc, target_ver->desc,
                                copy ? host_ver->desc_len : 0);
}

static inline abi_long target_to_host_drmversion(struct drm_version *host_ver,
                                          struct target_drm_version *target_ver)
{
    memset(host_ver, 0, sizeof(*host_ver));

    __get_user(host_ver->name_len, &target_ver->name_len);
    if (host_ver->name_len) {
        host_ver->name = lock_user(VERIFY_WRITE, target_ver->name,
                                   target_ver->name_len, 0);
        if (!host_ver->name) {
            return -EFAULT;
        }
    }

    __get_user(host_ver->date_len, &target_ver->date_len);
    if (host_ver->date_len) {
        host_ver->date = lock_user(VERIFY_WRITE, target_ver->date,
                                   target_ver->date_len, 0);
        if (!host_ver->date) {
            goto err;
        }
    }

    __get_user(host_ver->desc_len, &target_ver->desc_len);
    if (host_ver->desc_len) {
        host_ver->desc = lock_user(VERIFY_WRITE, target_ver->desc,
                                   target_ver->desc_len, 0);
        if (!host_ver->desc) {
            goto err;
        }
    }

    return 0;
err:
    unlock_drm_version(host_ver, target_ver, false);
    return -EFAULT;
}

static inline void host_to_target_drmversion(
                                          struct target_drm_version *target_ver,
                                          struct drm_version *host_ver)
{
    __put_user(host_ver->version_major, &target_ver->version_major);
    __put_user(host_ver->version_minor, &target_ver->version_minor);
    __put_user(host_ver->version_patchlevel, &target_ver->version_patchlevel);
    __put_user(host_ver->name_len, &target_ver->name_len);
    __put_user(host_ver->date_len, &target_ver->date_len);
    __put_user(host_ver->desc_len, &target_ver->desc_len);
    unlock_drm_version(host_ver, target_ver, true);
}

static abi_long do_ioctl_drm(const IOCTLEntry *ie, uint8_t *buf_temp,
                             int fd, int cmd, abi_long arg)
{
    struct drm_version *ver;
    struct target_drm_version *target_ver;
    abi_long ret;

    switch (ie->host_cmd) {
    case DRM_IOCTL_VERSION:
        if (!lock_user_struct(VERIFY_WRITE, target_ver, arg, 0)) {
            return -TARGET_EFAULT;
        }
        ver = (struct drm_version *)buf_temp;
        ret = target_to_host_drmversion(ver, target_ver);
        if (!is_error(ret)) {
            ret = get_errno(safe_ioctl(fd, ie->host_cmd, ver));
            if (is_error(ret)) {
                unlock_drm_version(ver, target_ver, false);
            } else {
                host_to_target_drmversion(target_ver, ver);
            }
        }
        unlock_user_struct(target_ver, arg, 0);
        return ret;
    }
    return -TARGET_ENOSYS;
}

static abi_long do_ioctl_drm_i915_getparam(const IOCTLEntry *ie,
                                           struct drm_i915_getparam *gparam,
                                           int fd, abi_long arg)
{
    abi_long ret;
    int value;
    struct target_drm_i915_getparam *target_gparam;

    if (!lock_user_struct(VERIFY_READ, target_gparam, arg, 0)) {
        return -TARGET_EFAULT;
    }

    __get_user(gparam->param, &target_gparam->param);
    gparam->value = &value;
    ret = get_errno(safe_ioctl(fd, ie->host_cmd, gparam));
    put_user_s32(value, target_gparam->value);

    unlock_user_struct(target_gparam, arg, 0);
    return ret;
}

static abi_long do_ioctl_drm_i915(const IOCTLEntry *ie, uint8_t *buf_temp,
                                  int fd, int cmd, abi_long arg)
{
    switch (ie->host_cmd) {
    case DRM_IOCTL_I915_GETPARAM:
        return do_ioctl_drm_i915_getparam(ie,
                                          (struct drm_i915_getparam *)buf_temp,
                                          fd, arg);
    default:
        return -TARGET_ENOSYS;
    }
}

#endif

static abi_long do_ioctl_TUNSETTXFILTER(const IOCTLEntry *ie, uint8_t *buf_temp,
                                        int fd, int cmd, abi_long arg)
{
    struct tun_filter *filter = (struct tun_filter *)buf_temp;
    struct tun_filter *target_filter;
    char *target_addr;

    assert(ie->access == IOC_W);

    target_filter = lock_user(VERIFY_READ, arg, sizeof(*target_filter), 1);
    if (!target_filter) {
        return -TARGET_EFAULT;
    }
    filter->flags = tswap16(target_filter->flags);
    filter->count = tswap16(target_filter->count);
    unlock_user(target_filter, arg, 0);

    if (filter->count) {
        if (offsetof(struct tun_filter, addr) + filter->count * ETH_ALEN >
            MAX_STRUCT_SIZE) {
            return -TARGET_EFAULT;
        }

        target_addr = lock_user(VERIFY_READ,
                                arg + offsetof(struct tun_filter, addr),
                                filter->count * ETH_ALEN, 1);
        if (!target_addr) {
            return -TARGET_EFAULT;
        }
        memcpy(filter->addr, target_addr, filter->count * ETH_ALEN);
        unlock_user(target_addr, arg + offsetof(struct tun_filter, addr), 0);
    }

    return get_errno(safe_ioctl(fd, ie->host_cmd, filter));
}

IOCTLEntry ioctl_entries[] = {
#define IOCTL(cmd, access, ...) \
    { TARGET_ ## cmd, cmd, #cmd, access, 0, {  __VA_ARGS__ } },
#define IOCTL_SPECIAL(cmd, access, dofn, ...)                      \
    { TARGET_ ## cmd, cmd, #cmd, access, dofn, {  __VA_ARGS__ } },
#define IOCTL_IGNORE(cmd) \
    { TARGET_ ## cmd, 0, #cmd },
#include "ioctls.h"
    { 0, 0, },
};

/* ??? Implement proper locking for ioctls.  */
/* do_ioctl() Must return target values and target errnos. */
static abi_long do_ioctl(int fd, int cmd, abi_long arg)
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
            qemu_log_mask(
                LOG_UNIMP, "Unsupported ioctl: cmd=0x%04lx\n", (long)cmd);
            return -TARGET_ENOSYS;
        }
        if (ie->target_cmd == cmd)
            break;
        ie++;
    }
    arg_type = ie->arg_type;
    if (ie->do_ioctl) {
        return ie->do_ioctl(ie, buf_temp, fd, cmd, arg);
    } else if (!ie->host_cmd) {
        /* Some architectures define BSD ioctls in their headers
           that are not implemented in Linux.  */
        return -TARGET_ENOSYS;
    }

    switch(arg_type[0]) {
    case TYPE_NULL:
        /* no argument */
        ret = get_errno(safe_ioctl(fd, ie->host_cmd));
        break;
    case TYPE_PTRVOID:
    case TYPE_INT:
    case TYPE_LONG:
    case TYPE_ULONG:
        ret = get_errno(safe_ioctl(fd, ie->host_cmd, arg));
        break;
    case TYPE_PTR:
        arg_type++;
        target_size = thunk_type_size(arg_type, 0);
        switch(ie->access) {
        case IOC_R:
            ret = get_errno(safe_ioctl(fd, ie->host_cmd, buf_temp));
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
            ret = get_errno(safe_ioctl(fd, ie->host_cmd, buf_temp));
            break;
        default:
        case IOC_RW:
            argptr = lock_user(VERIFY_READ, arg, target_size, 1);
            if (!argptr)
                return -TARGET_EFAULT;
            thunk_convert(buf_temp, argptr, arg_type, THUNK_HOST);
            unlock_user(argptr, arg, 0);
            ret = get_errno(safe_ioctl(fd, ie->host_cmd, buf_temp));
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
        qemu_log_mask(LOG_UNIMP,
                      "Unsupported ioctl type: cmd=0x%04lx type=%d\n",
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
        { TARGET_IUTF8, TARGET_IUTF8, IUTF8, IUTF8},
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
  { TARGET_EXTPROC, TARGET_EXTPROC, EXTPROC, EXTPROC},
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
    .print = print_termios,
};

static const bitmask_transtbl mmap_flags_tbl[] = {
    { TARGET_MAP_SHARED, TARGET_MAP_SHARED, MAP_SHARED, MAP_SHARED },
    { TARGET_MAP_PRIVATE, TARGET_MAP_PRIVATE, MAP_PRIVATE, MAP_PRIVATE },
    { TARGET_MAP_FIXED, TARGET_MAP_FIXED, MAP_FIXED, MAP_FIXED },
    { TARGET_MAP_ANONYMOUS, TARGET_MAP_ANONYMOUS,
      MAP_ANONYMOUS, MAP_ANONYMOUS },
    { TARGET_MAP_GROWSDOWN, TARGET_MAP_GROWSDOWN,
      MAP_GROWSDOWN, MAP_GROWSDOWN },
    { TARGET_MAP_DENYWRITE, TARGET_MAP_DENYWRITE,
      MAP_DENYWRITE, MAP_DENYWRITE },
    { TARGET_MAP_EXECUTABLE, TARGET_MAP_EXECUTABLE,
      MAP_EXECUTABLE, MAP_EXECUTABLE },
    { TARGET_MAP_LOCKED, TARGET_MAP_LOCKED, MAP_LOCKED, MAP_LOCKED },
    { TARGET_MAP_NORESERVE, TARGET_MAP_NORESERVE,
      MAP_NORESERVE, MAP_NORESERVE },
    { TARGET_MAP_HUGETLB, TARGET_MAP_HUGETLB, MAP_HUGETLB, MAP_HUGETLB },
    /* MAP_STACK had been ignored by the kernel for quite some time.
       Recognize it for the target insofar as we do not want to pass
       it through to the host.  */
    { TARGET_MAP_STACK, TARGET_MAP_STACK, 0, 0 },
    { 0, 0, 0, 0 }
};

/*
 * NOTE: TARGET_ABI32 is defined for TARGET_I386 (but not for TARGET_X86_64)
 *       TARGET_I386 is defined if TARGET_X86_64 is defined
 */
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
        memset(g2h_untagged(env->ldt.base), 0,
               TARGET_LDT_ENTRIES * TARGET_LDT_ENTRY_SIZE);
        env->ldt.limit = 0xffff;
        ldt_table = g2h_untagged(env->ldt.base);
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

#if defined(TARGET_ABI32)
abi_long do_set_thread_area(CPUX86State *env, abi_ulong ptr)
{
    uint64_t *gdt_table = g2h_untagged(env->gdt.base);
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
    uint64_t *gdt_table = g2h_untagged(env->gdt.base);
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

abi_long do_arch_prctl(CPUX86State *env, int code, abi_ulong addr)
{
    return -TARGET_ENOSYS;
}
#else
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
#endif /* defined(TARGET_ABI32 */
#endif /* defined(TARGET_I386) */

/*
 * These constants are generic.  Supply any that are missing from the host.
 */
#ifndef PR_SET_NAME
# define PR_SET_NAME    15
# define PR_GET_NAME    16
#endif
#ifndef PR_SET_FP_MODE
# define PR_SET_FP_MODE 45
# define PR_GET_FP_MODE 46
# define PR_FP_MODE_FR   (1 << 0)
# define PR_FP_MODE_FRE  (1 << 1)
#endif
#ifndef PR_SVE_SET_VL
# define PR_SVE_SET_VL  50
# define PR_SVE_GET_VL  51
# define PR_SVE_VL_LEN_MASK  0xffff
# define PR_SVE_VL_INHERIT   (1 << 17)
#endif
#ifndef PR_PAC_RESET_KEYS
# define PR_PAC_RESET_KEYS  54
# define PR_PAC_APIAKEY   (1 << 0)
# define PR_PAC_APIBKEY   (1 << 1)
# define PR_PAC_APDAKEY   (1 << 2)
# define PR_PAC_APDBKEY   (1 << 3)
# define PR_PAC_APGAKEY   (1 << 4)
#endif
#ifndef PR_SET_TAGGED_ADDR_CTRL
# define PR_SET_TAGGED_ADDR_CTRL 55
# define PR_GET_TAGGED_ADDR_CTRL 56
# define PR_TAGGED_ADDR_ENABLE  (1UL << 0)
#endif
#ifndef PR_MTE_TCF_SHIFT
# define PR_MTE_TCF_SHIFT       1
# define PR_MTE_TCF_NONE        (0UL << PR_MTE_TCF_SHIFT)
# define PR_MTE_TCF_SYNC        (1UL << PR_MTE_TCF_SHIFT)
# define PR_MTE_TCF_ASYNC       (2UL << PR_MTE_TCF_SHIFT)
# define PR_MTE_TCF_MASK        (3UL << PR_MTE_TCF_SHIFT)
# define PR_MTE_TAG_SHIFT       3
# define PR_MTE_TAG_MASK        (0xffffUL << PR_MTE_TAG_SHIFT)
#endif
#ifndef PR_SET_IO_FLUSHER
# define PR_SET_IO_FLUSHER 57
# define PR_GET_IO_FLUSHER 58
#endif
#ifndef PR_SET_SYSCALL_USER_DISPATCH
# define PR_SET_SYSCALL_USER_DISPATCH 59
#endif
#ifndef PR_SME_SET_VL
# define PR_SME_SET_VL  63
# define PR_SME_GET_VL  64
# define PR_SME_VL_LEN_MASK  0xffff
# define PR_SME_VL_INHERIT   (1 << 17)
#endif

#include "target_prctl.h"

static abi_long do_prctl_inval0(CPUArchState *env)
{
    return -TARGET_EINVAL;
}

static abi_long do_prctl_inval1(CPUArchState *env, abi_long arg2)
{
    return -TARGET_EINVAL;
}

#ifndef do_prctl_get_fp_mode
#define do_prctl_get_fp_mode do_prctl_inval0
#endif
#ifndef do_prctl_set_fp_mode
#define do_prctl_set_fp_mode do_prctl_inval1
#endif
#ifndef do_prctl_sve_get_vl
#define do_prctl_sve_get_vl do_prctl_inval0
#endif
#ifndef do_prctl_sve_set_vl
#define do_prctl_sve_set_vl do_prctl_inval1
#endif
#ifndef do_prctl_reset_keys
#define do_prctl_reset_keys do_prctl_inval1
#endif
#ifndef do_prctl_set_tagged_addr_ctrl
#define do_prctl_set_tagged_addr_ctrl do_prctl_inval1
#endif
#ifndef do_prctl_get_tagged_addr_ctrl
#define do_prctl_get_tagged_addr_ctrl do_prctl_inval0
#endif
#ifndef do_prctl_get_unalign
#define do_prctl_get_unalign do_prctl_inval1
#endif
#ifndef do_prctl_set_unalign
#define do_prctl_set_unalign do_prctl_inval1
#endif
#ifndef do_prctl_sme_get_vl
#define do_prctl_sme_get_vl do_prctl_inval0
#endif
#ifndef do_prctl_sme_set_vl
#define do_prctl_sme_set_vl do_prctl_inval1
#endif

static abi_long do_prctl(CPUArchState *env, abi_long option, abi_long arg2,
                         abi_long arg3, abi_long arg4, abi_long arg5)
{
    abi_long ret;

    switch (option) {
    case PR_GET_PDEATHSIG:
        {
            int deathsig;
            ret = get_errno(prctl(PR_GET_PDEATHSIG, &deathsig,
                                  arg3, arg4, arg5));
            if (!is_error(ret) &&
                put_user_s32(host_to_target_signal(deathsig), arg2)) {
                return -TARGET_EFAULT;
            }
            return ret;
        }
    case PR_SET_PDEATHSIG:
        return get_errno(prctl(PR_SET_PDEATHSIG, target_to_host_signal(arg2),
                               arg3, arg4, arg5));
    case PR_GET_NAME:
        {
            void *name = lock_user(VERIFY_WRITE, arg2, 16, 1);
            if (!name) {
                return -TARGET_EFAULT;
            }
            ret = get_errno(prctl(PR_GET_NAME, (uintptr_t)name,
                                  arg3, arg4, arg5));
            unlock_user(name, arg2, 16);
            return ret;
        }
    case PR_SET_NAME:
        {
            void *name = lock_user(VERIFY_READ, arg2, 16, 1);
            if (!name) {
                return -TARGET_EFAULT;
            }
            ret = get_errno(prctl(PR_SET_NAME, (uintptr_t)name,
                                  arg3, arg4, arg5));
            unlock_user(name, arg2, 0);
            return ret;
        }
    case PR_GET_FP_MODE:
        return do_prctl_get_fp_mode(env);
    case PR_SET_FP_MODE:
        return do_prctl_set_fp_mode(env, arg2);
    case PR_SVE_GET_VL:
        return do_prctl_sve_get_vl(env);
    case PR_SVE_SET_VL:
        return do_prctl_sve_set_vl(env, arg2);
    case PR_SME_GET_VL:
        return do_prctl_sme_get_vl(env);
    case PR_SME_SET_VL:
        return do_prctl_sme_set_vl(env, arg2);
    case PR_PAC_RESET_KEYS:
        if (arg3 || arg4 || arg5) {
            return -TARGET_EINVAL;
        }
        return do_prctl_reset_keys(env, arg2);
    case PR_SET_TAGGED_ADDR_CTRL:
        if (arg3 || arg4 || arg5) {
            return -TARGET_EINVAL;
        }
        return do_prctl_set_tagged_addr_ctrl(env, arg2);
    case PR_GET_TAGGED_ADDR_CTRL:
        if (arg2 || arg3 || arg4 || arg5) {
            return -TARGET_EINVAL;
        }
        return do_prctl_get_tagged_addr_ctrl(env);

    case PR_GET_UNALIGN:
        return do_prctl_get_unalign(env, arg2);
    case PR_SET_UNALIGN:
        return do_prctl_set_unalign(env, arg2);

    case PR_CAP_AMBIENT:
    case PR_CAPBSET_READ:
    case PR_CAPBSET_DROP:
    case PR_GET_DUMPABLE:
    case PR_SET_DUMPABLE:
    case PR_GET_KEEPCAPS:
    case PR_SET_KEEPCAPS:
    case PR_GET_SECUREBITS:
    case PR_SET_SECUREBITS:
    case PR_GET_TIMING:
    case PR_SET_TIMING:
    case PR_GET_TIMERSLACK:
    case PR_SET_TIMERSLACK:
    case PR_MCE_KILL:
    case PR_MCE_KILL_GET:
    case PR_GET_NO_NEW_PRIVS:
    case PR_SET_NO_NEW_PRIVS:
    case PR_GET_IO_FLUSHER:
    case PR_SET_IO_FLUSHER:
        /* Some prctl options have no pointer arguments and we can pass on. */
        return get_errno(prctl(option, arg2, arg3, arg4, arg5));

    case PR_GET_CHILD_SUBREAPER:
    case PR_SET_CHILD_SUBREAPER:
    case PR_GET_SPECULATION_CTRL:
    case PR_SET_SPECULATION_CTRL:
    case PR_GET_TID_ADDRESS:
        /* TODO */
        return -TARGET_EINVAL;

    case PR_GET_FPEXC:
    case PR_SET_FPEXC:
        /* Was used for SPE on PowerPC. */
        return -TARGET_EINVAL;

    case PR_GET_ENDIAN:
    case PR_SET_ENDIAN:
    case PR_GET_FPEMU:
    case PR_SET_FPEMU:
    case PR_SET_MM:
    case PR_GET_SECCOMP:
    case PR_SET_SECCOMP:
    case PR_SET_SYSCALL_USER_DISPATCH:
    case PR_GET_THP_DISABLE:
    case PR_SET_THP_DISABLE:
    case PR_GET_TSC:
    case PR_SET_TSC:
        /* Disable to prevent the target disabling stuff we need. */
        return -TARGET_EINVAL;

    default:
        qemu_log_mask(LOG_UNIMP, "Unsupported prctl: " TARGET_ABI_FMT_ld "\n",
                      option);
        return -TARGET_EINVAL;
    }
}

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

    rcu_register_thread();
    tcg_register_thread();
    env = info->env;
    cpu = env_cpu(env);
    thread_cpu = cpu;
    ts = (TaskState *)cpu->opaque;
    info->tid = sys_gettid();
    task_settid(ts);
    if (info->child_tidptr)
        put_user_u32(info->tid, info->child_tidptr);
    if (info->parent_tidptr)
        put_user_u32(info->tid, info->parent_tidptr);
    qemu_guest_random_seed_thread_part2(cpu->random_seed);
    /* Enable signals.  */
    sigprocmask(SIG_SETMASK, &info->sigmask, NULL);
    /* Signal to the parent that we're ready.  */
    pthread_mutex_lock(&info->mutex);
    pthread_cond_broadcast(&info->cond);
    pthread_mutex_unlock(&info->mutex);
    /* Wait until the parent has finished initializing the tls state.  */
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
    CPUState *cpu = env_cpu(env);
    int ret;
    TaskState *ts;
    CPUState *new_cpu;
    CPUArchState *new_env;
    sigset_t sigmask;

    flags &= ~CLONE_IGNORED_FLAGS;

    /* Emulate vfork() with fork() */
    if (flags & CLONE_VFORK)
        flags &= ~(CLONE_VFORK | CLONE_VM);

    if (flags & CLONE_VM) {
        TaskState *parent_ts = (TaskState *)cpu->opaque;
        new_thread_info info;
        pthread_attr_t attr;

        if (((flags & CLONE_THREAD_FLAGS) != CLONE_THREAD_FLAGS) ||
            (flags & CLONE_INVALID_THREAD_FLAGS)) {
            return -TARGET_EINVAL;
        }

        ts = g_new0(TaskState, 1);
        init_task_state(ts);

        /* Grab a mutex so that thread setup appears atomic.  */
        pthread_mutex_lock(&clone_lock);

        /*
         * If this is our first additional thread, we need to ensure we
         * generate code for parallel execution and flush old translations.
         * Do this now so that the copy gets CF_PARALLEL too.
         */
        if (!(cpu->tcg_cflags & CF_PARALLEL)) {
            cpu->tcg_cflags |= CF_PARALLEL;
            tb_flush(cpu);
        }

        /* we create a new CPU instance. */
        new_env = cpu_copy(env);
        /* Init regs that differ from the parent.  */
        cpu_clone_regs_child(new_env, newsp, flags);
        cpu_clone_regs_parent(env, flags);
        new_cpu = env_cpu(new_env);
        new_cpu->opaque = ts;
        ts->bprm = parent_ts->bprm;
        ts->info = parent_ts->info;
        ts->signal_mask = parent_ts->signal_mask;

        if (flags & CLONE_CHILD_CLEARTID) {
            ts->child_tidptr = child_tidptr;
        }

        if (flags & CLONE_SETTLS) {
            cpu_set_tls (new_env, newtls);
        }

        memset(&info, 0, sizeof(info));
        pthread_mutex_init(&info.mutex, NULL);
        pthread_mutex_lock(&info.mutex);
        pthread_cond_init(&info.cond, NULL);
        info.env = new_env;
        if (flags & CLONE_CHILD_SETTID) {
            info.child_tidptr = child_tidptr;
        }
        if (flags & CLONE_PARENT_SETTID) {
            info.parent_tidptr = parent_tidptr;
        }

        ret = pthread_attr_init(&attr);
        ret = pthread_attr_setstacksize(&attr, NEW_STACK_SIZE);
        ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        /* It is not safe to deliver signals until the child has finished
           initializing, so temporarily block all signals.  */
        sigfillset(&sigmask);
        sigprocmask(SIG_BLOCK, &sigmask, &info.sigmask);
        cpu->random_seed = qemu_guest_random_seed_thread_part1();

        ret = pthread_create(&info.thread, &attr, clone_func, &info);
        /* TODO: Free new CPU state if thread creation failed.  */

        sigprocmask(SIG_SETMASK, &info.sigmask, NULL);
        pthread_attr_destroy(&attr);
        if (ret == 0) {
            /* Wait for the child to initialize.  */
            pthread_cond_wait(&info.cond, &info.mutex);
            ret = info.tid;
        } else {
            ret = -1;
        }
        pthread_mutex_unlock(&info.mutex);
        pthread_cond_destroy(&info.cond);
        pthread_mutex_destroy(&info.mutex);
        pthread_mutex_unlock(&clone_lock);
    } else {
        /* if no CLONE_VM, we consider it is a fork */
        if (flags & CLONE_INVALID_FORK_FLAGS) {
            return -TARGET_EINVAL;
        }

        /* We can't support custom termination signals */
        if ((flags & CSIGNAL) != TARGET_SIGCHLD) {
            return -TARGET_EINVAL;
        }

        if (block_signals()) {
            return -QEMU_ERESTARTSYS;
        }

        fork_start();
        ret = fork();
        if (ret == 0) {
            /* Child Process.  */
            cpu_clone_regs_child(env, newsp, flags);
            fork_end(1);
            /* There is a race condition here.  The parent process could
               theoretically read the TID in the child process before the child
               tid is set.  This would require using either ptrace
               (not implemented) or having *_tidptr to point at a shared memory
               mapping.  We can't repeat the spinlock hack used above because
               the child process gets its own copy of the lock.  */
            if (flags & CLONE_CHILD_SETTID)
                put_user_u32(sys_gettid(), child_tidptr);
            if (flags & CLONE_PARENT_SETTID)
                put_user_u32(sys_gettid(), parent_tidptr);
            ts = (TaskState *)cpu->opaque;
            if (flags & CLONE_SETTLS)
                cpu_set_tls (env, newtls);
            if (flags & CLONE_CHILD_CLEARTID)
                ts->child_tidptr = child_tidptr;
        } else {
            cpu_clone_regs_parent(env, flags);
            fork_end(0);
        }
    }
    return ret;
}

/* warning : doesn't handle linux specific flags... */
static int target_to_host_fcntl_cmd(int cmd)
{
    int ret;

    switch(cmd) {
    case TARGET_F_DUPFD:
    case TARGET_F_GETFD:
    case TARGET_F_SETFD:
    case TARGET_F_GETFL:
    case TARGET_F_SETFL:
    case TARGET_F_OFD_GETLK:
    case TARGET_F_OFD_SETLK:
    case TARGET_F_OFD_SETLKW:
        ret = cmd;
        break;
    case TARGET_F_GETLK:
        ret = F_GETLK64;
        break;
    case TARGET_F_SETLK:
        ret = F_SETLK64;
        break;
    case TARGET_F_SETLKW:
        ret = F_SETLKW64;
        break;
    case TARGET_F_GETOWN:
        ret = F_GETOWN;
        break;
    case TARGET_F_SETOWN:
        ret = F_SETOWN;
        break;
    case TARGET_F_GETSIG:
        ret = F_GETSIG;
        break;
    case TARGET_F_SETSIG:
        ret = F_SETSIG;
        break;
#if TARGET_ABI_BITS == 32
    case TARGET_F_GETLK64:
        ret = F_GETLK64;
        break;
    case TARGET_F_SETLK64:
        ret = F_SETLK64;
        break;
    case TARGET_F_SETLKW64:
        ret = F_SETLKW64;
        break;
#endif
    case TARGET_F_SETLEASE:
        ret = F_SETLEASE;
        break;
    case TARGET_F_GETLEASE:
        ret = F_GETLEASE;
        break;
#ifdef F_DUPFD_CLOEXEC
    case TARGET_F_DUPFD_CLOEXEC:
        ret = F_DUPFD_CLOEXEC;
        break;
#endif
    case TARGET_F_NOTIFY:
        ret = F_NOTIFY;
        break;
#ifdef F_GETOWN_EX
    case TARGET_F_GETOWN_EX:
        ret = F_GETOWN_EX;
        break;
#endif
#ifdef F_SETOWN_EX
    case TARGET_F_SETOWN_EX:
        ret = F_SETOWN_EX;
        break;
#endif
#ifdef F_SETPIPE_SZ
    case TARGET_F_SETPIPE_SZ:
        ret = F_SETPIPE_SZ;
        break;
    case TARGET_F_GETPIPE_SZ:
        ret = F_GETPIPE_SZ;
        break;
#endif
#ifdef F_ADD_SEALS
    case TARGET_F_ADD_SEALS:
        ret = F_ADD_SEALS;
        break;
    case TARGET_F_GET_SEALS:
        ret = F_GET_SEALS;
        break;
#endif
    default:
        ret = -TARGET_EINVAL;
        break;
    }

#if defined(__powerpc64__)
    /* On PPC64, glibc headers has the F_*LK* defined to 12, 13 and 14 and
     * is not supported by kernel. The glibc fcntl call actually adjusts
     * them to 5, 6 and 7 before making the syscall(). Since we make the
     * syscall directly, adjust to what is supported by the kernel.
     */
    if (ret >= F_GETLK64 && ret <= F_SETLKW64) {
        ret -= F_GETLK64 - 5;
    }
#endif

    return ret;
}

#define FLOCK_TRANSTBL \
    switch (type) { \
    TRANSTBL_CONVERT(F_RDLCK); \
    TRANSTBL_CONVERT(F_WRLCK); \
    TRANSTBL_CONVERT(F_UNLCK); \
    }

static int target_to_host_flock(int type)
{
#define TRANSTBL_CONVERT(a) case TARGET_##a: return a
    FLOCK_TRANSTBL
#undef  TRANSTBL_CONVERT
    return -TARGET_EINVAL;
}

static int host_to_target_flock(int type)
{
#define TRANSTBL_CONVERT(a) case a: return TARGET_##a
    FLOCK_TRANSTBL
#undef  TRANSTBL_CONVERT
    /* if we don't know how to convert the value coming
     * from the host we copy to the target field as-is
     */
    return type;
}

static inline abi_long copy_from_user_flock(struct flock64 *fl,
                                            abi_ulong target_flock_addr)
{
    struct target_flock *target_fl;
    int l_type;

    if (!lock_user_struct(VERIFY_READ, target_fl, target_flock_addr, 1)) {
        return -TARGET_EFAULT;
    }

    __get_user(l_type, &target_fl->l_type);
    l_type = target_to_host_flock(l_type);
    if (l_type < 0) {
        return l_type;
    }
    fl->l_type = l_type;
    __get_user(fl->l_whence, &target_fl->l_whence);
    __get_user(fl->l_start, &target_fl->l_start);
    __get_user(fl->l_len, &target_fl->l_len);
    __get_user(fl->l_pid, &target_fl->l_pid);
    unlock_user_struct(target_fl, target_flock_addr, 0);
    return 0;
}

static inline abi_long copy_to_user_flock(abi_ulong target_flock_addr,
                                          const struct flock64 *fl)
{
    struct target_flock *target_fl;
    short l_type;

    if (!lock_user_struct(VERIFY_WRITE, target_fl, target_flock_addr, 0)) {
        return -TARGET_EFAULT;
    }

    l_type = host_to_target_flock(fl->l_type);
    __put_user(l_type, &target_fl->l_type);
    __put_user(fl->l_whence, &target_fl->l_whence);
    __put_user(fl->l_start, &target_fl->l_start);
    __put_user(fl->l_len, &target_fl->l_len);
    __put_user(fl->l_pid, &target_fl->l_pid);
    unlock_user_struct(target_fl, target_flock_addr, 1);
    return 0;
}

typedef abi_long from_flock64_fn(struct flock64 *fl, abi_ulong target_addr);
typedef abi_long to_flock64_fn(abi_ulong target_addr, const struct flock64 *fl);

#if defined(TARGET_ARM) && TARGET_ABI_BITS == 32
struct target_oabi_flock64 {
    abi_short l_type;
    abi_short l_whence;
    abi_llong l_start;
    abi_llong l_len;
    abi_int   l_pid;
} QEMU_PACKED;

static inline abi_long copy_from_user_oabi_flock64(struct flock64 *fl,
                                                   abi_ulong target_flock_addr)
{
    struct target_oabi_flock64 *target_fl;
    int l_type;

    if (!lock_user_struct(VERIFY_READ, target_fl, target_flock_addr, 1)) {
        return -TARGET_EFAULT;
    }

    __get_user(l_type, &target_fl->l_type);
    l_type = target_to_host_flock(l_type);
    if (l_type < 0) {
        return l_type;
    }
    fl->l_type = l_type;
    __get_user(fl->l_whence, &target_fl->l_whence);
    __get_user(fl->l_start, &target_fl->l_start);
    __get_user(fl->l_len, &target_fl->l_len);
    __get_user(fl->l_pid, &target_fl->l_pid);
    unlock_user_struct(target_fl, target_flock_addr, 0);
    return 0;
}

static inline abi_long copy_to_user_oabi_flock64(abi_ulong target_flock_addr,
                                                 const struct flock64 *fl)
{
    struct target_oabi_flock64 *target_fl;
    short l_type;

    if (!lock_user_struct(VERIFY_WRITE, target_fl, target_flock_addr, 0)) {
        return -TARGET_EFAULT;
    }

    l_type = host_to_target_flock(fl->l_type);
    __put_user(l_type, &target_fl->l_type);
    __put_user(fl->l_whence, &target_fl->l_whence);
    __put_user(fl->l_start, &target_fl->l_start);
    __put_user(fl->l_len, &target_fl->l_len);
    __put_user(fl->l_pid, &target_fl->l_pid);
    unlock_user_struct(target_fl, target_flock_addr, 1);
    return 0;
}
#endif

static inline abi_long copy_from_user_flock64(struct flock64 *fl,
                                              abi_ulong target_flock_addr)
{
    struct target_flock64 *target_fl;
    int l_type;

    if (!lock_user_struct(VERIFY_READ, target_fl, target_flock_addr, 1)) {
        return -TARGET_EFAULT;
    }

    __get_user(l_type, &target_fl->l_type);
    l_type = target_to_host_flock(l_type);
    if (l_type < 0) {
        return l_type;
    }
    fl->l_type = l_type;
    __get_user(fl->l_whence, &target_fl->l_whence);
    __get_user(fl->l_start, &target_fl->l_start);
    __get_user(fl->l_len, &target_fl->l_len);
    __get_user(fl->l_pid, &target_fl->l_pid);
    unlock_user_struct(target_fl, target_flock_addr, 0);
    return 0;
}

static inline abi_long copy_to_user_flock64(abi_ulong target_flock_addr,
                                            const struct flock64 *fl)
{
    struct target_flock64 *target_fl;
    short l_type;

    if (!lock_user_struct(VERIFY_WRITE, target_fl, target_flock_addr, 0)) {
        return -TARGET_EFAULT;
    }

    l_type = host_to_target_flock(fl->l_type);
    __put_user(l_type, &target_fl->l_type);
    __put_user(fl->l_whence, &target_fl->l_whence);
    __put_user(fl->l_start, &target_fl->l_start);
    __put_user(fl->l_len, &target_fl->l_len);
    __put_user(fl->l_pid, &target_fl->l_pid);
    unlock_user_struct(target_fl, target_flock_addr, 1);
    return 0;
}

static abi_long do_fcntl(int fd, int cmd, abi_ulong arg)
{
    struct flock64 fl64;
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
        ret = copy_from_user_flock(&fl64, arg);
        if (ret) {
            return ret;
        }
        ret = get_errno(safe_fcntl(fd, host_cmd, &fl64));
        if (ret == 0) {
            ret = copy_to_user_flock(arg, &fl64);
        }
        break;

    case TARGET_F_SETLK:
    case TARGET_F_SETLKW:
        ret = copy_from_user_flock(&fl64, arg);
        if (ret) {
            return ret;
        }
        ret = get_errno(safe_fcntl(fd, host_cmd, &fl64));
        break;

    case TARGET_F_GETLK64:
    case TARGET_F_OFD_GETLK:
        ret = copy_from_user_flock64(&fl64, arg);
        if (ret) {
            return ret;
        }
        ret = get_errno(safe_fcntl(fd, host_cmd, &fl64));
        if (ret == 0) {
            ret = copy_to_user_flock64(arg, &fl64);
        }
        break;
    case TARGET_F_SETLK64:
    case TARGET_F_SETLKW64:
    case TARGET_F_OFD_SETLK:
    case TARGET_F_OFD_SETLKW:
        ret = copy_from_user_flock64(&fl64, arg);
        if (ret) {
            return ret;
        }
        ret = get_errno(safe_fcntl(fd, host_cmd, &fl64));
        break;

    case TARGET_F_GETFL:
        ret = get_errno(safe_fcntl(fd, host_cmd, arg));
        if (ret >= 0) {
            ret = host_to_target_bitmask(ret, fcntl_flags_tbl);
        }
        break;

    case TARGET_F_SETFL:
        ret = get_errno(safe_fcntl(fd, host_cmd,
                                   target_to_host_bitmask(arg,
                                                          fcntl_flags_tbl)));
        break;

#ifdef F_GETOWN_EX
    case TARGET_F_GETOWN_EX:
        ret = get_errno(safe_fcntl(fd, host_cmd, &fox));
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
        ret = get_errno(safe_fcntl(fd, host_cmd, &fox));
        break;
#endif

    case TARGET_F_SETSIG:
        ret = get_errno(safe_fcntl(fd, host_cmd, target_to_host_signal(arg)));
        break;

    case TARGET_F_GETSIG:
        ret = host_to_target_signal(get_errno(safe_fcntl(fd, host_cmd, arg)));
        break;

    case TARGET_F_SETOWN:
    case TARGET_F_GETOWN:
    case TARGET_F_SETLEASE:
    case TARGET_F_GETLEASE:
    case TARGET_F_SETPIPE_SZ:
    case TARGET_F_GETPIPE_SZ:
    case TARGET_F_ADD_SEALS:
    case TARGET_F_GET_SEALS:
        ret = get_errno(safe_fcntl(fd, host_cmd, arg));
        break;

    default:
        ret = get_errno(safe_fcntl(fd, cmd, arg));
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

/* We must do direct syscalls for setting UID/GID, because we want to
 * implement the Linux system call semantics of "change only for this thread",
 * not the libc/POSIX semantics of "change for all threads in process".
 * (See http://ewontfix.com/17/ for more details.)
 * We use the 32-bit version of the syscalls if present; if it is not
 * then either the host architecture supports 32-bit UIDs natively with
 * the standard syscall, or the 16-bit UID is the best we can do.
 */
#ifdef __NR_setuid32
#define __NR_sys_setuid __NR_setuid32
#else
#define __NR_sys_setuid __NR_setuid
#endif
#ifdef __NR_setgid32
#define __NR_sys_setgid __NR_setgid32
#else
#define __NR_sys_setgid __NR_setgid
#endif
#ifdef __NR_setresuid32
#define __NR_sys_setresuid __NR_setresuid32
#else
#define __NR_sys_setresuid __NR_setresuid
#endif
#ifdef __NR_setresgid32
#define __NR_sys_setresgid __NR_setresgid32
#else
#define __NR_sys_setresgid __NR_setresgid
#endif

_syscall1(int, sys_setuid, uid_t, uid)
_syscall1(int, sys_setgid, gid_t, gid)
_syscall3(int, sys_setresuid, uid_t, ruid, uid_t, euid, uid_t, suid)
_syscall3(int, sys_setresgid, gid_t, rgid, gid_t, egid, gid_t, sgid)

void syscall_init(void)
{
    IOCTLEntry *ie;
    const argtype *arg_type;
    int size;

    thunk_init(STRUCT_MAX);

#define STRUCT(name, ...) thunk_register_struct(STRUCT_ ## name, #name, struct_ ## name ## _def);
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

#ifdef TARGET_NR_truncate64
static inline abi_long target_truncate64(CPUArchState *cpu_env, const char *arg1,
                                         abi_long arg2,
                                         abi_long arg3,
                                         abi_long arg4)
{
    if (regpairs_aligned(cpu_env, TARGET_NR_truncate64)) {
        arg2 = arg3;
        arg3 = arg4;
    }
    return get_errno(truncate64(arg1, target_offset64(arg2, arg3)));
}
#endif

#ifdef TARGET_NR_ftruncate64
static inline abi_long target_ftruncate64(CPUArchState *cpu_env, abi_long arg1,
                                          abi_long arg2,
                                          abi_long arg3,
                                          abi_long arg4)
{
    if (regpairs_aligned(cpu_env, TARGET_NR_ftruncate64)) {
        arg2 = arg3;
        arg3 = arg4;
    }
    return get_errno(ftruncate64(arg1, target_offset64(arg2, arg3)));
}
#endif

#if defined(TARGET_NR_timer_settime) || \
    (defined(TARGET_NR_timerfd_settime) && defined(CONFIG_TIMERFD))
static inline abi_long target_to_host_itimerspec(struct itimerspec *host_its,
                                                 abi_ulong target_addr)
{
    if (target_to_host_timespec(&host_its->it_interval, target_addr +
                                offsetof(struct target_itimerspec,
                                         it_interval)) ||
        target_to_host_timespec(&host_its->it_value, target_addr +
                                offsetof(struct target_itimerspec,
                                         it_value))) {
        return -TARGET_EFAULT;
    }

    return 0;
}
#endif

#if defined(TARGET_NR_timer_settime64) || \
    (defined(TARGET_NR_timerfd_settime64) && defined(CONFIG_TIMERFD))
static inline abi_long target_to_host_itimerspec64(struct itimerspec *host_its,
                                                   abi_ulong target_addr)
{
    if (target_to_host_timespec64(&host_its->it_interval, target_addr +
                                  offsetof(struct target__kernel_itimerspec,
                                           it_interval)) ||
        target_to_host_timespec64(&host_its->it_value, target_addr +
                                  offsetof(struct target__kernel_itimerspec,
                                           it_value))) {
        return -TARGET_EFAULT;
    }

    return 0;
}
#endif

#if ((defined(TARGET_NR_timerfd_gettime) || \
      defined(TARGET_NR_timerfd_settime)) && defined(CONFIG_TIMERFD)) || \
      defined(TARGET_NR_timer_gettime) || defined(TARGET_NR_timer_settime)
static inline abi_long host_to_target_itimerspec(abi_ulong target_addr,
                                                 struct itimerspec *host_its)
{
    if (host_to_target_timespec(target_addr + offsetof(struct target_itimerspec,
                                                       it_interval),
                                &host_its->it_interval) ||
        host_to_target_timespec(target_addr + offsetof(struct target_itimerspec,
                                                       it_value),
                                &host_its->it_value)) {
        return -TARGET_EFAULT;
    }
    return 0;
}
#endif

#if ((defined(TARGET_NR_timerfd_gettime64) || \
      defined(TARGET_NR_timerfd_settime64)) && defined(CONFIG_TIMERFD)) || \
      defined(TARGET_NR_timer_gettime64) || defined(TARGET_NR_timer_settime64)
static inline abi_long host_to_target_itimerspec64(abi_ulong target_addr,
                                                   struct itimerspec *host_its)
{
    if (host_to_target_timespec64(target_addr +
                                  offsetof(struct target__kernel_itimerspec,
                                           it_interval),
                                  &host_its->it_interval) ||
        host_to_target_timespec64(target_addr +
                                  offsetof(struct target__kernel_itimerspec,
                                           it_value),
                                  &host_its->it_value)) {
        return -TARGET_EFAULT;
    }
    return 0;
}
#endif

#if defined(TARGET_NR_adjtimex) || \
    (defined(TARGET_NR_clock_adjtime) && defined(CONFIG_CLOCK_ADJTIME))
static inline abi_long target_to_host_timex(struct timex *host_tx,
                                            abi_long target_addr)
{
    struct target_timex *target_tx;

    if (!lock_user_struct(VERIFY_READ, target_tx, target_addr, 1)) {
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
    __get_user(host_tx->time.tv_sec, &target_tx->time.tv_sec);
    __get_user(host_tx->time.tv_usec, &target_tx->time.tv_usec);
    __get_user(host_tx->tick, &target_tx->tick);
    __get_user(host_tx->ppsfreq, &target_tx->ppsfreq);
    __get_user(host_tx->jitter, &target_tx->jitter);
    __get_user(host_tx->shift, &target_tx->shift);
    __get_user(host_tx->stabil, &target_tx->stabil);
    __get_user(host_tx->jitcnt, &target_tx->jitcnt);
    __get_user(host_tx->calcnt, &target_tx->calcnt);
    __get_user(host_tx->errcnt, &target_tx->errcnt);
    __get_user(host_tx->stbcnt, &target_tx->stbcnt);
    __get_user(host_tx->tai, &target_tx->tai);

    unlock_user_struct(target_tx, target_addr, 0);
    return 0;
}

static inline abi_long host_to_target_timex(abi_long target_addr,
                                            struct timex *host_tx)
{
    struct target_timex *target_tx;

    if (!lock_user_struct(VERIFY_WRITE, target_tx, target_addr, 0)) {
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
    __put_user(host_tx->time.tv_sec, &target_tx->time.tv_sec);
    __put_user(host_tx->time.tv_usec, &target_tx->time.tv_usec);
    __put_user(host_tx->tick, &target_tx->tick);
    __put_user(host_tx->ppsfreq, &target_tx->ppsfreq);
    __put_user(host_tx->jitter, &target_tx->jitter);
    __put_user(host_tx->shift, &target_tx->shift);
    __put_user(host_tx->stabil, &target_tx->stabil);
    __put_user(host_tx->jitcnt, &target_tx->jitcnt);
    __put_user(host_tx->calcnt, &target_tx->calcnt);
    __put_user(host_tx->errcnt, &target_tx->errcnt);
    __put_user(host_tx->stbcnt, &target_tx->stbcnt);
    __put_user(host_tx->tai, &target_tx->tai);

    unlock_user_struct(target_tx, target_addr, 1);
    return 0;
}
#endif


#if defined(TARGET_NR_clock_adjtime64) && defined(CONFIG_CLOCK_ADJTIME)
static inline abi_long target_to_host_timex64(struct timex *host_tx,
                                              abi_long target_addr)
{
    struct target__kernel_timex *target_tx;

    if (copy_from_user_timeval64(&host_tx->time, target_addr +
                                 offsetof(struct target__kernel_timex,
                                          time))) {
        return -TARGET_EFAULT;
    }

    if (!lock_user_struct(VERIFY_READ, target_tx, target_addr, 1)) {
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
    __get_user(host_tx->tick, &target_tx->tick);
    __get_user(host_tx->ppsfreq, &target_tx->ppsfreq);
    __get_user(host_tx->jitter, &target_tx->jitter);
    __get_user(host_tx->shift, &target_tx->shift);
    __get_user(host_tx->stabil, &target_tx->stabil);
    __get_user(host_tx->jitcnt, &target_tx->jitcnt);
    __get_user(host_tx->calcnt, &target_tx->calcnt);
    __get_user(host_tx->errcnt, &target_tx->errcnt);
    __get_user(host_tx->stbcnt, &target_tx->stbcnt);
    __get_user(host_tx->tai, &target_tx->tai);

    unlock_user_struct(target_tx, target_addr, 0);
    return 0;
}

static inline abi_long host_to_target_timex64(abi_long target_addr,
                                              struct timex *host_tx)
{
    struct target__kernel_timex *target_tx;

   if (copy_to_user_timeval64(target_addr +
                              offsetof(struct target__kernel_timex, time),
                              &host_tx->time)) {
        return -TARGET_EFAULT;
    }

    if (!lock_user_struct(VERIFY_WRITE, target_tx, target_addr, 0)) {
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
    __put_user(host_tx->tick, &target_tx->tick);
    __put_user(host_tx->ppsfreq, &target_tx->ppsfreq);
    __put_user(host_tx->jitter, &target_tx->jitter);
    __put_user(host_tx->shift, &target_tx->shift);
    __put_user(host_tx->stabil, &target_tx->stabil);
    __put_user(host_tx->jitcnt, &target_tx->jitcnt);
    __put_user(host_tx->calcnt, &target_tx->calcnt);
    __put_user(host_tx->errcnt, &target_tx->errcnt);
    __put_user(host_tx->stbcnt, &target_tx->stbcnt);
    __put_user(host_tx->tai, &target_tx->tai);

    unlock_user_struct(target_tx, target_addr, 1);
    return 0;
}
#endif

#ifndef HAVE_SIGEV_NOTIFY_THREAD_ID
#define sigev_notify_thread_id _sigev_un._tid
#endif

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
    host_sevp->sigev_notify_thread_id = tswap32(target_sevp->_sigev_un._tid);

    unlock_user_struct(target_sevp, target_addr, 1);
    return 0;
}

#if defined(TARGET_NR_mlockall)
static inline int target_to_host_mlockall_arg(int arg)
{
    int result = 0;

    if (arg & TARGET_MCL_CURRENT) {
        result |= MCL_CURRENT;
    }
    if (arg & TARGET_MCL_FUTURE) {
        result |= MCL_FUTURE;
    }
#ifdef MCL_ONFAULT
    if (arg & TARGET_MCL_ONFAULT) {
        result |= MCL_ONFAULT;
    }
#endif

    return result;
}
#endif

#if (defined(TARGET_NR_stat64) || defined(TARGET_NR_lstat64) ||     \
     defined(TARGET_NR_fstat64) || defined(TARGET_NR_fstatat64) ||  \
     defined(TARGET_NR_newfstatat))
static inline abi_long host_to_target_stat64(CPUArchState *cpu_env,
                                             abi_ulong target_addr,
                                             struct stat *host_st)
{
#if defined(TARGET_ARM) && defined(TARGET_ABI32)
    if (cpu_env->eabi) {
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
#ifdef HAVE_STRUCT_STAT_ST_ATIM
        __put_user(host_st->st_atim.tv_nsec, &target_st->target_st_atime_nsec);
        __put_user(host_st->st_mtim.tv_nsec, &target_st->target_st_mtime_nsec);
        __put_user(host_st->st_ctim.tv_nsec, &target_st->target_st_ctime_nsec);
#endif
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
#ifdef HAVE_STRUCT_STAT_ST_ATIM
        __put_user(host_st->st_atim.tv_nsec, &target_st->target_st_atime_nsec);
        __put_user(host_st->st_mtim.tv_nsec, &target_st->target_st_mtime_nsec);
        __put_user(host_st->st_ctim.tv_nsec, &target_st->target_st_ctime_nsec);
#endif
        unlock_user_struct(target_st, target_addr, 1);
    }

    return 0;
}
#endif

#if defined(TARGET_NR_statx) && defined(__NR_statx)
static inline abi_long host_to_target_statx(struct target_statx *host_stx,
                                            abi_ulong target_addr)
{
    struct target_statx *target_stx;

    if (!lock_user_struct(VERIFY_WRITE, target_stx, target_addr,  0)) {
        return -TARGET_EFAULT;
    }
    memset(target_stx, 0, sizeof(*target_stx));

    __put_user(host_stx->stx_mask, &target_stx->stx_mask);
    __put_user(host_stx->stx_blksize, &target_stx->stx_blksize);
    __put_user(host_stx->stx_attributes, &target_stx->stx_attributes);
    __put_user(host_stx->stx_nlink, &target_stx->stx_nlink);
    __put_user(host_stx->stx_uid, &target_stx->stx_uid);
    __put_user(host_stx->stx_gid, &target_stx->stx_gid);
    __put_user(host_stx->stx_mode, &target_stx->stx_mode);
    __put_user(host_stx->stx_ino, &target_stx->stx_ino);
    __put_user(host_stx->stx_size, &target_stx->stx_size);
    __put_user(host_stx->stx_blocks, &target_stx->stx_blocks);
    __put_user(host_stx->stx_attributes_mask, &target_stx->stx_attributes_mask);
    __put_user(host_stx->stx_atime.tv_sec, &target_stx->stx_atime.tv_sec);
    __put_user(host_stx->stx_atime.tv_nsec, &target_stx->stx_atime.tv_nsec);
    __put_user(host_stx->stx_btime.tv_sec, &target_stx->stx_btime.tv_sec);
    __put_user(host_stx->stx_btime.tv_nsec, &target_stx->stx_btime.tv_nsec);
    __put_user(host_stx->stx_ctime.tv_sec, &target_stx->stx_ctime.tv_sec);
    __put_user(host_stx->stx_ctime.tv_nsec, &target_stx->stx_ctime.tv_nsec);
    __put_user(host_stx->stx_mtime.tv_sec, &target_stx->stx_mtime.tv_sec);
    __put_user(host_stx->stx_mtime.tv_nsec, &target_stx->stx_mtime.tv_nsec);
    __put_user(host_stx->stx_rdev_major, &target_stx->stx_rdev_major);
    __put_user(host_stx->stx_rdev_minor, &target_stx->stx_rdev_minor);
    __put_user(host_stx->stx_dev_major, &target_stx->stx_dev_major);
    __put_user(host_stx->stx_dev_minor, &target_stx->stx_dev_minor);

    unlock_user_struct(target_stx, target_addr, 1);

    return 0;
}
#endif

static int do_sys_futex(int *uaddr, int op, int val,
                         const struct timespec *timeout, int *uaddr2,
                         int val3)
{
#if HOST_LONG_BITS == 64
#if defined(__NR_futex)
    /* always a 64-bit time_t, it doesn't define _time64 version  */
    return sys_futex(uaddr, op, val, timeout, uaddr2, val3);

#endif
#else /* HOST_LONG_BITS == 64 */
#if defined(__NR_futex_time64)
    if (sizeof(timeout->tv_sec) == 8) {
        /* _time64 function on 32bit arch */
        return sys_futex_time64(uaddr, op, val, timeout, uaddr2, val3);
    }
#endif
#if defined(__NR_futex)
    /* old function on 32bit arch */
    return sys_futex(uaddr, op, val, timeout, uaddr2, val3);
#endif
#endif /* HOST_LONG_BITS == 64 */
    g_assert_not_reached();
}

static int do_safe_futex(int *uaddr, int op, int val,
                         const struct timespec *timeout, int *uaddr2,
                         int val3)
{
#if HOST_LONG_BITS == 64
#if defined(__NR_futex)
    /* always a 64-bit time_t, it doesn't define _time64 version  */
    return get_errno(safe_futex(uaddr, op, val, timeout, uaddr2, val3));
#endif
#else /* HOST_LONG_BITS == 64 */
#if defined(__NR_futex_time64)
    if (sizeof(timeout->tv_sec) == 8) {
        /* _time64 function on 32bit arch */
        return get_errno(safe_futex_time64(uaddr, op, val, timeout, uaddr2,
                                           val3));
    }
#endif
#if defined(__NR_futex)
    /* old function on 32bit arch */
    return get_errno(safe_futex(uaddr, op, val, timeout, uaddr2, val3));
#endif
#endif /* HOST_LONG_BITS == 64 */
    return -TARGET_ENOSYS;
}

/* ??? Using host futex calls even when target atomic operations
   are not really atomic probably breaks things.  However implementing
   futexes locally would make futexes shared between multiple processes
   tricky.  However they're probably useless because guest atomic
   operations won't work either.  */
#if defined(TARGET_NR_futex) || defined(TARGET_NR_futex_time64)
static int do_futex(CPUState *cpu, bool time64, target_ulong uaddr,
                    int op, int val, target_ulong timeout,
                    target_ulong uaddr2, int val3)
{
    struct timespec ts, *pts = NULL;
    void *haddr2 = NULL;
    int base_op;

    /* We assume FUTEX_* constants are the same on both host and target. */
#ifdef FUTEX_CMD_MASK
    base_op = op & FUTEX_CMD_MASK;
#else
    base_op = op;
#endif
    switch (base_op) {
    case FUTEX_WAIT:
    case FUTEX_WAIT_BITSET:
        val = tswap32(val);
        break;
    case FUTEX_WAIT_REQUEUE_PI:
        val = tswap32(val);
        haddr2 = g2h(cpu, uaddr2);
        break;
    case FUTEX_LOCK_PI:
    case FUTEX_LOCK_PI2:
        break;
    case FUTEX_WAKE:
    case FUTEX_WAKE_BITSET:
    case FUTEX_TRYLOCK_PI:
    case FUTEX_UNLOCK_PI:
        timeout = 0;
        break;
    case FUTEX_FD:
        val = target_to_host_signal(val);
        timeout = 0;
        break;
    case FUTEX_CMP_REQUEUE:
    case FUTEX_CMP_REQUEUE_PI:
        val3 = tswap32(val3);
        /* fall through */
    case FUTEX_REQUEUE:
    case FUTEX_WAKE_OP:
        /*
         * For these, the 4th argument is not TIMEOUT, but VAL2.
         * But the prototype of do_safe_futex takes a pointer, so
         * insert casts to satisfy the compiler.  We do not need
         * to tswap VAL2 since it's not compared to guest memory.
          */
        pts = (struct timespec *)(uintptr_t)timeout;
        timeout = 0;
        haddr2 = g2h(cpu, uaddr2);
        break;
    default:
        return -TARGET_ENOSYS;
    }
    if (timeout) {
        pts = &ts;
        if (time64
            ? target_to_host_timespec64(pts, timeout)
            : target_to_host_timespec(pts, timeout)) {
            return -TARGET_EFAULT;
        }
    }
    return do_safe_futex(g2h(cpu, uaddr), op, val, pts, haddr2, val3);
}
#endif

#if defined(TARGET_NR_name_to_handle_at) && defined(CONFIG_OPEN_BY_HANDLE)
static abi_long do_name_to_handle_at(abi_long dirfd, abi_long pathname,
                                     abi_long handle, abi_long mount_id,
                                     abi_long flags)
{
    struct file_handle *target_fh;
    struct file_handle *fh;
    int mid = 0;
    abi_long ret;
    char *name;
    unsigned int size, total_size;

    if (get_user_s32(size, handle)) {
        return -TARGET_EFAULT;
    }

    name = lock_user_string(pathname);
    if (!name) {
        return -TARGET_EFAULT;
    }

    total_size = sizeof(struct file_handle) + size;
    target_fh = lock_user(VERIFY_WRITE, handle, total_size, 0);
    if (!target_fh) {
        unlock_user(name, pathname, 0);
        return -TARGET_EFAULT;
    }

    fh = g_malloc0(total_size);
    fh->handle_bytes = size;

    ret = get_errno(name_to_handle_at(dirfd, path(name), fh, &mid, flags));
    unlock_user(name, pathname, 0);

    /* man name_to_handle_at(2):
     * Other than the use of the handle_bytes field, the caller should treat
     * the file_handle structure as an opaque data type
     */

    memcpy(target_fh, fh, total_size);
    target_fh->handle_bytes = tswap32(fh->handle_bytes);
    target_fh->handle_type = tswap32(fh->handle_type);
    g_free(fh);
    unlock_user(target_fh, handle, total_size);

    if (put_user_s32(mid, mount_id)) {
        return -TARGET_EFAULT;
    }

    return ret;

}
#endif

#if defined(TARGET_NR_open_by_handle_at) && defined(CONFIG_OPEN_BY_HANDLE)
static abi_long do_open_by_handle_at(abi_long mount_fd, abi_long handle,
                                     abi_long flags)
{
    struct file_handle *target_fh;
    struct file_handle *fh;
    unsigned int size, total_size;
    abi_long ret;

    if (get_user_s32(size, handle)) {
        return -TARGET_EFAULT;
    }

    total_size = sizeof(struct file_handle) + size;
    target_fh = lock_user(VERIFY_READ, handle, total_size, 1);
    if (!target_fh) {
        return -TARGET_EFAULT;
    }

    fh = g_memdup(target_fh, total_size);
    fh->handle_bytes = size;
    fh->handle_type = tswap32(target_fh->handle_type);

    ret = get_errno(open_by_handle_at(mount_fd, fh,
                    target_to_host_bitmask(flags, fcntl_flags_tbl)));

    g_free(fh);

    unlock_user(target_fh, handle, total_size);

    return ret;
}
#endif

#if defined(TARGET_NR_signalfd) || defined(TARGET_NR_signalfd4)

static abi_long do_signalfd4(int fd, abi_long mask, int flags)
{
    int host_flags;
    target_sigset_t *target_mask;
    sigset_t host_mask;
    abi_long ret;

    if (flags & ~(TARGET_O_NONBLOCK_MASK | TARGET_O_CLOEXEC)) {
        return -TARGET_EINVAL;
    }
    if (!lock_user_struct(VERIFY_READ, target_mask, mask, 1)) {
        return -TARGET_EFAULT;
    }

    target_to_host_sigset(&host_mask, target_mask);

    host_flags = target_to_host_bitmask(flags, fcntl_flags_tbl);

    ret = get_errno(signalfd(fd, &host_mask, host_flags));
    if (ret >= 0) {
        fd_trans_register(ret, &target_signalfd_trans);
    }

    unlock_user_struct(target_mask, mask, 0);

    return ret;
}
#endif

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

static int open_self_cmdline(CPUArchState *cpu_env, int fd)
{
    CPUState *cpu = env_cpu(cpu_env);
    struct linux_binprm *bprm = ((TaskState *)cpu->opaque)->bprm;
    int i;

    for (i = 0; i < bprm->argc; i++) {
        size_t len = strlen(bprm->argv[i]) + 1;

        if (write(fd, bprm->argv[i], len) != len) {
            return -1;
        }
    }

    return 0;
}

static int open_self_maps(CPUArchState *cpu_env, int fd)
{
    CPUState *cpu = env_cpu(cpu_env);
    TaskState *ts = cpu->opaque;
    GSList *map_info = read_self_maps();
    GSList *s;
    int count;

    for (s = map_info; s; s = g_slist_next(s)) {
        MapInfo *e = (MapInfo *) s->data;

        if (h2g_valid(e->start)) {
            unsigned long min = e->start;
            unsigned long max = e->end;
            int flags = page_get_flags(h2g(min));
            const char *path;

            max = h2g_valid(max - 1) ?
                max : (uintptr_t) g2h_untagged(GUEST_ADDR_MAX) + 1;

            if (page_check_range(h2g(min), max - min, flags) == -1) {
                continue;
            }

#ifdef TARGET_HPPA
            if (h2g(max) == ts->info->stack_limit) {
#else
            if (h2g(min) == ts->info->stack_limit) {
#endif
                path = "[stack]";
            } else {
                path = e->path;
            }

            count = dprintf(fd, TARGET_ABI_FMT_ptr "-" TARGET_ABI_FMT_ptr
                            " %c%c%c%c %08" PRIx64 " %s %"PRId64,
                            h2g(min), h2g(max - 1) + 1,
                            (flags & PAGE_READ) ? 'r' : '-',
                            (flags & PAGE_WRITE_ORG) ? 'w' : '-',
                            (flags & PAGE_EXEC) ? 'x' : '-',
                            e->is_priv ? 'p' : 's',
                            (uint64_t) e->offset, e->dev, e->inode);
            if (path) {
                dprintf(fd, "%*s%s\n", 73 - count, "", path);
            } else {
                dprintf(fd, "\n");
            }
        }
    }

    free_self_maps(map_info);

#ifdef TARGET_VSYSCALL_PAGE
    /*
     * We only support execution from the vsyscall page.
     * This is as if CONFIG_LEGACY_VSYSCALL_XONLY=y from v5.3.
     */
    count = dprintf(fd, TARGET_FMT_lx "-" TARGET_FMT_lx
                    " --xp 00000000 00:00 0",
                    TARGET_VSYSCALL_PAGE, TARGET_VSYSCALL_PAGE + TARGET_PAGE_SIZE);
    dprintf(fd, "%*s%s\n", 73 - count, "",  "[vsyscall]");
#endif

    return 0;
}

static int open_self_stat(CPUArchState *cpu_env, int fd)
{
    CPUState *cpu = env_cpu(cpu_env);
    TaskState *ts = cpu->opaque;
    g_autoptr(GString) buf = g_string_new(NULL);
    int i;

    for (i = 0; i < 44; i++) {
        if (i == 0) {
            /* pid */
            g_string_printf(buf, FMT_pid " ", getpid());
        } else if (i == 1) {
            /* app name */
            gchar *bin = g_strrstr(ts->bprm->argv[0], "/");
            bin = bin ? bin + 1 : ts->bprm->argv[0];
            g_string_printf(buf, "(%.15s) ", bin);
        } else if (i == 3) {
            /* ppid */
            g_string_printf(buf, FMT_pid " ", getppid());
        } else if (i == 21) {
            /* starttime */
            g_string_printf(buf, "%" PRIu64 " ", ts->start_boottime);
        } else if (i == 27) {
            /* stack bottom */
            g_string_printf(buf, TARGET_ABI_FMT_ld " ", ts->info->start_stack);
        } else {
            /* for the rest, there is MasterCard */
            g_string_printf(buf, "0%c", i == 43 ? '\n' : ' ');
        }

        if (write(fd, buf->str, buf->len) != buf->len) {
            return -1;
        }
    }

    return 0;
}

static int open_self_auxv(CPUArchState *cpu_env, int fd)
{
    CPUState *cpu = env_cpu(cpu_env);
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

#if HOST_BIG_ENDIAN != TARGET_BIG_ENDIAN || \
    defined(TARGET_SPARC) || defined(TARGET_M68K) || defined(TARGET_HPPA)
static int is_proc(const char *filename, const char *entry)
{
    return strcmp(filename, entry) == 0;
}
#endif

#if HOST_BIG_ENDIAN != TARGET_BIG_ENDIAN
static int open_net_route(CPUArchState *cpu_env, int fd)
{
    FILE *fp;
    char *line = NULL;
    size_t len = 0;
    ssize_t read;

    fp = fopen("/proc/net/route", "r");
    if (fp == NULL) {
        return -1;
    }

    /* read header */

    read = getline(&line, &len, fp);
    dprintf(fd, "%s", line);

    /* read routes */

    while ((read = getline(&line, &len, fp)) != -1) {
        char iface[16];
        uint32_t dest, gw, mask;
        unsigned int flags, refcnt, use, metric, mtu, window, irtt;
        int fields;

        fields = sscanf(line,
                        "%s\t%08x\t%08x\t%04x\t%d\t%d\t%d\t%08x\t%d\t%u\t%u\n",
                        iface, &dest, &gw, &flags, &refcnt, &use, &metric,
                        &mask, &mtu, &window, &irtt);
        if (fields != 11) {
            continue;
        }
        dprintf(fd, "%s\t%08x\t%08x\t%04x\t%d\t%d\t%d\t%08x\t%d\t%u\t%u\n",
                iface, tswap32(dest), tswap32(gw), flags, refcnt, use,
                metric, tswap32(mask), mtu, window, irtt);
    }

    free(line);
    fclose(fp);

    return 0;
}
#endif

#if defined(TARGET_SPARC)
static int open_cpuinfo(CPUArchState *cpu_env, int fd)
{
    dprintf(fd, "type\t\t: sun4u\n");
    return 0;
}
#endif

#if defined(TARGET_HPPA)
static int open_cpuinfo(CPUArchState *cpu_env, int fd)
{
    dprintf(fd, "cpu family\t: PA-RISC 1.1e\n");
    dprintf(fd, "cpu\t\t: PA7300LC (PCX-L2)\n");
    dprintf(fd, "capabilities\t: os32\n");
    dprintf(fd, "model\t\t: 9000/778/B160L\n");
    dprintf(fd, "model name\t: Merlin L2 160 QEMU (9000/778/B160L)\n");
    return 0;
}
#endif

#if defined(TARGET_M68K)
static int open_hardware(CPUArchState *cpu_env, int fd)
{
    dprintf(fd, "Model:\t\tqemu-m68k\n");
    return 0;
}
#endif

static int do_openat(CPUArchState *cpu_env, int dirfd, const char *pathname, int flags, mode_t mode)
{
    struct fake_open {
        const char *filename;
        int (*fill)(CPUArchState *cpu_env, int fd);
        int (*cmp)(const char *s1, const char *s2);
    };
    const struct fake_open *fake_open;
    static const struct fake_open fakes[] = {
        { "maps", open_self_maps, is_proc_myself },
        { "stat", open_self_stat, is_proc_myself },
        { "auxv", open_self_auxv, is_proc_myself },
        { "cmdline", open_self_cmdline, is_proc_myself },
#if HOST_BIG_ENDIAN != TARGET_BIG_ENDIAN
        { "/proc/net/route", open_net_route, is_proc },
#endif
#if defined(TARGET_SPARC) || defined(TARGET_HPPA)
        { "/proc/cpuinfo", open_cpuinfo, is_proc },
#endif
#if defined(TARGET_M68K)
        { "/proc/hardware", open_hardware, is_proc },
#endif
        { NULL, NULL, NULL }
    };

    if (is_proc_myself(pathname, "exe")) {
        int execfd = qemu_getauxval(AT_EXECFD);
        return execfd ? execfd : safe_openat(dirfd, exec_path, flags, mode);
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

        fd = memfd_create("qemu-open", 0);
        if (fd < 0) {
            if (errno != ENOSYS) {
                return fd;
            }
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
        }

        if ((r = fake_open->fill(cpu_env, fd))) {
            int e = errno;
            close(fd);
            errno = e;
            return r;
        }
        lseek(fd, 0, SEEK_SET);

        return fd;
    }

    return safe_openat(dirfd, path(pathname), flags, mode);
}

#define TIMER_MAGIC 0x0caf0000
#define TIMER_MAGIC_MASK 0xffff0000

/* Convert QEMU provided timer ID back to internal 16bit index format */
static target_timer_t get_timer_id(abi_long arg)
{
    target_timer_t timerid = arg;

    if ((timerid & TIMER_MAGIC_MASK) != TIMER_MAGIC) {
        return -TARGET_EINVAL;
    }

    timerid &= 0xffff;

    if (timerid >= ARRAY_SIZE(g_posix_timers)) {
        return -TARGET_EINVAL;
    }

    return timerid;
}

static int target_to_host_cpu_mask(unsigned long *host_mask,
                                   size_t host_size,
                                   abi_ulong target_addr,
                                   size_t target_size)
{
    unsigned target_bits = sizeof(abi_ulong) * 8;
    unsigned host_bits = sizeof(*host_mask) * 8;
    abi_ulong *target_mask;
    unsigned i, j;

    assert(host_size >= target_size);

    target_mask = lock_user(VERIFY_READ, target_addr, target_size, 1);
    if (!target_mask) {
        return -TARGET_EFAULT;
    }
    memset(host_mask, 0, host_size);

    for (i = 0 ; i < target_size / sizeof(abi_ulong); i++) {
        unsigned bit = i * target_bits;
        abi_ulong val;

        __get_user(val, &target_mask[i]);
        for (j = 0; j < target_bits; j++, bit++) {
            if (val & (1UL << j)) {
                host_mask[bit / host_bits] |= 1UL << (bit % host_bits);
            }
        }
    }

    unlock_user(target_mask, target_addr, 0);
    return 0;
}

static int host_to_target_cpu_mask(const unsigned long *host_mask,
                                   size_t host_size,
                                   abi_ulong target_addr,
                                   size_t target_size)
{
    unsigned target_bits = sizeof(abi_ulong) * 8;
    unsigned host_bits = sizeof(*host_mask) * 8;
    abi_ulong *target_mask;
    unsigned i, j;

    assert(host_size >= target_size);

    target_mask = lock_user(VERIFY_WRITE, target_addr, target_size, 0);
    if (!target_mask) {
        return -TARGET_EFAULT;
    }

    for (i = 0 ; i < target_size / sizeof(abi_ulong); i++) {
        unsigned bit = i * target_bits;
        abi_ulong val = 0;

        for (j = 0; j < target_bits; j++, bit++) {
            if (host_mask[bit / host_bits] & (1UL << (bit % host_bits))) {
                val |= 1UL << j;
            }
        }
        __put_user(val, &target_mask[i]);
    }

    unlock_user(target_mask, target_addr, target_size);
    return 0;
}

#ifdef TARGET_NR_getdents
static int do_getdents(abi_long dirfd, abi_long arg2, abi_long count)
{
    g_autofree void *hdirp = NULL;
    void *tdirp;
    int hlen, hoff, toff;
    int hreclen, treclen;
    off64_t prev_diroff = 0;

    hdirp = g_try_malloc(count);
    if (!hdirp) {
        return -TARGET_ENOMEM;
    }

#ifdef EMULATE_GETDENTS_WITH_GETDENTS
    hlen = sys_getdents(dirfd, hdirp, count);
#else
    hlen = sys_getdents64(dirfd, hdirp, count);
#endif

    hlen = get_errno(hlen);
    if (is_error(hlen)) {
        return hlen;
    }

    tdirp = lock_user(VERIFY_WRITE, arg2, count, 0);
    if (!tdirp) {
        return -TARGET_EFAULT;
    }

    for (hoff = toff = 0; hoff < hlen; hoff += hreclen, toff += treclen) {
#ifdef EMULATE_GETDENTS_WITH_GETDENTS
        struct linux_dirent *hde = hdirp + hoff;
#else
        struct linux_dirent64 *hde = hdirp + hoff;
#endif
        struct target_dirent *tde = tdirp + toff;
        int namelen;
        uint8_t type;

        namelen = strlen(hde->d_name);
        hreclen = hde->d_reclen;
        treclen = offsetof(struct target_dirent, d_name) + namelen + 2;
        treclen = QEMU_ALIGN_UP(treclen, __alignof(struct target_dirent));

        if (toff + treclen > count) {
            /*
             * If the host struct is smaller than the target struct, or
             * requires less alignment and thus packs into less space,
             * then the host can return more entries than we can pass
             * on to the guest.
             */
            if (toff == 0) {
                toff = -TARGET_EINVAL; /* result buffer is too small */
                break;
            }
            /*
             * Return what we have, resetting the file pointer to the
             * location of the first record not returned.
             */
            lseek64(dirfd, prev_diroff, SEEK_SET);
            break;
        }

        prev_diroff = hde->d_off;
        tde->d_ino = tswapal(hde->d_ino);
        tde->d_off = tswapal(hde->d_off);
        tde->d_reclen = tswap16(treclen);
        memcpy(tde->d_name, hde->d_name, namelen + 1);

        /*
         * The getdents type is in what was formerly a padding byte at the
         * end of the structure.
         */
#ifdef EMULATE_GETDENTS_WITH_GETDENTS
        type = *((uint8_t *)hde + hreclen - 1);
#else
        type = hde->d_type;
#endif
        *((uint8_t *)tde + treclen - 1) = type;
    }

    unlock_user(tdirp, arg2, toff);
    return toff;
}
#endif /* TARGET_NR_getdents */

#if defined(TARGET_NR_getdents64) && defined(__NR_getdents64)
static int do_getdents64(abi_long dirfd, abi_long arg2, abi_long count)
{
    g_autofree void *hdirp = NULL;
    void *tdirp;
    int hlen, hoff, toff;
    int hreclen, treclen;
    off64_t prev_diroff = 0;

    hdirp = g_try_malloc(count);
    if (!hdirp) {
        return -TARGET_ENOMEM;
    }

    hlen = get_errno(sys_getdents64(dirfd, hdirp, count));
    if (is_error(hlen)) {
        return hlen;
    }

    tdirp = lock_user(VERIFY_WRITE, arg2, count, 0);
    if (!tdirp) {
        return -TARGET_EFAULT;
    }

    for (hoff = toff = 0; hoff < hlen; hoff += hreclen, toff += treclen) {
        struct linux_dirent64 *hde = hdirp + hoff;
        struct target_dirent64 *tde = tdirp + toff;
        int namelen;

        namelen = strlen(hde->d_name) + 1;
        hreclen = hde->d_reclen;
        treclen = offsetof(struct target_dirent64, d_name) + namelen;
        treclen = QEMU_ALIGN_UP(treclen, __alignof(struct target_dirent64));

        if (toff + treclen > count) {
            /*
             * If the host struct is smaller than the target struct, or
             * requires less alignment and thus packs into less space,
             * then the host can return more entries than we can pass
             * on to the guest.
             */
            if (toff == 0) {
                toff = -TARGET_EINVAL; /* result buffer is too small */
                break;
            }
            /*
             * Return what we have, resetting the file pointer to the
             * location of the first record not returned.
             */
            lseek64(dirfd, prev_diroff, SEEK_SET);
            break;
        }

        prev_diroff = hde->d_off;
        tde->d_ino = tswap64(hde->d_ino);
        tde->d_off = tswap64(hde->d_off);
        tde->d_reclen = tswap16(treclen);
        tde->d_type = hde->d_type;
        memcpy(tde->d_name, hde->d_name, namelen);
    }

    unlock_user(tdirp, arg2, toff);
    return toff;
}
#endif /* TARGET_NR_getdents64 */

#if defined(TARGET_NR_pivot_root) && defined(__NR_pivot_root)
_syscall2(int, pivot_root, const char *, new_root, const char *, put_old)
#endif

/* This is an internal helper for do_syscall so that it is easier
 * to have a single return point, so that actions, such as logging
 * of syscall results, can be performed.
 * All errnos that do_syscall() returns must be -TARGET_<errcode>.
 */
static abi_long do_syscall1(CPUArchState *cpu_env, int num, abi_long arg1,
                            abi_long arg2, abi_long arg3, abi_long arg4,
                            abi_long arg5, abi_long arg6, abi_long arg7,
                            abi_long arg8)
{
    CPUState *cpu = env_cpu(cpu_env);
    abi_long ret;
#if defined(TARGET_NR_stat) || defined(TARGET_NR_stat64) \
    || defined(TARGET_NR_lstat) || defined(TARGET_NR_lstat64) \
    || defined(TARGET_NR_fstat) || defined(TARGET_NR_fstat64) \
    || defined(TARGET_NR_statx)
    struct stat st;
#endif
#if defined(TARGET_NR_statfs) || defined(TARGET_NR_statfs64) \
    || defined(TARGET_NR_fstatfs)
    struct statfs stfs;
#endif
    void *p;

    switch(num) {
    case TARGET_NR_exit:
        /* In old applications this may be used to implement _exit(2).
           However in threaded applications it is used for thread termination,
           and _exit_group is used for application termination.
           Do thread termination if we have more then one thread.  */

        if (block_signals()) {
            return -QEMU_ERESTARTSYS;
        }

        pthread_mutex_lock(&clone_lock);

        if (CPU_NEXT(first_cpu)) {
            TaskState *ts = cpu->opaque;

            object_property_set_bool(OBJECT(cpu), "realized", false, NULL);
            object_unref(OBJECT(cpu));
            /*
             * At this point the CPU should be unrealized and removed
             * from cpu lists. We can clean-up the rest of the thread
             * data without the lock held.
             */

            pthread_mutex_unlock(&clone_lock);

            if (ts->child_tidptr) {
                put_user_u32(0, ts->child_tidptr);
                do_sys_futex(g2h(cpu, ts->child_tidptr),
                             FUTEX_WAKE, INT_MAX, NULL, NULL, 0);
            }
            thread_cpu = NULL;
            g_free(ts);
            rcu_unregister_thread();
            pthread_exit(NULL);
        }

        pthread_mutex_unlock(&clone_lock);
        preexit_cleanup(cpu_env, arg1);
        _exit(arg1);
        return 0; /* avoid warning */
    case TARGET_NR_read:
        if (arg2 == 0 && arg3 == 0) {
            return get_errno(safe_read(arg1, 0, 0));
        } else {
            if (!(p = lock_user(VERIFY_WRITE, arg2, arg3, 0)))
                return -TARGET_EFAULT;
            ret = get_errno(safe_read(arg1, p, arg3));
            if (ret >= 0 &&
                fd_trans_host_to_target_data(arg1)) {
                ret = fd_trans_host_to_target_data(arg1)(p, ret);
            }
            unlock_user(p, arg2, ret);
        }
        return ret;
    case TARGET_NR_write:
        if (arg2 == 0 && arg3 == 0) {
            return get_errno(safe_write(arg1, 0, 0));
        }
        if (!(p = lock_user(VERIFY_READ, arg2, arg3, 1)))
            return -TARGET_EFAULT;
        if (fd_trans_target_to_host_data(arg1)) {
            void *copy = g_malloc(arg3);
            memcpy(copy, p, arg3);
            ret = fd_trans_target_to_host_data(arg1)(copy, arg3);
            if (ret >= 0) {
                ret = get_errno(safe_write(arg1, copy, ret));
            }
            g_free(copy);
        } else {
            ret = get_errno(safe_write(arg1, p, arg3));
        }
        unlock_user(p, arg2, 0);
        return ret;

#ifdef TARGET_NR_open
    case TARGET_NR_open:
        if (!(p = lock_user_string(arg1)))
            return -TARGET_EFAULT;
        ret = get_errno(do_openat(cpu_env, AT_FDCWD, p,
                                  target_to_host_bitmask(arg2, fcntl_flags_tbl),
                                  arg3));
        fd_trans_unregister(ret);
        unlock_user(p, arg1, 0);
        return ret;
#endif
    case TARGET_NR_openat:
        if (!(p = lock_user_string(arg2)))
            return -TARGET_EFAULT;
        ret = get_errno(do_openat(cpu_env, arg1, p,
                                  target_to_host_bitmask(arg3, fcntl_flags_tbl),
                                  arg4));
        fd_trans_unregister(ret);
        unlock_user(p, arg2, 0);
        return ret;
#if defined(TARGET_NR_name_to_handle_at) && defined(CONFIG_OPEN_BY_HANDLE)
    case TARGET_NR_name_to_handle_at:
        ret = do_name_to_handle_at(arg1, arg2, arg3, arg4, arg5);
        return ret;
#endif
#if defined(TARGET_NR_open_by_handle_at) && defined(CONFIG_OPEN_BY_HANDLE)
    case TARGET_NR_open_by_handle_at:
        ret = do_open_by_handle_at(arg1, arg2, arg3);
        fd_trans_unregister(ret);
        return ret;
#endif
#if defined(__NR_pidfd_open) && defined(TARGET_NR_pidfd_open)
    case TARGET_NR_pidfd_open:
        return get_errno(pidfd_open(arg1, arg2));
#endif
#if defined(__NR_pidfd_send_signal) && defined(TARGET_NR_pidfd_send_signal)
    case TARGET_NR_pidfd_send_signal:
        {
            siginfo_t uinfo;

            p = lock_user(VERIFY_READ, arg3, sizeof(target_siginfo_t), 1);
            if (!p) {
                return -TARGET_EFAULT;
            }
            target_to_host_siginfo(&uinfo, p);
            unlock_user(p, arg3, 0);
            ret = get_errno(pidfd_send_signal(arg1, target_to_host_signal(arg2),
                &uinfo, arg4));
        }
        return ret;
#endif
#if defined(__NR_pidfd_getfd) && defined(TARGET_NR_pidfd_getfd)
    case TARGET_NR_pidfd_getfd:
        return get_errno(pidfd_getfd(arg1, arg2, arg3));
#endif
    case TARGET_NR_close:
        fd_trans_unregister(arg1);
        return get_errno(close(arg1));

    case TARGET_NR_brk:
        return do_brk(arg1);
#ifdef TARGET_NR_fork
    case TARGET_NR_fork:
        return get_errno(do_fork(cpu_env, TARGET_SIGCHLD, 0, 0, 0, 0));
#endif
#ifdef TARGET_NR_waitpid
    case TARGET_NR_waitpid:
        {
            int status;
            ret = get_errno(safe_wait4(arg1, &status, arg3, 0));
            if (!is_error(ret) && arg2 && ret
                && put_user_s32(host_to_target_waitstatus(status), arg2))
                return -TARGET_EFAULT;
        }
        return ret;
#endif
#ifdef TARGET_NR_waitid
    case TARGET_NR_waitid:
        {
            siginfo_t info;
            info.si_pid = 0;
            ret = get_errno(safe_waitid(arg1, arg2, &info, arg4, NULL));
            if (!is_error(ret) && arg3 && info.si_pid != 0) {
                if (!(p = lock_user(VERIFY_WRITE, arg3, sizeof(target_siginfo_t), 0)))
                    return -TARGET_EFAULT;
                host_to_target_siginfo(p, &info);
                unlock_user(p, arg3, sizeof(target_siginfo_t));
            }
        }
        return ret;
#endif
#ifdef TARGET_NR_creat /* not on alpha */
    case TARGET_NR_creat:
        if (!(p = lock_user_string(arg1)))
            return -TARGET_EFAULT;
        ret = get_errno(creat(p, arg2));
        fd_trans_unregister(ret);
        unlock_user(p, arg1, 0);
        return ret;
#endif
#ifdef TARGET_NR_link
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
        return ret;
#endif
#if defined(TARGET_NR_linkat)
    case TARGET_NR_linkat:
        {
            void * p2 = NULL;
            if (!arg2 || !arg4)
                return -TARGET_EFAULT;
            p  = lock_user_string(arg2);
            p2 = lock_user_string(arg4);
            if (!p || !p2)
                ret = -TARGET_EFAULT;
            else
                ret = get_errno(linkat(arg1, p, arg3, p2, arg5));
            unlock_user(p, arg2, 0);
            unlock_user(p2, arg4, 0);
        }
        return ret;
#endif
#ifdef TARGET_NR_unlink
    case TARGET_NR_unlink:
        if (!(p = lock_user_string(arg1)))
            return -TARGET_EFAULT;
        ret = get_errno(unlink(p));
        unlock_user(p, arg1, 0);
        return ret;
#endif
#if defined(TARGET_NR_unlinkat)
    case TARGET_NR_unlinkat:
        if (!(p = lock_user_string(arg2)))
            return -TARGET_EFAULT;
        ret = get_errno(unlinkat(arg1, p, arg3));
        unlock_user(p, arg2, 0);
        return ret;
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
                    return -TARGET_EFAULT;
                if (!addr)
                    break;
                argc++;
            }
            envc = 0;
            guest_envp = arg3;
            for (gp = guest_envp; gp; gp += sizeof(abi_ulong)) {
                if (get_user_ual(addr, gp))
                    return -TARGET_EFAULT;
                if (!addr)
                    break;
                envc++;
            }

            argp = g_new0(char *, argc + 1);
            envp = g_new0(char *, envc + 1);

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
            /* Although execve() is not an interruptible syscall it is
             * a special case where we must use the safe_syscall wrapper:
             * if we allow a signal to happen before we make the host
             * syscall then we will 'lose' it, because at the point of
             * execve the process leaves QEMU's control. So we use the
             * safe syscall wrapper to ensure that we either take the
             * signal as a guest signal, or else it does not happen
             * before the execve completes and makes it the other
             * program's problem.
             */
            ret = get_errno(safe_execve(p, argp, envp));
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

            g_free(argp);
            g_free(envp);
        }
        return ret;
    case TARGET_NR_chdir:
        if (!(p = lock_user_string(arg1)))
            return -TARGET_EFAULT;
        ret = get_errno(chdir(p));
        unlock_user(p, arg1, 0);
        return ret;
#ifdef TARGET_NR_time
    case TARGET_NR_time:
        {
            time_t host_time;
            ret = get_errno(time(&host_time));
            if (!is_error(ret)
                && arg1
                && put_user_sal(host_time, arg1))
                return -TARGET_EFAULT;
        }
        return ret;
#endif
#ifdef TARGET_NR_mknod
    case TARGET_NR_mknod:
        if (!(p = lock_user_string(arg1)))
            return -TARGET_EFAULT;
        ret = get_errno(mknod(p, arg2, arg3));
        unlock_user(p, arg1, 0);
        return ret;
#endif
#if defined(TARGET_NR_mknodat)
    case TARGET_NR_mknodat:
        if (!(p = lock_user_string(arg2)))
            return -TARGET_EFAULT;
        ret = get_errno(mknodat(arg1, p, arg3, arg4));
        unlock_user(p, arg2, 0);
        return ret;
#endif
#ifdef TARGET_NR_chmod
    case TARGET_NR_chmod:
        if (!(p = lock_user_string(arg1)))
            return -TARGET_EFAULT;
        ret = get_errno(chmod(p, arg2));
        unlock_user(p, arg1, 0);
        return ret;
#endif
#ifdef TARGET_NR_lseek
    case TARGET_NR_lseek:
        return get_errno(lseek(arg1, arg2, arg3));
#endif
#if defined(TARGET_NR_getxpid) && defined(TARGET_ALPHA)
    /* Alpha specific */
    case TARGET_NR_getxpid:
        cpu_env->ir[IR_A4] = getppid();
        return get_errno(getpid());
#endif
#ifdef TARGET_NR_getpid
    case TARGET_NR_getpid:
        return get_errno(getpid());
#endif
    case TARGET_NR_mount:
        {
            /* need to look at the data field */
            void *p2, *p3;

            if (arg1) {
                p = lock_user_string(arg1);
                if (!p) {
                    return -TARGET_EFAULT;
                }
            } else {
                p = NULL;
            }

            p2 = lock_user_string(arg2);
            if (!p2) {
                if (arg1) {
                    unlock_user(p, arg1, 0);
                }
                return -TARGET_EFAULT;
            }

            if (arg3) {
                p3 = lock_user_string(arg3);
                if (!p3) {
                    if (arg1) {
                        unlock_user(p, arg1, 0);
                    }
                    unlock_user(p2, arg2, 0);
                    return -TARGET_EFAULT;
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
                ret = mount(p, p2, p3, (unsigned long)arg4, g2h(cpu, arg5));
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
        return ret;
#if defined(TARGET_NR_umount) || defined(TARGET_NR_oldumount)
#if defined(TARGET_NR_umount)
    case TARGET_NR_umount:
#endif
#if defined(TARGET_NR_oldumount)
    case TARGET_NR_oldumount:
#endif
        if (!(p = lock_user_string(arg1)))
            return -TARGET_EFAULT;
        ret = get_errno(umount(p));
        unlock_user(p, arg1, 0);
        return ret;
#endif
#ifdef TARGET_NR_stime /* not on alpha */
    case TARGET_NR_stime:
        {
            struct timespec ts;
            ts.tv_nsec = 0;
            if (get_user_sal(ts.tv_sec, arg1)) {
                return -TARGET_EFAULT;
            }
            return get_errno(clock_settime(CLOCK_REALTIME, &ts));
        }
#endif
#ifdef TARGET_NR_alarm /* not on alpha */
    case TARGET_NR_alarm:
        return alarm(arg1);
#endif
#ifdef TARGET_NR_pause /* not on alpha */
    case TARGET_NR_pause:
        if (!block_signals()) {
            sigsuspend(&((TaskState *)cpu->opaque)->signal_mask);
        }
        return -TARGET_EINTR;
#endif
#ifdef TARGET_NR_utime
    case TARGET_NR_utime:
        {
            struct utimbuf tbuf, *host_tbuf;
            struct target_utimbuf *target_tbuf;
            if (arg2) {
                if (!lock_user_struct(VERIFY_READ, target_tbuf, arg2, 1))
                    return -TARGET_EFAULT;
                tbuf.actime = tswapal(target_tbuf->actime);
                tbuf.modtime = tswapal(target_tbuf->modtime);
                unlock_user_struct(target_tbuf, arg2, 0);
                host_tbuf = &tbuf;
            } else {
                host_tbuf = NULL;
            }
            if (!(p = lock_user_string(arg1)))
                return -TARGET_EFAULT;
            ret = get_errno(utime(p, host_tbuf));
            unlock_user(p, arg1, 0);
        }
        return ret;
#endif
#ifdef TARGET_NR_utimes
    case TARGET_NR_utimes:
        {
            struct timeval *tvp, tv[2];
            if (arg2) {
                if (copy_from_user_timeval(&tv[0], arg2)
                    || copy_from_user_timeval(&tv[1],
                                              arg2 + sizeof(struct target_timeval)))
                    return -TARGET_EFAULT;
                tvp = tv;
            } else {
                tvp = NULL;
            }
            if (!(p = lock_user_string(arg1)))
                return -TARGET_EFAULT;
            ret = get_errno(utimes(p, tvp));
            unlock_user(p, arg1, 0);
        }
        return ret;
#endif
#if defined(TARGET_NR_futimesat)
    case TARGET_NR_futimesat:
        {
            struct timeval *tvp, tv[2];
            if (arg3) {
                if (copy_from_user_timeval(&tv[0], arg3)
                    || copy_from_user_timeval(&tv[1],
                                              arg3 + sizeof(struct target_timeval)))
                    return -TARGET_EFAULT;
                tvp = tv;
            } else {
                tvp = NULL;
            }
            if (!(p = lock_user_string(arg2))) {
                return -TARGET_EFAULT;
            }
            ret = get_errno(futimesat(arg1, path(p), tvp));
            unlock_user(p, arg2, 0);
        }
        return ret;
#endif
#ifdef TARGET_NR_access
    case TARGET_NR_access:
        if (!(p = lock_user_string(arg1))) {
            return -TARGET_EFAULT;
        }
        ret = get_errno(access(path(p), arg2));
        unlock_user(p, arg1, 0);
        return ret;
#endif
#if defined(TARGET_NR_faccessat) && defined(__NR_faccessat)
    case TARGET_NR_faccessat:
        if (!(p = lock_user_string(arg2))) {
            return -TARGET_EFAULT;
        }
        ret = get_errno(faccessat(arg1, p, arg3, 0));
        unlock_user(p, arg2, 0);
        return ret;
#endif
#ifdef TARGET_NR_nice /* not on alpha */
    case TARGET_NR_nice:
        return get_errno(nice(arg1));
#endif
    case TARGET_NR_sync:
        sync();
        return 0;
#if defined(TARGET_NR_syncfs) && defined(CONFIG_SYNCFS)
    case TARGET_NR_syncfs:
        return get_errno(syncfs(arg1));
#endif
    case TARGET_NR_kill:
        return get_errno(safe_kill(arg1, target_to_host_signal(arg2)));
#ifdef TARGET_NR_rename
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
        return ret;
#endif
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
        return ret;
#endif
#if defined(TARGET_NR_renameat2)
    case TARGET_NR_renameat2:
        {
            void *p2;
            p  = lock_user_string(arg2);
            p2 = lock_user_string(arg4);
            if (!p || !p2) {
                ret = -TARGET_EFAULT;
            } else {
                ret = get_errno(sys_renameat2(arg1, p, arg3, p2, arg5));
            }
            unlock_user(p2, arg4, 0);
            unlock_user(p, arg2, 0);
        }
        return ret;
#endif
#ifdef TARGET_NR_mkdir
    case TARGET_NR_mkdir:
        if (!(p = lock_user_string(arg1)))
            return -TARGET_EFAULT;
        ret = get_errno(mkdir(p, arg2));
        unlock_user(p, arg1, 0);
        return ret;
#endif
#if defined(TARGET_NR_mkdirat)
    case TARGET_NR_mkdirat:
        if (!(p = lock_user_string(arg2)))
            return -TARGET_EFAULT;
        ret = get_errno(mkdirat(arg1, p, arg3));
        unlock_user(p, arg2, 0);
        return ret;
#endif
#ifdef TARGET_NR_rmdir
    case TARGET_NR_rmdir:
        if (!(p = lock_user_string(arg1)))
            return -TARGET_EFAULT;
        ret = get_errno(rmdir(p));
        unlock_user(p, arg1, 0);
        return ret;
#endif
    case TARGET_NR_dup:
        ret = get_errno(dup(arg1));
        if (ret >= 0) {
            fd_trans_dup(arg1, ret);
        }
        return ret;
#ifdef TARGET_NR_pipe
    case TARGET_NR_pipe:
        return do_pipe(cpu_env, arg1, 0, 0);
#endif
#ifdef TARGET_NR_pipe2
    case TARGET_NR_pipe2:
        return do_pipe(cpu_env, arg1,
                       target_to_host_bitmask(arg2, fcntl_flags_tbl), 1);
#endif
    case TARGET_NR_times:
        {
            struct target_tms *tmsp;
            struct tms tms;
            ret = get_errno(times(&tms));
            if (arg1) {
                tmsp = lock_user(VERIFY_WRITE, arg1, sizeof(struct target_tms), 0);
                if (!tmsp)
                    return -TARGET_EFAULT;
                tmsp->tms_utime = tswapal(host_to_target_clock_t(tms.tms_utime));
                tmsp->tms_stime = tswapal(host_to_target_clock_t(tms.tms_stime));
                tmsp->tms_cutime = tswapal(host_to_target_clock_t(tms.tms_cutime));
                tmsp->tms_cstime = tswapal(host_to_target_clock_t(tms.tms_cstime));
            }
            if (!is_error(ret))
                ret = host_to_target_clock_t(ret);
        }
        return ret;
    case TARGET_NR_acct:
        if (arg1 == 0) {
            ret = get_errno(acct(NULL));
        } else {
            if (!(p = lock_user_string(arg1))) {
                return -TARGET_EFAULT;
            }
            ret = get_errno(acct(path(p)));
            unlock_user(p, arg1, 0);
        }
        return ret;
#ifdef TARGET_NR_umount2
    case TARGET_NR_umount2:
        if (!(p = lock_user_string(arg1)))
            return -TARGET_EFAULT;
        ret = get_errno(umount2(p, arg2));
        unlock_user(p, arg1, 0);
        return ret;
#endif
    case TARGET_NR_ioctl:
        return do_ioctl(arg1, arg2, arg3);
#ifdef TARGET_NR_fcntl
    case TARGET_NR_fcntl:
        return do_fcntl(arg1, arg2, arg3);
#endif
    case TARGET_NR_setpgid:
        return get_errno(setpgid(arg1, arg2));
    case TARGET_NR_umask:
        return get_errno(umask(arg1));
    case TARGET_NR_chroot:
        if (!(p = lock_user_string(arg1)))
            return -TARGET_EFAULT;
        ret = get_errno(chroot(p));
        unlock_user(p, arg1, 0);
        return ret;
#ifdef TARGET_NR_dup2
    case TARGET_NR_dup2:
        ret = get_errno(dup2(arg1, arg2));
        if (ret >= 0) {
            fd_trans_dup(arg1, arg2);
        }
        return ret;
#endif
#if defined(CONFIG_DUP3) && defined(TARGET_NR_dup3)
    case TARGET_NR_dup3:
    {
        int host_flags;

        if ((arg3 & ~TARGET_O_CLOEXEC) != 0) {
            return -EINVAL;
        }
        host_flags = target_to_host_bitmask(arg3, fcntl_flags_tbl);
        ret = get_errno(dup3(arg1, arg2, host_flags));
        if (ret >= 0) {
            fd_trans_dup(arg1, arg2);
        }
        return ret;
    }
#endif
#ifdef TARGET_NR_getppid /* not on alpha */
    case TARGET_NR_getppid:
        return get_errno(getppid());
#endif
#ifdef TARGET_NR_getpgrp
    case TARGET_NR_getpgrp:
        return get_errno(getpgrp());
#endif
    case TARGET_NR_setsid:
        return get_errno(setsid());
#ifdef TARGET_NR_sigaction
    case TARGET_NR_sigaction:
        {
#if defined(TARGET_MIPS)
	    struct target_sigaction act, oact, *pact, *old_act;

	    if (arg2) {
                if (!lock_user_struct(VERIFY_READ, old_act, arg2, 1))
                    return -TARGET_EFAULT;
		act._sa_handler = old_act->_sa_handler;
		target_siginitset(&act.sa_mask, old_act->sa_mask.sig[0]);
		act.sa_flags = old_act->sa_flags;
		unlock_user_struct(old_act, arg2, 0);
		pact = &act;
	    } else {
		pact = NULL;
	    }

        ret = get_errno(do_sigaction(arg1, pact, &oact, 0));

	    if (!is_error(ret) && arg3) {
                if (!lock_user_struct(VERIFY_WRITE, old_act, arg3, 0))
                    return -TARGET_EFAULT;
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
                    return -TARGET_EFAULT;
                act._sa_handler = old_act->_sa_handler;
                target_siginitset(&act.sa_mask, old_act->sa_mask);
                act.sa_flags = old_act->sa_flags;
#ifdef TARGET_ARCH_HAS_SA_RESTORER
                act.sa_restorer = old_act->sa_restorer;
#endif
                unlock_user_struct(old_act, arg2, 0);
                pact = &act;
            } else {
                pact = NULL;
            }
            ret = get_errno(do_sigaction(arg1, pact, &oact, 0));
            if (!is_error(ret) && arg3) {
                if (!lock_user_struct(VERIFY_WRITE, old_act, arg3, 0))
                    return -TARGET_EFAULT;
                old_act->_sa_handler = oact._sa_handler;
                old_act->sa_mask = oact.sa_mask.sig[0];
                old_act->sa_flags = oact.sa_flags;
#ifdef TARGET_ARCH_HAS_SA_RESTORER
                old_act->sa_restorer = oact.sa_restorer;
#endif
                unlock_user_struct(old_act, arg3, 1);
            }
#endif
        }
        return ret;
#endif
    case TARGET_NR_rt_sigaction:
        {
            /*
             * For Alpha and SPARC this is a 5 argument syscall, with
             * a 'restorer' parameter which must be copied into the
             * sa_restorer field of the sigaction struct.
             * For Alpha that 'restorer' is arg5; for SPARC it is arg4,
             * and arg5 is the sigsetsize.
             */
#if defined(TARGET_ALPHA)
            target_ulong sigsetsize = arg4;
            target_ulong restorer = arg5;
#elif defined(TARGET_SPARC)
            target_ulong restorer = arg4;
            target_ulong sigsetsize = arg5;
#else
            target_ulong sigsetsize = arg4;
            target_ulong restorer = 0;
#endif
            struct target_sigaction *act = NULL;
            struct target_sigaction *oact = NULL;

            if (sigsetsize != sizeof(target_sigset_t)) {
                return -TARGET_EINVAL;
            }
            if (arg2 && !lock_user_struct(VERIFY_READ, act, arg2, 1)) {
                return -TARGET_EFAULT;
            }
            if (arg3 && !lock_user_struct(VERIFY_WRITE, oact, arg3, 0)) {
                ret = -TARGET_EFAULT;
            } else {
                ret = get_errno(do_sigaction(arg1, act, oact, restorer));
                if (oact) {
                    unlock_user_struct(oact, arg3, 1);
                }
            }
            if (act) {
                unlock_user_struct(act, arg2, 0);
            }
        }
        return ret;
#ifdef TARGET_NR_sgetmask /* not on alpha */
    case TARGET_NR_sgetmask:
        {
            sigset_t cur_set;
            abi_ulong target_set;
            ret = do_sigprocmask(0, NULL, &cur_set);
            if (!ret) {
                host_to_target_old_sigset(&target_set, &cur_set);
                ret = target_set;
            }
        }
        return ret;
#endif
#ifdef TARGET_NR_ssetmask /* not on alpha */
    case TARGET_NR_ssetmask:
        {
            sigset_t set, oset;
            abi_ulong target_set = arg1;
            target_to_host_old_sigset(&set, &target_set);
            ret = do_sigprocmask(SIG_SETMASK, &set, &oset);
            if (!ret) {
                host_to_target_old_sigset(&target_set, &oset);
                ret = target_set;
            }
        }
        return ret;
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
                return -TARGET_EINVAL;
            }
            mask = arg2;
            target_to_host_old_sigset(&set, &mask);

            ret = do_sigprocmask(how, &set, &oldset);
            if (!is_error(ret)) {
                host_to_target_old_sigset(&mask, &oldset);
                ret = mask;
                cpu_env->ir[IR_V0] = 0; /* force no error */
            }
#else
            sigset_t set, oldset, *set_ptr;
            int how;

            if (arg2) {
                p = lock_user(VERIFY_READ, arg2, sizeof(target_sigset_t), 1);
                if (!p) {
                    return -TARGET_EFAULT;
                }
                target_to_host_old_sigset(&set, p);
                unlock_user(p, arg2, 0);
                set_ptr = &set;
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
                    return -TARGET_EINVAL;
                }
            } else {
                how = 0;
                set_ptr = NULL;
            }
            ret = do_sigprocmask(how, set_ptr, &oldset);
            if (!is_error(ret) && arg3) {
                if (!(p = lock_user(VERIFY_WRITE, arg3, sizeof(target_sigset_t), 0)))
                    return -TARGET_EFAULT;
                host_to_target_old_sigset(p, &oldset);
                unlock_user(p, arg3, sizeof(target_sigset_t));
            }
#endif
        }
        return ret;
#endif
    case TARGET_NR_rt_sigprocmask:
        {
            int how = arg1;
            sigset_t set, oldset, *set_ptr;

            if (arg4 != sizeof(target_sigset_t)) {
                return -TARGET_EINVAL;
            }

            if (arg2) {
                p = lock_user(VERIFY_READ, arg2, sizeof(target_sigset_t), 1);
                if (!p) {
                    return -TARGET_EFAULT;
                }
                target_to_host_sigset(&set, p);
                unlock_user(p, arg2, 0);
                set_ptr = &set;
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
                    return -TARGET_EINVAL;
                }
            } else {
                how = 0;
                set_ptr = NULL;
            }
            ret = do_sigprocmask(how, set_ptr, &oldset);
            if (!is_error(ret) && arg3) {
                if (!(p = lock_user(VERIFY_WRITE, arg3, sizeof(target_sigset_t), 0)))
                    return -TARGET_EFAULT;
                host_to_target_sigset(p, &oldset);
                unlock_user(p, arg3, sizeof(target_sigset_t));
            }
        }
        return ret;
#ifdef TARGET_NR_sigpending
    case TARGET_NR_sigpending:
        {
            sigset_t set;
            ret = get_errno(sigpending(&set));
            if (!is_error(ret)) {
                if (!(p = lock_user(VERIFY_WRITE, arg1, sizeof(target_sigset_t), 0)))
                    return -TARGET_EFAULT;
                host_to_target_old_sigset(p, &set);
                unlock_user(p, arg1, sizeof(target_sigset_t));
            }
        }
        return ret;
#endif
    case TARGET_NR_rt_sigpending:
        {
            sigset_t set;

            /* Yes, this check is >, not != like most. We follow the kernel's
             * logic and it does it like this because it implements
             * NR_sigpending through the same code path, and in that case
             * the old_sigset_t is smaller in size.
             */
            if (arg2 > sizeof(target_sigset_t)) {
                return -TARGET_EINVAL;
            }

            ret = get_errno(sigpending(&set));
            if (!is_error(ret)) {
                if (!(p = lock_user(VERIFY_WRITE, arg1, sizeof(target_sigset_t), 0)))
                    return -TARGET_EFAULT;
                host_to_target_sigset(p, &set);
                unlock_user(p, arg1, sizeof(target_sigset_t));
            }
        }
        return ret;
#ifdef TARGET_NR_sigsuspend
    case TARGET_NR_sigsuspend:
        {
            sigset_t *set;

#if defined(TARGET_ALPHA)
            TaskState *ts = cpu->opaque;
            /* target_to_host_old_sigset will bswap back */
            abi_ulong mask = tswapal(arg1);
            set = &ts->sigsuspend_mask;
            target_to_host_old_sigset(set, &mask);
#else
            ret = process_sigsuspend_mask(&set, arg1, sizeof(target_sigset_t));
            if (ret != 0) {
                return ret;
            }
#endif
            ret = get_errno(safe_rt_sigsuspend(set, SIGSET_T_SIZE));
            finish_sigsuspend_mask(ret);
        }
        return ret;
#endif
    case TARGET_NR_rt_sigsuspend:
        {
            sigset_t *set;

            ret = process_sigsuspend_mask(&set, arg1, arg2);
            if (ret != 0) {
                return ret;
            }
            ret = get_errno(safe_rt_sigsuspend(set, SIGSET_T_SIZE));
            finish_sigsuspend_mask(ret);
        }
        return ret;
#ifdef TARGET_NR_rt_sigtimedwait
    case TARGET_NR_rt_sigtimedwait:
        {
            sigset_t set;
            struct timespec uts, *puts;
            siginfo_t uinfo;

            if (arg4 != sizeof(target_sigset_t)) {
                return -TARGET_EINVAL;
            }

            if (!(p = lock_user(VERIFY_READ, arg1, sizeof(target_sigset_t), 1)))
                return -TARGET_EFAULT;
            target_to_host_sigset(&set, p);
            unlock_user(p, arg1, 0);
            if (arg3) {
                puts = &uts;
                if (target_to_host_timespec(puts, arg3)) {
                    return -TARGET_EFAULT;
                }
            } else {
                puts = NULL;
            }
            ret = get_errno(safe_rt_sigtimedwait(&set, &uinfo, puts,
                                                 SIGSET_T_SIZE));
            if (!is_error(ret)) {
                if (arg2) {
                    p = lock_user(VERIFY_WRITE, arg2, sizeof(target_siginfo_t),
                                  0);
                    if (!p) {
                        return -TARGET_EFAULT;
                    }
                    host_to_target_siginfo(p, &uinfo);
                    unlock_user(p, arg2, sizeof(target_siginfo_t));
                }
                ret = host_to_target_signal(ret);
            }
        }
        return ret;
#endif
#ifdef TARGET_NR_rt_sigtimedwait_time64
    case TARGET_NR_rt_sigtimedwait_time64:
        {
            sigset_t set;
            struct timespec uts, *puts;
            siginfo_t uinfo;

            if (arg4 != sizeof(target_sigset_t)) {
                return -TARGET_EINVAL;
            }

            p = lock_user(VERIFY_READ, arg1, sizeof(target_sigset_t), 1);
            if (!p) {
                return -TARGET_EFAULT;
            }
            target_to_host_sigset(&set, p);
            unlock_user(p, arg1, 0);
            if (arg3) {
                puts = &uts;
                if (target_to_host_timespec64(puts, arg3)) {
                    return -TARGET_EFAULT;
                }
            } else {
                puts = NULL;
            }
            ret = get_errno(safe_rt_sigtimedwait(&set, &uinfo, puts,
                                                 SIGSET_T_SIZE));
            if (!is_error(ret)) {
                if (arg2) {
                    p = lock_user(VERIFY_WRITE, arg2,
                                  sizeof(target_siginfo_t), 0);
                    if (!p) {
                        return -TARGET_EFAULT;
                    }
                    host_to_target_siginfo(p, &uinfo);
                    unlock_user(p, arg2, sizeof(target_siginfo_t));
                }
                ret = host_to_target_signal(ret);
            }
        }
        return ret;
#endif
    case TARGET_NR_rt_sigqueueinfo:
        {
            siginfo_t uinfo;

            p = lock_user(VERIFY_READ, arg3, sizeof(target_siginfo_t), 1);
            if (!p) {
                return -TARGET_EFAULT;
            }
            target_to_host_siginfo(&uinfo, p);
            unlock_user(p, arg3, 0);
            ret = get_errno(sys_rt_sigqueueinfo(arg1, target_to_host_signal(arg2), &uinfo));
        }
        return ret;
    case TARGET_NR_rt_tgsigqueueinfo:
        {
            siginfo_t uinfo;

            p = lock_user(VERIFY_READ, arg4, sizeof(target_siginfo_t), 1);
            if (!p) {
                return -TARGET_EFAULT;
            }
            target_to_host_siginfo(&uinfo, p);
            unlock_user(p, arg4, 0);
            ret = get_errno(sys_rt_tgsigqueueinfo(arg1, arg2, target_to_host_signal(arg3), &uinfo));
        }
        return ret;
#ifdef TARGET_NR_sigreturn
    case TARGET_NR_sigreturn:
        if (block_signals()) {
            return -QEMU_ERESTARTSYS;
        }
        return do_sigreturn(cpu_env);
#endif
    case TARGET_NR_rt_sigreturn:
        if (block_signals()) {
            return -QEMU_ERESTARTSYS;
        }
        return do_rt_sigreturn(cpu_env);
    case TARGET_NR_sethostname:
        if (!(p = lock_user_string(arg1)))
            return -TARGET_EFAULT;
        ret = get_errno(sethostname(p, arg2));
        unlock_user(p, arg1, 0);
        return ret;
#ifdef TARGET_NR_setrlimit
    case TARGET_NR_setrlimit:
        {
            int resource = target_to_host_resource(arg1);
            struct target_rlimit *target_rlim;
            struct rlimit rlim;
            if (!lock_user_struct(VERIFY_READ, target_rlim, arg2, 1))
                return -TARGET_EFAULT;
            rlim.rlim_cur = target_to_host_rlim(target_rlim->rlim_cur);
            rlim.rlim_max = target_to_host_rlim(target_rlim->rlim_max);
            unlock_user_struct(target_rlim, arg2, 0);
            /*
             * If we just passed through resource limit settings for memory then
             * they would also apply to QEMU's own allocations, and QEMU will
             * crash or hang or die if its allocations fail. Ideally we would
             * track the guest allocations in QEMU and apply the limits ourselves.
             * For now, just tell the guest the call succeeded but don't actually
             * limit anything.
             */
            if (resource != RLIMIT_AS &&
                resource != RLIMIT_DATA &&
                resource != RLIMIT_STACK) {
                return get_errno(setrlimit(resource, &rlim));
            } else {
                return 0;
            }
        }
#endif
#ifdef TARGET_NR_getrlimit
    case TARGET_NR_getrlimit:
        {
            int resource = target_to_host_resource(arg1);
            struct target_rlimit *target_rlim;
            struct rlimit rlim;

            ret = get_errno(getrlimit(resource, &rlim));
            if (!is_error(ret)) {
                if (!lock_user_struct(VERIFY_WRITE, target_rlim, arg2, 0))
                    return -TARGET_EFAULT;
                target_rlim->rlim_cur = host_to_target_rlim(rlim.rlim_cur);
                target_rlim->rlim_max = host_to_target_rlim(rlim.rlim_max);
                unlock_user_struct(target_rlim, arg2, 1);
            }
        }
        return ret;
#endif
    case TARGET_NR_getrusage:
        {
            struct rusage rusage;
            ret = get_errno(getrusage(arg1, &rusage));
            if (!is_error(ret)) {
                ret = host_to_target_rusage(arg2, &rusage);
            }
        }
        return ret;
#if defined(TARGET_NR_gettimeofday)
    case TARGET_NR_gettimeofday:
        {
            struct timeval tv;
            struct timezone tz;

            ret = get_errno(gettimeofday(&tv, &tz));
            if (!is_error(ret)) {
                if (arg1 && copy_to_user_timeval(arg1, &tv)) {
                    return -TARGET_EFAULT;
                }
                if (arg2 && copy_to_user_timezone(arg2, &tz)) {
                    return -TARGET_EFAULT;
                }
            }
        }
        return ret;
#endif
#if defined(TARGET_NR_settimeofday)
    case TARGET_NR_settimeofday:
        {
            struct timeval tv, *ptv = NULL;
            struct timezone tz, *ptz = NULL;

            if (arg1) {
                if (copy_from_user_timeval(&tv, arg1)) {
                    return -TARGET_EFAULT;
                }
                ptv = &tv;
            }

            if (arg2) {
                if (copy_from_user_timezone(&tz, arg2)) {
                    return -TARGET_EFAULT;
                }
                ptz = &tz;
            }

            return get_errno(settimeofday(ptv, ptz));
        }
#endif
#if defined(TARGET_NR_select)
    case TARGET_NR_select:
#if defined(TARGET_WANT_NI_OLD_SELECT)
        /* some architectures used to have old_select here
         * but now ENOSYS it.
         */
        ret = -TARGET_ENOSYS;
#elif defined(TARGET_WANT_OLD_SYS_SELECT)
        ret = do_old_select(arg1);
#else
        ret = do_select(arg1, arg2, arg3, arg4, arg5);
#endif
        return ret;
#endif
#ifdef TARGET_NR_pselect6
    case TARGET_NR_pselect6:
        return do_pselect6(arg1, arg2, arg3, arg4, arg5, arg6, false);
#endif
#ifdef TARGET_NR_pselect6_time64
    case TARGET_NR_pselect6_time64:
        return do_pselect6(arg1, arg2, arg3, arg4, arg5, arg6, true);
#endif
#ifdef TARGET_NR_symlink
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
        return ret;
#endif
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
        return ret;
#endif
#ifdef TARGET_NR_readlink
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
        return ret;
#endif
#if defined(TARGET_NR_readlinkat)
    case TARGET_NR_readlinkat:
        {
            void *p2;
            p  = lock_user_string(arg2);
            p2 = lock_user(VERIFY_WRITE, arg3, arg4, 0);
            if (!p || !p2) {
                ret = -TARGET_EFAULT;
            } else if (!arg4) {
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
                    ret = MIN(strlen(real), arg4);
                    /* We cannot NUL terminate the string. */
                    memcpy(p2, real, ret);
                }
            } else {
                ret = get_errno(readlinkat(arg1, path(p), p2, arg4));
            }
            unlock_user(p2, arg3, ret);
            unlock_user(p, arg2, 0);
        }
        return ret;
#endif
#ifdef TARGET_NR_swapon
    case TARGET_NR_swapon:
        if (!(p = lock_user_string(arg1)))
            return -TARGET_EFAULT;
        ret = get_errno(swapon(p, arg2));
        unlock_user(p, arg1, 0);
        return ret;
#endif
    case TARGET_NR_reboot:
        if (arg3 == LINUX_REBOOT_CMD_RESTART2) {
           /* arg4 must be ignored in all other cases */
           p = lock_user_string(arg4);
           if (!p) {
               return -TARGET_EFAULT;
           }
           ret = get_errno(reboot(arg1, arg2, arg3, p));
           unlock_user(p, arg4, 0);
        } else {
           ret = get_errno(reboot(arg1, arg2, arg3, NULL));
        }
        return ret;
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
                return -TARGET_EFAULT;
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
        /* mmap pointers are always untagged */
        ret = get_errno(target_mmap(arg1, arg2, arg3,
                                    target_to_host_bitmask(arg4, mmap_flags_tbl),
                                    arg5,
                                    arg6));
#endif
        return ret;
#endif
#ifdef TARGET_NR_mmap2
    case TARGET_NR_mmap2:
#ifndef MMAP_SHIFT
#define MMAP_SHIFT 12
#endif
        ret = target_mmap(arg1, arg2, arg3,
                          target_to_host_bitmask(arg4, mmap_flags_tbl),
                          arg5, arg6 << MMAP_SHIFT);
        return get_errno(ret);
#endif
    case TARGET_NR_munmap:
        arg1 = cpu_untagged_addr(cpu, arg1);
        return get_errno(target_munmap(arg1, arg2));
    case TARGET_NR_mprotect:
        arg1 = cpu_untagged_addr(cpu, arg1);
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
        return get_errno(target_mprotect(arg1, arg2, arg3));
#ifdef TARGET_NR_mremap
    case TARGET_NR_mremap:
        arg1 = cpu_untagged_addr(cpu, arg1);
        /* mremap new_addr (arg5) is always untagged */
        return get_errno(target_mremap(arg1, arg2, arg3, arg4, arg5));
#endif
        /* ??? msync/mlock/munlock are broken for softmmu.  */
#ifdef TARGET_NR_msync
    case TARGET_NR_msync:
        return get_errno(msync(g2h(cpu, arg1), arg2, arg3));
#endif
#ifdef TARGET_NR_mlock
    case TARGET_NR_mlock:
        return get_errno(mlock(g2h(cpu, arg1), arg2));
#endif
#ifdef TARGET_NR_munlock
    case TARGET_NR_munlock:
        return get_errno(munlock(g2h(cpu, arg1), arg2));
#endif
#ifdef TARGET_NR_mlockall
    case TARGET_NR_mlockall:
        return get_errno(mlockall(target_to_host_mlockall_arg(arg1)));
#endif
#ifdef TARGET_NR_munlockall
    case TARGET_NR_munlockall:
        return get_errno(munlockall());
#endif
#ifdef TARGET_NR_truncate
    case TARGET_NR_truncate:
        if (!(p = lock_user_string(arg1)))
            return -TARGET_EFAULT;
        ret = get_errno(truncate(p, arg2));
        unlock_user(p, arg1, 0);
        return ret;
#endif
#ifdef TARGET_NR_ftruncate
    case TARGET_NR_ftruncate:
        return get_errno(ftruncate(arg1, arg2));
#endif
    case TARGET_NR_fchmod:
        return get_errno(fchmod(arg1, arg2));
#if defined(TARGET_NR_fchmodat)
    case TARGET_NR_fchmodat:
        if (!(p = lock_user_string(arg2)))
            return -TARGET_EFAULT;
        ret = get_errno(fchmodat(arg1, p, arg3, 0));
        unlock_user(p, arg2, 0);
        return ret;
#endif
    case TARGET_NR_getpriority:
        /* Note that negative values are valid for getpriority, so we must
           differentiate based on errno settings.  */
        errno = 0;
        ret = getpriority(arg1, arg2);
        if (ret == -1 && errno != 0) {
            return -host_to_target_errno(errno);
        }
#ifdef TARGET_ALPHA
        /* Return value is the unbiased priority.  Signal no error.  */
        cpu_env->ir[IR_V0] = 0;
#else
        /* Return value is a biased priority to avoid negative numbers.  */
        ret = 20 - ret;
#endif
        return ret;
    case TARGET_NR_setpriority:
        return get_errno(setpriority(arg1, arg2, arg3));
#ifdef TARGET_NR_statfs
    case TARGET_NR_statfs:
        if (!(p = lock_user_string(arg1))) {
            return -TARGET_EFAULT;
        }
        ret = get_errno(statfs(path(p), &stfs));
        unlock_user(p, arg1, 0);
    convert_statfs:
        if (!is_error(ret)) {
            struct target_statfs *target_stfs;

            if (!lock_user_struct(VERIFY_WRITE, target_stfs, arg2, 0))
                return -TARGET_EFAULT;
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
#ifdef _STATFS_F_FLAGS
            __put_user(stfs.f_flags, &target_stfs->f_flags);
#else
            __put_user(0, &target_stfs->f_flags);
#endif
            memset(target_stfs->f_spare, 0, sizeof(target_stfs->f_spare));
            unlock_user_struct(target_stfs, arg2, 1);
        }
        return ret;
#endif
#ifdef TARGET_NR_fstatfs
    case TARGET_NR_fstatfs:
        ret = get_errno(fstatfs(arg1, &stfs));
        goto convert_statfs;
#endif
#ifdef TARGET_NR_statfs64
    case TARGET_NR_statfs64:
        if (!(p = lock_user_string(arg1))) {
            return -TARGET_EFAULT;
        }
        ret = get_errno(statfs(path(p), &stfs));
        unlock_user(p, arg1, 0);
    convert_statfs64:
        if (!is_error(ret)) {
            struct target_statfs64 *target_stfs;

            if (!lock_user_struct(VERIFY_WRITE, target_stfs, arg3, 0))
                return -TARGET_EFAULT;
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
#ifdef _STATFS_F_FLAGS
            __put_user(stfs.f_flags, &target_stfs->f_flags);
#else
            __put_user(0, &target_stfs->f_flags);
#endif
            memset(target_stfs->f_spare, 0, sizeof(target_stfs->f_spare));
            unlock_user_struct(target_stfs, arg3, 1);
        }
        return ret;
    case TARGET_NR_fstatfs64:
        ret = get_errno(fstatfs(arg1, &stfs));
        goto convert_statfs64;
#endif
#ifdef TARGET_NR_socketcall
    case TARGET_NR_socketcall:
        return do_socketcall(arg1, arg2);
#endif
#ifdef TARGET_NR_accept
    case TARGET_NR_accept:
        return do_accept4(arg1, arg2, arg3, 0);
#endif
#ifdef TARGET_NR_accept4
    case TARGET_NR_accept4:
        return do_accept4(arg1, arg2, arg3, arg4);
#endif
#ifdef TARGET_NR_bind
    case TARGET_NR_bind:
        return do_bind(arg1, arg2, arg3);
#endif
#ifdef TARGET_NR_connect
    case TARGET_NR_connect:
        return do_connect(arg1, arg2, arg3);
#endif
#ifdef TARGET_NR_getpeername
    case TARGET_NR_getpeername:
        return do_getpeername(arg1, arg2, arg3);
#endif
#ifdef TARGET_NR_getsockname
    case TARGET_NR_getsockname:
        return do_getsockname(arg1, arg2, arg3);
#endif
#ifdef TARGET_NR_getsockopt
    case TARGET_NR_getsockopt:
        return do_getsockopt(arg1, arg2, arg3, arg4, arg5);
#endif
#ifdef TARGET_NR_listen
    case TARGET_NR_listen:
        return get_errno(listen(arg1, arg2));
#endif
#ifdef TARGET_NR_recv
    case TARGET_NR_recv:
        return do_recvfrom(arg1, arg2, arg3, arg4, 0, 0);
#endif
#ifdef TARGET_NR_recvfrom
    case TARGET_NR_recvfrom:
        return do_recvfrom(arg1, arg2, arg3, arg4, arg5, arg6);
#endif
#ifdef TARGET_NR_recvmsg
    case TARGET_NR_recvmsg:
        return do_sendrecvmsg(arg1, arg2, arg3, 0);
#endif
#ifdef TARGET_NR_send
    case TARGET_NR_send:
        return do_sendto(arg1, arg2, arg3, arg4, 0, 0);
#endif
#ifdef TARGET_NR_sendmsg
    case TARGET_NR_sendmsg:
        return do_sendrecvmsg(arg1, arg2, arg3, 1);
#endif
#ifdef TARGET_NR_sendmmsg
    case TARGET_NR_sendmmsg:
        return do_sendrecvmmsg(arg1, arg2, arg3, arg4, 1);
#endif
#ifdef TARGET_NR_recvmmsg
    case TARGET_NR_recvmmsg:
        return do_sendrecvmmsg(arg1, arg2, arg3, arg4, 0);
#endif
#ifdef TARGET_NR_sendto
    case TARGET_NR_sendto:
        return do_sendto(arg1, arg2, arg3, arg4, arg5, arg6);
#endif
#ifdef TARGET_NR_shutdown
    case TARGET_NR_shutdown:
        return get_errno(shutdown(arg1, arg2));
#endif
#if defined(TARGET_NR_getrandom) && defined(__NR_getrandom)
    case TARGET_NR_getrandom:
        p = lock_user(VERIFY_WRITE, arg1, arg2, 0);
        if (!p) {
            return -TARGET_EFAULT;
        }
        ret = get_errno(getrandom(p, arg2, arg3));
        unlock_user(p, arg1, ret);
        return ret;
#endif
#ifdef TARGET_NR_socket
    case TARGET_NR_socket:
        return do_socket(arg1, arg2, arg3);
#endif
#ifdef TARGET_NR_socketpair
    case TARGET_NR_socketpair:
        return do_socketpair(arg1, arg2, arg3, arg4);
#endif
#ifdef TARGET_NR_setsockopt
    case TARGET_NR_setsockopt:
        return do_setsockopt(arg1, arg2, arg3, arg4, (socklen_t) arg5);
#endif
#if defined(TARGET_NR_syslog)
    case TARGET_NR_syslog:
        {
            int len = arg2;

            switch (arg1) {
            case TARGET_SYSLOG_ACTION_CLOSE:         /* Close log */
            case TARGET_SYSLOG_ACTION_OPEN:          /* Open log */
            case TARGET_SYSLOG_ACTION_CLEAR:         /* Clear ring buffer */
            case TARGET_SYSLOG_ACTION_CONSOLE_OFF:   /* Disable logging */
            case TARGET_SYSLOG_ACTION_CONSOLE_ON:    /* Enable logging */
            case TARGET_SYSLOG_ACTION_CONSOLE_LEVEL: /* Set messages level */
            case TARGET_SYSLOG_ACTION_SIZE_UNREAD:   /* Number of chars */
            case TARGET_SYSLOG_ACTION_SIZE_BUFFER:   /* Size of the buffer */
                return get_errno(sys_syslog((int)arg1, NULL, (int)arg3));
            case TARGET_SYSLOG_ACTION_READ:          /* Read from log */
            case TARGET_SYSLOG_ACTION_READ_CLEAR:    /* Read/clear msgs */
            case TARGET_SYSLOG_ACTION_READ_ALL:      /* Read last messages */
                {
                    if (len < 0) {
                        return -TARGET_EINVAL;
                    }
                    if (len == 0) {
                        return 0;
                    }
                    p = lock_user(VERIFY_WRITE, arg2, arg3, 0);
                    if (!p) {
                        return -TARGET_EFAULT;
                    }
                    ret = get_errno(sys_syslog((int)arg1, p, (int)arg3));
                    unlock_user(p, arg2, arg3);
                }
                return ret;
            default:
                return -TARGET_EINVAL;
            }
        }
        break;
#endif
    case TARGET_NR_setitimer:
        {
            struct itimerval value, ovalue, *pvalue;

            if (arg2) {
                pvalue = &value;
                if (copy_from_user_timeval(&pvalue->it_interval, arg2)
                    || copy_from_user_timeval(&pvalue->it_value,
                                              arg2 + sizeof(struct target_timeval)))
                    return -TARGET_EFAULT;
            } else {
                pvalue = NULL;
            }
            ret = get_errno(setitimer(arg1, pvalue, &ovalue));
            if (!is_error(ret) && arg3) {
                if (copy_to_user_timeval(arg3,
                                         &ovalue.it_interval)
                    || copy_to_user_timeval(arg3 + sizeof(struct target_timeval),
                                            &ovalue.it_value))
                    return -TARGET_EFAULT;
            }
        }
        return ret;
    case TARGET_NR_getitimer:
        {
            struct itimerval value;

            ret = get_errno(getitimer(arg1, &value));
            if (!is_error(ret) && arg2) {
                if (copy_to_user_timeval(arg2,
                                         &value.it_interval)
                    || copy_to_user_timeval(arg2 + sizeof(struct target_timeval),
                                            &value.it_value))
                    return -TARGET_EFAULT;
            }
        }
        return ret;
#ifdef TARGET_NR_stat
    case TARGET_NR_stat:
        if (!(p = lock_user_string(arg1))) {
            return -TARGET_EFAULT;
        }
        ret = get_errno(stat(path(p), &st));
        unlock_user(p, arg1, 0);
        goto do_stat;
#endif
#ifdef TARGET_NR_lstat
    case TARGET_NR_lstat:
        if (!(p = lock_user_string(arg1))) {
            return -TARGET_EFAULT;
        }
        ret = get_errno(lstat(path(p), &st));
        unlock_user(p, arg1, 0);
        goto do_stat;
#endif
#ifdef TARGET_NR_fstat
    case TARGET_NR_fstat:
        {
            ret = get_errno(fstat(arg1, &st));
#if defined(TARGET_NR_stat) || defined(TARGET_NR_lstat)
        do_stat:
#endif
            if (!is_error(ret)) {
                struct target_stat *target_st;

                if (!lock_user_struct(VERIFY_WRITE, target_st, arg2, 0))
                    return -TARGET_EFAULT;
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
#if defined(HAVE_STRUCT_STAT_ST_ATIM) && defined(TARGET_STAT_HAVE_NSEC)
                __put_user(st.st_atim.tv_nsec,
                           &target_st->target_st_atime_nsec);
                __put_user(st.st_mtim.tv_nsec,
                           &target_st->target_st_mtime_nsec);
                __put_user(st.st_ctim.tv_nsec,
                           &target_st->target_st_ctime_nsec);
#endif
                unlock_user_struct(target_st, arg2, 1);
            }
        }
        return ret;
#endif
    case TARGET_NR_vhangup:
        return get_errno(vhangup());
#ifdef TARGET_NR_syscall
    case TARGET_NR_syscall:
        return do_syscall(cpu_env, arg1 & 0xffff, arg2, arg3, arg4, arg5,
                          arg6, arg7, arg8, 0);
#endif
#if defined(TARGET_NR_wait4)
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
            ret = get_errno(safe_wait4(arg1, &status, arg3, rusage_ptr));
            if (!is_error(ret)) {
                if (status_ptr && ret) {
                    status = host_to_target_waitstatus(status);
                    if (put_user_s32(status, status_ptr))
                        return -TARGET_EFAULT;
                }
                if (target_rusage) {
                    rusage_err = host_to_target_rusage(target_rusage, &rusage);
                    if (rusage_err) {
                        ret = rusage_err;
                    }
                }
            }
        }
        return ret;
#endif
#ifdef TARGET_NR_swapoff
    case TARGET_NR_swapoff:
        if (!(p = lock_user_string(arg1)))
            return -TARGET_EFAULT;
        ret = get_errno(swapoff(p));
        unlock_user(p, arg1, 0);
        return ret;
#endif
    case TARGET_NR_sysinfo:
        {
            struct target_sysinfo *target_value;
            struct sysinfo value;
            ret = get_errno(sysinfo(&value));
            if (!is_error(ret) && arg1)
            {
                if (!lock_user_struct(VERIFY_WRITE, target_value, arg1, 0))
                    return -TARGET_EFAULT;
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
        return ret;
#ifdef TARGET_NR_ipc
    case TARGET_NR_ipc:
        return do_ipc(cpu_env, arg1, arg2, arg3, arg4, arg5, arg6);
#endif
#ifdef TARGET_NR_semget
    case TARGET_NR_semget:
        return get_errno(semget(arg1, arg2, arg3));
#endif
#ifdef TARGET_NR_semop
    case TARGET_NR_semop:
        return do_semtimedop(arg1, arg2, arg3, 0, false);
#endif
#ifdef TARGET_NR_semtimedop
    case TARGET_NR_semtimedop:
        return do_semtimedop(arg1, arg2, arg3, arg4, false);
#endif
#ifdef TARGET_NR_semtimedop_time64
    case TARGET_NR_semtimedop_time64:
        return do_semtimedop(arg1, arg2, arg3, arg4, true);
#endif
#ifdef TARGET_NR_semctl
    case TARGET_NR_semctl:
        return do_semctl(arg1, arg2, arg3, arg4);
#endif
#ifdef TARGET_NR_msgctl
    case TARGET_NR_msgctl:
        return do_msgctl(arg1, arg2, arg3);
#endif
#ifdef TARGET_NR_msgget
    case TARGET_NR_msgget:
        return get_errno(msgget(arg1, arg2));
#endif
#ifdef TARGET_NR_msgrcv
    case TARGET_NR_msgrcv:
        return do_msgrcv(arg1, arg2, arg3, arg4, arg5);
#endif
#ifdef TARGET_NR_msgsnd
    case TARGET_NR_msgsnd:
        return do_msgsnd(arg1, arg2, arg3, arg4);
#endif
#ifdef TARGET_NR_shmget
    case TARGET_NR_shmget:
        return get_errno(shmget(arg1, arg2, arg3));
#endif
#ifdef TARGET_NR_shmctl
    case TARGET_NR_shmctl:
        return do_shmctl(arg1, arg2, arg3);
#endif
#ifdef TARGET_NR_shmat
    case TARGET_NR_shmat:
        return do_shmat(cpu_env, arg1, arg2, arg3);
#endif
#ifdef TARGET_NR_shmdt
    case TARGET_NR_shmdt:
        return do_shmdt(arg1);
#endif
    case TARGET_NR_fsync:
        return get_errno(fsync(arg1));
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
        return ret;
#ifdef __NR_exit_group
        /* new thread calls */
    case TARGET_NR_exit_group:
        preexit_cleanup(cpu_env, arg1);
        return get_errno(exit_group(arg1));
#endif
    case TARGET_NR_setdomainname:
        if (!(p = lock_user_string(arg1)))
            return -TARGET_EFAULT;
        ret = get_errno(setdomainname(p, arg2));
        unlock_user(p, arg1, 0);
        return ret;
    case TARGET_NR_uname:
        /* no need to transcode because we use the linux syscall */
        {
            struct new_utsname * buf;

            if (!lock_user_struct(VERIFY_WRITE, buf, arg1, 0))
                return -TARGET_EFAULT;
            ret = get_errno(sys_uname(buf));
            if (!is_error(ret)) {
                /* Overwrite the native machine name with whatever is being
                   emulated. */
                g_strlcpy(buf->machine, cpu_to_uname_machine(cpu_env),
                          sizeof(buf->machine));
                /* Allow the user to override the reported release.  */
                if (qemu_uname_release && *qemu_uname_release) {
                    g_strlcpy(buf->release, qemu_uname_release,
                              sizeof(buf->release));
                }
            }
            unlock_user_struct(buf, arg1, 1);
        }
        return ret;
#ifdef TARGET_I386
    case TARGET_NR_modify_ldt:
        return do_modify_ldt(cpu_env, arg1, arg2, arg3);
#if !defined(TARGET_X86_64)
    case TARGET_NR_vm86:
        return do_vm86(cpu_env, arg1, arg2);
#endif
#endif
#if defined(TARGET_NR_adjtimex)
    case TARGET_NR_adjtimex:
        {
            struct timex host_buf;

            if (target_to_host_timex(&host_buf, arg1) != 0) {
                return -TARGET_EFAULT;
            }
            ret = get_errno(adjtimex(&host_buf));
            if (!is_error(ret)) {
                if (host_to_target_timex(arg1, &host_buf) != 0) {
                    return -TARGET_EFAULT;
                }
            }
        }
        return ret;
#endif
#if defined(TARGET_NR_clock_adjtime) && defined(CONFIG_CLOCK_ADJTIME)
    case TARGET_NR_clock_adjtime:
        {
            struct timex htx, *phtx = &htx;

            if (target_to_host_timex(phtx, arg2) != 0) {
                return -TARGET_EFAULT;
            }
            ret = get_errno(clock_adjtime(arg1, phtx));
            if (!is_error(ret) && phtx) {
                if (host_to_target_timex(arg2, phtx) != 0) {
                    return -TARGET_EFAULT;
                }
            }
        }
        return ret;
#endif
#if defined(TARGET_NR_clock_adjtime64) && defined(CONFIG_CLOCK_ADJTIME)
    case TARGET_NR_clock_adjtime64:
        {
            struct timex htx;

            if (target_to_host_timex64(&htx, arg2) != 0) {
                return -TARGET_EFAULT;
            }
            ret = get_errno(clock_adjtime(arg1, &htx));
            if (!is_error(ret) && host_to_target_timex64(arg2, &htx)) {
                    return -TARGET_EFAULT;
            }
        }
        return ret;
#endif
    case TARGET_NR_getpgid:
        return get_errno(getpgid(arg1));
    case TARGET_NR_fchdir:
        return get_errno(fchdir(arg1));
    case TARGET_NR_personality:
        return get_errno(personality(arg1));
#ifdef TARGET_NR__llseek /* Not on alpha */
    case TARGET_NR__llseek:
        {
            int64_t res;
#if !defined(__NR_llseek)
            res = lseek(arg1, ((uint64_t)arg2 << 32) | (abi_ulong)arg3, arg5);
            if (res == -1) {
                ret = get_errno(res);
            } else {
                ret = 0;
            }
#else
            ret = get_errno(_llseek(arg1, arg2, arg3, &res, arg5));
#endif
            if ((ret == 0) && put_user_s64(res, arg4)) {
                return -TARGET_EFAULT;
            }
        }
        return ret;
#endif
#ifdef TARGET_NR_getdents
    case TARGET_NR_getdents:
        return do_getdents(arg1, arg2, arg3);
#endif /* TARGET_NR_getdents */
#if defined(TARGET_NR_getdents64) && defined(__NR_getdents64)
    case TARGET_NR_getdents64:
        return do_getdents64(arg1, arg2, arg3);
#endif /* TARGET_NR_getdents64 */
#if defined(TARGET_NR__newselect)
    case TARGET_NR__newselect:
        return do_select(arg1, arg2, arg3, arg4, arg5);
#endif
#ifdef TARGET_NR_poll
    case TARGET_NR_poll:
        return do_ppoll(arg1, arg2, arg3, arg4, arg5, false, false);
#endif
#ifdef TARGET_NR_ppoll
    case TARGET_NR_ppoll:
        return do_ppoll(arg1, arg2, arg3, arg4, arg5, true, false);
#endif
#ifdef TARGET_NR_ppoll_time64
    case TARGET_NR_ppoll_time64:
        return do_ppoll(arg1, arg2, arg3, arg4, arg5, true, true);
#endif
    case TARGET_NR_flock:
        /* NOTE: the flock constant seems to be the same for every
           Linux platform */
        return get_errno(safe_flock(arg1, arg2));
    case TARGET_NR_readv:
        {
            struct iovec *vec = lock_iovec(VERIFY_WRITE, arg2, arg3, 0);
            if (vec != NULL) {
                ret = get_errno(safe_readv(arg1, vec, arg3));
                unlock_iovec(vec, arg2, arg3, 1);
            } else {
                ret = -host_to_target_errno(errno);
            }
        }
        return ret;
    case TARGET_NR_writev:
        {
            struct iovec *vec = lock_iovec(VERIFY_READ, arg2, arg3, 1);
            if (vec != NULL) {
                ret = get_errno(safe_writev(arg1, vec, arg3));
                unlock_iovec(vec, arg2, arg3, 0);
            } else {
                ret = -host_to_target_errno(errno);
            }
        }
        return ret;
#if defined(TARGET_NR_preadv)
    case TARGET_NR_preadv:
        {
            struct iovec *vec = lock_iovec(VERIFY_WRITE, arg2, arg3, 0);
            if (vec != NULL) {
                unsigned long low, high;

                target_to_host_low_high(arg4, arg5, &low, &high);
                ret = get_errno(safe_preadv(arg1, vec, arg3, low, high));
                unlock_iovec(vec, arg2, arg3, 1);
            } else {
                ret = -host_to_target_errno(errno);
           }
        }
        return ret;
#endif
#if defined(TARGET_NR_pwritev)
    case TARGET_NR_pwritev:
        {
            struct iovec *vec = lock_iovec(VERIFY_READ, arg2, arg3, 1);
            if (vec != NULL) {
                unsigned long low, high;

                target_to_host_low_high(arg4, arg5, &low, &high);
                ret = get_errno(safe_pwritev(arg1, vec, arg3, low, high));
                unlock_iovec(vec, arg2, arg3, 0);
            } else {
                ret = -host_to_target_errno(errno);
           }
        }
        return ret;
#endif
    case TARGET_NR_getsid:
        return get_errno(getsid(arg1));
#if defined(TARGET_NR_fdatasync) /* Not on alpha (osf_datasync ?) */
    case TARGET_NR_fdatasync:
        return get_errno(fdatasync(arg1));
#endif
    case TARGET_NR_sched_getaffinity:
        {
            unsigned int mask_size;
            unsigned long *mask;

            /*
             * sched_getaffinity needs multiples of ulong, so need to take
             * care of mismatches between target ulong and host ulong sizes.
             */
            if (arg2 & (sizeof(abi_ulong) - 1)) {
                return -TARGET_EINVAL;
            }
            mask_size = (arg2 + (sizeof(*mask) - 1)) & ~(sizeof(*mask) - 1);

            mask = alloca(mask_size);
            memset(mask, 0, mask_size);
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
                        return -TARGET_EINVAL;
                    }
                    ret = arg2;
                }

                if (host_to_target_cpu_mask(mask, mask_size, arg3, ret)) {
                    return -TARGET_EFAULT;
                }
            }
        }
        return ret;
    case TARGET_NR_sched_setaffinity:
        {
            unsigned int mask_size;
            unsigned long *mask;

            /*
             * sched_setaffinity needs multiples of ulong, so need to take
             * care of mismatches between target ulong and host ulong sizes.
             */
            if (arg2 & (sizeof(abi_ulong) - 1)) {
                return -TARGET_EINVAL;
            }
            mask_size = (arg2 + (sizeof(*mask) - 1)) & ~(sizeof(*mask) - 1);
            mask = alloca(mask_size);

            ret = target_to_host_cpu_mask(mask, mask_size, arg3, arg2);
            if (ret) {
                return ret;
            }

            return get_errno(sys_sched_setaffinity(arg1, mask_size, mask));
        }
    case TARGET_NR_getcpu:
        {
            unsigned cpu, node;
            ret = get_errno(sys_getcpu(arg1 ? &cpu : NULL,
                                       arg2 ? &node : NULL,
                                       NULL));
            if (is_error(ret)) {
                return ret;
            }
            if (arg1 && put_user_u32(cpu, arg1)) {
                return -TARGET_EFAULT;
            }
            if (arg2 && put_user_u32(node, arg2)) {
                return -TARGET_EFAULT;
            }
        }
        return ret;
    case TARGET_NR_sched_setparam:
        {
            struct target_sched_param *target_schp;
            struct sched_param schp;

            if (arg2 == 0) {
                return -TARGET_EINVAL;
            }
            if (!lock_user_struct(VERIFY_READ, target_schp, arg2, 1)) {
                return -TARGET_EFAULT;
            }
            schp.sched_priority = tswap32(target_schp->sched_priority);
            unlock_user_struct(target_schp, arg2, 0);
            return get_errno(sys_sched_setparam(arg1, &schp));
        }
    case TARGET_NR_sched_getparam:
        {
            struct target_sched_param *target_schp;
            struct sched_param schp;

            if (arg2 == 0) {
                return -TARGET_EINVAL;
            }
            ret = get_errno(sys_sched_getparam(arg1, &schp));
            if (!is_error(ret)) {
                if (!lock_user_struct(VERIFY_WRITE, target_schp, arg2, 0)) {
                    return -TARGET_EFAULT;
                }
                target_schp->sched_priority = tswap32(schp.sched_priority);
                unlock_user_struct(target_schp, arg2, 1);
            }
        }
        return ret;
    case TARGET_NR_sched_setscheduler:
        {
            struct target_sched_param *target_schp;
            struct sched_param schp;
            if (arg3 == 0) {
                return -TARGET_EINVAL;
            }
            if (!lock_user_struct(VERIFY_READ, target_schp, arg3, 1)) {
                return -TARGET_EFAULT;
            }
            schp.sched_priority = tswap32(target_schp->sched_priority);
            unlock_user_struct(target_schp, arg3, 0);
            return get_errno(sys_sched_setscheduler(arg1, arg2, &schp));
        }
    case TARGET_NR_sched_getscheduler:
        return get_errno(sys_sched_getscheduler(arg1));
    case TARGET_NR_sched_getattr:
        {
            struct target_sched_attr *target_scha;
            struct sched_attr scha;
            if (arg2 == 0) {
                return -TARGET_EINVAL;
            }
            if (arg3 > sizeof(scha)) {
                arg3 = sizeof(scha);
            }
            ret = get_errno(sys_sched_getattr(arg1, &scha, arg3, arg4));
            if (!is_error(ret)) {
                target_scha = lock_user(VERIFY_WRITE, arg2, arg3, 0);
                if (!target_scha) {
                    return -TARGET_EFAULT;
                }
                target_scha->size = tswap32(scha.size);
                target_scha->sched_policy = tswap32(scha.sched_policy);
                target_scha->sched_flags = tswap64(scha.sched_flags);
                target_scha->sched_nice = tswap32(scha.sched_nice);
                target_scha->sched_priority = tswap32(scha.sched_priority);
                target_scha->sched_runtime = tswap64(scha.sched_runtime);
                target_scha->sched_deadline = tswap64(scha.sched_deadline);
                target_scha->sched_period = tswap64(scha.sched_period);
                if (scha.size > offsetof(struct sched_attr, sched_util_min)) {
                    target_scha->sched_util_min = tswap32(scha.sched_util_min);
                    target_scha->sched_util_max = tswap32(scha.sched_util_max);
                }
                unlock_user(target_scha, arg2, arg3);
            }
            return ret;
        }
    case TARGET_NR_sched_setattr:
        {
            struct target_sched_attr *target_scha;
            struct sched_attr scha;
            uint32_t size;
            int zeroed;
            if (arg2 == 0) {
                return -TARGET_EINVAL;
            }
            if (get_user_u32(size, arg2)) {
                return -TARGET_EFAULT;
            }
            if (!size) {
                size = offsetof(struct target_sched_attr, sched_util_min);
            }
            if (size < offsetof(struct target_sched_attr, sched_util_min)) {
                if (put_user_u32(sizeof(struct target_sched_attr), arg2)) {
                    return -TARGET_EFAULT;
                }
                return -TARGET_E2BIG;
            }

            zeroed = check_zeroed_user(arg2, sizeof(struct target_sched_attr), size);
            if (zeroed < 0) {
                return zeroed;
            } else if (zeroed == 0) {
                if (put_user_u32(sizeof(struct target_sched_attr), arg2)) {
                    return -TARGET_EFAULT;
                }
                return -TARGET_E2BIG;
            }
            if (size > sizeof(struct target_sched_attr)) {
                size = sizeof(struct target_sched_attr);
            }

            target_scha = lock_user(VERIFY_READ, arg2, size, 1);
            if (!target_scha) {
                return -TARGET_EFAULT;
            }
            scha.size = size;
            scha.sched_policy = tswap32(target_scha->sched_policy);
            scha.sched_flags = tswap64(target_scha->sched_flags);
            scha.sched_nice = tswap32(target_scha->sched_nice);
            scha.sched_priority = tswap32(target_scha->sched_priority);
            scha.sched_runtime = tswap64(target_scha->sched_runtime);
            scha.sched_deadline = tswap64(target_scha->sched_deadline);
            scha.sched_period = tswap64(target_scha->sched_period);
            if (size > offsetof(struct target_sched_attr, sched_util_min)) {
                scha.sched_util_min = tswap32(target_scha->sched_util_min);
                scha.sched_util_max = tswap32(target_scha->sched_util_max);
            }
            unlock_user(target_scha, arg2, 0);
            return get_errno(sys_sched_setattr(arg1, &scha, arg3));
        }
    case TARGET_NR_sched_yield:
        return get_errno(sched_yield());
    case TARGET_NR_sched_get_priority_max:
        return get_errno(sched_get_priority_max(arg1));
    case TARGET_NR_sched_get_priority_min:
        return get_errno(sched_get_priority_min(arg1));
#ifdef TARGET_NR_sched_rr_get_interval
    case TARGET_NR_sched_rr_get_interval:
        {
            struct timespec ts;
            ret = get_errno(sched_rr_get_interval(arg1, &ts));
            if (!is_error(ret)) {
                ret = host_to_target_timespec(arg2, &ts);
            }
        }
        return ret;
#endif
#ifdef TARGET_NR_sched_rr_get_interval_time64
    case TARGET_NR_sched_rr_get_interval_time64:
        {
            struct timespec ts;
            ret = get_errno(sched_rr_get_interval(arg1, &ts));
            if (!is_error(ret)) {
                ret = host_to_target_timespec64(arg2, &ts);
            }
        }
        return ret;
#endif
#if defined(TARGET_NR_nanosleep)
    case TARGET_NR_nanosleep:
        {
            struct timespec req, rem;
            target_to_host_timespec(&req, arg1);
            ret = get_errno(safe_nanosleep(&req, &rem));
            if (is_error(ret) && arg2) {
                host_to_target_timespec(arg2, &rem);
            }
        }
        return ret;
#endif
    case TARGET_NR_prctl:
        return do_prctl(cpu_env, arg1, arg2, arg3, arg4, arg5);
        break;
#ifdef TARGET_NR_arch_prctl
    case TARGET_NR_arch_prctl:
        return do_arch_prctl(cpu_env, arg1, arg2);
#endif
#ifdef TARGET_NR_pread64
    case TARGET_NR_pread64:
        if (regpairs_aligned(cpu_env, num)) {
            arg4 = arg5;
            arg5 = arg6;
        }
        if (arg2 == 0 && arg3 == 0) {
            /* Special-case NULL buffer and zero length, which should succeed */
            p = 0;
        } else {
            p = lock_user(VERIFY_WRITE, arg2, arg3, 0);
            if (!p) {
                return -TARGET_EFAULT;
            }
        }
        ret = get_errno(pread64(arg1, p, arg3, target_offset64(arg4, arg5)));
        unlock_user(p, arg2, ret);
        return ret;
    case TARGET_NR_pwrite64:
        if (regpairs_aligned(cpu_env, num)) {
            arg4 = arg5;
            arg5 = arg6;
        }
        if (arg2 == 0 && arg3 == 0) {
            /* Special-case NULL buffer and zero length, which should succeed */
            p = 0;
        } else {
            p = lock_user(VERIFY_READ, arg2, arg3, 1);
            if (!p) {
                return -TARGET_EFAULT;
            }
        }
        ret = get_errno(pwrite64(arg1, p, arg3, target_offset64(arg4, arg5)));
        unlock_user(p, arg2, 0);
        return ret;
#endif
    case TARGET_NR_getcwd:
        if (!(p = lock_user(VERIFY_WRITE, arg1, arg2, 0)))
            return -TARGET_EFAULT;
        ret = get_errno(sys_getcwd1(p, arg2));
        unlock_user(p, arg1, ret);
        return ret;
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
            return -TARGET_EFAULT;
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
                return -TARGET_EFAULT;
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
        return ret;
    }
    case TARGET_NR_sigaltstack:
        return do_sigaltstack(arg1, arg2, cpu_env);

#ifdef CONFIG_SENDFILE
#ifdef TARGET_NR_sendfile
    case TARGET_NR_sendfile:
    {
        off_t *offp = NULL;
        off_t off;
        if (arg3) {
            ret = get_user_sal(off, arg3);
            if (is_error(ret)) {
                return ret;
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
        return ret;
    }
#endif
#ifdef TARGET_NR_sendfile64
    case TARGET_NR_sendfile64:
    {
        off_t *offp = NULL;
        off_t off;
        if (arg3) {
            ret = get_user_s64(off, arg3);
            if (is_error(ret)) {
                return ret;
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
        return ret;
    }
#endif
#endif
#ifdef TARGET_NR_vfork
    case TARGET_NR_vfork:
        return get_errno(do_fork(cpu_env,
                         CLONE_VFORK | CLONE_VM | TARGET_SIGCHLD,
                         0, 0, 0, 0));
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
                return -TARGET_EFAULT;
	    target_rlim->rlim_cur = host_to_target_rlim(rlim.rlim_cur);
	    target_rlim->rlim_max = host_to_target_rlim(rlim.rlim_max);
            unlock_user_struct(target_rlim, arg2, 1);
	}
        return ret;
    }
#endif
#ifdef TARGET_NR_truncate64
    case TARGET_NR_truncate64:
        if (!(p = lock_user_string(arg1)))
            return -TARGET_EFAULT;
	ret = target_truncate64(cpu_env, p, arg2, arg3, arg4);
        unlock_user(p, arg1, 0);
        return ret;
#endif
#ifdef TARGET_NR_ftruncate64
    case TARGET_NR_ftruncate64:
        return target_ftruncate64(cpu_env, arg1, arg2, arg3, arg4);
#endif
#ifdef TARGET_NR_stat64
    case TARGET_NR_stat64:
        if (!(p = lock_user_string(arg1))) {
            return -TARGET_EFAULT;
        }
        ret = get_errno(stat(path(p), &st));
        unlock_user(p, arg1, 0);
        if (!is_error(ret))
            ret = host_to_target_stat64(cpu_env, arg2, &st);
        return ret;
#endif
#ifdef TARGET_NR_lstat64
    case TARGET_NR_lstat64:
        if (!(p = lock_user_string(arg1))) {
            return -TARGET_EFAULT;
        }
        ret = get_errno(lstat(path(p), &st));
        unlock_user(p, arg1, 0);
        if (!is_error(ret))
            ret = host_to_target_stat64(cpu_env, arg2, &st);
        return ret;
#endif
#ifdef TARGET_NR_fstat64
    case TARGET_NR_fstat64:
        ret = get_errno(fstat(arg1, &st));
        if (!is_error(ret))
            ret = host_to_target_stat64(cpu_env, arg2, &st);
        return ret;
#endif
#if (defined(TARGET_NR_fstatat64) || defined(TARGET_NR_newfstatat))
#ifdef TARGET_NR_fstatat64
    case TARGET_NR_fstatat64:
#endif
#ifdef TARGET_NR_newfstatat
    case TARGET_NR_newfstatat:
#endif
        if (!(p = lock_user_string(arg2))) {
            return -TARGET_EFAULT;
        }
        ret = get_errno(fstatat(arg1, path(p), &st, arg4));
        unlock_user(p, arg2, 0);
        if (!is_error(ret))
            ret = host_to_target_stat64(cpu_env, arg3, &st);
        return ret;
#endif
#if defined(TARGET_NR_statx)
    case TARGET_NR_statx:
        {
            struct target_statx *target_stx;
            int dirfd = arg1;
            int flags = arg3;

            p = lock_user_string(arg2);
            if (p == NULL) {
                return -TARGET_EFAULT;
            }
#if defined(__NR_statx)
            {
                /*
                 * It is assumed that struct statx is architecture independent.
                 */
                struct target_statx host_stx;
                int mask = arg4;

                ret = get_errno(sys_statx(dirfd, p, flags, mask, &host_stx));
                if (!is_error(ret)) {
                    if (host_to_target_statx(&host_stx, arg5) != 0) {
                        unlock_user(p, arg2, 0);
                        return -TARGET_EFAULT;
                    }
                }

                if (ret != -TARGET_ENOSYS) {
                    unlock_user(p, arg2, 0);
                    return ret;
                }
            }
#endif
            ret = get_errno(fstatat(dirfd, path(p), &st, flags));
            unlock_user(p, arg2, 0);

            if (!is_error(ret)) {
                if (!lock_user_struct(VERIFY_WRITE, target_stx, arg5, 0)) {
                    return -TARGET_EFAULT;
                }
                memset(target_stx, 0, sizeof(*target_stx));
                __put_user(major(st.st_dev), &target_stx->stx_dev_major);
                __put_user(minor(st.st_dev), &target_stx->stx_dev_minor);
                __put_user(st.st_ino, &target_stx->stx_ino);
                __put_user(st.st_mode, &target_stx->stx_mode);
                __put_user(st.st_uid, &target_stx->stx_uid);
                __put_user(st.st_gid, &target_stx->stx_gid);
                __put_user(st.st_nlink, &target_stx->stx_nlink);
                __put_user(major(st.st_rdev), &target_stx->stx_rdev_major);
                __put_user(minor(st.st_rdev), &target_stx->stx_rdev_minor);
                __put_user(st.st_size, &target_stx->stx_size);
                __put_user(st.st_blksize, &target_stx->stx_blksize);
                __put_user(st.st_blocks, &target_stx->stx_blocks);
                __put_user(st.st_atime, &target_stx->stx_atime.tv_sec);
                __put_user(st.st_mtime, &target_stx->stx_mtime.tv_sec);
                __put_user(st.st_ctime, &target_stx->stx_ctime.tv_sec);
                unlock_user_struct(target_stx, arg5, 1);
            }
        }
        return ret;
#endif
#ifdef TARGET_NR_lchown
    case TARGET_NR_lchown:
        if (!(p = lock_user_string(arg1)))
            return -TARGET_EFAULT;
        ret = get_errno(lchown(p, low2highuid(arg2), low2highgid(arg3)));
        unlock_user(p, arg1, 0);
        return ret;
#endif
#ifdef TARGET_NR_getuid
    case TARGET_NR_getuid:
        return get_errno(high2lowuid(getuid()));
#endif
#ifdef TARGET_NR_getgid
    case TARGET_NR_getgid:
        return get_errno(high2lowgid(getgid()));
#endif
#ifdef TARGET_NR_geteuid
    case TARGET_NR_geteuid:
        return get_errno(high2lowuid(geteuid()));
#endif
#ifdef TARGET_NR_getegid
    case TARGET_NR_getegid:
        return get_errno(high2lowgid(getegid()));
#endif
    case TARGET_NR_setreuid:
        return get_errno(setreuid(low2highuid(arg1), low2highuid(arg2)));
    case TARGET_NR_setregid:
        return get_errno(setregid(low2highgid(arg1), low2highgid(arg2)));
    case TARGET_NR_getgroups:
        {
            int gidsetsize = arg1;
            target_id *target_grouplist;
            gid_t *grouplist;
            int i;

            grouplist = alloca(gidsetsize * sizeof(gid_t));
            ret = get_errno(getgroups(gidsetsize, grouplist));
            if (gidsetsize == 0)
                return ret;
            if (!is_error(ret)) {
                target_grouplist = lock_user(VERIFY_WRITE, arg2, gidsetsize * sizeof(target_id), 0);
                if (!target_grouplist)
                    return -TARGET_EFAULT;
                for(i = 0;i < ret; i++)
                    target_grouplist[i] = tswapid(high2lowgid(grouplist[i]));
                unlock_user(target_grouplist, arg2, gidsetsize * sizeof(target_id));
            }
        }
        return ret;
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
                    return -TARGET_EFAULT;
                }
                for (i = 0; i < gidsetsize; i++) {
                    grouplist[i] = low2highgid(tswapid(target_grouplist[i]));
                }
                unlock_user(target_grouplist, arg2, 0);
            }
            return get_errno(setgroups(gidsetsize, grouplist));
        }
    case TARGET_NR_fchown:
        return get_errno(fchown(arg1, low2highuid(arg2), low2highgid(arg3)));
#if defined(TARGET_NR_fchownat)
    case TARGET_NR_fchownat:
        if (!(p = lock_user_string(arg2))) 
            return -TARGET_EFAULT;
        ret = get_errno(fchownat(arg1, p, low2highuid(arg3),
                                 low2highgid(arg4), arg5));
        unlock_user(p, arg2, 0);
        return ret;
#endif
#ifdef TARGET_NR_setresuid
    case TARGET_NR_setresuid:
        return get_errno(sys_setresuid(low2highuid(arg1),
                                       low2highuid(arg2),
                                       low2highuid(arg3)));
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
                    return -TARGET_EFAULT;
            }
        }
        return ret;
#endif
#ifdef TARGET_NR_getresgid
    case TARGET_NR_setresgid:
        return get_errno(sys_setresgid(low2highgid(arg1),
                                       low2highgid(arg2),
                                       low2highgid(arg3)));
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
                    return -TARGET_EFAULT;
            }
        }
        return ret;
#endif
#ifdef TARGET_NR_chown
    case TARGET_NR_chown:
        if (!(p = lock_user_string(arg1)))
            return -TARGET_EFAULT;
        ret = get_errno(chown(p, low2highuid(arg2), low2highgid(arg3)));
        unlock_user(p, arg1, 0);
        return ret;
#endif
    case TARGET_NR_setuid:
        return get_errno(sys_setuid(low2highuid(arg1)));
    case TARGET_NR_setgid:
        return get_errno(sys_setgid(low2highgid(arg1)));
    case TARGET_NR_setfsuid:
        return get_errno(setfsuid(arg1));
    case TARGET_NR_setfsgid:
        return get_errno(setfsgid(arg1));

#ifdef TARGET_NR_lchown32
    case TARGET_NR_lchown32:
        if (!(p = lock_user_string(arg1)))
            return -TARGET_EFAULT;
        ret = get_errno(lchown(p, arg2, arg3));
        unlock_user(p, arg1, 0);
        return ret;
#endif
#ifdef TARGET_NR_getuid32
    case TARGET_NR_getuid32:
        return get_errno(getuid());
#endif

#if defined(TARGET_NR_getxuid) && defined(TARGET_ALPHA)
   /* Alpha specific */
    case TARGET_NR_getxuid:
         {
            uid_t euid;
            euid=geteuid();
            cpu_env->ir[IR_A4]=euid;
         }
        return get_errno(getuid());
#endif
#if defined(TARGET_NR_getxgid) && defined(TARGET_ALPHA)
   /* Alpha specific */
    case TARGET_NR_getxgid:
         {
            uid_t egid;
            egid=getegid();
            cpu_env->ir[IR_A4]=egid;
         }
        return get_errno(getgid());
#endif
#if defined(TARGET_NR_osf_getsysinfo) && defined(TARGET_ALPHA)
    /* Alpha specific */
    case TARGET_NR_osf_getsysinfo:
        ret = -TARGET_EOPNOTSUPP;
        switch (arg1) {
          case TARGET_GSI_IEEE_FP_CONTROL:
            {
                uint64_t fpcr = cpu_alpha_load_fpcr(cpu_env);
                uint64_t swcr = cpu_env->swcr;

                swcr &= ~SWCR_STATUS_MASK;
                swcr |= (fpcr >> 35) & SWCR_STATUS_MASK;

                if (put_user_u64 (swcr, arg2))
                        return -TARGET_EFAULT;
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
        return ret;
#endif
#if defined(TARGET_NR_osf_setsysinfo) && defined(TARGET_ALPHA)
    /* Alpha specific */
    case TARGET_NR_osf_setsysinfo:
        ret = -TARGET_EOPNOTSUPP;
        switch (arg1) {
          case TARGET_SSI_IEEE_FP_CONTROL:
            {
                uint64_t swcr, fpcr;

                if (get_user_u64 (swcr, arg2)) {
                    return -TARGET_EFAULT;
                }

                /*
                 * The kernel calls swcr_update_status to update the
                 * status bits from the fpcr at every point that it
                 * could be queried.  Therefore, we store the status
                 * bits only in FPCR.
                 */
                cpu_env->swcr = swcr & (SWCR_TRAP_ENABLE_MASK | SWCR_MAP_MASK);

                fpcr = cpu_alpha_load_fpcr(cpu_env);
                fpcr &= ((uint64_t)FPCR_DYN_MASK << 32);
                fpcr |= alpha_ieee_swcr_to_fpcr(swcr);
                cpu_alpha_store_fpcr(cpu_env, fpcr);
                ret = 0;
            }
            break;

          case TARGET_SSI_IEEE_RAISE_EXCEPTION:
            {
                uint64_t exc, fpcr, fex;

                if (get_user_u64(exc, arg2)) {
                    return -TARGET_EFAULT;
                }
                exc &= SWCR_STATUS_MASK;
                fpcr = cpu_alpha_load_fpcr(cpu_env);

                /* Old exceptions are not signaled.  */
                fex = alpha_ieee_fpcr_to_swcr(fpcr);
                fex = exc & ~fex;
                fex >>= SWCR_STATUS_TO_EXCSUM_SHIFT;
                fex &= (cpu_env)->swcr;

                /* Update the hardware fpcr.  */
                fpcr |= alpha_ieee_swcr_to_fpcr(exc);
                cpu_alpha_store_fpcr(cpu_env, fpcr);

                if (fex) {
                    int si_code = TARGET_FPE_FLTUNK;
                    target_siginfo_t info;

                    if (fex & SWCR_TRAP_ENABLE_DNO) {
                        si_code = TARGET_FPE_FLTUND;
                    }
                    if (fex & SWCR_TRAP_ENABLE_INE) {
                        si_code = TARGET_FPE_FLTRES;
                    }
                    if (fex & SWCR_TRAP_ENABLE_UNF) {
                        si_code = TARGET_FPE_FLTUND;
                    }
                    if (fex & SWCR_TRAP_ENABLE_OVF) {
                        si_code = TARGET_FPE_FLTOVF;
                    }
                    if (fex & SWCR_TRAP_ENABLE_DZE) {
                        si_code = TARGET_FPE_FLTDIV;
                    }
                    if (fex & SWCR_TRAP_ENABLE_INV) {
                        si_code = TARGET_FPE_FLTINV;
                    }

                    info.si_signo = SIGFPE;
                    info.si_errno = 0;
                    info.si_code = si_code;
                    info._sifields._sigfault._addr = (cpu_env)->pc;
                    queue_signal(cpu_env, info.si_signo,
                                 QEMU_SI_FAULT, &info);
                }
                ret = 0;
            }
            break;

          /* case SSI_NVPAIRS:
             -- Used with SSIN_UACPROC to enable unaligned accesses.
             case SSI_IEEE_STATE_AT_SIGNAL:
             case SSI_IEEE_IGNORE_STATE_AT_SIGNAL:
             -- Not implemented in linux kernel
          */
        }
        return ret;
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
                return -TARGET_EINVAL;
            }
            mask = arg2;
            target_to_host_old_sigset(&set, &mask);
            ret = do_sigprocmask(how, &set, &oldset);
            if (!ret) {
                host_to_target_old_sigset(&mask, &oldset);
                ret = mask;
            }
        }
        return ret;
#endif

#ifdef TARGET_NR_getgid32
    case TARGET_NR_getgid32:
        return get_errno(getgid());
#endif
#ifdef TARGET_NR_geteuid32
    case TARGET_NR_geteuid32:
        return get_errno(geteuid());
#endif
#ifdef TARGET_NR_getegid32
    case TARGET_NR_getegid32:
        return get_errno(getegid());
#endif
#ifdef TARGET_NR_setreuid32
    case TARGET_NR_setreuid32:
        return get_errno(setreuid(arg1, arg2));
#endif
#ifdef TARGET_NR_setregid32
    case TARGET_NR_setregid32:
        return get_errno(setregid(arg1, arg2));
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
                return ret;
            if (!is_error(ret)) {
                target_grouplist = lock_user(VERIFY_WRITE, arg2, gidsetsize * 4, 0);
                if (!target_grouplist) {
                    return -TARGET_EFAULT;
                }
                for(i = 0;i < ret; i++)
                    target_grouplist[i] = tswap32(grouplist[i]);
                unlock_user(target_grouplist, arg2, gidsetsize * 4);
            }
        }
        return ret;
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
                return -TARGET_EFAULT;
            }
            for(i = 0;i < gidsetsize; i++)
                grouplist[i] = tswap32(target_grouplist[i]);
            unlock_user(target_grouplist, arg2, 0);
            return get_errno(setgroups(gidsetsize, grouplist));
        }
#endif
#ifdef TARGET_NR_fchown32
    case TARGET_NR_fchown32:
        return get_errno(fchown(arg1, arg2, arg3));
#endif
#ifdef TARGET_NR_setresuid32
    case TARGET_NR_setresuid32:
        return get_errno(sys_setresuid(arg1, arg2, arg3));
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
                    return -TARGET_EFAULT;
            }
        }
        return ret;
#endif
#ifdef TARGET_NR_setresgid32
    case TARGET_NR_setresgid32:
        return get_errno(sys_setresgid(arg1, arg2, arg3));
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
                    return -TARGET_EFAULT;
            }
        }
        return ret;
#endif
#ifdef TARGET_NR_chown32
    case TARGET_NR_chown32:
        if (!(p = lock_user_string(arg1)))
            return -TARGET_EFAULT;
        ret = get_errno(chown(p, arg2, arg3));
        unlock_user(p, arg1, 0);
        return ret;
#endif
#ifdef TARGET_NR_setuid32
    case TARGET_NR_setuid32:
        return get_errno(sys_setuid(arg1));
#endif
#ifdef TARGET_NR_setgid32
    case TARGET_NR_setgid32:
        return get_errno(sys_setgid(arg1));
#endif
#ifdef TARGET_NR_setfsuid32
    case TARGET_NR_setfsuid32:
        return get_errno(setfsuid(arg1));
#endif
#ifdef TARGET_NR_setfsgid32
    case TARGET_NR_setfsgid32:
        return get_errno(setfsgid(arg1));
#endif
#ifdef TARGET_NR_mincore
    case TARGET_NR_mincore:
        {
            void *a = lock_user(VERIFY_READ, arg1, arg2, 0);
            if (!a) {
                return -TARGET_ENOMEM;
            }
            p = lock_user_string(arg3);
            if (!p) {
                ret = -TARGET_EFAULT;
            } else {
                ret = get_errno(mincore(a, arg2, p));
                unlock_user(p, arg3, ret);
            }
            unlock_user(a, arg1, 0);
        }
        return ret;
#endif
#ifdef TARGET_NR_arm_fadvise64_64
    case TARGET_NR_arm_fadvise64_64:
        /* arm_fadvise64_64 looks like fadvise64_64 but
         * with different argument order: fd, advice, offset, len
         * rather than the usual fd, offset, len, advice.
         * Note that offset and len are both 64-bit so appear as
         * pairs of 32-bit registers.
         */
        ret = posix_fadvise(arg1, target_offset64(arg3, arg4),
                            target_offset64(arg5, arg6), arg2);
        return -host_to_target_errno(ret);
#endif

#if TARGET_ABI_BITS == 32 && !defined(TARGET_ABI_MIPSN32)

#ifdef TARGET_NR_fadvise64_64
    case TARGET_NR_fadvise64_64:
#if defined(TARGET_PPC) || defined(TARGET_XTENSA)
        /* 6 args: fd, advice, offset (high, low), len (high, low) */
        ret = arg2;
        arg2 = arg3;
        arg3 = arg4;
        arg4 = arg5;
        arg5 = arg6;
        arg6 = ret;
#else
        /* 6 args: fd, offset (high, low), len (high, low), advice */
        if (regpairs_aligned(cpu_env, num)) {
            /* offset is in (3,4), len in (5,6) and advice in 7 */
            arg2 = arg3;
            arg3 = arg4;
            arg4 = arg5;
            arg5 = arg6;
            arg6 = arg7;
        }
#endif
        ret = posix_fadvise(arg1, target_offset64(arg2, arg3),
                            target_offset64(arg4, arg5), arg6);
        return -host_to_target_errno(ret);
#endif

#ifdef TARGET_NR_fadvise64
    case TARGET_NR_fadvise64:
        /* 5 args: fd, offset (high, low), len, advice */
        if (regpairs_aligned(cpu_env, num)) {
            /* offset is in (3,4), len in 5 and advice in 6 */
            arg2 = arg3;
            arg3 = arg4;
            arg4 = arg5;
            arg5 = arg6;
        }
        ret = posix_fadvise(arg1, target_offset64(arg2, arg3), arg4, arg5);
        return -host_to_target_errno(ret);
#endif

#else /* not a 32-bit ABI */
#if defined(TARGET_NR_fadvise64_64) || defined(TARGET_NR_fadvise64)
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
        return -host_to_target_errno(posix_fadvise(arg1, arg2, arg3, arg4));
#endif
#endif /* end of 64-bit ABI fadvise handling */

#ifdef TARGET_NR_madvise
    case TARGET_NR_madvise:
        return target_madvise(arg1, arg2, arg3);
#endif
#ifdef TARGET_NR_fcntl64
    case TARGET_NR_fcntl64:
    {
        int cmd;
        struct flock64 fl;
        from_flock64_fn *copyfrom = copy_from_user_flock64;
        to_flock64_fn *copyto = copy_to_user_flock64;

#ifdef TARGET_ARM
        if (!cpu_env->eabi) {
            copyfrom = copy_from_user_oabi_flock64;
            copyto = copy_to_user_oabi_flock64;
        }
#endif

        cmd = target_to_host_fcntl_cmd(arg2);
        if (cmd == -TARGET_EINVAL) {
            return cmd;
        }

        switch(arg2) {
        case TARGET_F_GETLK64:
            ret = copyfrom(&fl, arg3);
            if (ret) {
                break;
            }
            ret = get_errno(safe_fcntl(arg1, cmd, &fl));
            if (ret == 0) {
                ret = copyto(arg3, &fl);
            }
	    break;

        case TARGET_F_SETLK64:
        case TARGET_F_SETLKW64:
            ret = copyfrom(&fl, arg3);
            if (ret) {
                break;
            }
            ret = get_errno(safe_fcntl(arg1, cmd, &fl));
	    break;
        default:
            ret = do_fcntl(arg1, arg2, arg3);
            break;
        }
        return ret;
    }
#endif
#ifdef TARGET_NR_cacheflush
    case TARGET_NR_cacheflush:
        /* self-modifying code is handled automatically, so nothing needed */
        return 0;
#endif
#ifdef TARGET_NR_getpagesize
    case TARGET_NR_getpagesize:
        return TARGET_PAGE_SIZE;
#endif
    case TARGET_NR_gettid:
        return get_errno(sys_gettid());
#ifdef TARGET_NR_readahead
    case TARGET_NR_readahead:
#if TARGET_ABI_BITS == 32 && !defined(TARGET_ABI_MIPSN32)
        if (regpairs_aligned(cpu_env, num)) {
            arg2 = arg3;
            arg3 = arg4;
            arg4 = arg5;
        }
        ret = get_errno(readahead(arg1, target_offset64(arg2, arg3) , arg4));
#else
        ret = get_errno(readahead(arg1, arg2, arg3));
#endif
        return ret;
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
                return -TARGET_EFAULT;
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
        return ret;
    }
    case TARGET_NR_flistxattr:
    {
        void *b = 0;
        if (arg2) {
            b = lock_user(VERIFY_WRITE, arg2, arg3, 0);
            if (!b) {
                return -TARGET_EFAULT;
            }
        }
        ret = get_errno(flistxattr(arg1, b, arg3));
        unlock_user(b, arg2, arg3);
        return ret;
    }
    case TARGET_NR_setxattr:
    case TARGET_NR_lsetxattr:
        {
            void *p, *n, *v = 0;
            if (arg3) {
                v = lock_user(VERIFY_READ, arg3, arg4, 1);
                if (!v) {
                    return -TARGET_EFAULT;
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
        return ret;
    case TARGET_NR_fsetxattr:
        {
            void *n, *v = 0;
            if (arg3) {
                v = lock_user(VERIFY_READ, arg3, arg4, 1);
                if (!v) {
                    return -TARGET_EFAULT;
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
        return ret;
    case TARGET_NR_getxattr:
    case TARGET_NR_lgetxattr:
        {
            void *p, *n, *v = 0;
            if (arg3) {
                v = lock_user(VERIFY_WRITE, arg3, arg4, 0);
                if (!v) {
                    return -TARGET_EFAULT;
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
        return ret;
    case TARGET_NR_fgetxattr:
        {
            void *n, *v = 0;
            if (arg3) {
                v = lock_user(VERIFY_WRITE, arg3, arg4, 0);
                if (!v) {
                    return -TARGET_EFAULT;
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
        return ret;
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
        return ret;
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
        return ret;
#endif
#endif /* CONFIG_ATTR */
#ifdef TARGET_NR_set_thread_area
    case TARGET_NR_set_thread_area:
#if defined(TARGET_MIPS)
      cpu_env->active_tc.CP0_UserLocal = arg1;
      return 0;
#elif defined(TARGET_CRIS)
      if (arg1 & 0xff)
          ret = -TARGET_EINVAL;
      else {
          cpu_env->pregs[PR_PID] = arg1;
          ret = 0;
      }
      return ret;
#elif defined(TARGET_I386) && defined(TARGET_ABI32)
      return do_set_thread_area(cpu_env, arg1);
#elif defined(TARGET_M68K)
      {
          TaskState *ts = cpu->opaque;
          ts->tp_value = arg1;
          return 0;
      }
#else
      return -TARGET_ENOSYS;
#endif
#endif
#ifdef TARGET_NR_get_thread_area
    case TARGET_NR_get_thread_area:
#if defined(TARGET_I386) && defined(TARGET_ABI32)
        return do_get_thread_area(cpu_env, arg1);
#elif defined(TARGET_M68K)
        {
            TaskState *ts = cpu->opaque;
            return ts->tp_value;
        }
#else
        return -TARGET_ENOSYS;
#endif
#endif
#ifdef TARGET_NR_getdomainname
    case TARGET_NR_getdomainname:
        return -TARGET_ENOSYS;
#endif

#ifdef TARGET_NR_clock_settime
    case TARGET_NR_clock_settime:
    {
        struct timespec ts;

        ret = target_to_host_timespec(&ts, arg2);
        if (!is_error(ret)) {
            ret = get_errno(clock_settime(arg1, &ts));
        }
        return ret;
    }
#endif
#ifdef TARGET_NR_clock_settime64
    case TARGET_NR_clock_settime64:
    {
        struct timespec ts;

        ret = target_to_host_timespec64(&ts, arg2);
        if (!is_error(ret)) {
            ret = get_errno(clock_settime(arg1, &ts));
        }
        return ret;
    }
#endif
#ifdef TARGET_NR_clock_gettime
    case TARGET_NR_clock_gettime:
    {
        struct timespec ts;
        ret = get_errno(clock_gettime(arg1, &ts));
        if (!is_error(ret)) {
            ret = host_to_target_timespec(arg2, &ts);
        }
        return ret;
    }
#endif
#ifdef TARGET_NR_clock_gettime64
    case TARGET_NR_clock_gettime64:
    {
        struct timespec ts;
        ret = get_errno(clock_gettime(arg1, &ts));
        if (!is_error(ret)) {
            ret = host_to_target_timespec64(arg2, &ts);
        }
        return ret;
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
        return ret;
    }
#endif
#ifdef TARGET_NR_clock_getres_time64
    case TARGET_NR_clock_getres_time64:
    {
        struct timespec ts;
        ret = get_errno(clock_getres(arg1, &ts));
        if (!is_error(ret)) {
            host_to_target_timespec64(arg2, &ts);
        }
        return ret;
    }
#endif
#ifdef TARGET_NR_clock_nanosleep
    case TARGET_NR_clock_nanosleep:
    {
        struct timespec ts;
        if (target_to_host_timespec(&ts, arg3)) {
            return -TARGET_EFAULT;
        }
        ret = get_errno(safe_clock_nanosleep(arg1, arg2,
                                             &ts, arg4 ? &ts : NULL));
        /*
         * if the call is interrupted by a signal handler, it fails
         * with error -TARGET_EINTR and if arg4 is not NULL and arg2 is not
         * TIMER_ABSTIME, it returns the remaining unslept time in arg4.
         */
        if (ret == -TARGET_EINTR && arg4 && arg2 != TIMER_ABSTIME &&
            host_to_target_timespec(arg4, &ts)) {
              return -TARGET_EFAULT;
        }

        return ret;
    }
#endif
#ifdef TARGET_NR_clock_nanosleep_time64
    case TARGET_NR_clock_nanosleep_time64:
    {
        struct timespec ts;

        if (target_to_host_timespec64(&ts, arg3)) {
            return -TARGET_EFAULT;
        }

        ret = get_errno(safe_clock_nanosleep(arg1, arg2,
                                             &ts, arg4 ? &ts : NULL));

        if (ret == -TARGET_EINTR && arg4 && arg2 != TIMER_ABSTIME &&
            host_to_target_timespec64(arg4, &ts)) {
            return -TARGET_EFAULT;
        }
        return ret;
    }
#endif

#if defined(TARGET_NR_set_tid_address)
    case TARGET_NR_set_tid_address:
    {
        TaskState *ts = cpu->opaque;
        ts->child_tidptr = arg1;
        /* do not call host set_tid_address() syscall, instead return tid() */
        return get_errno(sys_gettid());
    }
#endif

    case TARGET_NR_tkill:
        return get_errno(safe_tkill((int)arg1, target_to_host_signal(arg2)));

    case TARGET_NR_tgkill:
        return get_errno(safe_tgkill((int)arg1, (int)arg2,
                         target_to_host_signal(arg3)));

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
        return -TARGET_ENOSYS;
#endif

#if defined(TARGET_NR_utimensat)
    case TARGET_NR_utimensat:
        {
            struct timespec *tsp, ts[2];
            if (!arg3) {
                tsp = NULL;
            } else {
                if (target_to_host_timespec(ts, arg3)) {
                    return -TARGET_EFAULT;
                }
                if (target_to_host_timespec(ts + 1, arg3 +
                                            sizeof(struct target_timespec))) {
                    return -TARGET_EFAULT;
                }
                tsp = ts;
            }
            if (!arg2)
                ret = get_errno(sys_utimensat(arg1, NULL, tsp, arg4));
            else {
                if (!(p = lock_user_string(arg2))) {
                    return -TARGET_EFAULT;
                }
                ret = get_errno(sys_utimensat(arg1, path(p), tsp, arg4));
                unlock_user(p, arg2, 0);
            }
        }
        return ret;
#endif
#ifdef TARGET_NR_utimensat_time64
    case TARGET_NR_utimensat_time64:
        {
            struct timespec *tsp, ts[2];
            if (!arg3) {
                tsp = NULL;
            } else {
                if (target_to_host_timespec64(ts, arg3)) {
                    return -TARGET_EFAULT;
                }
                if (target_to_host_timespec64(ts + 1, arg3 +
                                     sizeof(struct target__kernel_timespec))) {
                    return -TARGET_EFAULT;
                }
                tsp = ts;
            }
            if (!arg2)
                ret = get_errno(sys_utimensat(arg1, NULL, tsp, arg4));
            else {
                p = lock_user_string(arg2);
                if (!p) {
                    return -TARGET_EFAULT;
                }
                ret = get_errno(sys_utimensat(arg1, path(p), tsp, arg4));
                unlock_user(p, arg2, 0);
            }
        }
        return ret;
#endif
#ifdef TARGET_NR_futex
    case TARGET_NR_futex:
        return do_futex(cpu, false, arg1, arg2, arg3, arg4, arg5, arg6);
#endif
#ifdef TARGET_NR_futex_time64
    case TARGET_NR_futex_time64:
        return do_futex(cpu, true, arg1, arg2, arg3, arg4, arg5, arg6);
#endif
#ifdef CONFIG_INOTIFY
#if defined(TARGET_NR_inotify_init)
    case TARGET_NR_inotify_init:
        ret = get_errno(inotify_init());
        if (ret >= 0) {
            fd_trans_register(ret, &target_inotify_trans);
        }
        return ret;
#endif
#if defined(TARGET_NR_inotify_init1) && defined(CONFIG_INOTIFY1)
    case TARGET_NR_inotify_init1:
        ret = get_errno(inotify_init1(target_to_host_bitmask(arg1,
                                          fcntl_flags_tbl)));
        if (ret >= 0) {
            fd_trans_register(ret, &target_inotify_trans);
        }
        return ret;
#endif
#if defined(TARGET_NR_inotify_add_watch)
    case TARGET_NR_inotify_add_watch:
        p = lock_user_string(arg2);
        ret = get_errno(inotify_add_watch(arg1, path(p), arg3));
        unlock_user(p, arg2, 0);
        return ret;
#endif
#if defined(TARGET_NR_inotify_rm_watch)
    case TARGET_NR_inotify_rm_watch:
        return get_errno(inotify_rm_watch(arg1, arg2));
#endif
#endif

#if defined(TARGET_NR_mq_open) && defined(__NR_mq_open)
    case TARGET_NR_mq_open:
        {
            struct mq_attr posix_mq_attr;
            struct mq_attr *pposix_mq_attr;
            int host_flags;

            host_flags = target_to_host_bitmask(arg2, fcntl_flags_tbl);
            pposix_mq_attr = NULL;
            if (arg4) {
                if (copy_from_user_mq_attr(&posix_mq_attr, arg4) != 0) {
                    return -TARGET_EFAULT;
                }
                pposix_mq_attr = &posix_mq_attr;
            }
            p = lock_user_string(arg1 - 1);
            if (!p) {
                return -TARGET_EFAULT;
            }
            ret = get_errno(mq_open(p, host_flags, arg3, pposix_mq_attr));
            unlock_user (p, arg1, 0);
        }
        return ret;

    case TARGET_NR_mq_unlink:
        p = lock_user_string(arg1 - 1);
        if (!p) {
            return -TARGET_EFAULT;
        }
        ret = get_errno(mq_unlink(p));
        unlock_user (p, arg1, 0);
        return ret;

#ifdef TARGET_NR_mq_timedsend
    case TARGET_NR_mq_timedsend:
        {
            struct timespec ts;

            p = lock_user (VERIFY_READ, arg2, arg3, 1);
            if (arg5 != 0) {
                if (target_to_host_timespec(&ts, arg5)) {
                    return -TARGET_EFAULT;
                }
                ret = get_errno(safe_mq_timedsend(arg1, p, arg3, arg4, &ts));
                if (!is_error(ret) && host_to_target_timespec(arg5, &ts)) {
                    return -TARGET_EFAULT;
                }
            } else {
                ret = get_errno(safe_mq_timedsend(arg1, p, arg3, arg4, NULL));
            }
            unlock_user (p, arg2, arg3);
        }
        return ret;
#endif
#ifdef TARGET_NR_mq_timedsend_time64
    case TARGET_NR_mq_timedsend_time64:
        {
            struct timespec ts;

            p = lock_user(VERIFY_READ, arg2, arg3, 1);
            if (arg5 != 0) {
                if (target_to_host_timespec64(&ts, arg5)) {
                    return -TARGET_EFAULT;
                }
                ret = get_errno(safe_mq_timedsend(arg1, p, arg3, arg4, &ts));
                if (!is_error(ret) && host_to_target_timespec64(arg5, &ts)) {
                    return -TARGET_EFAULT;
                }
            } else {
                ret = get_errno(safe_mq_timedsend(arg1, p, arg3, arg4, NULL));
            }
            unlock_user(p, arg2, arg3);
        }
        return ret;
#endif

#ifdef TARGET_NR_mq_timedreceive
    case TARGET_NR_mq_timedreceive:
        {
            struct timespec ts;
            unsigned int prio;

            p = lock_user (VERIFY_READ, arg2, arg3, 1);
            if (arg5 != 0) {
                if (target_to_host_timespec(&ts, arg5)) {
                    return -TARGET_EFAULT;
                }
                ret = get_errno(safe_mq_timedreceive(arg1, p, arg3,
                                                     &prio, &ts));
                if (!is_error(ret) && host_to_target_timespec(arg5, &ts)) {
                    return -TARGET_EFAULT;
                }
            } else {
                ret = get_errno(safe_mq_timedreceive(arg1, p, arg3,
                                                     &prio, NULL));
            }
            unlock_user (p, arg2, arg3);
            if (arg4 != 0)
                put_user_u32(prio, arg4);
        }
        return ret;
#endif
#ifdef TARGET_NR_mq_timedreceive_time64
    case TARGET_NR_mq_timedreceive_time64:
        {
            struct timespec ts;
            unsigned int prio;

            p = lock_user(VERIFY_READ, arg2, arg3, 1);
            if (arg5 != 0) {
                if (target_to_host_timespec64(&ts, arg5)) {
                    return -TARGET_EFAULT;
                }
                ret = get_errno(safe_mq_timedreceive(arg1, p, arg3,
                                                     &prio, &ts));
                if (!is_error(ret) && host_to_target_timespec64(arg5, &ts)) {
                    return -TARGET_EFAULT;
                }
            } else {
                ret = get_errno(safe_mq_timedreceive(arg1, p, arg3,
                                                     &prio, NULL));
            }
            unlock_user(p, arg2, arg3);
            if (arg4 != 0) {
                put_user_u32(prio, arg4);
            }
        }
        return ret;
#endif

    /* Not implemented for now... */
/*     case TARGET_NR_mq_notify: */
/*         break; */

    case TARGET_NR_mq_getsetattr:
        {
            struct mq_attr posix_mq_attr_in, posix_mq_attr_out;
            ret = 0;
            if (arg2 != 0) {
                copy_from_user_mq_attr(&posix_mq_attr_in, arg2);
                ret = get_errno(mq_setattr(arg1, &posix_mq_attr_in,
                                           &posix_mq_attr_out));
            } else if (arg3 != 0) {
                ret = get_errno(mq_getattr(arg1, &posix_mq_attr_out));
            }
            if (ret == 0 && arg3 != 0) {
                copy_to_user_mq_attr(arg3, &posix_mq_attr_out);
            }
        }
        return ret;
#endif

#ifdef CONFIG_SPLICE
#ifdef TARGET_NR_tee
    case TARGET_NR_tee:
        {
            ret = get_errno(tee(arg1,arg2,arg3,arg4));
        }
        return ret;
#endif
#ifdef TARGET_NR_splice
    case TARGET_NR_splice:
        {
            loff_t loff_in, loff_out;
            loff_t *ploff_in = NULL, *ploff_out = NULL;
            if (arg2) {
                if (get_user_u64(loff_in, arg2)) {
                    return -TARGET_EFAULT;
                }
                ploff_in = &loff_in;
            }
            if (arg4) {
                if (get_user_u64(loff_out, arg4)) {
                    return -TARGET_EFAULT;
                }
                ploff_out = &loff_out;
            }
            ret = get_errno(splice(arg1, ploff_in, arg3, ploff_out, arg5, arg6));
            if (arg2) {
                if (put_user_u64(loff_in, arg2)) {
                    return -TARGET_EFAULT;
                }
            }
            if (arg4) {
                if (put_user_u64(loff_out, arg4)) {
                    return -TARGET_EFAULT;
                }
            }
        }
        return ret;
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
        return ret;
#endif
#endif /* CONFIG_SPLICE */
#ifdef CONFIG_EVENTFD
#if defined(TARGET_NR_eventfd)
    case TARGET_NR_eventfd:
        ret = get_errno(eventfd(arg1, 0));
        if (ret >= 0) {
            fd_trans_register(ret, &target_eventfd_trans);
        }
        return ret;
#endif
#if defined(TARGET_NR_eventfd2)
    case TARGET_NR_eventfd2:
    {
        int host_flags = arg2 & (~(TARGET_O_NONBLOCK_MASK | TARGET_O_CLOEXEC));
        if (arg2 & TARGET_O_NONBLOCK) {
            host_flags |= O_NONBLOCK;
        }
        if (arg2 & TARGET_O_CLOEXEC) {
            host_flags |= O_CLOEXEC;
        }
        ret = get_errno(eventfd(arg1, host_flags));
        if (ret >= 0) {
            fd_trans_register(ret, &target_eventfd_trans);
        }
        return ret;
    }
#endif
#endif /* CONFIG_EVENTFD  */
#if defined(CONFIG_FALLOCATE) && defined(TARGET_NR_fallocate)
    case TARGET_NR_fallocate:
#if TARGET_ABI_BITS == 32 && !defined(TARGET_ABI_MIPSN32)
        ret = get_errno(fallocate(arg1, arg2, target_offset64(arg3, arg4),
                                  target_offset64(arg5, arg6)));
#else
        ret = get_errno(fallocate(arg1, arg2, arg3, arg4));
#endif
        return ret;
#endif
#if defined(CONFIG_SYNC_FILE_RANGE)
#if defined(TARGET_NR_sync_file_range)
    case TARGET_NR_sync_file_range:
#if TARGET_ABI_BITS == 32 && !defined(TARGET_ABI_MIPSN32)
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
        return ret;
#endif
#if defined(TARGET_NR_sync_file_range2) || \
    defined(TARGET_NR_arm_sync_file_range)
#if defined(TARGET_NR_sync_file_range2)
    case TARGET_NR_sync_file_range2:
#endif
#if defined(TARGET_NR_arm_sync_file_range)
    case TARGET_NR_arm_sync_file_range:
#endif
        /* This is like sync_file_range but the arguments are reordered */
#if TARGET_ABI_BITS == 32 && !defined(TARGET_ABI_MIPSN32)
        ret = get_errno(sync_file_range(arg1, target_offset64(arg3, arg4),
                                        target_offset64(arg5, arg6), arg2));
#else
        ret = get_errno(sync_file_range(arg1, arg3, arg4, arg2));
#endif
        return ret;
#endif
#endif
#if defined(TARGET_NR_signalfd4)
    case TARGET_NR_signalfd4:
        return do_signalfd4(arg1, arg2, arg4);
#endif
#if defined(TARGET_NR_signalfd)
    case TARGET_NR_signalfd:
        return do_signalfd4(arg1, arg2, 0);
#endif
#if defined(CONFIG_EPOLL)
#if defined(TARGET_NR_epoll_create)
    case TARGET_NR_epoll_create:
        return get_errno(epoll_create(arg1));
#endif
#if defined(TARGET_NR_epoll_create1) && defined(CONFIG_EPOLL_CREATE1)
    case TARGET_NR_epoll_create1:
        return get_errno(epoll_create1(target_to_host_bitmask(arg1, fcntl_flags_tbl)));
#endif
#if defined(TARGET_NR_epoll_ctl)
    case TARGET_NR_epoll_ctl:
    {
        struct epoll_event ep;
        struct epoll_event *epp = 0;
        if (arg4) {
            if (arg2 != EPOLL_CTL_DEL) {
                struct target_epoll_event *target_ep;
                if (!lock_user_struct(VERIFY_READ, target_ep, arg4, 1)) {
                    return -TARGET_EFAULT;
                }
                ep.events = tswap32(target_ep->events);
                /*
                 * The epoll_data_t union is just opaque data to the kernel,
                 * so we transfer all 64 bits across and need not worry what
                 * actual data type it is.
                 */
                ep.data.u64 = tswap64(target_ep->data.u64);
                unlock_user_struct(target_ep, arg4, 0);
            }
            /*
             * before kernel 2.6.9, EPOLL_CTL_DEL operation required a
             * non-null pointer, even though this argument is ignored.
             *
             */
            epp = &ep;
        }
        return get_errno(epoll_ctl(arg1, arg2, arg3, epp));
    }
#endif

#if defined(TARGET_NR_epoll_wait) || defined(TARGET_NR_epoll_pwait)
#if defined(TARGET_NR_epoll_wait)
    case TARGET_NR_epoll_wait:
#endif
#if defined(TARGET_NR_epoll_pwait)
    case TARGET_NR_epoll_pwait:
#endif
    {
        struct target_epoll_event *target_ep;
        struct epoll_event *ep;
        int epfd = arg1;
        int maxevents = arg3;
        int timeout = arg4;

        if (maxevents <= 0 || maxevents > TARGET_EP_MAX_EVENTS) {
            return -TARGET_EINVAL;
        }

        target_ep = lock_user(VERIFY_WRITE, arg2,
                              maxevents * sizeof(struct target_epoll_event), 1);
        if (!target_ep) {
            return -TARGET_EFAULT;
        }

        ep = g_try_new(struct epoll_event, maxevents);
        if (!ep) {
            unlock_user(target_ep, arg2, 0);
            return -TARGET_ENOMEM;
        }

        switch (num) {
#if defined(TARGET_NR_epoll_pwait)
        case TARGET_NR_epoll_pwait:
        {
            sigset_t *set = NULL;

            if (arg5) {
                ret = process_sigsuspend_mask(&set, arg5, arg6);
                if (ret != 0) {
                    break;
                }
            }

            ret = get_errno(safe_epoll_pwait(epfd, ep, maxevents, timeout,
                                             set, SIGSET_T_SIZE));

            if (set) {
                finish_sigsuspend_mask(ret);
            }
            break;
        }
#endif
#if defined(TARGET_NR_epoll_wait)
        case TARGET_NR_epoll_wait:
            ret = get_errno(safe_epoll_pwait(epfd, ep, maxevents, timeout,
                                             NULL, 0));
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
            unlock_user(target_ep, arg2,
                        ret * sizeof(struct target_epoll_event));
        } else {
            unlock_user(target_ep, arg2, 0);
        }
        g_free(ep);
        return ret;
    }
#endif
#endif
#ifdef TARGET_NR_prlimit64
    case TARGET_NR_prlimit64:
    {
        /* args: pid, resource number, ptr to new rlimit, ptr to old rlimit */
        struct target_rlimit64 *target_rnew, *target_rold;
        struct host_rlimit64 rnew, rold, *rnewp = 0;
        int resource = target_to_host_resource(arg2);

        if (arg3 && (resource != RLIMIT_AS &&
                     resource != RLIMIT_DATA &&
                     resource != RLIMIT_STACK)) {
            if (!lock_user_struct(VERIFY_READ, target_rnew, arg3, 1)) {
                return -TARGET_EFAULT;
            }
            rnew.rlim_cur = tswap64(target_rnew->rlim_cur);
            rnew.rlim_max = tswap64(target_rnew->rlim_max);
            unlock_user_struct(target_rnew, arg3, 0);
            rnewp = &rnew;
        }

        ret = get_errno(sys_prlimit64(arg1, resource, rnewp, arg4 ? &rold : 0));
        if (!is_error(ret) && arg4) {
            if (!lock_user_struct(VERIFY_WRITE, target_rold, arg4, 1)) {
                return -TARGET_EFAULT;
            }
            target_rold->rlim_cur = tswap64(rold.rlim_cur);
            target_rold->rlim_max = tswap64(rold.rlim_max);
            unlock_user_struct(target_rold, arg4, 1);
        }
        return ret;
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
        return ret;
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
            queue_signal(cpu_env, info.si_signo, QEMU_SI_FAULT, &info);
            ret = 0xdeadbeef;

        }
        if (mem_value == arg2)
            put_user_u32(arg1, arg6);
        return mem_value;
    }
#endif
#ifdef TARGET_NR_atomic_barrier
    case TARGET_NR_atomic_barrier:
        /* Like the kernel implementation and the
           qemu arm barrier, no-op this? */
        return 0;
#endif

#ifdef TARGET_NR_timer_create
    case TARGET_NR_timer_create:
    {
        /* args: clockid_t clockid, struct sigevent *sevp, timer_t *timerid */

        struct sigevent host_sevp = { {0}, }, *phost_sevp = NULL;

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
                    free_host_timer_slot(timer_index);
                    return ret;
                }
            }

            ret = get_errno(timer_create(clkid, phost_sevp, phtimer));
            if (ret) {
                free_host_timer_slot(timer_index);
            } else {
                if (put_user(TIMER_MAGIC | timer_index, arg3, target_timer_t)) {
                    timer_delete(*phtimer);
                    free_host_timer_slot(timer_index);
                    return -TARGET_EFAULT;
                }
            }
        }
        return ret;
    }
#endif

#ifdef TARGET_NR_timer_settime
    case TARGET_NR_timer_settime:
    {
        /* args: timer_t timerid, int flags, const struct itimerspec *new_value,
         * struct itimerspec * old_value */
        target_timer_t timerid = get_timer_id(arg1);

        if (timerid < 0) {
            ret = timerid;
        } else if (arg3 == 0) {
            ret = -TARGET_EINVAL;
        } else {
            timer_t htimer = g_posix_timers[timerid];
            struct itimerspec hspec_new = {{0},}, hspec_old = {{0},};

            if (target_to_host_itimerspec(&hspec_new, arg3)) {
                return -TARGET_EFAULT;
            }
            ret = get_errno(
                          timer_settime(htimer, arg2, &hspec_new, &hspec_old));
            if (arg4 && host_to_target_itimerspec(arg4, &hspec_old)) {
                return -TARGET_EFAULT;
            }
        }
        return ret;
    }
#endif

#ifdef TARGET_NR_timer_settime64
    case TARGET_NR_timer_settime64:
    {
        target_timer_t timerid = get_timer_id(arg1);

        if (timerid < 0) {
            ret = timerid;
        } else if (arg3 == 0) {
            ret = -TARGET_EINVAL;
        } else {
            timer_t htimer = g_posix_timers[timerid];
            struct itimerspec hspec_new = {{0},}, hspec_old = {{0},};

            if (target_to_host_itimerspec64(&hspec_new, arg3)) {
                return -TARGET_EFAULT;
            }
            ret = get_errno(
                          timer_settime(htimer, arg2, &hspec_new, &hspec_old));
            if (arg4 && host_to_target_itimerspec64(arg4, &hspec_old)) {
                return -TARGET_EFAULT;
            }
        }
        return ret;
    }
#endif

#ifdef TARGET_NR_timer_gettime
    case TARGET_NR_timer_gettime:
    {
        /* args: timer_t timerid, struct itimerspec *curr_value */
        target_timer_t timerid = get_timer_id(arg1);

        if (timerid < 0) {
            ret = timerid;
        } else if (!arg2) {
            ret = -TARGET_EFAULT;
        } else {
            timer_t htimer = g_posix_timers[timerid];
            struct itimerspec hspec;
            ret = get_errno(timer_gettime(htimer, &hspec));

            if (host_to_target_itimerspec(arg2, &hspec)) {
                ret = -TARGET_EFAULT;
            }
        }
        return ret;
    }
#endif

#ifdef TARGET_NR_timer_gettime64
    case TARGET_NR_timer_gettime64:
    {
        /* args: timer_t timerid, struct itimerspec64 *curr_value */
        target_timer_t timerid = get_timer_id(arg1);

        if (timerid < 0) {
            ret = timerid;
        } else if (!arg2) {
            ret = -TARGET_EFAULT;
        } else {
            timer_t htimer = g_posix_timers[timerid];
            struct itimerspec hspec;
            ret = get_errno(timer_gettime(htimer, &hspec));

            if (host_to_target_itimerspec64(arg2, &hspec)) {
                ret = -TARGET_EFAULT;
            }
        }
        return ret;
    }
#endif

#ifdef TARGET_NR_timer_getoverrun
    case TARGET_NR_timer_getoverrun:
    {
        /* args: timer_t timerid */
        target_timer_t timerid = get_timer_id(arg1);

        if (timerid < 0) {
            ret = timerid;
        } else {
            timer_t htimer = g_posix_timers[timerid];
            ret = get_errno(timer_getoverrun(htimer));
        }
        return ret;
    }
#endif

#ifdef TARGET_NR_timer_delete
    case TARGET_NR_timer_delete:
    {
        /* args: timer_t timerid */
        target_timer_t timerid = get_timer_id(arg1);

        if (timerid < 0) {
            ret = timerid;
        } else {
            timer_t htimer = g_posix_timers[timerid];
            ret = get_errno(timer_delete(htimer));
            free_host_timer_slot(timerid);
        }
        return ret;
    }
#endif

#if defined(TARGET_NR_timerfd_create) && defined(CONFIG_TIMERFD)
    case TARGET_NR_timerfd_create:
        return get_errno(timerfd_create(arg1,
                          target_to_host_bitmask(arg2, fcntl_flags_tbl)));
#endif

#if defined(TARGET_NR_timerfd_gettime) && defined(CONFIG_TIMERFD)
    case TARGET_NR_timerfd_gettime:
        {
            struct itimerspec its_curr;

            ret = get_errno(timerfd_gettime(arg1, &its_curr));

            if (arg2 && host_to_target_itimerspec(arg2, &its_curr)) {
                return -TARGET_EFAULT;
            }
        }
        return ret;
#endif

#if defined(TARGET_NR_timerfd_gettime64) && defined(CONFIG_TIMERFD)
    case TARGET_NR_timerfd_gettime64:
        {
            struct itimerspec its_curr;

            ret = get_errno(timerfd_gettime(arg1, &its_curr));

            if (arg2 && host_to_target_itimerspec64(arg2, &its_curr)) {
                return -TARGET_EFAULT;
            }
        }
        return ret;
#endif

#if defined(TARGET_NR_timerfd_settime) && defined(CONFIG_TIMERFD)
    case TARGET_NR_timerfd_settime:
        {
            struct itimerspec its_new, its_old, *p_new;

            if (arg3) {
                if (target_to_host_itimerspec(&its_new, arg3)) {
                    return -TARGET_EFAULT;
                }
                p_new = &its_new;
            } else {
                p_new = NULL;
            }

            ret = get_errno(timerfd_settime(arg1, arg2, p_new, &its_old));

            if (arg4 && host_to_target_itimerspec(arg4, &its_old)) {
                return -TARGET_EFAULT;
            }
        }
        return ret;
#endif

#if defined(TARGET_NR_timerfd_settime64) && defined(CONFIG_TIMERFD)
    case TARGET_NR_timerfd_settime64:
        {
            struct itimerspec its_new, its_old, *p_new;

            if (arg3) {
                if (target_to_host_itimerspec64(&its_new, arg3)) {
                    return -TARGET_EFAULT;
                }
                p_new = &its_new;
            } else {
                p_new = NULL;
            }

            ret = get_errno(timerfd_settime(arg1, arg2, p_new, &its_old));

            if (arg4 && host_to_target_itimerspec64(arg4, &its_old)) {
                return -TARGET_EFAULT;
            }
        }
        return ret;
#endif

#if defined(TARGET_NR_ioprio_get) && defined(__NR_ioprio_get)
    case TARGET_NR_ioprio_get:
        return get_errno(ioprio_get(arg1, arg2));
#endif

#if defined(TARGET_NR_ioprio_set) && defined(__NR_ioprio_set)
    case TARGET_NR_ioprio_set:
        return get_errno(ioprio_set(arg1, arg2, arg3));
#endif

#if defined(TARGET_NR_setns) && defined(CONFIG_SETNS)
    case TARGET_NR_setns:
        return get_errno(setns(arg1, arg2));
#endif
#if defined(TARGET_NR_unshare) && defined(CONFIG_SETNS)
    case TARGET_NR_unshare:
        return get_errno(unshare(arg1));
#endif
#if defined(TARGET_NR_kcmp) && defined(__NR_kcmp)
    case TARGET_NR_kcmp:
        return get_errno(kcmp(arg1, arg2, arg3, arg4, arg5));
#endif
#ifdef TARGET_NR_swapcontext
    case TARGET_NR_swapcontext:
        /* PowerPC specific.  */
        return do_swapcontext(cpu_env, arg1, arg2, arg3);
#endif
#ifdef TARGET_NR_memfd_create
    case TARGET_NR_memfd_create:
        p = lock_user_string(arg1);
        if (!p) {
            return -TARGET_EFAULT;
        }
        ret = get_errno(memfd_create(p, arg2));
        fd_trans_unregister(ret);
        unlock_user(p, arg1, 0);
        return ret;
#endif
#if defined TARGET_NR_membarrier && defined __NR_membarrier
    case TARGET_NR_membarrier:
        return get_errno(membarrier(arg1, arg2));
#endif

#if defined(TARGET_NR_copy_file_range) && defined(__NR_copy_file_range)
    case TARGET_NR_copy_file_range:
        {
            loff_t inoff, outoff;
            loff_t *pinoff = NULL, *poutoff = NULL;

            if (arg2) {
                if (get_user_u64(inoff, arg2)) {
                    return -TARGET_EFAULT;
                }
                pinoff = &inoff;
            }
            if (arg4) {
                if (get_user_u64(outoff, arg4)) {
                    return -TARGET_EFAULT;
                }
                poutoff = &outoff;
            }
            /* Do not sign-extend the count parameter. */
            ret = get_errno(safe_copy_file_range(arg1, pinoff, arg3, poutoff,
                                                 (abi_ulong)arg5, arg6));
            if (!is_error(ret) && ret > 0) {
                if (arg2) {
                    if (put_user_u64(inoff, arg2)) {
                        return -TARGET_EFAULT;
                    }
                }
                if (arg4) {
                    if (put_user_u64(outoff, arg4)) {
                        return -TARGET_EFAULT;
                    }
                }
            }
        }
        return ret;
#endif

#if defined(TARGET_NR_pivot_root)
    case TARGET_NR_pivot_root:
        {
            void *p2;
            p = lock_user_string(arg1); /* new_root */
            p2 = lock_user_string(arg2); /* put_old */
            if (!p || !p2) {
                ret = -TARGET_EFAULT;
            } else {
                ret = get_errno(pivot_root(p, p2));
            }
            unlock_user(p2, arg2, 0);
            unlock_user(p, arg1, 0);
        }
        return ret;
#endif

    default:
        qemu_log_mask(LOG_UNIMP, "Unsupported syscall: %d\n", num);
        return -TARGET_ENOSYS;
    }
    return ret;
}

abi_long do_syscall(CPUArchState *cpu_env, int num, abi_long arg1,
                    abi_long arg2, abi_long arg3, abi_long arg4,
                    abi_long arg5, abi_long arg6, abi_long arg7,
                    abi_long arg8)
{
    CPUState *cpu = env_cpu(cpu_env);
    abi_long ret;

#ifdef DEBUG_ERESTARTSYS
    /* Debug-only code for exercising the syscall-restart code paths
     * in the per-architecture cpu main loops: restart every syscall
     * the guest makes once before letting it through.
     */
    {
        static bool flag;
        flag = !flag;
        if (flag) {
            return -QEMU_ERESTARTSYS;
        }
    }
#endif

    record_syscall_start(cpu, num, arg1,
                         arg2, arg3, arg4, arg5, arg6, arg7, arg8);

    if (unlikely(qemu_loglevel_mask(LOG_STRACE))) {
        print_syscall(cpu_env, num, arg1, arg2, arg3, arg4, arg5, arg6);
    }

    ret = do_syscall1(cpu_env, num, arg1, arg2, arg3, arg4,
                      arg5, arg6, arg7, arg8);

    if (unlikely(qemu_loglevel_mask(LOG_STRACE))) {
        print_syscall_ret(cpu_env, num, ret, arg1, arg2,
                          arg3, arg4, arg5, arg6);
    }

    record_syscall_return(cpu, num, ret);
    return ret;
}

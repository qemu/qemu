/*
 *  qemu user cpu loop
 *
 *  Copyright (c) 2003-2008 Fabrice Bellard
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

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu.h"
#include "cpu_loop-common.h"
#include "elf.h"

# ifdef TARGET_ABI_MIPSO32
#  define MIPS_SYS(name, args) args,
static const uint8_t mips_syscall_args[] = {
        MIPS_SYS(sys_syscall    , 8)    /* 4000 */
        MIPS_SYS(sys_exit       , 1)
        MIPS_SYS(sys_fork       , 0)
        MIPS_SYS(sys_read       , 3)
        MIPS_SYS(sys_write      , 3)
        MIPS_SYS(sys_open       , 3)    /* 4005 */
        MIPS_SYS(sys_close      , 1)
        MIPS_SYS(sys_waitpid    , 3)
        MIPS_SYS(sys_creat      , 2)
        MIPS_SYS(sys_link       , 2)
        MIPS_SYS(sys_unlink     , 1)    /* 4010 */
        MIPS_SYS(sys_execve     , 0)
        MIPS_SYS(sys_chdir      , 1)
        MIPS_SYS(sys_time       , 1)
        MIPS_SYS(sys_mknod      , 3)
        MIPS_SYS(sys_chmod      , 2)    /* 4015 */
        MIPS_SYS(sys_lchown     , 3)
        MIPS_SYS(sys_ni_syscall , 0)
        MIPS_SYS(sys_ni_syscall , 0)    /* was sys_stat */
        MIPS_SYS(sys_lseek      , 3)
        MIPS_SYS(sys_getpid     , 0)    /* 4020 */
        MIPS_SYS(sys_mount      , 5)
        MIPS_SYS(sys_umount     , 1)
        MIPS_SYS(sys_setuid     , 1)
        MIPS_SYS(sys_getuid     , 0)
        MIPS_SYS(sys_stime      , 1)    /* 4025 */
        MIPS_SYS(sys_ptrace     , 4)
        MIPS_SYS(sys_alarm      , 1)
        MIPS_SYS(sys_ni_syscall , 0)    /* was sys_fstat */
        MIPS_SYS(sys_pause      , 0)
        MIPS_SYS(sys_utime      , 2)    /* 4030 */
        MIPS_SYS(sys_ni_syscall , 0)
        MIPS_SYS(sys_ni_syscall , 0)
        MIPS_SYS(sys_access     , 2)
        MIPS_SYS(sys_nice       , 1)
        MIPS_SYS(sys_ni_syscall , 0)    /* 4035 */
        MIPS_SYS(sys_sync       , 0)
        MIPS_SYS(sys_kill       , 2)
        MIPS_SYS(sys_rename     , 2)
        MIPS_SYS(sys_mkdir      , 2)
        MIPS_SYS(sys_rmdir      , 1)    /* 4040 */
        MIPS_SYS(sys_dup                , 1)
        MIPS_SYS(sys_pipe       , 0)
        MIPS_SYS(sys_times      , 1)
        MIPS_SYS(sys_ni_syscall , 0)
        MIPS_SYS(sys_brk                , 1)    /* 4045 */
        MIPS_SYS(sys_setgid     , 1)
        MIPS_SYS(sys_getgid     , 0)
        MIPS_SYS(sys_ni_syscall , 0)    /* was signal(2) */
        MIPS_SYS(sys_geteuid    , 0)
        MIPS_SYS(sys_getegid    , 0)    /* 4050 */
        MIPS_SYS(sys_acct       , 0)
        MIPS_SYS(sys_umount2    , 2)
        MIPS_SYS(sys_ni_syscall , 0)
        MIPS_SYS(sys_ioctl      , 3)
        MIPS_SYS(sys_fcntl      , 3)    /* 4055 */
        MIPS_SYS(sys_ni_syscall , 2)
        MIPS_SYS(sys_setpgid    , 2)
        MIPS_SYS(sys_ni_syscall , 0)
        MIPS_SYS(sys_olduname   , 1)
        MIPS_SYS(sys_umask      , 1)    /* 4060 */
        MIPS_SYS(sys_chroot     , 1)
        MIPS_SYS(sys_ustat      , 2)
        MIPS_SYS(sys_dup2       , 2)
        MIPS_SYS(sys_getppid    , 0)
        MIPS_SYS(sys_getpgrp    , 0)    /* 4065 */
        MIPS_SYS(sys_setsid     , 0)
        MIPS_SYS(sys_sigaction  , 3)
        MIPS_SYS(sys_sgetmask   , 0)
        MIPS_SYS(sys_ssetmask   , 1)
        MIPS_SYS(sys_setreuid   , 2)    /* 4070 */
        MIPS_SYS(sys_setregid   , 2)
        MIPS_SYS(sys_sigsuspend , 0)
        MIPS_SYS(sys_sigpending , 1)
        MIPS_SYS(sys_sethostname        , 2)
        MIPS_SYS(sys_setrlimit  , 2)    /* 4075 */
        MIPS_SYS(sys_getrlimit  , 2)
        MIPS_SYS(sys_getrusage  , 2)
        MIPS_SYS(sys_gettimeofday, 2)
        MIPS_SYS(sys_settimeofday, 2)
        MIPS_SYS(sys_getgroups  , 2)    /* 4080 */
        MIPS_SYS(sys_setgroups  , 2)
        MIPS_SYS(sys_ni_syscall , 0)    /* old_select */
        MIPS_SYS(sys_symlink    , 2)
        MIPS_SYS(sys_ni_syscall , 0)    /* was sys_lstat */
        MIPS_SYS(sys_readlink   , 3)    /* 4085 */
        MIPS_SYS(sys_uselib     , 1)
        MIPS_SYS(sys_swapon     , 2)
        MIPS_SYS(sys_reboot     , 3)
        MIPS_SYS(old_readdir    , 3)
        MIPS_SYS(old_mmap       , 6)    /* 4090 */
        MIPS_SYS(sys_munmap     , 2)
        MIPS_SYS(sys_truncate   , 2)
        MIPS_SYS(sys_ftruncate  , 2)
        MIPS_SYS(sys_fchmod     , 2)
        MIPS_SYS(sys_fchown     , 3)    /* 4095 */
        MIPS_SYS(sys_getpriority        , 2)
        MIPS_SYS(sys_setpriority        , 3)
        MIPS_SYS(sys_ni_syscall , 0)
        MIPS_SYS(sys_statfs     , 2)
        MIPS_SYS(sys_fstatfs    , 2)    /* 4100 */
        MIPS_SYS(sys_ni_syscall , 0)    /* was ioperm(2) */
        MIPS_SYS(sys_socketcall , 2)
        MIPS_SYS(sys_syslog     , 3)
        MIPS_SYS(sys_setitimer  , 3)
        MIPS_SYS(sys_getitimer  , 2)    /* 4105 */
        MIPS_SYS(sys_newstat    , 2)
        MIPS_SYS(sys_newlstat   , 2)
        MIPS_SYS(sys_newfstat   , 2)
        MIPS_SYS(sys_uname      , 1)
        MIPS_SYS(sys_ni_syscall , 0)    /* 4110 was iopl(2) */
        MIPS_SYS(sys_vhangup    , 0)
        MIPS_SYS(sys_ni_syscall , 0)    /* was sys_idle() */
        MIPS_SYS(sys_ni_syscall , 0)    /* was sys_vm86 */
        MIPS_SYS(sys_wait4      , 4)
        MIPS_SYS(sys_swapoff    , 1)    /* 4115 */
        MIPS_SYS(sys_sysinfo    , 1)
        MIPS_SYS(sys_ipc                , 6)
        MIPS_SYS(sys_fsync      , 1)
        MIPS_SYS(sys_sigreturn  , 0)
        MIPS_SYS(sys_clone      , 6)    /* 4120 */
        MIPS_SYS(sys_setdomainname, 2)
        MIPS_SYS(sys_newuname   , 1)
        MIPS_SYS(sys_ni_syscall , 0)    /* sys_modify_ldt */
        MIPS_SYS(sys_adjtimex   , 1)
        MIPS_SYS(sys_mprotect   , 3)    /* 4125 */
        MIPS_SYS(sys_sigprocmask        , 3)
        MIPS_SYS(sys_ni_syscall , 0)    /* was create_module */
        MIPS_SYS(sys_init_module        , 5)
        MIPS_SYS(sys_delete_module, 1)
        MIPS_SYS(sys_ni_syscall , 0)    /* 4130 was get_kernel_syms */
        MIPS_SYS(sys_quotactl   , 0)
        MIPS_SYS(sys_getpgid    , 1)
        MIPS_SYS(sys_fchdir     , 1)
        MIPS_SYS(sys_bdflush    , 2)
        MIPS_SYS(sys_sysfs      , 3)    /* 4135 */
        MIPS_SYS(sys_personality        , 1)
        MIPS_SYS(sys_ni_syscall , 0)    /* for afs_syscall */
        MIPS_SYS(sys_setfsuid   , 1)
        MIPS_SYS(sys_setfsgid   , 1)
        MIPS_SYS(sys_llseek     , 5)    /* 4140 */
        MIPS_SYS(sys_getdents   , 3)
        MIPS_SYS(sys_select     , 5)
        MIPS_SYS(sys_flock      , 2)
        MIPS_SYS(sys_msync      , 3)
        MIPS_SYS(sys_readv      , 3)    /* 4145 */
        MIPS_SYS(sys_writev     , 3)
        MIPS_SYS(sys_cacheflush , 3)
        MIPS_SYS(sys_cachectl   , 3)
        MIPS_SYS(sys_sysmips    , 4)
        MIPS_SYS(sys_ni_syscall , 0)    /* 4150 */
        MIPS_SYS(sys_getsid     , 1)
        MIPS_SYS(sys_fdatasync  , 0)
        MIPS_SYS(sys_sysctl     , 1)
        MIPS_SYS(sys_mlock      , 2)
        MIPS_SYS(sys_munlock    , 2)    /* 4155 */
        MIPS_SYS(sys_mlockall   , 1)
        MIPS_SYS(sys_munlockall , 0)
        MIPS_SYS(sys_sched_setparam, 2)
        MIPS_SYS(sys_sched_getparam, 2)
        MIPS_SYS(sys_sched_setscheduler, 3)     /* 4160 */
        MIPS_SYS(sys_sched_getscheduler, 1)
        MIPS_SYS(sys_sched_yield        , 0)
        MIPS_SYS(sys_sched_get_priority_max, 1)
        MIPS_SYS(sys_sched_get_priority_min, 1)
        MIPS_SYS(sys_sched_rr_get_interval, 2)  /* 4165 */
        MIPS_SYS(sys_nanosleep, 2)
        MIPS_SYS(sys_mremap     , 5)
        MIPS_SYS(sys_accept     , 3)
        MIPS_SYS(sys_bind       , 3)
        MIPS_SYS(sys_connect    , 3)    /* 4170 */
        MIPS_SYS(sys_getpeername        , 3)
        MIPS_SYS(sys_getsockname        , 3)
        MIPS_SYS(sys_getsockopt , 5)
        MIPS_SYS(sys_listen     , 2)
        MIPS_SYS(sys_recv       , 4)    /* 4175 */
        MIPS_SYS(sys_recvfrom   , 6)
        MIPS_SYS(sys_recvmsg    , 3)
        MIPS_SYS(sys_send       , 4)
        MIPS_SYS(sys_sendmsg    , 3)
        MIPS_SYS(sys_sendto     , 6)    /* 4180 */
        MIPS_SYS(sys_setsockopt , 5)
        MIPS_SYS(sys_shutdown   , 2)
        MIPS_SYS(sys_socket     , 3)
        MIPS_SYS(sys_socketpair , 4)
        MIPS_SYS(sys_setresuid  , 3)    /* 4185 */
        MIPS_SYS(sys_getresuid  , 3)
        MIPS_SYS(sys_ni_syscall , 0)    /* was sys_query_module */
        MIPS_SYS(sys_poll       , 3)
        MIPS_SYS(sys_nfsservctl , 3)
        MIPS_SYS(sys_setresgid  , 3)    /* 4190 */
        MIPS_SYS(sys_getresgid  , 3)
        MIPS_SYS(sys_prctl      , 5)
        MIPS_SYS(sys_rt_sigreturn, 0)
        MIPS_SYS(sys_rt_sigaction, 4)
        MIPS_SYS(sys_rt_sigprocmask, 4) /* 4195 */
        MIPS_SYS(sys_rt_sigpending, 2)
        MIPS_SYS(sys_rt_sigtimedwait, 4)
        MIPS_SYS(sys_rt_sigqueueinfo, 3)
        MIPS_SYS(sys_rt_sigsuspend, 0)
        MIPS_SYS(sys_pread64    , 6)    /* 4200 */
        MIPS_SYS(sys_pwrite64   , 6)
        MIPS_SYS(sys_chown      , 3)
        MIPS_SYS(sys_getcwd     , 2)
        MIPS_SYS(sys_capget     , 2)
        MIPS_SYS(sys_capset     , 2)    /* 4205 */
        MIPS_SYS(sys_sigaltstack        , 2)
        MIPS_SYS(sys_sendfile   , 4)
        MIPS_SYS(sys_ni_syscall , 0)
        MIPS_SYS(sys_ni_syscall , 0)
        MIPS_SYS(sys_mmap2      , 6)    /* 4210 */
        MIPS_SYS(sys_truncate64 , 4)
        MIPS_SYS(sys_ftruncate64        , 4)
        MIPS_SYS(sys_stat64     , 2)
        MIPS_SYS(sys_lstat64    , 2)
        MIPS_SYS(sys_fstat64    , 2)    /* 4215 */
        MIPS_SYS(sys_pivot_root , 2)
        MIPS_SYS(sys_mincore    , 3)
        MIPS_SYS(sys_madvise    , 3)
        MIPS_SYS(sys_getdents64 , 3)
        MIPS_SYS(sys_fcntl64    , 3)    /* 4220 */
        MIPS_SYS(sys_ni_syscall , 0)
        MIPS_SYS(sys_gettid     , 0)
        MIPS_SYS(sys_readahead  , 5)
        MIPS_SYS(sys_setxattr   , 5)
        MIPS_SYS(sys_lsetxattr  , 5)    /* 4225 */
        MIPS_SYS(sys_fsetxattr  , 5)
        MIPS_SYS(sys_getxattr   , 4)
        MIPS_SYS(sys_lgetxattr  , 4)
        MIPS_SYS(sys_fgetxattr  , 4)
        MIPS_SYS(sys_listxattr  , 3)    /* 4230 */
        MIPS_SYS(sys_llistxattr , 3)
        MIPS_SYS(sys_flistxattr , 3)
        MIPS_SYS(sys_removexattr        , 2)
        MIPS_SYS(sys_lremovexattr, 2)
        MIPS_SYS(sys_fremovexattr, 2)   /* 4235 */
        MIPS_SYS(sys_tkill      , 2)
        MIPS_SYS(sys_sendfile64 , 5)
        MIPS_SYS(sys_futex      , 6)
        MIPS_SYS(sys_sched_setaffinity, 3)
        MIPS_SYS(sys_sched_getaffinity, 3)      /* 4240 */
        MIPS_SYS(sys_io_setup   , 2)
        MIPS_SYS(sys_io_destroy , 1)
        MIPS_SYS(sys_io_getevents, 5)
        MIPS_SYS(sys_io_submit  , 3)
        MIPS_SYS(sys_io_cancel  , 3)    /* 4245 */
        MIPS_SYS(sys_exit_group , 1)
        MIPS_SYS(sys_lookup_dcookie, 3)
        MIPS_SYS(sys_epoll_create, 1)
        MIPS_SYS(sys_epoll_ctl  , 4)
        MIPS_SYS(sys_epoll_wait , 3)    /* 4250 */
        MIPS_SYS(sys_remap_file_pages, 5)
        MIPS_SYS(sys_set_tid_address, 1)
        MIPS_SYS(sys_restart_syscall, 0)
        MIPS_SYS(sys_fadvise64_64, 7)
        MIPS_SYS(sys_statfs64   , 3)    /* 4255 */
        MIPS_SYS(sys_fstatfs64  , 2)
        MIPS_SYS(sys_timer_create, 3)
        MIPS_SYS(sys_timer_settime, 4)
        MIPS_SYS(sys_timer_gettime, 2)
        MIPS_SYS(sys_timer_getoverrun, 1)       /* 4260 */
        MIPS_SYS(sys_timer_delete, 1)
        MIPS_SYS(sys_clock_settime, 2)
        MIPS_SYS(sys_clock_gettime, 2)
        MIPS_SYS(sys_clock_getres, 2)
        MIPS_SYS(sys_clock_nanosleep, 4)        /* 4265 */
        MIPS_SYS(sys_tgkill     , 3)
        MIPS_SYS(sys_utimes     , 2)
        MIPS_SYS(sys_mbind      , 4)
        MIPS_SYS(sys_ni_syscall , 0)    /* sys_get_mempolicy */
        MIPS_SYS(sys_ni_syscall , 0)    /* 4270 sys_set_mempolicy */
        MIPS_SYS(sys_mq_open    , 4)
        MIPS_SYS(sys_mq_unlink  , 1)
        MIPS_SYS(sys_mq_timedsend, 5)
        MIPS_SYS(sys_mq_timedreceive, 5)
        MIPS_SYS(sys_mq_notify  , 2)    /* 4275 */
        MIPS_SYS(sys_mq_getsetattr, 3)
        MIPS_SYS(sys_ni_syscall , 0)    /* sys_vserver */
        MIPS_SYS(sys_waitid     , 4)
        MIPS_SYS(sys_ni_syscall , 0)    /* available, was setaltroot */
        MIPS_SYS(sys_add_key    , 5)
        MIPS_SYS(sys_request_key, 4)
        MIPS_SYS(sys_keyctl     , 5)
        MIPS_SYS(sys_set_thread_area, 1)
        MIPS_SYS(sys_inotify_init, 0)
        MIPS_SYS(sys_inotify_add_watch, 3) /* 4285 */
        MIPS_SYS(sys_inotify_rm_watch, 2)
        MIPS_SYS(sys_migrate_pages, 4)
        MIPS_SYS(sys_openat, 4)
        MIPS_SYS(sys_mkdirat, 3)
        MIPS_SYS(sys_mknodat, 4)        /* 4290 */
        MIPS_SYS(sys_fchownat, 5)
        MIPS_SYS(sys_futimesat, 3)
        MIPS_SYS(sys_fstatat64, 4)
        MIPS_SYS(sys_unlinkat, 3)
        MIPS_SYS(sys_renameat, 4)       /* 4295 */
        MIPS_SYS(sys_linkat, 5)
        MIPS_SYS(sys_symlinkat, 3)
        MIPS_SYS(sys_readlinkat, 4)
        MIPS_SYS(sys_fchmodat, 3)
        MIPS_SYS(sys_faccessat, 3)      /* 4300 */
        MIPS_SYS(sys_pselect6, 6)
        MIPS_SYS(sys_ppoll, 5)
        MIPS_SYS(sys_unshare, 1)
        MIPS_SYS(sys_splice, 6)
        MIPS_SYS(sys_sync_file_range, 7) /* 4305 */
        MIPS_SYS(sys_tee, 4)
        MIPS_SYS(sys_vmsplice, 4)
        MIPS_SYS(sys_move_pages, 6)
        MIPS_SYS(sys_set_robust_list, 2)
        MIPS_SYS(sys_get_robust_list, 3) /* 4310 */
        MIPS_SYS(sys_kexec_load, 4)
        MIPS_SYS(sys_getcpu, 3)
        MIPS_SYS(sys_epoll_pwait, 6)
        MIPS_SYS(sys_ioprio_set, 3)
        MIPS_SYS(sys_ioprio_get, 2)
        MIPS_SYS(sys_utimensat, 4)
        MIPS_SYS(sys_signalfd, 3)
        MIPS_SYS(sys_ni_syscall, 0)     /* was timerfd */
        MIPS_SYS(sys_eventfd, 1)
        MIPS_SYS(sys_fallocate, 6)      /* 4320 */
        MIPS_SYS(sys_timerfd_create, 2)
        MIPS_SYS(sys_timerfd_gettime, 2)
        MIPS_SYS(sys_timerfd_settime, 4)
        MIPS_SYS(sys_signalfd4, 4)
        MIPS_SYS(sys_eventfd2, 2)       /* 4325 */
        MIPS_SYS(sys_epoll_create1, 1)
        MIPS_SYS(sys_dup3, 3)
        MIPS_SYS(sys_pipe2, 2)
        MIPS_SYS(sys_inotify_init1, 1)
        MIPS_SYS(sys_preadv, 5)         /* 4330 */
        MIPS_SYS(sys_pwritev, 5)
        MIPS_SYS(sys_rt_tgsigqueueinfo, 4)
        MIPS_SYS(sys_perf_event_open, 5)
        MIPS_SYS(sys_accept4, 4)
        MIPS_SYS(sys_recvmmsg, 5)       /* 4335 */
        MIPS_SYS(sys_fanotify_init, 2)
        MIPS_SYS(sys_fanotify_mark, 6)
        MIPS_SYS(sys_prlimit64, 4)
        MIPS_SYS(sys_name_to_handle_at, 5)
        MIPS_SYS(sys_open_by_handle_at, 3) /* 4340 */
        MIPS_SYS(sys_clock_adjtime, 2)
        MIPS_SYS(sys_syncfs, 1)
        MIPS_SYS(sys_sendmmsg, 4)
        MIPS_SYS(sys_setns, 2)
        MIPS_SYS(sys_process_vm_readv, 6) /* 345 */
        MIPS_SYS(sys_process_vm_writev, 6)
        MIPS_SYS(sys_kcmp, 5)
        MIPS_SYS(sys_finit_module, 3)
        MIPS_SYS(sys_sched_setattr, 2)
        MIPS_SYS(sys_sched_getattr, 3)  /* 350 */
        MIPS_SYS(sys_renameat2, 5)
        MIPS_SYS(sys_seccomp, 3)
        MIPS_SYS(sys_getrandom, 3)
        MIPS_SYS(sys_memfd_create, 2)
        MIPS_SYS(sys_bpf, 3)            /* 355 */
        MIPS_SYS(sys_execveat, 5)
        MIPS_SYS(sys_userfaultfd, 1)
        MIPS_SYS(sys_membarrier, 2)
        MIPS_SYS(sys_mlock2, 3)
        MIPS_SYS(sys_copy_file_range, 6) /* 360 */
        MIPS_SYS(sys_preadv2, 6)
        MIPS_SYS(sys_pwritev2, 6)
};
#  undef MIPS_SYS
# endif /* O32 */

/* Break codes */
enum {
    BRK_OVERFLOW = 6,
    BRK_DIVZERO = 7
};

static int do_break(CPUMIPSState *env, target_siginfo_t *info,
                    unsigned int code)
{
    int ret = -1;

    switch (code) {
    case BRK_OVERFLOW:
    case BRK_DIVZERO:
        info->si_signo = TARGET_SIGFPE;
        info->si_errno = 0;
        info->si_code = (code == BRK_OVERFLOW) ? FPE_INTOVF : FPE_INTDIV;
        queue_signal(env, info->si_signo, QEMU_SI_FAULT, &*info);
        ret = 0;
        break;
    default:
        info->si_signo = TARGET_SIGTRAP;
        info->si_errno = 0;
        queue_signal(env, info->si_signo, QEMU_SI_FAULT, &*info);
        ret = 0;
        break;
    }

    return ret;
}

void cpu_loop(CPUMIPSState *env)
{
    CPUState *cs = env_cpu(env);
    target_siginfo_t info;
    int trapnr;
    abi_long ret;
# ifdef TARGET_ABI_MIPSO32
    unsigned int syscall_num;
# endif

    for(;;) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        process_queued_cpu_work(cs);

        switch(trapnr) {
        case EXCP_SYSCALL:
            env->active_tc.PC += 4;
# ifdef TARGET_ABI_MIPSO32
            syscall_num = env->active_tc.gpr[2] - 4000;
            if (syscall_num >= sizeof(mips_syscall_args)) {
                ret = -TARGET_ENOSYS;
            } else {
                int nb_args;
                abi_ulong sp_reg;
                abi_ulong arg5 = 0, arg6 = 0, arg7 = 0, arg8 = 0;

                nb_args = mips_syscall_args[syscall_num];
                sp_reg = env->active_tc.gpr[29];
                switch (nb_args) {
                /* these arguments are taken from the stack */
                case 8:
                    if ((ret = get_user_ual(arg8, sp_reg + 28)) != 0) {
                        goto done_syscall;
                    }
                case 7:
                    if ((ret = get_user_ual(arg7, sp_reg + 24)) != 0) {
                        goto done_syscall;
                    }
                case 6:
                    if ((ret = get_user_ual(arg6, sp_reg + 20)) != 0) {
                        goto done_syscall;
                    }
                case 5:
                    if ((ret = get_user_ual(arg5, sp_reg + 16)) != 0) {
                        goto done_syscall;
                    }
                default:
                    break;
                }
                ret = do_syscall(env, env->active_tc.gpr[2],
                                 env->active_tc.gpr[4],
                                 env->active_tc.gpr[5],
                                 env->active_tc.gpr[6],
                                 env->active_tc.gpr[7],
                                 arg5, arg6, arg7, arg8);
            }
done_syscall:
# else
            ret = do_syscall(env, env->active_tc.gpr[2],
                             env->active_tc.gpr[4], env->active_tc.gpr[5],
                             env->active_tc.gpr[6], env->active_tc.gpr[7],
                             env->active_tc.gpr[8], env->active_tc.gpr[9],
                             env->active_tc.gpr[10], env->active_tc.gpr[11]);
# endif /* O32 */
            if (ret == -TARGET_ERESTARTSYS) {
                env->active_tc.PC -= 4;
                break;
            }
            if (ret == -TARGET_QEMU_ESIGRETURN) {
                /* Returning from a successful sigreturn syscall.
                   Avoid clobbering register state.  */
                break;
            }
            if ((abi_ulong)ret >= (abi_ulong)-1133) {
                env->active_tc.gpr[7] = 1; /* error flag */
                ret = -ret;
            } else {
                env->active_tc.gpr[7] = 0; /* error flag */
            }
            env->active_tc.gpr[2] = ret;
            break;
        case EXCP_TLBL:
        case EXCP_TLBS:
        case EXCP_AdEL:
        case EXCP_AdES:
            info.si_signo = TARGET_SIGSEGV;
            info.si_errno = 0;
            /* XXX: check env->error_code */
            info.si_code = TARGET_SEGV_MAPERR;
            info._sifields._sigfault._addr = env->CP0_BadVAddr;
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;
        case EXCP_CpU:
        case EXCP_RI:
            info.si_signo = TARGET_SIGILL;
            info.si_errno = 0;
            info.si_code = 0;
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        case EXCP_DEBUG:
            info.si_signo = TARGET_SIGTRAP;
            info.si_errno = 0;
            info.si_code = TARGET_TRAP_BRKPT;
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;
        case EXCP_DSPDIS:
            info.si_signo = TARGET_SIGILL;
            info.si_errno = 0;
            info.si_code = TARGET_ILL_ILLOPC;
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;
        case EXCP_FPE:
            info.si_signo = TARGET_SIGFPE;
            info.si_errno = 0;
            info.si_code = TARGET_FPE_FLTUNK;
            if (GET_FP_CAUSE(env->active_fpu.fcr31) & FP_INVALID) {
                info.si_code = TARGET_FPE_FLTINV;
            } else if (GET_FP_CAUSE(env->active_fpu.fcr31) & FP_DIV0) {
                info.si_code = TARGET_FPE_FLTDIV;
            } else if (GET_FP_CAUSE(env->active_fpu.fcr31) & FP_OVERFLOW) {
                info.si_code = TARGET_FPE_FLTOVF;
            } else if (GET_FP_CAUSE(env->active_fpu.fcr31) & FP_UNDERFLOW) {
                info.si_code = TARGET_FPE_FLTUND;
            } else if (GET_FP_CAUSE(env->active_fpu.fcr31) & FP_INEXACT) {
                info.si_code = TARGET_FPE_FLTRES;
            }
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;
        /* The code below was inspired by the MIPS Linux kernel trap
         * handling code in arch/mips/kernel/traps.c.
         */
        case EXCP_BREAK:
            {
                abi_ulong trap_instr;
                unsigned int code;

                if (env->hflags & MIPS_HFLAG_M16) {
                    if (env->insn_flags & ASE_MICROMIPS) {
                        /* microMIPS mode */
                        ret = get_user_u16(trap_instr, env->active_tc.PC);
                        if (ret != 0) {
                            goto error;
                        }

                        if ((trap_instr >> 10) == 0x11) {
                            /* 16-bit instruction */
                            code = trap_instr & 0xf;
                        } else {
                            /* 32-bit instruction */
                            abi_ulong instr_lo;

                            ret = get_user_u16(instr_lo,
                                               env->active_tc.PC + 2);
                            if (ret != 0) {
                                goto error;
                            }
                            trap_instr = (trap_instr << 16) | instr_lo;
                            code = ((trap_instr >> 6) & ((1 << 20) - 1));
                            /* Unfortunately, microMIPS also suffers from
                               the old assembler bug...  */
                            if (code >= (1 << 10)) {
                                code >>= 10;
                            }
                        }
                    } else {
                        /* MIPS16e mode */
                        ret = get_user_u16(trap_instr, env->active_tc.PC);
                        if (ret != 0) {
                            goto error;
                        }
                        code = (trap_instr >> 6) & 0x3f;
                    }
                } else {
                    ret = get_user_u32(trap_instr, env->active_tc.PC);
                    if (ret != 0) {
                        goto error;
                    }

                    /* As described in the original Linux kernel code, the
                     * below checks on 'code' are to work around an old
                     * assembly bug.
                     */
                    code = ((trap_instr >> 6) & ((1 << 20) - 1));
                    if (code >= (1 << 10)) {
                        code >>= 10;
                    }
                }

                if (do_break(env, &info, code) != 0) {
                    goto error;
                }
            }
            break;
        case EXCP_TRAP:
            {
                abi_ulong trap_instr;
                unsigned int code = 0;

                if (env->hflags & MIPS_HFLAG_M16) {
                    /* microMIPS mode */
                    abi_ulong instr[2];

                    ret = get_user_u16(instr[0], env->active_tc.PC) ||
                          get_user_u16(instr[1], env->active_tc.PC + 2);

                    trap_instr = (instr[0] << 16) | instr[1];
                } else {
                    ret = get_user_u32(trap_instr, env->active_tc.PC);
                }

                if (ret != 0) {
                    goto error;
                }

                /* The immediate versions don't provide a code.  */
                if (!(trap_instr & 0xFC000000)) {
                    if (env->hflags & MIPS_HFLAG_M16) {
                        /* microMIPS mode */
                        code = ((trap_instr >> 12) & ((1 << 4) - 1));
                    } else {
                        code = ((trap_instr >> 6) & ((1 << 10) - 1));
                    }
                }

                if (do_break(env, &info, code) != 0) {
                    goto error;
                }
            }
            break;
        case EXCP_ATOMIC:
            cpu_exec_step_atomic(cs);
            break;
        default:
error:
            EXCP_DUMP(env, "qemu: unhandled CPU exception 0x%x - aborting\n", trapnr);
            abort();
        }
        process_pending_signals(env);
    }
}

void target_cpu_copy_regs(CPUArchState *env, struct target_pt_regs *regs)
{
    CPUState *cpu = env_cpu(env);
    TaskState *ts = cpu->opaque;
    struct image_info *info = ts->info;
    int i;

    struct mode_req {
        bool single;
        bool soft;
        bool fr1;
        bool frdefault;
        bool fre;
    };

    static const struct mode_req fpu_reqs[] = {
        [MIPS_ABI_FP_ANY]    = { true,  true,  true,  true,  true  },
        [MIPS_ABI_FP_DOUBLE] = { false, false, false, true,  true  },
        [MIPS_ABI_FP_SINGLE] = { true,  false, false, false, false },
        [MIPS_ABI_FP_SOFT]   = { false, true,  false, false, false },
        [MIPS_ABI_FP_OLD_64] = { false, false, false, false, false },
        [MIPS_ABI_FP_XX]     = { false, false, true,  true,  true  },
        [MIPS_ABI_FP_64]     = { false, false, true,  false, false },
        [MIPS_ABI_FP_64A]    = { false, false, true,  false, true  }
    };

    /*
     * Mode requirements when .MIPS.abiflags is not present in the ELF.
     * Not present means that everything is acceptable except FR1.
     */
    static struct mode_req none_req = { true, true, false, true, true };

    struct mode_req prog_req;
    struct mode_req interp_req;

    for(i = 0; i < 32; i++) {
        env->active_tc.gpr[i] = regs->regs[i];
    }
    env->active_tc.PC = regs->cp0_epc & ~(target_ulong)1;
    if (regs->cp0_epc & 1) {
        env->hflags |= MIPS_HFLAG_M16;
    }

#ifdef TARGET_ABI_MIPSO32
# define MAX_FP_ABI MIPS_ABI_FP_64A
#else
# define MAX_FP_ABI MIPS_ABI_FP_SOFT
#endif
     if ((info->fp_abi > MAX_FP_ABI && info->fp_abi != MIPS_ABI_FP_UNKNOWN)
        || (info->interp_fp_abi > MAX_FP_ABI &&
            info->interp_fp_abi != MIPS_ABI_FP_UNKNOWN)) {
        fprintf(stderr, "qemu: Unexpected FPU mode\n");
        exit(1);
    }

    prog_req = (info->fp_abi == MIPS_ABI_FP_UNKNOWN) ? none_req
                                            : fpu_reqs[info->fp_abi];
    interp_req = (info->interp_fp_abi == MIPS_ABI_FP_UNKNOWN) ? none_req
                                            : fpu_reqs[info->interp_fp_abi];

    prog_req.single &= interp_req.single;
    prog_req.soft &= interp_req.soft;
    prog_req.fr1 &= interp_req.fr1;
    prog_req.frdefault &= interp_req.frdefault;
    prog_req.fre &= interp_req.fre;

    bool cpu_has_mips_r2_r6 = env->insn_flags & ISA_MIPS32R2 ||
                              env->insn_flags & ISA_MIPS64R2 ||
                              env->insn_flags & ISA_MIPS32R6 ||
                              env->insn_flags & ISA_MIPS64R6;

    if (prog_req.fre && !prog_req.frdefault && !prog_req.fr1) {
        env->CP0_Config5 |= (1 << CP0C5_FRE);
        if (env->active_fpu.fcr0 & (1 << FCR0_FREP)) {
            env->hflags |= MIPS_HFLAG_FRE;
        }
    } else if ((prog_req.fr1 && prog_req.frdefault) ||
         (prog_req.single && !prog_req.frdefault)) {
        if ((env->active_fpu.fcr0 & (1 << FCR0_F64)
            && cpu_has_mips_r2_r6) || prog_req.fr1) {
            env->CP0_Status |= (1 << CP0St_FR);
            env->hflags |= MIPS_HFLAG_F64;
        }
    } else  if (!prog_req.fre && !prog_req.frdefault &&
          !prog_req.fr1 && !prog_req.single && !prog_req.soft) {
        fprintf(stderr, "qemu: Can't find a matching FPU mode\n");
        exit(1);
    }

    if (env->insn_flags & ISA_NANOMIPS32) {
        return;
    }
    if (((info->elf_flags & EF_MIPS_NAN2008) != 0) !=
        ((env->active_fpu.fcr31 & (1 << FCR31_NAN2008)) != 0)) {
        if ((env->active_fpu.fcr31_rw_bitmask &
              (1 << FCR31_NAN2008)) == 0) {
            fprintf(stderr, "ELF binary's NaN mode not supported by CPU\n");
            exit(1);
        }
        if ((info->elf_flags & EF_MIPS_NAN2008) != 0) {
            env->active_fpu.fcr31 |= (1 << FCR31_NAN2008);
        } else {
            env->active_fpu.fcr31 &= ~(1 << FCR31_NAN2008);
        }
        restore_snan_bit_mode(env);
    }
}

#  define MIPS_SYS(name, args) args,
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
        MIPS_SYS(sys_pkey_mprotect, 4)
        MIPS_SYS(sys_pkey_alloc, 2)
        MIPS_SYS(sys_pkey_free, 1)                 /* 365 */
        MIPS_SYS(sys_statx, 5)
        MIPS_SYS(sys_rseq, 4)
        MIPS_SYS(sys_io_pgetevents, 6)
        MIPS_SYSCALL_NUMBER_UNUSED,
        MIPS_SYSCALL_NUMBER_UNUSED,                /* 370 */
        MIPS_SYSCALL_NUMBER_UNUSED,
        MIPS_SYSCALL_NUMBER_UNUSED,
        MIPS_SYSCALL_NUMBER_UNUSED,
        MIPS_SYSCALL_NUMBER_UNUSED,
        MIPS_SYSCALL_NUMBER_UNUSED,                /* 375 */
        MIPS_SYSCALL_NUMBER_UNUSED,
        MIPS_SYSCALL_NUMBER_UNUSED,
        MIPS_SYSCALL_NUMBER_UNUSED,
        MIPS_SYSCALL_NUMBER_UNUSED,
        MIPS_SYSCALL_NUMBER_UNUSED,                /* 380 */
        MIPS_SYSCALL_NUMBER_UNUSED,
        MIPS_SYSCALL_NUMBER_UNUSED,
        MIPS_SYSCALL_NUMBER_UNUSED,
        MIPS_SYSCALL_NUMBER_UNUSED,
        MIPS_SYSCALL_NUMBER_UNUSED,                /* 385 */
        MIPS_SYSCALL_NUMBER_UNUSED,
        MIPS_SYSCALL_NUMBER_UNUSED,
        MIPS_SYSCALL_NUMBER_UNUSED,
        MIPS_SYSCALL_NUMBER_UNUSED,
        MIPS_SYSCALL_NUMBER_UNUSED,                /* 390 */
        MIPS_SYSCALL_NUMBER_UNUSED,
        MIPS_SYSCALL_NUMBER_UNUSED,
        MIPS_SYS(sys_semget, 3)
        MIPS_SYS(sys_semctl, 4)
        MIPS_SYS(sys_shmget, 3)                    /* 395 */
        MIPS_SYS(sys_shmctl, 3)
        MIPS_SYS(sys_shmat, 3)
        MIPS_SYS(sys_shmdt, 1)
        MIPS_SYS(sys_msgget, 2)
        MIPS_SYS(sys_msgsnd, 4)                    /* 400 */
        MIPS_SYS(sys_msgrcv, 5)
        MIPS_SYS(sys_msgctl, 3)
        MIPS_SYS(sys_clock_gettime64, 2)
        MIPS_SYS(sys_clock_settime64, 2)
        MIPS_SYS(sys_clock_adjtime64, 2)           /* 405 */
        MIPS_SYS(sys_clock_getres_time64, 2)
        MIPS_SYS(sys_clock_nanosleep_time64, 4)
        MIPS_SYS(sys_timer_gettime64, 2)
        MIPS_SYS(sys_timer_settime64, 4)
        MIPS_SYS(sys_timerfd_gettime64, 2)         /* 410 */
        MIPS_SYS(sys_timerfd_settime64, 4)
        MIPS_SYS(sys_utimensat_time64, 4)
        MIPS_SYS(sys_pselect6_time64, 6)
        MIPS_SYS(sys_ppoll_time64, 5)
        MIPS_SYSCALL_NUMBER_UNUSED,                /* 415 */
        MIPS_SYS(sys_io_pgetevents_time64, 6)
        MIPS_SYS(sys_recvmmsg_time64, 5)
        MIPS_SYS(sys_mq_timedsend_time64, 5)
        MIPS_SYS(sys_mq_timedreceive_time64, 5)
        MIPS_SYS(sys_semtimedop_time64, 4)         /* 420 */
        MIPS_SYS(sys_rt_sigtimedwait_time64, 4)
        MIPS_SYS(sys_futex_time64, 6)
        MIPS_SYS(sys_sched_rr_get_interval_time64, 2)
        MIPS_SYS(sys_pidfd_send_signal, 4)
        MIPS_SYS(sys_io_uring_setup, 2)            /* 425 */
        MIPS_SYS(sys_io_uring_enter, 6)
        MIPS_SYS(sys_io_uring_register, 4)
        MIPS_SYS(sys_open_tree, 3)
        MIPS_SYS(sys_move_mount, 5)
        MIPS_SYS(sys_fsopen, 2)                    /* 430 */
        MIPS_SYS(sys_fsconfig, 5)
        MIPS_SYS(sys_fsmount, 3)
        MIPS_SYS(sys_fspick, 3)
        MIPS_SYS(sys_pidfd_open, 2)
        MIPS_SYS(sys_clone3, 2)                    /* 435 */
#  undef MIPS_SYS

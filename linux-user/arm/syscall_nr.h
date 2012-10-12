/*
 * This file contains the system call numbers.
 */

#define TARGET_NR_restart_syscall		(  0)
#define TARGET_NR_exit			(  1)
#define TARGET_NR_fork			(  2)
#define TARGET_NR_read			(  3)
#define TARGET_NR_write			(  4)
#define TARGET_NR_open			(  5)
#define TARGET_NR_close			(  6)
#define TARGET_NR_waitpid			(  7)	/* removed */
#define TARGET_NR_creat			(  8)
#define TARGET_NR_link			(  9)
#define TARGET_NR_unlink			( 10)
#define TARGET_NR_execve			( 11)
#define TARGET_NR_chdir			( 12)
#define TARGET_NR_time			( 13)
#define TARGET_NR_mknod			( 14)
#define TARGET_NR_chmod			( 15)
#define TARGET_NR_lchown			( 16)
#define TARGET_NR_break			( 17)	/* removed */
					/* 18 was sys_stat */
#define TARGET_NR_lseek			( 19)
#define TARGET_NR_getpid			( 20)
#define TARGET_NR_mount			( 21)
#define TARGET_NR_umount			( 22)
#define TARGET_NR_setuid			( 23)
#define TARGET_NR_getuid			( 24)
#define TARGET_NR_stime			( 25)
#define TARGET_NR_ptrace			( 26)
#define TARGET_NR_alarm			( 27)

#define TARGET_NR_pause			( 29)
#define TARGET_NR_utime			( 30)
#define TARGET_NR_stty			( 31)	/* removed */
#define TARGET_NR_gtty			( 32)	/* removed */
#define TARGET_NR_access			( 33)
#define TARGET_NR_nice			( 34)
#define TARGET_NR_ftime			( 35)	/* removed */
#define TARGET_NR_sync			( 36)
#define TARGET_NR_kill			( 37)
#define TARGET_NR_rename			( 38)
#define TARGET_NR_mkdir			( 39)
#define TARGET_NR_rmdir			( 40)
#define TARGET_NR_dup			( 41)
#define TARGET_NR_pipe			( 42)
#define TARGET_NR_times			( 43)
#define TARGET_NR_prof			( 44)	/* removed */
#define TARGET_NR_brk			( 45)
#define TARGET_NR_setgid			( 46)
#define TARGET_NR_getgid			( 47)
#define TARGET_NR_signal			( 48)	/* removed */
#define TARGET_NR_geteuid			( 49)
#define TARGET_NR_getegid			( 50)
#define TARGET_NR_acct			( 51)
#define TARGET_NR_umount2			( 52)
#define TARGET_NR_lock			( 53)	/* removed */
#define TARGET_NR_ioctl			( 54)
#define TARGET_NR_fcntl			( 55)
#define TARGET_NR_mpx			( 56)	/* removed */
#define TARGET_NR_setpgid			( 57)
#define TARGET_NR_ulimit			( 58)	/* removed */
					/* 59 was sys_olduname */
#define TARGET_NR_umask			( 60)
#define TARGET_NR_chroot			( 61)
#define TARGET_NR_ustat			( 62)
#define TARGET_NR_dup2			( 63)
#define TARGET_NR_getppid			( 64)
#define TARGET_NR_getpgrp			( 65)
#define TARGET_NR_setsid			( 66)
#define TARGET_NR_sigaction			( 67)
#define TARGET_NR_sgetmask			( 68)	/* removed */
#define TARGET_NR_ssetmask			( 69)	/* removed */
#define TARGET_NR_setreuid			( 70)
#define TARGET_NR_setregid			( 71)
#define TARGET_NR_sigsuspend			( 72)
#define TARGET_NR_sigpending			( 73)
#define TARGET_NR_sethostname		( 74)
#define TARGET_NR_setrlimit			( 75)
#define TARGET_NR_getrlimit			( 76)	/* Back compat 2GB limited rlimit */
#define TARGET_NR_getrusage			( 77)
#define TARGET_NR_gettimeofday		( 78)
#define TARGET_NR_settimeofday		( 79)
#define TARGET_NR_getgroups			( 80)
#define TARGET_NR_setgroups			( 81)
#define TARGET_NR_select			( 82)
#define TARGET_NR_symlink			( 83)
					/* 84 was sys_lstat */
#define TARGET_NR_readlink			( 85)
#define TARGET_NR_uselib			( 86)
#define TARGET_NR_swapon			( 87)
#define TARGET_NR_reboot			( 88)
#define TARGET_NR_readdir			( 89)
#define TARGET_NR_mmap			( 90)
#define TARGET_NR_munmap			( 91)
#define TARGET_NR_truncate			( 92)
#define TARGET_NR_ftruncate			( 93)
#define TARGET_NR_fchmod			( 94)
#define TARGET_NR_fchown			( 95)
#define TARGET_NR_getpriority		( 96)
#define TARGET_NR_setpriority		( 97)
#define TARGET_NR_profil			( 98)	/* removed */
#define TARGET_NR_statfs			( 99)
#define TARGET_NR_fstatfs			(100)
#define TARGET_NR_ioperm			(101)
#define TARGET_NR_socketcall			(102)
#define TARGET_NR_syslog			(103)
#define TARGET_NR_setitimer			(104)
#define TARGET_NR_getitimer			(105)
#define TARGET_NR_stat			(106)
#define TARGET_NR_lstat			(107)
#define TARGET_NR_fstat			(108)
					/* 109 was sys_uname */
					/* 110 was sys_iopl */
#define TARGET_NR_vhangup			(111)
#define TARGET_NR_idle			(112)
#define TARGET_NR_syscall			(113) /* syscall to call a syscall! */
#define TARGET_NR_wait4			(114)
#define TARGET_NR_swapoff			(115)
#define TARGET_NR_sysinfo			(116)
#define TARGET_NR_ipc			(117)
#define TARGET_NR_fsync			(118)
#define TARGET_NR_sigreturn			(119)
#define TARGET_NR_clone			(120)
#define TARGET_NR_setdomainname		(121)
#define TARGET_NR_uname			(122)
#define TARGET_NR_modify_ldt			(123)
#define TARGET_NR_adjtimex			(124)
#define TARGET_NR_mprotect			(125)
#define TARGET_NR_sigprocmask		(126)
#define TARGET_NR_create_module		(127)	/* removed */
#define TARGET_NR_init_module		(128)
#define TARGET_NR_delete_module		(129)
#define TARGET_NR_get_kernel_syms		(130)	/* removed */
#define TARGET_NR_quotactl			(131)
#define TARGET_NR_getpgid			(132)
#define TARGET_NR_fchdir			(133)
#define TARGET_NR_bdflush			(134)
#define TARGET_NR_sysfs			(135)
#define TARGET_NR_personality		(136)
#define TARGET_NR_afs_syscall		(137) /* Syscall for Andrew File System */
#define TARGET_NR_setfsuid			(138)
#define TARGET_NR_setfsgid			(139)
#define TARGET_NR__llseek			(140)
#define TARGET_NR_getdents			(141)
#define TARGET_NR__newselect			(142)
#define TARGET_NR_flock			(143)
#define TARGET_NR_msync			(144)
#define TARGET_NR_readv			(145)
#define TARGET_NR_writev			(146)
#define TARGET_NR_getsid			(147)
#define TARGET_NR_fdatasync			(148)
#define TARGET_NR__sysctl			(149)
#define TARGET_NR_mlock			(150)
#define TARGET_NR_munlock			(151)
#define TARGET_NR_mlockall			(152)
#define TARGET_NR_munlockall			(153)
#define TARGET_NR_sched_setparam		(154)
#define TARGET_NR_sched_getparam		(155)
#define TARGET_NR_sched_setscheduler		(156)
#define TARGET_NR_sched_getscheduler		(157)
#define TARGET_NR_sched_yield		(158)
#define TARGET_NR_sched_get_priority_max	(159)
#define TARGET_NR_sched_get_priority_min	(160)
#define TARGET_NR_sched_rr_get_interval	(161)
#define TARGET_NR_nanosleep			(162)
#define TARGET_NR_mremap			(163)
#define TARGET_NR_setresuid			(164)
#define TARGET_NR_getresuid			(165)
#define TARGET_NR_vm86			(166)	/* removed */
#define TARGET_NR_query_module		(167)	/* removed */
#define TARGET_NR_poll			(168)
#define TARGET_NR_nfsservctl			(169)
#define TARGET_NR_setresgid			(170)
#define TARGET_NR_getresgid			(171)
#define TARGET_NR_prctl			(172)
#define TARGET_NR_rt_sigreturn		(173)
#define TARGET_NR_rt_sigaction		(174)
#define TARGET_NR_rt_sigprocmask		(175)
#define TARGET_NR_rt_sigpending		(176)
#define TARGET_NR_rt_sigtimedwait		(177)
#define TARGET_NR_rt_sigqueueinfo		(178)
#define TARGET_NR_rt_sigsuspend		(179)
#define TARGET_NR_pread64                       (180)
#define TARGET_NR_pwrite64                      (181)
#define TARGET_NR_chown			(182)
#define TARGET_NR_getcwd			(183)
#define TARGET_NR_capget			(184)
#define TARGET_NR_capset			(185)
#define TARGET_NR_sigaltstack		(186)
#define TARGET_NR_sendfile			(187)
					/* 188 reserved */
					/* 189 reserved */
#define TARGET_NR_vfork			(190)
#define TARGET_NR_ugetrlimit			(191)	/* SuS compliant getrlimit */
#define TARGET_NR_mmap2			(192)
#define TARGET_NR_truncate64			(193)
#define TARGET_NR_ftruncate64		(194)
#define TARGET_NR_stat64			(195)
#define TARGET_NR_lstat64			(196)
#define TARGET_NR_fstat64			(197)
#define TARGET_NR_lchown32			(198)
#define TARGET_NR_getuid32			(199)
#define TARGET_NR_getgid32			(200)
#define TARGET_NR_geteuid32			(201)
#define TARGET_NR_getegid32			(202)
#define TARGET_NR_setreuid32			(203)
#define TARGET_NR_setregid32			(204)
#define TARGET_NR_getgroups32		(205)
#define TARGET_NR_setgroups32		(206)
#define TARGET_NR_fchown32			(207)
#define TARGET_NR_setresuid32		(208)
#define TARGET_NR_getresuid32		(209)
#define TARGET_NR_setresgid32		(210)
#define TARGET_NR_getresgid32		(211)
#define TARGET_NR_chown32			(212)
#define TARGET_NR_setuid32			(213)
#define TARGET_NR_setgid32			(214)
#define TARGET_NR_setfsuid32			(215)
#define TARGET_NR_setfsgid32			(216)
#define TARGET_NR_getdents64			(217)
#define TARGET_NR_pivot_root			(218)
#define TARGET_NR_mincore			(219)
#define TARGET_NR_madvise			(220)
#define TARGET_NR_fcntl64			(221)
					/* 222 for tux */
					/* 223 is unused */
#define TARGET_NR_gettid			(224)
#define TARGET_NR_readahead			(225)
#define TARGET_NR_setxattr			(226)
#define TARGET_NR_lsetxattr			(227)
#define TARGET_NR_fsetxattr			(228)
#define TARGET_NR_getxattr			(229)
#define TARGET_NR_lgetxattr			(230)
#define TARGET_NR_fgetxattr			(231)
#define TARGET_NR_listxattr			(232)
#define TARGET_NR_llistxattr			(233)
#define TARGET_NR_flistxattr			(234)
#define TARGET_NR_removexattr		(235)
#define TARGET_NR_lremovexattr		(236)
#define TARGET_NR_fremovexattr		(237)
#define TARGET_NR_tkill			(238)
#define TARGET_NR_sendfile64			(239)
#define TARGET_NR_futex			(240)
#define TARGET_NR_sched_setaffinity		(241)
#define TARGET_NR_sched_getaffinity		(242)
#define TARGET_NR_io_setup			(243)
#define TARGET_NR_io_destroy			(244)
#define TARGET_NR_io_getevents		(245)
#define TARGET_NR_io_submit			(246)
#define TARGET_NR_io_cancel			(247)
#define TARGET_NR_exit_group			(248)
#define TARGET_NR_lookup_dcookie		(249)
#define TARGET_NR_epoll_create		(250)
#define TARGET_NR_epoll_ctl			(251)
#define TARGET_NR_epoll_wait			(252)
#define TARGET_NR_remap_file_pages		(253)
					/* 254 for set_thread_area */
					/* 255 for get_thread_area */
					/* 256 for set_tid_address */
#define TARGET_NR_set_tid_address		256
#define TARGET_NR_timer_create		257
#define TARGET_NR_timer_settime		258
#define TARGET_NR_timer_gettime		259
#define TARGET_NR_timer_getoverrun		260
#define TARGET_NR_timer_delete		261
#define TARGET_NR_clock_settime		262
#define TARGET_NR_clock_gettime		263
#define TARGET_NR_clock_getres		264
#define TARGET_NR_clock_nanosleep		265
#define TARGET_NR_statfs64			266
#define TARGET_NR_fstatfs64			267
#define TARGET_NR_tgkill			268
#define TARGET_NR_utimes			269
#define TARGET_NR_arm_fadvise64_64		270
#define TARGET_NR_pciconfig_iobase		271
#define TARGET_NR_pciconfig_read		272
#define TARGET_NR_pciconfig_write		273
#define TARGET_NR_mq_open			274
#define TARGET_NR_mq_unlink			275
#define TARGET_NR_mq_timedsend		276
#define TARGET_NR_mq_timedreceive		277
#define TARGET_NR_mq_notify			278
#define TARGET_NR_mq_getsetattr		279
#define TARGET_NR_waitid			280
#define TARGET_NR_socket			281
#define TARGET_NR_bind			282
#define TARGET_NR_connect			283
#define TARGET_NR_listen			284
#define TARGET_NR_accept			285
#define TARGET_NR_getsockname		286
#define TARGET_NR_getpeername		287
#define TARGET_NR_socketpair			288
#define TARGET_NR_send			289
#define TARGET_NR_sendto			290
#define TARGET_NR_recv			291
#define TARGET_NR_recvfrom			292
#define TARGET_NR_shutdown			293
#define TARGET_NR_setsockopt			294
#define TARGET_NR_getsockopt			295
#define TARGET_NR_sendmsg			296
#define TARGET_NR_recvmsg			297
#define TARGET_NR_semop			298
#define TARGET_NR_semget			299
#define TARGET_NR_semctl			300
#define TARGET_NR_msgsnd			301
#define TARGET_NR_msgrcv			302
#define TARGET_NR_msgget			303
#define TARGET_NR_msgctl			304
#define TARGET_NR_shmat			305
#define TARGET_NR_shmdt			306
#define TARGET_NR_shmget			307
#define TARGET_NR_shmctl			308
#define TARGET_NR_add_key			309
#define TARGET_NR_request_key		310
#define TARGET_NR_keyctl			311
#define TARGET_NR_semtimedop			312
#define TARGET_NR_vserver			313
#define TARGET_NR_ioprio_set			314
#define TARGET_NR_ioprio_get			315
#define TARGET_NR_inotify_init		316
#define TARGET_NR_inotify_add_watch		317
#define TARGET_NR_inotify_rm_watch		318
#define TARGET_NR_mbind			319
#define TARGET_NR_get_mempolicy		320
#define TARGET_NR_set_mempolicy		321
#define TARGET_NR_openat			(322)
#define TARGET_NR_mkdirat			(323)
#define TARGET_NR_mknodat			(324)
#define TARGET_NR_fchownat			(325)
#define TARGET_NR_futimesat			(326)
#define TARGET_NR_fstatat64			(327)
#define TARGET_NR_unlinkat			(328)
#define TARGET_NR_renameat			(329)
#define TARGET_NR_linkat			(330)
#define TARGET_NR_symlinkat			(331)
#define TARGET_NR_readlinkat			(332)
#define TARGET_NR_fchmodat			(333)
#define TARGET_NR_faccessat			(334)
#define TARGET_NR_pselect6			(335)
#define TARGET_NR_ppoll                         (336)
#define TARGET_NR_unshare			(337)
#define TARGET_NR_set_robust_list		(338)
#define TARGET_NR_get_robust_list		(339)
#define TARGET_NR_splice			(340)
#define TARGET_NR_arm_sync_file_range	(341)
#define TARGET_NR_sync_file_range2		TARGET_NR_arm_sync_file_range
#define TARGET_NR_tee			(342)
#define TARGET_NR_vmsplice			(343)
#define TARGET_NR_move_pages			(344)
#define TARGET_NR_getcpu			(345)
					/* 346 for epoll_pwait */
#define TARGET_NR_kexec_load			(347)
#define TARGET_NR_utimensat			(348)
#define TARGET_NR_signalfd			(349)
#define TARGET_NR_timerfd			(350)
#define TARGET_NR_eventfd			(351)
#define TARGET_NR_fallocate			(352)
#define TARGET_NR_timerfd_settime		(353)
#define TARGET_NR_timerfd_gettime		(354)
#define TARGET_NR_signalfd4			(355)
#define TARGET_NR_eventfd2			(356)
#define TARGET_NR_epoll_create1		(357)
#define TARGET_NR_dup3				(358)
#define TARGET_NR_pipe2			(359)
#define TARGET_NR_inotify_init1		(360)
#define TARGET_NR_preadv                       (361)
#define TARGET_NR_pwritev                      (362)
#define TARGET_NR_rt_tgsigqueueinfo            (363)
#define TARGET_NR_perf_event_open              (364)
#define TARGET_NR_recvmmsg                     (365)
#define TARGET_NR_accept4                      (366)
#define TARGET_NR_fanotify_init                (367)
#define TARGET_NR_fanotify_mark                (368)
#define TARGET_NR_prlimit64                    (369)
#define TARGET_NR_name_to_handle_at            (370)
#define TARGET_NR_open_by_handle_at            (371)
#define TARGET_NR_clock_adjtime                (372)
#define TARGET_NR_syncfs                       (373)

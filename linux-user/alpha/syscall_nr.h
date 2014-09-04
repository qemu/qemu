#define TARGET_NR_osf_syscall	  0	/* not implemented */
#define TARGET_NR_exit		  1
#define TARGET_NR_fork		  2
#define TARGET_NR_read		  3
#define TARGET_NR_write		  4
#define TARGET_NR_osf_old_open	  5	/* not implemented */
#define TARGET_NR_close		  6
#define TARGET_NR_osf_wait4		  7
#define TARGET_NR_osf_old_creat	  8	/* not implemented */
#define TARGET_NR_link		  9
#define TARGET_NR_unlink		 10
#define TARGET_NR_osf_execve		 11	/* not implemented */
#define TARGET_NR_chdir		 12
#define TARGET_NR_fchdir		 13
#define TARGET_NR_mknod		 14
#define TARGET_NR_chmod		 15
#define TARGET_NR_chown		 16
#define TARGET_NR_brk		 17
#define TARGET_NR_osf_getfsstat	 18	/* not implemented */
#define TARGET_NR_lseek		 19
#define TARGET_NR_getxpid		 20
#define TARGET_NR_osf_mount		 21
#define TARGET_NR_umount2		 22
#define TARGET_NR_setuid		 23
#define TARGET_NR_getxuid		 24
#define TARGET_NR_exec_with_loader	 25	/* not implemented */
#define TARGET_NR_ptrace		 26
#define TARGET_NR_osf_nrecvmsg	 27	/* not implemented */
#define TARGET_NR_osf_nsendmsg	 28	/* not implemented */
#define TARGET_NR_osf_nrecvfrom	 29	/* not implemented */
#define TARGET_NR_osf_naccept	 30	/* not implemented */
#define TARGET_NR_osf_ngetpeername	 31	/* not implemented */
#define TARGET_NR_osf_ngetsockname	 32	/* not implemented */
#define TARGET_NR_access		 33
#define TARGET_NR_osf_chflags	 34	/* not implemented */
#define TARGET_NR_osf_fchflags	 35	/* not implemented */
#define TARGET_NR_sync		 36
#define TARGET_NR_kill		 37
#define TARGET_NR_osf_old_stat	 38	/* not implemented */
#define TARGET_NR_setpgid		 39
#define TARGET_NR_osf_old_lstat	 40	/* not implemented */
#define TARGET_NR_dup		 41
#define TARGET_NR_pipe		 42
#define TARGET_NR_osf_set_program_attributes	43
#define TARGET_NR_osf_profil		 44	/* not implemented */
#define TARGET_NR_open		 45
#define TARGET_NR_osf_old_sigaction	 46	/* not implemented */
#define TARGET_NR_getxgid		 47
#define TARGET_NR_sigprocmask    48
#define TARGET_NR_osf_getlogin	 49	/* not implemented */
#define TARGET_NR_osf_setlogin	 50	/* not implemented */
#define TARGET_NR_acct		 51
#define TARGET_NR_sigpending		 52

#define TARGET_NR_ioctl		 54
#define TARGET_NR_osf_reboot		 55	/* not implemented */
#define TARGET_NR_osf_revoke		 56	/* not implemented */
#define TARGET_NR_symlink		 57
#define TARGET_NR_readlink		 58
#define TARGET_NR_execve		 59
#define TARGET_NR_umask		 60
#define TARGET_NR_chroot		 61
#define TARGET_NR_osf_old_fstat	 62	/* not implemented */
#define TARGET_NR_getpgrp		 63
#define TARGET_NR_getpagesize	 64
#define TARGET_NR_osf_mremap		 65	/* not implemented */
#define TARGET_NR_vfork		 66
#define TARGET_NR_stat		 67
#define TARGET_NR_lstat		 68
#define TARGET_NR_osf_sbrk		 69	/* not implemented */
#define TARGET_NR_osf_sstk		 70	/* not implemented */
#define TARGET_NR_mmap		 71	/* OSF/1 mmap is superset of Linux */
#define TARGET_NR_osf_old_vadvise	 72	/* not implemented */
#define TARGET_NR_munmap		 73
#define TARGET_NR_mprotect		 74
#define TARGET_NR_madvise		 75
#define TARGET_NR_vhangup		 76
#define TARGET_NR_osf_kmodcall	 77	/* not implemented */
#define TARGET_NR_osf_mincore	 78	/* not implemented */
#define TARGET_NR_getgroups		 79
#define TARGET_NR_setgroups		 80
#define TARGET_NR_osf_old_getpgrp	 81	/* not implemented */
#define TARGET_NR_setpgrp		 82	/* BSD alias for setpgid */
#define TARGET_NR_osf_setitimer	 83
#define TARGET_NR_osf_old_wait	 84	/* not implemented */
#define TARGET_NR_osf_table		 85	/* not implemented */
#define TARGET_NR_osf_getitimer	 86
#define TARGET_NR_gethostname	 87
#define TARGET_NR_sethostname	 88
#define TARGET_NR_getdtablesize	 89
#define TARGET_NR_dup2		 90
#define TARGET_NR_fstat		 91
#define TARGET_NR_fcntl		 92
#define TARGET_NR_osf_select		 93
#define TARGET_NR_poll		 94
#define TARGET_NR_fsync		 95
#define TARGET_NR_setpriority	 96
#define TARGET_NR_socket		 97
#define TARGET_NR_connect		 98
#define TARGET_NR_accept		 99
#define TARGET_NR_getpriority	100
#define TARGET_NR_send		101
#define TARGET_NR_recv		102
#define TARGET_NR_sigreturn		103
#define TARGET_NR_bind		104
#define TARGET_NR_setsockopt		105
#define TARGET_NR_listen		106
#define TARGET_NR_osf_plock		107	/* not implemented */
#define TARGET_NR_osf_old_sigvec	108	/* not implemented */
#define TARGET_NR_osf_old_sigblock	109	/* not implemented */
#define TARGET_NR_osf_old_sigsetmask	110	/* not implemented */
#define TARGET_NR_sigsuspend		111
#define TARGET_NR_osf_sigstack	112
#define TARGET_NR_recvmsg		113
#define TARGET_NR_sendmsg		114
#define TARGET_NR_osf_old_vtrace	115	/* not implemented */
#define TARGET_NR_osf_gettimeofday	116
#define TARGET_NR_osf_getrusage	117
#define TARGET_NR_getsockopt		118

#define TARGET_NR_readv		120
#define TARGET_NR_writev		121
#define TARGET_NR_osf_settimeofday	122
#define TARGET_NR_fchown		123
#define TARGET_NR_fchmod		124
#define TARGET_NR_recvfrom		125
#define TARGET_NR_setreuid		126
#define TARGET_NR_setregid		127
#define TARGET_NR_rename		128
#define TARGET_NR_truncate		129
#define TARGET_NR_ftruncate		130
#define TARGET_NR_flock		131
#define TARGET_NR_setgid		132
#define TARGET_NR_sendto		133
#define TARGET_NR_shutdown		134
#define TARGET_NR_socketpair		135
#define TARGET_NR_mkdir		136
#define TARGET_NR_rmdir		137
#define TARGET_NR_osf_utimes		138
#define TARGET_NR_osf_old_sigreturn	139	/* not implemented */
#define TARGET_NR_osf_adjtime	140	/* not implemented */
#define TARGET_NR_getpeername	141
#define TARGET_NR_osf_gethostid	142	/* not implemented */
#define TARGET_NR_osf_sethostid	143	/* not implemented */
#define TARGET_NR_getrlimit		144
#define TARGET_NR_setrlimit		145
#define TARGET_NR_osf_old_killpg	146	/* not implemented */
#define TARGET_NR_setsid		147
#define TARGET_NR_quotactl		148
#define TARGET_NR_osf_oldquota	149	/* not implemented */
#define TARGET_NR_getsockname	150

#define TARGET_NR_osf_pid_block	153	/* not implemented */
#define TARGET_NR_osf_pid_unblock	154	/* not implemented */

#define TARGET_NR_sigaction		156
#define TARGET_NR_osf_sigwaitprim	157	/* not implemented */
#define TARGET_NR_osf_nfssvc		158	/* not implemented */
#define TARGET_NR_osf_getdirentries	159
#define TARGET_NR_osf_statfs		160
#define TARGET_NR_osf_fstatfs	161

#define TARGET_NR_osf_asynch_daemon	163	/* not implemented */
#define TARGET_NR_osf_getfh		164	/* not implemented */
#define TARGET_NR_osf_getdomainname	165
#define TARGET_NR_setdomainname	166

#define TARGET_NR_osf_exportfs	169	/* not implemented */

#define TARGET_NR_osf_alt_plock	181	/* not implemented */

#define TARGET_NR_osf_getmnt		184	/* not implemented */

#define TARGET_NR_osf_alt_sigpending	187	/* not implemented */
#define TARGET_NR_osf_alt_setsid	188	/* not implemented */

#define TARGET_NR_osf_swapon		199
#define TARGET_NR_msgctl		200
#define TARGET_NR_msgget		201
#define TARGET_NR_msgrcv		202
#define TARGET_NR_msgsnd		203
#define TARGET_NR_semctl		204
#define TARGET_NR_semget		205
#define TARGET_NR_semop		206
#define TARGET_NR_osf_utsname	207
#define TARGET_NR_lchown		208
#define TARGET_NR_osf_shmat		209
#define TARGET_NR_shmctl		210
#define TARGET_NR_shmdt		211
#define TARGET_NR_shmget		212
#define TARGET_NR_osf_mvalid		213	/* not implemented */
#define TARGET_NR_osf_getaddressconf	214	/* not implemented */
#define TARGET_NR_osf_msleep		215	/* not implemented */
#define TARGET_NR_osf_mwakeup	216	/* not implemented */
#define TARGET_NR_msync		217
#define TARGET_NR_osf_signal		218	/* not implemented */
#define TARGET_NR_osf_utc_gettime	219	/* not implemented */
#define TARGET_NR_osf_utc_adjtime	220	/* not implemented */

#define TARGET_NR_osf_security	222	/* not implemented */
#define TARGET_NR_osf_kloadcall	223	/* not implemented */

#define TARGET_NR_getpgid		233
#define TARGET_NR_getsid		234
#define TARGET_NR_sigaltstack	235
#define TARGET_NR_osf_waitid		236	/* not implemented */
#define TARGET_NR_osf_priocntlset	237	/* not implemented */
#define TARGET_NR_osf_sigsendset	238	/* not implemented */
#define TARGET_NR_osf_set_speculative	239	/* not implemented */
#define TARGET_NR_osf_msfs_syscall	240	/* not implemented */
#define TARGET_NR_osf_sysinfo	241
#define TARGET_NR_osf_uadmin		242	/* not implemented */
#define TARGET_NR_osf_fuser		243	/* not implemented */
#define TARGET_NR_osf_proplist_syscall    244
#define TARGET_NR_osf_ntp_adjtime	245	/* not implemented */
#define TARGET_NR_osf_ntp_gettime	246	/* not implemented */
#define TARGET_NR_osf_pathconf	247	/* not implemented */
#define TARGET_NR_osf_fpathconf	248	/* not implemented */

#define TARGET_NR_osf_uswitch	250	/* not implemented */
#define TARGET_NR_osf_usleep_thread	251
#define TARGET_NR_osf_audcntl	252	/* not implemented */
#define TARGET_NR_osf_audgen		253	/* not implemented */
#define TARGET_NR_sysfs		254
#define TARGET_NR_osf_subsys_info	255	/* not implemented */
#define TARGET_NR_osf_getsysinfo	256
#define TARGET_NR_osf_setsysinfo	257
#define TARGET_NR_osf_afs_syscall	258	/* not implemented */
#define TARGET_NR_osf_swapctl	259	/* not implemented */
#define TARGET_NR_osf_memcntl	260	/* not implemented */
#define TARGET_NR_osf_fdatasync	261	/* not implemented */


/*
 * Linux-specific system calls begin at 300
 */
#define TARGET_NR_bdflush		300
#define TARGET_NR_sethae		301
#define TARGET_NR_mount		302
#define TARGET_NR_old_adjtimex	303
#define TARGET_NR_swapoff		304
#define TARGET_NR_getdents		305
#define TARGET_NR_create_module	306
#define TARGET_NR_init_module	307
#define TARGET_NR_delete_module	308
#define TARGET_NR_get_kernel_syms	309
#define TARGET_NR_syslog		310
#define TARGET_NR_reboot		311
#define TARGET_NR_clone		312
#define TARGET_NR_uselib		313
#define TARGET_NR_mlock		314
#define TARGET_NR_munlock		315
#define TARGET_NR_mlockall		316
#define TARGET_NR_munlockall		317
#define TARGET_NR_sysinfo		318
#define TARGET_NR__sysctl		319
/* 320 was sys_idle.  */
#define TARGET_NR_umount		321
#define TARGET_NR_swapon		322
#define TARGET_NR_times		323
#define TARGET_NR_personality	324
#define TARGET_NR_setfsuid		325
#define TARGET_NR_setfsgid		326
#define TARGET_NR_ustat		327
#define TARGET_NR_statfs		328
#define TARGET_NR_fstatfs		329
#define TARGET_NR_sched_setparam		330
#define TARGET_NR_sched_getparam		331
#define TARGET_NR_sched_setscheduler		332
#define TARGET_NR_sched_getscheduler		333
#define TARGET_NR_sched_yield		334
#define TARGET_NR_sched_get_priority_max	335
#define TARGET_NR_sched_get_priority_min	336
#define TARGET_NR_sched_rr_get_interval	337
#define TARGET_NR_afs_syscall		338
#define TARGET_NR_uname			339
#define TARGET_NR_nanosleep			340
#define TARGET_NR_mremap			341
#define TARGET_NR_nfsservctl			342
#define TARGET_NR_setresuid			343
#define TARGET_NR_getresuid			344
#define TARGET_NR_pciconfig_read		345
#define TARGET_NR_pciconfig_write		346
#define TARGET_NR_query_module		347
#define TARGET_NR_prctl			348
#define TARGET_NR_pread64			349
#define TARGET_NR_pwrite64			350
#define TARGET_NR_rt_sigreturn		351
#define TARGET_NR_rt_sigaction		352
#define TARGET_NR_rt_sigprocmask		353
#define TARGET_NR_rt_sigpending		354
#define TARGET_NR_rt_sigtimedwait		355
#define TARGET_NR_rt_sigqueueinfo		356
#define TARGET_NR_rt_sigsuspend		357
#define TARGET_NR_select			358
#define TARGET_NR_gettimeofday		359
#define TARGET_NR_settimeofday		360
#define TARGET_NR_getitimer			361
#define TARGET_NR_setitimer			362
#define TARGET_NR_utimes			363
#define TARGET_NR_getrusage			364
#define TARGET_NR_wait4			365
#define TARGET_NR_adjtimex			366
#define TARGET_NR_getcwd			367
#define TARGET_NR_capget			368
#define TARGET_NR_capset			369
#define TARGET_NR_sendfile			370
#define TARGET_NR_setresgid			371
#define TARGET_NR_getresgid			372
#define TARGET_NR_dipc			373
#define TARGET_NR_pivot_root			374
#define TARGET_NR_mincore			375
#define TARGET_NR_pciconfig_iobase		376
#define TARGET_NR_getdents64			377
#define TARGET_NR_gettid			378
#define TARGET_NR_readahead			379
/* 380 is unused */
#define TARGET_NR_tkill			381
#define TARGET_NR_setxattr			382
#define TARGET_NR_lsetxattr			383
#define TARGET_NR_fsetxattr			384
#define TARGET_NR_getxattr			385
#define TARGET_NR_lgetxattr			386
#define TARGET_NR_fgetxattr			387
#define TARGET_NR_listxattr			388
#define TARGET_NR_llistxattr			389
#define TARGET_NR_flistxattr			390
#define TARGET_NR_removexattr		391
#define TARGET_NR_lremovexattr		392
#define TARGET_NR_fremovexattr		393
#define TARGET_NR_futex			394
#define TARGET_NR_sched_setaffinity		395
#define TARGET_NR_sched_getaffinity		396
#define TARGET_NR_tuxcall			397
#define TARGET_NR_io_setup			398
#define TARGET_NR_io_destroy			399
#define TARGET_NR_io_getevents		400
#define TARGET_NR_io_submit			401
#define TARGET_NR_io_cancel			402
#define TARGET_NR_exit_group			405
#define TARGET_NR_lookup_dcookie		406
#define TARGET_NR_sys_epoll_create		407
#define TARGET_NR_sys_epoll_ctl		408
#define TARGET_NR_sys_epoll_wait		409
#define TARGET_NR_remap_file_pages		410
#define TARGET_NR_set_tid_address		411
#define TARGET_NR_restart_syscall		412
#define TARGET_NR_fadvise64			413
#define TARGET_NR_timer_create		414
#define TARGET_NR_timer_settime		415
#define TARGET_NR_timer_gettime		416
#define TARGET_NR_timer_getoverrun		417
#define TARGET_NR_timer_delete		418
#define TARGET_NR_clock_settime		419
#define TARGET_NR_clock_gettime		420
#define TARGET_NR_clock_getres		421
#define TARGET_NR_clock_nanosleep		422
#define TARGET_NR_semtimedop			423
#define TARGET_NR_tgkill			424
#define TARGET_NR_stat64			425
#define TARGET_NR_lstat64			426
#define TARGET_NR_fstat64			427
#define TARGET_NR_vserver			428
#define TARGET_NR_mbind			429
#define TARGET_NR_get_mempolicy		430
#define TARGET_NR_set_mempolicy		431
#define TARGET_NR_mq_open			432
#define TARGET_NR_mq_unlink			433
#define TARGET_NR_mq_timedsend		434
#define TARGET_NR_mq_timedreceive		435
#define TARGET_NR_mq_notify			436
#define TARGET_NR_mq_getsetattr		437
#define TARGET_NR_waitid			438
#define TARGET_NR_add_key			439
#define TARGET_NR_request_key		440
#define TARGET_NR_keyctl			441
#define TARGET_NR_ioprio_set			442
#define TARGET_NR_ioprio_get			443
#define TARGET_NR_inotify_init		444
#define TARGET_NR_inotify_add_watch		445
#define TARGET_NR_inotify_rm_watch		446
#define TARGET_NR_fdatasync			447
#define TARGET_NR_kexec_load			448
#define TARGET_NR_migrate_pages		449
#define TARGET_NR_openat			450
#define TARGET_NR_mkdirat			451
#define TARGET_NR_mknodat			452
#define TARGET_NR_fchownat			453
#define TARGET_NR_futimesat			454
#define TARGET_NR_fstatat64			455
#define TARGET_NR_unlinkat			456
#define TARGET_NR_renameat			457
#define TARGET_NR_linkat			458
#define TARGET_NR_symlinkat			459
#define TARGET_NR_readlinkat			460
#define TARGET_NR_fchmodat			461
#define TARGET_NR_faccessat			462
#define TARGET_NR_pselect6			463
#define TARGET_NR_ppoll			464
#define TARGET_NR_unshare			465
#define TARGET_NR_set_robust_list		466
#define TARGET_NR_get_robust_list		467
#define TARGET_NR_splice			468
#define TARGET_NR_sync_file_range		469
#define TARGET_NR_tee			470
#define TARGET_NR_vmsplice			471
#define TARGET_NR_move_pages			472
#define TARGET_NR_getcpu			473
#define TARGET_NR_epoll_pwait		474
#define TARGET_NR_utimensat			475
#define TARGET_NR_signalfd			476
#define TARGET_NR_timerfd			477
#define TARGET_NR_eventfd			478
#define TARGET_NR_recvmmsg                      479
#define TARGET_NR_fallocate                     480
#define TARGET_NR_timerfd_create                481
#define TARGET_NR_timerfd_settime               482
#define TARGET_NR_timerfd_gettime               483
#define TARGET_NR_signalfd4                     484
#define TARGET_NR_eventfd2                      485
#define TARGET_NR_epoll_create1                 486
#define TARGET_NR_dup3                          487
#define TARGET_NR_pipe2                         488
#define TARGET_NR_inotify_init1                 489
#define TARGET_NR_preadv                        490
#define TARGET_NR_pwritev                       491
#define TARGET_NR_rt_tgsigqueueinfo             492
#define TARGET_NR_perf_event_open               493
#define TARGET_NR_fanotify_init                 494
#define TARGET_NR_fanotify_mark                 495
#define TARGET_NR_prlimit64                     496
#define TARGET_NR_name_to_handle_at             497
#define TARGET_NR_open_by_handle_at             498
#define TARGET_NR_clock_adjtime                 499
#define TARGET_NR_syncfs                        500
#define TARGET_NR_setns                         501
#define TARGET_NR_accept4                       502
#define TARGET_NR_sendmmsg                      503
#define TARGET_NR_process_vm_readv              504
#define TARGET_NR_process_vm_writev             505
#define TARGET_NR_kcmp                          506
#define TARGET_NR_finit_module                  507

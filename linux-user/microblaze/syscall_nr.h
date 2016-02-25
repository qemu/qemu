#define TARGET_NR_restart_syscall	0 /* ok */
#define TARGET_NR_exit		1 /* ok */
#define TARGET_NR_fork		2 /* not for no MMU - weird */
#define TARGET_NR_read		3 /* ok */
#define TARGET_NR_write		4 /* ok */
#define TARGET_NR_open		5 /* openat */
#define TARGET_NR_close		6 /* ok */
#define TARGET_NR_waitpid		7 /* waitid */
#define TARGET_NR_creat		8 /* openat */
#define TARGET_NR_link		9 /* linkat */
#define TARGET_NR_unlink		10 /* unlinkat */
#define TARGET_NR_execve		11 /* ok */
#define TARGET_NR_chdir		12 /* ok */
#define TARGET_NR_time		13 /* obsolete -> sys_gettimeofday */
#define TARGET_NR_mknod		14 /* mknodat */
#define TARGET_NR_chmod		15 /* fchmodat */
#define TARGET_NR_lchown		16 /* ok */
#define TARGET_NR_break		17 /* don't know */
#define TARGET_NR_oldstat		18 /* remove */
#define TARGET_NR_lseek		19 /* ok */
#define TARGET_NR_getpid		20 /* ok */
#define TARGET_NR_mount		21 /* ok */
#define TARGET_NR_umount		22 /* ok */  /* use only umount2 */
#define TARGET_NR_setuid		23 /* ok */
#define TARGET_NR_getuid		24 /* ok */
#define TARGET_NR_stime		25 /* obsolete -> sys_settimeofday */
#define TARGET_NR_ptrace		26 /* ok */
#define TARGET_NR_alarm		27 /* obsolete -> sys_setitimer */
#define TARGET_NR_oldfstat		28 /* remove */
#define TARGET_NR_pause		29 /* obsolete -> sys_rt_sigtimedwait */
#define TARGET_NR_utime		30 /* obsolete -> sys_utimesat */
#define TARGET_NR_stty		31 /* remove */
#define TARGET_NR_gtty		32 /* remove */
#define TARGET_NR_access		33 /* faccessat */
#define TARGET_NR_nice		34 /* can be implemented by sys_setpriority */
#define TARGET_NR_ftime		35 /* remove */
#define TARGET_NR_sync		36 /* ok */
#define TARGET_NR_kill		37 /* ok */
#define TARGET_NR_rename		38 /* renameat */
#define TARGET_NR_mkdir		39 /* mkdirat */
#define TARGET_NR_rmdir		40 /* unlinkat */
#define TARGET_NR_dup		41 /* ok */
#define TARGET_NR_pipe		42 /* ok */
#define TARGET_NR_times		43 /* ok */
#define TARGET_NR_prof		44 /* remove */
#define TARGET_NR_brk		45 /* ok -mmu, nommu specific */
#define TARGET_NR_setgid		46 /* ok */
#define TARGET_NR_getgid		47 /* ok */
#define TARGET_NR_signal		48 /* obsolete -> sys_rt_sigaction */
#define TARGET_NR_geteuid		49 /* ok */
#define TARGET_NR_getegid		50 /* ok */
#define TARGET_NR_acct		51 /* add it and then I can disable it */
#define TARGET_NR_umount2		52 /* remove */
#define TARGET_NR_lock		53 /* remove */
#define TARGET_NR_ioctl		54 /* ok */
#define TARGET_NR_fcntl		55 /* ok -> 64bit version*/
#define TARGET_NR_mpx		56 /* remove */
#define TARGET_NR_setpgid		57 /* ok */
#define TARGET_NR_ulimit		58 /* remove */
#define TARGET_NR_oldolduname	59 /* remove */
#define TARGET_NR_umask		60 /* ok */
#define TARGET_NR_chroot		61 /* ok */
#define TARGET_NR_ustat		62 /* obsolete -> statfs64 */
#define TARGET_NR_dup2		63 /* ok */
#define TARGET_NR_getppid		64 /* ok */
#define TARGET_NR_getpgrp		65 /* obsolete -> sys_getpgid */
#define TARGET_NR_setsid		66 /* ok */
#define TARGET_NR_sigaction		67 /* obsolete -> rt_sigaction */
#define TARGET_NR_sgetmask		68 /* obsolete -> sys_rt_sigprocmask */
#define TARGET_NR_ssetmask		69 /* obsolete ->sys_rt_sigprocmask */
#define TARGET_NR_setreuid		70 /* ok */
#define TARGET_NR_setregid		71 /* ok */
#define TARGET_NR_sigsuspend		72 /* obsolete -> rt_sigsuspend */
#define TARGET_NR_sigpending		73 /* obsolete -> sys_rt_sigpending */
#define TARGET_NR_sethostname	74 /* ok */
#define TARGET_NR_setrlimit		75 /* ok */
#define TARGET_NR_getrlimit		76 /* ok Back compatible 2Gig limited rlimit */
#define TARGET_NR_getrusage		77 /* ok */
#define TARGET_NR_gettimeofday	78 /* ok */
#define TARGET_NR_settimeofday	79 /* ok */
#define TARGET_NR_getgroups		80 /* ok */
#define TARGET_NR_setgroups		81 /* ok */
#define TARGET_NR_select		82 /* obsolete -> sys_pselect7 */
#define TARGET_NR_symlink		83 /* symlinkat */
#define TARGET_NR_oldlstat		84 /* remove */
#define TARGET_NR_readlink		85 /* obsolete -> sys_readlinkat */
#define TARGET_NR_uselib		86 /* remove */
#define TARGET_NR_swapon		87 /* ok */
#define TARGET_NR_reboot		88 /* ok */
#define TARGET_NR_readdir		89 /* remove ? */
#define TARGET_NR_mmap		90 /* obsolete -> sys_mmap2 */
#define TARGET_NR_munmap		91 /* ok - mmu and nommu */
#define TARGET_NR_truncate		92 /* ok or truncate64 */
#define TARGET_NR_ftruncate		93 /* ok or ftruncate64 */
#define TARGET_NR_fchmod		94 /* ok */
#define TARGET_NR_fchown		95 /* ok */
#define TARGET_NR_getpriority	96 /* ok */
#define TARGET_NR_setpriority	97 /* ok */
#define TARGET_NR_profil		98 /* remove */
#define TARGET_NR_statfs		99 /* ok or statfs64 */
#define TARGET_NR_fstatfs		100  /* ok or fstatfs64 */
#define TARGET_NR_ioperm		101 /* remove */
#define TARGET_NR_socketcall		102 /* remove */
#define TARGET_NR_syslog		103 /* ok */
#define TARGET_NR_setitimer		104 /* ok */
#define TARGET_NR_getitimer		105 /* ok */
#define TARGET_NR_stat		106 /* remove */
#define TARGET_NR_lstat		107 /* remove */
#define TARGET_NR_fstat		108 /* remove */
#define TARGET_NR_olduname		109 /* remove */
#define TARGET_NR_iopl		110 /* remove */
#define TARGET_NR_vhangup		111 /* ok */
#define TARGET_NR_idle		112 /* remove */
#define TARGET_NR_vm86old		113 /* remove */
#define TARGET_NR_wait4		114 /* obsolete -> waitid */
#define TARGET_NR_swapoff		115 /* ok */
#define TARGET_NR_sysinfo		116 /* ok */
#define TARGET_NR_ipc		117 /* remove - direct call */
#define TARGET_NR_fsync		118 /* ok */
#define TARGET_NR_sigreturn		119 /* obsolete -> sys_rt_sigreturn */
#define TARGET_NR_clone		120 /* ok */
#define TARGET_NR_setdomainname	121 /* ok */
#define TARGET_NR_uname		122 /* remove */
#define TARGET_NR_modify_ldt		123 /* remove */
#define TARGET_NR_adjtimex		124 /* ok */
#define TARGET_NR_mprotect		125 /* remove */
#define TARGET_NR_sigprocmask	126 /* obsolete -> sys_rt_sigprocmask */
#define TARGET_NR_create_module	127 /* remove */
#define TARGET_NR_init_module	128 /* ok */
#define TARGET_NR_delete_module	129 /* ok */
#define TARGET_NR_get_kernel_syms	130 /* remove */
#define TARGET_NR_quotactl		131 /* ok */
#define TARGET_NR_getpgid		132 /* ok */
#define TARGET_NR_fchdir		133 /* ok */
#define TARGET_NR_bdflush		134 /* remove */
#define TARGET_NR_sysfs		135 /* needed for busybox */
#define TARGET_NR_personality	136 /* ok */
#define TARGET_NR_afs_syscall	137 /* Syscall for Andrew File System */
#define TARGET_NR_setfsuid		138 /* ok */
#define TARGET_NR_setfsgid		139 /* ok */
#define TARGET_NR__llseek		140 /* remove only lseek */
#define TARGET_NR_getdents		141 /* ok or getdents64 */
#define TARGET_NR__newselect		142 /* remove */
#define TARGET_NR_flock		143 /* ok */
#define TARGET_NR_msync		144 /* remove */
#define TARGET_NR_readv		145 /* ok */
#define TARGET_NR_writev		146 /* ok */
#define TARGET_NR_getsid		147 /* ok */
#define TARGET_NR_fdatasync		148 /* ok */
#define TARGET_NR__sysctl		149 /* remove */
#define TARGET_NR_mlock		150 /* ok - nommu or mmu */
#define TARGET_NR_munlock		151 /* ok - nommu or mmu */
#define TARGET_NR_mlockall		152 /* ok - nommu or mmu */
#define TARGET_NR_munlockall		153 /* ok - nommu or mmu */
#define TARGET_NR_sched_setparam		154 /* ok */
#define TARGET_NR_sched_getparam		155 /* ok */
#define TARGET_NR_sched_setscheduler		156 /* ok */
#define TARGET_NR_sched_getscheduler		157 /* ok */
#define TARGET_NR_sched_yield		158 /* ok */
#define TARGET_NR_sched_get_priority_max	159 /* ok */
#define TARGET_NR_sched_get_priority_min	160 /* ok */
#define TARGET_NR_sched_rr_get_interval	161 /* ok */
#define TARGET_NR_nanosleep		162 /* ok */
#define TARGET_NR_mremap		163 /* ok - nommu or mmu */
#define TARGET_NR_setresuid		164 /* ok */
#define TARGET_NR_getresuid		165 /* ok */
#define TARGET_NR_vm86		166 /* remove */
#define TARGET_NR_query_module	167 /* ok */
#define TARGET_NR_poll		168 /* obsolete -> sys_ppoll */
#define TARGET_NR_nfsservctl		169 /* ok */
#define TARGET_NR_setresgid		170 /* ok */
#define TARGET_NR_getresgid		171 /* ok */
#define TARGET_NR_prctl		172 /* ok */
#define TARGET_NR_rt_sigreturn	173 /* ok */
#define TARGET_NR_rt_sigaction	174 /* ok */
#define TARGET_NR_rt_sigprocmask	175 /* ok */
#define TARGET_NR_rt_sigpending	176 /* ok */
#define TARGET_NR_rt_sigtimedwait	177 /* ok */
#define TARGET_NR_rt_sigqueueinfo	178 /* ok */
#define TARGET_NR_rt_sigsuspend	179 /* ok */
#define TARGET_NR_pread64		180 /* ok */
#define TARGET_NR_pwrite64		181 /* ok */
#define TARGET_NR_chown		182 /* obsolete -> fchownat */
#define TARGET_NR_getcwd		183 /* ok */
#define TARGET_NR_capget		184 /* ok */
#define TARGET_NR_capset		185 /* ok */
#define TARGET_NR_sigaltstack	186 /* remove */
#define TARGET_NR_sendfile		187 /* ok -> exist 64bit version*/
#define TARGET_NR_getpmsg		188 /* remove - some people actually want streams */
#define TARGET_NR_putpmsg		189 /* remove - some people actually want streams */
#define TARGET_NR_vfork		190 /* for noMMU - group with clone -> maybe remove */
#define TARGET_NR_ugetrlimit		191 /* remove - SuS compliant getrlimit */
#define TARGET_NR_mmap2		192 /* ok */
#define TARGET_NR_truncate64		193 /* ok */
#define TARGET_NR_ftruncate64	194 /* ok */
#define TARGET_NR_stat64		195 /* remove _ARCH_WANT_STAT64 */
#define TARGET_NR_lstat64		196 /* remove _ARCH_WANT_STAT64 */
#define TARGET_NR_fstat64		197 /* remove _ARCH_WANT_STAT64 */
#define TARGET_NR_lchown32		198 /* ok - without 32 */
#define TARGET_NR_getuid32		199 /* ok - without 32 */
#define TARGET_NR_getgid32		200 /* ok - without 32 */
#define TARGET_NR_geteuid32		201 /* ok - without 32 */
#define TARGET_NR_getegid32		202 /* ok - without 32 */
#define TARGET_NR_setreuid32		203 /* ok - without 32 */
#define TARGET_NR_setregid32		204 /* ok - without 32 */
#define TARGET_NR_getgroups32	205 /* ok - without 32 */
#define TARGET_NR_setgroups32	206 /* ok - without 32 */
#define TARGET_NR_fchown32		207 /* ok - without 32 */
#define TARGET_NR_setresuid32	208 /* ok - without 32 */
#define TARGET_NR_getresuid32	209 /* ok - without 32 */
#define TARGET_NR_setresgid32	210 /* ok - without 32 */
#define TARGET_NR_getresgid32	211 /* ok - without 32 */
#define TARGET_NR_chown32		212 /* ok - without 32 -obsolete -> fchownat */
#define TARGET_NR_setuid32		213 /* ok - without 32 */
#define TARGET_NR_setgid32		214 /* ok - without 32 */
#define TARGET_NR_setfsuid32		215 /* ok - without 32 */
#define TARGET_NR_setfsgid32		216 /* ok - without 32 */
#define TARGET_NR_pivot_root		217 /* ok */
#define TARGET_NR_mincore		218 /* ok */
#define TARGET_NR_madvise		219 /* ok */
//#define TARGET_NR_madvise1		219 /* remove delete when C lib stub is removed */
#define TARGET_NR_getdents64		220 /* ok */
#define TARGET_NR_fcntl64		221 /* ok */
/* 223 is unused */
#define TARGET_NR_gettid		224 /* ok */
#define TARGET_NR_readahead		225 /* ok */
#define TARGET_NR_setxattr		226 /* ok */
#define TARGET_NR_lsetxattr		227 /* ok */
#define TARGET_NR_fsetxattr		228 /* ok */
#define TARGET_NR_getxattr		229 /* ok */
#define TARGET_NR_lgetxattr		230 /* ok */
#define TARGET_NR_fgetxattr		231 /* ok */
#define TARGET_NR_listxattr		232 /* ok */
#define TARGET_NR_llistxattr		233 /* ok */
#define TARGET_NR_flistxattr		234 /* ok */
#define TARGET_NR_removexattr	235 /* ok */
#define TARGET_NR_lremovexattr	236 /* ok */
#define TARGET_NR_fremovexattr	237 /* ok */
#define TARGET_NR_tkill		238 /* ok */
#define TARGET_NR_sendfile64		239 /* ok */
#define TARGET_NR_futex		240 /* ok */
#define TARGET_NR_sched_setaffinity	241 /* ok */
#define TARGET_NR_sched_getaffinity	242 /* ok */
#define TARGET_NR_set_thread_area	243 /* remove */
#define TARGET_NR_get_thread_area	244 /* remove */
#define TARGET_NR_io_setup		245 /* ok */
#define TARGET_NR_io_destroy		246 /* ok */
#define TARGET_NR_io_getevents	247 /* ok */
#define TARGET_NR_io_submit		248 /* ok */
#define TARGET_NR_io_cancel		249 /* ok */
#define TARGET_NR_fadvise64		250 /* remove -> sys_fadvise64_64 */
/* 251 is available for reuse (was briefly sys_set_zone_reclaim) */
#define TARGET_NR_exit_group		252 /* ok */
#define TARGET_NR_lookup_dcookie	253 /* ok */
#define TARGET_NR_epoll_create	254 /* ok */
#define TARGET_NR_epoll_ctl		255 /* ok */
#define TARGET_NR_epoll_wait		256 /* obsolete -> sys_epoll_pwait */
#define TARGET_NR_remap_file_pages	257 /* only for mmu */
#define TARGET_NR_set_tid_address	258 /* ok */
#define TARGET_NR_timer_create	259 /* ok */
#define TARGET_NR_timer_settime	(TARGET_NR_timer_create+1) /* 260 */ /* ok */
#define TARGET_NR_timer_gettime	(TARGET_NR_timer_create+2) /* 261 */ /* ok */
#define TARGET_NR_timer_getoverrun	(TARGET_NR_timer_create+3) /* 262 */ /* ok */
#define TARGET_NR_timer_delete	(TARGET_NR_timer_create+4) /* 263 */ /* ok */
#define TARGET_NR_clock_settime	(TARGET_NR_timer_create+5) /* 264 */ /* ok */
#define TARGET_NR_clock_gettime	(TARGET_NR_timer_create+6) /* 265 */ /* ok */
#define TARGET_NR_clock_getres	(TARGET_NR_timer_create+7) /* 266 */ /* ok */
#define TARGET_NR_clock_nanosleep	(TARGET_NR_timer_create+8) /* 267 */ /* ok */
#define TARGET_NR_statfs64		268 /* ok */
#define TARGET_NR_fstatfs64		269 /* ok */
#define TARGET_NR_tgkill		270 /* ok */
#define TARGET_NR_utimes		271 /* obsolete -> sys_futimesat */
#define TARGET_NR_fadvise64_64	272 /* ok */
#define TARGET_NR_vserver		273 /* ok */
#define TARGET_NR_mbind		274 /* only for mmu */
#define TARGET_NR_get_mempolicy	275 /* only for mmu */
#define TARGET_NR_set_mempolicy	276 /* only for mmu */
#define TARGET_NR_mq_open		277 /* ok */
#define TARGET_NR_mq_unlink		(TARGET_NR_mq_open+1) /* 278 */ /* ok */
#define TARGET_NR_mq_timedsend	(TARGET_NR_mq_open+2) /* 279 */ /* ok */
#define TARGET_NR_mq_timedreceive	(TARGET_NR_mq_open+3) /* 280 */ /* ok */
#define TARGET_NR_mq_notify		(TARGET_NR_mq_open+4) /* 281 */ /* ok */
#define TARGET_NR_mq_getsetattr	(TARGET_NR_mq_open+5) /* 282 */ /* ok */
#define TARGET_NR_kexec_load		283 /* ok */
#define TARGET_NR_waitid		284 /* ok */
/* #define TARGET_NR_sys_setaltroot	285 */
#define TARGET_NR_add_key		286 /* ok */
#define TARGET_NR_request_key	287 /* ok */
#define TARGET_NR_keyctl		288 /* ok */
#define TARGET_NR_ioprio_set		289 /* ok */
#define TARGET_NR_ioprio_get		290 /* ok */
#define TARGET_NR_inotify_init	291 /* ok */
#define TARGET_NR_inotify_add_watch	292 /* ok */
#define TARGET_NR_inotify_rm_watch	293 /* ok */
#define TARGET_NR_migrate_pages	294 /* mmu */
#define TARGET_NR_openat		295 /* ok */
#define TARGET_NR_mkdirat		296 /* ok */
#define TARGET_NR_mknodat		297 /* ok */
#define TARGET_NR_fchownat		298 /* ok */
#define TARGET_NR_futimesat		299 /* obsolete -> sys_utimesat */
#define TARGET_NR_fstatat64		300 /* stat64 */
#define TARGET_NR_unlinkat		301 /* ok */
#define TARGET_NR_renameat		302 /* ok */
#define TARGET_NR_linkat		303 /* ok */
#define TARGET_NR_symlinkat		304 /* ok */
#define TARGET_NR_readlinkat		305 /* ok */
#define TARGET_NR_fchmodat		306 /* ok */
#define TARGET_NR_faccessat		307 /* ok */
#define TARGET_NR_pselect6		308 /* obsolete -> sys_pselect7 */
#define TARGET_NR_ppoll		309 /* ok */
#define TARGET_NR_unshare		310 /* ok */
#define TARGET_NR_set_robust_list	311 /* ok */
#define TARGET_NR_get_robust_list	312 /* ok */
#define TARGET_NR_splice		313 /* ok */
#define TARGET_NR_sync_file_range	314 /* ok */
#define TARGET_NR_tee		315 /* ok */
#define TARGET_NR_vmsplice		316 /* ok */
#define TARGET_NR_move_pages		317 /* mmu */
#define TARGET_NR_getcpu		318 /* ok */
#define TARGET_NR_epoll_pwait	319 /* ok */
#define TARGET_NR_utimensat		320 /* ok */
#define TARGET_NR_signalfd		321 /* ok */
#define TARGET_NR_timerfd_create	322 /* ok */
#define TARGET_NR_eventfd		323 /* ok */
#define TARGET_NR_fallocate		324 /* ok */
#define TARGET_NR_semtimedop		325 /* ok - semaphore group */
#define TARGET_NR_timerfd_settime	326 /* ok */
#define TARGET_NR_timerfd_gettime	327 /* ok */
/* sysv ipc syscalls */
#define TARGET_NR_semctl		328 /* ok */
#define TARGET_NR_semget		329 /* ok */
#define TARGET_NR_semop		330 /* ok */
#define TARGET_NR_msgctl		331 /* ok */
#define TARGET_NR_msgget		332 /* ok */
#define TARGET_NR_msgrcv		333 /* ok */
#define TARGET_NR_msgsnd		334 /* ok */
#define TARGET_NR_shmat		335 /* ok */
#define TARGET_NR_shmctl		336 /* ok */
#define TARGET_NR_shmdt		337 /* ok */
#define TARGET_NR_shmget		338 /* ok */


#define TARGET_NR_signalfd4		339 /* new */
#define TARGET_NR_eventfd2		340 /* new */
#define TARGET_NR_epoll_create1	341 /* new */
#define TARGET_NR_dup3		342 /* new */
#define TARGET_NR_pipe2		343 /* new */
#define TARGET_NR_inotify_init1	344 /* new */
#define TARGET_NR_socket		345 /* new */
#define TARGET_NR_socketpair		346 /* new */
#define TARGET_NR_bind		347 /* new */
#define TARGET_NR_listen		348 /* new */
#define TARGET_NR_accept		349 /* new */
#define TARGET_NR_connect		350 /* new */
#define TARGET_NR_getsockname	351 /* new */
#define TARGET_NR_getpeername	352 /* new */
#define TARGET_NR_sendto		353 /* new */
#define TARGET_NR_send		354 /* new */
#define TARGET_NR_recvfrom		355 /* new */
#define TARGET_NR_recv		356 /* new */
#define TARGET_NR_setsockopt		357 /* new */
#define TARGET_NR_getsockopt		358 /* new */
#define TARGET_NR_shutdown		359 /* new */
#define TARGET_NR_sendmsg		360 /* new */
#define TARGET_NR_recvmsg		361 /* new */
#define TARGET_NR_accept04		362 /* new */
#define TARGET_NR_preadv                363 /* new */
#define TARGET_NR_pwritev               364 /* new */
#define TARGET_NR_rt_tgsigqueueinfo     365 /* new */
#define TARGET_NR_perf_event_open       366 /* new */
#define TARGET_NR_recvmmsg              367 /* new */
#define TARGET_NR_fanotify_init         368
#define TARGET_NR_fanotify_mark         369
#define TARGET_NR_prlimit64             370
#define TARGET_NR_name_to_handle_at     371
#define TARGET_NR_open_by_handle_at     372
#define TARGET_NR_clock_adjtime         373
#define TARGET_NR_syncfs                374
#define TARGET_NR_setns                 375
#define TARGET_NR_sendmmsg              376
#define TARGET_NR_process_vm_readv      377
#define TARGET_NR_process_vm_writev     378
#define TARGET_NR_kcmp                  379
#define TARGET_NR_finit_module          380
#define TARGET_NR_sched_setattr         381
#define TARGET_NR_sched_getattr         382
#define TARGET_NR_renameat2             383
#define TARGET_NR_seccomp               384
#define TARGET_NR_getrandom             385
#define TARGET_NR_memfd_create          386
#define TARGET_NR_bpf                   387
#define TARGET_NR_execveat              388

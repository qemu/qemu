#ifndef LINUX_USER_OPENRISC_SYSCALL_NR_H
#define LINUX_USER_OPENRISC_SYSCALL_NR_H

#define TARGET_NR_io_setup 0
#define TARGET_NR_io_destroy 1
#define TARGET_NR_io_submit 2
#define TARGET_NR_io_cancel 3
#define TARGET_NR_io_getevents 4

/* fs/xattr.c */
#define TARGET_NR_setxattr 5
#define TARGET_NR_lsetxattr 6
#define TARGET_NR_fsetxattr 7
#define TARGET_NR_getxattr 8
#define TARGET_NR_lgetxattr 9
#define TARGET_NR_fgetxattr 10
#define TARGET_NR_listxattr 11
#define TARGET_NR_llistxattr 12
#define TARGET_NR_flistxattr 13
#define TARGET_NR_removexattr 14
#define TARGET_NR_lremovexattr 15
#define TARGET_NR_fremovexattr 16

/* fs/dcache.c */
#define TARGET_NR_getcwd 17

/* fs/cookies.c */
#define TARGET_NR_lookup_dcookie 18

/* fs/eventfd.c */
#define TARGET_NR_eventfd2 19

/* fs/eventpoll.c */
#define TARGET_NR_epoll_create1 20
#define TARGET_NR_epoll_ctl 21
#define TARGET_NR_epoll_pwait 22

/* fs/fcntl.c */
#define TARGET_NR_dup 23
#define TARGET_NR_dup3 24
#define TARGET_NR_3264_fcntl 25

/* fs/inotify_user.c */
#define TARGET_NR_inotify_init1 26
#define TARGET_NR_inotify_add_watch 27
#define TARGET_NR_inotify_rm_watch 28

/* fs/ioctl.c */
#define TARGET_NR_ioctl 29

/* fs/ioprio.c */
#define TARGET_NR_ioprio_set 30
#define TARGET_NR_ioprio_get 31

/* fs/locks.c */
#define TARGET_NR_flock 32

/* fs/namei.c */
#define TARGET_NR_mknodat 33
#define TARGET_NR_mkdirat 34
#define TARGET_NR_unlinkat 35
#define TARGET_NR_symlinkat 36
#define TARGET_NR_linkat 37
#define TARGET_NR_renameat 38

/* fs/namespace.c */
#define TARGET_NR_umount2 39
#define TARGET_NR_mount 40
#define TARGET_NR_pivot_root 41

/* fs/nfsctl.c */
#define TARGET_NR_nfsservctl 42

/* fs/open.c */
#define TARGET_NR_3264_statfs 43
#define TARGET_NR_3264_fstatfs 44
#define TARGET_NR_3264_truncate 45
#define TARGET_NR_3264_ftruncate 46

#define TARGET_NR_fallocate 47
#define TARGET_NR_faccessat 48
#define TARGET_NR_chdir 49
#define TARGET_NR_fchdir 50
#define TARGET_NR_chroot 51
#define TARGET_NR_fchmod 52
#define TARGET_NR_fchmodat 53
#define TARGET_NR_fchownat 54
#define TARGET_NR_fchown 55
#define TARGET_NR_openat 56
#define TARGET_NR_close 57
#define TARGET_NR_vhangup 58

/* fs/pipe.c */
#define TARGET_NR_pipe2 59

/* fs/quota.c */
#define TARGET_NR_quotactl 60

/* fs/readdir.c */
#define TARGET_NR_getdents64 61

/* fs/read_write.c */
#define TARGET_NR_3264_lseek 62
#define TARGET_NR_read 63
#define TARGET_NR_write 64
#define TARGET_NR_readv 65
#define TARGET_NR_writev 66
#define TARGET_NR_pread64 67
#define TARGET_NR_pwrite64 68
#define TARGET_NR_preadv 69
#define TARGET_NR_pwritev 70

/* fs/sendfile.c */
#define TARGET_NR_3264_sendfile 71

/* fs/select.c */
#define TARGET_NR_pselect6 72
#define TARGET_NR_ppoll 73

/* fs/signalfd.c */
#define TARGET_NR_signalfd4 74

/* fs/splice.c */
#define TARGET_NR_vmsplice 75
#define TARGET_NR_splice 76
#define TARGET_NR_tee 77

/* fs/stat.c */
#define TARGET_NR_readlinkat 78
#define TARGET_NR_3264_fstatat 79
#define TARGET_NR_3264_fstat 80

/* fs/sync.c */
#define TARGET_NR_sync 81
#define TARGET_NR_fsync 82
#define TARGET_NR_fdatasync 83

#ifdef __ARCH_WANT_SYNC_FILE_RANGE2
#define TARGET_NR_sync_file_range2 84
#else
#define TARGET_NR_sync_file_range 84
#endif

/* fs/timerfd.c */
#define TARGET_NR_timerfd_create 85
#define TARGET_NR_timerfd_settime 86
#define TARGET_NR_timerfd_gettime 87

/* fs/utimes.c */
#define TARGET_NR_utimensat 88

/* kernel/acct.c */
#define TARGET_NR_acct 89

/* kernel/capability.c */
#define TARGET_NR_capget 90
#define TARGET_NR_capset 91

/* kernel/exec_domain.c */
#define TARGET_NR_personality 92

/* kernel/exit.c */
#define TARGET_NR_exit 93
#define TARGET_NR_exit_group 94
#define TARGET_NR_waitid 95

/* kernel/fork.c */
#define TARGET_NR_set_tid_address 96
#define TARGET_NR_unshare 97

/* kernel/futex.c */
#define TARGET_NR_futex 98
#define TARGET_NR_set_robust_list 99
#define TARGET_NR_get_robust_list 100

/* kernel/hrtimer.c */
#define TARGET_NR_nanosleep 101

/* kernel/itimer.c */
#define TARGET_NR_getitimer 102
#define TARGET_NR_setitimer 103

/* kernel/kexec.c */
#define TARGET_NR_kexec_load 104

/* kernel/module.c */
#define TARGET_NR_init_module 105
#define TARGET_NR_delete_module 106

/* kernel/posix-timers.c */
#define TARGET_NR_timer_create 107
#define TARGET_NR_timer_gettime 108
#define TARGET_NR_timer_getoverrun 109
#define TARGET_NR_timer_settime 110
#define TARGET_NR_timer_delete 111
#define TARGET_NR_clock_settime 112
#define TARGET_NR_clock_gettime 113
#define TARGET_NR_clock_getres 114
#define TARGET_NR_clock_nanosleep 115

/* kernel/printk.c */
#define TARGET_NR_syslog 116

/* kernel/ptrace.c */
#define TARGET_NR_ptrace 117

/* kernel/sched.c */
#define TARGET_NR_sched_setparam 118
#define TARGET_NR_sched_setscheduler 119
#define TARGET_NR_sched_getscheduler 120
#define TARGET_NR_sched_getparam 121
#define TARGET_NR_sched_setaffinity 122
#define TARGET_NR_sched_getaffinity 123
#define TARGET_NR_sched_yield 124
#define TARGET_NR_sched_get_priority_max 125
#define TARGET_NR_sched_get_priority_min 126
#define TARGET_NR_sched_rr_get_interval 127

/* kernel/signal.c */
#define TARGET_NR_restart_syscall 128
#define TARGET_NR_kill 129
#define TARGET_NR_tkill 130
#define TARGET_NR_tgkill 131
#define TARGET_NR_sigaltstack 132
#define TARGET_NR_rt_sigsuspend 133
#define TARGET_NR_rt_sigaction 134
#define TARGET_NR_rt_sigprocmask 135
#define TARGET_NR_rt_sigpending 136
#define TARGET_NR_rt_sigtimedwait 137
#define TARGET_NR_rt_sigqueueinfo 138
#define TARGET_NR_rt_sigreturn 139

/* kernel/sys.c */
#define TARGET_NR_setpriority 140
#define TARGET_NR_getpriority 141
#define TARGET_NR_reboot 142
#define TARGET_NR_setregid 143
#define TARGET_NR_setgid 144
#define TARGET_NR_setreuid 145
#define TARGET_NR_setuid 146
#define TARGET_NR_setresuid 147
#define TARGET_NR_getresuid 148
#define TARGET_NR_setresgid 149
#define TARGET_NR_getresgid 150
#define TARGET_NR_setfsuid 151
#define TARGET_NR_setfsgid 152
#define TARGET_NR_times 153
#define TARGET_NR_setpgid 154
#define TARGET_NR_getpgid 155
#define TARGET_NR_getsid 156
#define TARGET_NR_setsid 157
#define TARGET_NR_getgroups 158
#define TARGET_NR_setgroups 159
#define TARGET_NR_uname 160
#define TARGET_NR_sethostname 161
#define TARGET_NR_setdomainname 162
#define TARGET_NR_getrlimit 163
#define TARGET_NR_setrlimit 164
#define TARGET_NR_getrusage 165
#define TARGET_NR_umask 166
#define TARGET_NR_prctl 167
#define TARGET_NR_getcpu 168

/* kernel/time.c */
#define TARGET_NR_gettimeofday 169
#define TARGET_NR_settimeofday 170
#define TARGET_NR_adjtimex 171

/* kernel/timer.c */
#define TARGET_NR_getpid 172
#define TARGET_NR_getppid 173
#define TARGET_NR_getuid 174
#define TARGET_NR_geteuid 175
#define TARGET_NR_getgid 176
#define TARGET_NR_getegid 177
#define TARGET_NR_gettid 178
#define TARGET_NR_sysinfo 179

/* ipc/mqueue.c */
#define TARGET_NR_mq_open 180
#define TARGET_NR_mq_unlink 181
#define TARGET_NR_mq_timedsend 182
#define TARGET_NR_mq_timedreceive 183
#define TARGET_NR_mq_notify 184
#define TARGET_NR_mq_getsetattr 185

/* ipc/msg.c */
#define TARGET_NR_msgget 186
#define TARGET_NR_msgctl 187
#define TARGET_NR_msgrcv 188
#define TARGET_NR_msgsnd 189

/* ipc/sem.c */
#define TARGET_NR_semget 190
#define TARGET_NR_semctl 191
#define TARGET_NR_semtimedop 192
#define TARGET_NR_semop 193

/* ipc/shm.c */
#define TARGET_NR_shmget 194
#define TARGET_NR_shmctl 195
#define TARGET_NR_shmat 196
#define TARGET_NR_shmdt 197

/* net/socket.c */
#define TARGET_NR_socket 198
#define TARGET_NR_socketpair 199
#define TARGET_NR_bind 200
#define TARGET_NR_listen 201
#define TARGET_NR_accept 202
#define TARGET_NR_connect 203
#define TARGET_NR_getsockname 204
#define TARGET_NR_getpeername 205
#define TARGET_NR_sendto 206
#define TARGET_NR_recvfrom 207
#define TARGET_NR_setsockopt 208
#define TARGET_NR_getsockopt 209
#define TARGET_NR_shutdown 210
#define TARGET_NR_sendmsg 211
#define TARGET_NR_recvmsg 212

/* mm/filemap.c */
#define TARGET_NR_readahead 213

/* mm/nommu.c, also with MMU */
#define TARGET_NR_brk 214
#define TARGET_NR_munmap 215
#define TARGET_NR_mremap 216

/* security/keys/keyctl.c */
#define TARGET_NR_add_key 217
#define TARGET_NR_request_key 218
#define TARGET_NR_keyctl 219

/* arch/example/kernel/sys_example.c */
#define TARGET_NR_clone 220
#define TARGET_NR_execve 221

#define TARGET_NR_3264_mmap 222
/* mm/fadvise.c */
#define TARGET_NR_3264_fadvise64 223

/* mm/, CONFIG_MMU only */
#ifndef __ARCH_NOMMU
#define TARGET_NR_swapon 224
#define TARGET_NR_swapoff 225
#define TARGET_NR_mprotect 226
#define TARGET_NR_msync 227
#define TARGET_NR_mlock 228
#define TARGET_NR_munlock 229
#define TARGET_NR_mlockall 230
#define TARGET_NR_munlockall 231
#define TARGET_NR_mincore 232
#define TARGET_NR_madvise 233
#define TARGET_NR_remap_file_pages 234
#define TARGET_NR_mbind 235
#define TARGET_NR_get_mempolicy 236
#define TARGET_NR_set_mempolicy 237
#define TARGET_NR_migrate_pages 238
#define TARGET_NR_move_pages 239
#endif

#define TARGET_NR_rt_tgsigqueueinfo 240
#define TARGET_NR_perf_event_open 241
#define TARGET_NR_accept4 242
#define TARGET_NR_recvmmsg 243

/*
 * Architectures may provide up to 16 syscalls of their own
 * starting with this value.
 */
#define TARGET_NR_arch_specific_syscall 244

#define TARGET_NR_wait4 260
#define TARGET_NR_prlimit64 261
#define TARGET_NR_fanotify_init 262
#define TARGET_NR_fanotify_mark 263
#define TARGET_NR_name_to_handle_at         264
#define TARGET_NR_open_by_handle_at         265
#define TARGET_NR_clock_adjtime 266
#define TARGET_NR_syncfs 267
#define TARGET_NR_setns 268
#define TARGET_NR_sendmmsg 269
#define TARGET_NR_process_vm_readv 270
#define TARGET_NR_process_vm_writev 271
#define TARGET_NR_kcmp 272
#define TARGET_NR_finit_module 273
#define TARGET_NR_sched_setattr 274
#define TARGET_NR_sched_getattr 275
#define TARGET_NR_renameat2 276
#define TARGET_NR_seccomp 277
#define TARGET_NR_getrandom 278
#define TARGET_NR_memfd_create 279
#define TARGET_NR_bpf 280
#define TARGET_NR_execveat 281
#define TARGET_NR_userfaultfd 282
#define TARGET_NR_membarrier 283
#define TARGET_NR_mlock2 284
#define TARGET_NR_copy_file_range 285

/*
 * All syscalls below here should go away really,
 * these are provided for both review and as a porting
 * help for the C library version.
*
 * Last chance: are any of these important enough to
 * enable by default?
 */
#define TARGET_NR_open 1024
#define TARGET_NR_link 1025
#define TARGET_NR_unlink 1026
#define TARGET_NR_mknod 1027
#define TARGET_NR_chmod 1028
#define TARGET_NR_chown 1029
#define TARGET_NR_mkdir 1030
#define TARGET_NR_rmdir 1031
#define TARGET_NR_lchown 1032
#define TARGET_NR_access 1033
#define TARGET_NR_rename 1034
#define TARGET_NR_readlink 1035
#define TARGET_NR_symlink 1036
#define TARGET_NR_utimes 1037
#define TARGET_NR_3264_stat 1038
#define TARGET_NR_3264_lstat 1039

#define TARGET_NR_pipe 1040
#define TARGET_NR_dup2 1041
#define TARGET_NR_epoll_create 1042
#define TARGET_NR_inotify_init 1043
#define TARGET_NR_eventfd 1044
#define TARGET_NR_signalfd 1045

#define TARGET_NR_sendfile 1046
#define TARGET_NR_ftruncate 1047
#define TARGET_NR_truncate 1048
#define TARGET_NR_stat 1049
#define TARGET_NR_lstat 1050
#define TARGET_NR_fstat 1051
#define TARGET_NR_fcntl 1052
#define TARGET_NR_fadvise64 1053
#define __ARCH_WANT_SYS_FADVISE64
#define TARGET_NR_newfstatat 1054
#define __ARCH_WANT_SYS_NEWFSTATAT
#define TARGET_NR_fstatfs 1055
#define TARGET_NR_statfs 1056
#define TARGET_NR_lseek 1057
#define TARGET_NR_mmap 1058

#define TARGET_NR_alarm 1059
#define __ARCH_WANT_SYS_ALARM
#define TARGET_NR_getpgrp 1060
#define __ARCH_WANT_SYS_GETPGRP
#define TARGET_NR_pause 1061
#define __ARCH_WANT_SYS_PAUSE
#define TARGET_NR_time 1062
#define __ARCH_WANT_SYS_TIME
#define __ARCH_WANT_COMPAT_SYS_TIME
#define TARGET_NR_utime 1063
#define __ARCH_WANT_SYS_UTIME

#define TARGET_NR_creat 1064
#define TARGET_NR_getdents 1065
#define __ARCH_WANT_SYS_GETDENTS
#define TARGET_NR_futimesat 1066
#define TARGET_NR_poll 1068
#define TARGET_NR_epoll_wait 1069
#define TARGET_NR_ustat 1070
#define TARGET_NR_vfork 1071
#define TARGET_NR_oldwait4 1072
#define TARGET_NR_recv 1073
#define TARGET_NR_send 1074
#define TARGET_NR_bdflush 1075
#define TARGET_NR_umount 1076
#define __ARCH_WANT_SYS_OLDUMOUNT
#define TARGET_NR_uselib 1077
#define TARGET_NR__sysctl 1078

#define TARGET_NR_fork 1079


/*
 * 32 bit systems traditionally used different
 * syscalls for off_t and loff_t arguments, while
 * 64 bit systems only need the off_t version.
 * For new 32 bit platforms, there is no need to
 * implement the old 32 bit off_t syscalls, so
 * they take different names.
 * Here we map the numbers so that both versions
 * use the same syscall table layout.
 */

#define TARGET_NR_fcntl64 TARGET_NR_3264_fcntl
#define TARGET_NR_statfs64 TARGET_NR_3264_statfs
#define TARGET_NR_fstatfs64 TARGET_NR_3264_fstatfs
#define TARGET_NR_truncate64 TARGET_NR_3264_truncate
#define TARGET_NR_ftruncate64 TARGET_NR_3264_ftruncate
#define TARGET_NR_llseek TARGET_NR_3264_lseek
#define TARGET_NR_sendfile64 TARGET_NR_3264_sendfile
#define TARGET_NR_fstatat64 TARGET_NR_3264_fstatat
#define TARGET_NR_fstat64 TARGET_NR_3264_fstat
#define TARGET_NR_mmap2 TARGET_NR_3264_mmap
#define TARGET_NR_fadvise64_64 TARGET_NR_3264_fadvise64

#ifdef TARGET_NR_3264_stat
#define TARGET_NR_stat64 TARGET_NR_3264_stat
#define TARGET_NR_lstat64 TARGET_NR_3264_lstat
#endif

#endif

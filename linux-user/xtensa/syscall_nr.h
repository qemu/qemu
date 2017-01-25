/*
 * include/asm-xtensa/unistd.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2009 Tensilica Inc.
 */

#ifndef _XTENSA_UNISTD_H
#define _XTENSA_UNISTD_H

#define TARGET_NR_spill                                0
#define TARGET_NR_xtensa                               1
#define TARGET_NR_available4                           2
#define TARGET_NR_available5                           3
#define TARGET_NR_available6                           4
#define TARGET_NR_available7                           5
#define TARGET_NR_available8                           6
#define TARGET_NR_available9                           7

/* File Operations */

#define TARGET_NR_open                                 8
#define TARGET_NR_close                                9
#define TARGET_NR_dup                                 10
#define TARGET_NR_dup2                                11
#define TARGET_NR_read                                12
#define TARGET_NR_write                               13
#define TARGET_NR_select                              14
#define TARGET_NR_lseek                               15
#define TARGET_NR_poll                                16
#define TARGET_NR__llseek                             17
#define TARGET_NR_epoll_wait                          18
#define TARGET_NR_epoll_ctl                           19
#define TARGET_NR_epoll_create                        20
#define TARGET_NR_creat                               21
#define TARGET_NR_truncate                            22
#define TARGET_NR_ftruncate                           23
#define TARGET_NR_readv                               24
#define TARGET_NR_writev                              25
#define TARGET_NR_fsync                               26
#define TARGET_NR_fdatasync                           27
#define TARGET_NR_truncate64                          28
#define TARGET_NR_ftruncate64                         29
#define TARGET_NR_pread64                             30
#define TARGET_NR_pwrite64                            31

#define TARGET_NR_link                                32
#define TARGET_NR_rename                              33
#define TARGET_NR_symlink                             34
#define TARGET_NR_readlink                            35
#define TARGET_NR_mknod                               36
#define TARGET_NR_pipe                                37
#define TARGET_NR_unlink                              38
#define TARGET_NR_rmdir                               39

#define TARGET_NR_mkdir                               40
#define TARGET_NR_chdir                               41
#define TARGET_NR_fchdir                              42
#define TARGET_NR_getcwd                              43

#define TARGET_NR_chmod                               44
#define TARGET_NR_chown                               45
#define TARGET_NR_stat                                46
#define TARGET_NR_stat64                              47

#define TARGET_NR_lchown                              48
#define TARGET_NR_lstat                               49
#define TARGET_NR_lstat64                             50
#define TARGET_NR_available51                         51

#define TARGET_NR_fchmod                              52
#define TARGET_NR_fchown                              53
#define TARGET_NR_fstat                               54
#define TARGET_NR_fstat64                             55

#define TARGET_NR_flock                               56
#define TARGET_NR_access                              57
#define TARGET_NR_umask                               58
#define TARGET_NR_getdents                            59
#define TARGET_NR_getdents64                          60
#define TARGET_NR_fcntl64                             61
#define TARGET_NR_fallocate                           62
#define TARGET_NR_fadvise64_64                        63
#define TARGET_NR_utime                               64     /* glibc 2.3.3 ?? */
#define TARGET_NR_utimes                              65
#define TARGET_NR_ioctl                               66
#define TARGET_NR_fcntl                               67

#define TARGET_NR_setxattr                            68
#define TARGET_NR_getxattr                            69
#define TARGET_NR_listxattr                           70
#define TARGET_NR_removexattr                         71
#define TARGET_NR_lsetxattr                           72
#define TARGET_NR_lgetxattr                           73
#define TARGET_NR_llistxattr                          74
#define TARGET_NR_lremovexattr                        75
#define TARGET_NR_fsetxattr                           76
#define TARGET_NR_fgetxattr                           77
#define TARGET_NR_flistxattr                          78
#define TARGET_NR_fremovexattr                        79

/* File Map / Shared Memory Operations */

#define TARGET_NR_mmap2                               80
#define TARGET_NR_munmap                              81
#define TARGET_NR_mprotect                            82
#define TARGET_NR_brk                                 83
#define TARGET_NR_mlock                               84
#define TARGET_NR_munlock                             85
#define TARGET_NR_mlockall                            86
#define TARGET_NR_munlockall                          87
#define TARGET_NR_mremap                              88
#define TARGET_NR_msync                               89
#define TARGET_NR_mincore                             90
#define TARGET_NR_madvise                             91
#define TARGET_NR_shmget                              92
#define TARGET_NR_shmat                               93
#define TARGET_NR_shmctl                              94
#define TARGET_NR_shmdt                               95

/* Socket Operations */

#define TARGET_NR_socket                              96
#define TARGET_NR_setsockopt                          97
#define TARGET_NR_getsockopt                          98
#define TARGET_NR_shutdown                            99

#define TARGET_NR_bind                               100
#define TARGET_NR_connect                            101
#define TARGET_NR_listen                             102
#define TARGET_NR_accept                             103

#define TARGET_NR_getsockname                        104
#define TARGET_NR_getpeername                        105
#define TARGET_NR_sendmsg                            106
#define TARGET_NR_recvmsg                            107
#define TARGET_NR_send                               108
#define TARGET_NR_recv                               109
#define TARGET_NR_sendto                             110
#define TARGET_NR_recvfrom                           111

#define TARGET_NR_socketpair                         112
#define TARGET_NR_sendfile                           113
#define TARGET_NR_sendfile64                         114
#define TARGET_NR_sendmmsg                           115

/* Process Operations */

#define TARGET_NR_clone                              116
#define TARGET_NR_execve                             117
#define TARGET_NR_exit                               118
#define TARGET_NR_exit_group                         119
#define TARGET_NR_getpid                             120
#define TARGET_NR_wait4                              121
#define TARGET_NR_waitid                             122
#define TARGET_NR_kill                               123
#define TARGET_NR_tkill                              124
#define TARGET_NR_tgkill                             125
#define TARGET_NR_set_tid_address                    126
#define TARGET_NR_gettid                             127
#define TARGET_NR_setsid                             128
#define TARGET_NR_getsid                             129
#define TARGET_NR_prctl                              130
#define TARGET_NR_personality                        131
#define TARGET_NR_getpriority                        132
#define TARGET_NR_setpriority                        133
#define TARGET_NR_setitimer                          134
#define TARGET_NR_getitimer                          135
#define TARGET_NR_setuid                             136
#define TARGET_NR_getuid                             137
#define TARGET_NR_setgid                             138
#define TARGET_NR_getgid                             139
#define TARGET_NR_geteuid                            140
#define TARGET_NR_getegid                            141
#define TARGET_NR_setreuid                           142
#define TARGET_NR_setregid                           143
#define TARGET_NR_setresuid                          144
#define TARGET_NR_getresuid                          145
#define TARGET_NR_setresgid                          146
#define TARGET_NR_getresgid                          147
#define TARGET_NR_setpgid                            148
#define TARGET_NR_getpgid                            149
#define TARGET_NR_getppid                            150
#define TARGET_NR_getpgrp                            151

#define TARGET_NR_reserved152                        152     /* set_thread_area */
#define TARGET_NR_reserved153                        153     /* get_thread_area */
#define TARGET_NR_times                              154
#define TARGET_NR_acct                               155
#define TARGET_NR_sched_setaffinity                  156
#define TARGET_NR_sched_getaffinity                  157
#define TARGET_NR_capget                             158
#define TARGET_NR_capset                             159
#define TARGET_NR_ptrace                             160
#define TARGET_NR_semtimedop                         161
#define TARGET_NR_semget                             162
#define TARGET_NR_semop                              163
#define TARGET_NR_semctl                             164
#define TARGET_NR_available165                       165
#define TARGET_NR_msgget                             166
#define TARGET_NR_msgsnd                             167
#define TARGET_NR_msgrcv                             168
#define TARGET_NR_msgctl                             169
#define TARGET_NR_available170                       170

/* File System */

#define TARGET_NR_umount2                            171
#define TARGET_NR_mount                              172
#define TARGET_NR_swapon                             173
#define TARGET_NR_chroot                             174
#define TARGET_NR_pivot_root                         175
#define TARGET_NR_umount                             176
#define TARGET_NR_swapoff                            177
#define TARGET_NR_sync                               178
#define TARGET_NR_syncfs                             179
#define TARGET_NR_setfsuid                           180
#define TARGET_NR_setfsgid                           181
#define TARGET_NR_sysfs                              182
#define TARGET_NR_ustat                              183
#define TARGET_NR_statfs                             184
#define TARGET_NR_fstatfs                            185
#define TARGET_NR_statfs64                           186
#define TARGET_NR_fstatfs64                          187

/* System */

#define TARGET_NR_setrlimit                          188
#define TARGET_NR_getrlimit                          189
#define TARGET_NR_getrusage                          190
#define TARGET_NR_futex                              191
#define TARGET_NR_gettimeofday                       192
#define TARGET_NR_settimeofday                       193
#define TARGET_NR_adjtimex                           194
#define TARGET_NR_nanosleep                          195
#define TARGET_NR_getgroups                          196
#define TARGET_NR_setgroups                          197
#define TARGET_NR_sethostname                        198
#define TARGET_NR_setdomainname                      199
#define TARGET_NR_syslog                             200
#define TARGET_NR_vhangup                            201
#define TARGET_NR_uselib                             202
#define TARGET_NR_reboot                             203
#define TARGET_NR_quotactl                           204
#define TARGET_NR_nfsservctl                         205
#define TARGET_NR__sysctl                            206
#define TARGET_NR_bdflush                            207
#define TARGET_NR_uname                              208
#define TARGET_NR_sysinfo                            209
#define TARGET_NR_init_module                        210
#define TARGET_NR_delete_module                      211

#define TARGET_NR_sched_setparam                     212
#define TARGET_NR_sched_getparam                     213
#define TARGET_NR_sched_setscheduler                 214
#define TARGET_NR_sched_getscheduler                 215
#define TARGET_NR_sched_get_priority_max             216
#define TARGET_NR_sched_get_priority_min             217
#define TARGET_NR_sched_rr_get_interval              218
#define TARGET_NR_sched_yield                        219
#define TARGET_NR_available222                       222

/* Signal Handling */

#define TARGET_NR_restart_syscall                    223
#define TARGET_NR_sigaltstack                        224
#define TARGET_NR_rt_sigreturn                       225
#define TARGET_NR_rt_sigaction                       226
#define TARGET_NR_rt_sigprocmask                     227
#define TARGET_NR_rt_sigpending                      228
#define TARGET_NR_rt_sigtimedwait                    229
#define TARGET_NR_rt_sigqueueinfo                    230
#define TARGET_NR_rt_sigsuspend                      231

/* Message */

#define TARGET_NR_mq_open                            232
#define TARGET_NR_mq_unlink                          233
#define TARGET_NR_mq_timedsend                       234
#define TARGET_NR_mq_timedreceive                    235
#define TARGET_NR_mq_notify                          236
#define TARGET_NR_mq_getsetattr                      237
#define TARGET_NR_available238                       238

/* IO */

#define TARGET_NR_io_setup                           239
#define TARGET_NR_io_destroy                         240
#define TARGET_NR_io_submit                          241
#define TARGET_NR_io_getevents                       242
#define TARGET_NR_io_cancel                          243
#define TARGET_NR_clock_settime                      244
#define TARGET_NR_clock_gettime                      245
#define TARGET_NR_clock_getres                       246
#define TARGET_NR_clock_nanosleep                    247

/* Timer */

#define TARGET_NR_timer_create                       248
#define TARGET_NR_timer_delete                       249
#define TARGET_NR_timer_settime                      250
#define TARGET_NR_timer_gettime                      251
#define TARGET_NR_timer_getoverrun                   252

/* System */

#define TARGET_NR_reserved253                        253
#define TARGET_NR_lookup_dcookie                     254
#define TARGET_NR_available255                       255
#define TARGET_NR_add_key                            256
#define TARGET_NR_request_key                        257
#define TARGET_NR_keyctl                             258
#define TARGET_NR_available259                       259


#define TARGET_NR_readahead                          260
#define TARGET_NR_remap_file_pages                   261
#define TARGET_NR_migrate_pages                      262
#define TARGET_NR_mbind                              263
#define TARGET_NR_get_mempolicy                      264
#define TARGET_NR_set_mempolicy                      265
#define TARGET_NR_unshare                            266
#define TARGET_NR_move_pages                         267
#define TARGET_NR_splice                             268
#define TARGET_NR_tee                                269
#define TARGET_NR_vmsplice                           270
#define TARGET_NR_available271                       271

#define TARGET_NR_pselect6                           272
#define TARGET_NR_ppoll                              273
#define TARGET_NR_epoll_pwait                        274
#define TARGET_NR_epoll_create1                      275

#define TARGET_NR_inotify_init                       276
#define TARGET_NR_inotify_add_watch                  277
#define TARGET_NR_inotify_rm_watch                   278
#define TARGET_NR_inotify_init1                      279

#define TARGET_NR_getcpu                             280
#define TARGET_NR_kexec_load                         281

#define TARGET_NR_ioprio_set                         282
#define TARGET_NR_ioprio_get                         283

#define TARGET_NR_set_robust_list                    284
#define TARGET_NR_get_robust_list                    285
#define TARGET_NR_available286                       286
#define TARGET_NR_available287                       287

/* Relative File Operations */

#define TARGET_NR_openat                             288
#define TARGET_NR_mkdirat                            289
#define TARGET_NR_mknodat                            290
#define TARGET_NR_unlinkat                           291
#define TARGET_NR_renameat                           292
#define TARGET_NR_linkat                             293
#define TARGET_NR_symlinkat                          294
#define TARGET_NR_readlinkat                         295
#define TARGET_NR_utimensat                          296
#define TARGET_NR_fchownat                           297
#define TARGET_NR_futimesat                          298
#define TARGET_NR_fstatat64                          299
#define TARGET_NR_fchmodat                           300
#define TARGET_NR_faccessat                          301
#define TARGET_NR_available302                       302
#define TARGET_NR_available303                       303

#define TARGET_NR_signalfd                           304
/*  305 was TARGET_NR_timerfd  */
#define TARGET_NR_eventfd                            306
#define TARGET_NR_recvmmsg                           307

#define TARGET_NR_setns                              308
#define TARGET_NR_signalfd4                          309
#define TARGET_NR_dup3                               310
#define TARGET_NR_pipe2                              311

#define TARGET_NR_timerfd_create                     312
#define TARGET_NR_timerfd_settime                    313
#define TARGET_NR_timerfd_gettime                    314
#define TARGET_NR_available315                       315

#define TARGET_NR_eventfd2                           316
#define TARGET_NR_preadv                             317
#define TARGET_NR_pwritev                            318
#define TARGET_NR_available319                       319

#define TARGET_NR_fanotify_init                      320
#define TARGET_NR_fanotify_mark                      321
#define TARGET_NR_process_vm_readv                   322
#define TARGET_NR_process_vm_writev                  323

#define TARGET_NR_name_to_handle_at                  324
#define TARGET_NR_open_by_handle_at                  325
#define TARGET_NR_sync_file_range2                   326
#define TARGET_NR_perf_event_open                    327

#define TARGET_NR_rt_tgsigqueueinfo                  328
#define TARGET_NR_clock_adjtime                      329
#define TARGET_NR_prlimit64                          330
#define TARGET_NR_kcmp                               331

#define TARGET_NR_finit_module                       332

#define TARGET_NR_accept4                            333

#define TARGET_NR_sched_setattr                      334
#define TARGET_NR_sched_getattr                      335

#define TARGET_NR_renameat2                          336

#define TARGET_NR_seccomp                            337
#define TARGET_NR_getrandom                          338
#define TARGET_NR_memfd_create                       339
#define TARGET_NR_bpf                                340
#define TARGET_NR_execveat                           341

#define TARGET_NR_userfaultfd                        342
#define TARGET_NR_membarrier                         343
#define TARGET_NR_mlock2                             344
#define TARGET_NR_copy_file_range                    345
#define TARGET_NR_preadv2                            346
#define TARGET_NR_pwritev2                           347

#define TARGET_NR_pkey_mprotect                      348
#define TARGET_NR_pkey_alloc                         349
#define TARGET_NR_pkey_free                          350

#define TARGET_NR_statx                              351

#define TARGET_NR_syscall_count                      352

#endif  /* _XTENSA_UNISTD_H */

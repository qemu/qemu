/*      $OpenBSD: syscall.h,v 1.101 2008/03/16 19:43:41 otto Exp $      */

/*
 * System call numbers.
 *
 * created from;        OpenBSD: syscalls.master,v 1.90 2008/03/16 19:42:57 otto Exp
 */

#define TARGET_OPENBSD_NR_syscall     0
#define TARGET_OPENBSD_NR_exit        1
#define TARGET_OPENBSD_NR_fork        2
#define TARGET_OPENBSD_NR_read        3
#define TARGET_OPENBSD_NR_write       4
#define TARGET_OPENBSD_NR_open        5
#define TARGET_OPENBSD_NR_close       6
#define TARGET_OPENBSD_NR_wait4       7
#define TARGET_OPENBSD_NR_link        9
#define TARGET_OPENBSD_NR_unlink      10
#define TARGET_OPENBSD_NR_chdir       12
#define TARGET_OPENBSD_NR_fchdir      13
#define TARGET_OPENBSD_NR_mknod       14
#define TARGET_OPENBSD_NR_chmod       15
#define TARGET_OPENBSD_NR_chown       16
#define TARGET_OPENBSD_NR_break       17
#define TARGET_OPENBSD_NR_getpid      20
#define TARGET_OPENBSD_NR_mount       21
#define TARGET_OPENBSD_NR_unmount     22
#define TARGET_OPENBSD_NR_setuid      23
#define TARGET_OPENBSD_NR_getuid      24
#define TARGET_OPENBSD_NR_geteuid     25
#define TARGET_OPENBSD_NR_ptrace      26
#define TARGET_OPENBSD_NR_recvmsg     27
#define TARGET_OPENBSD_NR_sendmsg     28
#define TARGET_OPENBSD_NR_recvfrom    29
#define TARGET_OPENBSD_NR_accept      30
#define TARGET_OPENBSD_NR_getpeername 31
#define TARGET_OPENBSD_NR_getsockname 32
#define TARGET_OPENBSD_NR_access      33
#define TARGET_OPENBSD_NR_chflags     34
#define TARGET_OPENBSD_NR_fchflags    35
#define TARGET_OPENBSD_NR_sync        36
#define TARGET_OPENBSD_NR_kill        37
#define TARGET_OPENBSD_NR_getppid     39
#define TARGET_OPENBSD_NR_dup 41
#define TARGET_OPENBSD_NR_opipe       42
#define TARGET_OPENBSD_NR_getegid     43
#define TARGET_OPENBSD_NR_profil      44
#define TARGET_OPENBSD_NR_ktrace      45
#define TARGET_OPENBSD_NR_sigaction   46
#define TARGET_OPENBSD_NR_getgid      47
#define TARGET_OPENBSD_NR_sigprocmask 48
#define TARGET_OPENBSD_NR_getlogin    49
#define TARGET_OPENBSD_NR_setlogin    50
#define TARGET_OPENBSD_NR_acct        51
#define TARGET_OPENBSD_NR_sigpending  52
#define TARGET_OPENBSD_NR_osigaltstack        53
#define TARGET_OPENBSD_NR_ioctl       54
#define TARGET_OPENBSD_NR_reboot      55
#define TARGET_OPENBSD_NR_revoke      56
#define TARGET_OPENBSD_NR_symlink     57
#define TARGET_OPENBSD_NR_readlink    58
#define TARGET_OPENBSD_NR_execve      59
#define TARGET_OPENBSD_NR_umask       60
#define TARGET_OPENBSD_NR_chroot      61
#define TARGET_OPENBSD_NR_vfork       66
#define TARGET_OPENBSD_NR_sbrk        69
#define TARGET_OPENBSD_NR_sstk        70
#define TARGET_OPENBSD_NR_munmap      73
#define TARGET_OPENBSD_NR_mprotect    74
#define TARGET_OPENBSD_NR_madvise     75
#define TARGET_OPENBSD_NR_mincore     78
#define TARGET_OPENBSD_NR_getgroups   79
#define TARGET_OPENBSD_NR_setgroups   80
#define TARGET_OPENBSD_NR_getpgrp     81
#define TARGET_OPENBSD_NR_setpgid     82
#define TARGET_OPENBSD_NR_setitimer   83
#define TARGET_OPENBSD_NR_getitimer   86
#define TARGET_OPENBSD_NR_dup2        90
#define TARGET_OPENBSD_NR_fcntl       92
#define TARGET_OPENBSD_NR_select      93
#define TARGET_OPENBSD_NR_fsync       95
#define TARGET_OPENBSD_NR_setpriority 96
#define TARGET_OPENBSD_NR_socket      97
#define TARGET_OPENBSD_NR_connect     98
#define TARGET_OPENBSD_NR_getpriority 100
#define TARGET_OPENBSD_NR_sigreturn   103
#define TARGET_OPENBSD_NR_bind        104
#define TARGET_OPENBSD_NR_setsockopt  105
#define TARGET_OPENBSD_NR_listen      106
#define TARGET_OPENBSD_NR_sigsuspend  111
#define TARGET_OPENBSD_NR_gettimeofday        116
#define TARGET_OPENBSD_NR_getrusage   117
#define TARGET_OPENBSD_NR_getsockopt  118
#define TARGET_OPENBSD_NR_readv       120
#define TARGET_OPENBSD_NR_writev      121
#define TARGET_OPENBSD_NR_settimeofday        122
#define TARGET_OPENBSD_NR_fchown      123
#define TARGET_OPENBSD_NR_fchmod      124
#define TARGET_OPENBSD_NR_setreuid    126
#define TARGET_OPENBSD_NR_setregid    127
#define TARGET_OPENBSD_NR_rename      128
#define TARGET_OPENBSD_NR_flock       131
#define TARGET_OPENBSD_NR_mkfifo      132
#define TARGET_OPENBSD_NR_sendto      133
#define TARGET_OPENBSD_NR_shutdown    134
#define TARGET_OPENBSD_NR_socketpair  135
#define TARGET_OPENBSD_NR_mkdir       136
#define TARGET_OPENBSD_NR_rmdir       137
#define TARGET_OPENBSD_NR_utimes      138
#define TARGET_OPENBSD_NR_adjtime     140
#define TARGET_OPENBSD_NR_setsid      147
#define TARGET_OPENBSD_NR_quotactl    148
#define TARGET_OPENBSD_NR_nfssvc      155
#define TARGET_OPENBSD_NR_getfh       161
#define TARGET_OPENBSD_NR_sysarch     165
#define TARGET_OPENBSD_NR_pread       173
#define TARGET_OPENBSD_NR_pwrite      174
#define TARGET_OPENBSD_NR_setgid      181
#define TARGET_OPENBSD_NR_setegid     182
#define TARGET_OPENBSD_NR_seteuid     183
#define TARGET_OPENBSD_NR_lfs_bmapv   184
#define TARGET_OPENBSD_NR_lfs_markv   185
#define TARGET_OPENBSD_NR_lfs_segclean        186
#define TARGET_OPENBSD_NR_lfs_segwait 187
#define TARGET_OPENBSD_NR_pathconf    191
#define TARGET_OPENBSD_NR_fpathconf   192
#define TARGET_OPENBSD_NR_swapctl     193
#define TARGET_OPENBSD_NR_getrlimit   194
#define TARGET_OPENBSD_NR_setrlimit   195
#define TARGET_OPENBSD_NR_getdirentries       196
#define TARGET_OPENBSD_NR_mmap        197
#define TARGET_OPENBSD_NR___syscall   198
#define TARGET_OPENBSD_NR_lseek       199
#define TARGET_OPENBSD_NR_truncate    200
#define TARGET_OPENBSD_NR_ftruncate   201
#define TARGET_OPENBSD_NR___sysctl    202
#define TARGET_OPENBSD_NR_mlock       203
#define TARGET_OPENBSD_NR_munlock     204
#define TARGET_OPENBSD_NR_futimes     206
#define TARGET_OPENBSD_NR_getpgid     207
#define TARGET_OPENBSD_NR_xfspioctl   208
#define TARGET_OPENBSD_NR_semget      221
#define TARGET_OPENBSD_NR_msgget      225
#define TARGET_OPENBSD_NR_msgsnd      226
#define TARGET_OPENBSD_NR_msgrcv      227
#define TARGET_OPENBSD_NR_shmat       228
#define TARGET_OPENBSD_NR_shmdt       230
#define TARGET_OPENBSD_NR_clock_gettime       232
#define TARGET_OPENBSD_NR_clock_settime       233
#define TARGET_OPENBSD_NR_clock_getres        234
#define TARGET_OPENBSD_NR_nanosleep   240
#define TARGET_OPENBSD_NR_minherit    250
#define TARGET_OPENBSD_NR_rfork       251
#define TARGET_OPENBSD_NR_poll        252
#define TARGET_OPENBSD_NR_issetugid   253
#define TARGET_OPENBSD_NR_lchown      254
#define TARGET_OPENBSD_NR_getsid      255
#define TARGET_OPENBSD_NR_msync       256
#define TARGET_OPENBSD_NR_pipe        263
#define TARGET_OPENBSD_NR_fhopen      264
#define TARGET_OPENBSD_NR_preadv      267
#define TARGET_OPENBSD_NR_pwritev     268
#define TARGET_OPENBSD_NR_kqueue      269
#define TARGET_OPENBSD_NR_kevent      270
#define TARGET_OPENBSD_NR_mlockall    271
#define TARGET_OPENBSD_NR_munlockall  272
#define TARGET_OPENBSD_NR_getpeereid  273
#define TARGET_OPENBSD_NR_getresuid   281
#define TARGET_OPENBSD_NR_setresuid   282
#define TARGET_OPENBSD_NR_getresgid   283
#define TARGET_OPENBSD_NR_setresgid   284
#define TARGET_OPENBSD_NR_mquery      286
#define TARGET_OPENBSD_NR_closefrom   287
#define TARGET_OPENBSD_NR_sigaltstack 288
#define TARGET_OPENBSD_NR_shmget      289
#define TARGET_OPENBSD_NR_semop       290
#define TARGET_OPENBSD_NR_stat        291
#define TARGET_OPENBSD_NR_fstat       292
#define TARGET_OPENBSD_NR_lstat       293
#define TARGET_OPENBSD_NR_fhstat      294
#define TARGET_OPENBSD_NR___semctl    295
#define TARGET_OPENBSD_NR_shmctl      296
#define TARGET_OPENBSD_NR_msgctl      297
#define TARGET_OPENBSD_NR_sched_yield 298
#define TARGET_OPENBSD_NR_getthrid    299
#define TARGET_OPENBSD_NR_thrsleep    300
#define TARGET_OPENBSD_NR_thrwakeup   301
#define TARGET_OPENBSD_NR_threxit     302
#define TARGET_OPENBSD_NR_thrsigdivert        303
#define TARGET_OPENBSD_NR___getcwd    304
#define TARGET_OPENBSD_NR_adjfreq     305
#define TARGET_OPENBSD_NR_getfsstat   306
#define TARGET_OPENBSD_NR_statfs      307
#define TARGET_OPENBSD_NR_fstatfs     308
#define TARGET_OPENBSD_NR_fhstatfs    309

/* syscall flags from machine/trap.h */

/*      $OpenBSD: trap.h,v 1.4 2008/07/04 22:04:37 kettenis Exp $       */
/*      $NetBSD: trap.h,v 1.4 1999/06/07 05:28:04 eeh Exp $ */

/*
 * Copyright (c) 1996-1999 Eduardo Horvath
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR  ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR  BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
#define TARGET_OPENBSD_SYSCALL_G2RFLAG 0x400   /* on success, return to %g2 rather than npc */
#define TARGET_OPENBSD_SYSCALL_G7RFLAG 0x800   /* use %g7 as above (deprecated) */

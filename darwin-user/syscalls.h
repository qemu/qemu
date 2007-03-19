/* generated from xnu/bsd/kern/syscalls.master */

 ENTRY("syscall",                  SYS_syscall,                        do_unix_syscall_indirect,          0, CALL_INDIRECT, VOID) /* 0  indirect syscall */
 ENTRY("exit",                     SYS_exit,                           do_exit,                           1, CALL_DIRECT, INT)   /* 1  */
 ENTRY("fork",                     SYS_fork,                           fork,                              0, CALL_NOERRNO, VOID)  /* 2  */
 ENTRY("read",                     SYS_read,                           do_read,                           3, CALL_DIRECT, INT, PTR, SIZE)   /* 3  */
 ENTRY("write",                    SYS_write,                          write,                             3, CALL_DIRECT, INT, PTR, SIZE)   /* 4  */
 ENTRY("open",                     SYS_open,                           do_open,                           3, CALL_DIRECT, PTR, INT, INT)   /* 5  */
 ENTRY("close",                    SYS_close,                          close,                             1, CALL_DIRECT, INT)   /* 6  */
 ENTRY("wait4",                    SYS_wait4,                          wait4,                             4, CALL_DIRECT, INT, PTR, INT, PTR)   /* 7  */
 ENTRY("",                         8,                                  no_syscall,                        0, CALL_INDIRECT, VOID) /* 8  old creat */
 ENTRY("link",                     SYS_link,                           link,                              2, CALL_DIRECT, PTR, PTR)   /* 9  */
 ENTRY("unlink",                   SYS_unlink,                         unlink,                            1, CALL_DIRECT, PTR)   /* 10  */
 ENTRY("",                         11,                                 no_syscall,                        0, CALL_INDIRECT, VOID) /* 11  old execv */
 ENTRY("chdir",                    SYS_chdir,                          chdir,                             1, CALL_DIRECT, PTR)   /* 12  */
 ENTRY("fchdir",                   SYS_fchdir,                         fchdir,                            1, CALL_DIRECT, INT)   /* 13  */
 ENTRY("mknod",                    SYS_mknod,                          mknod,                             3, CALL_DIRECT, PTR, INT, INT)   /* 14  */
 ENTRY("chmod",                    SYS_chmod,                          chmod,                             2, CALL_DIRECT, PTR, INT)   /* 15  */
 ENTRY("chown",                    SYS_chown,                          chown,                             3, CALL_DIRECT, PTR, INT, INT)   /* 16  */
 ENTRY("obreak",                   SYS_obreak,                         no_syscall,                        1, CALL_INDIRECT, VOID)   /* 17  old break */
 ENTRY("ogetfsstat",               18,                                 unimpl_unix_syscall,               3, CALL_INDIRECT, PTR, INT, INT)   /* 18  */
 ENTRY("",                         19,                                 no_syscall,                        0, CALL_INDIRECT, VOID) /* 19  old lseek */
 ENTRY("getpid",                   SYS_getpid,                         getpid,                            0, CALL_NOERRNO, VOID)   /* 20  */
 ENTRY("",                         21,                                 no_syscall,                        0, CALL_INDIRECT, VOID) /* 21  old mount */
 ENTRY("",                         22,                                 no_syscall,                        0, CALL_INDIRECT, VOID) /* 22  old umount */
 ENTRY("setuid",                   SYS_setuid,                         setuid,                            1, CALL_DIRECT, INT)   /* 23  */
 ENTRY("getuid",                   SYS_getuid,                         getuid,                            0, CALL_NOERRNO, VOID)   /* 24  */
 ENTRY("geteuid",                  SYS_geteuid,                        geteuid,                           0, CALL_NOERRNO, VOID)   /* 25  */
 ENTRY("ptrace",                   SYS_ptrace,                         ptrace,                            4, CALL_DIRECT, INT, INT, PTR, INT)   /* 26  */
 ENTRY("recvmsg",                  SYS_recvmsg,                        recvmsg,                           3, CALL_DIRECT, INT, PTR, INT)   /* 27  */
 ENTRY("sendmsg",                  SYS_sendmsg,                        sendmsg,                           3, CALL_DIRECT, INT, PTR, INT)   /* 28  */
 ENTRY("recvfrom",                 SYS_recvfrom,                       recvfrom,                          6, CALL_DIRECT, INT, PTR, INT, INT, PTR, PTR)   /* 29  */
 ENTRY("accept",                   SYS_accept,                         accept,                            3, CALL_DIRECT, INT, PTR, PTR)   /* 30  */
 ENTRY("getpeername",              SYS_getpeername,                    getpeername,                       3, CALL_DIRECT, INT, PTR, PTR)   /* 31  */
 ENTRY("getsockname",              SYS_getsockname,                    getsockname,                       3, CALL_DIRECT, INT, PTR, PTR)   /* 32  */
 ENTRY("access",                   SYS_access,                         access,                            2, CALL_DIRECT, PTR, INT)   /* 33  */
 ENTRY("chflags",                  SYS_chflags,                        chflags,                           2, CALL_DIRECT, PTR, INT)   /* 34  */
 ENTRY("fchflags",                 SYS_fchflags,                       fchflags,                          2, CALL_DIRECT, INT, INT)   /* 35  */
 ENTRY("sync",                     SYS_sync,                           do_sync,                           0, CALL_INDIRECT, VOID)   /* 36  */
 ENTRY("kill",                     SYS_kill,                           kill,                              2, CALL_DIRECT, INT, INT)   /* 37  */
 ENTRY("",                         38,                                 no_syscall,                        0, CALL_INDIRECT, VOID) /* 38  old stat */
 ENTRY("getppid",                  SYS_getppid,                        getppid,                           0, CALL_DIRECT, VOID)   /* 39  */
 ENTRY("",                         40,                                 no_syscall,                        0, CALL_INDIRECT, VOID) /* 40  old lstat */
 ENTRY("dup",                      SYS_dup,                            dup,                               1, CALL_DIRECT, INT)   /* 41  */
 ENTRY("pipe",                     SYS_pipe,                           pipe,               0, CALL_INDIRECT, PTR)   /* 42  */
 ENTRY("getegid",                  SYS_getegid,                        getegid,                           0, CALL_NOERRNO, VOID)  /* 43  */
 ENTRY("profil",                   SYS_profil,                         profil,                            4, CALL_DIRECT, PTR, SIZE, INT, INT)   /* 44  */
 ENTRY("ktrace",                   SYS_ktrace,                         no_syscall,                        4, CALL_INDIRECT, VOID) /* 45  */
 ENTRY("sigaction",                SYS_sigaction,                      do_sigaction,                      3, CALL_DIRECT, INT, PTR, PTR)   /* 46  */
 ENTRY("getgid",                   SYS_getgid,                         getgid,                            0, CALL_NOERRNO, VOID)  /* 47  */
 ENTRY("sigprocmask",              SYS_sigprocmask,                    do_sigprocmask,                    3, CALL_DIRECT, INT, PTR, PTR)   /* 48  */
 ENTRY("getlogin",                 SYS_getlogin,                       do_getlogin,                       2, CALL_DIRECT, PTR, UINT)   /* 49 XXX */
 ENTRY("setlogin",                 SYS_setlogin,                       setlogin,                          1, CALL_DIRECT, PTR)   /* 50  */
 ENTRY("acct",                     SYS_acct,                           acct,                              1, CALL_DIRECT, PTR)   /* 51  */
 ENTRY("sigpending",               SYS_sigpending,                     sigpending,                        1, CALL_DIRECT, PTR)   /* 52  */
 ENTRY("sigaltstack",              SYS_sigaltstack,                    do_sigaltstack,                    2, CALL_DIRECT, PTR, PTR)   /* 53  */
 ENTRY("ioctl",                    SYS_ioctl,                          do_ioctl,                          3, CALL_DIRECT, INT, INT, INT)   /* 54  */
 ENTRY("reboot",                   SYS_reboot,                         unimpl_unix_syscall,               2, CALL_INDIRECT, INT, PTR)   /* 55  */
 ENTRY("revoke",                   SYS_revoke,                         revoke,                            1, CALL_DIRECT, PTR)   /* 56  */
 ENTRY("symlink",                  SYS_symlink,                        symlink,                           2, CALL_DIRECT, PTR, PTR)   /* 57  */
 ENTRY("readlink",                 SYS_readlink,                       readlink,                          3, CALL_DIRECT, PTR, PTR, INT)   /* 58  */
 ENTRY("execve",                   SYS_execve,                         do_execve,                         3, CALL_DIRECT, PTR, PTR, PTR)   /* 59  */
 ENTRY("umask",                    SYS_umask,                          umask,                             1, CALL_DIRECT, INT)   /* 60  */
 ENTRY("chroot",                   SYS_chroot,                         chroot,                            1, CALL_DIRECT, PTR)   /* 61  */
 ENTRY("",                         62,                                 no_syscall,                        0, CALL_INDIRECT, VOID) /* 62  old fstat */
 ENTRY("",                         63,                                 no_syscall,                        0, CALL_INDIRECT, VOID) /* 63  used internally , reserved */
 ENTRY("",                         64,                                 no_syscall,                        0, CALL_INDIRECT, VOID) /* 64  old getpagesize */
 ENTRY("msync",                    SYS_msync,                          target_msync,                      3, CALL_DIRECT, UINT /*PTR*/, SIZE, INT)   /* 65  */
 ENTRY("vfork",                    SYS_vfork,                          vfork,                             0, CALL_DIRECT, VOID)   /* 66  */
 ENTRY("",                         67,                                 no_syscall,                        0, CALL_INDIRECT, VOID) /* 67  old vread */
 ENTRY("",                         68,                                 no_syscall,                        0, CALL_INDIRECT, VOID) /* 68  old vwrite */
 ENTRY("sbrk",                     SYS_sbrk,                           sbrk,                              1, CALL_DIRECT, INT)   /* 69  */
 ENTRY("sstk",                     SYS_sstk,                           no_syscall,                        1, CALL_INDIRECT, VOID) /* 70  */
 ENTRY("",                         71,                                 no_syscall,                        0, CALL_INDIRECT, VOID) /* 71  old mmap */
 ENTRY("ovadvise",                 SYS_ovadvise,                       no_syscall,                        0, CALL_INDIRECT, VOID) /* 72  old vadvise */
 ENTRY("munmap",                   SYS_munmap,                         target_munmap,                     2, CALL_DIRECT, UINT /* PTR */, SIZE)   /* 73  */
 ENTRY("mprotect",                 SYS_mprotect,                       mprotect,                          3, CALL_DIRECT, PTR, SIZE, INT)   /* 74  */
 ENTRY("madvise",                  SYS_madvise,                        madvise,                           3, CALL_DIRECT, PTR, SIZE, INT)   /* 75  */
 ENTRY("",                         76,                                 no_syscall,                        0, CALL_INDIRECT, VOID) /* 76  old vhangup */
 ENTRY("",                         77,                                 no_syscall,                        0, CALL_INDIRECT, VOID) /* 77  old vlimit */
 ENTRY("mincore",                  SYS_mincore,                        mincore,                           3, CALL_DIRECT, PTR, SIZE, PTR)   /* 78  */
 ENTRY("getgroups",                SYS_getgroups,                      do_getgroups,                      2, CALL_DIRECT, UINT, PTR)   /* 79  */
 ENTRY("setgroups",                SYS_setgroups,                      setgroups,                         2, CALL_DIRECT, UINT, PTR)   /* 80  */
 ENTRY("getpgrp",                  SYS_getpgrp,                        getpgrp,                           0, CALL_DIRECT, VOID)   /* 81  */
 ENTRY("setpgid",                  SYS_setpgid,                        setpgid,                           2, CALL_DIRECT, INT, INT)   /* 82  */
 ENTRY("setitimer",                SYS_setitimer,                      setitimer,                         3, CALL_DIRECT, INT, PTR, PTR)   /* 83  */
 ENTRY("",                         84,                                 no_syscall,                        0, CALL_INDIRECT, VOID) /* 84  old wait */
 ENTRY("swapon",                   SYS_swapon,                         unimpl_unix_syscall,               0, CALL_INDIRECT, VOID)   /* 85  */
 ENTRY("getitimer",                SYS_getitimer,                      getitimer,                         2, CALL_DIRECT, INT, PTR)   /* 86  */
 ENTRY("",                         87,                                 no_syscall,                        0, CALL_INDIRECT, VOID) /* 87  old gethostname */
 ENTRY("",                         88,                                 no_syscall,                        0, CALL_INDIRECT, VOID) /* 88  old sethostname */
 ENTRY("getdtablesize",            SYS_getdtablesize,                  getdtablesize,                     0, CALL_DIRECT, VOID)   /* 89  */
 ENTRY("dup2",                     SYS_dup2,                           dup2,                              2, CALL_DIRECT, INT, INT)   /* 90  */
 ENTRY("",                         91,                                 no_syscall,                        0, CALL_INDIRECT, VOID) /* 91  old getdopt */
 ENTRY("fcntl",                    SYS_fcntl,                          do_fcntl,                          3, CALL_DIRECT, INT, INT, INT)   /* 92  */
 ENTRY("select",                   SYS_select,                         select,                            5, CALL_DIRECT, INT, PTR, PTR, PTR, PTR)   /* 93  */
 ENTRY("",                         94,                                 no_syscall,                        0, CALL_INDIRECT, VOID) /* 94  old setdopt */
 ENTRY("fsync",                    SYS_fsync,                          fsync,                             1, CALL_DIRECT, INT)   /* 95  */
 ENTRY("setpriority",              SYS_setpriority,                    setpriority,                       3, CALL_DIRECT, INT, INT, INT)   /* 96  */
 ENTRY("socket",                   SYS_socket,                         socket,                            3, CALL_DIRECT, INT, INT, INT)   /* 97  */
 ENTRY("connect",                  SYS_connect,                        connect,                           3, CALL_DIRECT, INT, PTR, INT)   /* 98  */
 ENTRY("",                         99,                                 no_syscall,                        0, CALL_INDIRECT, VOID) /* 99  old accept */
 ENTRY("getpriority",              SYS_getpriority,                    getpriority,                       2, CALL_DIRECT, INT, INT)   /* 100  */
 ENTRY("",                         101,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 101  old send */
 ENTRY("",                         102,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 102  old recv */
 ENTRY("",                         103,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 103  old sigreturn */
 ENTRY("bind",                     SYS_bind,                           bind,                              3, CALL_DIRECT, INT, PTR, INT)   /* 104  */
 ENTRY("setsockopt",               SYS_setsockopt,                     setsockopt,                        5, CALL_DIRECT, INT, INT, INT, PTR, INT)   /* 105  */
 ENTRY("listen",                   SYS_listen,                         listen,                            2, CALL_DIRECT, INT, INT)   /* 106  */
 ENTRY("",                         107,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 107  old vtimes */
 ENTRY("",                         108,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 108  old sigvec */
 ENTRY("",                         109,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 109  old sigblock */
 ENTRY("",                         110,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 110  old sigsetmask */
 ENTRY("sigsuspend",               SYS_sigsuspend,                     unimpl_unix_syscall,               1, CALL_INDIRECT, INT)   /* 111  */
 ENTRY("",                         112,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 112  old sigstack */
 ENTRY("",                         113,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 113  old recvmsg */
 ENTRY("",                         114,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 114  old sendmsg */
 ENTRY("",                         115,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 115  old vtrace */
 ENTRY("gettimeofday",             SYS_gettimeofday,                   do_gettimeofday,                   2, CALL_DIRECT, PTR, PTR) /* 116  */
 ENTRY("getrusage",                SYS_getrusage,                      getrusage,                         2, CALL_DIRECT, INT, PTR)   /* 117  */
 ENTRY("getsockopt",               SYS_getsockopt,                     getsockopt,                        5, CALL_DIRECT, INT, INT, INT, PTR, PTR)   /* 118  */
 ENTRY("",                         119,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 119  old resuba */
 ENTRY("readv",                    SYS_readv,                          do_readv,                          3, CALL_DIRECT, INT, PTR, UINT)   /* 120  */
 ENTRY("writev",                   SYS_writev,                         do_writev,                         3, CALL_DIRECT, INT, PTR, UINT)   /* 121  */
 ENTRY("settimeofday",             SYS_settimeofday,                   settimeofday,                      2, CALL_DIRECT, PTR, PTR)   /* 122  */
 ENTRY("fchown",                   SYS_fchown,                         fchown,                            3, CALL_DIRECT, INT, INT, INT)   /* 123  */
 ENTRY("fchmod",                   SYS_fchmod,                         fchmod,                            2, CALL_DIRECT, INT, INT)   /* 124  */
 ENTRY("",                         125,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 125  old recvfrom */
 ENTRY("",                         126,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 126  old setreuid */
 ENTRY("",                         127,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 127  old setregid */
 ENTRY("rename",                   SYS_rename,                         rename,                            2, CALL_DIRECT, PTR, PTR)   /* 128  */
 ENTRY("",                         129,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 129  old truncate */
 ENTRY("",                         130,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 130  old ftruncate */
 ENTRY("flock",                    SYS_flock,                          flock,                             2, CALL_DIRECT, INT, INT)   /* 131  */
 ENTRY("mkfifo",                   SYS_mkfifo,                         mkfifo,                            2, CALL_DIRECT, PTR, INT)   /* 132  */
 ENTRY("sendto",                   SYS_sendto,                         sendto,                            6, CALL_DIRECT, INT, PTR, SIZE, INT, PTR, INT)   /* 133  */
 ENTRY("shutdown",                 SYS_shutdown,                       shutdown,                          2, CALL_DIRECT, INT, INT)   /* 134  */
 ENTRY("socketpair",               SYS_socketpair,                     socketpair,                        4, CALL_DIRECT, INT, INT, INT, PTR)   /* 135  */
 ENTRY("mkdir",                    SYS_mkdir,                          mkdir,                             2, CALL_DIRECT, PTR, INT)   /* 136  */
 ENTRY("rmdir",                    SYS_rmdir,                          rmdir,                             1, CALL_DIRECT, PTR)   /* 137  */
 ENTRY("utimes",                   SYS_utimes,                         do_utimes,                         2, CALL_DIRECT, PTR, PTR)   /* 138  */
 ENTRY("futimes",                  SYS_futimes,                        do_futimes,                        2, CALL_DIRECT, INT, PTR)   /* 139  */
 ENTRY("adjtime",                  SYS_adjtime,                        adjtime,                           2, CALL_DIRECT, PTR, PTR)   /* 140  */
 ENTRY("",                         141,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 141  old getpeername */
 ENTRY("",                         142,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 142  old gethostid */
 ENTRY("",                         143,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 143  old sethostid */
 ENTRY("",                         144,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 144  old getrlimit */
 ENTRY("",                         145,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 145  old setrlimit */
 ENTRY("",                         146,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 146  old killpg */
 ENTRY("setsid",                   SYS_setsid,                         setsid,                            0, CALL_DIRECT, VOID)   /* 147  */
 ENTRY("",                         148,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 148  old setquota */
 ENTRY("",                         149,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 149  old qquota */
 ENTRY("",                         150,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 150  old getsockname */
 ENTRY("getpgid",                  SYS_getpgid,                        getpgid,                           1, CALL_DIRECT, INT)   /* 151  */
 ENTRY("setprivexec",              SYS_setprivexec,                    no_syscall,                        1, CALL_INDIRECT, VOID) /* 152  */
 ENTRY("pread",                    SYS_pread,                          do_pread,                          4, CALL_DIRECT, INT, PTR, SIZE, OFFSET)   /* 153  */
 ENTRY("pwrite",                   SYS_pwrite,                         pwrite,                            4, CALL_DIRECT, INT, PTR, SIZE, OFFSET)   /* 154  */
#ifdef SYS_nfssvc
 ENTRY("nfssvc",                   SYS_nfssvc,                         nfssvc,                            2, CALL_DIRECT, INT, PTR)   /* 155  */
#else
 ENTRY("nfssvc",                   155,                                no_syscall,                        2, CALL_INDIRECT, VOID)   /* 155  */
#endif
 ENTRY("",                         155,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 155  */
 ENTRY("",                         156,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 156  old getdirentries */
 ENTRY("statfs",                   SYS_statfs,                         do_statfs,                         2, CALL_DIRECT, PTR, PTR)   /* 157  */
 ENTRY("fstatfs",                  SYS_fstatfs,                        do_fstatfs,                        2, CALL_DIRECT, INT, PTR)   /* 158  */
 ENTRY("unmount",                  SYS_unmount,                        unmount,                           2, CALL_DIRECT, PTR, INT)   /* 159  */
 ENTRY("",                         160,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 160  old async_daemon */
 ENTRY("",                         161,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 161  */
 ENTRY("",                         162,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 162  old getdomainname */
 ENTRY("",                         163,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 163  old setdomainname */
 ENTRY("",                         164,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 164  */
 ENTRY("quotactl",                 SYS_quotactl,                       no_syscall,                        4, CALL_INDIRECT, VOID) /* 165  */
 ENTRY("",                         166,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 166  old exportfs */
 ENTRY("mount",                    SYS_mount,                          mount,                             4, CALL_DIRECT, PTR, PTR, INT, PTR)   /* 167  */
 ENTRY("",                         168,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 168  old ustat */
 ENTRY("",                         169,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 169  */
 ENTRY("table",                    SYS_table,                          no_syscall,                        0, CALL_INDIRECT, VOID) /* 170  old table */
 ENTRY("",                         171,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 171  old wait3 */
 ENTRY("",                         172,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 172  old rpause */
 ENTRY("waitid",                   SYS_waitid,                         unimpl_unix_syscall,               4, CALL_INDIRECT, VOID) /* 173  */
 ENTRY("",                         174,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 174  old getdents */
 ENTRY("",                         175,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 175  old gc_control */
 ENTRY("add_profil",               SYS_add_profil,                     add_profil,                        4, CALL_DIRECT, PTR, SIZE, UINT, UINT)   /* 176  */
 ENTRY("",                         177,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 177  */
 ENTRY("",                         178,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 178  */
 ENTRY("",                         179,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 179  */
 ENTRY("kdebug_trace",             SYS_kdebug_trace,                   no_syscall,                        6, CALL_INDIRECT, VOID) /* 180  */
 ENTRY("setgid",                   SYS_setgid,                         setgid,                            1, CALL_DIRECT, INT)   /* 181  */
 ENTRY("setegid",                  SYS_setegid,                        setegid,                           1, CALL_DIRECT, INT)   /* 182  */
 ENTRY("seteuid",                  SYS_seteuid,                        seteuid,                           1, CALL_DIRECT, INT)   /* 183  */
 ENTRY("sigreturn",                SYS_sigreturn,                      do_sigreturn,                      2, CALL_INDIRECT, PTR, INT)   /* 184  */
 ENTRY("chud",                     SYS_chud,                           unimpl_unix_syscall,               6, CALL_INDIRECT, VOID)   /* 185  */
 ENTRY("",                         186,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 186  */
 ENTRY("",                         187,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 187  */
 ENTRY("stat",                     SYS_stat,                           do_stat,                           2, CALL_DIRECT, PTR, PTR)   /* 188  */
 ENTRY("fstat",                    SYS_fstat,                          do_fstat,                          2, CALL_DIRECT, INT, PTR)   /* 189  */
 ENTRY("lstat",                    SYS_lstat,                          do_lstat,                          2, CALL_DIRECT, PTR, PTR)   /* 190  */
 ENTRY("pathconf",                 SYS_pathconf,                       pathconf,                          2, CALL_DIRECT, PTR, INT)   /* 191  */
 ENTRY("fpathconf",                SYS_fpathconf,                      fpathconf,                         2, CALL_DIRECT, INT, INT)   /* 192  */
 ENTRY("getfsstat",                SYS_getfsstat,                      do_getfsstat,                      3, CALL_DIRECT, PTR, INT, INT)   /* 193  */
 ENTRY("",                         193,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 193  */
 ENTRY("getrlimit",                SYS_getrlimit,                      getrlimit,                         2, CALL_DIRECT, UINT, PTR)   /* 194  */
 ENTRY("setrlimit",                SYS_setrlimit,                      setrlimit,                         2, CALL_DIRECT, UINT, PTR)   /* 195  */
 ENTRY("getdirentries",            SYS_getdirentries,                  do_getdirentries,                  4, CALL_DIRECT, INT, PTR, UINT, PTR)   /* 196  */
 ENTRY("mmap",                     SYS_mmap,                           target_mmap,                       6, CALL_DIRECT, UINT /*PTR*/, SIZE, INT, INT, INT, OFFSET)   /* 197  */
 ENTRY("",                         198,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 198  __syscall */
 ENTRY("lseek",                    SYS_lseek,                          do_lseek,                          3, CALL_INDIRECT, INT, OFFSET, INT)   /* 199  */
 ENTRY("truncate",                 SYS_truncate,                       truncate,                          2, CALL_DIRECT, PTR, OFFSET)   /* 200  */
 ENTRY("ftruncate",                SYS_ftruncate,                      ftruncate,                         2, CALL_DIRECT, INT, OFFSET)   /* 201  */
 ENTRY("__sysctl",                 SYS___sysctl,                       do___sysctl,                       6, CALL_DIRECT, PTR, INT, PTR, PTR, PTR, SIZE)   /* 202  */
 ENTRY("mlock",                    SYS_mlock,                          mlock,                             2, CALL_DIRECT, PTR, SIZE)   /* 203  */
 ENTRY("munlock",                  SYS_munlock,                        munlock,                           2, CALL_DIRECT, PTR, SIZE)   /* 204  */
 ENTRY("undelete",                 SYS_undelete,                       undelete,                          1, CALL_DIRECT, PTR)   /* 205  */
 ENTRY("ATsocket",                 SYS_ATsocket,                       no_syscall,                        1, CALL_INDIRECT, VOID) /* 206  */
 ENTRY("ATgetmsg",                 SYS_ATgetmsg,                       no_syscall,                        4, CALL_INDIRECT, VOID) /* 207  */
 ENTRY("ATputmsg",                 SYS_ATputmsg,                       no_syscall,                        4, CALL_INDIRECT, VOID) /* 208  */
 ENTRY("ATPsndreq",                SYS_ATPsndreq,                      no_syscall,                        4, CALL_INDIRECT, VOID) /* 209  */
 ENTRY("ATPsndrsp",                SYS_ATPsndrsp,                      no_syscall,                        4, CALL_INDIRECT, VOID) /* 210  */
 ENTRY("ATPgetreq",                SYS_ATPgetreq,                      no_syscall,                        3, CALL_INDIRECT, VOID) /* 211  */
 ENTRY("ATPgetrsp",                SYS_ATPgetrsp,                      no_syscall,                        2, CALL_INDIRECT, VOID) /* 212  */
 ENTRY("",                         213,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 213  Reserved for AppleTalk */
 ENTRY("kqueue_from_portset_np",   SYS_kqueue_from_portset_np,         no_syscall,                        1, CALL_INDIRECT, VOID) /* 214  */
 ENTRY("kqueue_portset_np",        SYS_kqueue_portset_np,              no_syscall,                        1, CALL_INDIRECT, VOID) /* 215  */
 ENTRY("mkcomplex",                SYS_mkcomplex,                      no_syscall,                        3, CALL_INDIRECT, VOID)   /* 216  soon to be obsolete */
 ENTRY("statv",                    SYS_statv,                          no_syscall,                        2, CALL_INDIRECT, VOID)   /* 217  soon to be obsolete */
 ENTRY("lstatv",                   SYS_lstatv,                         no_syscall,                        2, CALL_INDIRECT, VOID)   /* 218  soon to be obsolete */
 ENTRY("fstatv",                   SYS_fstatv,                         no_syscall,                        2, CALL_INDIRECT, VOID)   /* 219  soon to be obsolete */
 ENTRY("getattrlist",              SYS_getattrlist,                    do_getattrlist,                    5, CALL_DIRECT, PTR, PTR, PTR, SIZE, UINT)   /* 220  */
 ENTRY("setattrlist",              SYS_setattrlist,                    unimpl_unix_syscall,               5, CALL_INDIRECT, VOID) /* 221  */
 ENTRY("getdirentriesattr",        SYS_getdirentriesattr,              do_getdirentriesattr,              8, CALL_DIRECT, INT, PTR, PTR, SIZE, PTR, PTR, PTR, UINT)   /* 222  */
 ENTRY("exchangedata",             SYS_exchangedata,                   exchangedata,                      3, CALL_DIRECT, PTR, PTR, UINT)   /* 223  */
 ENTRY("checkuseraccess",          SYS_checkuseraccess,                checkuseraccess,                   6, CALL_DIRECT, PTR, INT, PTR, INT, INT, UINT)   /* 224  */
 ENTRY("",                         224,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 224  HFS checkuseraccess check access to a file */
 ENTRY("searchfs",                 SYS_searchfs,                       searchfs,                          6, CALL_DIRECT, PTR, PTR, PTR, UINT, UINT, PTR)   /* 225  */
 ENTRY("delete",                   SYS_delete,                         no_syscall,                        1, CALL_INDIRECT, VOID)   /* 226  private delete ( Carbon semantics ) */
 ENTRY("copyfile",                 SYS_copyfile,                       no_syscall,                        4, CALL_INDIRECT, VOID)   /* 227  */
 ENTRY("",                         228,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 228  */
 ENTRY("",                         229,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 229  */
 ENTRY("poll",                     SYS_poll,                           no_syscall,                        3, CALL_INDIRECT, VOID) /* 230  */
 ENTRY("watchevent",               SYS_watchevent,                     no_syscall,                        2, CALL_INDIRECT, VOID)   /* 231  */
 ENTRY("waitevent",                SYS_waitevent,                      no_syscall,                        2, CALL_INDIRECT, VOID)   /* 232  */
 ENTRY("modwatch",                 SYS_modwatch,                       no_syscall,                        2, CALL_INDIRECT, VOID)   /* 233  */
 ENTRY("getxattr",                 SYS_getxattr,                       no_syscall,                        6, CALL_INDIRECT, VOID)   /* 234  */
 ENTRY("fgetxattr",                SYS_fgetxattr,                      no_syscall,                        6, CALL_INDIRECT, VOID)   /* 235  */
 ENTRY("setxattr",                 SYS_setxattr,                       no_syscall,                        6, CALL_INDIRECT, VOID)   /* 236  */
 ENTRY("fsetxattr",                SYS_fsetxattr,                      no_syscall,                        6, CALL_INDIRECT, VOID)   /* 237  */
 ENTRY("removexattr",              SYS_removexattr,                    no_syscall,                        3, CALL_INDIRECT, VOID)   /* 238  */
 ENTRY("fremovexattr",             SYS_fremovexattr,                   no_syscall,                        3, CALL_INDIRECT, VOID)   /* 239  */
 ENTRY("listxattr",                SYS_listxattr,                      listxattr,                         4, CALL_INDIRECT, VOID)   /* 240  */
 ENTRY("flistxattr",               SYS_flistxattr,                     no_syscall,                        4, CALL_INDIRECT, VOID)   /* 241  */
 ENTRY("fsctl",                    SYS_fsctl,                          fsctl,                             4, CALL_DIRECT, PTR, UINT, PTR, UINT)   /* 242  */
 ENTRY("initgroups",               SYS_initgroups,                     unimpl_unix_syscall,               3, CALL_INDIRECT, UINT, PTR, INT)   /* 243  */
 ENTRY("",                         244,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 244  */
 ENTRY("",                         245,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 245  */
 ENTRY("",                         246,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 246  */
#ifdef SYS_nfsclnt
 ENTRY("nfsclnt",                  SYS_nfsclnt,                        nfsclnt,                           2, CALL_DIRECT, INT, PTR)   /* 247  */
#else
 ENTRY("nfsclnt",                  247,                                no_syscall,                        2, CALL_INDIRECT, VOID)   /* 247  */
#endif
 ENTRY("",                         247,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 247  */
 ENTRY("",                         248,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 248  */
 ENTRY("",                         249,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 249  */
 ENTRY("minherit",                 SYS_minherit,                       minherit,                          3, CALL_DIRECT, PTR, INT, INT)   /* 250  */
 ENTRY("semsys",                   SYS_semsys,                         unimpl_unix_syscall,               5, CALL_INDIRECT, VOID)   /* 251  */
 ENTRY("msgsys",                   SYS_msgsys,                         unimpl_unix_syscall,               5, CALL_INDIRECT, VOID)   /* 252  */
 ENTRY("shmsys",                   SYS_shmsys,                         unimpl_unix_syscall,               4, CALL_INDIRECT, VOID)   /* 253  */
 ENTRY("semctl",                   SYS_semctl,                         unimpl_unix_syscall,               4, CALL_INDIRECT, VOID)   /* 254  */
 ENTRY("semget",                   SYS_semget,                         unimpl_unix_syscall,               3, CALL_INDIRECT, VOID)   /* 255  */
 ENTRY("semop",                    SYS_semop,                          unimpl_unix_syscall,               3, CALL_INDIRECT, VOID)   /* 256  */
 ENTRY("",                         257,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 257  */
 ENTRY("msgctl",                   SYS_msgctl,                         unimpl_unix_syscall,               3, CALL_INDIRECT, VOID)   /* 258  */
 ENTRY("msgget",                   SYS_msgget,                         unimpl_unix_syscall,               2, CALL_INDIRECT, VOID)   /* 259  */
 ENTRY("msgsnd",                   SYS_msgsnd,                         unimpl_unix_syscall,               4, CALL_INDIRECT, VOID)   /* 260  */
 ENTRY("msgrcv",                   SYS_msgrcv,                         unimpl_unix_syscall,               5, CALL_INDIRECT, VOID)   /* 261  */
 ENTRY("shmat",                    SYS_shmat,                          unimpl_unix_syscall,               3, CALL_INDIRECT, VOID)   /* 262  */
 ENTRY("shmctl",                   SYS_shmctl,                         unimpl_unix_syscall,               3, CALL_INDIRECT, VOID)   /* 263  */
 ENTRY("shmdt",                    SYS_shmdt,                          unimpl_unix_syscall,               1, CALL_INDIRECT, VOID)   /* 264  */
 ENTRY("shmget",                   SYS_shmget,                         unimpl_unix_syscall,               3, CALL_INDIRECT, VOID)   /* 265  */
 ENTRY("shm_open",                 SYS_shm_open,                       shm_open,                          3, CALL_DIRECT, PTR, INT, INT)   /* 266  */
 ENTRY("shm_unlink",               SYS_shm_unlink,                     shm_unlink,                        1, CALL_DIRECT, PTR)   /* 267  */
 ENTRY("sem_open",                 SYS_sem_open,                       unimpl_unix_syscall,               4, CALL_INDIRECT, VOID)   /* 268  */
 ENTRY("sem_close",                SYS_sem_close,                      unimpl_unix_syscall,               1, CALL_INDIRECT, VOID)   /* 269  */
 ENTRY("sem_unlink",               SYS_sem_unlink,                     unimpl_unix_syscall,               1, CALL_INDIRECT, VOID)   /* 270  */
 ENTRY("sem_wait",                 SYS_sem_wait,                       unimpl_unix_syscall,               1, CALL_INDIRECT, VOID)   /* 271  */
 ENTRY("sem_trywait",              SYS_sem_trywait,                    unimpl_unix_syscall,               1, CALL_INDIRECT, VOID)   /* 272  */
 ENTRY("sem_post",                 SYS_sem_post,                       unimpl_unix_syscall,               1, CALL_INDIRECT, VOID)   /* 273  */
 ENTRY("sem_getvalue",             SYS_sem_getvalue,                   unimpl_unix_syscall,               2, CALL_INDIRECT, VOID)   /* 274  */
 ENTRY("sem_init",                 SYS_sem_init,                       unimpl_unix_syscall,               3, CALL_INDIRECT, VOID)   /* 275  */
 ENTRY("sem_destroy",              SYS_sem_destroy,                    unimpl_unix_syscall,               1, CALL_INDIRECT, VOID)   /* 276  */
 ENTRY("open_extended",            SYS_open_extended,                  unimpl_unix_syscall,               6, CALL_INDIRECT, VOID)   /* 277  */
 ENTRY("umask_extended",           SYS_umask_extended,                 unimpl_unix_syscall,               2, CALL_INDIRECT, VOID)   /* 278  */
 ENTRY("stat_extended",            SYS_stat_extended,                  unimpl_unix_syscall,               4, CALL_INDIRECT, VOID)   /* 279  */
 ENTRY("lstat_extended",           SYS_lstat_extended,                 unimpl_unix_syscall,               4, CALL_INDIRECT, VOID)   /* 280  */
 ENTRY("fstat_extended",           SYS_fstat_extended,                 unimpl_unix_syscall,               4, CALL_INDIRECT, VOID)   /* 281  */
 ENTRY("chmod_extended",           SYS_chmod_extended,                 unimpl_unix_syscall,               5, CALL_INDIRECT, VOID)   /* 282  */
 ENTRY("fchmod_extended",          SYS_fchmod_extended,                unimpl_unix_syscall,               5, CALL_INDIRECT, VOID)   /* 283  */
 ENTRY("access_extended",          SYS_access_extended,                unimpl_unix_syscall,               4, CALL_INDIRECT, VOID)   /* 284  */
 ENTRY("settid",                   SYS_settid,                         unimpl_unix_syscall,               2, CALL_INDIRECT, VOID)   /* 285  */
 ENTRY("gettid",                   SYS_gettid,                         unimpl_unix_syscall,               2, CALL_INDIRECT, VOID)   /* 286  */
 ENTRY("setsgroups",               SYS_setsgroups,                     unimpl_unix_syscall,               2, CALL_INDIRECT, VOID)   /* 287  */
 ENTRY("getsgroups",               SYS_getsgroups,                     unimpl_unix_syscall,               2, CALL_INDIRECT, VOID)   /* 288  */
 ENTRY("setwgroups",               SYS_setwgroups,                     unimpl_unix_syscall,               2, CALL_INDIRECT, VOID)   /* 289  */
 ENTRY("getwgroups",               SYS_getwgroups,                     unimpl_unix_syscall,               2, CALL_INDIRECT, VOID)   /* 290  */
 ENTRY("mkfifo_extended",          SYS_mkfifo_extended,                unimpl_unix_syscall,               5, CALL_INDIRECT, VOID)   /* 291  */
 ENTRY("mkdir_extended",           SYS_mkdir_extended,                 unimpl_unix_syscall,               5, CALL_INDIRECT, VOID)   /* 292  */
 ENTRY("identitysvc",              SYS_identitysvc,                    unimpl_unix_syscall,               2, CALL_INDIRECT, VOID)   /* 293  */
 ENTRY("",                         294,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 294  */
 ENTRY("",                         295,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 295  */
 ENTRY("load_shared_file",         SYS_load_shared_file,               unimpl_unix_syscall,               7, CALL_INDIRECT, VOID)   /* 296  */
 ENTRY("reset_shared_file",        SYS_reset_shared_file,              unimpl_unix_syscall,               3, CALL_INDIRECT, VOID)   /* 297  */
 ENTRY("new_system_shared_regions",  SYS_new_system_shared_regions,    unimpl_unix_syscall,               0, CALL_INDIRECT, VOID)   /* 298  */
 ENTRY("shared_region_map_file_np",  SYS_shared_region_map_file_np,    unimpl_unix_syscall,               4, CALL_INDIRECT, VOID)   /* 299  */
 ENTRY("shared_region_make_private_np",  SYS_shared_region_make_private_np,  unimpl_unix_syscall,         2, CALL_INDIRECT, VOID)   /* 300  */
 ENTRY("",                         301,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 301  */
 ENTRY("",                         302,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 302  */
 ENTRY("",                         303,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 303  */
 ENTRY("",                         304,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 304  */
 ENTRY("",                         305,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 305  */
 ENTRY("",                         306,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 306  */
 ENTRY("",                         307,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 307  */
 ENTRY("",                         308,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 308  */
 ENTRY("",                         309,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 309  */
 ENTRY("getsid",                   SYS_getsid,                         getsid,                            1, CALL_DIRECT, INT)   /* 310  */
 ENTRY("settid_with_pid",          SYS_settid_with_pid,                unimpl_unix_syscall,               2, CALL_INDIRECT, VOID)   /* 311  */
 ENTRY("",                         312,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 312  */
 ENTRY("aio_fsync",                SYS_aio_fsync,                      unimpl_unix_syscall,               2, CALL_INDIRECT, VOID)   /* 313  */
 ENTRY("aio_return",               SYS_aio_return,                     unimpl_unix_syscall,               1, CALL_INDIRECT, VOID)   /* 314  */
 ENTRY("aio_suspend",              SYS_aio_suspend,                    unimpl_unix_syscall,               3, CALL_INDIRECT, VOID)   /* 315  */
 ENTRY("aio_cancel",               SYS_aio_cancel,                     unimpl_unix_syscall,               2, CALL_INDIRECT, VOID)   /* 316  */
 ENTRY("aio_error",                SYS_aio_error,                      unimpl_unix_syscall,               1, CALL_INDIRECT, VOID)   /* 317  */
 ENTRY("aio_read",                 SYS_aio_read,                       unimpl_unix_syscall,               1, CALL_INDIRECT, VOID)   /* 318  */
 ENTRY("aio_write",                SYS_aio_write,                      unimpl_unix_syscall,               1, CALL_INDIRECT, VOID)   /* 319  */
 ENTRY("lio_listio",               SYS_lio_listio,                     unimpl_unix_syscall,               4, CALL_INDIRECT, VOID)   /* 320  */
 ENTRY("",                         321,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 321  */
 ENTRY("",                         322,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 322  */
 ENTRY("",                         323,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 323  */
 ENTRY("mlockall",                 SYS_mlockall,                       mlockall,                          1, CALL_DIRECT, INT)   /* 324  */
 ENTRY("munlockall",               SYS_munlockall,                     unimpl_unix_syscall,               1, CALL_INDIRECT, VOID)   /* 325  */
 ENTRY("",                         326,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 326  */
 ENTRY("issetugid",                SYS_issetugid,                      issetugid,                         0, CALL_DIRECT, VOID)   /* 327  */
 ENTRY("__pthread_kill",           SYS___pthread_kill,                 unimpl_unix_syscall,               2, CALL_INDIRECT, VOID)   /* 328  */
 ENTRY("pthread_sigmask",          SYS_pthread_sigmask,                pthread_sigmask,                   3, CALL_DIRECT, INT, PTR, PTR)   /* 329  */
 ENTRY("sigwait",                  SYS_sigwait,                        sigwait,                           2, CALL_DIRECT, PTR, PTR)   /* 330  */
 ENTRY("__disable_threadsignal",   SYS___disable_threadsignal,         unimpl_unix_syscall,               1, CALL_INDIRECT, VOID)   /* 331  */
 ENTRY("__pthread_markcancel",     SYS___pthread_markcancel,           unimpl_unix_syscall,               1, CALL_INDIRECT, VOID)   /* 332  */
 ENTRY("__pthread_canceled",       SYS___pthread_canceled,             unimpl_unix_syscall,               1, CALL_INDIRECT, VOID)   /* 333  */
 ENTRY("__semwait_signal",         SYS___semwait_signal,               unimpl_unix_syscall,               6, CALL_INDIRECT, VOID)   /* 334  */
 ENTRY("utrace",                   SYS_utrace,                         unimpl_unix_syscall,               2, CALL_INDIRECT, VOID)   /* 335  */
 ENTRY("proc_info",                SYS_proc_info,                      unimpl_unix_syscall,               6, CALL_INDIRECT, VOID)   /* 336  */
 ENTRY("",                         337,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 337  */
 ENTRY("",                         338,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 338  */
 ENTRY("",                         339,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 339  */
 ENTRY("",                         340,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 340  */
 ENTRY("",                         341,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 341  */
 ENTRY("",                         342,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 342  */
 ENTRY("",                         343,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 343  */
 ENTRY("",                         344,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 344  */
 ENTRY("",                         345,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 345  */
 ENTRY("",                         346,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 346  */
 ENTRY("",                         347,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 347  */
 ENTRY("",                         348,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 348  */
 ENTRY("",                         349,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 349  */
 ENTRY("audit",                    SYS_audit,                          audit,                             2, CALL_DIRECT, PTR, INT)   /* 350  */
 ENTRY("auditon",                  SYS_auditon,                        auditon,                           3, CALL_DIRECT, INT, PTR, INT)   /* 351  */
 ENTRY("",                         352,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 352  */
 ENTRY("getauid",                  SYS_getauid,                        getauid,                           1, CALL_DIRECT, PTR)   /* 353  */
 ENTRY("setauid",                  SYS_setauid,                        setauid,                           1, CALL_DIRECT, PTR)   /* 354  */
 ENTRY("getaudit",                 SYS_getaudit,                       getaudit,                          1, CALL_DIRECT, PTR)   /* 355  */
 ENTRY("setaudit",                 SYS_setaudit,                       setaudit,                          1, CALL_DIRECT, PTR)   /* 356  */
 ENTRY("getaudit_addr",            SYS_getaudit_addr,                  getaudit_addr,                     2, CALL_DIRECT, PTR, INT)   /* 357  */
 ENTRY("setaudit_addr",            SYS_setaudit_addr,                  setaudit_addr,                     2, CALL_DIRECT, PTR, INT)   /* 358  */
 ENTRY("auditctl",                 SYS_auditctl,                       auditctl,                          1, CALL_DIRECT, PTR)   /* 359  */
 ENTRY("",                         360,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 360  */
 ENTRY("",                         361,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 361  */
 ENTRY("kqueue",                   SYS_kqueue,                         kqueue,                            0, CALL_DIRECT, VOID)   /* 362  */
 ENTRY("kevent",                   SYS_kevent,                         kevent,                            6, CALL_DIRECT, INT, PTR, INT, PTR, INT, PTR)   /* 363  */
 ENTRY("lchown",                   SYS_lchown,                         lchown,                            3, CALL_DIRECT, PTR, INT , INT)   /* 364  */
 ENTRY("stack_snapshot",           SYS_stack_snapshot,                 unimpl_unix_syscall,               4, CALL_INDIRECT, VOID)   /* 365  */
 ENTRY("",                         366,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 366  */
 ENTRY("",                         367,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 367  */
 ENTRY("",                         368,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 368  */
 ENTRY("",                         369,                                no_syscall,                        0, CALL_INDIRECT, VOID) /* 369  */

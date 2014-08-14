/*
 * QEMU seccomp mode 2 support with libseccomp
 *
 * Copyright IBM, Corp. 2012
 *
 * Authors:
 *  Eduardo Otubo    <eotubo@br.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */
#include <stdio.h>
#include <seccomp.h>
#include "sysemu/seccomp.h"

struct QemuSeccompSyscall {
    int32_t num;
    uint8_t priority;
};

static const struct QemuSeccompSyscall seccomp_whitelist[] = {
    { SCMP_SYS(timer_settime), 255 },
    { SCMP_SYS(timer_gettime), 254 },
    { SCMP_SYS(futex), 253 },
    { SCMP_SYS(select), 252 },
    { SCMP_SYS(recvfrom), 251 },
    { SCMP_SYS(sendto), 250 },
    { SCMP_SYS(socketcall), 250 },
    { SCMP_SYS(read), 249 },
    { SCMP_SYS(io_submit), 249 },
    { SCMP_SYS(brk), 248 },
    { SCMP_SYS(clone), 247 },
    { SCMP_SYS(mmap), 247 },
    { SCMP_SYS(mprotect), 246 },
    { SCMP_SYS(execve), 245 },
    { SCMP_SYS(open), 245 },
    { SCMP_SYS(ioctl), 245 },
    { SCMP_SYS(socket), 245 },
    { SCMP_SYS(setsockopt), 245 },
    { SCMP_SYS(recvmsg), 245 },
    { SCMP_SYS(sendmsg), 245 },
    { SCMP_SYS(accept), 245 },
    { SCMP_SYS(connect), 245 },
    { SCMP_SYS(socketpair), 245 },
    { SCMP_SYS(bind), 245 },
    { SCMP_SYS(listen), 245 },
    { SCMP_SYS(semget), 245 },
    { SCMP_SYS(ipc), 245 },
    { SCMP_SYS(gettimeofday), 245 },
    { SCMP_SYS(readlink), 245 },
    { SCMP_SYS(access), 245 },
    { SCMP_SYS(prctl), 245 },
    { SCMP_SYS(signalfd), 245 },
    { SCMP_SYS(getrlimit), 245 },
    { SCMP_SYS(set_tid_address), 245 },
    { SCMP_SYS(statfs), 245 },
    { SCMP_SYS(unlink), 245 },
    { SCMP_SYS(wait4), 245 },
    { SCMP_SYS(fcntl64), 245 },
    { SCMP_SYS(fstat64), 245 },
    { SCMP_SYS(stat64), 245 },
    { SCMP_SYS(getgid32), 245 },
    { SCMP_SYS(getegid32), 245 },
    { SCMP_SYS(getuid32), 245 },
    { SCMP_SYS(geteuid32), 245 },
    { SCMP_SYS(sigreturn), 245 },
    { SCMP_SYS(_newselect), 245 },
    { SCMP_SYS(_llseek), 245 },
    { SCMP_SYS(mmap2), 245 },
    { SCMP_SYS(sigprocmask), 245 },
    { SCMP_SYS(sched_getparam), 245 },
    { SCMP_SYS(sched_getscheduler), 245 },
    { SCMP_SYS(fstat), 245 },
    { SCMP_SYS(clock_getres), 245 },
    { SCMP_SYS(sched_get_priority_min), 245 },
    { SCMP_SYS(sched_get_priority_max), 245 },
    { SCMP_SYS(stat), 245 },
    { SCMP_SYS(uname), 245 },
    { SCMP_SYS(eventfd2), 245 },
    { SCMP_SYS(io_getevents), 245 },
    { SCMP_SYS(dup), 245 },
    { SCMP_SYS(dup2), 245 },
    { SCMP_SYS(dup3), 245 },
    { SCMP_SYS(gettid), 245 },
    { SCMP_SYS(getgid), 245 },
    { SCMP_SYS(getegid), 245 },
    { SCMP_SYS(getuid), 245 },
    { SCMP_SYS(geteuid), 245 },
    { SCMP_SYS(timer_create), 245 },
    { SCMP_SYS(times), 245 },
    { SCMP_SYS(exit), 245 },
    { SCMP_SYS(clock_gettime), 245 },
    { SCMP_SYS(time), 245 },
    { SCMP_SYS(restart_syscall), 245 },
    { SCMP_SYS(pwrite64), 245 },
    { SCMP_SYS(nanosleep), 245 },
    { SCMP_SYS(chown), 245 },
    { SCMP_SYS(openat), 245 },
    { SCMP_SYS(getdents), 245 },
    { SCMP_SYS(timer_delete), 245 },
    { SCMP_SYS(exit_group), 245 },
    { SCMP_SYS(rt_sigreturn), 245 },
    { SCMP_SYS(sync), 245 },
    { SCMP_SYS(pread64), 245 },
    { SCMP_SYS(madvise), 245 },
    { SCMP_SYS(set_robust_list), 245 },
    { SCMP_SYS(lseek), 245 },
    { SCMP_SYS(pselect6), 245 },
    { SCMP_SYS(fork), 245 },
    { SCMP_SYS(rt_sigprocmask), 245 },
    { SCMP_SYS(write), 244 },
    { SCMP_SYS(fcntl), 243 },
    { SCMP_SYS(tgkill), 242 },
    { SCMP_SYS(kill), 242 },
    { SCMP_SYS(rt_sigaction), 242 },
    { SCMP_SYS(pipe2), 242 },
    { SCMP_SYS(munmap), 242 },
    { SCMP_SYS(mremap), 242 },
    { SCMP_SYS(fdatasync), 242 },
    { SCMP_SYS(close), 242 },
    { SCMP_SYS(rt_sigpending), 242 },
    { SCMP_SYS(rt_sigtimedwait), 242 },
    { SCMP_SYS(readv), 242 },
    { SCMP_SYS(writev), 242 },
    { SCMP_SYS(preadv), 242 },
    { SCMP_SYS(pwritev), 242 },
    { SCMP_SYS(setrlimit), 242 },
    { SCMP_SYS(ftruncate), 242 },
    { SCMP_SYS(lstat), 242 },
    { SCMP_SYS(pipe), 242 },
    { SCMP_SYS(umask), 242 },
    { SCMP_SYS(chdir), 242 },
    { SCMP_SYS(setitimer), 242 },
    { SCMP_SYS(setsid), 242 },
    { SCMP_SYS(poll), 242 },
    { SCMP_SYS(epoll_create), 242 },
    { SCMP_SYS(epoll_ctl), 242 },
    { SCMP_SYS(epoll_wait), 242 },
    { SCMP_SYS(waitpid), 242 },
    { SCMP_SYS(getsockname), 242 },
    { SCMP_SYS(getpeername), 242 },
    { SCMP_SYS(accept4), 242 },
    { SCMP_SYS(timerfd_settime), 242 },
    { SCMP_SYS(newfstatat), 241 },
    { SCMP_SYS(shutdown), 241 },
    { SCMP_SYS(getsockopt), 241 },
    { SCMP_SYS(semop), 241 },
    { SCMP_SYS(semtimedop), 241 },
    { SCMP_SYS(epoll_ctl_old), 241 },
    { SCMP_SYS(epoll_wait_old), 241 },
    { SCMP_SYS(epoll_pwait), 241 },
    { SCMP_SYS(epoll_create1), 241 },
    { SCMP_SYS(ppoll), 241 },
    { SCMP_SYS(creat), 241 },
    { SCMP_SYS(link), 241 },
    { SCMP_SYS(getpid), 241 },
    { SCMP_SYS(getppid), 241 },
    { SCMP_SYS(getpgrp), 241 },
    { SCMP_SYS(getpgid), 241 },
    { SCMP_SYS(getsid), 241 },
    { SCMP_SYS(getdents64), 241 },
    { SCMP_SYS(getresuid), 241 },
    { SCMP_SYS(getresgid), 241 },
    { SCMP_SYS(getgroups), 241 },
    { SCMP_SYS(getresuid32), 241 },
    { SCMP_SYS(getresgid32), 241 },
    { SCMP_SYS(getgroups32), 241 },
    { SCMP_SYS(signal), 241 },
    { SCMP_SYS(sigaction), 241 },
    { SCMP_SYS(sigsuspend), 241 },
    { SCMP_SYS(sigpending), 241 },
    { SCMP_SYS(truncate64), 241 },
    { SCMP_SYS(ftruncate64), 241 },
    { SCMP_SYS(fchown32), 241 },
    { SCMP_SYS(chown32), 241 },
    { SCMP_SYS(lchown32), 241 },
    { SCMP_SYS(statfs64), 241 },
    { SCMP_SYS(fstatfs64), 241 },
    { SCMP_SYS(fstatat64), 241 },
    { SCMP_SYS(lstat64), 241 },
    { SCMP_SYS(sendfile64), 241 },
    { SCMP_SYS(ugetrlimit), 241 },
    { SCMP_SYS(alarm), 241 },
    { SCMP_SYS(rt_sigsuspend), 241 },
    { SCMP_SYS(rt_sigqueueinfo), 241 },
    { SCMP_SYS(rt_tgsigqueueinfo), 241 },
    { SCMP_SYS(sigaltstack), 241 },
    { SCMP_SYS(signalfd4), 241 },
    { SCMP_SYS(truncate), 241 },
    { SCMP_SYS(fchown), 241 },
    { SCMP_SYS(lchown), 241 },
    { SCMP_SYS(fchownat), 241 },
    { SCMP_SYS(fstatfs), 241 },
    { SCMP_SYS(getitimer), 241 },
    { SCMP_SYS(syncfs), 241 },
    { SCMP_SYS(fsync), 241 },
    { SCMP_SYS(fchdir), 241 },
    { SCMP_SYS(msync), 241 },
    { SCMP_SYS(sched_setparam), 241 },
    { SCMP_SYS(sched_setscheduler), 241 },
    { SCMP_SYS(sched_yield), 241 },
    { SCMP_SYS(sched_rr_get_interval), 241 },
    { SCMP_SYS(sched_setaffinity), 241 },
    { SCMP_SYS(sched_getaffinity), 241 },
    { SCMP_SYS(readahead), 241 },
    { SCMP_SYS(timer_getoverrun), 241 },
    { SCMP_SYS(unlinkat), 241 },
    { SCMP_SYS(readlinkat), 241 },
    { SCMP_SYS(faccessat), 241 },
    { SCMP_SYS(get_robust_list), 241 },
    { SCMP_SYS(splice), 241 },
    { SCMP_SYS(vmsplice), 241 },
    { SCMP_SYS(getcpu), 241 },
    { SCMP_SYS(sendmmsg), 241 },
    { SCMP_SYS(recvmmsg), 241 },
    { SCMP_SYS(prlimit64), 241 },
    { SCMP_SYS(waitid), 241 },
    { SCMP_SYS(io_cancel), 241 },
    { SCMP_SYS(io_setup), 241 },
    { SCMP_SYS(io_destroy), 241 },
    { SCMP_SYS(arch_prctl), 240 },
    { SCMP_SYS(mkdir), 240 },
    { SCMP_SYS(fchmod), 240 },
    { SCMP_SYS(shmget), 240 },
    { SCMP_SYS(shmat), 240 },
    { SCMP_SYS(shmdt), 240 },
    { SCMP_SYS(timerfd_create), 240 },
    { SCMP_SYS(shmctl), 240 },
    { SCMP_SYS(mlock), 240 },
    { SCMP_SYS(munlock), 240 },
    { SCMP_SYS(semctl), 240 }
};

int seccomp_start(void)
{
    int rc = 0;
    unsigned int i = 0;
    scmp_filter_ctx ctx;

    ctx = seccomp_init(SCMP_ACT_KILL);
    if (ctx == NULL) {
        rc = -1;
        goto seccomp_return;
    }

    for (i = 0; i < ARRAY_SIZE(seccomp_whitelist); i++) {
        rc = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, seccomp_whitelist[i].num, 0);
        if (rc < 0) {
            goto seccomp_return;
        }
        rc = seccomp_syscall_priority(ctx, seccomp_whitelist[i].num,
                                      seccomp_whitelist[i].priority);
        if (rc < 0) {
            goto seccomp_return;
        }
    }

    rc = seccomp_load(ctx);

  seccomp_return:
    seccomp_release(ctx);
    return rc;
}

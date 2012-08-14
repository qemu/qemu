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
#include "qemu-seccomp.h"

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
    { SCMP_SYS(read), 249 },
    { SCMP_SYS(brk), 248 },
    { SCMP_SYS(clone), 247 },
    { SCMP_SYS(mmap), 247 },
    { SCMP_SYS(mprotect), 246 },
    { SCMP_SYS(execve), 245 },
    { SCMP_SYS(open), 245 },
    { SCMP_SYS(ioctl), 245 },
    { SCMP_SYS(recvmsg), 245 },
    { SCMP_SYS(sendmsg), 245 },
    { SCMP_SYS(accept), 245 },
    { SCMP_SYS(connect), 245 },
    { SCMP_SYS(gettimeofday), 245 },
    { SCMP_SYS(readlink), 245 },
    { SCMP_SYS(access), 245 },
    { SCMP_SYS(prctl), 245 },
    { SCMP_SYS(signalfd), 245 },
#if defined(__i386__)
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
    { SCMP_SYS(mmap2), 245},
    { SCMP_SYS(sigprocmask), 245 },
#elif defined(__x86_64__)
    { SCMP_SYS(sched_getparam), 245},
    { SCMP_SYS(sched_getscheduler), 245},
    { SCMP_SYS(fstat), 245},
    { SCMP_SYS(clock_getres), 245},
    { SCMP_SYS(sched_get_priority_min), 245},
    { SCMP_SYS(sched_get_priority_max), 245},
    { SCMP_SYS(stat), 245},
    { SCMP_SYS(socket), 245},
    { SCMP_SYS(setsockopt), 245},
    { SCMP_SYS(uname), 245},
    { SCMP_SYS(semget), 245},
#endif
    { SCMP_SYS(eventfd2), 245 },
    { SCMP_SYS(dup), 245 },
    { SCMP_SYS(gettid), 245 },
    { SCMP_SYS(timer_create), 245 },
    { SCMP_SYS(exit), 245 },
    { SCMP_SYS(clock_gettime), 245 },
    { SCMP_SYS(time), 245 },
    { SCMP_SYS(restart_syscall), 245 },
    { SCMP_SYS(pwrite64), 245 },
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
    { SCMP_SYS(bind), 245 },
    { SCMP_SYS(listen), 245 },
    { SCMP_SYS(eventfd), 245 },
    { SCMP_SYS(rt_sigprocmask), 245 },
    { SCMP_SYS(write), 244 },
    { SCMP_SYS(fcntl), 243 },
    { SCMP_SYS(tgkill), 242 },
    { SCMP_SYS(rt_sigaction), 242 },
    { SCMP_SYS(pipe2), 242 },
    { SCMP_SYS(munmap), 242 },
    { SCMP_SYS(mremap), 242 },
    { SCMP_SYS(getsockname), 242 },
    { SCMP_SYS(getpeername), 242 },
    { SCMP_SYS(fdatasync), 242 },
    { SCMP_SYS(close), 242 }
};

int seccomp_start(void)
{
    int rc = 0;
    unsigned int i = 0;
    scmp_filter_ctx ctx;

    ctx = seccomp_init(SCMP_ACT_KILL);
    if (ctx == NULL) {
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

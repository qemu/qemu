/*
 * Seccomp sandboxing for virtiofsd
 *
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "seccomp.h"
#include "fuse_i.h"
#include "fuse_log.h"
#include <errno.h>
#include <glib.h>
#include <seccomp.h>
#include <stdlib.h>

/* Bodge for libseccomp 2.4.2 which broke ppoll */
#if !defined(__SNR_ppoll) && defined(__SNR_brk)
#ifdef __NR_ppoll
#define __SNR_ppoll __NR_ppoll
#else
#define __SNR_ppoll __PNR_ppoll
#endif
#endif

static const int syscall_whitelist[] = {
    /* TODO ireg sem*() syscalls */
    SCMP_SYS(brk),
    SCMP_SYS(capget), /* For CAP_FSETID */
    SCMP_SYS(capset),
    SCMP_SYS(clock_gettime),
    SCMP_SYS(clone),
#ifdef __NR_clone3
    SCMP_SYS(clone3),
#endif
    SCMP_SYS(close),
    SCMP_SYS(copy_file_range),
    SCMP_SYS(dup),
    SCMP_SYS(eventfd2),
    SCMP_SYS(exit),
    SCMP_SYS(exit_group),
    SCMP_SYS(fallocate),
    SCMP_SYS(fchmodat),
    SCMP_SYS(fchownat),
    SCMP_SYS(fcntl),
    SCMP_SYS(fdatasync),
    SCMP_SYS(fgetxattr),
    SCMP_SYS(flistxattr),
    SCMP_SYS(flock),
    SCMP_SYS(fremovexattr),
    SCMP_SYS(fsetxattr),
    SCMP_SYS(fstat),
    SCMP_SYS(fstatfs),
    SCMP_SYS(fsync),
    SCMP_SYS(ftruncate),
    SCMP_SYS(futex),
    SCMP_SYS(getdents),
    SCMP_SYS(getdents64),
    SCMP_SYS(getegid),
    SCMP_SYS(geteuid),
    SCMP_SYS(getpid),
    SCMP_SYS(gettid),
    SCMP_SYS(gettimeofday),
    SCMP_SYS(linkat),
    SCMP_SYS(lseek),
    SCMP_SYS(madvise),
    SCMP_SYS(mkdirat),
    SCMP_SYS(mknodat),
    SCMP_SYS(mmap),
    SCMP_SYS(mprotect),
    SCMP_SYS(mremap),
    SCMP_SYS(munmap),
    SCMP_SYS(newfstatat),
    SCMP_SYS(open),
    SCMP_SYS(openat),
    SCMP_SYS(ppoll),
    SCMP_SYS(prctl), /* TODO restrict to just PR_SET_NAME? */
    SCMP_SYS(preadv),
    SCMP_SYS(pread64),
    SCMP_SYS(pwritev),
    SCMP_SYS(pwrite64),
    SCMP_SYS(read),
    SCMP_SYS(readlinkat),
    SCMP_SYS(recvmsg),
    SCMP_SYS(renameat),
    SCMP_SYS(renameat2),
    SCMP_SYS(rt_sigaction),
    SCMP_SYS(rt_sigprocmask),
    SCMP_SYS(rt_sigreturn),
    SCMP_SYS(sendmsg),
    SCMP_SYS(setresgid),
    SCMP_SYS(setresuid),
#ifdef __NR_setresgid32
    SCMP_SYS(setresgid32),
#endif
#ifdef __NR_setresuid32
    SCMP_SYS(setresuid32),
#endif
    SCMP_SYS(set_robust_list),
    SCMP_SYS(symlinkat),
    SCMP_SYS(time), /* Rarely needed, except on static builds */
    SCMP_SYS(tgkill),
    SCMP_SYS(unlinkat),
    SCMP_SYS(utimensat),
    SCMP_SYS(write),
    SCMP_SYS(writev),
};

void setup_seccomp(void)
{
    scmp_filter_ctx ctx;
    size_t i;

#ifdef SCMP_ACT_KILL_PROCESS
    ctx = seccomp_init(SCMP_ACT_KILL_PROCESS);
    /* Handle a newer libseccomp but an older kernel */
    if (!ctx && errno == EOPNOTSUPP) {
        ctx = seccomp_init(SCMP_ACT_TRAP);
    }
#else
    ctx = seccomp_init(SCMP_ACT_TRAP);
#endif
    if (!ctx) {
        fuse_log(FUSE_LOG_ERR, "seccomp_init() failed\n");
        exit(1);
    }

    for (i = 0; i < G_N_ELEMENTS(syscall_whitelist); i++) {
        if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW,
                             syscall_whitelist[i], 0) != 0) {
            fuse_log(FUSE_LOG_ERR, "seccomp_rule_add syscall %d",
                     syscall_whitelist[i]);
            exit(1);
        }
    }

    /* libvhost-user calls this for post-copy migration, we don't need it */
    if (seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOSYS),
                         SCMP_SYS(userfaultfd), 0) != 0) {
        fuse_log(FUSE_LOG_ERR, "seccomp_rule_add userfaultfd failed\n");
        exit(1);
    }

    if (seccomp_load(ctx) < 0) {
        fuse_log(FUSE_LOG_ERR, "seccomp_load() failed\n");
        exit(1);
    }

    seccomp_release(ctx);
}

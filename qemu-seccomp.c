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
#include "qemu/osdep.h"
#include <seccomp.h>
#include "sysemu/seccomp.h"

/* For some architectures (notably ARM) cacheflush is not supported until
 * libseccomp 2.2.3, but configure enforces that we are using a more recent
 * version on those hosts, so it is OK for this check to be less strict.
 */
#if SCMP_VER_MAJOR >= 3
  #define HAVE_CACHEFLUSH
#elif SCMP_VER_MAJOR == 2 && SCMP_VER_MINOR >= 2
  #define HAVE_CACHEFLUSH
#endif

struct QemuSeccompSyscall {
    int32_t num;
    uint8_t set;
};

static const struct QemuSeccompSyscall blacklist[] = {
    /* default set of syscalls to blacklist */
    { SCMP_SYS(reboot),                 QEMU_SECCOMP_SET_DEFAULT },
    { SCMP_SYS(swapon),                 QEMU_SECCOMP_SET_DEFAULT },
    { SCMP_SYS(swapoff),                QEMU_SECCOMP_SET_DEFAULT },
    { SCMP_SYS(syslog),                 QEMU_SECCOMP_SET_DEFAULT },
    { SCMP_SYS(mount),                  QEMU_SECCOMP_SET_DEFAULT },
    { SCMP_SYS(umount),                 QEMU_SECCOMP_SET_DEFAULT },
    { SCMP_SYS(kexec_load),             QEMU_SECCOMP_SET_DEFAULT },
    { SCMP_SYS(afs_syscall),            QEMU_SECCOMP_SET_DEFAULT },
    { SCMP_SYS(break),                  QEMU_SECCOMP_SET_DEFAULT },
    { SCMP_SYS(ftime),                  QEMU_SECCOMP_SET_DEFAULT },
    { SCMP_SYS(getpmsg),                QEMU_SECCOMP_SET_DEFAULT },
    { SCMP_SYS(gtty),                   QEMU_SECCOMP_SET_DEFAULT },
    { SCMP_SYS(lock),                   QEMU_SECCOMP_SET_DEFAULT },
    { SCMP_SYS(mpx),                    QEMU_SECCOMP_SET_DEFAULT },
    { SCMP_SYS(prof),                   QEMU_SECCOMP_SET_DEFAULT },
    { SCMP_SYS(profil),                 QEMU_SECCOMP_SET_DEFAULT },
    { SCMP_SYS(putpmsg),                QEMU_SECCOMP_SET_DEFAULT },
    { SCMP_SYS(security),               QEMU_SECCOMP_SET_DEFAULT },
    { SCMP_SYS(stty),                   QEMU_SECCOMP_SET_DEFAULT },
    { SCMP_SYS(tuxcall),                QEMU_SECCOMP_SET_DEFAULT },
    { SCMP_SYS(ulimit),                 QEMU_SECCOMP_SET_DEFAULT },
    { SCMP_SYS(vserver),                QEMU_SECCOMP_SET_DEFAULT },
    /* obsolete */
    { SCMP_SYS(readdir),                QEMU_SECCOMP_SET_OBSOLETE },
    { SCMP_SYS(_sysctl),                QEMU_SECCOMP_SET_OBSOLETE },
    { SCMP_SYS(bdflush),                QEMU_SECCOMP_SET_OBSOLETE },
    { SCMP_SYS(create_module),          QEMU_SECCOMP_SET_OBSOLETE },
    { SCMP_SYS(get_kernel_syms),        QEMU_SECCOMP_SET_OBSOLETE },
    { SCMP_SYS(query_module),           QEMU_SECCOMP_SET_OBSOLETE },
    { SCMP_SYS(sgetmask),               QEMU_SECCOMP_SET_OBSOLETE },
    { SCMP_SYS(ssetmask),               QEMU_SECCOMP_SET_OBSOLETE },
    { SCMP_SYS(sysfs),                  QEMU_SECCOMP_SET_OBSOLETE },
    { SCMP_SYS(uselib),                 QEMU_SECCOMP_SET_OBSOLETE },
    { SCMP_SYS(ustat),                  QEMU_SECCOMP_SET_OBSOLETE },
    /* privileged */
    { SCMP_SYS(setuid),                 QEMU_SECCOMP_SET_PRIVILEGED },
    { SCMP_SYS(setgid),                 QEMU_SECCOMP_SET_PRIVILEGED },
    { SCMP_SYS(setpgid),                QEMU_SECCOMP_SET_PRIVILEGED },
    { SCMP_SYS(setsid),                 QEMU_SECCOMP_SET_PRIVILEGED },
    { SCMP_SYS(setreuid),               QEMU_SECCOMP_SET_PRIVILEGED },
    { SCMP_SYS(setregid),               QEMU_SECCOMP_SET_PRIVILEGED },
    { SCMP_SYS(setresuid),              QEMU_SECCOMP_SET_PRIVILEGED },
    { SCMP_SYS(setresgid),              QEMU_SECCOMP_SET_PRIVILEGED },
    { SCMP_SYS(setfsuid),               QEMU_SECCOMP_SET_PRIVILEGED },
    { SCMP_SYS(setfsgid),               QEMU_SECCOMP_SET_PRIVILEGED },
    /* spawn */
    { SCMP_SYS(fork),                   QEMU_SECCOMP_SET_SPAWN },
    { SCMP_SYS(vfork),                  QEMU_SECCOMP_SET_SPAWN },
    { SCMP_SYS(execve),                 QEMU_SECCOMP_SET_SPAWN },
    /* resource control */
    { SCMP_SYS(getpriority),            QEMU_SECCOMP_SET_RESOURCECTL },
    { SCMP_SYS(setpriority),            QEMU_SECCOMP_SET_RESOURCECTL },
    { SCMP_SYS(sched_setparam),         QEMU_SECCOMP_SET_RESOURCECTL },
    { SCMP_SYS(sched_getparam),         QEMU_SECCOMP_SET_RESOURCECTL },
    { SCMP_SYS(sched_setscheduler),     QEMU_SECCOMP_SET_RESOURCECTL },
    { SCMP_SYS(sched_getscheduler),     QEMU_SECCOMP_SET_RESOURCECTL },
    { SCMP_SYS(sched_setaffinity),      QEMU_SECCOMP_SET_RESOURCECTL },
    { SCMP_SYS(sched_getaffinity),      QEMU_SECCOMP_SET_RESOURCECTL },
    { SCMP_SYS(sched_get_priority_max), QEMU_SECCOMP_SET_RESOURCECTL },
    { SCMP_SYS(sched_get_priority_min), QEMU_SECCOMP_SET_RESOURCECTL },
};


int seccomp_start(uint32_t seccomp_opts)
{
    int rc = 0;
    unsigned int i = 0;
    scmp_filter_ctx ctx;

    ctx = seccomp_init(SCMP_ACT_ALLOW);
    if (ctx == NULL) {
        rc = -1;
        goto seccomp_return;
    }

    for (i = 0; i < ARRAY_SIZE(blacklist); i++) {
        if (!(seccomp_opts & blacklist[i].set)) {
            continue;
        }

        rc = seccomp_rule_add(ctx, SCMP_ACT_KILL, blacklist[i].num, 0);
        if (rc < 0) {
            goto seccomp_return;
        }
    }

    rc = seccomp_load(ctx);

  seccomp_return:
    seccomp_release(ctx);
    return rc;
}

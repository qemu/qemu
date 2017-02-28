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
};

int seccomp_start(void)
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

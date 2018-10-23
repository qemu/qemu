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
#include "qapi/error.h"
#include "qemu/config-file.h"
#include "qemu/option.h"
#include "qemu/module.h"
#include <sys/prctl.h>
#include <seccomp.h>
#include "sysemu/seccomp.h"
#include <linux/seccomp.h>

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
    uint8_t narg;
    const struct scmp_arg_cmp *arg_cmp;
};

const struct scmp_arg_cmp sched_setscheduler_arg[] = {
    SCMP_A1(SCMP_CMP_NE, SCHED_IDLE)
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
    { SCMP_SYS(sched_setscheduler),     QEMU_SECCOMP_SET_RESOURCECTL,
      ARRAY_SIZE(sched_setscheduler_arg), sched_setscheduler_arg },
    { SCMP_SYS(sched_getscheduler),     QEMU_SECCOMP_SET_RESOURCECTL },
    { SCMP_SYS(sched_setaffinity),      QEMU_SECCOMP_SET_RESOURCECTL },
    { SCMP_SYS(sched_getaffinity),      QEMU_SECCOMP_SET_RESOURCECTL },
    { SCMP_SYS(sched_get_priority_max), QEMU_SECCOMP_SET_RESOURCECTL },
    { SCMP_SYS(sched_get_priority_min), QEMU_SECCOMP_SET_RESOURCECTL },
};

static inline __attribute__((unused)) int
qemu_seccomp(unsigned int operation, unsigned int flags, void *args)
{
#ifdef __NR_seccomp
    return syscall(__NR_seccomp, operation, flags, args);
#else
    errno = ENOSYS;
    return -1;
#endif
}

static uint32_t qemu_seccomp_get_kill_action(void)
{
#if defined(SECCOMP_GET_ACTION_AVAIL) && defined(SCMP_ACT_KILL_PROCESS) && \
    defined(SECCOMP_RET_KILL_PROCESS)
    {
        uint32_t action = SECCOMP_RET_KILL_PROCESS;

        if (qemu_seccomp(SECCOMP_GET_ACTION_AVAIL, 0, &action) == 0) {
            return SCMP_ACT_KILL_PROCESS;
        }
    }
#endif

    return SCMP_ACT_TRAP;
}


static int seccomp_start(uint32_t seccomp_opts)
{
    int rc = 0;
    unsigned int i = 0;
    scmp_filter_ctx ctx;
    uint32_t action = qemu_seccomp_get_kill_action();

    ctx = seccomp_init(SCMP_ACT_ALLOW);
    if (ctx == NULL) {
        rc = -1;
        goto seccomp_return;
    }

    rc = seccomp_attr_set(ctx, SCMP_FLTATR_CTL_TSYNC, 1);
    if (rc != 0) {
        goto seccomp_return;
    }

    for (i = 0; i < ARRAY_SIZE(blacklist); i++) {
        if (!(seccomp_opts & blacklist[i].set)) {
            continue;
        }

        rc = seccomp_rule_add_array(ctx, action, blacklist[i].num,
                                    blacklist[i].narg, blacklist[i].arg_cmp);
        if (rc < 0) {
            goto seccomp_return;
        }
    }

    rc = seccomp_load(ctx);

  seccomp_return:
    seccomp_release(ctx);
    return rc;
}

#ifdef CONFIG_SECCOMP
int parse_sandbox(void *opaque, QemuOpts *opts, Error **errp)
{
    if (qemu_opt_get_bool(opts, "enable", false)) {
        uint32_t seccomp_opts = QEMU_SECCOMP_SET_DEFAULT
                | QEMU_SECCOMP_SET_OBSOLETE;
        const char *value = NULL;

        value = qemu_opt_get(opts, "obsolete");
        if (value) {
            if (g_str_equal(value, "allow")) {
                seccomp_opts &= ~QEMU_SECCOMP_SET_OBSOLETE;
            } else if (g_str_equal(value, "deny")) {
                /* this is the default option, this if is here
                 * to provide a little bit of consistency for
                 * the command line */
            } else {
                error_setg(errp, "invalid argument for obsolete");
                return -1;
            }
        }

        value = qemu_opt_get(opts, "elevateprivileges");
        if (value) {
            if (g_str_equal(value, "deny")) {
                seccomp_opts |= QEMU_SECCOMP_SET_PRIVILEGED;
            } else if (g_str_equal(value, "children")) {
                seccomp_opts |= QEMU_SECCOMP_SET_PRIVILEGED;

                /* calling prctl directly because we're
                 * not sure if host has CAP_SYS_ADMIN set*/
                if (prctl(PR_SET_NO_NEW_PRIVS, 1)) {
                    error_setg(errp, "failed to set no_new_privs aborting");
                    return -1;
                }
            } else if (g_str_equal(value, "allow")) {
                /* default value */
            } else {
                error_setg(errp, "invalid argument for elevateprivileges");
                return -1;
            }
        }

        value = qemu_opt_get(opts, "spawn");
        if (value) {
            if (g_str_equal(value, "deny")) {
                seccomp_opts |= QEMU_SECCOMP_SET_SPAWN;
            } else if (g_str_equal(value, "allow")) {
                /* default value */
            } else {
                error_setg(errp, "invalid argument for spawn");
                return -1;
            }
        }

        value = qemu_opt_get(opts, "resourcecontrol");
        if (value) {
            if (g_str_equal(value, "deny")) {
                seccomp_opts |= QEMU_SECCOMP_SET_RESOURCECTL;
            } else if (g_str_equal(value, "allow")) {
                /* default value */
            } else {
                error_setg(errp, "invalid argument for resourcecontrol");
                return -1;
            }
        }

        if (seccomp_start(seccomp_opts) < 0) {
            error_setg(errp, "failed to install seccomp syscall filter "
                       "in the kernel");
            return -1;
        }
    }

    return 0;
}

static QemuOptsList qemu_sandbox_opts = {
    .name = "sandbox",
    .implied_opt_name = "enable",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_sandbox_opts.head),
    .desc = {
        {
            .name = "enable",
            .type = QEMU_OPT_BOOL,
        },
        {
            .name = "obsolete",
            .type = QEMU_OPT_STRING,
        },
        {
            .name = "elevateprivileges",
            .type = QEMU_OPT_STRING,
        },
        {
            .name = "spawn",
            .type = QEMU_OPT_STRING,
        },
        {
            .name = "resourcecontrol",
            .type = QEMU_OPT_STRING,
        },
        { /* end of list */ }
    },
};

static void seccomp_register(void)
{
    bool add = false;

    /* FIXME: use seccomp_api_get() >= 2 check when released */

#if defined(SECCOMP_FILTER_FLAG_TSYNC)
    int check;

    /* check host TSYNC capability, it returns errno == ENOSYS if unavailable */
    check = qemu_seccomp(SECCOMP_SET_MODE_FILTER,
                         SECCOMP_FILTER_FLAG_TSYNC, NULL);
    if (check < 0 && errno == EFAULT) {
        add = true;
    }
#endif

    if (add) {
        qemu_add_opts(&qemu_sandbox_opts);
    }
}
opts_init(seccomp_register);
#endif

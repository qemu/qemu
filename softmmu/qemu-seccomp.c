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
    uint32_t action;
};

const struct scmp_arg_cmp sched_setscheduler_arg[] = {
    /* was SCMP_A1(SCMP_CMP_NE, SCHED_IDLE), but expanded due to GCC 4.x bug */
    { .arg = 1, .op = SCMP_CMP_NE, .datum_a = SCHED_IDLE }
};

/*
 * See 'NOTES' in 'man 2 clone' - s390 & cross have 'flags' in
 *  different position to other architectures
 */
#if defined(HOST_S390X) || defined(HOST_S390) || defined(HOST_CRIS)
#define CLONE_FLAGS_ARG 1
#else
#define CLONE_FLAGS_ARG 0
#endif

#ifndef CLONE_PIDFD
# define CLONE_PIDFD 0x00001000
#endif

#define REQUIRE_CLONE_FLAG(flag) \
    const struct scmp_arg_cmp clone_arg ## flag[] = { \
    { .arg = CLONE_FLAGS_ARG, \
      .op = SCMP_CMP_MASKED_EQ, \
      .datum_a = flag, .datum_b = 0 } }

#define FORBID_CLONE_FLAG(flag) \
    const struct scmp_arg_cmp clone_arg ## flag[] = { \
    { .arg = CLONE_FLAGS_ARG, \
      .op = SCMP_CMP_MASKED_EQ, \
      .datum_a = flag, .datum_b = flag } }

#define RULE_CLONE_FLAG(flag) \
    { SCMP_SYS(clone),                  QEMU_SECCOMP_SET_SPAWN, \
      ARRAY_SIZE(clone_arg ## flag), clone_arg ## flag, SCMP_ACT_TRAP }

/* If no CLONE_* flags are set, except CSIGNAL, deny */
const struct scmp_arg_cmp clone_arg_none[] = {
    { .arg = CLONE_FLAGS_ARG,
      .op = SCMP_CMP_MASKED_EQ,
      .datum_a = ~(CSIGNAL), .datum_b = 0 }
};

/*
 * pthread_create should always set all of these.
 */
REQUIRE_CLONE_FLAG(CLONE_VM);
REQUIRE_CLONE_FLAG(CLONE_FS);
REQUIRE_CLONE_FLAG(CLONE_FILES);
REQUIRE_CLONE_FLAG(CLONE_SIGHAND);
REQUIRE_CLONE_FLAG(CLONE_THREAD);
REQUIRE_CLONE_FLAG(CLONE_SYSVSEM);
REQUIRE_CLONE_FLAG(CLONE_SETTLS);
REQUIRE_CLONE_FLAG(CLONE_PARENT_SETTID);
REQUIRE_CLONE_FLAG(CLONE_CHILD_CLEARTID);
/*
 * Musl sets this in pthread_create too, but it is
 * obsolete and harmless since its behaviour is
 * subsumed under CLONE_THREAD
 */
/*REQUIRE_CLONE_FLAG(CLONE_DETACHED);*/


/*
 * These all indicate an attempt to spawn a process
 * instead of a thread, or other undesirable scenarios
 */
FORBID_CLONE_FLAG(CLONE_PIDFD);
FORBID_CLONE_FLAG(CLONE_PTRACE);
FORBID_CLONE_FLAG(CLONE_VFORK);
FORBID_CLONE_FLAG(CLONE_PARENT);
FORBID_CLONE_FLAG(CLONE_NEWNS);
FORBID_CLONE_FLAG(CLONE_UNTRACED);
FORBID_CLONE_FLAG(CLONE_NEWCGROUP);
FORBID_CLONE_FLAG(CLONE_NEWUTS);
FORBID_CLONE_FLAG(CLONE_NEWIPC);
FORBID_CLONE_FLAG(CLONE_NEWUSER);
FORBID_CLONE_FLAG(CLONE_NEWPID);
FORBID_CLONE_FLAG(CLONE_NEWNET);
FORBID_CLONE_FLAG(CLONE_IO);


static const struct QemuSeccompSyscall denylist[] = {
    /* default set of syscalls that should get blocked */
    { SCMP_SYS(reboot),                 QEMU_SECCOMP_SET_DEFAULT,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(swapon),                 QEMU_SECCOMP_SET_DEFAULT,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(swapoff),                QEMU_SECCOMP_SET_DEFAULT,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(syslog),                 QEMU_SECCOMP_SET_DEFAULT,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(mount),                  QEMU_SECCOMP_SET_DEFAULT,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(umount),                 QEMU_SECCOMP_SET_DEFAULT,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(kexec_load),             QEMU_SECCOMP_SET_DEFAULT,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(afs_syscall),            QEMU_SECCOMP_SET_DEFAULT,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(break),                  QEMU_SECCOMP_SET_DEFAULT,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(ftime),                  QEMU_SECCOMP_SET_DEFAULT,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(getpmsg),                QEMU_SECCOMP_SET_DEFAULT,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(gtty),                   QEMU_SECCOMP_SET_DEFAULT,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(lock),                   QEMU_SECCOMP_SET_DEFAULT,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(mpx),                    QEMU_SECCOMP_SET_DEFAULT,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(prof),                   QEMU_SECCOMP_SET_DEFAULT,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(profil),                 QEMU_SECCOMP_SET_DEFAULT,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(putpmsg),                QEMU_SECCOMP_SET_DEFAULT,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(security),               QEMU_SECCOMP_SET_DEFAULT,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(stty),                   QEMU_SECCOMP_SET_DEFAULT,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(tuxcall),                QEMU_SECCOMP_SET_DEFAULT,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(ulimit),                 QEMU_SECCOMP_SET_DEFAULT,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(vserver),                QEMU_SECCOMP_SET_DEFAULT,
      0, NULL, SCMP_ACT_TRAP },
    /* obsolete */
    { SCMP_SYS(readdir),                QEMU_SECCOMP_SET_OBSOLETE,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(_sysctl),                QEMU_SECCOMP_SET_OBSOLETE,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(bdflush),                QEMU_SECCOMP_SET_OBSOLETE,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(create_module),          QEMU_SECCOMP_SET_OBSOLETE,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(get_kernel_syms),        QEMU_SECCOMP_SET_OBSOLETE,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(query_module),           QEMU_SECCOMP_SET_OBSOLETE,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(sgetmask),               QEMU_SECCOMP_SET_OBSOLETE,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(ssetmask),               QEMU_SECCOMP_SET_OBSOLETE,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(sysfs),                  QEMU_SECCOMP_SET_OBSOLETE,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(uselib),                 QEMU_SECCOMP_SET_OBSOLETE,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(ustat),                  QEMU_SECCOMP_SET_OBSOLETE,
      0, NULL, SCMP_ACT_TRAP },
    /* privileged */
    { SCMP_SYS(setuid),                 QEMU_SECCOMP_SET_PRIVILEGED,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(setgid),                 QEMU_SECCOMP_SET_PRIVILEGED,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(setpgid),                QEMU_SECCOMP_SET_PRIVILEGED,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(setsid),                 QEMU_SECCOMP_SET_PRIVILEGED,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(setreuid),               QEMU_SECCOMP_SET_PRIVILEGED,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(setregid),               QEMU_SECCOMP_SET_PRIVILEGED,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(setresuid),              QEMU_SECCOMP_SET_PRIVILEGED,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(setresgid),              QEMU_SECCOMP_SET_PRIVILEGED,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(setfsuid),               QEMU_SECCOMP_SET_PRIVILEGED,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(setfsgid),               QEMU_SECCOMP_SET_PRIVILEGED,
      0, NULL, SCMP_ACT_TRAP },
    /* spawn */
    { SCMP_SYS(fork),                   QEMU_SECCOMP_SET_SPAWN,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(vfork),                  QEMU_SECCOMP_SET_SPAWN,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(execve),                 QEMU_SECCOMP_SET_SPAWN,
      0, NULL, SCMP_ACT_TRAP },
    { SCMP_SYS(clone),                  QEMU_SECCOMP_SET_SPAWN,
      ARRAY_SIZE(clone_arg_none), clone_arg_none, SCMP_ACT_TRAP },
    RULE_CLONE_FLAG(CLONE_VM),
    RULE_CLONE_FLAG(CLONE_FS),
    RULE_CLONE_FLAG(CLONE_FILES),
    RULE_CLONE_FLAG(CLONE_SIGHAND),
    RULE_CLONE_FLAG(CLONE_THREAD),
    RULE_CLONE_FLAG(CLONE_SYSVSEM),
    RULE_CLONE_FLAG(CLONE_SETTLS),
    RULE_CLONE_FLAG(CLONE_PARENT_SETTID),
    RULE_CLONE_FLAG(CLONE_CHILD_CLEARTID),
    /*RULE_CLONE_FLAG(CLONE_DETACHED),*/
    RULE_CLONE_FLAG(CLONE_PIDFD),
    RULE_CLONE_FLAG(CLONE_PTRACE),
    RULE_CLONE_FLAG(CLONE_VFORK),
    RULE_CLONE_FLAG(CLONE_PARENT),
    RULE_CLONE_FLAG(CLONE_NEWNS),
    RULE_CLONE_FLAG(CLONE_UNTRACED),
    RULE_CLONE_FLAG(CLONE_NEWCGROUP),
    RULE_CLONE_FLAG(CLONE_NEWUTS),
    RULE_CLONE_FLAG(CLONE_NEWIPC),
    RULE_CLONE_FLAG(CLONE_NEWUSER),
    RULE_CLONE_FLAG(CLONE_NEWPID),
    RULE_CLONE_FLAG(CLONE_NEWNET),
    RULE_CLONE_FLAG(CLONE_IO),
#ifdef __SNR_clone3
    { SCMP_SYS(clone3),                 QEMU_SECCOMP_SET_SPAWN,
      0, NULL, SCMP_ACT_ERRNO(ENOSYS) },
#endif
#ifdef __SNR_execveat
    { SCMP_SYS(execveat),               QEMU_SECCOMP_SET_SPAWN },
#endif
    { SCMP_SYS(setns),                  QEMU_SECCOMP_SET_SPAWN },
    { SCMP_SYS(unshare),                QEMU_SECCOMP_SET_SPAWN },
    /* resource control */
    { SCMP_SYS(setpriority),            QEMU_SECCOMP_SET_RESOURCECTL,
      0, NULL, SCMP_ACT_ERRNO(EPERM) },
    { SCMP_SYS(sched_setparam),         QEMU_SECCOMP_SET_RESOURCECTL,
      0, NULL, SCMP_ACT_ERRNO(EPERM) },
    { SCMP_SYS(sched_setscheduler),     QEMU_SECCOMP_SET_RESOURCECTL,
      ARRAY_SIZE(sched_setscheduler_arg), sched_setscheduler_arg,
      SCMP_ACT_ERRNO(EPERM) },
    { SCMP_SYS(sched_setaffinity),      QEMU_SECCOMP_SET_RESOURCECTL,
      0, NULL, SCMP_ACT_ERRNO(EPERM) },
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

static uint32_t qemu_seccomp_update_action(uint32_t action)
{
#if defined(SECCOMP_GET_ACTION_AVAIL) && defined(SCMP_ACT_KILL_PROCESS) && \
    defined(SECCOMP_RET_KILL_PROCESS)
    if (action == SCMP_ACT_TRAP) {
        static int kill_process = -1;
        if (kill_process == -1) {
            uint32_t action = SECCOMP_RET_KILL_PROCESS;

            if (qemu_seccomp(SECCOMP_GET_ACTION_AVAIL, 0, &action) == 0) {
                kill_process = 1;
            } else {
                kill_process = 0;
            }
        }
        if (kill_process == 1) {
            return SCMP_ACT_KILL_PROCESS;
        }
    }
#endif
    return action;
}


static int seccomp_start(uint32_t seccomp_opts, Error **errp)
{
    int rc = -1;
    unsigned int i = 0;
    scmp_filter_ctx ctx;

    ctx = seccomp_init(SCMP_ACT_ALLOW);
    if (ctx == NULL) {
        error_setg(errp, "failed to initialize seccomp context");
        goto seccomp_return;
    }

#if defined(CONFIG_SECCOMP_SYSRAWRC)
    /*
     * This must be the first seccomp_attr_set() call to have full
     * error propagation from subsequent seccomp APIs.
     */
    rc = seccomp_attr_set(ctx, SCMP_FLTATR_API_SYSRAWRC, 1);
    if (rc != 0) {
        error_setg_errno(errp, -rc,
                         "failed to set seccomp rawrc attribute");
        goto seccomp_return;
    }
#endif

    rc = seccomp_attr_set(ctx, SCMP_FLTATR_CTL_TSYNC, 1);
    if (rc != 0) {
        error_setg_errno(errp, -rc,
                         "failed to set seccomp thread synchronization");
        goto seccomp_return;
    }

    for (i = 0; i < ARRAY_SIZE(denylist); i++) {
        uint32_t action;
        if (!(seccomp_opts & denylist[i].set)) {
            continue;
        }

        action = qemu_seccomp_update_action(denylist[i].action);
        rc = seccomp_rule_add_array(ctx, action, denylist[i].num,
                                    denylist[i].narg, denylist[i].arg_cmp);
        if (rc < 0) {
            error_setg_errno(errp, -rc,
                             "failed to add seccomp denylist rules");
            goto seccomp_return;
        }
    }

    rc = seccomp_load(ctx);
    if (rc < 0) {
        error_setg_errno(errp, -rc,
                         "failed to load seccomp syscall filter in kernel");
    }

  seccomp_return:
    seccomp_release(ctx);
    return rc < 0 ? -1 : 0;
}

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

        if (seccomp_start(seccomp_opts, errp) < 0) {
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

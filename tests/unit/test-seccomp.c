/*
 * QEMU seccomp test suite
 *
 * Copyright (c) 2021 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "qemu/config-file.h"
#include "qemu/option.h"
#include "sysemu/seccomp.h"
#include "qapi/error.h"
#include "qemu/module.h"

#include <unistd.h>
#include <sys/syscall.h>

static void test_seccomp_helper(const char *args, bool killed,
                                int errnum, int (*doit)(void))
{
    if (g_test_subprocess()) {
        QemuOptsList *olist;
        QemuOpts *opts;
        int ret;

        module_call_init(MODULE_INIT_OPTS);
        olist = qemu_find_opts("sandbox");
        g_assert(olist != NULL);

        opts = qemu_opts_parse_noisily(olist, args, true);
        g_assert(opts != NULL);

        parse_sandbox(NULL, opts, &error_abort);

        /* Running in a child process */
        ret = doit();

        if (errnum != 0) {
            g_assert(ret != 0);
            g_assert(errno == errnum);
        } else {
            g_assert(ret == 0);
        }

        _exit(0);
    } else {
        /* Running in main test process, spawning the child */
        g_test_trap_subprocess(NULL, 0, 0);
        if (killed) {
            g_test_trap_assert_failed();
        } else {
            g_test_trap_assert_passed();
        }
    }
}


static void test_seccomp_killed(const char *args, int (*doit)(void))
{
    test_seccomp_helper(args, true, 0, doit);
}

static void test_seccomp_errno(const char *args, int errnum, int (*doit)(void))
{
    test_seccomp_helper(args, false, errnum, doit);
}

static void test_seccomp_passed(const char *args, int (*doit)(void))
{
    test_seccomp_helper(args, false, 0, doit);
}

#ifdef SYS_fork
static int doit_sys_fork(void)
{
    int ret = syscall(SYS_fork);
    if (ret < 0) {
        return ret;
    }
    if (ret == 0) {
        _exit(0);
    }
    return 0;
}

static void test_seccomp_sys_fork_on_nospawn(void)
{
    test_seccomp_killed("on,spawn=deny", doit_sys_fork);
}

static void test_seccomp_sys_fork_on(void)
{
    test_seccomp_passed("on", doit_sys_fork);
}

static void test_seccomp_sys_fork_off(void)
{
    test_seccomp_passed("off", doit_sys_fork);
}
#endif

static int doit_fork(void)
{
    int ret = fork();
    if (ret < 0) {
        return ret;
    }
    if (ret == 0) {
        _exit(0);
    }
    return 0;
}

static void test_seccomp_fork_on_nospawn(void)
{
    test_seccomp_killed("on,spawn=deny", doit_fork);
}

static void test_seccomp_fork_on(void)
{
    test_seccomp_passed("on", doit_fork);
}

static void test_seccomp_fork_off(void)
{
    test_seccomp_passed("off", doit_fork);
}

static void *noop(void *arg)
{
    return arg;
}

static int doit_thread(void)
{
    pthread_t th;
    int ret = pthread_create(&th, NULL, noop, NULL);
    if (ret != 0) {
        errno = ret;
        return -1;
    } else {
        pthread_join(th, NULL);
        return 0;
    }
}

static void test_seccomp_thread_on(void)
{
    test_seccomp_passed("on", doit_thread);
}

static void test_seccomp_thread_on_nospawn(void)
{
    test_seccomp_passed("on,spawn=deny", doit_thread);
}

static void test_seccomp_thread_off(void)
{
    test_seccomp_passed("off", doit_thread);
}

static int doit_sched(void)
{
    struct sched_param param = { .sched_priority = 0 };
    return sched_setscheduler(getpid(), SCHED_OTHER, &param);
}

static void test_seccomp_sched_on_nores(void)
{
    test_seccomp_errno("on,resourcecontrol=deny", EPERM, doit_sched);
}

static void test_seccomp_sched_on(void)
{
    test_seccomp_passed("on", doit_sched);
}

static void test_seccomp_sched_off(void)
{
    test_seccomp_passed("off", doit_sched);
}

static bool can_play_with_seccomp(void)
{
    g_autofree char *status = NULL;
    g_auto(GStrv) lines = NULL;
    size_t i;

    if (!g_file_get_contents("/proc/self/status", &status, NULL, NULL)) {
        return false;
    }

    lines = g_strsplit(status, "\n", 0);

    for (i = 0; lines[i] != NULL; i++) {
        if (g_str_has_prefix(lines[i], "Seccomp:")) {
            /*
             * "Seccomp: 1" or "Seccomp: 2" indicate we're already
             * confined, probably as we're inside a container. In
             * this case our tests might get unexpected results,
             * so we can't run reliably
             */
            if (!strchr(lines[i], '0')) {
                return false;
            }

            return true;
        }
    }

    /* Doesn't look like seccomp is enabled in the kernel */
    return false;
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    if (can_play_with_seccomp()) {
#ifdef SYS_fork
        g_test_add_func("/softmmu/seccomp/sys-fork/on",
                        test_seccomp_sys_fork_on);
        g_test_add_func("/softmmu/seccomp/sys-fork/on-nospawn",
                        test_seccomp_sys_fork_on_nospawn);
        g_test_add_func("/softmmu/seccomp/sys-fork/off",
                        test_seccomp_sys_fork_off);
#endif

        g_test_add_func("/softmmu/seccomp/fork/on",
                        test_seccomp_fork_on);
        g_test_add_func("/softmmu/seccomp/fork/on-nospawn",
                        test_seccomp_fork_on_nospawn);
        g_test_add_func("/softmmu/seccomp/fork/off",
                        test_seccomp_fork_off);

        g_test_add_func("/softmmu/seccomp/thread/on",
                        test_seccomp_thread_on);
        g_test_add_func("/softmmu/seccomp/thread/on-nospawn",
                        test_seccomp_thread_on_nospawn);
        g_test_add_func("/softmmu/seccomp/thread/off",
                        test_seccomp_thread_off);

        if (doit_sched() == 0) {
            /*
             * musl doesn't impl sched_setscheduler, hence
             * we check above if it works first
             */
            g_test_add_func("/softmmu/seccomp/sched/on",
                            test_seccomp_sched_on);
            g_test_add_func("/softmmu/seccomp/sched/on-nores",
                            test_seccomp_sched_on_nores);
            g_test_add_func("/softmmu/seccomp/sched/off",
                            test_seccomp_sched_off);
        }
    }
    return g_test_run();
}

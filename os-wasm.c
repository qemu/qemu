/* SPDX-License-Identifier: MIT */
/*
 * os-wasm.c
 * Forked from os-posix.c, removing functions not working on Emscripten
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2010 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include <sys/resource.h>
#include <sys/wait.h>
#include <pwd.h>
#include <grp.h>
#include <libgen.h>

#include "qemu/error-report.h"
#include "qemu/log.h"
#include "system/runstate.h"
#include "qemu/cutils.h"

void os_setup_post(void){}
void os_set_line_buffering(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
}
void os_setup_early_signal_handling(void)
{
    struct sigaction act;
    sigfillset(&act.sa_mask);
    act.sa_flags = 0;
    act.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &act, NULL);
}
void os_set_proc_name(const char *s)
{
    error_report("Change of process name not supported by your OS");
    exit(1);
}
static void termsig_handler(int signal, siginfo_t *info, void *c)
{
    qemu_system_killed(info->si_signo, info->si_pid);
}

void os_setup_signal_handling(void)
{
    struct sigaction act;

    memset(&act, 0, sizeof(act));
    act.sa_sigaction = termsig_handler;
    act.sa_flags = SA_SIGINFO;
    sigaction(SIGINT,  &act, NULL);
    sigaction(SIGHUP,  &act, NULL);
    sigaction(SIGTERM, &act, NULL);
}
void os_setup_limits(void)
{
    struct rlimit nofile;

    if (getrlimit(RLIMIT_NOFILE, &nofile) < 0) {
        warn_report("unable to query NOFILE limit: %s", strerror(errno));
        return;
    }

    if (nofile.rlim_cur == nofile.rlim_max) {
        return;
    }

    nofile.rlim_cur = nofile.rlim_max;

    if (setrlimit(RLIMIT_NOFILE, &nofile) < 0) {
        warn_report("unable to set NOFILE limit: %s", strerror(errno));
        return;
    }
}
int os_mlock(bool on_fault)
{
#ifdef HAVE_MLOCKALL
    int ret = 0;
    int flags = MCL_CURRENT | MCL_FUTURE;

    if (on_fault) {
#ifdef HAVE_MLOCK_ONFAULT
        flags |= MCL_ONFAULT;
#else
        error_report("mlockall: on_fault not supported");
        return -EINVAL;
#endif
    }

    ret = mlockall(flags);
    if (ret < 0) {
        error_report("mlockall: %s", strerror(errno));
    }

    return ret;
#else
    (void)on_fault;
    return -ENOSYS;
#endif
}

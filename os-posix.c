/*
 * os-posix.c
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

#ifdef CONFIG_LINUX
#include <sys/prctl.h>
#endif


void os_setup_early_signal_handling(void)
{
    struct sigaction act;
    sigfillset(&act.sa_mask);
    act.sa_flags = 0;
    act.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &act, NULL);
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

void os_set_proc_name(const char *s)
{
#if defined(PR_SET_NAME)
    char name[16];
    if (!s)
        return;
    pstrcpy(name, sizeof(name), s);
    /* Could rewrite argv[0] too, but that's a bit more complicated.
       This simple way is enough for `top'. */
    if (prctl(PR_SET_NAME, name)) {
        error_report("unable to change process name: %s", strerror(errno));
        exit(1);
    }
#else
    error_report("Change of process name not supported by your OS");
    exit(1);
#endif
}


/*
 * Must set all three of these at once.
 * Legal combinations are              unset   by name   by uid
 */
static struct passwd *user_pwd;    /*   NULL   non-NULL   NULL   */
static uid_t user_uid = (uid_t)-1; /*   -1      -1        >=0    */
static gid_t user_gid = (gid_t)-1; /*   -1      -1        >=0    */

/*
 * Prepare to change user ID. user_id can be one of 3 forms:
 *   - a username, in which case user ID will be changed to its uid,
 *     with primary and supplementary groups set up too;
 *   - a numeric uid, in which case only the uid will be set;
 *   - a pair of numeric uid:gid.
 */
bool os_set_runas(const char *user_id)
{
    unsigned long lv;
    const char *ep;
    uid_t got_uid;
    gid_t got_gid;
    int rc;

    user_pwd = getpwnam(user_id);
    if (user_pwd) {
        user_uid = -1;
        user_gid = -1;
        return true;
    }

    rc = qemu_strtoul(user_id, &ep, 0, &lv);
    got_uid = lv; /* overflow here is ID in C99 */
    if (rc || *ep != ':' || got_uid != lv || got_uid == (uid_t)-1) {
        return false;
    }

    rc = qemu_strtoul(ep + 1, 0, 0, &lv);
    got_gid = lv; /* overflow here is ID in C99 */
    if (rc || got_gid != lv || got_gid == (gid_t)-1) {
        return false;
    }

    user_pwd = NULL;
    user_uid = got_uid;
    user_gid = got_gid;
    return true;
}

static void change_process_uid(void)
{
    assert((user_uid == (uid_t)-1) || user_pwd == NULL);
    assert((user_uid == (uid_t)-1) ==
           (user_gid == (gid_t)-1));

    if (user_pwd || user_uid != (uid_t)-1) {
        gid_t intended_gid = user_pwd ? user_pwd->pw_gid : user_gid;
        uid_t intended_uid = user_pwd ? user_pwd->pw_uid : user_uid;
        if (setgid(intended_gid) < 0) {
            error_report("Failed to setgid(%d)", intended_gid);
            exit(1);
        }
        if (user_pwd) {
            if (initgroups(user_pwd->pw_name, user_pwd->pw_gid) < 0) {
                error_report("Failed to initgroups(\"%s\", %d)",
                        user_pwd->pw_name, user_pwd->pw_gid);
                exit(1);
            }
        } else {
            if (setgroups(1, &user_gid) < 0) {
                error_report("Failed to setgroups(1, [%d])",
                        user_gid);
                exit(1);
            }
        }
        if (setuid(intended_uid) < 0) {
            error_report("Failed to setuid(%d)", intended_uid);
            exit(1);
        }
        if (setuid(0) != -1) {
            error_report("Dropping privileges failed");
            exit(1);
        }
    }
}


static const char *chroot_dir;

void os_set_chroot(const char *path)
{
    chroot_dir = path;
}

static void change_root(void)
{
    if (chroot_dir) {
        if (chroot(chroot_dir) < 0) {
            error_report("chroot failed");
            exit(1);
        }
        if (chdir("/")) {
            error_report("not able to chdir to /: %s", strerror(errno));
            exit(1);
        }
    }

}


static int daemonize;
static int daemon_pipe;

bool is_daemonized(void)
{
    return daemonize;
}

int os_set_daemonize(bool d)
{
    daemonize = d;
    return 0;
}

void os_daemonize(void)
{
    if (daemonize) {
        pid_t pid;
        int fds[2];

        if (!g_unix_open_pipe(fds, FD_CLOEXEC, NULL)) {
            exit(1);
        }

        pid = fork();
        if (pid > 0) {
            uint8_t status;
            ssize_t len;

            close(fds[1]);

            do {
                len = read(fds[0], &status, 1);
            } while (len < 0 && errno == EINTR);

            /* only exit successfully if our child actually wrote
             * a one-byte zero to our pipe, upon successful init */
            exit(len == 1 && status == 0 ? 0 : 1);

        } else if (pid < 0) {
            exit(1);
        }

        close(fds[0]);
        daemon_pipe = fds[1];

        setsid();

        pid = fork();
        if (pid > 0) {
            exit(0);
        } else if (pid < 0) {
            exit(1);
        }
        umask(027);

        signal(SIGTSTP, SIG_IGN);
        signal(SIGTTOU, SIG_IGN);
        signal(SIGTTIN, SIG_IGN);
    }
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

#ifdef CONFIG_DARWIN
    nofile.rlim_cur = OPEN_MAX < nofile.rlim_max ? OPEN_MAX : nofile.rlim_max;
#else
    nofile.rlim_cur = nofile.rlim_max;
#endif

    if (setrlimit(RLIMIT_NOFILE, &nofile) < 0) {
        warn_report("unable to set NOFILE limit: %s", strerror(errno));
        return;
    }
}

void os_setup_post(void)
{
    int fd = 0;

    if (daemonize) {
        if (chdir("/")) {
            error_report("not able to chdir to /: %s", strerror(errno));
            exit(1);
        }
        fd = RETRY_ON_EINTR(qemu_open_old("/dev/null", O_RDWR));
        if (fd == -1) {
            exit(1);
        }
    }

    change_root();
    change_process_uid();

    if (daemonize) {
        uint8_t status = 0;
        ssize_t len;

        dup2(fd, 0);
        dup2(fd, 1);
        /* In case -D is given do not redirect stderr to /dev/null */
        if (!qemu_log_enabled()) {
            dup2(fd, 2);
        }

        close(fd);

        do {        
            len = write(daemon_pipe, &status, 1);
        } while (len < 0 && errno == EINTR);
        if (len != 1) {
            exit(1);
        }
    }
}

void os_set_line_buffering(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
}

int os_mlock(void)
{
#ifdef HAVE_MLOCKALL
    int ret = 0;

    ret = mlockall(MCL_CURRENT | MCL_FUTURE);
    if (ret < 0) {
        error_report("mlockall: %s", strerror(errno));
    }

    return ret;
#else
    return -ENOSYS;
#endif
}

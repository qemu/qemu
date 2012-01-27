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

#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
/*needed for MAP_POPULATE before including qemu-options.h */
#include <sys/mman.h>
#include <pwd.h>
#include <grp.h>
#include <libgen.h>

/* Needed early for CONFIG_BSD etc. */
#include "config-host.h"
#include "sysemu.h"
#include "net/slirp.h"
#include "qemu-options.h"

#ifdef CONFIG_LINUX
#include <sys/prctl.h>
#endif

static struct passwd *user_pwd;
static const char *chroot_dir;
static int daemonize;
static int fds[2];

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

/* Find a likely location for support files using the location of the binary.
   For installed binaries this will be "$bindir/../share/qemu".  When
   running from the build tree this will be "$bindir/../pc-bios".  */
#define SHARE_SUFFIX "/share/qemu"
#define BUILD_SUFFIX "/pc-bios"
char *os_find_datadir(const char *argv0)
{
    char *dir;
    char *p = NULL;
    char *res;
    char buf[PATH_MAX];
    size_t max_len;

#if defined(__linux__)
    {
        int len;
        len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (len > 0) {
            buf[len] = 0;
            p = buf;
        }
    }
#elif defined(__FreeBSD__)
    {
        static int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1};
        size_t len = sizeof(buf) - 1;

        *buf = '\0';
        if (!sysctl(mib, ARRAY_SIZE(mib), buf, &len, NULL, 0) &&
            *buf) {
            buf[sizeof(buf) - 1] = '\0';
            p = buf;
        }
    }
#endif
    /* If we don't have any way of figuring out the actual executable
       location then try argv[0].  */
    if (!p) {
        p = realpath(argv0, buf);
        if (!p) {
            return NULL;
        }
    }
    dir = dirname(p);
    dir = dirname(dir);

    max_len = strlen(dir) +
        MAX(strlen(SHARE_SUFFIX), strlen(BUILD_SUFFIX)) + 1;
    res = g_malloc0(max_len);
    snprintf(res, max_len, "%s%s", dir, SHARE_SUFFIX);
    if (access(res, R_OK)) {
        snprintf(res, max_len, "%s%s", dir, BUILD_SUFFIX);
        if (access(res, R_OK)) {
            g_free(res);
            res = NULL;
        }
    }

    return res;
}
#undef SHARE_SUFFIX
#undef BUILD_SUFFIX

void os_set_proc_name(const char *s)
{
#if defined(PR_SET_NAME)
    char name[16];
    if (!s)
        return;
    name[sizeof(name) - 1] = 0;
    strncpy(name, s, sizeof(name));
    /* Could rewrite argv[0] too, but that's a bit more complicated.
       This simple way is enough for `top'. */
    if (prctl(PR_SET_NAME, name)) {
        perror("unable to change process name");
        exit(1);
    }
#else
    fprintf(stderr, "Change of process name not supported by your OS\n");
    exit(1);
#endif
}

/*
 * Parse OS specific command line options.
 * return 0 if option handled, -1 otherwise
 */
void os_parse_cmd_args(int index, const char *optarg)
{
    switch (index) {
#ifdef CONFIG_SLIRP
    case QEMU_OPTION_smb:
        if (net_slirp_smb(optarg) < 0)
            exit(1);
        break;
#endif
    case QEMU_OPTION_runas:
        user_pwd = getpwnam(optarg);
        if (!user_pwd) {
            fprintf(stderr, "User \"%s\" doesn't exist\n", optarg);
            exit(1);
        }
        break;
    case QEMU_OPTION_chroot:
        chroot_dir = optarg;
        break;
    case QEMU_OPTION_daemonize:
        daemonize = 1;
        break;
    }
    return;
}

static void change_process_uid(void)
{
    if (user_pwd) {
        if (setgid(user_pwd->pw_gid) < 0) {
            fprintf(stderr, "Failed to setgid(%d)\n", user_pwd->pw_gid);
            exit(1);
        }
        if (initgroups(user_pwd->pw_name, user_pwd->pw_gid) < 0) {
            fprintf(stderr, "Failed to initgroups(\"%s\", %d)\n",
                    user_pwd->pw_name, user_pwd->pw_gid);
            exit(1);
        }
        if (setuid(user_pwd->pw_uid) < 0) {
            fprintf(stderr, "Failed to setuid(%d)\n", user_pwd->pw_uid);
            exit(1);
        }
        if (setuid(0) != -1) {
            fprintf(stderr, "Dropping privileges failed\n");
            exit(1);
        }
    }
}

static void change_root(void)
{
    if (chroot_dir) {
        if (chroot(chroot_dir) < 0) {
            fprintf(stderr, "chroot failed\n");
            exit(1);
        }
        if (chdir("/")) {
            perror("not able to chdir to /");
            exit(1);
        }
    }

}

void os_daemonize(void)
{
    if (daemonize) {
	pid_t pid;

	if (pipe(fds) == -1)
	    exit(1);

	pid = fork();
	if (pid > 0) {
	    uint8_t status;
	    ssize_t len;

	    close(fds[1]);

	again:
            len = read(fds[0], &status, 1);
            if (len == -1 && (errno == EINTR))
                goto again;

            if (len != 1)
                exit(1);
            else if (status == 1) {
                fprintf(stderr, "Could not acquire pidfile: %s\n", strerror(errno));
                exit(1);
            } else
                exit(0);
	} else if (pid < 0)
            exit(1);

	close(fds[0]);
	qemu_set_cloexec(fds[1]);

	setsid();

	pid = fork();
	if (pid > 0)
	    exit(0);
	else if (pid < 0)
	    exit(1);

	umask(027);

        signal(SIGTSTP, SIG_IGN);
        signal(SIGTTOU, SIG_IGN);
        signal(SIGTTIN, SIG_IGN);
    }
}

void os_setup_post(void)
{
    int fd = 0;

    if (daemonize) {
	uint8_t status = 0;
	ssize_t len;

    again1:
	len = write(fds[1], &status, 1);
	if (len == -1 && (errno == EINTR))
	    goto again1;

	if (len != 1)
	    exit(1);

        if (chdir("/")) {
            perror("not able to chdir to /");
            exit(1);
        }
	TFR(fd = qemu_open("/dev/null", O_RDWR));
	if (fd == -1)
	    exit(1);
    }

    change_root();
    change_process_uid();

    if (daemonize) {
        dup2(fd, 0);
        dup2(fd, 1);
        dup2(fd, 2);

        close(fd);
    }
}

void os_pidfile_error(void)
{
    if (daemonize) {
        uint8_t status = 1;
        if (write(fds[1], &status, 1) != 1) {
            perror("daemonize. Writing to pipe\n");
        }
    } else
        fprintf(stderr, "Could not acquire pid file: %s\n", strerror(errno));
}

void os_set_line_buffering(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
}

int qemu_create_pidfile(const char *filename)
{
    char buffer[128];
    int len;
    int fd;

    fd = qemu_open(filename, O_RDWR | O_CREAT, 0600);
    if (fd == -1) {
        return -1;
    }
    if (lockf(fd, F_TLOCK, 0) == -1) {
        close(fd);
        return -1;
    }
    len = snprintf(buffer, sizeof(buffer), FMT_pid "\n", getpid());
    if (write(fd, buffer, len) != len) {
        close(fd);
        return -1;
    }

    /* keep pidfile open & locked forever */
    return 0;
}

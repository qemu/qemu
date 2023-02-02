/*
 * Asynchronous teardown
 *
 * Copyright IBM, Corp. 2022
 *
 * Authors:
 *  Claudio Imbrenda <imbrenda@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at your
 * option) any later version.  See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include <dirent.h>
#include <sys/prctl.h>
#include <sched.h>

#include "qemu/async-teardown.h"

#ifdef _SC_THREAD_STACK_MIN
#define CLONE_STACK_SIZE sysconf(_SC_THREAD_STACK_MIN)
#else
#define CLONE_STACK_SIZE 16384
#endif

static pid_t the_ppid;

/*
 * Close all open file descriptors.
 */
static void close_all_open_fd(void)
{
    struct dirent *de;
    int fd, dfd;
    DIR *dir;

#ifdef CONFIG_CLOSE_RANGE
    int r = close_range(0, ~0U, 0);
    if (!r) {
        /* Success, no need to try other ways. */
        return;
    }
#endif

    dir = opendir("/proc/self/fd");
    if (!dir) {
        /* If /proc is not mounted, there is nothing that can be done. */
        return;
    }
    /* Avoid closing the directory. */
    dfd = dirfd(dir);

    for (de = readdir(dir); de; de = readdir(dir)) {
        fd = atoi(de->d_name);
        if (fd != dfd) {
            close(fd);
        }
    }
    closedir(dir);
}

static void hup_handler(int signal)
{
    /* Check every second if this process has been reparented. */
    while (the_ppid == getppid()) {
        /* sleep() is safe to use in a signal handler. */
        sleep(1);
    }

    /* At this point the parent process has terminated completely. */
    _exit(0);
}

static int async_teardown_fn(void *arg)
{
    struct sigaction sa = { .sa_handler = hup_handler };
    sigset_t hup_signal;
    char name[16];

    /* Set a meaningful name for this process. */
    snprintf(name, 16, "cleanup/%d", the_ppid);
    prctl(PR_SET_NAME, (unsigned long)name);

    /*
     * Close all file descriptors that might have been inherited from the
     * main qemu process when doing clone, needed to make libvirt happy.
     * Not using close_range for increased compatibility with older kernels.
     */
    close_all_open_fd();

    /* Set up a handler for SIGHUP and unblock SIGHUP. */
    sigaction(SIGHUP, &sa, NULL);
    sigemptyset(&hup_signal);
    sigaddset(&hup_signal, SIGHUP);
    sigprocmask(SIG_UNBLOCK, &hup_signal, NULL);

    /* Ask to receive SIGHUP when the parent dies. */
    prctl(PR_SET_PDEATHSIG, SIGHUP);

    /*
     * Sleep forever, unless the parent process has already terminated. The
     * only interruption can come from the SIGHUP signal, which in normal
     * operation is received when the parent process dies.
     */
    if (the_ppid == getppid()) {
        pause();
    }

    /* At this point the parent process has terminated completely. */
    _exit(0);
}

/*
 * Allocate a new stack of a reasonable size, and return a pointer to its top.
 */
static void *new_stack_for_clone(void)
{
    size_t stack_size = CLONE_STACK_SIZE;
    char *stack_ptr;

    /* Allocate a new stack and get a pointer to its top. */
    stack_ptr = qemu_alloc_stack(&stack_size);
#if !defined(HOST_HPPA)
    /* The top is at the end of the area, except on HPPA. */
    stack_ptr += stack_size;
#endif

    return stack_ptr;
}

/*
 * Block all signals, start (clone) a new process sharing the address space
 * with qemu (CLONE_VM), then restore signals.
 */
void init_async_teardown(void)
{
    sigset_t all_signals, old_signals;

    the_ppid = getpid();

    sigfillset(&all_signals);
    sigprocmask(SIG_BLOCK, &all_signals, &old_signals);
    clone(async_teardown_fn, new_stack_for_clone(), CLONE_VM, NULL);
    sigprocmask(SIG_SETMASK, &old_signals, NULL);
}

/*
 * FUSE: Filesystem in Userspace
 * Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
 *
 * Utility functions for setting signal handlers.
 *
 * This program can be distributed under the terms of the GNU LGPLv2.
 * See the file COPYING.LIB
 */

#include "qemu/osdep.h"
#include "fuse_i.h"
#include "fuse_lowlevel.h"


static struct fuse_session *fuse_instance;

static void exit_handler(int sig)
{
    if (fuse_instance) {
        fuse_session_exit(fuse_instance);
        if (sig <= 0) {
            fuse_log(FUSE_LOG_ERR, "assertion error: signal value <= 0\n");
            abort();
        }
        fuse_instance->error = sig;
    }
}

static void do_nothing(int sig)
{
    (void)sig;
}

static int set_one_signal_handler(int sig, void (*handler)(int), int remove)
{
    struct sigaction sa;
    struct sigaction old_sa;

    memset(&sa, 0, sizeof(struct sigaction));
    sa.sa_handler = remove ? SIG_DFL : handler;
    sigemptyset(&(sa.sa_mask));
    sa.sa_flags = 0;

    if (sigaction(sig, NULL, &old_sa) == -1) {
        fuse_log(FUSE_LOG_ERR, "fuse: cannot get old signal handler: %s\n",
                 strerror(errno));
        return -1;
    }

    if (old_sa.sa_handler == (remove ? handler : SIG_DFL) &&
        sigaction(sig, &sa, NULL) == -1) {
        fuse_log(FUSE_LOG_ERR, "fuse: cannot set signal handler: %s\n",
                 strerror(errno));
        return -1;
    }
    return 0;
}

int fuse_set_signal_handlers(struct fuse_session *se)
{
    /*
     * If we used SIG_IGN instead of the do_nothing function,
     * then we would be unable to tell if we set SIG_IGN (and
     * thus should reset to SIG_DFL in fuse_remove_signal_handlers)
     * or if it was already set to SIG_IGN (and should be left
     * untouched.
     */
    if (set_one_signal_handler(SIGHUP, exit_handler, 0) == -1 ||
        set_one_signal_handler(SIGINT, exit_handler, 0) == -1 ||
        set_one_signal_handler(SIGTERM, exit_handler, 0) == -1 ||
        set_one_signal_handler(SIGPIPE, do_nothing, 0) == -1) {
        return -1;
    }

    fuse_instance = se;
    return 0;
}

void fuse_remove_signal_handlers(struct fuse_session *se)
{
    if (fuse_instance != se) {
        fuse_log(FUSE_LOG_ERR,
                 "fuse: fuse_remove_signal_handlers: unknown session\n");
    } else {
        fuse_instance = NULL;
    }

    set_one_signal_handler(SIGHUP, exit_handler, 1);
    set_one_signal_handler(SIGINT, exit_handler, 1);
    set_one_signal_handler(SIGTERM, exit_handler, 1);
    set_one_signal_handler(SIGPIPE, do_nothing, 1);
}

/*
 * FUSE: Filesystem in Userspace
 * Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
 *
 * Helper functions to create (simple) standalone programs. With the
 * aid of these functions it should be possible to create full FUSE
 * file system by implementing nothing but the request handlers.

 * This program can be distributed under the terms of the GNU LGPLv2.
 * See the file COPYING.LIB.
 */

#include "qemu/osdep.h"
#include "fuse_i.h"
#include "fuse_lowlevel.h"
#include "fuse_misc.h"
#include "fuse_opt.h"

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>

#define FUSE_HELPER_OPT(t, p)                       \
    {                                               \
        t, offsetof(struct fuse_cmdline_opts, p), 1 \
    }
#define FUSE_HELPER_OPT_VALUE(t, p, v)              \
    {                                               \
        t, offsetof(struct fuse_cmdline_opts, p), v \
    }

static const struct fuse_opt fuse_helper_opts[] = {
    FUSE_HELPER_OPT("-h", show_help),
    FUSE_HELPER_OPT("--help", show_help),
    FUSE_HELPER_OPT("-V", show_version),
    FUSE_HELPER_OPT("--version", show_version),
    FUSE_HELPER_OPT("--print-capabilities", print_capabilities),
    FUSE_HELPER_OPT("-d", debug),
    FUSE_HELPER_OPT("debug", debug),
    FUSE_HELPER_OPT("-d", foreground),
    FUSE_HELPER_OPT("debug", foreground),
    FUSE_OPT_KEY("-d", FUSE_OPT_KEY_KEEP),
    FUSE_OPT_KEY("debug", FUSE_OPT_KEY_KEEP),
    FUSE_HELPER_OPT("-f", foreground),
    FUSE_HELPER_OPT_VALUE("--daemonize", foreground, 0),
    FUSE_HELPER_OPT("fsname=", nodefault_subtype),
    FUSE_OPT_KEY("fsname=", FUSE_OPT_KEY_KEEP),
    FUSE_HELPER_OPT("subtype=", nodefault_subtype),
    FUSE_OPT_KEY("subtype=", FUSE_OPT_KEY_KEEP),
    FUSE_HELPER_OPT("max_idle_threads=%u", max_idle_threads),
    FUSE_HELPER_OPT("--rlimit-nofile=%lu", rlimit_nofile),
    FUSE_HELPER_OPT("--syslog", syslog),
    FUSE_HELPER_OPT_VALUE("log_level=debug", log_level, FUSE_LOG_DEBUG),
    FUSE_HELPER_OPT_VALUE("log_level=info", log_level, FUSE_LOG_INFO),
    FUSE_HELPER_OPT_VALUE("log_level=warn", log_level, FUSE_LOG_WARNING),
    FUSE_HELPER_OPT_VALUE("log_level=err", log_level, FUSE_LOG_ERR),
    FUSE_OPT_END
};

struct fuse_conn_info_opts {
    int atomic_o_trunc;
    int no_remote_posix_lock;
    int no_remote_flock;
    int splice_write;
    int splice_move;
    int splice_read;
    int no_splice_write;
    int no_splice_move;
    int no_splice_read;
    int auto_inval_data;
    int no_auto_inval_data;
    int no_readdirplus;
    int no_readdirplus_auto;
    int async_dio;
    int no_async_dio;
    int writeback_cache;
    int no_writeback_cache;
    int async_read;
    int sync_read;
    unsigned max_write;
    unsigned max_readahead;
    unsigned max_background;
    unsigned congestion_threshold;
    unsigned time_gran;
    int set_max_write;
    int set_max_readahead;
    int set_max_background;
    int set_congestion_threshold;
    int set_time_gran;
};

#define CONN_OPTION(t, p, v)                          \
    {                                                 \
        t, offsetof(struct fuse_conn_info_opts, p), v \
    }
static const struct fuse_opt conn_info_opt_spec[] = {
    CONN_OPTION("max_write=%u", max_write, 0),
    CONN_OPTION("max_write=", set_max_write, 1),
    CONN_OPTION("max_readahead=%u", max_readahead, 0),
    CONN_OPTION("max_readahead=", set_max_readahead, 1),
    CONN_OPTION("max_background=%u", max_background, 0),
    CONN_OPTION("max_background=", set_max_background, 1),
    CONN_OPTION("congestion_threshold=%u", congestion_threshold, 0),
    CONN_OPTION("congestion_threshold=", set_congestion_threshold, 1),
    CONN_OPTION("sync_read", sync_read, 1),
    CONN_OPTION("async_read", async_read, 1),
    CONN_OPTION("atomic_o_trunc", atomic_o_trunc, 1),
    CONN_OPTION("no_remote_lock", no_remote_posix_lock, 1),
    CONN_OPTION("no_remote_lock", no_remote_flock, 1),
    CONN_OPTION("no_remote_flock", no_remote_flock, 1),
    CONN_OPTION("no_remote_posix_lock", no_remote_posix_lock, 1),
    CONN_OPTION("splice_write", splice_write, 1),
    CONN_OPTION("no_splice_write", no_splice_write, 1),
    CONN_OPTION("splice_move", splice_move, 1),
    CONN_OPTION("no_splice_move", no_splice_move, 1),
    CONN_OPTION("splice_read", splice_read, 1),
    CONN_OPTION("no_splice_read", no_splice_read, 1),
    CONN_OPTION("auto_inval_data", auto_inval_data, 1),
    CONN_OPTION("no_auto_inval_data", no_auto_inval_data, 1),
    CONN_OPTION("readdirplus=no", no_readdirplus, 1),
    CONN_OPTION("readdirplus=yes", no_readdirplus, 0),
    CONN_OPTION("readdirplus=yes", no_readdirplus_auto, 1),
    CONN_OPTION("readdirplus=auto", no_readdirplus, 0),
    CONN_OPTION("readdirplus=auto", no_readdirplus_auto, 0),
    CONN_OPTION("async_dio", async_dio, 1),
    CONN_OPTION("no_async_dio", no_async_dio, 1),
    CONN_OPTION("writeback_cache", writeback_cache, 1),
    CONN_OPTION("no_writeback_cache", no_writeback_cache, 1),
    CONN_OPTION("time_gran=%u", time_gran, 0),
    CONN_OPTION("time_gran=", set_time_gran, 1),
    FUSE_OPT_END
};


void fuse_cmdline_help(void)
{
    printf("    -h   --help                print help\n"
           "    -V   --version             print version\n"
           "    --print-capabilities       print vhost-user.json\n"
           "    -d   -o debug              enable debug output (implies -f)\n"
           "    --syslog                   log to syslog (default stderr)\n"
           "    -f                         foreground operation\n"
           "    --daemonize                run in background\n"
           "    -o cache=<mode>            cache mode. could be one of \"auto, "
           "always, none\"\n"
           "                               default: auto\n"
           "    -o flock|no_flock          enable/disable flock\n"
           "                               default: no_flock\n"
           "    -o log_level=<level>       log level, default to \"info\"\n"
           "                               level could be one of \"debug, "
           "info, warn, err\"\n"
           "    -o max_idle_threads        the maximum number of idle worker "
           "threads\n"
           "                               allowed (default: 10)\n"
           "    -o norace                  disable racy fallback\n"
           "                               default: false\n"
           "    -o posix_lock|no_posix_lock\n"
           "                               enable/disable remote posix lock\n"
           "                               default: posix_lock\n"
           "    -o readdirplus|no_readdirplus\n"
           "                               enable/disable readirplus\n"
           "                               default: readdirplus except with "
           "cache=none\n"
           "    -o timeout=<number>        I/O timeout (seconds)\n"
           "                               default: depends on cache= option.\n"
           "    -o writeback|no_writeback  enable/disable writeback cache\n"
           "                               default: no_writeback\n"
           "    -o xattr|no_xattr          enable/disable xattr\n"
           "                               default: no_xattr\n"
           "    -o modcaps=CAPLIST         Modify the list of capabilities\n"
           "                               e.g. -o modcaps=+sys_admin:-chown\n"
           "    --rlimit-nofile=<num>      set maximum number of file descriptors\n"
           "                               (0 leaves rlimit unchanged)\n"
           "                               default: min(1000000, fs.file-max - 16384)\n"
           "                                        if the current rlimit is lower\n"
           );
}

static int fuse_helper_opt_proc(void *data, const char *arg, int key,
                                struct fuse_args *outargs)
{
    (void)data;
    (void)outargs;

    switch (key) {
    case FUSE_OPT_KEY_NONOPT:
        fuse_log(FUSE_LOG_ERR, "fuse: invalid argument `%s'\n", arg);
        return -1;

    default:
        /* Pass through unknown options */
        return 1;
    }
}

static unsigned long get_default_rlimit_nofile(void)
{
    g_autofree gchar *file_max_str = NULL;
    const rlim_t reserved_fds = 16384; /* leave at least this many fds free */
    rlim_t max_fds = 1000000; /* our default RLIMIT_NOFILE target */
    rlim_t file_max;
    struct rlimit rlim;

    /*
     * Reduce max_fds below the system-wide maximum, if necessary.  This
     * ensures there are fds available for other processes so we don't
     * cause resource exhaustion.
     */
    if (!g_file_get_contents("/proc/sys/fs/file-max", &file_max_str,
                             NULL, NULL)) {
        fuse_log(FUSE_LOG_ERR, "can't read /proc/sys/fs/file-max\n");
        exit(1);
    }
    file_max = g_ascii_strtoull(file_max_str, NULL, 10);
    if (file_max < 2 * reserved_fds) {
        fuse_log(FUSE_LOG_ERR,
                 "The fs.file-max sysctl is too low (%lu) to allow a "
                 "reasonable number of open files.\n",
                 (unsigned long)file_max);
        exit(1);
    }
    max_fds = MIN(file_max - reserved_fds, max_fds);

    if (getrlimit(RLIMIT_NOFILE, &rlim) < 0) {
        fuse_log(FUSE_LOG_ERR, "getrlimit(RLIMIT_NOFILE): %m\n");
        exit(1);
    }

    if (rlim.rlim_cur >= max_fds) {
        return 0; /* we have more fds available than required! */
    }
    return max_fds;
}

int fuse_parse_cmdline(struct fuse_args *args, struct fuse_cmdline_opts *opts)
{
    memset(opts, 0, sizeof(struct fuse_cmdline_opts));

    opts->max_idle_threads = 10;
    opts->rlimit_nofile = get_default_rlimit_nofile();
    opts->foreground = 1;

    if (fuse_opt_parse(args, opts, fuse_helper_opts, fuse_helper_opt_proc) ==
        -1) {
        return -1;
    }

    return 0;
}


int fuse_daemonize(int foreground)
{
    int ret = 0, rett;
    if (!foreground) {
        int nullfd;
        int waiter[2];
        char completed;

        if (pipe(waiter)) {
            fuse_log(FUSE_LOG_ERR, "fuse_daemonize: pipe: %s\n",
                     strerror(errno));
            return -1;
        }

        /*
         * demonize current process by forking it and killing the
         * parent.  This makes current process as a child of 'init'.
         */
        switch (fork()) {
        case -1:
            fuse_log(FUSE_LOG_ERR, "fuse_daemonize: fork: %s\n",
                     strerror(errno));
            return -1;
        case 0:
            break;
        default:
            _exit(read(waiter[0], &completed,
                       sizeof(completed) != sizeof(completed)));
        }

        if (setsid() == -1) {
            fuse_log(FUSE_LOG_ERR, "fuse_daemonize: setsid: %s\n",
                     strerror(errno));
            return -1;
        }

        ret = chdir("/");

        nullfd = open("/dev/null", O_RDWR, 0);
        if (nullfd != -1) {
            rett = dup2(nullfd, 0);
            if (!ret) {
                ret = rett;
            }
            rett = dup2(nullfd, 1);
            if (!ret) {
                ret = rett;
            }
            rett = dup2(nullfd, 2);
            if (!ret) {
                ret = rett;
            }
            if (nullfd > 2) {
                close(nullfd);
            }
        }

        /* Propagate completion of daemon initialization */
        completed = 1;
        rett = write(waiter[1], &completed, sizeof(completed));
        if (!ret) {
            ret = rett;
        }
        close(waiter[0]);
        close(waiter[1]);
    } else {
        ret = chdir("/");
    }
    return ret;
}

void fuse_apply_conn_info_opts(struct fuse_conn_info_opts *opts,
                               struct fuse_conn_info *conn)
{
    if (opts->set_max_write) {
        conn->max_write = opts->max_write;
    }
    if (opts->set_max_background) {
        conn->max_background = opts->max_background;
    }
    if (opts->set_congestion_threshold) {
        conn->congestion_threshold = opts->congestion_threshold;
    }
    if (opts->set_time_gran) {
        conn->time_gran = opts->time_gran;
    }
    if (opts->set_max_readahead) {
        conn->max_readahead = opts->max_readahead;
    }

#define LL_ENABLE(cond, cap) \
    if (cond)                \
        conn->want |= (cap)
#define LL_DISABLE(cond, cap) \
    if (cond)                 \
        conn->want &= ~(cap)

    LL_ENABLE(opts->splice_read, FUSE_CAP_SPLICE_READ);
    LL_DISABLE(opts->no_splice_read, FUSE_CAP_SPLICE_READ);

    LL_ENABLE(opts->splice_write, FUSE_CAP_SPLICE_WRITE);
    LL_DISABLE(opts->no_splice_write, FUSE_CAP_SPLICE_WRITE);

    LL_ENABLE(opts->splice_move, FUSE_CAP_SPLICE_MOVE);
    LL_DISABLE(opts->no_splice_move, FUSE_CAP_SPLICE_MOVE);

    LL_ENABLE(opts->auto_inval_data, FUSE_CAP_AUTO_INVAL_DATA);
    LL_DISABLE(opts->no_auto_inval_data, FUSE_CAP_AUTO_INVAL_DATA);

    LL_DISABLE(opts->no_readdirplus, FUSE_CAP_READDIRPLUS);
    LL_DISABLE(opts->no_readdirplus_auto, FUSE_CAP_READDIRPLUS_AUTO);

    LL_ENABLE(opts->async_dio, FUSE_CAP_ASYNC_DIO);
    LL_DISABLE(opts->no_async_dio, FUSE_CAP_ASYNC_DIO);

    LL_ENABLE(opts->writeback_cache, FUSE_CAP_WRITEBACK_CACHE);
    LL_DISABLE(opts->no_writeback_cache, FUSE_CAP_WRITEBACK_CACHE);

    LL_ENABLE(opts->async_read, FUSE_CAP_ASYNC_READ);
    LL_DISABLE(opts->sync_read, FUSE_CAP_ASYNC_READ);

    LL_DISABLE(opts->no_remote_posix_lock, FUSE_CAP_POSIX_LOCKS);
    LL_DISABLE(opts->no_remote_flock, FUSE_CAP_FLOCK_LOCKS);
}

struct fuse_conn_info_opts *fuse_parse_conn_info_opts(struct fuse_args *args)
{
    struct fuse_conn_info_opts *opts;

    opts = calloc(1, sizeof(struct fuse_conn_info_opts));
    if (opts == NULL) {
        fuse_log(FUSE_LOG_ERR, "calloc failed\n");
        return NULL;
    }
    if (fuse_opt_parse(args, opts, conn_info_opt_spec, NULL) == -1) {
        free(opts);
        return NULL;
    }
    return opts;
}

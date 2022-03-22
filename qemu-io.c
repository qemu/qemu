/*
 * Command line utility to exercise the QEMU I/O path.
 *
 * Copyright (C) 2009 Red Hat, Inc.
 * Copyright (c) 2003-2005 Silicon Graphics, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <getopt.h>
#include <libgen.h>
#ifndef _WIN32
#include <termios.h>
#endif

#include "qemu-common.h"
#include "qapi/error.h"
#include "qemu-io.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/config-file.h"
#include "qemu/readline.h"
#include "qemu/log.h"
#include "qemu/sockets.h"
#include "qapi/qmp/qstring.h"
#include "qapi/qmp/qdict.h"
#include "qom/object_interfaces.h"
#include "sysemu/block-backend.h"
#include "block/block_int.h"
#include "trace/control.h"
#include "crypto/init.h"
#include "qemu-version.h"

#define CMD_NOFILE_OK   0x01

static BlockBackend *qemuio_blk;
static bool quit_qemu_io;

/* qemu-io commands passed using -c */
static int ncmdline;
static char **cmdline;
static bool imageOpts;

static ReadLineState *readline_state;

static int ttyEOF;

static int get_eof_char(void)
{
#ifdef _WIN32
    return 0x4; /* Ctrl-D */
#else
    struct termios tty;
    if (tcgetattr(STDIN_FILENO, &tty) != 0) {
        if (errno == ENOTTY) {
            return 0x0; /* just expect read() == 0 */
        } else {
            return 0x4; /* Ctrl-D */
        }
    }

    return tty.c_cc[VEOF];
#endif
}

static int close_f(BlockBackend *blk, int argc, char **argv)
{
    blk_unref(qemuio_blk);
    qemuio_blk = NULL;
    return 0;
}

static const cmdinfo_t close_cmd = {
    .name       = "close",
    .altname    = "c",
    .cfunc      = close_f,
    .oneline    = "close the current open file",
};

static int openfile(char *name, int flags, bool writethrough, bool force_share,
                    QDict *opts)
{
    Error *local_err = NULL;

    if (qemuio_blk) {
        error_report("file open already, try 'help close'");
        qobject_unref(opts);
        return 1;
    }

    if (force_share) {
        if (!opts) {
            opts = qdict_new();
        }
        if (qdict_haskey(opts, BDRV_OPT_FORCE_SHARE)
            && strcmp(qdict_get_str(opts, BDRV_OPT_FORCE_SHARE), "on")) {
            error_report("-U conflicts with image options");
            qobject_unref(opts);
            return 1;
        }
        qdict_put_str(opts, BDRV_OPT_FORCE_SHARE, "on");
    }
    qemuio_blk = blk_new_open(name, NULL, opts, flags, &local_err);
    if (!qemuio_blk) {
        error_reportf_err(local_err, "can't open%s%s: ",
                          name ? " device " : "", name ?: "");
        return 1;
    }

    blk_set_enable_write_cache(qemuio_blk, !writethrough);

    return 0;
}

static void open_help(void)
{
    printf(
"\n"
" opens a new file in the requested mode\n"
"\n"
" Example:\n"
" 'open -n -o driver=raw /tmp/data' - opens raw data file read-write, uncached\n"
"\n"
" Opens a file for subsequent use by all of the other qemu-io commands.\n"
" -r, -- open file read-only\n"
" -s, -- use snapshot file\n"
" -C, -- use copy-on-read\n"
" -n, -- disable host cache, short for -t none\n"
" -U, -- force shared permissions\n"
" -k, -- use kernel AIO implementation (Linux only, prefer use of -i)\n"
" -i, -- use AIO mode (threads, native or io_uring)\n"
" -t, -- use the given cache mode for the image\n"
" -d, -- use the given discard mode for the image\n"
" -o, -- options to be given to the block driver"
"\n");
}

static int open_f(BlockBackend *blk, int argc, char **argv);

static const cmdinfo_t open_cmd = {
    .name       = "open",
    .altname    = "o",
    .cfunc      = open_f,
    .argmin     = 1,
    .argmax     = -1,
    .flags      = CMD_NOFILE_OK,
    .args       = "[-rsCnkU] [-t cache] [-d discard] [-o options] [path]",
    .oneline    = "open the file specified by path",
    .help       = open_help,
};

static QemuOptsList empty_opts = {
    .name = "drive",
    .merge_lists = true,
    .head = QTAILQ_HEAD_INITIALIZER(empty_opts.head),
    .desc = {
        /* no elements => accept any params */
        { /* end of list */ }
    },
};

static int open_f(BlockBackend *blk, int argc, char **argv)
{
    int flags = BDRV_O_UNMAP;
    int readonly = 0;
    bool writethrough = true;
    int c;
    int ret;
    QemuOpts *qopts;
    QDict *opts;
    bool force_share = false;

    while ((c = getopt(argc, argv, "snCro:ki:t:d:U")) != -1) {
        switch (c) {
        case 's':
            flags |= BDRV_O_SNAPSHOT;
            break;
        case 'n':
            flags |= BDRV_O_NOCACHE;
            writethrough = false;
            break;
        case 'C':
            flags |= BDRV_O_COPY_ON_READ;
            break;
        case 'r':
            readonly = 1;
            break;
        case 'k':
            flags |= BDRV_O_NATIVE_AIO;
            break;
        case 't':
            if (bdrv_parse_cache_mode(optarg, &flags, &writethrough) < 0) {
                error_report("Invalid cache option: %s", optarg);
                qemu_opts_reset(&empty_opts);
                return -EINVAL;
            }
            break;
        case 'd':
            if (bdrv_parse_discard_flags(optarg, &flags) < 0) {
                error_report("Invalid discard option: %s", optarg);
                qemu_opts_reset(&empty_opts);
                return -EINVAL;
            }
            break;
        case 'i':
            if (bdrv_parse_aio(optarg, &flags) < 0) {
                error_report("Invalid aio option: %s", optarg);
                qemu_opts_reset(&empty_opts);
                return -EINVAL;
            }
            break;
        case 'o':
            if (imageOpts) {
                printf("--image-opts and 'open -o' are mutually exclusive\n");
                qemu_opts_reset(&empty_opts);
                return -EINVAL;
            }
            if (!qemu_opts_parse_noisily(&empty_opts, optarg, false)) {
                qemu_opts_reset(&empty_opts);
                return -EINVAL;
            }
            break;
        case 'U':
            force_share = true;
            break;
        default:
            qemu_opts_reset(&empty_opts);
            qemuio_command_usage(&open_cmd);
            return -EINVAL;
        }
    }

    if (!readonly) {
        flags |= BDRV_O_RDWR;
    }

    if (imageOpts && (optind == argc - 1)) {
        if (!qemu_opts_parse_noisily(&empty_opts, argv[optind], false)) {
            qemu_opts_reset(&empty_opts);
            return -EINVAL;
        }
        optind++;
    }

    qopts = qemu_opts_find(&empty_opts, NULL);
    opts = qopts ? qemu_opts_to_qdict(qopts, NULL) : NULL;
    qemu_opts_reset(&empty_opts);

    if (optind == argc - 1) {
        ret = openfile(argv[optind], flags, writethrough, force_share, opts);
    } else if (optind == argc) {
        ret = openfile(NULL, flags, writethrough, force_share, opts);
    } else {
        qobject_unref(opts);
        qemuio_command_usage(&open_cmd);
        return -EINVAL;
    }

    if (ret) {
        return -EINVAL;
    }

    return 0;
}

static int quit_f(BlockBackend *blk, int argc, char **argv)
{
    quit_qemu_io = true;
    return 0;
}

static const cmdinfo_t quit_cmd = {
    .name       = "quit",
    .altname    = "q",
    .cfunc      = quit_f,
    .argmin     = -1,
    .argmax     = -1,
    .flags      = CMD_FLAG_GLOBAL,
    .oneline    = "exit the program",
};

static void usage(const char *name)
{
    printf(
"Usage: %s [OPTIONS]... [-c STRING]... [file]\n"
"QEMU Disk exerciser\n"
"\n"
"  --object OBJECTDEF   define an object such as 'secret' for\n"
"                       passwords and/or encryption keys\n"
"  --image-opts         treat file as option string\n"
"  -c, --cmd STRING     execute command with its arguments\n"
"                       from the given string\n"
"  -f, --format FMT     specifies the block driver to use\n"
"  -r, --read-only      export read-only\n"
"  -s, --snapshot       use snapshot file\n"
"  -n, --nocache        disable host cache, short for -t none\n"
"  -C, --copy-on-read   enable copy-on-read\n"
"  -m, --misalign       misalign allocations for O_DIRECT\n"
"  -k, --native-aio     use kernel AIO implementation\n"
"                       (Linux only, prefer use of -i)\n"
"  -i, --aio=MODE       use AIO mode (threads, native or io_uring)\n"
"  -t, --cache=MODE     use the given cache mode for the image\n"
"  -d, --discard=MODE   use the given discard mode for the image\n"
"  -T, --trace [[enable=]<pattern>][,events=<file>][,file=<file>]\n"
"                       specify tracing options\n"
"                       see qemu-img(1) man page for full description\n"
"  -U, --force-share    force shared permissions\n"
"  -h, --help           display this help and exit\n"
"  -V, --version        output version information and exit\n"
"\n"
"See '%s -c help' for information on available commands.\n"
"\n"
QEMU_HELP_BOTTOM "\n",
    name, name);
}

static char *get_prompt(void)
{
    static char prompt[FILENAME_MAX + 2 /*"> "*/ + 1 /*"\0"*/ ];

    if (!prompt[0]) {
        snprintf(prompt, sizeof(prompt), "%s> ", g_get_prgname());
    }

    return prompt;
}

static void G_GNUC_PRINTF(2, 3) readline_printf_func(void *opaque,
                                                    const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

static void readline_flush_func(void *opaque)
{
    fflush(stdout);
}

static void readline_func(void *opaque, const char *str, void *readline_opaque)
{
    char **line = readline_opaque;
    *line = g_strdup(str);
}

static void completion_match(const char *cmd, void *opaque)
{
    readline_add_completion(readline_state, cmd);
}

static void readline_completion_func(void *opaque, const char *str)
{
    readline_set_completion_index(readline_state, strlen(str));
    qemuio_complete_command(str, completion_match, NULL);
}

static char *fetchline_readline(void)
{
    char *line = NULL;

    readline_start(readline_state, get_prompt(), 0, readline_func, &line);
    while (!line) {
        int ch = getchar();
        if (ttyEOF != 0x0 && ch == ttyEOF) {
            printf("\n");
            break;
        }
        readline_handle_byte(readline_state, ch);
    }
    return line;
}

#define MAXREADLINESZ 1024
static char *fetchline_fgets(void)
{
    char *p, *line = g_malloc(MAXREADLINESZ);

    if (!fgets(line, MAXREADLINESZ, stdin)) {
        g_free(line);
        return NULL;
    }

    p = line + strlen(line);
    if (p != line && p[-1] == '\n') {
        p[-1] = '\0';
    }

    return line;
}

static char *fetchline(void)
{
    if (readline_state) {
        return fetchline_readline();
    } else {
        return fetchline_fgets();
    }
}

static void prep_fetchline(void *opaque)
{
    int *fetchable = opaque;

    qemu_set_fd_handler(STDIN_FILENO, NULL, NULL, NULL);
    *fetchable= 1;
}

static int do_qemuio_command(const char *cmd)
{
    int ret;
    AioContext *ctx =
        qemuio_blk ? blk_get_aio_context(qemuio_blk) : qemu_get_aio_context();

    aio_context_acquire(ctx);
    ret = qemuio_command(qemuio_blk, cmd);
    aio_context_release(ctx);

    return ret;
}

static int command_loop(void)
{
    int i, fetchable = 0, prompted = 0;
    int ret, last_error = 0;
    char *input;

    for (i = 0; !quit_qemu_io && i < ncmdline; i++) {
        ret = do_qemuio_command(cmdline[i]);
        if (ret < 0) {
            last_error = ret;
        }
    }
    if (cmdline) {
        g_free(cmdline);
        return last_error;
    }

    while (!quit_qemu_io) {
        if (!prompted) {
            printf("%s", get_prompt());
            fflush(stdout);
            qemu_set_fd_handler(STDIN_FILENO, prep_fetchline, NULL, &fetchable);
            prompted = 1;
        }

        main_loop_wait(false);

        if (!fetchable) {
            continue;
        }

        input = fetchline();
        if (input == NULL) {
            break;
        }
        ret = do_qemuio_command(input);
        g_free(input);

        if (ret < 0) {
            last_error = ret;
        }

        prompted = 0;
        fetchable = 0;
    }
    qemu_set_fd_handler(STDIN_FILENO, NULL, NULL, NULL);

    return last_error;
}

static void add_user_command(char *optarg)
{
    cmdline = g_renew(char *, cmdline, ++ncmdline);
    cmdline[ncmdline-1] = optarg;
}

static void reenable_tty_echo(void)
{
    qemu_set_tty_echo(STDIN_FILENO, true);
}

enum {
    OPTION_OBJECT = 256,
    OPTION_IMAGE_OPTS = 257,
};

static QemuOptsList file_opts = {
    .name = "file",
    .implied_opt_name = "file",
    .head = QTAILQ_HEAD_INITIALIZER(file_opts.head),
    .desc = {
        /* no elements => accept any params */
        { /* end of list */ }
    },
};

int main(int argc, char **argv)
{
    int readonly = 0;
    const char *sopt = "hVc:d:f:rsnCmki:t:T:U";
    const struct option lopt[] = {
        { "help", no_argument, NULL, 'h' },
        { "version", no_argument, NULL, 'V' },
        { "cmd", required_argument, NULL, 'c' },
        { "format", required_argument, NULL, 'f' },
        { "read-only", no_argument, NULL, 'r' },
        { "snapshot", no_argument, NULL, 's' },
        { "nocache", no_argument, NULL, 'n' },
        { "copy-on-read", no_argument, NULL, 'C' },
        { "misalign", no_argument, NULL, 'm' },
        { "native-aio", no_argument, NULL, 'k' },
        { "aio", required_argument, NULL, 'i' },
        { "discard", required_argument, NULL, 'd' },
        { "cache", required_argument, NULL, 't' },
        { "trace", required_argument, NULL, 'T' },
        { "object", required_argument, NULL, OPTION_OBJECT },
        { "image-opts", no_argument, NULL, OPTION_IMAGE_OPTS },
        { "force-share", no_argument, 0, 'U'},
        { NULL, 0, NULL, 0 }
    };
    int c;
    int opt_index = 0;
    int flags = BDRV_O_UNMAP;
    int ret;
    bool writethrough = true;
    QDict *opts = NULL;
    const char *format = NULL;
    bool force_share = false;

#ifdef CONFIG_POSIX
    signal(SIGPIPE, SIG_IGN);
#endif

    socket_init();
    error_init(argv[0]);
    module_call_init(MODULE_INIT_TRACE);
    qemu_init_exec_dir(argv[0]);

    qcrypto_init(&error_fatal);

    module_call_init(MODULE_INIT_QOM);
    qemu_add_opts(&qemu_trace_opts);
    bdrv_init();

    while ((c = getopt_long(argc, argv, sopt, lopt, &opt_index)) != -1) {
        switch (c) {
        case 's':
            flags |= BDRV_O_SNAPSHOT;
            break;
        case 'n':
            flags |= BDRV_O_NOCACHE;
            writethrough = false;
            break;
        case 'C':
            flags |= BDRV_O_COPY_ON_READ;
            break;
        case 'd':
            if (bdrv_parse_discard_flags(optarg, &flags) < 0) {
                error_report("Invalid discard option: %s", optarg);
                exit(1);
            }
            break;
        case 'f':
            format = optarg;
            break;
        case 'c':
            add_user_command(optarg);
            break;
        case 'r':
            readonly = 1;
            break;
        case 'm':
            qemuio_misalign = true;
            break;
        case 'k':
            flags |= BDRV_O_NATIVE_AIO;
            break;
        case 'i':
            if (bdrv_parse_aio(optarg, &flags) < 0) {
                error_report("Invalid aio option: %s", optarg);
                exit(1);
            }
            break;
        case 't':
            if (bdrv_parse_cache_mode(optarg, &flags, &writethrough) < 0) {
                error_report("Invalid cache option: %s", optarg);
                exit(1);
            }
            break;
        case 'T':
            trace_opt_parse(optarg);
            break;
        case 'V':
            printf("%s version " QEMU_FULL_VERSION "\n"
                   QEMU_COPYRIGHT "\n", g_get_prgname());
            exit(0);
        case 'h':
            usage(g_get_prgname());
            exit(0);
        case 'U':
            force_share = true;
            break;
        case OPTION_OBJECT:
            user_creatable_process_cmdline(optarg);
            break;
        case OPTION_IMAGE_OPTS:
            imageOpts = true;
            break;
        default:
            usage(g_get_prgname());
            exit(1);
        }
    }

    if ((argc - optind) > 1) {
        usage(g_get_prgname());
        exit(1);
    }

    if (format && imageOpts) {
        error_report("--image-opts and -f are mutually exclusive");
        exit(1);
    }

    qemu_init_main_loop(&error_fatal);

    if (!trace_init_backends()) {
        exit(1);
    }
    trace_init_file();
    qemu_set_log(LOG_TRACE);

    /* initialize commands */
    qemuio_add_command(&quit_cmd);
    qemuio_add_command(&open_cmd);
    qemuio_add_command(&close_cmd);

    if (isatty(STDIN_FILENO)) {
        ttyEOF = get_eof_char();
        readline_state = readline_init(readline_printf_func,
                                       readline_flush_func,
                                       NULL,
                                       readline_completion_func);
        qemu_set_tty_echo(STDIN_FILENO, false);
        atexit(reenable_tty_echo);
    }

    /* open the device */
    if (!readonly) {
        flags |= BDRV_O_RDWR;
    }

    if ((argc - optind) == 1) {
        if (imageOpts) {
            QemuOpts *qopts = NULL;
            qopts = qemu_opts_parse_noisily(&file_opts, argv[optind], false);
            if (!qopts) {
                exit(1);
            }
            opts = qemu_opts_to_qdict(qopts, NULL);
            if (openfile(NULL, flags, writethrough, force_share, opts)) {
                exit(1);
            }
        } else {
            if (format) {
                opts = qdict_new();
                qdict_put_str(opts, "driver", format);
            }
            if (openfile(argv[optind], flags, writethrough,
                         force_share, opts)) {
                exit(1);
            }
        }
    }
    ret = command_loop();

    /*
     * Make sure all outstanding requests complete before the program exits.
     */
    bdrv_drain_all();

    blk_unref(qemuio_blk);
    g_free(readline_state);

    if (ret < 0) {
        return 1;
    } else {
        return 0;
    }
}

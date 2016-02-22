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

#include "qemu-io.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/option.h"
#include "qemu/config-file.h"
#include "qemu/readline.h"
#include "qapi/qmp/qstring.h"
#include "qom/object_interfaces.h"
#include "sysemu/block-backend.h"
#include "block/block_int.h"
#include "trace/control.h"

#define CMD_NOFILE_OK   0x01

static char *progname;

static BlockBackend *qemuio_blk;

/* qemu-io commands passed using -c */
static int ncmdline;
static char **cmdline;
static bool imageOpts;

static ReadLineState *readline_state;

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

static int openfile(char *name, int flags, QDict *opts)
{
    Error *local_err = NULL;
    BlockDriverState *bs;

    if (qemuio_blk) {
        error_report("file open already, try 'help close'");
        QDECREF(opts);
        return 1;
    }

    qemuio_blk = blk_new_open("hda", name, NULL, opts, flags, &local_err);
    if (!qemuio_blk) {
        error_reportf_err(local_err, "can't open%s%s: ",
                          name ? " device " : "", name ?: "");
        return 1;
    }

    bs = blk_bs(qemuio_blk);
    if (bdrv_is_encrypted(bs)) {
        char password[256];
        printf("Disk image '%s' is encrypted.\n", name);
        if (qemu_read_password(password, sizeof(password)) < 0) {
            error_report("No password given");
            goto error;
        }
        if (bdrv_set_key(bs, password) < 0) {
            error_report("invalid password");
            goto error;
        }
    }


    return 0;

 error:
    blk_unref(qemuio_blk);
    qemuio_blk = NULL;
    return 1;
}

static void open_help(void)
{
    printf(
"\n"
" opens a new file in the requested mode\n"
"\n"
" Example:\n"
" 'open -Cn /tmp/data' - creates/opens data file read-write and uncached\n"
"\n"
" Opens a file for subsequent use by all of the other qemu-io commands.\n"
" -r, -- open file read-only\n"
" -s, -- use snapshot file\n"
" -n, -- disable host cache\n"
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
    .args       = "[-Crsn] [-o options] [path]",
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
    int flags = 0;
    int readonly = 0;
    int c;
    QemuOpts *qopts;
    QDict *opts;

    while ((c = getopt(argc, argv, "snrgo:")) != -1) {
        switch (c) {
        case 's':
            flags |= BDRV_O_SNAPSHOT;
            break;
        case 'n':
            flags |= BDRV_O_NOCACHE | BDRV_O_CACHE_WB;
            break;
        case 'r':
            readonly = 1;
            break;
        case 'o':
            if (imageOpts) {
                printf("--image-opts and 'open -o' are mutually exclusive\n");
                return 0;
            }
            if (!qemu_opts_parse_noisily(&empty_opts, optarg, false)) {
                qemu_opts_reset(&empty_opts);
                return 0;
            }
            break;
        default:
            qemu_opts_reset(&empty_opts);
            return qemuio_command_usage(&open_cmd);
        }
    }

    if (!readonly) {
        flags |= BDRV_O_RDWR;
    }

    if (imageOpts && (optind == argc - 1)) {
        if (!qemu_opts_parse_noisily(&empty_opts, argv[optind], false)) {
            qemu_opts_reset(&empty_opts);
            return 0;
        }
        optind++;
    }

    qopts = qemu_opts_find(&empty_opts, NULL);
    opts = qopts ? qemu_opts_to_qdict(qopts, NULL) : NULL;
    qemu_opts_reset(&empty_opts);

    if (optind == argc - 1) {
        return openfile(argv[optind], flags, opts);
    } else if (optind == argc) {
        return openfile(NULL, flags, opts);
    } else {
        QDECREF(opts);
        return qemuio_command_usage(&open_cmd);
    }
}

static int quit_f(BlockBackend *blk, int argc, char **argv)
{
    return 1;
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
"Usage: %s [-h] [-V] [-rsnm] [-f FMT] [-c STRING] ... [file]\n"
"QEMU Disk exerciser\n"
"\n"
"  --object OBJECTDEF   define an object such as 'secret' for\n"
"                       passwords and/or encryption keys\n"
"  -c, --cmd STRING     execute command with its arguments\n"
"                       from the given string\n"
"  -f, --format FMT     specifies the block driver to use\n"
"  -r, --read-only      export read-only\n"
"  -s, --snapshot       use snapshot file\n"
"  -n, --nocache        disable host cache\n"
"  -m, --misalign       misalign allocations for O_DIRECT\n"
"  -k, --native-aio     use kernel AIO implementation (on Linux only)\n"
"  -t, --cache=MODE     use the given cache mode for the image\n"
"  -T, --trace FILE     enable trace events listed in the given file\n"
"  -h, --help           display this help and exit\n"
"  -V, --version        output version information and exit\n"
"\n"
"See '%s -c help' for information on available commands."
"\n",
    name, name);
}

static char *get_prompt(void)
{
    static char prompt[FILENAME_MAX + 2 /*"> "*/ + 1 /*"\0"*/ ];

    if (!prompt[0]) {
        snprintf(prompt, sizeof(prompt), "%s> ", progname);
    }

    return prompt;
}

static void GCC_FMT_ATTR(2, 3) readline_printf_func(void *opaque,
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
        if (ch == EOF) {
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

static void command_loop(void)
{
    int i, done = 0, fetchable = 0, prompted = 0;
    char *input;

    for (i = 0; !done && i < ncmdline; i++) {
        done = qemuio_command(qemuio_blk, cmdline[i]);
    }
    if (cmdline) {
        g_free(cmdline);
        return;
    }

    while (!done) {
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
        done = qemuio_command(qemuio_blk, input);
        g_free(input);

        prompted = 0;
        fetchable = 0;
    }
    qemu_set_fd_handler(STDIN_FILENO, NULL, NULL, NULL);
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

static QemuOptsList qemu_object_opts = {
    .name = "object",
    .implied_opt_name = "qom-type",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_object_opts.head),
    .desc = {
        { }
    },
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
    const char *sopt = "hVc:d:f:rsnmgkt:T:";
    const struct option lopt[] = {
        { "help", no_argument, NULL, 'h' },
        { "version", no_argument, NULL, 'V' },
        { "offset", required_argument, NULL, 'o' },
        { "cmd", required_argument, NULL, 'c' },
        { "format", required_argument, NULL, 'f' },
        { "read-only", no_argument, NULL, 'r' },
        { "snapshot", no_argument, NULL, 's' },
        { "nocache", no_argument, NULL, 'n' },
        { "misalign", no_argument, NULL, 'm' },
        { "native-aio", no_argument, NULL, 'k' },
        { "discard", required_argument, NULL, 'd' },
        { "cache", required_argument, NULL, 't' },
        { "trace", required_argument, NULL, 'T' },
        { "object", required_argument, NULL, OPTION_OBJECT },
        { "image-opts", no_argument, NULL, OPTION_IMAGE_OPTS },
        { NULL, 0, NULL, 0 }
    };
    int c;
    int opt_index = 0;
    int flags = BDRV_O_UNMAP;
    Error *local_error = NULL;
    QDict *opts = NULL;
    const char *format = NULL;

#ifdef CONFIG_POSIX
    signal(SIGPIPE, SIG_IGN);
#endif

    progname = basename(argv[0]);
    qemu_init_exec_dir(argv[0]);

    module_call_init(MODULE_INIT_QOM);
    qemu_add_opts(&qemu_object_opts);
    bdrv_init();

    while ((c = getopt_long(argc, argv, sopt, lopt, &opt_index)) != -1) {
        switch (c) {
        case 's':
            flags |= BDRV_O_SNAPSHOT;
            break;
        case 'n':
            flags |= BDRV_O_NOCACHE | BDRV_O_CACHE_WB;
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
        case 't':
            if (bdrv_parse_cache_flags(optarg, &flags) < 0) {
                error_report("Invalid cache option: %s", optarg);
                exit(1);
            }
            break;
        case 'T':
            if (!trace_init_backends()) {
                exit(1); /* error message will have been printed */
            }
            break;
        case 'V':
            printf("%s version %s\n", progname, QEMU_VERSION);
            exit(0);
        case 'h':
            usage(progname);
            exit(0);
        case OPTION_OBJECT: {
            QemuOpts *qopts;
            qopts = qemu_opts_parse_noisily(&qemu_object_opts,
                                            optarg, true);
            if (!qopts) {
                exit(1);
            }
        }   break;
        case OPTION_IMAGE_OPTS:
            imageOpts = true;
            break;
        default:
            usage(progname);
            exit(1);
        }
    }

    if ((argc - optind) > 1) {
        usage(progname);
        exit(1);
    }

    if (format && imageOpts) {
        error_report("--image-opts and -f are mutually exclusive");
        exit(1);
    }

    if (qemu_init_main_loop(&local_error)) {
        error_report_err(local_error);
        exit(1);
    }

    if (qemu_opts_foreach(&qemu_object_opts,
                          user_creatable_add_opts_foreach,
                          NULL, &local_error)) {
        error_report_err(local_error);
        exit(1);
    }

    /* initialize commands */
    qemuio_add_command(&quit_cmd);
    qemuio_add_command(&open_cmd);
    qemuio_add_command(&close_cmd);

    if (isatty(STDIN_FILENO)) {
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
            openfile(NULL, flags, opts);
        } else {
            if (format) {
                opts = qdict_new();
                qdict_put(opts, "driver", qstring_from_str(format));
            }
            openfile(argv[optind], flags, opts);
        }
    }
    command_loop();

    /*
     * Make sure all outstanding requests complete before the program exits.
     */
    bdrv_drain_all();

    blk_unref(qemuio_blk);
    g_free(readline_state);
    return 0;
}

/*
 * Command line utility to exercise the QEMU I/O path.
 *
 * Copyright (C) 2009 Red Hat, Inc.
 * Copyright (c) 2003-2005 Silicon Graphics, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include <sys/time.h>
#include <sys/types.h>
#include <stdarg.h>
#include <stdio.h>
#include <getopt.h>
#include <libgen.h>

#include "qemu-io.h"
#include "qemu/main-loop.h"
#include "qemu/option.h"
#include "qemu/config-file.h"
#include "qemu/readline.h"
#include "block/block_int.h"
#include "trace/control.h"

#define CMD_NOFILE_OK   0x01

static char *progname;

static BlockDriverState *qemuio_bs;

/* qemu-io commands passed using -c */
static int ncmdline;
static char **cmdline;

static ReadLineState *readline_state;

static int close_f(BlockDriverState *bs, int argc, char **argv)
{
    bdrv_unref(bs);
    qemuio_bs = NULL;
    return 0;
}

static const cmdinfo_t close_cmd = {
    .name       = "close",
    .altname    = "c",
    .cfunc      = close_f,
    .oneline    = "close the current open file",
};

static int openfile(char *name, int flags, int growable, QDict *opts)
{
    Error *local_err = NULL;

    if (qemuio_bs) {
        fprintf(stderr, "file open already, try 'help close'\n");
        QDECREF(opts);
        return 1;
    }

    if (growable) {
        if (bdrv_open(&qemuio_bs, name, NULL, opts, flags | BDRV_O_PROTOCOL,
                      NULL, &local_err))
        {
            fprintf(stderr, "%s: can't open device %s: %s\n", progname, name,
                    error_get_pretty(local_err));
            error_free(local_err);
            return 1;
        }
    } else {
        qemuio_bs = bdrv_new("hda", &error_abort);

        if (bdrv_open(&qemuio_bs, name, NULL, opts, flags, NULL, &local_err)
            < 0)
        {
            fprintf(stderr, "%s: can't open device %s: %s\n", progname, name,
                    error_get_pretty(local_err));
            error_free(local_err);
            bdrv_unref(qemuio_bs);
            qemuio_bs = NULL;
            return 1;
        }
    }

    return 0;
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
" -g, -- allow file to grow (only applies to protocols)\n"
" -o, -- options to be given to the block driver"
"\n");
}

static int open_f(BlockDriverState *bs, int argc, char **argv);

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

static int open_f(BlockDriverState *bs, int argc, char **argv)
{
    int flags = 0;
    int readonly = 0;
    int growable = 0;
    int c;
    QemuOpts *qopts;
    QDict *opts;

    while ((c = getopt(argc, argv, "snrgo:")) != EOF) {
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
        case 'g':
            growable = 1;
            break;
        case 'o':
            if (!qemu_opts_parse(&empty_opts, optarg, 0)) {
                printf("could not parse option list -- %s\n", optarg);
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

    qopts = qemu_opts_find(&empty_opts, NULL);
    opts = qopts ? qemu_opts_to_qdict(qopts, NULL) : NULL;
    qemu_opts_reset(&empty_opts);

    if (optind == argc - 1) {
        return openfile(argv[optind], flags, growable, opts);
    } else if (optind == argc) {
        return openfile(NULL, flags, growable, opts);
    } else {
        QDECREF(opts);
        return qemuio_command_usage(&open_cmd);
    }
}

static int quit_f(BlockDriverState *bs, int argc, char **argv)
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
"Usage: %s [-h] [-V] [-rsnm] [-c STRING] ... [file]\n"
"QEMU Disk exerciser\n"
"\n"
"  -c, --cmd STRING     execute command with its arguments\n"
"                       from the given string\n"
"  -r, --read-only      export read-only\n"
"  -s, --snapshot       use snapshot file\n"
"  -n, --nocache        disable host cache\n"
"  -g, --growable       allow file to grow (only applies to protocols)\n"
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
        done = qemuio_command(qemuio_bs, cmdline[i]);
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
        done = qemuio_command(qemuio_bs, input);
        g_free(input);

        prompted = 0;
        fetchable = 0;
    }
    qemu_set_fd_handler(STDIN_FILENO, NULL, NULL, NULL);
}

static void add_user_command(char *optarg)
{
    cmdline = g_realloc(cmdline, ++ncmdline * sizeof(char *));
    cmdline[ncmdline-1] = optarg;
}

static void reenable_tty_echo(void)
{
    qemu_set_tty_echo(STDIN_FILENO, true);
}

int main(int argc, char **argv)
{
    int readonly = 0;
    int growable = 0;
    const char *sopt = "hVc:d:rsnmgkt:T:";
    const struct option lopt[] = {
        { "help", 0, NULL, 'h' },
        { "version", 0, NULL, 'V' },
        { "offset", 1, NULL, 'o' },
        { "cmd", 1, NULL, 'c' },
        { "read-only", 0, NULL, 'r' },
        { "snapshot", 0, NULL, 's' },
        { "nocache", 0, NULL, 'n' },
        { "misalign", 0, NULL, 'm' },
        { "growable", 0, NULL, 'g' },
        { "native-aio", 0, NULL, 'k' },
        { "discard", 1, NULL, 'd' },
        { "cache", 1, NULL, 't' },
        { "trace", 1, NULL, 'T' },
        { NULL, 0, NULL, 0 }
    };
    int c;
    int opt_index = 0;
    int flags = BDRV_O_UNMAP;

#ifdef CONFIG_POSIX
    signal(SIGPIPE, SIG_IGN);
#endif

    progname = basename(argv[0]);
    qemu_init_exec_dir(argv[0]);

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
        case 'c':
            add_user_command(optarg);
            break;
        case 'r':
            readonly = 1;
            break;
        case 'm':
            qemuio_misalign = true;
            break;
        case 'g':
            growable = 1;
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
            if (!trace_backend_init(optarg, NULL)) {
                exit(1); /* error message will have been printed */
            }
            break;
        case 'V':
            printf("%s version %s\n", progname, QEMU_VERSION);
            exit(0);
        case 'h':
            usage(progname);
            exit(0);
        default:
            usage(progname);
            exit(1);
        }
    }

    if ((argc - optind) > 1) {
        usage(progname);
        exit(1);
    }

    qemu_init_main_loop();
    bdrv_init();

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
        openfile(argv[optind], flags, growable, NULL);
    }
    command_loop();

    /*
     * Make sure all outstanding requests complete before the program exits.
     */
    bdrv_drain_all();

    if (qemuio_bs) {
        bdrv_unref(qemuio_bs);
    }
    g_free(readline_state);
    return 0;
}

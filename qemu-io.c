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
#include "block/block_int.h"
#include "trace/control.h"

#define CMD_NOFILE_OK   0x01

char *progname;

BlockDriverState *qemuio_bs;
extern int qemuio_misalign;

/* qemu-io commands passed using -c */
static int ncmdline;
static char **cmdline;

static int close_f(BlockDriverState *bs, int argc, char **argv)
{
    bdrv_delete(bs);
    qemuio_bs = NULL;
    return 0;
}

static const cmdinfo_t close_cmd = {
    .name       = "close",
    .altname    = "c",
    .cfunc      = close_f,
    .oneline    = "close the current open file",
};

static int openfile(char *name, int flags, int growable)
{
    if (qemuio_bs) {
        fprintf(stderr, "file open already, try 'help close'\n");
        return 1;
    }

    if (growable) {
        if (bdrv_file_open(&qemuio_bs, name, NULL, flags)) {
            fprintf(stderr, "%s: can't open device %s\n", progname, name);
            return 1;
        }
    } else {
        qemuio_bs = bdrv_new("hda");

        if (bdrv_open(qemuio_bs, name, NULL, flags, NULL) < 0) {
            fprintf(stderr, "%s: can't open device %s\n", progname, name);
            bdrv_delete(qemuio_bs);
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
" -g, -- allow file to grow (only applies to protocols)"
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
    .args       = "[-Crsn] [path]",
    .oneline    = "open the file specified by path",
    .help       = open_help,
};

static int open_f(BlockDriverState *bs, int argc, char **argv)
{
    int flags = 0;
    int readonly = 0;
    int growable = 0;
    int c;

    while ((c = getopt(argc, argv, "snrg")) != EOF) {
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
        default:
            return qemuio_command_usage(&open_cmd);
        }
    }

    if (!readonly) {
        flags |= BDRV_O_RDWR;
    }

    if (optind != argc - 1) {
        return qemuio_command_usage(&open_cmd);
    }

    return openfile(argv[optind], flags, growable);
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
"Usage: %s [-h] [-V] [-rsnm] [-c cmd] ... [file]\n"
"QEMU Disk exerciser\n"
"\n"
"  -c, --cmd            command to execute\n"
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
"\n",
    name);
}


#if defined(ENABLE_READLINE)
# include <readline/history.h>
# include <readline/readline.h>
#elif defined(ENABLE_EDITLINE)
# include <histedit.h>
#endif

static char *get_prompt(void)
{
    static char prompt[FILENAME_MAX + 2 /*"> "*/ + 1 /*"\0"*/ ];

    if (!prompt[0]) {
        snprintf(prompt, sizeof(prompt), "%s> ", progname);
    }

    return prompt;
}

#if defined(ENABLE_READLINE)
static char *fetchline(void)
{
    char *line = readline(get_prompt());
    if (line && *line) {
        add_history(line);
    }
    return line;
}
#elif defined(ENABLE_EDITLINE)
static char *el_get_prompt(EditLine *e)
{
    return get_prompt();
}

static char *fetchline(void)
{
    static EditLine *el;
    static History *hist;
    HistEvent hevent;
    char *line;
    int count;

    if (!el) {
        hist = history_init();
        history(hist, &hevent, H_SETSIZE, 100);
        el = el_init(progname, stdin, stdout, stderr);
        el_source(el, NULL);
        el_set(el, EL_SIGNAL, 1);
        el_set(el, EL_PROMPT, el_get_prompt);
        el_set(el, EL_HIST, history, (const char *)hist);
    }
    line = strdup(el_gets(el, &count));
    if (line) {
        if (count > 0) {
            line[count-1] = '\0';
        }
        if (*line) {
            history(hist, &hevent, H_ENTER, line);
        }
    }
    return line;
}
#else
# define MAXREADLINESZ 1024
static char *fetchline(void)
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
#endif

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

    progname = basename(argv[0]);

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
            qemuio_misalign = 1;
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

    /* open the device */
    if (!readonly) {
        flags |= BDRV_O_RDWR;
    }

    if ((argc - optind) == 1) {
        openfile(argv[optind], flags, growable);
    }
    command_loop();

    /*
     * Make sure all outstanding requests complete before the program exits.
     */
    bdrv_drain_all();

    if (qemuio_bs) {
        bdrv_delete(qemuio_bs);
    }
    return 0;
}

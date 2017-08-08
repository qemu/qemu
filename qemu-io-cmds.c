/*
 * Command line utility to exercise the QEMU I/O path.
 *
 * Copyright (C) 2009-2016 Red Hat, Inc.
 * Copyright (c) 2003-2005 Silicon Graphics, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-io.h"
#include "sysemu/block-backend.h"
#include "block/block.h"
#include "block/block_int.h" /* for info_f() */
#include "block/qapi.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/timer.h"
#include "qemu/cutils.h"

#define CMD_NOFILE_OK   0x01

bool qemuio_misalign;

static cmdinfo_t *cmdtab;
static int ncmds;

static int compare_cmdname(const void *a, const void *b)
{
    return strcmp(((const cmdinfo_t *)a)->name,
                  ((const cmdinfo_t *)b)->name);
}

void qemuio_add_command(const cmdinfo_t *ci)
{
    /* ci->perm assumes a file is open, but the GLOBAL and NOFILE_OK
     * flags allow it not to be, so that combination is invalid.
     * Catch it now rather than letting it manifest as a crash if a
     * particular set of command line options are used.
     */
    assert(ci->perm == 0 ||
           (ci->flags & (CMD_FLAG_GLOBAL | CMD_NOFILE_OK)) == 0);
    cmdtab = g_renew(cmdinfo_t, cmdtab, ++ncmds);
    cmdtab[ncmds - 1] = *ci;
    qsort(cmdtab, ncmds, sizeof(*cmdtab), compare_cmdname);
}

int qemuio_command_usage(const cmdinfo_t *ci)
{
    printf("%s %s -- %s\n", ci->name, ci->args, ci->oneline);
    return 0;
}

static int init_check_command(BlockBackend *blk, const cmdinfo_t *ct)
{
    if (ct->flags & CMD_FLAG_GLOBAL) {
        return 1;
    }
    if (!(ct->flags & CMD_NOFILE_OK) && !blk) {
        fprintf(stderr, "no file open, try 'help open'\n");
        return 0;
    }
    return 1;
}

static int command(BlockBackend *blk, const cmdinfo_t *ct, int argc,
                   char **argv)
{
    char *cmd = argv[0];

    if (!init_check_command(blk, ct)) {
        return 0;
    }

    if (argc - 1 < ct->argmin || (ct->argmax != -1 && argc - 1 > ct->argmax)) {
        if (ct->argmax == -1) {
            fprintf(stderr,
                    "bad argument count %d to %s, expected at least %d arguments\n",
                    argc-1, cmd, ct->argmin);
        } else if (ct->argmin == ct->argmax) {
            fprintf(stderr,
                    "bad argument count %d to %s, expected %d arguments\n",
                    argc-1, cmd, ct->argmin);
        } else {
            fprintf(stderr,
                    "bad argument count %d to %s, expected between %d and %d arguments\n",
                    argc-1, cmd, ct->argmin, ct->argmax);
        }
        return 0;
    }

    /* Request additional permissions if necessary for this command. The caller
     * is responsible for restoring the original permissions afterwards if this
     * is what it wants. */
    if (ct->perm && blk_is_available(blk)) {
        uint64_t orig_perm, orig_shared_perm;
        blk_get_perm(blk, &orig_perm, &orig_shared_perm);

        if (ct->perm & ~orig_perm) {
            uint64_t new_perm;
            Error *local_err = NULL;
            int ret;

            new_perm = orig_perm | ct->perm;

            ret = blk_set_perm(blk, new_perm, orig_shared_perm, &local_err);
            if (ret < 0) {
                error_report_err(local_err);
                return 0;
            }
        }
    }

    optind = 0;
    return ct->cfunc(blk, argc, argv);
}

static const cmdinfo_t *find_command(const char *cmd)
{
    cmdinfo_t *ct;

    for (ct = cmdtab; ct < &cmdtab[ncmds]; ct++) {
        if (strcmp(ct->name, cmd) == 0 ||
            (ct->altname && strcmp(ct->altname, cmd) == 0))
        {
            return (const cmdinfo_t *)ct;
        }
    }
    return NULL;
}

/* Invoke fn() for commands with a matching prefix */
void qemuio_complete_command(const char *input,
                             void (*fn)(const char *cmd, void *opaque),
                             void *opaque)
{
    cmdinfo_t *ct;
    size_t input_len = strlen(input);

    for (ct = cmdtab; ct < &cmdtab[ncmds]; ct++) {
        if (strncmp(input, ct->name, input_len) == 0) {
            fn(ct->name, opaque);
        }
    }
}

static char **breakline(char *input, int *count)
{
    int c = 0;
    char *p;
    char **rval = g_new0(char *, 1);

    while (rval && (p = qemu_strsep(&input, " ")) != NULL) {
        if (!*p) {
            continue;
        }
        c++;
        rval = g_renew(char *, rval, (c + 1));
        rval[c - 1] = p;
        rval[c] = NULL;
    }
    *count = c;
    return rval;
}

static int64_t cvtnum(const char *s)
{
    int err;
    uint64_t value;

    err = qemu_strtosz(s, NULL, &value);
    if (err < 0) {
        return err;
    }
    if (value > INT64_MAX) {
        return -ERANGE;
    }
    return value;
}

static void print_cvtnum_err(int64_t rc, const char *arg)
{
    switch (rc) {
    case -EINVAL:
        printf("Parsing error: non-numeric argument,"
               " or extraneous/unrecognized suffix -- %s\n", arg);
        break;
    case -ERANGE:
        printf("Parsing error: argument too large -- %s\n", arg);
        break;
    default:
        printf("Parsing error: %s\n", arg);
    }
}

#define EXABYTES(x)     ((long long)(x) << 60)
#define PETABYTES(x)    ((long long)(x) << 50)
#define TERABYTES(x)    ((long long)(x) << 40)
#define GIGABYTES(x)    ((long long)(x) << 30)
#define MEGABYTES(x)    ((long long)(x) << 20)
#define KILOBYTES(x)    ((long long)(x) << 10)

#define TO_EXABYTES(x)  ((x) / EXABYTES(1))
#define TO_PETABYTES(x) ((x) / PETABYTES(1))
#define TO_TERABYTES(x) ((x) / TERABYTES(1))
#define TO_GIGABYTES(x) ((x) / GIGABYTES(1))
#define TO_MEGABYTES(x) ((x) / MEGABYTES(1))
#define TO_KILOBYTES(x) ((x) / KILOBYTES(1))

static void cvtstr(double value, char *str, size_t size)
{
    char *trim;
    const char *suffix;

    if (value >= EXABYTES(1)) {
        suffix = " EiB";
        snprintf(str, size - 4, "%.3f", TO_EXABYTES(value));
    } else if (value >= PETABYTES(1)) {
        suffix = " PiB";
        snprintf(str, size - 4, "%.3f", TO_PETABYTES(value));
    } else if (value >= TERABYTES(1)) {
        suffix = " TiB";
        snprintf(str, size - 4, "%.3f", TO_TERABYTES(value));
    } else if (value >= GIGABYTES(1)) {
        suffix = " GiB";
        snprintf(str, size - 4, "%.3f", TO_GIGABYTES(value));
    } else if (value >= MEGABYTES(1)) {
        suffix = " MiB";
        snprintf(str, size - 4, "%.3f", TO_MEGABYTES(value));
    } else if (value >= KILOBYTES(1)) {
        suffix = " KiB";
        snprintf(str, size - 4, "%.3f", TO_KILOBYTES(value));
    } else {
        suffix = " bytes";
        snprintf(str, size - 6, "%f", value);
    }

    trim = strstr(str, ".000");
    if (trim) {
        strcpy(trim, suffix);
    } else {
        strcat(str, suffix);
    }
}



static struct timeval tsub(struct timeval t1, struct timeval t2)
{
    t1.tv_usec -= t2.tv_usec;
    if (t1.tv_usec < 0) {
        t1.tv_usec += 1000000;
        t1.tv_sec--;
    }
    t1.tv_sec -= t2.tv_sec;
    return t1;
}

static double tdiv(double value, struct timeval tv)
{
    return value / ((double)tv.tv_sec + ((double)tv.tv_usec / 1000000.0));
}

#define HOURS(sec)      ((sec) / (60 * 60))
#define MINUTES(sec)    (((sec) % (60 * 60)) / 60)
#define SECONDS(sec)    ((sec) % 60)

enum {
    DEFAULT_TIME        = 0x0,
    TERSE_FIXED_TIME    = 0x1,
    VERBOSE_FIXED_TIME  = 0x2,
};

static void timestr(struct timeval *tv, char *ts, size_t size, int format)
{
    double usec = (double)tv->tv_usec / 1000000.0;

    if (format & TERSE_FIXED_TIME) {
        if (!HOURS(tv->tv_sec)) {
            snprintf(ts, size, "%u:%02u.%02u",
                    (unsigned int) MINUTES(tv->tv_sec),
                    (unsigned int) SECONDS(tv->tv_sec),
                    (unsigned int) (usec * 100));
            return;
        }
        format |= VERBOSE_FIXED_TIME; /* fallback if hours needed */
    }

    if ((format & VERBOSE_FIXED_TIME) || tv->tv_sec) {
        snprintf(ts, size, "%u:%02u:%02u.%02u",
                (unsigned int) HOURS(tv->tv_sec),
                (unsigned int) MINUTES(tv->tv_sec),
                (unsigned int) SECONDS(tv->tv_sec),
                (unsigned int) (usec * 100));
    } else {
        snprintf(ts, size, "0.%04u sec", (unsigned int) (usec * 10000));
    }
}

/*
 * Parse the pattern argument to various sub-commands.
 *
 * Because the pattern is used as an argument to memset it must evaluate
 * to an unsigned integer that fits into a single byte.
 */
static int parse_pattern(const char *arg)
{
    char *endptr = NULL;
    long pattern;

    pattern = strtol(arg, &endptr, 0);
    if (pattern < 0 || pattern > UCHAR_MAX || *endptr != '\0') {
        printf("%s is not a valid pattern byte\n", arg);
        return -1;
    }

    return pattern;
}

/*
 * Memory allocation helpers.
 *
 * Make sure memory is aligned by default, or purposefully misaligned if
 * that is specified on the command line.
 */

#define MISALIGN_OFFSET     16
static void *qemu_io_alloc(BlockBackend *blk, size_t len, int pattern)
{
    void *buf;

    if (qemuio_misalign) {
        len += MISALIGN_OFFSET;
    }
    buf = blk_blockalign(blk, len);
    memset(buf, pattern, len);
    if (qemuio_misalign) {
        buf += MISALIGN_OFFSET;
    }
    return buf;
}

static void qemu_io_free(void *p)
{
    if (qemuio_misalign) {
        p -= MISALIGN_OFFSET;
    }
    qemu_vfree(p);
}

static void dump_buffer(const void *buffer, int64_t offset, int64_t len)
{
    uint64_t i;
    int j;
    const uint8_t *p;

    for (i = 0, p = buffer; i < len; i += 16) {
        const uint8_t *s = p;

        printf("%08" PRIx64 ":  ", offset + i);
        for (j = 0; j < 16 && i + j < len; j++, p++) {
            printf("%02x ", *p);
        }
        printf(" ");
        for (j = 0; j < 16 && i + j < len; j++, s++) {
            if (isalnum(*s)) {
                printf("%c", *s);
            } else {
                printf(".");
            }
        }
        printf("\n");
    }
}

static void print_report(const char *op, struct timeval *t, int64_t offset,
                         int64_t count, int64_t total, int cnt, bool Cflag)
{
    char s1[64], s2[64], ts[64];

    timestr(t, ts, sizeof(ts), Cflag ? VERBOSE_FIXED_TIME : 0);
    if (!Cflag) {
        cvtstr((double)total, s1, sizeof(s1));
        cvtstr(tdiv((double)total, *t), s2, sizeof(s2));
        printf("%s %"PRId64"/%"PRId64" bytes at offset %" PRId64 "\n",
               op, total, count, offset);
        printf("%s, %d ops; %s (%s/sec and %.4f ops/sec)\n",
               s1, cnt, ts, s2, tdiv((double)cnt, *t));
    } else {/* bytes,ops,time,bytes/sec,ops/sec */
        printf("%"PRId64",%d,%s,%.3f,%.3f\n",
            total, cnt, ts,
            tdiv((double)total, *t),
            tdiv((double)cnt, *t));
    }
}

/*
 * Parse multiple length statements for vectored I/O, and construct an I/O
 * vector matching it.
 */
static void *
create_iovec(BlockBackend *blk, QEMUIOVector *qiov, char **argv, int nr_iov,
             int pattern)
{
    size_t *sizes = g_new0(size_t, nr_iov);
    size_t count = 0;
    void *buf = NULL;
    void *p;
    int i;

    for (i = 0; i < nr_iov; i++) {
        char *arg = argv[i];
        int64_t len;

        len = cvtnum(arg);
        if (len < 0) {
            print_cvtnum_err(len, arg);
            goto fail;
        }

        if (len > BDRV_REQUEST_MAX_BYTES) {
            printf("Argument '%s' exceeds maximum size %" PRIu64 "\n", arg,
                   (uint64_t)BDRV_REQUEST_MAX_BYTES);
            goto fail;
        }

        if (count > BDRV_REQUEST_MAX_BYTES - len) {
            printf("The total number of bytes exceed the maximum size %" PRIu64
                   "\n", (uint64_t)BDRV_REQUEST_MAX_BYTES);
            goto fail;
        }

        sizes[i] = len;
        count += len;
    }

    qemu_iovec_init(qiov, nr_iov);

    buf = p = qemu_io_alloc(blk, count, pattern);

    for (i = 0; i < nr_iov; i++) {
        qemu_iovec_add(qiov, p, sizes[i]);
        p += sizes[i];
    }

fail:
    g_free(sizes);
    return buf;
}

static int do_pread(BlockBackend *blk, char *buf, int64_t offset,
                    int64_t bytes, int64_t *total)
{
    if (bytes > INT_MAX) {
        return -ERANGE;
    }

    *total = blk_pread(blk, offset, (uint8_t *)buf, bytes);
    if (*total < 0) {
        return *total;
    }
    return 1;
}

static int do_pwrite(BlockBackend *blk, char *buf, int64_t offset,
                     int64_t bytes, int flags, int64_t *total)
{
    if (bytes > INT_MAX) {
        return -ERANGE;
    }

    *total = blk_pwrite(blk, offset, (uint8_t *)buf, bytes, flags);
    if (*total < 0) {
        return *total;
    }
    return 1;
}

typedef struct {
    BlockBackend *blk;
    int64_t offset;
    int64_t bytes;
    int64_t *total;
    int flags;
    int ret;
    bool done;
} CoWriteZeroes;

static void coroutine_fn co_pwrite_zeroes_entry(void *opaque)
{
    CoWriteZeroes *data = opaque;

    data->ret = blk_co_pwrite_zeroes(data->blk, data->offset, data->bytes,
                                     data->flags);
    data->done = true;
    if (data->ret < 0) {
        *data->total = data->ret;
        return;
    }

    *data->total = data->bytes;
}

static int do_co_pwrite_zeroes(BlockBackend *blk, int64_t offset,
                               int64_t bytes, int flags, int64_t *total)
{
    Coroutine *co;
    CoWriteZeroes data = {
        .blk    = blk,
        .offset = offset,
        .bytes  = bytes,
        .total  = total,
        .flags  = flags,
        .done   = false,
    };

    if (bytes > INT_MAX) {
        return -ERANGE;
    }

    co = qemu_coroutine_create(co_pwrite_zeroes_entry, &data);
    bdrv_coroutine_enter(blk_bs(blk), co);
    while (!data.done) {
        aio_poll(blk_get_aio_context(blk), true);
    }
    if (data.ret < 0) {
        return data.ret;
    } else {
        return 1;
    }
}

static int do_write_compressed(BlockBackend *blk, char *buf, int64_t offset,
                               int64_t bytes, int64_t *total)
{
    int ret;

    if (bytes >> 9 > BDRV_REQUEST_MAX_SECTORS) {
        return -ERANGE;
    }

    ret = blk_pwrite_compressed(blk, offset, buf, bytes);
    if (ret < 0) {
        return ret;
    }
    *total = bytes;
    return 1;
}

static int do_load_vmstate(BlockBackend *blk, char *buf, int64_t offset,
                           int64_t count, int64_t *total)
{
    if (count > INT_MAX) {
        return -ERANGE;
    }

    *total = blk_load_vmstate(blk, (uint8_t *)buf, offset, count);
    if (*total < 0) {
        return *total;
    }
    return 1;
}

static int do_save_vmstate(BlockBackend *blk, char *buf, int64_t offset,
                           int64_t count, int64_t *total)
{
    if (count > INT_MAX) {
        return -ERANGE;
    }

    *total = blk_save_vmstate(blk, (uint8_t *)buf, offset, count);
    if (*total < 0) {
        return *total;
    }
    return 1;
}

#define NOT_DONE 0x7fffffff
static void aio_rw_done(void *opaque, int ret)
{
    *(int *)opaque = ret;
}

static int do_aio_readv(BlockBackend *blk, QEMUIOVector *qiov,
                        int64_t offset, int *total)
{
    int async_ret = NOT_DONE;

    blk_aio_preadv(blk, offset, qiov, 0, aio_rw_done, &async_ret);
    while (async_ret == NOT_DONE) {
        main_loop_wait(false);
    }

    *total = qiov->size;
    return async_ret < 0 ? async_ret : 1;
}

static int do_aio_writev(BlockBackend *blk, QEMUIOVector *qiov,
                         int64_t offset, int flags, int *total)
{
    int async_ret = NOT_DONE;

    blk_aio_pwritev(blk, offset, qiov, flags, aio_rw_done, &async_ret);
    while (async_ret == NOT_DONE) {
        main_loop_wait(false);
    }

    *total = qiov->size;
    return async_ret < 0 ? async_ret : 1;
}

static void read_help(void)
{
    printf(
"\n"
" reads a range of bytes from the given offset\n"
"\n"
" Example:\n"
" 'read -v 512 1k' - dumps 1 kilobyte read from 512 bytes into the file\n"
"\n"
" Reads a segment of the currently open file, optionally dumping it to the\n"
" standard output stream (with -v option) for subsequent inspection.\n"
" -b, -- read from the VM state rather than the virtual disk\n"
" -C, -- report statistics in a machine parsable format\n"
" -l, -- length for pattern verification (only with -P)\n"
" -p, -- ignored for backwards compatibility\n"
" -P, -- use a pattern to verify read data\n"
" -q, -- quiet mode, do not show I/O statistics\n"
" -s, -- start offset for pattern verification (only with -P)\n"
" -v, -- dump buffer to standard output\n"
"\n");
}

static int read_f(BlockBackend *blk, int argc, char **argv);

static const cmdinfo_t read_cmd = {
    .name       = "read",
    .altname    = "r",
    .cfunc      = read_f,
    .argmin     = 2,
    .argmax     = -1,
    .args       = "[-abCqv] [-P pattern [-s off] [-l len]] off len",
    .oneline    = "reads a number of bytes at a specified offset",
    .help       = read_help,
};

static int read_f(BlockBackend *blk, int argc, char **argv)
{
    struct timeval t1, t2;
    bool Cflag = false, qflag = false, vflag = false;
    bool Pflag = false, sflag = false, lflag = false, bflag = false;
    int c, cnt;
    char *buf;
    int64_t offset;
    int64_t count;
    /* Some compilers get confused and warn if this is not initialized.  */
    int64_t total = 0;
    int pattern = 0;
    int64_t pattern_offset = 0, pattern_count = 0;

    while ((c = getopt(argc, argv, "bCl:pP:qs:v")) != -1) {
        switch (c) {
        case 'b':
            bflag = true;
            break;
        case 'C':
            Cflag = true;
            break;
        case 'l':
            lflag = true;
            pattern_count = cvtnum(optarg);
            if (pattern_count < 0) {
                print_cvtnum_err(pattern_count, optarg);
                return 0;
            }
            break;
        case 'p':
            /* Ignored for backwards compatibility */
            break;
        case 'P':
            Pflag = true;
            pattern = parse_pattern(optarg);
            if (pattern < 0) {
                return 0;
            }
            break;
        case 'q':
            qflag = true;
            break;
        case 's':
            sflag = true;
            pattern_offset = cvtnum(optarg);
            if (pattern_offset < 0) {
                print_cvtnum_err(pattern_offset, optarg);
                return 0;
            }
            break;
        case 'v':
            vflag = true;
            break;
        default:
            return qemuio_command_usage(&read_cmd);
        }
    }

    if (optind != argc - 2) {
        return qemuio_command_usage(&read_cmd);
    }

    offset = cvtnum(argv[optind]);
    if (offset < 0) {
        print_cvtnum_err(offset, argv[optind]);
        return 0;
    }

    optind++;
    count = cvtnum(argv[optind]);
    if (count < 0) {
        print_cvtnum_err(count, argv[optind]);
        return 0;
    } else if (count > BDRV_REQUEST_MAX_BYTES) {
        printf("length cannot exceed %" PRIu64 ", given %s\n",
               (uint64_t)BDRV_REQUEST_MAX_BYTES, argv[optind]);
        return 0;
    }

    if (!Pflag && (lflag || sflag)) {
        return qemuio_command_usage(&read_cmd);
    }

    if (!lflag) {
        pattern_count = count - pattern_offset;
    }

    if ((pattern_count < 0) || (pattern_count + pattern_offset > count))  {
        printf("pattern verification range exceeds end of read data\n");
        return 0;
    }

    if (bflag) {
        if (!QEMU_IS_ALIGNED(offset, BDRV_SECTOR_SIZE)) {
            printf("%" PRId64 " is not a sector-aligned value for 'offset'\n",
                   offset);
            return 0;
        }
        if (!QEMU_IS_ALIGNED(count, BDRV_SECTOR_SIZE)) {
            printf("%"PRId64" is not a sector-aligned value for 'count'\n",
                   count);
            return 0;
        }
    }

    buf = qemu_io_alloc(blk, count, 0xab);

    gettimeofday(&t1, NULL);
    if (bflag) {
        cnt = do_load_vmstate(blk, buf, offset, count, &total);
    } else {
        cnt = do_pread(blk, buf, offset, count, &total);
    }
    gettimeofday(&t2, NULL);

    if (cnt < 0) {
        printf("read failed: %s\n", strerror(-cnt));
        goto out;
    }

    if (Pflag) {
        void *cmp_buf = g_malloc(pattern_count);
        memset(cmp_buf, pattern, pattern_count);
        if (memcmp(buf + pattern_offset, cmp_buf, pattern_count)) {
            printf("Pattern verification failed at offset %"
                   PRId64 ", %"PRId64" bytes\n",
                   offset + pattern_offset, pattern_count);
        }
        g_free(cmp_buf);
    }

    if (qflag) {
        goto out;
    }

    if (vflag) {
        dump_buffer(buf, offset, count);
    }

    /* Finally, report back -- -C gives a parsable format */
    t2 = tsub(t2, t1);
    print_report("read", &t2, offset, count, total, cnt, Cflag);

out:
    qemu_io_free(buf);

    return 0;
}

static void readv_help(void)
{
    printf(
"\n"
" reads a range of bytes from the given offset into multiple buffers\n"
"\n"
" Example:\n"
" 'readv -v 512 1k 1k ' - dumps 2 kilobytes read from 512 bytes into the file\n"
"\n"
" Reads a segment of the currently open file, optionally dumping it to the\n"
" standard output stream (with -v option) for subsequent inspection.\n"
" Uses multiple iovec buffers if more than one byte range is specified.\n"
" -C, -- report statistics in a machine parsable format\n"
" -P, -- use a pattern to verify read data\n"
" -v, -- dump buffer to standard output\n"
" -q, -- quiet mode, do not show I/O statistics\n"
"\n");
}

static int readv_f(BlockBackend *blk, int argc, char **argv);

static const cmdinfo_t readv_cmd = {
    .name       = "readv",
    .cfunc      = readv_f,
    .argmin     = 2,
    .argmax     = -1,
    .args       = "[-Cqv] [-P pattern] off len [len..]",
    .oneline    = "reads a number of bytes at a specified offset",
    .help       = readv_help,
};

static int readv_f(BlockBackend *blk, int argc, char **argv)
{
    struct timeval t1, t2;
    bool Cflag = false, qflag = false, vflag = false;
    int c, cnt;
    char *buf;
    int64_t offset;
    /* Some compilers get confused and warn if this is not initialized.  */
    int total = 0;
    int nr_iov;
    QEMUIOVector qiov;
    int pattern = 0;
    bool Pflag = false;

    while ((c = getopt(argc, argv, "CP:qv")) != -1) {
        switch (c) {
        case 'C':
            Cflag = true;
            break;
        case 'P':
            Pflag = true;
            pattern = parse_pattern(optarg);
            if (pattern < 0) {
                return 0;
            }
            break;
        case 'q':
            qflag = true;
            break;
        case 'v':
            vflag = true;
            break;
        default:
            return qemuio_command_usage(&readv_cmd);
        }
    }

    if (optind > argc - 2) {
        return qemuio_command_usage(&readv_cmd);
    }


    offset = cvtnum(argv[optind]);
    if (offset < 0) {
        print_cvtnum_err(offset, argv[optind]);
        return 0;
    }
    optind++;

    nr_iov = argc - optind;
    buf = create_iovec(blk, &qiov, &argv[optind], nr_iov, 0xab);
    if (buf == NULL) {
        return 0;
    }

    gettimeofday(&t1, NULL);
    cnt = do_aio_readv(blk, &qiov, offset, &total);
    gettimeofday(&t2, NULL);

    if (cnt < 0) {
        printf("readv failed: %s\n", strerror(-cnt));
        goto out;
    }

    if (Pflag) {
        void *cmp_buf = g_malloc(qiov.size);
        memset(cmp_buf, pattern, qiov.size);
        if (memcmp(buf, cmp_buf, qiov.size)) {
            printf("Pattern verification failed at offset %"
                   PRId64 ", %zd bytes\n", offset, qiov.size);
        }
        g_free(cmp_buf);
    }

    if (qflag) {
        goto out;
    }

    if (vflag) {
        dump_buffer(buf, offset, qiov.size);
    }

    /* Finally, report back -- -C gives a parsable format */
    t2 = tsub(t2, t1);
    print_report("read", &t2, offset, qiov.size, total, cnt, Cflag);

out:
    qemu_iovec_destroy(&qiov);
    qemu_io_free(buf);
    return 0;
}

static void write_help(void)
{
    printf(
"\n"
" writes a range of bytes from the given offset\n"
"\n"
" Example:\n"
" 'write 512 1k' - writes 1 kilobyte at 512 bytes into the open file\n"
"\n"
" Writes into a segment of the currently open file, using a buffer\n"
" filled with a set pattern (0xcdcdcdcd).\n"
" -b, -- write to the VM state rather than the virtual disk\n"
" -c, -- write compressed data with blk_write_compressed\n"
" -f, -- use Force Unit Access semantics\n"
" -p, -- ignored for backwards compatibility\n"
" -P, -- use different pattern to fill file\n"
" -C, -- report statistics in a machine parsable format\n"
" -q, -- quiet mode, do not show I/O statistics\n"
" -u, -- with -z, allow unmapping\n"
" -z, -- write zeroes using blk_co_pwrite_zeroes\n"
"\n");
}

static int write_f(BlockBackend *blk, int argc, char **argv);

static const cmdinfo_t write_cmd = {
    .name       = "write",
    .altname    = "w",
    .cfunc      = write_f,
    .perm       = BLK_PERM_WRITE,
    .argmin     = 2,
    .argmax     = -1,
    .args       = "[-bcCfquz] [-P pattern] off len",
    .oneline    = "writes a number of bytes at a specified offset",
    .help       = write_help,
};

static int write_f(BlockBackend *blk, int argc, char **argv)
{
    struct timeval t1, t2;
    bool Cflag = false, qflag = false, bflag = false;
    bool Pflag = false, zflag = false, cflag = false;
    int flags = 0;
    int c, cnt;
    char *buf = NULL;
    int64_t offset;
    int64_t count;
    /* Some compilers get confused and warn if this is not initialized.  */
    int64_t total = 0;
    int pattern = 0xcd;

    while ((c = getopt(argc, argv, "bcCfpP:quz")) != -1) {
        switch (c) {
        case 'b':
            bflag = true;
            break;
        case 'c':
            cflag = true;
            break;
        case 'C':
            Cflag = true;
            break;
        case 'f':
            flags |= BDRV_REQ_FUA;
            break;
        case 'p':
            /* Ignored for backwards compatibility */
            break;
        case 'P':
            Pflag = true;
            pattern = parse_pattern(optarg);
            if (pattern < 0) {
                return 0;
            }
            break;
        case 'q':
            qflag = true;
            break;
        case 'u':
            flags |= BDRV_REQ_MAY_UNMAP;
            break;
        case 'z':
            zflag = true;
            break;
        default:
            return qemuio_command_usage(&write_cmd);
        }
    }

    if (optind != argc - 2) {
        return qemuio_command_usage(&write_cmd);
    }

    if (bflag && zflag) {
        printf("-b and -z cannot be specified at the same time\n");
        return 0;
    }

    if ((flags & BDRV_REQ_FUA) && (bflag || cflag)) {
        printf("-f and -b or -c cannot be specified at the same time\n");
        return 0;
    }

    if ((flags & BDRV_REQ_MAY_UNMAP) && !zflag) {
        printf("-u requires -z to be specified\n");
        return 0;
    }

    if (zflag && Pflag) {
        printf("-z and -P cannot be specified at the same time\n");
        return 0;
    }

    offset = cvtnum(argv[optind]);
    if (offset < 0) {
        print_cvtnum_err(offset, argv[optind]);
        return 0;
    }

    optind++;
    count = cvtnum(argv[optind]);
    if (count < 0) {
        print_cvtnum_err(count, argv[optind]);
        return 0;
    } else if (count > BDRV_REQUEST_MAX_BYTES) {
        printf("length cannot exceed %" PRIu64 ", given %s\n",
               (uint64_t)BDRV_REQUEST_MAX_BYTES, argv[optind]);
        return 0;
    }

    if (bflag || cflag) {
        if (!QEMU_IS_ALIGNED(offset, BDRV_SECTOR_SIZE)) {
            printf("%" PRId64 " is not a sector-aligned value for 'offset'\n",
                   offset);
            return 0;
        }

        if (!QEMU_IS_ALIGNED(count, BDRV_SECTOR_SIZE)) {
            printf("%"PRId64" is not a sector-aligned value for 'count'\n",
                   count);
            return 0;
        }
    }

    if (!zflag) {
        buf = qemu_io_alloc(blk, count, pattern);
    }

    gettimeofday(&t1, NULL);
    if (bflag) {
        cnt = do_save_vmstate(blk, buf, offset, count, &total);
    } else if (zflag) {
        cnt = do_co_pwrite_zeroes(blk, offset, count, flags, &total);
    } else if (cflag) {
        cnt = do_write_compressed(blk, buf, offset, count, &total);
    } else {
        cnt = do_pwrite(blk, buf, offset, count, flags, &total);
    }
    gettimeofday(&t2, NULL);

    if (cnt < 0) {
        printf("write failed: %s\n", strerror(-cnt));
        goto out;
    }

    if (qflag) {
        goto out;
    }

    /* Finally, report back -- -C gives a parsable format */
    t2 = tsub(t2, t1);
    print_report("wrote", &t2, offset, count, total, cnt, Cflag);

out:
    if (!zflag) {
        qemu_io_free(buf);
    }

    return 0;
}

static void
writev_help(void)
{
    printf(
"\n"
" writes a range of bytes from the given offset source from multiple buffers\n"
"\n"
" Example:\n"
" 'writev 512 1k 1k' - writes 2 kilobytes at 512 bytes into the open file\n"
"\n"
" Writes into a segment of the currently open file, using a buffer\n"
" filled with a set pattern (0xcdcdcdcd).\n"
" -P, -- use different pattern to fill file\n"
" -C, -- report statistics in a machine parsable format\n"
" -f, -- use Force Unit Access semantics\n"
" -q, -- quiet mode, do not show I/O statistics\n"
"\n");
}

static int writev_f(BlockBackend *blk, int argc, char **argv);

static const cmdinfo_t writev_cmd = {
    .name       = "writev",
    .cfunc      = writev_f,
    .perm       = BLK_PERM_WRITE,
    .argmin     = 2,
    .argmax     = -1,
    .args       = "[-Cfq] [-P pattern] off len [len..]",
    .oneline    = "writes a number of bytes at a specified offset",
    .help       = writev_help,
};

static int writev_f(BlockBackend *blk, int argc, char **argv)
{
    struct timeval t1, t2;
    bool Cflag = false, qflag = false;
    int flags = 0;
    int c, cnt;
    char *buf;
    int64_t offset;
    /* Some compilers get confused and warn if this is not initialized.  */
    int total = 0;
    int nr_iov;
    int pattern = 0xcd;
    QEMUIOVector qiov;

    while ((c = getopt(argc, argv, "CfqP:")) != -1) {
        switch (c) {
        case 'C':
            Cflag = true;
            break;
        case 'f':
            flags |= BDRV_REQ_FUA;
            break;
        case 'q':
            qflag = true;
            break;
        case 'P':
            pattern = parse_pattern(optarg);
            if (pattern < 0) {
                return 0;
            }
            break;
        default:
            return qemuio_command_usage(&writev_cmd);
        }
    }

    if (optind > argc - 2) {
        return qemuio_command_usage(&writev_cmd);
    }

    offset = cvtnum(argv[optind]);
    if (offset < 0) {
        print_cvtnum_err(offset, argv[optind]);
        return 0;
    }
    optind++;

    nr_iov = argc - optind;
    buf = create_iovec(blk, &qiov, &argv[optind], nr_iov, pattern);
    if (buf == NULL) {
        return 0;
    }

    gettimeofday(&t1, NULL);
    cnt = do_aio_writev(blk, &qiov, offset, flags, &total);
    gettimeofday(&t2, NULL);

    if (cnt < 0) {
        printf("writev failed: %s\n", strerror(-cnt));
        goto out;
    }

    if (qflag) {
        goto out;
    }

    /* Finally, report back -- -C gives a parsable format */
    t2 = tsub(t2, t1);
    print_report("wrote", &t2, offset, qiov.size, total, cnt, Cflag);
out:
    qemu_iovec_destroy(&qiov);
    qemu_io_free(buf);
    return 0;
}

struct aio_ctx {
    BlockBackend *blk;
    QEMUIOVector qiov;
    int64_t offset;
    char *buf;
    bool qflag;
    bool vflag;
    bool Cflag;
    bool Pflag;
    bool zflag;
    BlockAcctCookie acct;
    int pattern;
    struct timeval t1;
};

static void aio_write_done(void *opaque, int ret)
{
    struct aio_ctx *ctx = opaque;
    struct timeval t2;

    gettimeofday(&t2, NULL);


    if (ret < 0) {
        printf("aio_write failed: %s\n", strerror(-ret));
        block_acct_failed(blk_get_stats(ctx->blk), &ctx->acct);
        goto out;
    }

    block_acct_done(blk_get_stats(ctx->blk), &ctx->acct);

    if (ctx->qflag) {
        goto out;
    }

    /* Finally, report back -- -C gives a parsable format */
    t2 = tsub(t2, ctx->t1);
    print_report("wrote", &t2, ctx->offset, ctx->qiov.size,
                 ctx->qiov.size, 1, ctx->Cflag);
out:
    if (!ctx->zflag) {
        qemu_io_free(ctx->buf);
        qemu_iovec_destroy(&ctx->qiov);
    }
    g_free(ctx);
}

static void aio_read_done(void *opaque, int ret)
{
    struct aio_ctx *ctx = opaque;
    struct timeval t2;

    gettimeofday(&t2, NULL);

    if (ret < 0) {
        printf("readv failed: %s\n", strerror(-ret));
        block_acct_failed(blk_get_stats(ctx->blk), &ctx->acct);
        goto out;
    }

    if (ctx->Pflag) {
        void *cmp_buf = g_malloc(ctx->qiov.size);

        memset(cmp_buf, ctx->pattern, ctx->qiov.size);
        if (memcmp(ctx->buf, cmp_buf, ctx->qiov.size)) {
            printf("Pattern verification failed at offset %"
                   PRId64 ", %zd bytes\n", ctx->offset, ctx->qiov.size);
        }
        g_free(cmp_buf);
    }

    block_acct_done(blk_get_stats(ctx->blk), &ctx->acct);

    if (ctx->qflag) {
        goto out;
    }

    if (ctx->vflag) {
        dump_buffer(ctx->buf, ctx->offset, ctx->qiov.size);
    }

    /* Finally, report back -- -C gives a parsable format */
    t2 = tsub(t2, ctx->t1);
    print_report("read", &t2, ctx->offset, ctx->qiov.size,
                 ctx->qiov.size, 1, ctx->Cflag);
out:
    qemu_io_free(ctx->buf);
    qemu_iovec_destroy(&ctx->qiov);
    g_free(ctx);
}

static void aio_read_help(void)
{
    printf(
"\n"
" asynchronously reads a range of bytes from the given offset\n"
"\n"
" Example:\n"
" 'aio_read -v 512 1k 1k ' - dumps 2 kilobytes read from 512 bytes into the file\n"
"\n"
" Reads a segment of the currently open file, optionally dumping it to the\n"
" standard output stream (with -v option) for subsequent inspection.\n"
" The read is performed asynchronously and the aio_flush command must be\n"
" used to ensure all outstanding aio requests have been completed.\n"
" -C, -- report statistics in a machine parsable format\n"
" -P, -- use a pattern to verify read data\n"
" -i, -- treat request as invalid, for exercising stats\n"
" -v, -- dump buffer to standard output\n"
" -q, -- quiet mode, do not show I/O statistics\n"
"\n");
}

static int aio_read_f(BlockBackend *blk, int argc, char **argv);

static const cmdinfo_t aio_read_cmd = {
    .name       = "aio_read",
    .cfunc      = aio_read_f,
    .argmin     = 2,
    .argmax     = -1,
    .args       = "[-Ciqv] [-P pattern] off len [len..]",
    .oneline    = "asynchronously reads a number of bytes",
    .help       = aio_read_help,
};

static int aio_read_f(BlockBackend *blk, int argc, char **argv)
{
    int nr_iov, c;
    struct aio_ctx *ctx = g_new0(struct aio_ctx, 1);

    ctx->blk = blk;
    while ((c = getopt(argc, argv, "CP:iqv")) != -1) {
        switch (c) {
        case 'C':
            ctx->Cflag = true;
            break;
        case 'P':
            ctx->Pflag = true;
            ctx->pattern = parse_pattern(optarg);
            if (ctx->pattern < 0) {
                g_free(ctx);
                return 0;
            }
            break;
        case 'i':
            printf("injecting invalid read request\n");
            block_acct_invalid(blk_get_stats(blk), BLOCK_ACCT_READ);
            g_free(ctx);
            return 0;
        case 'q':
            ctx->qflag = true;
            break;
        case 'v':
            ctx->vflag = true;
            break;
        default:
            g_free(ctx);
            return qemuio_command_usage(&aio_read_cmd);
        }
    }

    if (optind > argc - 2) {
        g_free(ctx);
        return qemuio_command_usage(&aio_read_cmd);
    }

    ctx->offset = cvtnum(argv[optind]);
    if (ctx->offset < 0) {
        print_cvtnum_err(ctx->offset, argv[optind]);
        g_free(ctx);
        return 0;
    }
    optind++;

    nr_iov = argc - optind;
    ctx->buf = create_iovec(blk, &ctx->qiov, &argv[optind], nr_iov, 0xab);
    if (ctx->buf == NULL) {
        block_acct_invalid(blk_get_stats(blk), BLOCK_ACCT_READ);
        g_free(ctx);
        return 0;
    }

    gettimeofday(&ctx->t1, NULL);
    block_acct_start(blk_get_stats(blk), &ctx->acct, ctx->qiov.size,
                     BLOCK_ACCT_READ);
    blk_aio_preadv(blk, ctx->offset, &ctx->qiov, 0, aio_read_done, ctx);
    return 0;
}

static void aio_write_help(void)
{
    printf(
"\n"
" asynchronously writes a range of bytes from the given offset source\n"
" from multiple buffers\n"
"\n"
" Example:\n"
" 'aio_write 512 1k 1k' - writes 2 kilobytes at 512 bytes into the open file\n"
"\n"
" Writes into a segment of the currently open file, using a buffer\n"
" filled with a set pattern (0xcdcdcdcd).\n"
" The write is performed asynchronously and the aio_flush command must be\n"
" used to ensure all outstanding aio requests have been completed.\n"
" -P, -- use different pattern to fill file\n"
" -C, -- report statistics in a machine parsable format\n"
" -f, -- use Force Unit Access semantics\n"
" -i, -- treat request as invalid, for exercising stats\n"
" -q, -- quiet mode, do not show I/O statistics\n"
" -u, -- with -z, allow unmapping\n"
" -z, -- write zeroes using blk_aio_pwrite_zeroes\n"
"\n");
}

static int aio_write_f(BlockBackend *blk, int argc, char **argv);

static const cmdinfo_t aio_write_cmd = {
    .name       = "aio_write",
    .cfunc      = aio_write_f,
    .perm       = BLK_PERM_WRITE,
    .argmin     = 2,
    .argmax     = -1,
    .args       = "[-Cfiquz] [-P pattern] off len [len..]",
    .oneline    = "asynchronously writes a number of bytes",
    .help       = aio_write_help,
};

static int aio_write_f(BlockBackend *blk, int argc, char **argv)
{
    int nr_iov, c;
    int pattern = 0xcd;
    struct aio_ctx *ctx = g_new0(struct aio_ctx, 1);
    int flags = 0;

    ctx->blk = blk;
    while ((c = getopt(argc, argv, "CfiqP:uz")) != -1) {
        switch (c) {
        case 'C':
            ctx->Cflag = true;
            break;
        case 'f':
            flags |= BDRV_REQ_FUA;
            break;
        case 'q':
            ctx->qflag = true;
            break;
        case 'u':
            flags |= BDRV_REQ_MAY_UNMAP;
            break;
        case 'P':
            pattern = parse_pattern(optarg);
            if (pattern < 0) {
                g_free(ctx);
                return 0;
            }
            break;
        case 'i':
            printf("injecting invalid write request\n");
            block_acct_invalid(blk_get_stats(blk), BLOCK_ACCT_WRITE);
            g_free(ctx);
            return 0;
        case 'z':
            ctx->zflag = true;
            break;
        default:
            g_free(ctx);
            return qemuio_command_usage(&aio_write_cmd);
        }
    }

    if (optind > argc - 2) {
        g_free(ctx);
        return qemuio_command_usage(&aio_write_cmd);
    }

    if (ctx->zflag && optind != argc - 2) {
        printf("-z supports only a single length parameter\n");
        g_free(ctx);
        return 0;
    }

    if ((flags & BDRV_REQ_MAY_UNMAP) && !ctx->zflag) {
        printf("-u requires -z to be specified\n");
        g_free(ctx);
        return 0;
    }

    if (ctx->zflag && ctx->Pflag) {
        printf("-z and -P cannot be specified at the same time\n");
        g_free(ctx);
        return 0;
    }

    ctx->offset = cvtnum(argv[optind]);
    if (ctx->offset < 0) {
        print_cvtnum_err(ctx->offset, argv[optind]);
        g_free(ctx);
        return 0;
    }
    optind++;

    if (ctx->zflag) {
        int64_t count = cvtnum(argv[optind]);
        if (count < 0) {
            print_cvtnum_err(count, argv[optind]);
            g_free(ctx);
            return 0;
        }

        ctx->qiov.size = count;
        blk_aio_pwrite_zeroes(blk, ctx->offset, count, flags, aio_write_done,
                              ctx);
    } else {
        nr_iov = argc - optind;
        ctx->buf = create_iovec(blk, &ctx->qiov, &argv[optind], nr_iov,
                                pattern);
        if (ctx->buf == NULL) {
            block_acct_invalid(blk_get_stats(blk), BLOCK_ACCT_WRITE);
            g_free(ctx);
            return 0;
        }

        gettimeofday(&ctx->t1, NULL);
        block_acct_start(blk_get_stats(blk), &ctx->acct, ctx->qiov.size,
                         BLOCK_ACCT_WRITE);

        blk_aio_pwritev(blk, ctx->offset, &ctx->qiov, flags, aio_write_done,
                        ctx);
    }
    return 0;
}

static int aio_flush_f(BlockBackend *blk, int argc, char **argv)
{
    BlockAcctCookie cookie;
    block_acct_start(blk_get_stats(blk), &cookie, 0, BLOCK_ACCT_FLUSH);
    blk_drain_all();
    block_acct_done(blk_get_stats(blk), &cookie);
    return 0;
}

static const cmdinfo_t aio_flush_cmd = {
    .name       = "aio_flush",
    .cfunc      = aio_flush_f,
    .oneline    = "completes all outstanding aio requests"
};

static int flush_f(BlockBackend *blk, int argc, char **argv)
{
    blk_flush(blk);
    return 0;
}

static const cmdinfo_t flush_cmd = {
    .name       = "flush",
    .altname    = "f",
    .cfunc      = flush_f,
    .oneline    = "flush all in-core file state to disk",
};

static int truncate_f(BlockBackend *blk, int argc, char **argv)
{
    Error *local_err = NULL;
    int64_t offset;
    int ret;

    offset = cvtnum(argv[1]);
    if (offset < 0) {
        print_cvtnum_err(offset, argv[1]);
        return 0;
    }

    ret = blk_truncate(blk, offset, PREALLOC_MODE_OFF, &local_err);
    if (ret < 0) {
        error_report_err(local_err);
        return 0;
    }

    return 0;
}

static const cmdinfo_t truncate_cmd = {
    .name       = "truncate",
    .altname    = "t",
    .cfunc      = truncate_f,
    .perm       = BLK_PERM_WRITE | BLK_PERM_RESIZE,
    .argmin     = 1,
    .argmax     = 1,
    .args       = "off",
    .oneline    = "truncates the current file at the given offset",
};

static int length_f(BlockBackend *blk, int argc, char **argv)
{
    int64_t size;
    char s1[64];

    size = blk_getlength(blk);
    if (size < 0) {
        printf("getlength: %s\n", strerror(-size));
        return 0;
    }

    cvtstr(size, s1, sizeof(s1));
    printf("%s\n", s1);
    return 0;
}


static const cmdinfo_t length_cmd = {
    .name   = "length",
    .altname    = "l",
    .cfunc      = length_f,
    .oneline    = "gets the length of the current file",
};


static int info_f(BlockBackend *blk, int argc, char **argv)
{
    BlockDriverState *bs = blk_bs(blk);
    BlockDriverInfo bdi;
    ImageInfoSpecific *spec_info;
    char s1[64], s2[64];
    int ret;

    if (bs->drv && bs->drv->format_name) {
        printf("format name: %s\n", bs->drv->format_name);
    }
    if (bs->drv && bs->drv->protocol_name) {
        printf("format name: %s\n", bs->drv->protocol_name);
    }

    ret = bdrv_get_info(bs, &bdi);
    if (ret) {
        return 0;
    }

    cvtstr(bdi.cluster_size, s1, sizeof(s1));
    cvtstr(bdi.vm_state_offset, s2, sizeof(s2));

    printf("cluster size: %s\n", s1);
    printf("vm state offset: %s\n", s2);

    spec_info = bdrv_get_specific_info(bs);
    if (spec_info) {
        printf("Format specific information:\n");
        bdrv_image_info_specific_dump(fprintf, stdout, spec_info);
        qapi_free_ImageInfoSpecific(spec_info);
    }

    return 0;
}



static const cmdinfo_t info_cmd = {
    .name       = "info",
    .altname    = "i",
    .cfunc      = info_f,
    .oneline    = "prints information about the current file",
};

static void discard_help(void)
{
    printf(
"\n"
" discards a range of bytes from the given offset\n"
"\n"
" Example:\n"
" 'discard 512 1k' - discards 1 kilobyte from 512 bytes into the file\n"
"\n"
" Discards a segment of the currently open file.\n"
" -C, -- report statistics in a machine parsable format\n"
" -q, -- quiet mode, do not show I/O statistics\n"
"\n");
}

static int discard_f(BlockBackend *blk, int argc, char **argv);

static const cmdinfo_t discard_cmd = {
    .name       = "discard",
    .altname    = "d",
    .cfunc      = discard_f,
    .perm       = BLK_PERM_WRITE,
    .argmin     = 2,
    .argmax     = -1,
    .args       = "[-Cq] off len",
    .oneline    = "discards a number of bytes at a specified offset",
    .help       = discard_help,
};

static int discard_f(BlockBackend *blk, int argc, char **argv)
{
    struct timeval t1, t2;
    bool Cflag = false, qflag = false;
    int c, ret;
    int64_t offset, bytes;

    while ((c = getopt(argc, argv, "Cq")) != -1) {
        switch (c) {
        case 'C':
            Cflag = true;
            break;
        case 'q':
            qflag = true;
            break;
        default:
            return qemuio_command_usage(&discard_cmd);
        }
    }

    if (optind != argc - 2) {
        return qemuio_command_usage(&discard_cmd);
    }

    offset = cvtnum(argv[optind]);
    if (offset < 0) {
        print_cvtnum_err(offset, argv[optind]);
        return 0;
    }

    optind++;
    bytes = cvtnum(argv[optind]);
    if (bytes < 0) {
        print_cvtnum_err(bytes, argv[optind]);
        return 0;
    } else if (bytes >> BDRV_SECTOR_BITS > BDRV_REQUEST_MAX_SECTORS) {
        printf("length cannot exceed %"PRIu64", given %s\n",
               (uint64_t)BDRV_REQUEST_MAX_SECTORS << BDRV_SECTOR_BITS,
               argv[optind]);
        return 0;
    }

    gettimeofday(&t1, NULL);
    ret = blk_pdiscard(blk, offset, bytes);
    gettimeofday(&t2, NULL);

    if (ret < 0) {
        printf("discard failed: %s\n", strerror(-ret));
        goto out;
    }

    /* Finally, report back -- -C gives a parsable format */
    if (!qflag) {
        t2 = tsub(t2, t1);
        print_report("discard", &t2, offset, bytes, bytes, 1, Cflag);
    }

out:
    return 0;
}

static int alloc_f(BlockBackend *blk, int argc, char **argv)
{
    BlockDriverState *bs = blk_bs(blk);
    int64_t offset, start, remaining, count;
    char s1[64];
    int ret;
    int64_t num, sum_alloc;

    start = offset = cvtnum(argv[1]);
    if (offset < 0) {
        print_cvtnum_err(offset, argv[1]);
        return 0;
    } else if (!QEMU_IS_ALIGNED(offset, BDRV_SECTOR_SIZE)) {
        printf("%" PRId64 " is not a sector-aligned value for 'offset'\n",
               offset);
        return 0;
    }

    if (argc == 3) {
        count = cvtnum(argv[2]);
        if (count < 0) {
            print_cvtnum_err(count, argv[2]);
            return 0;
        } else if (count > INT_MAX * BDRV_SECTOR_SIZE) {
            printf("length argument cannot exceed %llu, given %s\n",
                   INT_MAX * BDRV_SECTOR_SIZE, argv[2]);
            return 0;
        }
    } else {
        count = BDRV_SECTOR_SIZE;
    }
    if (!QEMU_IS_ALIGNED(count, BDRV_SECTOR_SIZE)) {
        printf("%" PRId64 " is not a sector-aligned value for 'count'\n",
               count);
        return 0;
    }

    remaining = count;
    sum_alloc = 0;
    while (remaining) {
        ret = bdrv_is_allocated(bs, offset, remaining, &num);
        if (ret < 0) {
            printf("is_allocated failed: %s\n", strerror(-ret));
            return 0;
        }
        offset += num;
        remaining -= num;
        if (ret) {
            sum_alloc += num;
        }
        if (num == 0) {
            count -= remaining;
            remaining = 0;
        }
    }

    cvtstr(start, s1, sizeof(s1));

    printf("%"PRId64"/%"PRId64" bytes allocated at offset %s\n",
           sum_alloc, count, s1);
    return 0;
}

static const cmdinfo_t alloc_cmd = {
    .name       = "alloc",
    .altname    = "a",
    .argmin     = 1,
    .argmax     = 2,
    .cfunc      = alloc_f,
    .args       = "offset [count]",
    .oneline    = "checks if offset is allocated in the file",
};


static int map_is_allocated(BlockDriverState *bs, int64_t offset,
                            int64_t bytes, int64_t *pnum)
{
    int64_t num;
    int num_checked;
    int ret, firstret;

    num_checked = MIN(bytes, BDRV_REQUEST_MAX_BYTES);
    ret = bdrv_is_allocated(bs, offset, num_checked, &num);
    if (ret < 0) {
        return ret;
    }

    firstret = ret;
    *pnum = num;

    while (bytes > 0 && ret == firstret) {
        offset += num;
        bytes -= num;

        num_checked = MIN(bytes, BDRV_REQUEST_MAX_BYTES);
        ret = bdrv_is_allocated(bs, offset, num_checked, &num);
        if (ret == firstret && num) {
            *pnum += num;
        } else {
            break;
        }
    }

    return firstret;
}

static int map_f(BlockBackend *blk, int argc, char **argv)
{
    int64_t offset, bytes;
    char s1[64], s2[64];
    int64_t num;
    int ret;
    const char *retstr;

    offset = 0;
    bytes = blk_getlength(blk);
    if (bytes < 0) {
        error_report("Failed to query image length: %s", strerror(-bytes));
        return 0;
    }

    while (bytes) {
        ret = map_is_allocated(blk_bs(blk), offset, bytes, &num);
        if (ret < 0) {
            error_report("Failed to get allocation status: %s", strerror(-ret));
            return 0;
        } else if (!num) {
            error_report("Unexpected end of image");
            return 0;
        }

        retstr = ret ? "    allocated" : "not allocated";
        cvtstr(num, s1, sizeof(s1));
        cvtstr(offset, s2, sizeof(s2));
        printf("%s (0x%" PRIx64 ") bytes %s at offset %s (0x%" PRIx64 ")\n",
               s1, num, retstr, s2, offset);

        offset += num;
        bytes -= num;
    }

    return 0;
}

static const cmdinfo_t map_cmd = {
       .name           = "map",
       .argmin         = 0,
       .argmax         = 0,
       .cfunc          = map_f,
       .args           = "",
       .oneline        = "prints the allocated areas of a file",
};

static void reopen_help(void)
{
    printf(
"\n"
" Changes the open options of an already opened image\n"
"\n"
" Example:\n"
" 'reopen -o lazy-refcounts=on' - activates lazy refcount writeback on a qcow2 image\n"
"\n"
" -r, -- Reopen the image read-only\n"
" -w, -- Reopen the image read-write\n"
" -c, -- Change the cache mode to the given value\n"
" -o, -- Changes block driver options (cf. 'open' command)\n"
"\n");
}

static int reopen_f(BlockBackend *blk, int argc, char **argv);

static QemuOptsList reopen_opts = {
    .name = "reopen",
    .merge_lists = true,
    .head = QTAILQ_HEAD_INITIALIZER(reopen_opts.head),
    .desc = {
        /* no elements => accept any params */
        { /* end of list */ }
    },
};

static const cmdinfo_t reopen_cmd = {
       .name           = "reopen",
       .argmin         = 0,
       .argmax         = -1,
       .cfunc          = reopen_f,
       .args           = "[(-r|-w)] [-c cache] [-o options]",
       .oneline        = "reopens an image with new options",
       .help           = reopen_help,
};

static int reopen_f(BlockBackend *blk, int argc, char **argv)
{
    BlockDriverState *bs = blk_bs(blk);
    QemuOpts *qopts;
    QDict *opts;
    int c;
    int flags = bs->open_flags;
    bool writethrough = !blk_enable_write_cache(blk);
    bool has_rw_option = false;

    BlockReopenQueue *brq;
    Error *local_err = NULL;

    while ((c = getopt(argc, argv, "c:o:rw")) != -1) {
        switch (c) {
        case 'c':
            if (bdrv_parse_cache_mode(optarg, &flags, &writethrough) < 0) {
                error_report("Invalid cache option: %s", optarg);
                return 0;
            }
            break;
        case 'o':
            if (!qemu_opts_parse_noisily(&reopen_opts, optarg, 0)) {
                qemu_opts_reset(&reopen_opts);
                return 0;
            }
            break;
        case 'r':
            if (has_rw_option) {
                error_report("Only one -r/-w option may be given");
                return 0;
            }
            flags &= ~BDRV_O_RDWR;
            has_rw_option = true;
            break;
        case 'w':
            if (has_rw_option) {
                error_report("Only one -r/-w option may be given");
                return 0;
            }
            flags |= BDRV_O_RDWR;
            has_rw_option = true;
            break;
        default:
            qemu_opts_reset(&reopen_opts);
            return qemuio_command_usage(&reopen_cmd);
        }
    }

    if (optind != argc) {
        qemu_opts_reset(&reopen_opts);
        return qemuio_command_usage(&reopen_cmd);
    }

    if (writethrough != blk_enable_write_cache(blk) &&
        blk_get_attached_dev(blk))
    {
        error_report("Cannot change cache.writeback: Device attached");
        qemu_opts_reset(&reopen_opts);
        return 0;
    }

    qopts = qemu_opts_find(&reopen_opts, NULL);
    opts = qopts ? qemu_opts_to_qdict(qopts, NULL) : NULL;
    qemu_opts_reset(&reopen_opts);

    brq = bdrv_reopen_queue(NULL, bs, opts, flags);
    bdrv_reopen_multiple(bdrv_get_aio_context(bs), brq, &local_err);
    if (local_err) {
        error_report_err(local_err);
    } else {
        blk_set_enable_write_cache(blk, !writethrough);
    }

    return 0;
}

static int break_f(BlockBackend *blk, int argc, char **argv)
{
    int ret;

    ret = bdrv_debug_breakpoint(blk_bs(blk), argv[1], argv[2]);
    if (ret < 0) {
        printf("Could not set breakpoint: %s\n", strerror(-ret));
    }

    return 0;
}

static int remove_break_f(BlockBackend *blk, int argc, char **argv)
{
    int ret;

    ret = bdrv_debug_remove_breakpoint(blk_bs(blk), argv[1]);
    if (ret < 0) {
        printf("Could not remove breakpoint %s: %s\n", argv[1], strerror(-ret));
    }

    return 0;
}

static const cmdinfo_t break_cmd = {
       .name           = "break",
       .argmin         = 2,
       .argmax         = 2,
       .cfunc          = break_f,
       .args           = "event tag",
       .oneline        = "sets a breakpoint on event and tags the stopped "
                         "request as tag",
};

static const cmdinfo_t remove_break_cmd = {
       .name           = "remove_break",
       .argmin         = 1,
       .argmax         = 1,
       .cfunc          = remove_break_f,
       .args           = "tag",
       .oneline        = "remove a breakpoint by tag",
};

static int resume_f(BlockBackend *blk, int argc, char **argv)
{
    int ret;

    ret = bdrv_debug_resume(blk_bs(blk), argv[1]);
    if (ret < 0) {
        printf("Could not resume request: %s\n", strerror(-ret));
    }

    return 0;
}

static const cmdinfo_t resume_cmd = {
       .name           = "resume",
       .argmin         = 1,
       .argmax         = 1,
       .cfunc          = resume_f,
       .args           = "tag",
       .oneline        = "resumes the request tagged as tag",
};

static int wait_break_f(BlockBackend *blk, int argc, char **argv)
{
    while (!bdrv_debug_is_suspended(blk_bs(blk), argv[1])) {
        aio_poll(blk_get_aio_context(blk), true);
    }

    return 0;
}

static const cmdinfo_t wait_break_cmd = {
       .name           = "wait_break",
       .argmin         = 1,
       .argmax         = 1,
       .cfunc          = wait_break_f,
       .args           = "tag",
       .oneline        = "waits for the suspension of a request",
};

static int abort_f(BlockBackend *blk, int argc, char **argv)
{
    abort();
}

static const cmdinfo_t abort_cmd = {
       .name           = "abort",
       .cfunc          = abort_f,
       .flags          = CMD_NOFILE_OK,
       .oneline        = "simulate a program crash using abort(3)",
};

static void sigraise_help(void)
{
    printf(
"\n"
" raises the given signal\n"
"\n"
" Example:\n"
" 'sigraise %i' - raises SIGTERM\n"
"\n"
" Invokes raise(signal), where \"signal\" is the mandatory integer argument\n"
" given to sigraise.\n"
"\n", SIGTERM);
}

static int sigraise_f(BlockBackend *blk, int argc, char **argv);

static const cmdinfo_t sigraise_cmd = {
    .name       = "sigraise",
    .cfunc      = sigraise_f,
    .argmin     = 1,
    .argmax     = 1,
    .flags      = CMD_NOFILE_OK,
    .args       = "signal",
    .oneline    = "raises a signal",
    .help       = sigraise_help,
};

static int sigraise_f(BlockBackend *blk, int argc, char **argv)
{
    int64_t sig = cvtnum(argv[1]);
    if (sig < 0) {
        print_cvtnum_err(sig, argv[1]);
        return 0;
    } else if (sig > NSIG) {
        printf("signal argument '%s' is too large to be a valid signal\n",
               argv[1]);
        return 0;
    }

    /* Using raise() to kill this process does not necessarily flush all open
     * streams. At least stdout and stderr (although the latter should be
     * non-buffered anyway) should be flushed, though. */
    fflush(stdout);
    fflush(stderr);

    raise(sig);
    return 0;
}

static void sleep_cb(void *opaque)
{
    bool *expired = opaque;
    *expired = true;
}

static int sleep_f(BlockBackend *blk, int argc, char **argv)
{
    char *endptr;
    long ms;
    struct QEMUTimer *timer;
    bool expired = false;

    ms = strtol(argv[1], &endptr, 0);
    if (ms < 0 || *endptr != '\0') {
        printf("%s is not a valid number\n", argv[1]);
        return 0;
    }

    timer = timer_new_ns(QEMU_CLOCK_HOST, sleep_cb, &expired);
    timer_mod(timer, qemu_clock_get_ns(QEMU_CLOCK_HOST) + SCALE_MS * ms);

    while (!expired) {
        main_loop_wait(false);
    }

    timer_free(timer);

    return 0;
}

static const cmdinfo_t sleep_cmd = {
       .name           = "sleep",
       .argmin         = 1,
       .argmax         = 1,
       .cfunc          = sleep_f,
       .flags          = CMD_NOFILE_OK,
       .oneline        = "waits for the given value in milliseconds",
};

static void help_oneline(const char *cmd, const cmdinfo_t *ct)
{
    if (cmd) {
        printf("%s ", cmd);
    } else {
        printf("%s ", ct->name);
        if (ct->altname) {
            printf("(or %s) ", ct->altname);
        }
    }

    if (ct->args) {
        printf("%s ", ct->args);
    }
    printf("-- %s\n", ct->oneline);
}

static void help_onecmd(const char *cmd, const cmdinfo_t *ct)
{
    help_oneline(cmd, ct);
    if (ct->help) {
        ct->help();
    }
}

static void help_all(void)
{
    const cmdinfo_t *ct;

    for (ct = cmdtab; ct < &cmdtab[ncmds]; ct++) {
        help_oneline(ct->name, ct);
    }
    printf("\nUse 'help commandname' for extended help.\n");
}

static int help_f(BlockBackend *blk, int argc, char **argv)
{
    const cmdinfo_t *ct;

    if (argc == 1) {
        help_all();
        return 0;
    }

    ct = find_command(argv[1]);
    if (ct == NULL) {
        printf("command %s not found\n", argv[1]);
        return 0;
    }

    help_onecmd(argv[1], ct);
    return 0;
}

static const cmdinfo_t help_cmd = {
    .name       = "help",
    .altname    = "?",
    .cfunc      = help_f,
    .argmin     = 0,
    .argmax     = 1,
    .flags      = CMD_FLAG_GLOBAL,
    .args       = "[command]",
    .oneline    = "help for one or all commands",
};

bool qemuio_command(BlockBackend *blk, const char *cmd)
{
    AioContext *ctx;
    char *input;
    const cmdinfo_t *ct;
    char **v;
    int c;
    bool done = false;

    input = g_strdup(cmd);
    v = breakline(input, &c);
    if (c) {
        ct = find_command(v[0]);
        if (ct) {
            ctx = blk ? blk_get_aio_context(blk) : qemu_get_aio_context();
            aio_context_acquire(ctx);
            done = command(blk, ct, c, v);
            aio_context_release(ctx);
        } else {
            fprintf(stderr, "command \"%s\" not found\n", v[0]);
        }
    }
    g_free(input);
    g_free(v);

    return done;
}

static void __attribute((constructor)) init_qemuio_commands(void)
{
    /* initialize commands */
    qemuio_add_command(&help_cmd);
    qemuio_add_command(&read_cmd);
    qemuio_add_command(&readv_cmd);
    qemuio_add_command(&write_cmd);
    qemuio_add_command(&writev_cmd);
    qemuio_add_command(&aio_read_cmd);
    qemuio_add_command(&aio_write_cmd);
    qemuio_add_command(&aio_flush_cmd);
    qemuio_add_command(&flush_cmd);
    qemuio_add_command(&truncate_cmd);
    qemuio_add_command(&length_cmd);
    qemuio_add_command(&info_cmd);
    qemuio_add_command(&discard_cmd);
    qemuio_add_command(&alloc_cmd);
    qemuio_add_command(&map_cmd);
    qemuio_add_command(&reopen_cmd);
    qemuio_add_command(&break_cmd);
    qemuio_add_command(&remove_break_cmd);
    qemuio_add_command(&resume_cmd);
    qemuio_add_command(&wait_break_cmd);
    qemuio_add_command(&abort_cmd);
    qemuio_add_command(&sleep_cmd);
    qemuio_add_command(&sigraise_cmd);
}

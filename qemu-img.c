/*
 * QEMU disk image utility
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
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
#include "qapi-visit.h"
#include "qapi/qmp-output-visitor.h"
#include "qapi/qmp/qjson.h"
#include "qemu-common.h"
#include "qemu/option.h"
#include "qemu/error-report.h"
#include "qemu/osdep.h"
#include "sysemu/sysemu.h"
#include "block/block_int.h"
#include "block/qapi.h"
#include <getopt.h>
#include <glib.h>

#define QEMU_IMG_VERSION "qemu-img version " QEMU_VERSION \
                          ", Copyright (c) 2004-2008 Fabrice Bellard\n"

typedef struct img_cmd_t {
    const char *name;
    int (*handler)(int argc, char **argv);
} img_cmd_t;

enum {
    OPTION_OUTPUT = 256,
    OPTION_BACKING_CHAIN = 257,
};

typedef enum OutputFormat {
    OFORMAT_JSON,
    OFORMAT_HUMAN,
} OutputFormat;

/* Default to cache=writeback as data integrity is not important for qemu-tcg. */
#define BDRV_O_FLAGS BDRV_O_CACHE_WB
#define BDRV_DEFAULT_CACHE "writeback"

static gint compare_data(gconstpointer a, gconstpointer b, gpointer user)
{
    return g_strcmp0(a, b);
}

static void print_format(gpointer data, gpointer user)
{
    printf(" %s", (char *)data);
}

static void add_format_to_seq(void *opaque, const char *fmt_name)
{
    GSequence *seq = opaque;

    g_sequence_insert_sorted(seq, (gpointer)fmt_name,
                             compare_data, NULL);
}

static void QEMU_NORETURN GCC_FMT_ATTR(1, 2) error_exit(const char *fmt, ...)
{
    va_list ap;

    error_printf("qemu-img: ");

    va_start(ap, fmt);
    error_vprintf(fmt, ap);
    va_end(ap);

    error_printf("\nTry 'qemu-img --help' for more information\n");
    exit(EXIT_FAILURE);
}

/* Please keep in synch with qemu-img.texi */
static void QEMU_NORETURN help(void)
{
    const char *help_msg =
           QEMU_IMG_VERSION
           "usage: qemu-img command [command options]\n"
           "QEMU disk image utility\n"
           "\n"
           "Command syntax:\n"
#define DEF(option, callback, arg_string)        \
           "  " arg_string "\n"
#include "qemu-img-cmds.h"
#undef DEF
#undef GEN_DOCS
           "\n"
           "Command parameters:\n"
           "  'filename' is a disk image filename\n"
           "  'fmt' is the disk image format. It is guessed automatically in most cases\n"
           "  'cache' is the cache mode used to write the output disk image, the valid\n"
           "    options are: 'none', 'writeback' (default, except for convert), 'writethrough',\n"
           "    'directsync' and 'unsafe' (default for convert)\n"
           "  'size' is the disk image size in bytes. Optional suffixes\n"
           "    'k' or 'K' (kilobyte, 1024), 'M' (megabyte, 1024k), 'G' (gigabyte, 1024M),\n"
           "    'T' (terabyte, 1024G), 'P' (petabyte, 1024T) and 'E' (exabyte, 1024P)  are\n"
           "    supported. 'b' is ignored.\n"
           "  'output_filename' is the destination disk image filename\n"
           "  'output_fmt' is the destination format\n"
           "  'options' is a comma separated list of format specific options in a\n"
           "    name=value format. Use -o ? for an overview of the options supported by the\n"
           "    used format\n"
           "  'snapshot_param' is param used for internal snapshot, format\n"
           "    is 'snapshot.id=[ID],snapshot.name=[NAME]', or\n"
           "    '[ID_OR_NAME]'\n"
           "  'snapshot_id_or_name' is deprecated, use 'snapshot_param'\n"
           "    instead\n"
           "  '-c' indicates that target image must be compressed (qcow format only)\n"
           "  '-u' enables unsafe rebasing. It is assumed that old and new backing file\n"
           "       match exactly. The image doesn't need a working backing file before\n"
           "       rebasing in this case (useful for renaming the backing file)\n"
           "  '-h' with or without a command shows this help and lists the supported formats\n"
           "  '-p' show progress of command (only certain commands)\n"
           "  '-q' use Quiet mode - do not print any output (except errors)\n"
           "  '-S' indicates the consecutive number of bytes (defaults to 4k) that must\n"
           "       contain only zeros for qemu-img to create a sparse image during\n"
           "       conversion. If the number of bytes is 0, the source will not be scanned for\n"
           "       unallocated or zero sectors, and the destination image will always be\n"
           "       fully allocated\n"
           "  '--output' takes the format in which the output must be done (human or json)\n"
           "  '-n' skips the target volume creation (useful if the volume is created\n"
           "       prior to running qemu-img)\n"
           "\n"
           "Parameters to check subcommand:\n"
           "  '-r' tries to repair any inconsistencies that are found during the check.\n"
           "       '-r leaks' repairs only cluster leaks, whereas '-r all' fixes all\n"
           "       kinds of errors, with a higher risk of choosing the wrong fix or\n"
           "       hiding corruption that has already occurred.\n"
           "\n"
           "Parameters to snapshot subcommand:\n"
           "  'snapshot' is the name of the snapshot to create, apply or delete\n"
           "  '-a' applies a snapshot (revert disk to saved state)\n"
           "  '-c' creates a snapshot\n"
           "  '-d' deletes a snapshot\n"
           "  '-l' lists all snapshots in the given image\n"
           "\n"
           "Parameters to compare subcommand:\n"
           "  '-f' first image format\n"
           "  '-F' second image format\n"
           "  '-s' run in Strict mode - fail on different image size or sector allocation\n";
    GSequence *seq;

    printf("%s\nSupported formats:", help_msg);
    seq = g_sequence_new(NULL);
    bdrv_iterate_format(add_format_to_seq, seq);
    g_sequence_foreach(seq, print_format, NULL);
    printf("\n");
    g_sequence_free(seq);

    exit(EXIT_SUCCESS);
}

static int GCC_FMT_ATTR(2, 3) qprintf(bool quiet, const char *fmt, ...)
{
    int ret = 0;
    if (!quiet) {
        va_list args;
        va_start(args, fmt);
        ret = vprintf(fmt, args);
        va_end(args);
    }
    return ret;
}

#if defined(WIN32)
/* XXX: put correct support for win32 */
static int read_password(char *buf, int buf_size)
{
    int c, i;
    printf("Password: ");
    fflush(stdout);
    i = 0;
    for(;;) {
        c = getchar();
        if (c == '\n')
            break;
        if (i < (buf_size - 1))
            buf[i++] = c;
    }
    buf[i] = '\0';
    return 0;
}

#else

#include <termios.h>

static struct termios oldtty;

static void term_exit(void)
{
    tcsetattr (0, TCSANOW, &oldtty);
}

static void term_init(void)
{
    struct termios tty;

    tcgetattr (0, &tty);
    oldtty = tty;

    tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP
                          |INLCR|IGNCR|ICRNL|IXON);
    tty.c_oflag |= OPOST;
    tty.c_lflag &= ~(ECHO|ECHONL|ICANON|IEXTEN);
    tty.c_cflag &= ~(CSIZE|PARENB);
    tty.c_cflag |= CS8;
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;

    tcsetattr (0, TCSANOW, &tty);

    atexit(term_exit);
}

static int read_password(char *buf, int buf_size)
{
    uint8_t ch;
    int i, ret;

    printf("password: ");
    fflush(stdout);
    term_init();
    i = 0;
    for(;;) {
        ret = read(0, &ch, 1);
        if (ret == -1) {
            if (errno == EAGAIN || errno == EINTR) {
                continue;
            } else {
                ret = -1;
                break;
            }
        } else if (ret == 0) {
            ret = -1;
            break;
        } else {
            if (ch == '\r') {
                ret = 0;
                break;
            }
            if (i < (buf_size - 1))
                buf[i++] = ch;
        }
    }
    term_exit();
    buf[i] = '\0';
    printf("\n");
    return ret;
}
#endif

static int print_block_option_help(const char *filename, const char *fmt)
{
    BlockDriver *drv, *proto_drv;
    QEMUOptionParameter *create_options = NULL;

    /* Find driver and parse its options */
    drv = bdrv_find_format(fmt);
    if (!drv) {
        error_report("Unknown file format '%s'", fmt);
        return 1;
    }

    create_options = append_option_parameters(create_options,
                                              drv->create_options);

    if (filename) {
        proto_drv = bdrv_find_protocol(filename, true);
        if (!proto_drv) {
            error_report("Unknown protocol '%s'", filename);
            free_option_parameters(create_options);
            return 1;
        }
        create_options = append_option_parameters(create_options,
                                                  proto_drv->create_options);
    }

    print_option_help(create_options);
    free_option_parameters(create_options);
    return 0;
}

static BlockDriverState *bdrv_new_open(const char *id,
                                       const char *filename,
                                       const char *fmt,
                                       int flags,
                                       bool require_io,
                                       bool quiet)
{
    BlockDriverState *bs;
    BlockDriver *drv;
    char password[256];
    Error *local_err = NULL;
    int ret;

    bs = bdrv_new(id, &error_abort);

    if (fmt) {
        drv = bdrv_find_format(fmt);
        if (!drv) {
            error_report("Unknown file format '%s'", fmt);
            goto fail;
        }
    } else {
        drv = NULL;
    }

    ret = bdrv_open(&bs, filename, NULL, NULL, flags, drv, &local_err);
    if (ret < 0) {
        error_report("Could not open '%s': %s", filename,
                     error_get_pretty(local_err));
        error_free(local_err);
        goto fail;
    }

    if (bdrv_is_encrypted(bs) && require_io) {
        qprintf(quiet, "Disk image '%s' is encrypted.\n", filename);
        if (read_password(password, sizeof(password)) < 0) {
            error_report("No password given");
            goto fail;
        }
        if (bdrv_set_key(bs, password) < 0) {
            error_report("invalid password");
            goto fail;
        }
    }
    return bs;
fail:
    bdrv_unref(bs);
    return NULL;
}

static int add_old_style_options(const char *fmt, QEMUOptionParameter *list,
                                 const char *base_filename,
                                 const char *base_fmt)
{
    if (base_filename) {
        if (set_option_parameter(list, BLOCK_OPT_BACKING_FILE, base_filename)) {
            error_report("Backing file not supported for file format '%s'",
                         fmt);
            return -1;
        }
    }
    if (base_fmt) {
        if (set_option_parameter(list, BLOCK_OPT_BACKING_FMT, base_fmt)) {
            error_report("Backing file format not supported for file "
                         "format '%s'", fmt);
            return -1;
        }
    }
    return 0;
}

static int img_create(int argc, char **argv)
{
    int c;
    uint64_t img_size = -1;
    const char *fmt = "raw";
    const char *base_fmt = NULL;
    const char *filename;
    const char *base_filename = NULL;
    char *options = NULL;
    Error *local_err = NULL;
    bool quiet = false;

    for(;;) {
        c = getopt(argc, argv, "F:b:f:he6o:q");
        if (c == -1) {
            break;
        }
        switch(c) {
        case '?':
        case 'h':
            help();
            break;
        case 'F':
            base_fmt = optarg;
            break;
        case 'b':
            base_filename = optarg;
            break;
        case 'f':
            fmt = optarg;
            break;
        case 'e':
            error_report("option -e is deprecated, please use \'-o "
                  "encryption\' instead!");
            goto fail;
        case '6':
            error_report("option -6 is deprecated, please use \'-o "
                  "compat6\' instead!");
            goto fail;
        case 'o':
            if (!is_valid_option_list(optarg)) {
                error_report("Invalid option list: %s", optarg);
                goto fail;
            }
            if (!options) {
                options = g_strdup(optarg);
            } else {
                char *old_options = options;
                options = g_strdup_printf("%s,%s", options, optarg);
                g_free(old_options);
            }
            break;
        case 'q':
            quiet = true;
            break;
        }
    }

    /* Get the filename */
    filename = (optind < argc) ? argv[optind] : NULL;
    if (options && has_help_option(options)) {
        g_free(options);
        return print_block_option_help(filename, fmt);
    }

    if (optind >= argc) {
        error_exit("Expecting image file name");
    }
    optind++;

    /* Get image size, if specified */
    if (optind < argc) {
        int64_t sval;
        char *end;
        sval = strtosz_suffix(argv[optind++], &end, STRTOSZ_DEFSUFFIX_B);
        if (sval < 0 || *end) {
            if (sval == -ERANGE) {
                error_report("Image size must be less than 8 EiB!");
            } else {
                error_report("Invalid image size specified! You may use k, M, "
                      "G, T, P or E suffixes for ");
                error_report("kilobytes, megabytes, gigabytes, terabytes, "
                             "petabytes and exabytes.");
            }
            goto fail;
        }
        img_size = (uint64_t)sval;
    }
    if (optind != argc) {
        error_exit("Unexpected argument: %s", argv[optind]);
    }

    bdrv_img_create(filename, fmt, base_filename, base_fmt,
                    options, img_size, BDRV_O_FLAGS, &local_err, quiet);
    if (local_err) {
        error_report("%s: %s", filename, error_get_pretty(local_err));
        error_free(local_err);
        goto fail;
    }

    g_free(options);
    return 0;

fail:
    g_free(options);
    return 1;
}

static void dump_json_image_check(ImageCheck *check, bool quiet)
{
    Error *local_err = NULL;
    QString *str;
    QmpOutputVisitor *ov = qmp_output_visitor_new();
    QObject *obj;
    visit_type_ImageCheck(qmp_output_get_visitor(ov),
                          &check, NULL, &local_err);
    obj = qmp_output_get_qobject(ov);
    str = qobject_to_json_pretty(obj);
    assert(str != NULL);
    qprintf(quiet, "%s\n", qstring_get_str(str));
    qobject_decref(obj);
    qmp_output_visitor_cleanup(ov);
    QDECREF(str);
}

static void dump_human_image_check(ImageCheck *check, bool quiet)
{
    if (!(check->corruptions || check->leaks || check->check_errors)) {
        qprintf(quiet, "No errors were found on the image.\n");
    } else {
        if (check->corruptions) {
            qprintf(quiet, "\n%" PRId64 " errors were found on the image.\n"
                    "Data may be corrupted, or further writes to the image "
                    "may corrupt it.\n",
                    check->corruptions);
        }

        if (check->leaks) {
            qprintf(quiet,
                    "\n%" PRId64 " leaked clusters were found on the image.\n"
                    "This means waste of disk space, but no harm to data.\n",
                    check->leaks);
        }

        if (check->check_errors) {
            qprintf(quiet,
                    "\n%" PRId64
                    " internal errors have occurred during the check.\n",
                    check->check_errors);
        }
    }

    if (check->total_clusters != 0 && check->allocated_clusters != 0) {
        qprintf(quiet, "%" PRId64 "/%" PRId64 " = %0.2f%% allocated, "
                "%0.2f%% fragmented, %0.2f%% compressed clusters\n",
                check->allocated_clusters, check->total_clusters,
                check->allocated_clusters * 100.0 / check->total_clusters,
                check->fragmented_clusters * 100.0 / check->allocated_clusters,
                check->compressed_clusters * 100.0 /
                check->allocated_clusters);
    }

    if (check->image_end_offset) {
        qprintf(quiet,
                "Image end offset: %" PRId64 "\n", check->image_end_offset);
    }
}

static int collect_image_check(BlockDriverState *bs,
                   ImageCheck *check,
                   const char *filename,
                   const char *fmt,
                   int fix)
{
    int ret;
    BdrvCheckResult result;

    ret = bdrv_check(bs, &result, fix);
    if (ret < 0) {
        return ret;
    }

    check->filename                 = g_strdup(filename);
    check->format                   = g_strdup(bdrv_get_format_name(bs));
    check->check_errors             = result.check_errors;
    check->corruptions              = result.corruptions;
    check->has_corruptions          = result.corruptions != 0;
    check->leaks                    = result.leaks;
    check->has_leaks                = result.leaks != 0;
    check->corruptions_fixed        = result.corruptions_fixed;
    check->has_corruptions_fixed    = result.corruptions != 0;
    check->leaks_fixed              = result.leaks_fixed;
    check->has_leaks_fixed          = result.leaks != 0;
    check->image_end_offset         = result.image_end_offset;
    check->has_image_end_offset     = result.image_end_offset != 0;
    check->total_clusters           = result.bfi.total_clusters;
    check->has_total_clusters       = result.bfi.total_clusters != 0;
    check->allocated_clusters       = result.bfi.allocated_clusters;
    check->has_allocated_clusters   = result.bfi.allocated_clusters != 0;
    check->fragmented_clusters      = result.bfi.fragmented_clusters;
    check->has_fragmented_clusters  = result.bfi.fragmented_clusters != 0;
    check->compressed_clusters      = result.bfi.compressed_clusters;
    check->has_compressed_clusters  = result.bfi.compressed_clusters != 0;

    return 0;
}

/*
 * Checks an image for consistency. Exit codes:
 *
 * 0 - Check completed, image is good
 * 1 - Check not completed because of internal errors
 * 2 - Check completed, image is corrupted
 * 3 - Check completed, image has leaked clusters, but is good otherwise
 */
static int img_check(int argc, char **argv)
{
    int c, ret;
    OutputFormat output_format = OFORMAT_HUMAN;
    const char *filename, *fmt, *output;
    BlockDriverState *bs;
    int fix = 0;
    int flags = BDRV_O_FLAGS | BDRV_O_CHECK;
    ImageCheck *check;
    bool quiet = false;

    fmt = NULL;
    output = NULL;
    for(;;) {
        int option_index = 0;
        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"format", required_argument, 0, 'f'},
            {"repair", required_argument, 0, 'r'},
            {"output", required_argument, 0, OPTION_OUTPUT},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, "f:hr:q",
                        long_options, &option_index);
        if (c == -1) {
            break;
        }
        switch(c) {
        case '?':
        case 'h':
            help();
            break;
        case 'f':
            fmt = optarg;
            break;
        case 'r':
            flags |= BDRV_O_RDWR;

            if (!strcmp(optarg, "leaks")) {
                fix = BDRV_FIX_LEAKS;
            } else if (!strcmp(optarg, "all")) {
                fix = BDRV_FIX_LEAKS | BDRV_FIX_ERRORS;
            } else {
                error_exit("Unknown option value for -r "
                           "(expecting 'leaks' or 'all'): %s", optarg);
            }
            break;
        case OPTION_OUTPUT:
            output = optarg;
            break;
        case 'q':
            quiet = true;
            break;
        }
    }
    if (optind != argc - 1) {
        error_exit("Expecting one image file name");
    }
    filename = argv[optind++];

    if (output && !strcmp(output, "json")) {
        output_format = OFORMAT_JSON;
    } else if (output && !strcmp(output, "human")) {
        output_format = OFORMAT_HUMAN;
    } else if (output) {
        error_report("--output must be used with human or json as argument.");
        return 1;
    }

    bs = bdrv_new_open("image", filename, fmt, flags, true, quiet);
    if (!bs) {
        return 1;
    }

    check = g_new0(ImageCheck, 1);
    ret = collect_image_check(bs, check, filename, fmt, fix);

    if (ret == -ENOTSUP) {
        error_report("This image format does not support checks");
        ret = 63;
        goto fail;
    }

    if (check->corruptions_fixed || check->leaks_fixed) {
        int corruptions_fixed, leaks_fixed;

        leaks_fixed         = check->leaks_fixed;
        corruptions_fixed   = check->corruptions_fixed;

        if (output_format == OFORMAT_HUMAN) {
            qprintf(quiet,
                    "The following inconsistencies were found and repaired:\n\n"
                    "    %" PRId64 " leaked clusters\n"
                    "    %" PRId64 " corruptions\n\n"
                    "Double checking the fixed image now...\n",
                    check->leaks_fixed,
                    check->corruptions_fixed);
        }

        ret = collect_image_check(bs, check, filename, fmt, 0);

        check->leaks_fixed          = leaks_fixed;
        check->corruptions_fixed    = corruptions_fixed;
    }

    switch (output_format) {
    case OFORMAT_HUMAN:
        dump_human_image_check(check, quiet);
        break;
    case OFORMAT_JSON:
        dump_json_image_check(check, quiet);
        break;
    }

    if (ret || check->check_errors) {
        ret = 1;
        goto fail;
    }

    if (check->corruptions) {
        ret = 2;
    } else if (check->leaks) {
        ret = 3;
    } else {
        ret = 0;
    }

fail:
    qapi_free_ImageCheck(check);
    bdrv_unref(bs);

    return ret;
}

static int img_commit(int argc, char **argv)
{
    int c, ret, flags;
    const char *filename, *fmt, *cache;
    BlockDriverState *bs;
    bool quiet = false;

    fmt = NULL;
    cache = BDRV_DEFAULT_CACHE;
    for(;;) {
        c = getopt(argc, argv, "f:ht:q");
        if (c == -1) {
            break;
        }
        switch(c) {
        case '?':
        case 'h':
            help();
            break;
        case 'f':
            fmt = optarg;
            break;
        case 't':
            cache = optarg;
            break;
        case 'q':
            quiet = true;
            break;
        }
    }
    if (optind != argc - 1) {
        error_exit("Expecting one image file name");
    }
    filename = argv[optind++];

    flags = BDRV_O_RDWR;
    ret = bdrv_parse_cache_flags(cache, &flags);
    if (ret < 0) {
        error_report("Invalid cache option: %s", cache);
        return -1;
    }

    bs = bdrv_new_open("image", filename, fmt, flags, true, quiet);
    if (!bs) {
        return 1;
    }
    ret = bdrv_commit(bs);
    switch(ret) {
    case 0:
        qprintf(quiet, "Image committed.\n");
        break;
    case -ENOENT:
        error_report("No disk inserted");
        break;
    case -EACCES:
        error_report("Image is read-only");
        break;
    case -ENOTSUP:
        error_report("Image is already committed");
        break;
    default:
        error_report("Error while committing image");
        break;
    }

    bdrv_unref(bs);
    if (ret) {
        return 1;
    }
    return 0;
}

/*
 * Returns true iff the first sector pointed to by 'buf' contains at least
 * a non-NUL byte.
 *
 * 'pnum' is set to the number of sectors (including and immediately following
 * the first one) that are known to be in the same allocated/unallocated state.
 */
static int is_allocated_sectors(const uint8_t *buf, int n, int *pnum)
{
    bool is_zero;
    int i;

    if (n <= 0) {
        *pnum = 0;
        return 0;
    }
    is_zero = buffer_is_zero(buf, 512);
    for(i = 1; i < n; i++) {
        buf += 512;
        if (is_zero != buffer_is_zero(buf, 512)) {
            break;
        }
    }
    *pnum = i;
    return !is_zero;
}

/*
 * Like is_allocated_sectors, but if the buffer starts with a used sector,
 * up to 'min' consecutive sectors containing zeros are ignored. This avoids
 * breaking up write requests for only small sparse areas.
 */
static int is_allocated_sectors_min(const uint8_t *buf, int n, int *pnum,
    int min)
{
    int ret;
    int num_checked, num_used;

    if (n < min) {
        min = n;
    }

    ret = is_allocated_sectors(buf, n, pnum);
    if (!ret) {
        return ret;
    }

    num_used = *pnum;
    buf += BDRV_SECTOR_SIZE * *pnum;
    n -= *pnum;
    num_checked = num_used;

    while (n > 0) {
        ret = is_allocated_sectors(buf, n, pnum);

        buf += BDRV_SECTOR_SIZE * *pnum;
        n -= *pnum;
        num_checked += *pnum;
        if (ret) {
            num_used = num_checked;
        } else if (*pnum >= min) {
            break;
        }
    }

    *pnum = num_used;
    return 1;
}

/*
 * Compares two buffers sector by sector. Returns 0 if the first sector of both
 * buffers matches, non-zero otherwise.
 *
 * pnum is set to the number of sectors (including and immediately following
 * the first one) that are known to have the same comparison result
 */
static int compare_sectors(const uint8_t *buf1, const uint8_t *buf2, int n,
    int *pnum)
{
    int res, i;

    if (n <= 0) {
        *pnum = 0;
        return 0;
    }

    res = !!memcmp(buf1, buf2, 512);
    for(i = 1; i < n; i++) {
        buf1 += 512;
        buf2 += 512;

        if (!!memcmp(buf1, buf2, 512) != res) {
            break;
        }
    }

    *pnum = i;
    return res;
}

#define IO_BUF_SIZE (2 * 1024 * 1024)

static int64_t sectors_to_bytes(int64_t sectors)
{
    return sectors << BDRV_SECTOR_BITS;
}

static int64_t sectors_to_process(int64_t total, int64_t from)
{
    return MIN(total - from, IO_BUF_SIZE >> BDRV_SECTOR_BITS);
}

/*
 * Check if passed sectors are empty (not allocated or contain only 0 bytes)
 *
 * Returns 0 in case sectors are filled with 0, 1 if sectors contain non-zero
 * data and negative value on error.
 *
 * @param bs:  Driver used for accessing file
 * @param sect_num: Number of first sector to check
 * @param sect_count: Number of sectors to check
 * @param filename: Name of disk file we are checking (logging purpose)
 * @param buffer: Allocated buffer for storing read data
 * @param quiet: Flag for quiet mode
 */
static int check_empty_sectors(BlockDriverState *bs, int64_t sect_num,
                               int sect_count, const char *filename,
                               uint8_t *buffer, bool quiet)
{
    int pnum, ret = 0;
    ret = bdrv_read(bs, sect_num, buffer, sect_count);
    if (ret < 0) {
        error_report("Error while reading offset %" PRId64 " of %s: %s",
                     sectors_to_bytes(sect_num), filename, strerror(-ret));
        return ret;
    }
    ret = is_allocated_sectors(buffer, sect_count, &pnum);
    if (ret || pnum != sect_count) {
        qprintf(quiet, "Content mismatch at offset %" PRId64 "!\n",
                sectors_to_bytes(ret ? sect_num : sect_num + pnum));
        return 1;
    }

    return 0;
}

/*
 * Compares two images. Exit codes:
 *
 * 0 - Images are identical
 * 1 - Images differ
 * >1 - Error occurred
 */
static int img_compare(int argc, char **argv)
{
    const char *fmt1 = NULL, *fmt2 = NULL, *filename1, *filename2;
    BlockDriverState *bs1, *bs2;
    int64_t total_sectors1, total_sectors2;
    uint8_t *buf1 = NULL, *buf2 = NULL;
    int pnum1, pnum2;
    int allocated1, allocated2;
    int ret = 0; /* return value - 0 Ident, 1 Different, >1 Error */
    bool progress = false, quiet = false, strict = false;
    int64_t total_sectors;
    int64_t sector_num = 0;
    int64_t nb_sectors;
    int c, pnum;
    uint64_t bs_sectors;
    uint64_t progress_base;

    for (;;) {
        c = getopt(argc, argv, "hpf:F:sq");
        if (c == -1) {
            break;
        }
        switch (c) {
        case '?':
        case 'h':
            help();
            break;
        case 'f':
            fmt1 = optarg;
            break;
        case 'F':
            fmt2 = optarg;
            break;
        case 'p':
            progress = true;
            break;
        case 'q':
            quiet = true;
            break;
        case 's':
            strict = true;
            break;
        }
    }

    /* Progress is not shown in Quiet mode */
    if (quiet) {
        progress = false;
    }


    if (optind != argc - 2) {
        error_exit("Expecting two image file names");
    }
    filename1 = argv[optind++];
    filename2 = argv[optind++];

    /* Initialize before goto out */
    qemu_progress_init(progress, 2.0);

    bs1 = bdrv_new_open("image 1", filename1, fmt1, BDRV_O_FLAGS, true, quiet);
    if (!bs1) {
        error_report("Can't open file %s", filename1);
        ret = 2;
        goto out3;
    }

    bs2 = bdrv_new_open("image 2", filename2, fmt2, BDRV_O_FLAGS, true, quiet);
    if (!bs2) {
        error_report("Can't open file %s", filename2);
        ret = 2;
        goto out2;
    }

    buf1 = qemu_blockalign(bs1, IO_BUF_SIZE);
    buf2 = qemu_blockalign(bs2, IO_BUF_SIZE);
    bdrv_get_geometry(bs1, &bs_sectors);
    total_sectors1 = bs_sectors;
    bdrv_get_geometry(bs2, &bs_sectors);
    total_sectors2 = bs_sectors;
    total_sectors = MIN(total_sectors1, total_sectors2);
    progress_base = MAX(total_sectors1, total_sectors2);

    qemu_progress_print(0, 100);

    if (strict && total_sectors1 != total_sectors2) {
        ret = 1;
        qprintf(quiet, "Strict mode: Image size mismatch!\n");
        goto out;
    }

    for (;;) {
        nb_sectors = sectors_to_process(total_sectors, sector_num);
        if (nb_sectors <= 0) {
            break;
        }
        allocated1 = bdrv_is_allocated_above(bs1, NULL, sector_num, nb_sectors,
                                             &pnum1);
        if (allocated1 < 0) {
            ret = 3;
            error_report("Sector allocation test failed for %s", filename1);
            goto out;
        }

        allocated2 = bdrv_is_allocated_above(bs2, NULL, sector_num, nb_sectors,
                                             &pnum2);
        if (allocated2 < 0) {
            ret = 3;
            error_report("Sector allocation test failed for %s", filename2);
            goto out;
        }
        nb_sectors = MIN(pnum1, pnum2);

        if (allocated1 == allocated2) {
            if (allocated1) {
                ret = bdrv_read(bs1, sector_num, buf1, nb_sectors);
                if (ret < 0) {
                    error_report("Error while reading offset %" PRId64 " of %s:"
                                 " %s", sectors_to_bytes(sector_num), filename1,
                                 strerror(-ret));
                    ret = 4;
                    goto out;
                }
                ret = bdrv_read(bs2, sector_num, buf2, nb_sectors);
                if (ret < 0) {
                    error_report("Error while reading offset %" PRId64
                                 " of %s: %s", sectors_to_bytes(sector_num),
                                 filename2, strerror(-ret));
                    ret = 4;
                    goto out;
                }
                ret = compare_sectors(buf1, buf2, nb_sectors, &pnum);
                if (ret || pnum != nb_sectors) {
                    qprintf(quiet, "Content mismatch at offset %" PRId64 "!\n",
                            sectors_to_bytes(
                                ret ? sector_num : sector_num + pnum));
                    ret = 1;
                    goto out;
                }
            }
        } else {
            if (strict) {
                ret = 1;
                qprintf(quiet, "Strict mode: Offset %" PRId64
                        " allocation mismatch!\n",
                        sectors_to_bytes(sector_num));
                goto out;
            }

            if (allocated1) {
                ret = check_empty_sectors(bs1, sector_num, nb_sectors,
                                          filename1, buf1, quiet);
            } else {
                ret = check_empty_sectors(bs2, sector_num, nb_sectors,
                                          filename2, buf1, quiet);
            }
            if (ret) {
                if (ret < 0) {
                    error_report("Error while reading offset %" PRId64 ": %s",
                                 sectors_to_bytes(sector_num), strerror(-ret));
                    ret = 4;
                }
                goto out;
            }
        }
        sector_num += nb_sectors;
        qemu_progress_print(((float) nb_sectors / progress_base)*100, 100);
    }

    if (total_sectors1 != total_sectors2) {
        BlockDriverState *bs_over;
        int64_t total_sectors_over;
        const char *filename_over;

        qprintf(quiet, "Warning: Image size mismatch!\n");
        if (total_sectors1 > total_sectors2) {
            total_sectors_over = total_sectors1;
            bs_over = bs1;
            filename_over = filename1;
        } else {
            total_sectors_over = total_sectors2;
            bs_over = bs2;
            filename_over = filename2;
        }

        for (;;) {
            nb_sectors = sectors_to_process(total_sectors_over, sector_num);
            if (nb_sectors <= 0) {
                break;
            }
            ret = bdrv_is_allocated_above(bs_over, NULL, sector_num,
                                          nb_sectors, &pnum);
            if (ret < 0) {
                ret = 3;
                error_report("Sector allocation test failed for %s",
                             filename_over);
                goto out;

            }
            nb_sectors = pnum;
            if (ret) {
                ret = check_empty_sectors(bs_over, sector_num, nb_sectors,
                                          filename_over, buf1, quiet);
                if (ret) {
                    if (ret < 0) {
                        error_report("Error while reading offset %" PRId64
                                     " of %s: %s", sectors_to_bytes(sector_num),
                                     filename_over, strerror(-ret));
                        ret = 4;
                    }
                    goto out;
                }
            }
            sector_num += nb_sectors;
            qemu_progress_print(((float) nb_sectors / progress_base)*100, 100);
        }
    }

    qprintf(quiet, "Images are identical.\n");
    ret = 0;

out:
    bdrv_unref(bs2);
    qemu_vfree(buf1);
    qemu_vfree(buf2);
out2:
    bdrv_unref(bs1);
out3:
    qemu_progress_end();
    return ret;
}

static int img_convert(int argc, char **argv)
{
    int c, n, n1, bs_n, bs_i, compress, cluster_sectors, skip_create;
    int64_t ret = 0;
    int progress = 0, flags;
    const char *fmt, *out_fmt, *cache, *out_baseimg, *out_filename;
    BlockDriver *drv, *proto_drv;
    BlockDriverState **bs = NULL, *out_bs = NULL;
    int64_t total_sectors, nb_sectors, sector_num, bs_offset;
    uint64_t bs_sectors;
    uint8_t * buf = NULL;
    size_t bufsectors = IO_BUF_SIZE / BDRV_SECTOR_SIZE;
    const uint8_t *buf1;
    BlockDriverInfo bdi;
    QEMUOptionParameter *param = NULL, *create_options = NULL;
    QEMUOptionParameter *out_baseimg_param;
    char *options = NULL;
    const char *snapshot_name = NULL;
    int min_sparse = 8; /* Need at least 4k of zeros for sparse detection */
    bool quiet = false;
    Error *local_err = NULL;
    QemuOpts *sn_opts = NULL;

    fmt = NULL;
    out_fmt = "raw";
    cache = "unsafe";
    out_baseimg = NULL;
    compress = 0;
    skip_create = 0;
    for(;;) {
        c = getopt(argc, argv, "f:O:B:s:hce6o:pS:t:qnl:");
        if (c == -1) {
            break;
        }
        switch(c) {
        case '?':
        case 'h':
            help();
            break;
        case 'f':
            fmt = optarg;
            break;
        case 'O':
            out_fmt = optarg;
            break;
        case 'B':
            out_baseimg = optarg;
            break;
        case 'c':
            compress = 1;
            break;
        case 'e':
            error_report("option -e is deprecated, please use \'-o "
                  "encryption\' instead!");
            ret = -1;
            goto fail_getopt;
        case '6':
            error_report("option -6 is deprecated, please use \'-o "
                  "compat6\' instead!");
            ret = -1;
            goto fail_getopt;
        case 'o':
            if (!is_valid_option_list(optarg)) {
                error_report("Invalid option list: %s", optarg);
                ret = -1;
                goto fail_getopt;
            }
            if (!options) {
                options = g_strdup(optarg);
            } else {
                char *old_options = options;
                options = g_strdup_printf("%s,%s", options, optarg);
                g_free(old_options);
            }
            break;
        case 's':
            snapshot_name = optarg;
            break;
        case 'l':
            if (strstart(optarg, SNAPSHOT_OPT_BASE, NULL)) {
                sn_opts = qemu_opts_parse(&internal_snapshot_opts, optarg, 0);
                if (!sn_opts) {
                    error_report("Failed in parsing snapshot param '%s'",
                                 optarg);
                    ret = -1;
                    goto fail_getopt;
                }
            } else {
                snapshot_name = optarg;
            }
            break;
        case 'S':
        {
            int64_t sval;
            char *end;
            sval = strtosz_suffix(optarg, &end, STRTOSZ_DEFSUFFIX_B);
            if (sval < 0 || *end) {
                error_report("Invalid minimum zero buffer size for sparse output specified");
                ret = -1;
                goto fail_getopt;
            }

            min_sparse = sval / BDRV_SECTOR_SIZE;
            break;
        }
        case 'p':
            progress = 1;
            break;
        case 't':
            cache = optarg;
            break;
        case 'q':
            quiet = true;
            break;
        case 'n':
            skip_create = 1;
            break;
        }
    }

    /* Initialize before goto out */
    if (quiet) {
        progress = 0;
    }
    qemu_progress_init(progress, 1.0);


    bs_n = argc - optind - 1;
    out_filename = bs_n >= 1 ? argv[argc - 1] : NULL;

    if (options && has_help_option(options)) {
        ret = print_block_option_help(out_filename, out_fmt);
        goto out;
    }

    if (bs_n < 1) {
        error_exit("Must specify image file name");
    }


    if (bs_n > 1 && out_baseimg) {
        error_report("-B makes no sense when concatenating multiple input "
                     "images");
        ret = -1;
        goto out;
    }

    qemu_progress_print(0, 100);

    bs = g_malloc0(bs_n * sizeof(BlockDriverState *));

    total_sectors = 0;
    for (bs_i = 0; bs_i < bs_n; bs_i++) {
        char *id = bs_n > 1 ? g_strdup_printf("source %d", bs_i)
                            : g_strdup("source");
        bs[bs_i] = bdrv_new_open(id, argv[optind + bs_i], fmt, BDRV_O_FLAGS,
                                 true, quiet);
        g_free(id);
        if (!bs[bs_i]) {
            error_report("Could not open '%s'", argv[optind + bs_i]);
            ret = -1;
            goto out;
        }
        bdrv_get_geometry(bs[bs_i], &bs_sectors);
        total_sectors += bs_sectors;
    }

    if (sn_opts) {
        ret = bdrv_snapshot_load_tmp(bs[0],
                                     qemu_opt_get(sn_opts, SNAPSHOT_OPT_ID),
                                     qemu_opt_get(sn_opts, SNAPSHOT_OPT_NAME),
                                     &local_err);
    } else if (snapshot_name != NULL) {
        if (bs_n > 1) {
            error_report("No support for concatenating multiple snapshot");
            ret = -1;
            goto out;
        }

        bdrv_snapshot_load_tmp_by_id_or_name(bs[0], snapshot_name, &local_err);
    }
    if (local_err) {
        error_report("Failed to load snapshot: %s",
                     error_get_pretty(local_err));
        error_free(local_err);
        ret = -1;
        goto out;
    }

    /* Find driver and parse its options */
    drv = bdrv_find_format(out_fmt);
    if (!drv) {
        error_report("Unknown file format '%s'", out_fmt);
        ret = -1;
        goto out;
    }

    proto_drv = bdrv_find_protocol(out_filename, true);
    if (!proto_drv) {
        error_report("Unknown protocol '%s'", out_filename);
        ret = -1;
        goto out;
    }

    create_options = append_option_parameters(create_options,
                                              drv->create_options);
    create_options = append_option_parameters(create_options,
                                              proto_drv->create_options);

    if (options) {
        param = parse_option_parameters(options, create_options, param);
        if (param == NULL) {
            error_report("Invalid options for file format '%s'.", out_fmt);
            ret = -1;
            goto out;
        }
    } else {
        param = parse_option_parameters("", create_options, param);
    }

    set_option_parameter_int(param, BLOCK_OPT_SIZE, total_sectors * 512);
    ret = add_old_style_options(out_fmt, param, out_baseimg, NULL);
    if (ret < 0) {
        goto out;
    }

    /* Get backing file name if -o backing_file was used */
    out_baseimg_param = get_option_parameter(param, BLOCK_OPT_BACKING_FILE);
    if (out_baseimg_param) {
        out_baseimg = out_baseimg_param->value.s;
    }

    /* Check if compression is supported */
    if (compress) {
        QEMUOptionParameter *encryption =
            get_option_parameter(param, BLOCK_OPT_ENCRYPT);
        QEMUOptionParameter *preallocation =
            get_option_parameter(param, BLOCK_OPT_PREALLOC);

        if (!drv->bdrv_write_compressed) {
            error_report("Compression not supported for this file format");
            ret = -1;
            goto out;
        }

        if (encryption && encryption->value.n) {
            error_report("Compression and encryption not supported at "
                         "the same time");
            ret = -1;
            goto out;
        }

        if (preallocation && preallocation->value.s
            && strcmp(preallocation->value.s, "off"))
        {
            error_report("Compression and preallocation not supported at "
                         "the same time");
            ret = -1;
            goto out;
        }
    }

    if (!skip_create) {
        /* Create the new image */
        ret = bdrv_create(drv, out_filename, param, &local_err);
        if (ret < 0) {
            error_report("%s: error while converting %s: %s",
                         out_filename, out_fmt, error_get_pretty(local_err));
            error_free(local_err);
            goto out;
        }
    }

    flags = min_sparse ? (BDRV_O_RDWR | BDRV_O_UNMAP) : BDRV_O_RDWR;
    ret = bdrv_parse_cache_flags(cache, &flags);
    if (ret < 0) {
        error_report("Invalid cache option: %s", cache);
        goto out;
    }

    out_bs = bdrv_new_open("target", out_filename, out_fmt, flags, true, quiet);
    if (!out_bs) {
        ret = -1;
        goto out;
    }

    bs_i = 0;
    bs_offset = 0;
    bdrv_get_geometry(bs[0], &bs_sectors);

    /* increase bufsectors from the default 4096 (2M) if opt_transfer_length
     * or discard_alignment of the out_bs is greater. Limit to 32768 (16MB)
     * as maximum. */
    bufsectors = MIN(32768,
                     MAX(bufsectors, MAX(out_bs->bl.opt_transfer_length,
                                         out_bs->bl.discard_alignment))
                    );

    buf = qemu_blockalign(out_bs, bufsectors * BDRV_SECTOR_SIZE);

    if (skip_create) {
        int64_t output_length = bdrv_getlength(out_bs);
        if (output_length < 0) {
            error_report("unable to get output image length: %s\n",
                         strerror(-output_length));
            ret = -1;
            goto out;
        } else if (output_length < total_sectors << BDRV_SECTOR_BITS) {
            error_report("output file is smaller than input file");
            ret = -1;
            goto out;
        }
    }

    cluster_sectors = 0;
    ret = bdrv_get_info(out_bs, &bdi);
    if (ret < 0) {
        if (compress) {
            error_report("could not get block driver info");
            goto out;
        }
    } else {
        compress = compress || bdi.needs_compressed_writes;
        cluster_sectors = bdi.cluster_size / BDRV_SECTOR_SIZE;
    }

    if (compress) {
        if (cluster_sectors <= 0 || cluster_sectors > bufsectors) {
            error_report("invalid cluster size");
            ret = -1;
            goto out;
        }
        sector_num = 0;

        nb_sectors = total_sectors;

        for(;;) {
            int64_t bs_num;
            int remainder;
            uint8_t *buf2;

            nb_sectors = total_sectors - sector_num;
            if (nb_sectors <= 0)
                break;
            if (nb_sectors >= cluster_sectors)
                n = cluster_sectors;
            else
                n = nb_sectors;

            bs_num = sector_num - bs_offset;
            assert (bs_num >= 0);
            remainder = n;
            buf2 = buf;
            while (remainder > 0) {
                int nlow;
                while (bs_num == bs_sectors) {
                    bs_i++;
                    assert (bs_i < bs_n);
                    bs_offset += bs_sectors;
                    bdrv_get_geometry(bs[bs_i], &bs_sectors);
                    bs_num = 0;
                    /* printf("changing part: sector_num=%" PRId64 ", "
                       "bs_i=%d, bs_offset=%" PRId64 ", bs_sectors=%" PRId64
                       "\n", sector_num, bs_i, bs_offset, bs_sectors); */
                }
                assert (bs_num < bs_sectors);

                nlow = (remainder > bs_sectors - bs_num) ? bs_sectors - bs_num : remainder;

                ret = bdrv_read(bs[bs_i], bs_num, buf2, nlow);
                if (ret < 0) {
                    error_report("error while reading sector %" PRId64 ": %s",
                                 bs_num, strerror(-ret));
                    goto out;
                }

                buf2 += nlow * 512;
                bs_num += nlow;

                remainder -= nlow;
            }
            assert (remainder == 0);

            if (!buffer_is_zero(buf, n * BDRV_SECTOR_SIZE)) {
                ret = bdrv_write_compressed(out_bs, sector_num, buf, n);
                if (ret != 0) {
                    error_report("error while compressing sector %" PRId64
                                 ": %s", sector_num, strerror(-ret));
                    goto out;
                }
            }
            sector_num += n;
            qemu_progress_print(100.0 * sector_num / total_sectors, 0);
        }
        /* signal EOF to align */
        bdrv_write_compressed(out_bs, 0, NULL, 0);
    } else {
        int64_t sectors_to_read, sectors_read, sector_num_next_status;
        bool count_allocated_sectors;
        int has_zero_init = min_sparse ? bdrv_has_zero_init(out_bs) : 0;

        if (!has_zero_init && bdrv_can_write_zeroes_with_unmap(out_bs)) {
            ret = bdrv_make_zero(out_bs, BDRV_REQ_MAY_UNMAP);
            if (ret < 0) {
                goto out;
            }
            has_zero_init = 1;
        }

        sectors_to_read = total_sectors;
        count_allocated_sectors = progress && (out_baseimg || has_zero_init);
restart:
        sector_num = 0; // total number of sectors converted so far
        sectors_read = 0;
        sector_num_next_status = 0;

        for(;;) {
            nb_sectors = total_sectors - sector_num;
            if (nb_sectors <= 0) {
                if (count_allocated_sectors) {
                    sectors_to_read = sectors_read;
                    count_allocated_sectors = false;
                    goto restart;
                }
                ret = 0;
                break;
            }

            while (sector_num - bs_offset >= bs_sectors) {
                bs_i ++;
                assert (bs_i < bs_n);
                bs_offset += bs_sectors;
                bdrv_get_geometry(bs[bs_i], &bs_sectors);
                /* printf("changing part: sector_num=%" PRId64 ", bs_i=%d, "
                  "bs_offset=%" PRId64 ", bs_sectors=%" PRId64 "\n",
                   sector_num, bs_i, bs_offset, bs_sectors); */
            }

            if ((out_baseimg || has_zero_init) &&
                sector_num >= sector_num_next_status) {
                n = nb_sectors > INT_MAX ? INT_MAX : nb_sectors;
                ret = bdrv_get_block_status(bs[bs_i], sector_num - bs_offset,
                                            n, &n1);
                if (ret < 0) {
                    error_report("error while reading block status of sector %"
                                 PRId64 ": %s", sector_num - bs_offset,
                                 strerror(-ret));
                    goto out;
                }
                /* If the output image is zero initialized, we are not working
                 * on a shared base and the input is zero we can skip the next
                 * n1 sectors */
                if (has_zero_init && !out_baseimg && (ret & BDRV_BLOCK_ZERO)) {
                    sector_num += n1;
                    continue;
                }
                /* If the output image is being created as a copy on write
                 * image, assume that sectors which are unallocated in the
                 * input image are present in both the output's and input's
                 * base images (no need to copy them). */
                if (out_baseimg) {
                    if (!(ret & BDRV_BLOCK_DATA)) {
                        sector_num += n1;
                        continue;
                    }
                    /* The next 'n1' sectors are allocated in the input image.
                     * Copy only those as they may be followed by unallocated
                     * sectors. */
                    nb_sectors = n1;
                }
                /* avoid redundant callouts to get_block_status */
                sector_num_next_status = sector_num + n1;
            }

            n = MIN(nb_sectors, bufsectors);

            /* round down request length to an aligned sector, but
             * do not bother doing this on short requests. They happen
             * when we found an all-zero area, and the next sector to
             * write will not be sector_num + n. */
            if (cluster_sectors > 0 && n >= cluster_sectors) {
                int64_t next_aligned_sector = (sector_num + n);
                next_aligned_sector -= next_aligned_sector % cluster_sectors;
                if (sector_num + n > next_aligned_sector) {
                    n = next_aligned_sector - sector_num;
                }
            }

            n = MIN(n, bs_sectors - (sector_num - bs_offset));

            sectors_read += n;
            if (count_allocated_sectors) {
                sector_num += n;
                continue;
            }

            n1 = n;
            ret = bdrv_read(bs[bs_i], sector_num - bs_offset, buf, n);
            if (ret < 0) {
                error_report("error while reading sector %" PRId64 ": %s",
                             sector_num - bs_offset, strerror(-ret));
                goto out;
            }
            /* NOTE: at the same time we convert, we do not write zero
               sectors to have a chance to compress the image. Ideally, we
               should add a specific call to have the info to go faster */
            buf1 = buf;
            while (n > 0) {
                if (!has_zero_init ||
                    is_allocated_sectors_min(buf1, n, &n1, min_sparse)) {
                    ret = bdrv_write(out_bs, sector_num, buf1, n1);
                    if (ret < 0) {
                        error_report("error while writing sector %" PRId64
                                     ": %s", sector_num, strerror(-ret));
                        goto out;
                    }
                }
                sector_num += n1;
                n -= n1;
                buf1 += n1 * 512;
            }
            qemu_progress_print(100.0 * sectors_read / sectors_to_read, 0);
        }
    }
out:
    if (!ret) {
        qemu_progress_print(100, 0);
    }
    qemu_progress_end();
    free_option_parameters(create_options);
    free_option_parameters(param);
    qemu_vfree(buf);
    if (sn_opts) {
        qemu_opts_del(sn_opts);
    }
    if (out_bs) {
        bdrv_unref(out_bs);
    }
    if (bs) {
        for (bs_i = 0; bs_i < bs_n; bs_i++) {
            if (bs[bs_i]) {
                bdrv_unref(bs[bs_i]);
            }
        }
        g_free(bs);
    }
fail_getopt:
    g_free(options);

    if (ret) {
        return 1;
    }
    return 0;
}


static void dump_snapshots(BlockDriverState *bs)
{
    QEMUSnapshotInfo *sn_tab, *sn;
    int nb_sns, i;

    nb_sns = bdrv_snapshot_list(bs, &sn_tab);
    if (nb_sns <= 0)
        return;
    printf("Snapshot list:\n");
    bdrv_snapshot_dump(fprintf, stdout, NULL);
    printf("\n");
    for(i = 0; i < nb_sns; i++) {
        sn = &sn_tab[i];
        bdrv_snapshot_dump(fprintf, stdout, sn);
        printf("\n");
    }
    g_free(sn_tab);
}

static void dump_json_image_info_list(ImageInfoList *list)
{
    Error *local_err = NULL;
    QString *str;
    QmpOutputVisitor *ov = qmp_output_visitor_new();
    QObject *obj;
    visit_type_ImageInfoList(qmp_output_get_visitor(ov),
                             &list, NULL, &local_err);
    obj = qmp_output_get_qobject(ov);
    str = qobject_to_json_pretty(obj);
    assert(str != NULL);
    printf("%s\n", qstring_get_str(str));
    qobject_decref(obj);
    qmp_output_visitor_cleanup(ov);
    QDECREF(str);
}

static void dump_json_image_info(ImageInfo *info)
{
    Error *local_err = NULL;
    QString *str;
    QmpOutputVisitor *ov = qmp_output_visitor_new();
    QObject *obj;
    visit_type_ImageInfo(qmp_output_get_visitor(ov),
                         &info, NULL, &local_err);
    obj = qmp_output_get_qobject(ov);
    str = qobject_to_json_pretty(obj);
    assert(str != NULL);
    printf("%s\n", qstring_get_str(str));
    qobject_decref(obj);
    qmp_output_visitor_cleanup(ov);
    QDECREF(str);
}

static void dump_human_image_info_list(ImageInfoList *list)
{
    ImageInfoList *elem;
    bool delim = false;

    for (elem = list; elem; elem = elem->next) {
        if (delim) {
            printf("\n");
        }
        delim = true;

        bdrv_image_info_dump(fprintf, stdout, elem->value);
    }
}

static gboolean str_equal_func(gconstpointer a, gconstpointer b)
{
    return strcmp(a, b) == 0;
}

/**
 * Open an image file chain and return an ImageInfoList
 *
 * @filename: topmost image filename
 * @fmt: topmost image format (may be NULL to autodetect)
 * @chain: true  - enumerate entire backing file chain
 *         false - only topmost image file
 *
 * Returns a list of ImageInfo objects or NULL if there was an error opening an
 * image file.  If there was an error a message will have been printed to
 * stderr.
 */
static ImageInfoList *collect_image_info_list(const char *filename,
                                              const char *fmt,
                                              bool chain)
{
    ImageInfoList *head = NULL;
    ImageInfoList **last = &head;
    GHashTable *filenames;
    Error *err = NULL;

    filenames = g_hash_table_new_full(g_str_hash, str_equal_func, NULL, NULL);

    while (filename) {
        BlockDriverState *bs;
        ImageInfo *info;
        ImageInfoList *elem;

        if (g_hash_table_lookup_extended(filenames, filename, NULL, NULL)) {
            error_report("Backing file '%s' creates an infinite loop.",
                         filename);
            goto err;
        }
        g_hash_table_insert(filenames, (gpointer)filename, NULL);

        bs = bdrv_new_open("image", filename, fmt,
                           BDRV_O_FLAGS | BDRV_O_NO_BACKING, false, false);
        if (!bs) {
            goto err;
        }

        bdrv_query_image_info(bs, &info, &err);
        if (err) {
            error_report("%s", error_get_pretty(err));
            error_free(err);
            bdrv_unref(bs);
            goto err;
        }

        elem = g_new0(ImageInfoList, 1);
        elem->value = info;
        *last = elem;
        last = &elem->next;

        bdrv_unref(bs);

        filename = fmt = NULL;
        if (chain) {
            if (info->has_full_backing_filename) {
                filename = info->full_backing_filename;
            } else if (info->has_backing_filename) {
                filename = info->backing_filename;
            }
            if (info->has_backing_filename_format) {
                fmt = info->backing_filename_format;
            }
        }
    }
    g_hash_table_destroy(filenames);
    return head;

err:
    qapi_free_ImageInfoList(head);
    g_hash_table_destroy(filenames);
    return NULL;
}

static int img_info(int argc, char **argv)
{
    int c;
    OutputFormat output_format = OFORMAT_HUMAN;
    bool chain = false;
    const char *filename, *fmt, *output;
    ImageInfoList *list;

    fmt = NULL;
    output = NULL;
    for(;;) {
        int option_index = 0;
        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"format", required_argument, 0, 'f'},
            {"output", required_argument, 0, OPTION_OUTPUT},
            {"backing-chain", no_argument, 0, OPTION_BACKING_CHAIN},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, "f:h",
                        long_options, &option_index);
        if (c == -1) {
            break;
        }
        switch(c) {
        case '?':
        case 'h':
            help();
            break;
        case 'f':
            fmt = optarg;
            break;
        case OPTION_OUTPUT:
            output = optarg;
            break;
        case OPTION_BACKING_CHAIN:
            chain = true;
            break;
        }
    }
    if (optind != argc - 1) {
        error_exit("Expecting one image file name");
    }
    filename = argv[optind++];

    if (output && !strcmp(output, "json")) {
        output_format = OFORMAT_JSON;
    } else if (output && !strcmp(output, "human")) {
        output_format = OFORMAT_HUMAN;
    } else if (output) {
        error_report("--output must be used with human or json as argument.");
        return 1;
    }

    list = collect_image_info_list(filename, fmt, chain);
    if (!list) {
        return 1;
    }

    switch (output_format) {
    case OFORMAT_HUMAN:
        dump_human_image_info_list(list);
        break;
    case OFORMAT_JSON:
        if (chain) {
            dump_json_image_info_list(list);
        } else {
            dump_json_image_info(list->value);
        }
        break;
    }

    qapi_free_ImageInfoList(list);
    return 0;
}


typedef struct MapEntry {
    int flags;
    int depth;
    int64_t start;
    int64_t length;
    int64_t offset;
    BlockDriverState *bs;
} MapEntry;

static void dump_map_entry(OutputFormat output_format, MapEntry *e,
                           MapEntry *next)
{
    switch (output_format) {
    case OFORMAT_HUMAN:
        if ((e->flags & BDRV_BLOCK_DATA) &&
            !(e->flags & BDRV_BLOCK_OFFSET_VALID)) {
            error_report("File contains external, encrypted or compressed clusters.");
            exit(1);
        }
        if ((e->flags & (BDRV_BLOCK_DATA|BDRV_BLOCK_ZERO)) == BDRV_BLOCK_DATA) {
            printf("%#-16"PRIx64"%#-16"PRIx64"%#-16"PRIx64"%s\n",
                   e->start, e->length, e->offset, e->bs->filename);
        }
        /* This format ignores the distinction between 0, ZERO and ZERO|DATA.
         * Modify the flags here to allow more coalescing.
         */
        if (next &&
            (next->flags & (BDRV_BLOCK_DATA|BDRV_BLOCK_ZERO)) != BDRV_BLOCK_DATA) {
            next->flags &= ~BDRV_BLOCK_DATA;
            next->flags |= BDRV_BLOCK_ZERO;
        }
        break;
    case OFORMAT_JSON:
        printf("%s{ \"start\": %"PRId64", \"length\": %"PRId64", \"depth\": %d,"
               " \"zero\": %s, \"data\": %s",
               (e->start == 0 ? "[" : ",\n"),
               e->start, e->length, e->depth,
               (e->flags & BDRV_BLOCK_ZERO) ? "true" : "false",
               (e->flags & BDRV_BLOCK_DATA) ? "true" : "false");
        if (e->flags & BDRV_BLOCK_OFFSET_VALID) {
            printf(", \"offset\": %"PRId64"", e->offset);
        }
        putchar('}');

        if (!next) {
            printf("]\n");
        }
        break;
    }
}

static int get_block_status(BlockDriverState *bs, int64_t sector_num,
                            int nb_sectors, MapEntry *e)
{
    int64_t ret;
    int depth;

    /* As an optimization, we could cache the current range of unallocated
     * clusters in each file of the chain, and avoid querying the same
     * range repeatedly.
     */

    depth = 0;
    for (;;) {
        ret = bdrv_get_block_status(bs, sector_num, nb_sectors, &nb_sectors);
        if (ret < 0) {
            return ret;
        }
        assert(nb_sectors);
        if (ret & (BDRV_BLOCK_ZERO|BDRV_BLOCK_DATA)) {
            break;
        }
        bs = bs->backing_hd;
        if (bs == NULL) {
            ret = 0;
            break;
        }

        depth++;
    }

    e->start = sector_num * BDRV_SECTOR_SIZE;
    e->length = nb_sectors * BDRV_SECTOR_SIZE;
    e->flags = ret & ~BDRV_BLOCK_OFFSET_MASK;
    e->offset = ret & BDRV_BLOCK_OFFSET_MASK;
    e->depth = depth;
    e->bs = bs;
    return 0;
}

static int img_map(int argc, char **argv)
{
    int c;
    OutputFormat output_format = OFORMAT_HUMAN;
    BlockDriverState *bs;
    const char *filename, *fmt, *output;
    int64_t length;
    MapEntry curr = { .length = 0 }, next;
    int ret = 0;

    fmt = NULL;
    output = NULL;
    for (;;) {
        int option_index = 0;
        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"format", required_argument, 0, 'f'},
            {"output", required_argument, 0, OPTION_OUTPUT},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, "f:h",
                        long_options, &option_index);
        if (c == -1) {
            break;
        }
        switch (c) {
        case '?':
        case 'h':
            help();
            break;
        case 'f':
            fmt = optarg;
            break;
        case OPTION_OUTPUT:
            output = optarg;
            break;
        }
    }
    if (optind != argc - 1) {
        error_exit("Expecting one image file name");
    }
    filename = argv[optind];

    if (output && !strcmp(output, "json")) {
        output_format = OFORMAT_JSON;
    } else if (output && !strcmp(output, "human")) {
        output_format = OFORMAT_HUMAN;
    } else if (output) {
        error_report("--output must be used with human or json as argument.");
        return 1;
    }

    bs = bdrv_new_open("image", filename, fmt, BDRV_O_FLAGS, true, false);
    if (!bs) {
        return 1;
    }

    if (output_format == OFORMAT_HUMAN) {
        printf("%-16s%-16s%-16s%s\n", "Offset", "Length", "Mapped to", "File");
    }

    length = bdrv_getlength(bs);
    while (curr.start + curr.length < length) {
        int64_t nsectors_left;
        int64_t sector_num;
        int n;

        sector_num = (curr.start + curr.length) >> BDRV_SECTOR_BITS;

        /* Probe up to 1 GiB at a time.  */
        nsectors_left = DIV_ROUND_UP(length, BDRV_SECTOR_SIZE) - sector_num;
        n = MIN(1 << (30 - BDRV_SECTOR_BITS), nsectors_left);
        ret = get_block_status(bs, sector_num, n, &next);

        if (ret < 0) {
            error_report("Could not read file metadata: %s", strerror(-ret));
            goto out;
        }

        if (curr.length != 0 && curr.flags == next.flags &&
            curr.depth == next.depth &&
            ((curr.flags & BDRV_BLOCK_OFFSET_VALID) == 0 ||
             curr.offset + curr.length == next.offset)) {
            curr.length += next.length;
            continue;
        }

        if (curr.length > 0) {
            dump_map_entry(output_format, &curr, &next);
        }
        curr = next;
    }

    dump_map_entry(output_format, &curr, NULL);

out:
    bdrv_unref(bs);
    return ret < 0;
}

#define SNAPSHOT_LIST   1
#define SNAPSHOT_CREATE 2
#define SNAPSHOT_APPLY  3
#define SNAPSHOT_DELETE 4

static int img_snapshot(int argc, char **argv)
{
    BlockDriverState *bs;
    QEMUSnapshotInfo sn;
    char *filename, *snapshot_name = NULL;
    int c, ret = 0, bdrv_oflags;
    int action = 0;
    qemu_timeval tv;
    bool quiet = false;
    Error *err = NULL;

    bdrv_oflags = BDRV_O_FLAGS | BDRV_O_RDWR;
    /* Parse commandline parameters */
    for(;;) {
        c = getopt(argc, argv, "la:c:d:hq");
        if (c == -1) {
            break;
        }
        switch(c) {
        case '?':
        case 'h':
            help();
            return 0;
        case 'l':
            if (action) {
                error_exit("Cannot mix '-l', '-a', '-c', '-d'");
                return 0;
            }
            action = SNAPSHOT_LIST;
            bdrv_oflags &= ~BDRV_O_RDWR; /* no need for RW */
            break;
        case 'a':
            if (action) {
                error_exit("Cannot mix '-l', '-a', '-c', '-d'");
                return 0;
            }
            action = SNAPSHOT_APPLY;
            snapshot_name = optarg;
            break;
        case 'c':
            if (action) {
                error_exit("Cannot mix '-l', '-a', '-c', '-d'");
                return 0;
            }
            action = SNAPSHOT_CREATE;
            snapshot_name = optarg;
            break;
        case 'd':
            if (action) {
                error_exit("Cannot mix '-l', '-a', '-c', '-d'");
                return 0;
            }
            action = SNAPSHOT_DELETE;
            snapshot_name = optarg;
            break;
        case 'q':
            quiet = true;
            break;
        }
    }

    if (optind != argc - 1) {
        error_exit("Expecting one image file name");
    }
    filename = argv[optind++];

    /* Open the image */
    bs = bdrv_new_open("image", filename, NULL, bdrv_oflags, true, quiet);
    if (!bs) {
        return 1;
    }

    /* Perform the requested action */
    switch(action) {
    case SNAPSHOT_LIST:
        dump_snapshots(bs);
        break;

    case SNAPSHOT_CREATE:
        memset(&sn, 0, sizeof(sn));
        pstrcpy(sn.name, sizeof(sn.name), snapshot_name);

        qemu_gettimeofday(&tv);
        sn.date_sec = tv.tv_sec;
        sn.date_nsec = tv.tv_usec * 1000;

        ret = bdrv_snapshot_create(bs, &sn);
        if (ret) {
            error_report("Could not create snapshot '%s': %d (%s)",
                snapshot_name, ret, strerror(-ret));
        }
        break;

    case SNAPSHOT_APPLY:
        ret = bdrv_snapshot_goto(bs, snapshot_name);
        if (ret) {
            error_report("Could not apply snapshot '%s': %d (%s)",
                snapshot_name, ret, strerror(-ret));
        }
        break;

    case SNAPSHOT_DELETE:
        bdrv_snapshot_delete_by_id_or_name(bs, snapshot_name, &err);
        if (err) {
            error_report("Could not delete snapshot '%s': (%s)",
                         snapshot_name, error_get_pretty(err));
            error_free(err);
            ret = 1;
        }
        break;
    }

    /* Cleanup */
    bdrv_unref(bs);
    if (ret) {
        return 1;
    }
    return 0;
}

static int img_rebase(int argc, char **argv)
{
    BlockDriverState *bs, *bs_old_backing = NULL, *bs_new_backing = NULL;
    BlockDriver *old_backing_drv, *new_backing_drv;
    char *filename;
    const char *fmt, *cache, *out_basefmt, *out_baseimg;
    int c, flags, ret;
    int unsafe = 0;
    int progress = 0;
    bool quiet = false;
    Error *local_err = NULL;

    /* Parse commandline parameters */
    fmt = NULL;
    cache = BDRV_DEFAULT_CACHE;
    out_baseimg = NULL;
    out_basefmt = NULL;
    for(;;) {
        c = getopt(argc, argv, "uhf:F:b:pt:q");
        if (c == -1) {
            break;
        }
        switch(c) {
        case '?':
        case 'h':
            help();
            return 0;
        case 'f':
            fmt = optarg;
            break;
        case 'F':
            out_basefmt = optarg;
            break;
        case 'b':
            out_baseimg = optarg;
            break;
        case 'u':
            unsafe = 1;
            break;
        case 'p':
            progress = 1;
            break;
        case 't':
            cache = optarg;
            break;
        case 'q':
            quiet = true;
            break;
        }
    }

    if (quiet) {
        progress = 0;
    }

    if (optind != argc - 1) {
        error_exit("Expecting one image file name");
    }
    if (!unsafe && !out_baseimg) {
        error_exit("Must specify backing file (-b) or use unsafe mode (-u)");
    }
    filename = argv[optind++];

    qemu_progress_init(progress, 2.0);
    qemu_progress_print(0, 100);

    flags = BDRV_O_RDWR | (unsafe ? BDRV_O_NO_BACKING : 0);
    ret = bdrv_parse_cache_flags(cache, &flags);
    if (ret < 0) {
        error_report("Invalid cache option: %s", cache);
        return -1;
    }

    /*
     * Open the images.
     *
     * Ignore the old backing file for unsafe rebase in case we want to correct
     * the reference to a renamed or moved backing file.
     */
    bs = bdrv_new_open("image", filename, fmt, flags, true, quiet);
    if (!bs) {
        return 1;
    }

    /* Find the right drivers for the backing files */
    old_backing_drv = NULL;
    new_backing_drv = NULL;

    if (!unsafe && bs->backing_format[0] != '\0') {
        old_backing_drv = bdrv_find_format(bs->backing_format);
        if (old_backing_drv == NULL) {
            error_report("Invalid format name: '%s'", bs->backing_format);
            ret = -1;
            goto out;
        }
    }

    if (out_basefmt != NULL) {
        new_backing_drv = bdrv_find_format(out_basefmt);
        if (new_backing_drv == NULL) {
            error_report("Invalid format name: '%s'", out_basefmt);
            ret = -1;
            goto out;
        }
    }

    /* For safe rebasing we need to compare old and new backing file */
    if (unsafe) {
        /* Make the compiler happy */
        bs_old_backing = NULL;
        bs_new_backing = NULL;
    } else {
        char backing_name[1024];

        bs_old_backing = bdrv_new("old_backing", &error_abort);
        bdrv_get_backing_filename(bs, backing_name, sizeof(backing_name));
        ret = bdrv_open(&bs_old_backing, backing_name, NULL, NULL, BDRV_O_FLAGS,
                        old_backing_drv, &local_err);
        if (ret) {
            error_report("Could not open old backing file '%s': %s",
                         backing_name, error_get_pretty(local_err));
            error_free(local_err);
            goto out;
        }
        if (out_baseimg[0]) {
            bs_new_backing = bdrv_new("new_backing", &error_abort);
            ret = bdrv_open(&bs_new_backing, out_baseimg, NULL, NULL,
                            BDRV_O_FLAGS, new_backing_drv, &local_err);
            if (ret) {
                error_report("Could not open new backing file '%s': %s",
                             out_baseimg, error_get_pretty(local_err));
                error_free(local_err);
                goto out;
            }
        }
    }

    /*
     * Check each unallocated cluster in the COW file. If it is unallocated,
     * accesses go to the backing file. We must therefore compare this cluster
     * in the old and new backing file, and if they differ we need to copy it
     * from the old backing file into the COW file.
     *
     * If qemu-img crashes during this step, no harm is done. The content of
     * the image is the same as the original one at any time.
     */
    if (!unsafe) {
        uint64_t num_sectors;
        uint64_t old_backing_num_sectors;
        uint64_t new_backing_num_sectors = 0;
        uint64_t sector;
        int n;
        uint8_t * buf_old;
        uint8_t * buf_new;
        float local_progress = 0;

        buf_old = qemu_blockalign(bs, IO_BUF_SIZE);
        buf_new = qemu_blockalign(bs, IO_BUF_SIZE);

        bdrv_get_geometry(bs, &num_sectors);
        bdrv_get_geometry(bs_old_backing, &old_backing_num_sectors);
        if (bs_new_backing) {
            bdrv_get_geometry(bs_new_backing, &new_backing_num_sectors);
        }

        if (num_sectors != 0) {
            local_progress = (float)100 /
                (num_sectors / MIN(num_sectors, IO_BUF_SIZE / 512));
        }

        for (sector = 0; sector < num_sectors; sector += n) {

            /* How many sectors can we handle with the next read? */
            if (sector + (IO_BUF_SIZE / 512) <= num_sectors) {
                n = (IO_BUF_SIZE / 512);
            } else {
                n = num_sectors - sector;
            }

            /* If the cluster is allocated, we don't need to take action */
            ret = bdrv_is_allocated(bs, sector, n, &n);
            if (ret < 0) {
                error_report("error while reading image metadata: %s",
                             strerror(-ret));
                goto out;
            }
            if (ret) {
                continue;
            }

            /*
             * Read old and new backing file and take into consideration that
             * backing files may be smaller than the COW image.
             */
            if (sector >= old_backing_num_sectors) {
                memset(buf_old, 0, n * BDRV_SECTOR_SIZE);
            } else {
                if (sector + n > old_backing_num_sectors) {
                    n = old_backing_num_sectors - sector;
                }

                ret = bdrv_read(bs_old_backing, sector, buf_old, n);
                if (ret < 0) {
                    error_report("error while reading from old backing file");
                    goto out;
                }
            }

            if (sector >= new_backing_num_sectors || !bs_new_backing) {
                memset(buf_new, 0, n * BDRV_SECTOR_SIZE);
            } else {
                if (sector + n > new_backing_num_sectors) {
                    n = new_backing_num_sectors - sector;
                }

                ret = bdrv_read(bs_new_backing, sector, buf_new, n);
                if (ret < 0) {
                    error_report("error while reading from new backing file");
                    goto out;
                }
            }

            /* If they differ, we need to write to the COW file */
            uint64_t written = 0;

            while (written < n) {
                int pnum;

                if (compare_sectors(buf_old + written * 512,
                    buf_new + written * 512, n - written, &pnum))
                {
                    ret = bdrv_write(bs, sector + written,
                        buf_old + written * 512, pnum);
                    if (ret < 0) {
                        error_report("Error while writing to COW image: %s",
                            strerror(-ret));
                        goto out;
                    }
                }

                written += pnum;
            }
            qemu_progress_print(local_progress, 100);
        }

        qemu_vfree(buf_old);
        qemu_vfree(buf_new);
    }

    /*
     * Change the backing file. All clusters that are different from the old
     * backing file are overwritten in the COW file now, so the visible content
     * doesn't change when we switch the backing file.
     */
    if (out_baseimg && *out_baseimg) {
        ret = bdrv_change_backing_file(bs, out_baseimg, out_basefmt);
    } else {
        ret = bdrv_change_backing_file(bs, NULL, NULL);
    }

    if (ret == -ENOSPC) {
        error_report("Could not change the backing file to '%s': No "
                     "space left in the file header", out_baseimg);
    } else if (ret < 0) {
        error_report("Could not change the backing file to '%s': %s",
            out_baseimg, strerror(-ret));
    }

    qemu_progress_print(100, 0);
    /*
     * TODO At this point it is possible to check if any clusters that are
     * allocated in the COW file are the same in the backing file. If so, they
     * could be dropped from the COW file. Don't do this before switching the
     * backing file, in case of a crash this would lead to corruption.
     */
out:
    qemu_progress_end();
    /* Cleanup */
    if (!unsafe) {
        if (bs_old_backing != NULL) {
            bdrv_unref(bs_old_backing);
        }
        if (bs_new_backing != NULL) {
            bdrv_unref(bs_new_backing);
        }
    }

    bdrv_unref(bs);
    if (ret) {
        return 1;
    }
    return 0;
}

static int img_resize(int argc, char **argv)
{
    int c, ret, relative;
    const char *filename, *fmt, *size;
    int64_t n, total_size;
    bool quiet = false;
    BlockDriverState *bs = NULL;
    QemuOpts *param;
    static QemuOptsList resize_options = {
        .name = "resize_options",
        .head = QTAILQ_HEAD_INITIALIZER(resize_options.head),
        .desc = {
            {
                .name = BLOCK_OPT_SIZE,
                .type = QEMU_OPT_SIZE,
                .help = "Virtual disk size"
            }, {
                /* end of list */
            }
        },
    };

    /* Remove size from argv manually so that negative numbers are not treated
     * as options by getopt. */
    if (argc < 3) {
        error_exit("Not enough arguments");
        return 1;
    }

    size = argv[--argc];

    /* Parse getopt arguments */
    fmt = NULL;
    for(;;) {
        c = getopt(argc, argv, "f:hq");
        if (c == -1) {
            break;
        }
        switch(c) {
        case '?':
        case 'h':
            help();
            break;
        case 'f':
            fmt = optarg;
            break;
        case 'q':
            quiet = true;
            break;
        }
    }
    if (optind != argc - 1) {
        error_exit("Expecting one image file name");
    }
    filename = argv[optind++];

    /* Choose grow, shrink, or absolute resize mode */
    switch (size[0]) {
    case '+':
        relative = 1;
        size++;
        break;
    case '-':
        relative = -1;
        size++;
        break;
    default:
        relative = 0;
        break;
    }

    /* Parse size */
    param = qemu_opts_create(&resize_options, NULL, 0, &error_abort);
    if (qemu_opt_set(param, BLOCK_OPT_SIZE, size)) {
        /* Error message already printed when size parsing fails */
        ret = -1;
        qemu_opts_del(param);
        goto out;
    }
    n = qemu_opt_get_size(param, BLOCK_OPT_SIZE, 0);
    qemu_opts_del(param);

    bs = bdrv_new_open("image", filename, fmt, BDRV_O_FLAGS | BDRV_O_RDWR,
                       true, quiet);
    if (!bs) {
        ret = -1;
        goto out;
    }

    if (relative) {
        total_size = bdrv_getlength(bs) + n * relative;
    } else {
        total_size = n;
    }
    if (total_size <= 0) {
        error_report("New image size must be positive");
        ret = -1;
        goto out;
    }

    ret = bdrv_truncate(bs, total_size);
    switch (ret) {
    case 0:
        qprintf(quiet, "Image resized.\n");
        break;
    case -ENOTSUP:
        error_report("This image does not support resize");
        break;
    case -EACCES:
        error_report("Image is read-only");
        break;
    default:
        error_report("Error resizing image (%d)", -ret);
        break;
    }
out:
    if (bs) {
        bdrv_unref(bs);
    }
    if (ret) {
        return 1;
    }
    return 0;
}

static int img_amend(int argc, char **argv)
{
    int c, ret = 0;
    char *options = NULL;
    QEMUOptionParameter *create_options = NULL, *options_param = NULL;
    const char *fmt = NULL, *filename;
    bool quiet = false;
    BlockDriverState *bs = NULL;

    for (;;) {
        c = getopt(argc, argv, "hqf:o:");
        if (c == -1) {
            break;
        }

        switch (c) {
            case 'h':
            case '?':
                help();
                break;
            case 'o':
                if (!is_valid_option_list(optarg)) {
                    error_report("Invalid option list: %s", optarg);
                    ret = -1;
                    goto out;
                }
                if (!options) {
                    options = g_strdup(optarg);
                } else {
                    char *old_options = options;
                    options = g_strdup_printf("%s,%s", options, optarg);
                    g_free(old_options);
                }
                break;
            case 'f':
                fmt = optarg;
                break;
            case 'q':
                quiet = true;
                break;
        }
    }

    if (!options) {
        error_exit("Must specify options (-o)");
    }

    filename = (optind == argc - 1) ? argv[argc - 1] : NULL;
    if (fmt && has_help_option(options)) {
        /* If a format is explicitly specified (and possibly no filename is
         * given), print option help here */
        ret = print_block_option_help(filename, fmt);
        goto out;
    }

    if (optind != argc - 1) {
        error_exit("Expecting one image file name");
    }

    bs = bdrv_new_open("image", filename, fmt,
                       BDRV_O_FLAGS | BDRV_O_RDWR, true, quiet);
    if (!bs) {
        error_report("Could not open image '%s'", filename);
        ret = -1;
        goto out;
    }

    fmt = bs->drv->format_name;

    if (has_help_option(options)) {
        /* If the format was auto-detected, print option help here */
        ret = print_block_option_help(filename, fmt);
        goto out;
    }

    create_options = append_option_parameters(create_options,
            bs->drv->create_options);
    options_param = parse_option_parameters(options, create_options,
            options_param);
    if (options_param == NULL) {
        error_report("Invalid options for file format '%s'", fmt);
        ret = -1;
        goto out;
    }

    ret = bdrv_amend_options(bs, options_param);
    if (ret < 0) {
        error_report("Error while amending options: %s", strerror(-ret));
        goto out;
    }

out:
    if (bs) {
        bdrv_unref(bs);
    }
    free_option_parameters(create_options);
    free_option_parameters(options_param);
    g_free(options);

    if (ret) {
        return 1;
    }
    return 0;
}

static const img_cmd_t img_cmds[] = {
#define DEF(option, callback, arg_string)        \
    { option, callback },
#include "qemu-img-cmds.h"
#undef DEF
#undef GEN_DOCS
    { NULL, NULL, },
};

int main(int argc, char **argv)
{
    const img_cmd_t *cmd;
    const char *cmdname;
    int c;
    static const struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 'v'},
        {0, 0, 0, 0}
    };

#ifdef CONFIG_POSIX
    signal(SIGPIPE, SIG_IGN);
#endif

    error_set_progname(argv[0]);
    qemu_init_exec_dir(argv[0]);

    qemu_init_main_loop();
    bdrv_init();
    if (argc < 2) {
        error_exit("Not enough arguments");
    }
    cmdname = argv[1];

    /* find the command */
    for (cmd = img_cmds; cmd->name != NULL; cmd++) {
        if (!strcmp(cmdname, cmd->name)) {
            return cmd->handler(argc - 1, argv + 1);
        }
    }

    c = getopt_long(argc, argv, "h", long_options, NULL);

    if (c == 'h') {
        help();
    }
    if (c == 'v') {
        printf(QEMU_IMG_VERSION);
        return 0;
    }

    /* not found */
    error_exit("Command not found: %s", cmdname);
}

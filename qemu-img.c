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

#include "qemu/osdep.h"
#include <getopt.h>

#include "qemu-common.h"
#include "qemu-version.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-block-core.h"
#include "qapi/qapi-visit-block-core.h"
#include "qapi/qobject-output-visitor.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qstring.h"
#include "qemu/cutils.h"
#include "qemu/config-file.h"
#include "qemu/option.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "qom/object_interfaces.h"
#include "sysemu/block-backend.h"
#include "block/block_int.h"
#include "block/blockjob.h"
#include "block/qapi.h"
#include "crypto/init.h"
#include "trace/control.h"

#define QEMU_IMG_VERSION "qemu-img version " QEMU_FULL_VERSION \
                          "\n" QEMU_COPYRIGHT "\n"

typedef struct img_cmd_t {
    const char *name;
    int (*handler)(int argc, char **argv);
} img_cmd_t;

enum {
    OPTION_OUTPUT = 256,
    OPTION_BACKING_CHAIN = 257,
    OPTION_OBJECT = 258,
    OPTION_IMAGE_OPTS = 259,
    OPTION_PATTERN = 260,
    OPTION_FLUSH_INTERVAL = 261,
    OPTION_NO_DRAIN = 262,
    OPTION_TARGET_IMAGE_OPTS = 263,
    OPTION_SIZE = 264,
    OPTION_PREALLOCATION = 265,
    OPTION_SHRINK = 266,
    OPTION_SALVAGE = 267,
    OPTION_TARGET_IS_ZERO = 268,
    OPTION_ADD = 269,
    OPTION_REMOVE = 270,
    OPTION_CLEAR = 271,
    OPTION_ENABLE = 272,
    OPTION_DISABLE = 273,
    OPTION_MERGE = 274,
    OPTION_BITMAPS = 275,
    OPTION_FORCE = 276,
};

typedef enum OutputFormat {
    OFORMAT_JSON,
    OFORMAT_HUMAN,
} OutputFormat;

/* Default to cache=writeback as data integrity is not important for qemu-img */
#define BDRV_DEFAULT_CACHE "writeback"

static void format_print(void *opaque, const char *name)
{
    printf(" %s", name);
}

static void QEMU_NORETURN GCC_FMT_ATTR(1, 2) error_exit(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    error_vreport(fmt, ap);
    va_end(ap);

    error_printf("Try 'qemu-img --help' for more information\n");
    exit(EXIT_FAILURE);
}

static void QEMU_NORETURN missing_argument(const char *option)
{
    error_exit("missing argument for option '%s'", option);
}

static void QEMU_NORETURN unrecognized_option(const char *option)
{
    error_exit("unrecognized option '%s'", option);
}

/* Please keep in synch with docs/tools/qemu-img.rst */
static void QEMU_NORETURN help(void)
{
    const char *help_msg =
           QEMU_IMG_VERSION
           "usage: qemu-img [standard options] command [command options]\n"
           "QEMU disk image utility\n"
           "\n"
           "    '-h', '--help'       display this help and exit\n"
           "    '-V', '--version'    output version information and exit\n"
           "    '-T', '--trace'      [[enable=]<pattern>][,events=<file>][,file=<file>]\n"
           "                         specify tracing options\n"
           "\n"
           "Command syntax:\n"
#define DEF(option, callback, arg_string)        \
           "  " arg_string "\n"
#include "qemu-img-cmds.h"
#undef DEF
           "\n"
           "Command parameters:\n"
           "  'filename' is a disk image filename\n"
           "  'objectdef' is a QEMU user creatable object definition. See the qemu(1)\n"
           "    manual page for a description of the object properties. The most common\n"
           "    object type is a 'secret', which is used to supply passwords and/or\n"
           "    encryption keys.\n"
           "  'fmt' is the disk image format. It is guessed automatically in most cases\n"
           "  'cache' is the cache mode used to write the output disk image, the valid\n"
           "    options are: 'none', 'writeback' (default, except for convert), 'writethrough',\n"
           "    'directsync' and 'unsafe' (default for convert)\n"
           "  'src_cache' is the cache mode used to read input disk images, the valid\n"
           "    options are the same as for the 'cache' option\n"
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
           "  '-c' indicates that target image must be compressed (qcow format only)\n"
           "  '-u' allows unsafe backing chains. For rebasing, it is assumed that old and\n"
           "       new backing file match exactly. The image doesn't need a working\n"
           "       backing file before rebasing in this case (useful for renaming the\n"
           "       backing file). For image creation, allow creating without attempting\n"
           "       to open the backing file.\n"
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
           "Parameters to bitmap subcommand:\n"
           "  'bitmap' is the name of the bitmap to manipulate, through one or more\n"
           "       actions from '--add', '--remove', '--clear', '--enable', '--disable',\n"
           "       or '--merge source'\n"
           "  '-g granularity' sets the granularity for '--add' actions\n"
           "  '-b source' and '-F src_fmt' tell '--merge' actions to find the source\n"
           "       bitmaps from an alternative file\n"
           "\n"
           "Parameters to check subcommand:\n"
           "  '-r' tries to repair any inconsistencies that are found during the check.\n"
           "       '-r leaks' repairs only cluster leaks, whereas '-r all' fixes all\n"
           "       kinds of errors, with a higher risk of choosing the wrong fix or\n"
           "       hiding corruption that has already occurred.\n"
           "\n"
           "Parameters to convert subcommand:\n"
           "  '--bitmaps' copies all top-level persistent bitmaps to destination\n"
           "  '-m' specifies how many coroutines work in parallel during the convert\n"
           "       process (defaults to 8)\n"
           "  '-W' allow to write to the target out of order rather than sequential\n"
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
           "  '-s' run in Strict mode - fail on different image size or sector allocation\n"
           "\n"
           "Parameters to dd subcommand:\n"
           "  'bs=BYTES' read and write up to BYTES bytes at a time "
           "(default: 512)\n"
           "  'count=N' copy only N input blocks\n"
           "  'if=FILE' read from FILE\n"
           "  'of=FILE' write to FILE\n"
           "  'skip=N' skip N bs-sized blocks at the start of input\n";

    printf("%s\nSupported formats:", help_msg);
    bdrv_iterate_format(format_print, NULL, false);
    printf("\n\n" QEMU_HELP_BOTTOM "\n");
    exit(EXIT_SUCCESS);
}

static QemuOptsList qemu_object_opts = {
    .name = "object",
    .implied_opt_name = "qom-type",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_object_opts.head),
    .desc = {
        { }
    },
};

static bool qemu_img_object_print_help(const char *type, QemuOpts *opts)
{
    if (user_creatable_print_help(type, opts)) {
        exit(0);
    }
    return true;
}

/*
 * Is @optarg safe for accumulate_options()?
 * It is when multiple of them can be joined together separated by ','.
 * To make that work, @optarg must not start with ',' (or else a
 * separating ',' preceding it gets escaped), and it must not end with
 * an odd number of ',' (or else a separating ',' following it gets
 * escaped), or be empty (or else a separating ',' preceding it can
 * escape a separating ',' following it).
 * 
 */
static bool is_valid_option_list(const char *optarg)
{
    size_t len = strlen(optarg);
    size_t i;

    if (!optarg[0] || optarg[0] == ',') {
        return false;
    }

    for (i = len; i > 0 && optarg[i - 1] == ','; i--) {
    }
    if ((len - i) % 2) {
        return false;
    }

    return true;
}

static int accumulate_options(char **options, char *optarg)
{
    char *new_options;

    if (!is_valid_option_list(optarg)) {
        error_report("Invalid option list: %s", optarg);
        return -1;
    }

    if (!*options) {
        *options = g_strdup(optarg);
    } else {
        new_options = g_strdup_printf("%s,%s", *options, optarg);
        g_free(*options);
        *options = new_options;
    }
    return 0;
}

static QemuOptsList qemu_source_opts = {
    .name = "source",
    .implied_opt_name = "file",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_source_opts.head),
    .desc = {
        { }
    },
};

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


static int print_block_option_help(const char *filename, const char *fmt)
{
    BlockDriver *drv, *proto_drv;
    QemuOptsList *create_opts = NULL;
    Error *local_err = NULL;

    /* Find driver and parse its options */
    drv = bdrv_find_format(fmt);
    if (!drv) {
        error_report("Unknown file format '%s'", fmt);
        return 1;
    }

    if (!drv->create_opts) {
        error_report("Format driver '%s' does not support image creation", fmt);
        return 1;
    }

    create_opts = qemu_opts_append(create_opts, drv->create_opts);
    if (filename) {
        proto_drv = bdrv_find_protocol(filename, true, &local_err);
        if (!proto_drv) {
            error_report_err(local_err);
            qemu_opts_free(create_opts);
            return 1;
        }
        if (!proto_drv->create_opts) {
            error_report("Protocol driver '%s' does not support image creation",
                         proto_drv->format_name);
            qemu_opts_free(create_opts);
            return 1;
        }
        create_opts = qemu_opts_append(create_opts, proto_drv->create_opts);
    }

    if (filename) {
        printf("Supported options:\n");
    } else {
        printf("Supported %s options:\n", fmt);
    }
    qemu_opts_print_help(create_opts, false);
    qemu_opts_free(create_opts);

    if (!filename) {
        printf("\n"
               "The protocol level may support further options.\n"
               "Specify the target filename to include those options.\n");
    }

    return 0;
}


static BlockBackend *img_open_opts(const char *optstr,
                                   QemuOpts *opts, int flags, bool writethrough,
                                   bool quiet, bool force_share)
{
    QDict *options;
    Error *local_err = NULL;
    BlockBackend *blk;
    options = qemu_opts_to_qdict(opts, NULL);
    if (force_share) {
        if (qdict_haskey(options, BDRV_OPT_FORCE_SHARE)
            && strcmp(qdict_get_str(options, BDRV_OPT_FORCE_SHARE), "on")) {
            error_report("--force-share/-U conflicts with image options");
            qobject_unref(options);
            return NULL;
        }
        qdict_put_str(options, BDRV_OPT_FORCE_SHARE, "on");
    }
    blk = blk_new_open(NULL, NULL, options, flags, &local_err);
    if (!blk) {
        error_reportf_err(local_err, "Could not open '%s': ", optstr);
        return NULL;
    }
    blk_set_enable_write_cache(blk, !writethrough);

    return blk;
}

static BlockBackend *img_open_file(const char *filename,
                                   QDict *options,
                                   const char *fmt, int flags,
                                   bool writethrough, bool quiet,
                                   bool force_share)
{
    BlockBackend *blk;
    Error *local_err = NULL;

    if (!options) {
        options = qdict_new();
    }
    if (fmt) {
        qdict_put_str(options, "driver", fmt);
    }

    if (force_share) {
        qdict_put_bool(options, BDRV_OPT_FORCE_SHARE, true);
    }
    blk = blk_new_open(filename, NULL, options, flags, &local_err);
    if (!blk) {
        error_reportf_err(local_err, "Could not open '%s': ", filename);
        return NULL;
    }
    blk_set_enable_write_cache(blk, !writethrough);

    return blk;
}


static int img_add_key_secrets(void *opaque,
                               const char *name, const char *value,
                               Error **errp)
{
    QDict *options = opaque;

    if (g_str_has_suffix(name, "key-secret")) {
        qdict_put_str(options, name, value);
    }

    return 0;
}


static BlockBackend *img_open(bool image_opts,
                              const char *filename,
                              const char *fmt, int flags, bool writethrough,
                              bool quiet, bool force_share)
{
    BlockBackend *blk;
    if (image_opts) {
        QemuOpts *opts;
        if (fmt) {
            error_report("--image-opts and --format are mutually exclusive");
            return NULL;
        }
        opts = qemu_opts_parse_noisily(qemu_find_opts("source"),
                                       filename, true);
        if (!opts) {
            return NULL;
        }
        blk = img_open_opts(filename, opts, flags, writethrough, quiet,
                            force_share);
    } else {
        blk = img_open_file(filename, NULL, fmt, flags, writethrough, quiet,
                            force_share);
    }
    return blk;
}


static int add_old_style_options(const char *fmt, QemuOpts *opts,
                                 const char *base_filename,
                                 const char *base_fmt)
{
    if (base_filename) {
        if (!qemu_opt_set(opts, BLOCK_OPT_BACKING_FILE, base_filename,
                          NULL)) {
            error_report("Backing file not supported for file format '%s'",
                         fmt);
            return -1;
        }
    }
    if (base_fmt) {
        if (!qemu_opt_set(opts, BLOCK_OPT_BACKING_FMT, base_fmt, NULL)) {
            error_report("Backing file format not supported for file "
                         "format '%s'", fmt);
            return -1;
        }
    }
    return 0;
}

static int64_t cvtnum_full(const char *name, const char *value, int64_t min,
                           int64_t max)
{
    int err;
    uint64_t res;

    err = qemu_strtosz(value, NULL, &res);
    if (err < 0 && err != -ERANGE) {
        error_report("Invalid %s specified. You may use "
                     "k, M, G, T, P or E suffixes for", name);
        error_report("kilobytes, megabytes, gigabytes, terabytes, "
                     "petabytes and exabytes.");
        return err;
    }
    if (err == -ERANGE || res > max || res < min) {
        error_report("Invalid %s specified. Must be between %" PRId64
                     " and %" PRId64 ".", name, min, max);
        return -ERANGE;
    }
    return res;
}

static int64_t cvtnum(const char *name, const char *value)
{
    return cvtnum_full(name, value, 0, INT64_MAX);
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
    int flags = 0;

    for(;;) {
        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"object", required_argument, 0, OPTION_OBJECT},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, ":F:b:f:ho:qu",
                        long_options, NULL);
        if (c == -1) {
            break;
        }
        switch(c) {
        case ':':
            missing_argument(argv[optind - 1]);
            break;
        case '?':
            unrecognized_option(argv[optind - 1]);
            break;
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
        case 'o':
            if (accumulate_options(&options, optarg) < 0) {
                goto fail;
            }
            break;
        case 'q':
            quiet = true;
            break;
        case 'u':
            flags |= BDRV_O_NO_BACKING;
            break;
        case OPTION_OBJECT: {
            QemuOpts *opts;
            opts = qemu_opts_parse_noisily(&qemu_object_opts,
                                           optarg, true);
            if (!opts) {
                goto fail;
            }
        }   break;
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

    if (qemu_opts_foreach(&qemu_object_opts,
                          user_creatable_add_opts_foreach,
                          qemu_img_object_print_help, &error_fatal)) {
        goto fail;
    }

    /* Get image size, if specified */
    if (optind < argc) {
        int64_t sval;

        sval = cvtnum("image size", argv[optind++]);
        if (sval < 0) {
            goto fail;
        }
        img_size = (uint64_t)sval;
    }
    if (optind != argc) {
        error_exit("Unexpected argument: %s", argv[optind]);
    }

    bdrv_img_create(filename, fmt, base_filename, base_fmt,
                    options, img_size, flags, quiet, &local_err);
    if (local_err) {
        error_reportf_err(local_err, "%s: ", filename);
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
    QString *str;
    QObject *obj;
    Visitor *v = qobject_output_visitor_new(&obj);

    visit_type_ImageCheck(v, NULL, &check, &error_abort);
    visit_complete(v, &obj);
    str = qobject_to_json_pretty(obj);
    assert(str != NULL);
    qprintf(quiet, "%s\n", qstring_get_str(str));
    qobject_unref(obj);
    visit_free(v);
    qobject_unref(str);
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
    check->has_corruptions_fixed    = result.corruptions_fixed != 0;
    check->leaks_fixed              = result.leaks_fixed;
    check->has_leaks_fixed          = result.leaks_fixed != 0;
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
 *  0 - Check completed, image is good
 *  1 - Check not completed because of internal errors
 *  2 - Check completed, image is corrupted
 *  3 - Check completed, image has leaked clusters, but is good otherwise
 * 63 - Checks are not supported by the image format
 */
static int img_check(int argc, char **argv)
{
    int c, ret;
    OutputFormat output_format = OFORMAT_HUMAN;
    const char *filename, *fmt, *output, *cache;
    BlockBackend *blk;
    BlockDriverState *bs;
    int fix = 0;
    int flags = BDRV_O_CHECK;
    bool writethrough;
    ImageCheck *check;
    bool quiet = false;
    bool image_opts = false;
    bool force_share = false;

    fmt = NULL;
    output = NULL;
    cache = BDRV_DEFAULT_CACHE;

    for(;;) {
        int option_index = 0;
        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"format", required_argument, 0, 'f'},
            {"repair", required_argument, 0, 'r'},
            {"output", required_argument, 0, OPTION_OUTPUT},
            {"object", required_argument, 0, OPTION_OBJECT},
            {"image-opts", no_argument, 0, OPTION_IMAGE_OPTS},
            {"force-share", no_argument, 0, 'U'},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, ":hf:r:T:qU",
                        long_options, &option_index);
        if (c == -1) {
            break;
        }
        switch(c) {
        case ':':
            missing_argument(argv[optind - 1]);
            break;
        case '?':
            unrecognized_option(argv[optind - 1]);
            break;
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
        case 'T':
            cache = optarg;
            break;
        case 'q':
            quiet = true;
            break;
        case 'U':
            force_share = true;
            break;
        case OPTION_OBJECT: {
            QemuOpts *opts;
            opts = qemu_opts_parse_noisily(&qemu_object_opts,
                                           optarg, true);
            if (!opts) {
                return 1;
            }
        }   break;
        case OPTION_IMAGE_OPTS:
            image_opts = true;
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

    if (qemu_opts_foreach(&qemu_object_opts,
                          user_creatable_add_opts_foreach,
                          qemu_img_object_print_help, &error_fatal)) {
        return 1;
    }

    ret = bdrv_parse_cache_mode(cache, &flags, &writethrough);
    if (ret < 0) {
        error_report("Invalid source cache option: %s", cache);
        return 1;
    }

    blk = img_open(image_opts, filename, fmt, flags, writethrough, quiet,
                   force_share);
    if (!blk) {
        return 1;
    }
    bs = blk_bs(blk);

    check = g_new0(ImageCheck, 1);
    ret = collect_image_check(bs, check, filename, fmt, fix);

    if (ret == -ENOTSUP) {
        error_report("This image format does not support checks");
        ret = 63;
        goto fail;
    }

    if (check->corruptions_fixed || check->leaks_fixed) {
        int corruptions_fixed, leaks_fixed;
        bool has_leaks_fixed, has_corruptions_fixed;

        leaks_fixed         = check->leaks_fixed;
        has_leaks_fixed     = check->has_leaks_fixed;
        corruptions_fixed   = check->corruptions_fixed;
        has_corruptions_fixed = check->has_corruptions_fixed;

        if (output_format == OFORMAT_HUMAN) {
            qprintf(quiet,
                    "The following inconsistencies were found and repaired:\n\n"
                    "    %" PRId64 " leaked clusters\n"
                    "    %" PRId64 " corruptions\n\n"
                    "Double checking the fixed image now...\n",
                    check->leaks_fixed,
                    check->corruptions_fixed);
        }

        qapi_free_ImageCheck(check);
        check = g_new0(ImageCheck, 1);
        ret = collect_image_check(bs, check, filename, fmt, 0);

        check->leaks_fixed          = leaks_fixed;
        check->has_leaks_fixed      = has_leaks_fixed;
        check->corruptions_fixed    = corruptions_fixed;
        check->has_corruptions_fixed = has_corruptions_fixed;
    }

    if (!ret) {
        switch (output_format) {
        case OFORMAT_HUMAN:
            dump_human_image_check(check, quiet);
            break;
        case OFORMAT_JSON:
            dump_json_image_check(check, quiet);
            break;
        }
    }

    if (ret || check->check_errors) {
        if (ret) {
            error_report("Check failed: %s", strerror(-ret));
        } else {
            error_report("Check failed");
        }
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
    blk_unref(blk);
    return ret;
}

typedef struct CommonBlockJobCBInfo {
    BlockDriverState *bs;
    Error **errp;
} CommonBlockJobCBInfo;

static void common_block_job_cb(void *opaque, int ret)
{
    CommonBlockJobCBInfo *cbi = opaque;

    if (ret < 0) {
        error_setg_errno(cbi->errp, -ret, "Block job failed");
    }
}

static void run_block_job(BlockJob *job, Error **errp)
{
    AioContext *aio_context = blk_get_aio_context(job->blk);
    int ret = 0;

    aio_context_acquire(aio_context);
    job_ref(&job->job);
    do {
        float progress = 0.0f;
        aio_poll(aio_context, true);
        if (job->job.progress.total) {
            progress = (float)job->job.progress.current /
                       job->job.progress.total * 100.f;
        }
        qemu_progress_print(progress, 0);
    } while (!job_is_ready(&job->job) && !job_is_completed(&job->job));

    if (!job_is_completed(&job->job)) {
        ret = job_complete_sync(&job->job, errp);
    } else {
        ret = job->job.ret;
    }
    job_unref(&job->job);
    aio_context_release(aio_context);

    /* publish completion progress only when success */
    if (!ret) {
        qemu_progress_print(100.f, 0);
    }
}

static int img_commit(int argc, char **argv)
{
    int c, ret, flags;
    const char *filename, *fmt, *cache, *base;
    BlockBackend *blk;
    BlockDriverState *bs, *base_bs;
    BlockJob *job;
    bool progress = false, quiet = false, drop = false;
    bool writethrough;
    Error *local_err = NULL;
    CommonBlockJobCBInfo cbi;
    bool image_opts = false;
    AioContext *aio_context;

    fmt = NULL;
    cache = BDRV_DEFAULT_CACHE;
    base = NULL;
    for(;;) {
        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"object", required_argument, 0, OPTION_OBJECT},
            {"image-opts", no_argument, 0, OPTION_IMAGE_OPTS},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, ":f:ht:b:dpq",
                        long_options, NULL);
        if (c == -1) {
            break;
        }
        switch(c) {
        case ':':
            missing_argument(argv[optind - 1]);
            break;
        case '?':
            unrecognized_option(argv[optind - 1]);
            break;
        case 'h':
            help();
            break;
        case 'f':
            fmt = optarg;
            break;
        case 't':
            cache = optarg;
            break;
        case 'b':
            base = optarg;
            /* -b implies -d */
            drop = true;
            break;
        case 'd':
            drop = true;
            break;
        case 'p':
            progress = true;
            break;
        case 'q':
            quiet = true;
            break;
        case OPTION_OBJECT: {
            QemuOpts *opts;
            opts = qemu_opts_parse_noisily(&qemu_object_opts,
                                           optarg, true);
            if (!opts) {
                return 1;
            }
        }   break;
        case OPTION_IMAGE_OPTS:
            image_opts = true;
            break;
        }
    }

    /* Progress is not shown in Quiet mode */
    if (quiet) {
        progress = false;
    }

    if (optind != argc - 1) {
        error_exit("Expecting one image file name");
    }
    filename = argv[optind++];

    if (qemu_opts_foreach(&qemu_object_opts,
                          user_creatable_add_opts_foreach,
                          qemu_img_object_print_help, &error_fatal)) {
        return 1;
    }

    flags = BDRV_O_RDWR | BDRV_O_UNMAP;
    ret = bdrv_parse_cache_mode(cache, &flags, &writethrough);
    if (ret < 0) {
        error_report("Invalid cache option: %s", cache);
        return 1;
    }

    blk = img_open(image_opts, filename, fmt, flags, writethrough, quiet,
                   false);
    if (!blk) {
        return 1;
    }
    bs = blk_bs(blk);

    qemu_progress_init(progress, 1.f);
    qemu_progress_print(0.f, 100);

    if (base) {
        base_bs = bdrv_find_backing_image(bs, base);
        if (!base_bs) {
            error_setg(&local_err,
                       "Did not find '%s' in the backing chain of '%s'",
                       base, filename);
            goto done;
        }
    } else {
        /* This is different from QMP, which by default uses the deepest file in
         * the backing chain (i.e., the very base); however, the traditional
         * behavior of qemu-img commit is using the immediate backing file. */
        base_bs = backing_bs(bs);
        if (!base_bs) {
            error_setg(&local_err, "Image does not have a backing file");
            goto done;
        }
    }

    cbi = (CommonBlockJobCBInfo){
        .errp = &local_err,
        .bs   = bs,
    };

    aio_context = bdrv_get_aio_context(bs);
    aio_context_acquire(aio_context);
    commit_active_start("commit", bs, base_bs, JOB_DEFAULT, 0,
                        BLOCKDEV_ON_ERROR_REPORT, NULL, common_block_job_cb,
                        &cbi, false, &local_err);
    aio_context_release(aio_context);
    if (local_err) {
        goto done;
    }

    /* When the block job completes, the BlockBackend reference will point to
     * the old backing file. In order to avoid that the top image is already
     * deleted, so we can still empty it afterwards, increment the reference
     * counter here preemptively. */
    if (!drop) {
        bdrv_ref(bs);
    }

    job = block_job_get("commit");
    assert(job);
    run_block_job(job, &local_err);
    if (local_err) {
        goto unref_backing;
    }

    if (!drop) {
        BlockBackend *old_backing_blk;

        old_backing_blk = blk_new_with_bs(bs, BLK_PERM_WRITE, BLK_PERM_ALL,
                                          &local_err);
        if (!old_backing_blk) {
            goto unref_backing;
        }
        ret = blk_make_empty(old_backing_blk, &local_err);
        blk_unref(old_backing_blk);
        if (ret == -ENOTSUP) {
            error_free(local_err);
            local_err = NULL;
        } else if (ret < 0) {
            goto unref_backing;
        }
    }

unref_backing:
    if (!drop) {
        bdrv_unref(bs);
    }

done:
    qemu_progress_end();

    blk_unref(blk);

    if (local_err) {
        error_report_err(local_err);
        return 1;
    }

    qprintf(quiet, "Image committed.\n");
    return 0;
}

/*
 * Returns -1 if 'buf' contains only zeroes, otherwise the byte index
 * of the first sector boundary within buf where the sector contains a
 * non-zero byte.  This function is robust to a buffer that is not
 * sector-aligned.
 */
static int64_t find_nonzero(const uint8_t *buf, int64_t n)
{
    int64_t i;
    int64_t end = QEMU_ALIGN_DOWN(n, BDRV_SECTOR_SIZE);

    for (i = 0; i < end; i += BDRV_SECTOR_SIZE) {
        if (!buffer_is_zero(buf + i, BDRV_SECTOR_SIZE)) {
            return i;
        }
    }
    if (i < n && !buffer_is_zero(buf + i, n - end)) {
        return i;
    }
    return -1;
}

/*
 * Returns true iff the first sector pointed to by 'buf' contains at least
 * a non-NUL byte.
 *
 * 'pnum' is set to the number of sectors (including and immediately following
 * the first one) that are known to be in the same allocated/unallocated state.
 * The function will try to align the end offset to alignment boundaries so
 * that the request will at least end aligned and consequtive requests will
 * also start at an aligned offset.
 */
static int is_allocated_sectors(const uint8_t *buf, int n, int *pnum,
                                int64_t sector_num, int alignment)
{
    bool is_zero;
    int i, tail;

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

    tail = (sector_num + i) & (alignment - 1);
    if (tail) {
        if (is_zero && i <= tail) {
            /* treat unallocated areas which only consist
             * of a small tail as allocated. */
            is_zero = false;
        }
        if (!is_zero) {
            /* align up end offset of allocated areas. */
            i += alignment - tail;
            i = MIN(i, n);
        } else {
            /* align down end offset of zero areas. */
            i -= tail;
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
    int min, int64_t sector_num, int alignment)
{
    int ret;
    int num_checked, num_used;

    if (n < min) {
        min = n;
    }

    ret = is_allocated_sectors(buf, n, pnum, sector_num, alignment);
    if (!ret) {
        return ret;
    }

    num_used = *pnum;
    buf += BDRV_SECTOR_SIZE * *pnum;
    n -= *pnum;
    sector_num += *pnum;
    num_checked = num_used;

    while (n > 0) {
        ret = is_allocated_sectors(buf, n, pnum, sector_num, alignment);

        buf += BDRV_SECTOR_SIZE * *pnum;
        n -= *pnum;
        sector_num += *pnum;
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
 * Compares two buffers sector by sector. Returns 0 if the first
 * sector of each buffer matches, non-zero otherwise.
 *
 * pnum is set to the sector-aligned size of the buffer prefix that
 * has the same matching status as the first sector.
 */
static int compare_buffers(const uint8_t *buf1, const uint8_t *buf2,
                           int64_t bytes, int64_t *pnum)
{
    bool res;
    int64_t i = MIN(bytes, BDRV_SECTOR_SIZE);

    assert(bytes > 0);

    res = !!memcmp(buf1, buf2, i);
    while (i < bytes) {
        int64_t len = MIN(bytes - i, BDRV_SECTOR_SIZE);

        if (!!memcmp(buf1 + i, buf2 + i, len) != res) {
            break;
        }
        i += len;
    }

    *pnum = i;
    return res;
}

#define IO_BUF_SIZE (2 * MiB)

/*
 * Check if passed sectors are empty (not allocated or contain only 0 bytes)
 *
 * Intended for use by 'qemu-img compare': Returns 0 in case sectors are
 * filled with 0, 1 if sectors contain non-zero data (this is a comparison
 * failure), and 4 on error (the exit status for read errors), after emitting
 * an error message.
 *
 * @param blk:  BlockBackend for the image
 * @param offset: Starting offset to check
 * @param bytes: Number of bytes to check
 * @param filename: Name of disk file we are checking (logging purpose)
 * @param buffer: Allocated buffer for storing read data
 * @param quiet: Flag for quiet mode
 */
static int check_empty_sectors(BlockBackend *blk, int64_t offset,
                               int64_t bytes, const char *filename,
                               uint8_t *buffer, bool quiet)
{
    int ret = 0;
    int64_t idx;

    ret = blk_pread(blk, offset, buffer, bytes);
    if (ret < 0) {
        error_report("Error while reading offset %" PRId64 " of %s: %s",
                     offset, filename, strerror(-ret));
        return 4;
    }
    idx = find_nonzero(buffer, bytes);
    if (idx >= 0) {
        qprintf(quiet, "Content mismatch at offset %" PRId64 "!\n",
                offset + idx);
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
    const char *fmt1 = NULL, *fmt2 = NULL, *cache, *filename1, *filename2;
    BlockBackend *blk1, *blk2;
    BlockDriverState *bs1, *bs2;
    int64_t total_size1, total_size2;
    uint8_t *buf1 = NULL, *buf2 = NULL;
    int64_t pnum1, pnum2;
    int allocated1, allocated2;
    int ret = 0; /* return value - 0 Ident, 1 Different, >1 Error */
    bool progress = false, quiet = false, strict = false;
    int flags;
    bool writethrough;
    int64_t total_size;
    int64_t offset = 0;
    int64_t chunk;
    int c;
    uint64_t progress_base;
    bool image_opts = false;
    bool force_share = false;

    cache = BDRV_DEFAULT_CACHE;
    for (;;) {
        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"object", required_argument, 0, OPTION_OBJECT},
            {"image-opts", no_argument, 0, OPTION_IMAGE_OPTS},
            {"force-share", no_argument, 0, 'U'},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, ":hf:F:T:pqsU",
                        long_options, NULL);
        if (c == -1) {
            break;
        }
        switch (c) {
        case ':':
            missing_argument(argv[optind - 1]);
            break;
        case '?':
            unrecognized_option(argv[optind - 1]);
            break;
        case 'h':
            help();
            break;
        case 'f':
            fmt1 = optarg;
            break;
        case 'F':
            fmt2 = optarg;
            break;
        case 'T':
            cache = optarg;
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
        case 'U':
            force_share = true;
            break;
        case OPTION_OBJECT: {
            QemuOpts *opts;
            opts = qemu_opts_parse_noisily(&qemu_object_opts,
                                           optarg, true);
            if (!opts) {
                ret = 2;
                goto out4;
            }
        }   break;
        case OPTION_IMAGE_OPTS:
            image_opts = true;
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

    if (qemu_opts_foreach(&qemu_object_opts,
                          user_creatable_add_opts_foreach,
                          qemu_img_object_print_help, &error_fatal)) {
        ret = 2;
        goto out4;
    }

    /* Initialize before goto out */
    qemu_progress_init(progress, 2.0);

    flags = 0;
    ret = bdrv_parse_cache_mode(cache, &flags, &writethrough);
    if (ret < 0) {
        error_report("Invalid source cache option: %s", cache);
        ret = 2;
        goto out3;
    }

    blk1 = img_open(image_opts, filename1, fmt1, flags, writethrough, quiet,
                    force_share);
    if (!blk1) {
        ret = 2;
        goto out3;
    }

    blk2 = img_open(image_opts, filename2, fmt2, flags, writethrough, quiet,
                    force_share);
    if (!blk2) {
        ret = 2;
        goto out2;
    }
    bs1 = blk_bs(blk1);
    bs2 = blk_bs(blk2);

    buf1 = blk_blockalign(blk1, IO_BUF_SIZE);
    buf2 = blk_blockalign(blk2, IO_BUF_SIZE);
    total_size1 = blk_getlength(blk1);
    if (total_size1 < 0) {
        error_report("Can't get size of %s: %s",
                     filename1, strerror(-total_size1));
        ret = 4;
        goto out;
    }
    total_size2 = blk_getlength(blk2);
    if (total_size2 < 0) {
        error_report("Can't get size of %s: %s",
                     filename2, strerror(-total_size2));
        ret = 4;
        goto out;
    }
    total_size = MIN(total_size1, total_size2);
    progress_base = MAX(total_size1, total_size2);

    qemu_progress_print(0, 100);

    if (strict && total_size1 != total_size2) {
        ret = 1;
        qprintf(quiet, "Strict mode: Image size mismatch!\n");
        goto out;
    }

    while (offset < total_size) {
        int status1, status2;

        status1 = bdrv_block_status_above(bs1, NULL, offset,
                                          total_size1 - offset, &pnum1, NULL,
                                          NULL);
        if (status1 < 0) {
            ret = 3;
            error_report("Sector allocation test failed for %s", filename1);
            goto out;
        }
        allocated1 = status1 & BDRV_BLOCK_ALLOCATED;

        status2 = bdrv_block_status_above(bs2, NULL, offset,
                                          total_size2 - offset, &pnum2, NULL,
                                          NULL);
        if (status2 < 0) {
            ret = 3;
            error_report("Sector allocation test failed for %s", filename2);
            goto out;
        }
        allocated2 = status2 & BDRV_BLOCK_ALLOCATED;

        assert(pnum1 && pnum2);
        chunk = MIN(pnum1, pnum2);

        if (strict) {
            if (status1 != status2) {
                ret = 1;
                qprintf(quiet, "Strict mode: Offset %" PRId64
                        " block status mismatch!\n", offset);
                goto out;
            }
        }
        if ((status1 & BDRV_BLOCK_ZERO) && (status2 & BDRV_BLOCK_ZERO)) {
            /* nothing to do */
        } else if (allocated1 == allocated2) {
            if (allocated1) {
                int64_t pnum;

                chunk = MIN(chunk, IO_BUF_SIZE);
                ret = blk_pread(blk1, offset, buf1, chunk);
                if (ret < 0) {
                    error_report("Error while reading offset %" PRId64
                                 " of %s: %s",
                                 offset, filename1, strerror(-ret));
                    ret = 4;
                    goto out;
                }
                ret = blk_pread(blk2, offset, buf2, chunk);
                if (ret < 0) {
                    error_report("Error while reading offset %" PRId64
                                 " of %s: %s",
                                 offset, filename2, strerror(-ret));
                    ret = 4;
                    goto out;
                }
                ret = compare_buffers(buf1, buf2, chunk, &pnum);
                if (ret || pnum != chunk) {
                    qprintf(quiet, "Content mismatch at offset %" PRId64 "!\n",
                            offset + (ret ? 0 : pnum));
                    ret = 1;
                    goto out;
                }
            }
        } else {
            chunk = MIN(chunk, IO_BUF_SIZE);
            if (allocated1) {
                ret = check_empty_sectors(blk1, offset, chunk,
                                          filename1, buf1, quiet);
            } else {
                ret = check_empty_sectors(blk2, offset, chunk,
                                          filename2, buf1, quiet);
            }
            if (ret) {
                goto out;
            }
        }
        offset += chunk;
        qemu_progress_print(((float) chunk / progress_base) * 100, 100);
    }

    if (total_size1 != total_size2) {
        BlockBackend *blk_over;
        const char *filename_over;

        qprintf(quiet, "Warning: Image size mismatch!\n");
        if (total_size1 > total_size2) {
            blk_over = blk1;
            filename_over = filename1;
        } else {
            blk_over = blk2;
            filename_over = filename2;
        }

        while (offset < progress_base) {
            ret = bdrv_block_status_above(blk_bs(blk_over), NULL, offset,
                                          progress_base - offset, &chunk,
                                          NULL, NULL);
            if (ret < 0) {
                ret = 3;
                error_report("Sector allocation test failed for %s",
                             filename_over);
                goto out;

            }
            if (ret & BDRV_BLOCK_ALLOCATED && !(ret & BDRV_BLOCK_ZERO)) {
                chunk = MIN(chunk, IO_BUF_SIZE);
                ret = check_empty_sectors(blk_over, offset, chunk,
                                          filename_over, buf1, quiet);
                if (ret) {
                    goto out;
                }
            }
            offset += chunk;
            qemu_progress_print(((float) chunk / progress_base) * 100, 100);
        }
    }

    qprintf(quiet, "Images are identical.\n");
    ret = 0;

out:
    qemu_vfree(buf1);
    qemu_vfree(buf2);
    blk_unref(blk2);
out2:
    blk_unref(blk1);
out3:
    qemu_progress_end();
out4:
    return ret;
}

/* Convenience wrapper around qmp_block_dirty_bitmap_merge */
static void do_dirty_bitmap_merge(const char *dst_node, const char *dst_name,
                                  const char *src_node, const char *src_name,
                                  Error **errp)
{
    BlockDirtyBitmapMergeSource *merge_src;
    BlockDirtyBitmapMergeSourceList *list;

    merge_src = g_new0(BlockDirtyBitmapMergeSource, 1);
    merge_src->type = QTYPE_QDICT;
    merge_src->u.external.node = g_strdup(src_node);
    merge_src->u.external.name = g_strdup(src_name);
    list = g_new0(BlockDirtyBitmapMergeSourceList, 1);
    list->value = merge_src;
    qmp_block_dirty_bitmap_merge(dst_node, dst_name, list, errp);
    qapi_free_BlockDirtyBitmapMergeSourceList(list);
}

enum ImgConvertBlockStatus {
    BLK_DATA,
    BLK_ZERO,
    BLK_BACKING_FILE,
};

#define MAX_COROUTINES 16

typedef struct ImgConvertState {
    BlockBackend **src;
    int64_t *src_sectors;
    int src_num;
    int64_t total_sectors;
    int64_t allocated_sectors;
    int64_t allocated_done;
    int64_t sector_num;
    int64_t wr_offs;
    enum ImgConvertBlockStatus status;
    int64_t sector_next_status;
    BlockBackend *target;
    bool has_zero_init;
    bool compressed;
    bool target_is_new;
    bool target_has_backing;
    int64_t target_backing_sectors; /* negative if unknown */
    bool wr_in_order;
    bool copy_range;
    bool salvage;
    bool quiet;
    int min_sparse;
    int alignment;
    size_t cluster_sectors;
    size_t buf_sectors;
    long num_coroutines;
    int running_coroutines;
    Coroutine *co[MAX_COROUTINES];
    int64_t wait_sector_num[MAX_COROUTINES];
    CoMutex lock;
    int ret;
} ImgConvertState;

static void convert_select_part(ImgConvertState *s, int64_t sector_num,
                                int *src_cur, int64_t *src_cur_offset)
{
    *src_cur = 0;
    *src_cur_offset = 0;
    while (sector_num - *src_cur_offset >= s->src_sectors[*src_cur]) {
        *src_cur_offset += s->src_sectors[*src_cur];
        (*src_cur)++;
        assert(*src_cur < s->src_num);
    }
}

static int convert_iteration_sectors(ImgConvertState *s, int64_t sector_num)
{
    int64_t src_cur_offset;
    int ret, n, src_cur;
    bool post_backing_zero = false;

    convert_select_part(s, sector_num, &src_cur, &src_cur_offset);

    assert(s->total_sectors > sector_num);
    n = MIN(s->total_sectors - sector_num, BDRV_REQUEST_MAX_SECTORS);

    if (s->target_backing_sectors >= 0) {
        if (sector_num >= s->target_backing_sectors) {
            post_backing_zero = true;
        } else if (sector_num + n > s->target_backing_sectors) {
            /* Split requests around target_backing_sectors (because
             * starting from there, zeros are handled differently) */
            n = s->target_backing_sectors - sector_num;
        }
    }

    if (s->sector_next_status <= sector_num) {
        uint64_t offset = (sector_num - src_cur_offset) * BDRV_SECTOR_SIZE;
        int64_t count;

        do {
            count = n * BDRV_SECTOR_SIZE;

            if (s->target_has_backing) {
                ret = bdrv_block_status(blk_bs(s->src[src_cur]), offset,
                                        count, &count, NULL, NULL);
            } else {
                ret = bdrv_block_status_above(blk_bs(s->src[src_cur]), NULL,
                                              offset, count, &count, NULL,
                                              NULL);
            }

            if (ret < 0) {
                if (s->salvage) {
                    if (n == 1) {
                        if (!s->quiet) {
                            warn_report("error while reading block status at "
                                        "offset %" PRIu64 ": %s", offset,
                                        strerror(-ret));
                        }
                        /* Just try to read the data, then */
                        ret = BDRV_BLOCK_DATA;
                        count = BDRV_SECTOR_SIZE;
                    } else {
                        /* Retry on a shorter range */
                        n = DIV_ROUND_UP(n, 4);
                    }
                } else {
                    error_report("error while reading block status at offset "
                                 "%" PRIu64 ": %s", offset, strerror(-ret));
                    return ret;
                }
            }
        } while (ret < 0);

        n = DIV_ROUND_UP(count, BDRV_SECTOR_SIZE);

        if (ret & BDRV_BLOCK_ZERO) {
            s->status = post_backing_zero ? BLK_BACKING_FILE : BLK_ZERO;
        } else if (ret & BDRV_BLOCK_DATA) {
            s->status = BLK_DATA;
        } else {
            s->status = s->target_has_backing ? BLK_BACKING_FILE : BLK_DATA;
        }

        s->sector_next_status = sector_num + n;
    }

    n = MIN(n, s->sector_next_status - sector_num);
    if (s->status == BLK_DATA) {
        n = MIN(n, s->buf_sectors);
    }

    /* We need to write complete clusters for compressed images, so if an
     * unallocated area is shorter than that, we must consider the whole
     * cluster allocated. */
    if (s->compressed) {
        if (n < s->cluster_sectors) {
            n = MIN(s->cluster_sectors, s->total_sectors - sector_num);
            s->status = BLK_DATA;
        } else {
            n = QEMU_ALIGN_DOWN(n, s->cluster_sectors);
        }
    }

    return n;
}

static int coroutine_fn convert_co_read(ImgConvertState *s, int64_t sector_num,
                                        int nb_sectors, uint8_t *buf)
{
    uint64_t single_read_until = 0;
    int n, ret;

    assert(nb_sectors <= s->buf_sectors);
    while (nb_sectors > 0) {
        BlockBackend *blk;
        int src_cur;
        int64_t bs_sectors, src_cur_offset;
        uint64_t offset;

        /* In the case of compression with multiple source files, we can get a
         * nb_sectors that spreads into the next part. So we must be able to
         * read across multiple BDSes for one convert_read() call. */
        convert_select_part(s, sector_num, &src_cur, &src_cur_offset);
        blk = s->src[src_cur];
        bs_sectors = s->src_sectors[src_cur];

        offset = (sector_num - src_cur_offset) << BDRV_SECTOR_BITS;

        n = MIN(nb_sectors, bs_sectors - (sector_num - src_cur_offset));
        if (single_read_until > offset) {
            n = 1;
        }

        ret = blk_co_pread(blk, offset, n << BDRV_SECTOR_BITS, buf, 0);
        if (ret < 0) {
            if (s->salvage) {
                if (n > 1) {
                    single_read_until = offset + (n << BDRV_SECTOR_BITS);
                    continue;
                } else {
                    if (!s->quiet) {
                        warn_report("error while reading offset %" PRIu64
                                    ": %s", offset, strerror(-ret));
                    }
                    memset(buf, 0, BDRV_SECTOR_SIZE);
                }
            } else {
                return ret;
            }
        }

        sector_num += n;
        nb_sectors -= n;
        buf += n * BDRV_SECTOR_SIZE;
    }

    return 0;
}


static int coroutine_fn convert_co_write(ImgConvertState *s, int64_t sector_num,
                                         int nb_sectors, uint8_t *buf,
                                         enum ImgConvertBlockStatus status)
{
    int ret;

    while (nb_sectors > 0) {
        int n = nb_sectors;
        BdrvRequestFlags flags = s->compressed ? BDRV_REQ_WRITE_COMPRESSED : 0;

        switch (status) {
        case BLK_BACKING_FILE:
            /* If we have a backing file, leave clusters unallocated that are
             * unallocated in the source image, so that the backing file is
             * visible at the respective offset. */
            assert(s->target_has_backing);
            break;

        case BLK_DATA:
            /* If we're told to keep the target fully allocated (-S 0) or there
             * is real non-zero data, we must write it. Otherwise we can treat
             * it as zero sectors.
             * Compressed clusters need to be written as a whole, so in that
             * case we can only save the write if the buffer is completely
             * zeroed. */
            if (!s->min_sparse ||
                (!s->compressed &&
                 is_allocated_sectors_min(buf, n, &n, s->min_sparse,
                                          sector_num, s->alignment)) ||
                (s->compressed &&
                 !buffer_is_zero(buf, n * BDRV_SECTOR_SIZE)))
            {
                ret = blk_co_pwrite(s->target, sector_num << BDRV_SECTOR_BITS,
                                    n << BDRV_SECTOR_BITS, buf, flags);
                if (ret < 0) {
                    return ret;
                }
                break;
            }
            /* fall-through */

        case BLK_ZERO:
            if (s->has_zero_init) {
                assert(!s->target_has_backing);
                break;
            }
            ret = blk_co_pwrite_zeroes(s->target,
                                       sector_num << BDRV_SECTOR_BITS,
                                       n << BDRV_SECTOR_BITS,
                                       BDRV_REQ_MAY_UNMAP);
            if (ret < 0) {
                return ret;
            }
            break;
        }

        sector_num += n;
        nb_sectors -= n;
        buf += n * BDRV_SECTOR_SIZE;
    }

    return 0;
}

static int coroutine_fn convert_co_copy_range(ImgConvertState *s, int64_t sector_num,
                                              int nb_sectors)
{
    int n, ret;

    while (nb_sectors > 0) {
        BlockBackend *blk;
        int src_cur;
        int64_t bs_sectors, src_cur_offset;
        int64_t offset;

        convert_select_part(s, sector_num, &src_cur, &src_cur_offset);
        offset = (sector_num - src_cur_offset) << BDRV_SECTOR_BITS;
        blk = s->src[src_cur];
        bs_sectors = s->src_sectors[src_cur];

        n = MIN(nb_sectors, bs_sectors - (sector_num - src_cur_offset));

        ret = blk_co_copy_range(blk, offset, s->target,
                                sector_num << BDRV_SECTOR_BITS,
                                n << BDRV_SECTOR_BITS, 0, 0);
        if (ret < 0) {
            return ret;
        }

        sector_num += n;
        nb_sectors -= n;
    }
    return 0;
}

static void coroutine_fn convert_co_do_copy(void *opaque)
{
    ImgConvertState *s = opaque;
    uint8_t *buf = NULL;
    int ret, i;
    int index = -1;

    for (i = 0; i < s->num_coroutines; i++) {
        if (s->co[i] == qemu_coroutine_self()) {
            index = i;
            break;
        }
    }
    assert(index >= 0);

    s->running_coroutines++;
    buf = blk_blockalign(s->target, s->buf_sectors * BDRV_SECTOR_SIZE);

    while (1) {
        int n;
        int64_t sector_num;
        enum ImgConvertBlockStatus status;
        bool copy_range;

        qemu_co_mutex_lock(&s->lock);
        if (s->ret != -EINPROGRESS || s->sector_num >= s->total_sectors) {
            qemu_co_mutex_unlock(&s->lock);
            break;
        }
        n = convert_iteration_sectors(s, s->sector_num);
        if (n < 0) {
            qemu_co_mutex_unlock(&s->lock);
            s->ret = n;
            break;
        }
        /* save current sector and allocation status to local variables */
        sector_num = s->sector_num;
        status = s->status;
        if (!s->min_sparse && s->status == BLK_ZERO) {
            n = MIN(n, s->buf_sectors);
        }
        /* increment global sector counter so that other coroutines can
         * already continue reading beyond this request */
        s->sector_num += n;
        qemu_co_mutex_unlock(&s->lock);

        if (status == BLK_DATA || (!s->min_sparse && status == BLK_ZERO)) {
            s->allocated_done += n;
            qemu_progress_print(100.0 * s->allocated_done /
                                        s->allocated_sectors, 0);
        }

retry:
        copy_range = s->copy_range && s->status == BLK_DATA;
        if (status == BLK_DATA && !copy_range) {
            ret = convert_co_read(s, sector_num, n, buf);
            if (ret < 0) {
                error_report("error while reading at byte %lld: %s",
                             sector_num * BDRV_SECTOR_SIZE, strerror(-ret));
                s->ret = ret;
            }
        } else if (!s->min_sparse && status == BLK_ZERO) {
            status = BLK_DATA;
            memset(buf, 0x00, n * BDRV_SECTOR_SIZE);
        }

        if (s->wr_in_order) {
            /* keep writes in order */
            while (s->wr_offs != sector_num && s->ret == -EINPROGRESS) {
                s->wait_sector_num[index] = sector_num;
                qemu_coroutine_yield();
            }
            s->wait_sector_num[index] = -1;
        }

        if (s->ret == -EINPROGRESS) {
            if (copy_range) {
                ret = convert_co_copy_range(s, sector_num, n);
                if (ret) {
                    s->copy_range = false;
                    goto retry;
                }
            } else {
                ret = convert_co_write(s, sector_num, n, buf, status);
            }
            if (ret < 0) {
                error_report("error while writing at byte %lld: %s",
                             sector_num * BDRV_SECTOR_SIZE, strerror(-ret));
                s->ret = ret;
            }
        }

        if (s->wr_in_order) {
            /* reenter the coroutine that might have waited
             * for this write to complete */
            s->wr_offs = sector_num + n;
            for (i = 0; i < s->num_coroutines; i++) {
                if (s->co[i] && s->wait_sector_num[i] == s->wr_offs) {
                    /*
                     * A -> B -> A cannot occur because A has
                     * s->wait_sector_num[i] == -1 during A -> B.  Therefore
                     * B will never enter A during this time window.
                     */
                    qemu_coroutine_enter(s->co[i]);
                    break;
                }
            }
        }
    }

    qemu_vfree(buf);
    s->co[index] = NULL;
    s->running_coroutines--;
    if (!s->running_coroutines && s->ret == -EINPROGRESS) {
        /* the convert job finished successfully */
        s->ret = 0;
    }
}

static int convert_do_copy(ImgConvertState *s)
{
    int ret, i, n;
    int64_t sector_num = 0;

    /* Check whether we have zero initialisation or can get it efficiently */
    if (!s->has_zero_init && s->target_is_new && s->min_sparse &&
        !s->target_has_backing) {
        s->has_zero_init = bdrv_has_zero_init(blk_bs(s->target));
    }

    /* Allocate buffer for copied data. For compressed images, only one cluster
     * can be copied at a time. */
    if (s->compressed) {
        if (s->cluster_sectors <= 0 || s->cluster_sectors > s->buf_sectors) {
            error_report("invalid cluster size");
            return -EINVAL;
        }
        s->buf_sectors = s->cluster_sectors;
    }

    while (sector_num < s->total_sectors) {
        n = convert_iteration_sectors(s, sector_num);
        if (n < 0) {
            return n;
        }
        if (s->status == BLK_DATA || (!s->min_sparse && s->status == BLK_ZERO))
        {
            s->allocated_sectors += n;
        }
        sector_num += n;
    }

    /* Do the copy */
    s->sector_next_status = 0;
    s->ret = -EINPROGRESS;

    qemu_co_mutex_init(&s->lock);
    for (i = 0; i < s->num_coroutines; i++) {
        s->co[i] = qemu_coroutine_create(convert_co_do_copy, s);
        s->wait_sector_num[i] = -1;
        qemu_coroutine_enter(s->co[i]);
    }

    while (s->running_coroutines) {
        main_loop_wait(false);
    }

    if (s->compressed && !s->ret) {
        /* signal EOF to align */
        ret = blk_pwrite_compressed(s->target, 0, NULL, 0);
        if (ret < 0) {
            return ret;
        }
    }

    return s->ret;
}

static int convert_copy_bitmaps(BlockDriverState *src, BlockDriverState *dst)
{
    BdrvDirtyBitmap *bm;
    Error *err = NULL;

    FOR_EACH_DIRTY_BITMAP(src, bm) {
        const char *name;

        if (!bdrv_dirty_bitmap_get_persistence(bm)) {
            continue;
        }
        name = bdrv_dirty_bitmap_name(bm);
        qmp_block_dirty_bitmap_add(dst->node_name, name,
                                   true, bdrv_dirty_bitmap_granularity(bm),
                                   true, true,
                                   true, !bdrv_dirty_bitmap_enabled(bm),
                                   &err);
        if (err) {
            error_reportf_err(err, "Failed to create bitmap %s: ", name);
            return -1;
        }

        do_dirty_bitmap_merge(dst->node_name, name, src->node_name, name,
                              &err);
        if (err) {
            error_reportf_err(err, "Failed to populate bitmap %s: ", name);
            return -1;
        }
    }

    return 0;
}

#define MAX_BUF_SECTORS 32768

static int img_convert(int argc, char **argv)
{
    int c, bs_i, flags, src_flags = 0;
    const char *fmt = NULL, *out_fmt = NULL, *cache = "unsafe",
               *src_cache = BDRV_DEFAULT_CACHE, *out_baseimg = NULL,
               *out_filename, *out_baseimg_param, *snapshot_name = NULL;
    BlockDriver *drv = NULL, *proto_drv = NULL;
    BlockDriverInfo bdi;
    BlockDriverState *out_bs;
    QemuOpts *opts = NULL, *sn_opts = NULL;
    QemuOptsList *create_opts = NULL;
    QDict *open_opts = NULL;
    char *options = NULL;
    Error *local_err = NULL;
    bool writethrough, src_writethrough, image_opts = false,
         skip_create = false, progress = false, tgt_image_opts = false;
    int64_t ret = -EINVAL;
    bool force_share = false;
    bool explict_min_sparse = false;
    bool bitmaps = false;

    ImgConvertState s = (ImgConvertState) {
        /* Need at least 4k of zeros for sparse detection */
        .min_sparse         = 8,
        .copy_range         = false,
        .buf_sectors        = IO_BUF_SIZE / BDRV_SECTOR_SIZE,
        .wr_in_order        = true,
        .num_coroutines     = 8,
    };

    for(;;) {
        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"object", required_argument, 0, OPTION_OBJECT},
            {"image-opts", no_argument, 0, OPTION_IMAGE_OPTS},
            {"force-share", no_argument, 0, 'U'},
            {"target-image-opts", no_argument, 0, OPTION_TARGET_IMAGE_OPTS},
            {"salvage", no_argument, 0, OPTION_SALVAGE},
            {"target-is-zero", no_argument, 0, OPTION_TARGET_IS_ZERO},
            {"bitmaps", no_argument, 0, OPTION_BITMAPS},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, ":hf:O:B:Cco:l:S:pt:T:qnm:WU",
                        long_options, NULL);
        if (c == -1) {
            break;
        }
        switch(c) {
        case ':':
            missing_argument(argv[optind - 1]);
            break;
        case '?':
            unrecognized_option(argv[optind - 1]);
            break;
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
        case 'C':
            s.copy_range = true;
            break;
        case 'c':
            s.compressed = true;
            break;
        case 'o':
            if (accumulate_options(&options, optarg) < 0) {
                goto fail_getopt;
            }
            break;
        case 'l':
            if (strstart(optarg, SNAPSHOT_OPT_BASE, NULL)) {
                sn_opts = qemu_opts_parse_noisily(&internal_snapshot_opts,
                                                  optarg, false);
                if (!sn_opts) {
                    error_report("Failed in parsing snapshot param '%s'",
                                 optarg);
                    goto fail_getopt;
                }
            } else {
                snapshot_name = optarg;
            }
            break;
        case 'S':
        {
            int64_t sval;

            sval = cvtnum("buffer size for sparse output", optarg);
            if (sval < 0) {
                goto fail_getopt;
            } else if (!QEMU_IS_ALIGNED(sval, BDRV_SECTOR_SIZE) ||
                sval / BDRV_SECTOR_SIZE > MAX_BUF_SECTORS) {
                error_report("Invalid buffer size for sparse output specified. "
                    "Valid sizes are multiples of %llu up to %llu. Select "
                    "0 to disable sparse detection (fully allocates output).",
                    BDRV_SECTOR_SIZE, MAX_BUF_SECTORS * BDRV_SECTOR_SIZE);
                goto fail_getopt;
            }

            s.min_sparse = sval / BDRV_SECTOR_SIZE;
            explict_min_sparse = true;
            break;
        }
        case 'p':
            progress = true;
            break;
        case 't':
            cache = optarg;
            break;
        case 'T':
            src_cache = optarg;
            break;
        case 'q':
            s.quiet = true;
            break;
        case 'n':
            skip_create = true;
            break;
        case 'm':
            if (qemu_strtol(optarg, NULL, 0, &s.num_coroutines) ||
                s.num_coroutines < 1 || s.num_coroutines > MAX_COROUTINES) {
                error_report("Invalid number of coroutines. Allowed number of"
                             " coroutines is between 1 and %d", MAX_COROUTINES);
                goto fail_getopt;
            }
            break;
        case 'W':
            s.wr_in_order = false;
            break;
        case 'U':
            force_share = true;
            break;
        case OPTION_OBJECT: {
            QemuOpts *object_opts;
            object_opts = qemu_opts_parse_noisily(&qemu_object_opts,
                                                  optarg, true);
            if (!object_opts) {
                goto fail_getopt;
            }
            break;
        }
        case OPTION_IMAGE_OPTS:
            image_opts = true;
            break;
        case OPTION_SALVAGE:
            s.salvage = true;
            break;
        case OPTION_TARGET_IMAGE_OPTS:
            tgt_image_opts = true;
            break;
        case OPTION_TARGET_IS_ZERO:
            /*
             * The user asserting that the target is blank has the
             * same effect as the target driver supporting zero
             * initialisation.
             */
            s.has_zero_init = true;
            break;
        case OPTION_BITMAPS:
            bitmaps = true;
            break;
        }
    }

    if (!out_fmt && !tgt_image_opts) {
        out_fmt = "raw";
    }

    if (qemu_opts_foreach(&qemu_object_opts,
                          user_creatable_add_opts_foreach,
                          qemu_img_object_print_help, &error_fatal)) {
        goto fail_getopt;
    }

    if (s.compressed && s.copy_range) {
        error_report("Cannot enable copy offloading when -c is used");
        goto fail_getopt;
    }

    if (explict_min_sparse && s.copy_range) {
        error_report("Cannot enable copy offloading when -S is used");
        goto fail_getopt;
    }

    if (s.copy_range && s.salvage) {
        error_report("Cannot use copy offloading in salvaging mode");
        goto fail_getopt;
    }

    if (tgt_image_opts && !skip_create) {
        error_report("--target-image-opts requires use of -n flag");
        goto fail_getopt;
    }

    if (skip_create && options) {
        error_report("-o has no effect when skipping image creation");
        goto fail_getopt;
    }

    if (s.has_zero_init && !skip_create) {
        error_report("--target-is-zero requires use of -n flag");
        goto fail_getopt;
    }

    s.src_num = argc - optind - 1;
    out_filename = s.src_num >= 1 ? argv[argc - 1] : NULL;

    if (options && has_help_option(options)) {
        if (out_fmt) {
            ret = print_block_option_help(out_filename, out_fmt);
            goto fail_getopt;
        } else {
            error_report("Option help requires a format be specified");
            goto fail_getopt;
        }
    }

    if (s.src_num < 1) {
        error_report("Must specify image file name");
        goto fail_getopt;
    }

    /* ret is still -EINVAL until here */
    ret = bdrv_parse_cache_mode(src_cache, &src_flags, &src_writethrough);
    if (ret < 0) {
        error_report("Invalid source cache option: %s", src_cache);
        goto fail_getopt;
    }

    /* Initialize before goto out */
    if (s.quiet) {
        progress = false;
    }
    qemu_progress_init(progress, 1.0);
    qemu_progress_print(0, 100);

    s.src = g_new0(BlockBackend *, s.src_num);
    s.src_sectors = g_new(int64_t, s.src_num);

    for (bs_i = 0; bs_i < s.src_num; bs_i++) {
        s.src[bs_i] = img_open(image_opts, argv[optind + bs_i],
                               fmt, src_flags, src_writethrough, s.quiet,
                               force_share);
        if (!s.src[bs_i]) {
            ret = -1;
            goto out;
        }
        s.src_sectors[bs_i] = blk_nb_sectors(s.src[bs_i]);
        if (s.src_sectors[bs_i] < 0) {
            error_report("Could not get size of %s: %s",
                         argv[optind + bs_i], strerror(-s.src_sectors[bs_i]));
            ret = -1;
            goto out;
        }
        s.total_sectors += s.src_sectors[bs_i];
    }

    if (sn_opts) {
        bdrv_snapshot_load_tmp(blk_bs(s.src[0]),
                               qemu_opt_get(sn_opts, SNAPSHOT_OPT_ID),
                               qemu_opt_get(sn_opts, SNAPSHOT_OPT_NAME),
                               &local_err);
    } else if (snapshot_name != NULL) {
        if (s.src_num > 1) {
            error_report("No support for concatenating multiple snapshot");
            ret = -1;
            goto out;
        }

        bdrv_snapshot_load_tmp_by_id_or_name(blk_bs(s.src[0]), snapshot_name,
                                             &local_err);
    }
    if (local_err) {
        error_reportf_err(local_err, "Failed to load snapshot: ");
        ret = -1;
        goto out;
    }

    if (!skip_create) {
        /* Find driver and parse its options */
        drv = bdrv_find_format(out_fmt);
        if (!drv) {
            error_report("Unknown file format '%s'", out_fmt);
            ret = -1;
            goto out;
        }

        proto_drv = bdrv_find_protocol(out_filename, true, &local_err);
        if (!proto_drv) {
            error_report_err(local_err);
            ret = -1;
            goto out;
        }

        if (!drv->create_opts) {
            error_report("Format driver '%s' does not support image creation",
                         drv->format_name);
            ret = -1;
            goto out;
        }

        if (!proto_drv->create_opts) {
            error_report("Protocol driver '%s' does not support image creation",
                         proto_drv->format_name);
            ret = -1;
            goto out;
        }

        create_opts = qemu_opts_append(create_opts, drv->create_opts);
        create_opts = qemu_opts_append(create_opts, proto_drv->create_opts);

        opts = qemu_opts_create(create_opts, NULL, 0, &error_abort);
        if (options) {
            if (!qemu_opts_do_parse(opts, options, NULL, &local_err)) {
                error_report_err(local_err);
                ret = -1;
                goto out;
            }
        }

        qemu_opt_set_number(opts, BLOCK_OPT_SIZE, s.total_sectors * 512,
                            &error_abort);
        ret = add_old_style_options(out_fmt, opts, out_baseimg, NULL);
        if (ret < 0) {
            goto out;
        }
    }

    /* Get backing file name if -o backing_file was used */
    out_baseimg_param = qemu_opt_get(opts, BLOCK_OPT_BACKING_FILE);
    if (out_baseimg_param) {
        out_baseimg = out_baseimg_param;
    }
    s.target_has_backing = (bool) out_baseimg;

    if (s.has_zero_init && s.target_has_backing) {
        error_report("Cannot use --target-is-zero when the destination "
                     "image has a backing file");
        goto out;
    }

    if (s.src_num > 1 && out_baseimg) {
        error_report("Having a backing file for the target makes no sense when "
                     "concatenating multiple input images");
        ret = -1;
        goto out;
    }

    if (out_baseimg_param) {
        if (!qemu_opt_get(opts, BLOCK_OPT_BACKING_FMT)) {
            warn_report("Deprecated use of backing file without explicit "
                        "backing format");
        }
    }

    /* Check if compression is supported */
    if (s.compressed) {
        bool encryption =
            qemu_opt_get_bool(opts, BLOCK_OPT_ENCRYPT, false);
        const char *encryptfmt =
            qemu_opt_get(opts, BLOCK_OPT_ENCRYPT_FORMAT);
        const char *preallocation =
            qemu_opt_get(opts, BLOCK_OPT_PREALLOC);

        if (drv && !block_driver_can_compress(drv)) {
            error_report("Compression not supported for this file format");
            ret = -1;
            goto out;
        }

        if (encryption || encryptfmt) {
            error_report("Compression and encryption not supported at "
                         "the same time");
            ret = -1;
            goto out;
        }

        if (preallocation
            && strcmp(preallocation, "off"))
        {
            error_report("Compression and preallocation not supported at "
                         "the same time");
            ret = -1;
            goto out;
        }
    }

    /* Determine if bitmaps need copying */
    if (bitmaps) {
        if (s.src_num > 1) {
            error_report("Copying bitmaps only possible with single source");
            ret = -1;
            goto out;
        }
        if (!bdrv_supports_persistent_dirty_bitmap(blk_bs(s.src[0]))) {
            error_report("Source lacks bitmap support");
            ret = -1;
            goto out;
        }
    }

    /*
     * The later open call will need any decryption secrets, and
     * bdrv_create() will purge "opts", so extract them now before
     * they are lost.
     */
    if (!skip_create) {
        open_opts = qdict_new();
        qemu_opt_foreach(opts, img_add_key_secrets, open_opts, &error_abort);

        /* Create the new image */
        ret = bdrv_create(drv, out_filename, opts, &local_err);
        if (ret < 0) {
            error_reportf_err(local_err, "%s: error while converting %s: ",
                              out_filename, out_fmt);
            goto out;
        }
    }

    s.target_is_new = !skip_create;

    flags = s.min_sparse ? (BDRV_O_RDWR | BDRV_O_UNMAP) : BDRV_O_RDWR;
    ret = bdrv_parse_cache_mode(cache, &flags, &writethrough);
    if (ret < 0) {
        error_report("Invalid cache option: %s", cache);
        goto out;
    }

    if (skip_create) {
        s.target = img_open(tgt_image_opts, out_filename, out_fmt,
                            flags, writethrough, s.quiet, false);
    } else {
        /* TODO ultimately we should allow --target-image-opts
         * to be used even when -n is not given.
         * That has to wait for bdrv_create to be improved
         * to allow filenames in option syntax
         */
        s.target = img_open_file(out_filename, open_opts, out_fmt,
                                 flags, writethrough, s.quiet, false);
        open_opts = NULL; /* blk_new_open will have freed it */
    }
    if (!s.target) {
        ret = -1;
        goto out;
    }
    out_bs = blk_bs(s.target);

    if (bitmaps && !bdrv_supports_persistent_dirty_bitmap(out_bs)) {
        error_report("Format driver '%s' does not support bitmaps",
                     out_bs->drv->format_name);
        ret = -1;
        goto out;
    }

    if (s.compressed && !block_driver_can_compress(out_bs->drv)) {
        error_report("Compression not supported for this file format");
        ret = -1;
        goto out;
    }

    /* increase bufsectors from the default 4096 (2M) if opt_transfer
     * or discard_alignment of the out_bs is greater. Limit to
     * MAX_BUF_SECTORS as maximum which is currently 32768 (16MB). */
    s.buf_sectors = MIN(MAX_BUF_SECTORS,
                        MAX(s.buf_sectors,
                            MAX(out_bs->bl.opt_transfer >> BDRV_SECTOR_BITS,
                                out_bs->bl.pdiscard_alignment >>
                                BDRV_SECTOR_BITS)));

    /* try to align the write requests to the destination to avoid unnecessary
     * RMW cycles. */
    s.alignment = MAX(pow2floor(s.min_sparse),
                      DIV_ROUND_UP(out_bs->bl.request_alignment,
                                   BDRV_SECTOR_SIZE));
    assert(is_power_of_2(s.alignment));

    if (skip_create) {
        int64_t output_sectors = blk_nb_sectors(s.target);
        if (output_sectors < 0) {
            error_report("unable to get output image length: %s",
                         strerror(-output_sectors));
            ret = -1;
            goto out;
        } else if (output_sectors < s.total_sectors) {
            error_report("output file is smaller than input file");
            ret = -1;
            goto out;
        }
    }

    if (s.target_has_backing && s.target_is_new) {
        /* Errors are treated as "backing length unknown" (which means
         * s.target_backing_sectors has to be negative, which it will
         * be automatically).  The backing file length is used only
         * for optimizations, so such a case is not fatal. */
        s.target_backing_sectors = bdrv_nb_sectors(out_bs->backing->bs);
    } else {
        s.target_backing_sectors = -1;
    }

    ret = bdrv_get_info(out_bs, &bdi);
    if (ret < 0) {
        if (s.compressed) {
            error_report("could not get block driver info");
            goto out;
        }
    } else {
        s.compressed = s.compressed || bdi.needs_compressed_writes;
        s.cluster_sectors = bdi.cluster_size / BDRV_SECTOR_SIZE;
    }

    ret = convert_do_copy(&s);

    /* Now copy the bitmaps */
    if (bitmaps && ret == 0) {
        ret = convert_copy_bitmaps(blk_bs(s.src[0]), out_bs);
    }

out:
    if (!ret) {
        qemu_progress_print(100, 0);
    }
    qemu_progress_end();
    qemu_opts_del(opts);
    qemu_opts_free(create_opts);
    qemu_opts_del(sn_opts);
    qobject_unref(open_opts);
    blk_unref(s.target);
    if (s.src) {
        for (bs_i = 0; bs_i < s.src_num; bs_i++) {
            blk_unref(s.src[bs_i]);
        }
        g_free(s.src);
    }
    g_free(s.src_sectors);
fail_getopt:
    g_free(options);

    return !!ret;
}


static void dump_snapshots(BlockDriverState *bs)
{
    QEMUSnapshotInfo *sn_tab, *sn;
    int nb_sns, i;

    nb_sns = bdrv_snapshot_list(bs, &sn_tab);
    if (nb_sns <= 0)
        return;
    printf("Snapshot list:\n");
    bdrv_snapshot_dump(NULL);
    printf("\n");
    for(i = 0; i < nb_sns; i++) {
        sn = &sn_tab[i];
        bdrv_snapshot_dump(sn);
        printf("\n");
    }
    g_free(sn_tab);
}

static void dump_json_image_info_list(ImageInfoList *list)
{
    QString *str;
    QObject *obj;
    Visitor *v = qobject_output_visitor_new(&obj);

    visit_type_ImageInfoList(v, NULL, &list, &error_abort);
    visit_complete(v, &obj);
    str = qobject_to_json_pretty(obj);
    assert(str != NULL);
    printf("%s\n", qstring_get_str(str));
    qobject_unref(obj);
    visit_free(v);
    qobject_unref(str);
}

static void dump_json_image_info(ImageInfo *info)
{
    QString *str;
    QObject *obj;
    Visitor *v = qobject_output_visitor_new(&obj);

    visit_type_ImageInfo(v, NULL, &info, &error_abort);
    visit_complete(v, &obj);
    str = qobject_to_json_pretty(obj);
    assert(str != NULL);
    printf("%s\n", qstring_get_str(str));
    qobject_unref(obj);
    visit_free(v);
    qobject_unref(str);
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

        bdrv_image_info_dump(elem->value);
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
static ImageInfoList *collect_image_info_list(bool image_opts,
                                              const char *filename,
                                              const char *fmt,
                                              bool chain, bool force_share)
{
    ImageInfoList *head = NULL;
    ImageInfoList **last = &head;
    GHashTable *filenames;
    Error *err = NULL;

    filenames = g_hash_table_new_full(g_str_hash, str_equal_func, NULL, NULL);

    while (filename) {
        BlockBackend *blk;
        BlockDriverState *bs;
        ImageInfo *info;
        ImageInfoList *elem;

        if (g_hash_table_lookup_extended(filenames, filename, NULL, NULL)) {
            error_report("Backing file '%s' creates an infinite loop.",
                         filename);
            goto err;
        }
        g_hash_table_insert(filenames, (gpointer)filename, NULL);

        blk = img_open(image_opts, filename, fmt,
                       BDRV_O_NO_BACKING | BDRV_O_NO_IO, false, false,
                       force_share);
        if (!blk) {
            goto err;
        }
        bs = blk_bs(blk);

        bdrv_query_image_info(bs, &info, &err);
        if (err) {
            error_report_err(err);
            blk_unref(blk);
            goto err;
        }

        elem = g_new0(ImageInfoList, 1);
        elem->value = info;
        *last = elem;
        last = &elem->next;

        blk_unref(blk);

        /* Clear parameters that only apply to the topmost image */
        filename = fmt = NULL;
        image_opts = false;

        if (chain) {
            if (info->has_full_backing_filename) {
                filename = info->full_backing_filename;
            } else if (info->has_backing_filename) {
                error_report("Could not determine absolute backing filename,"
                             " but backing filename '%s' present",
                             info->backing_filename);
                goto err;
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
    bool image_opts = false;
    bool force_share = false;

    fmt = NULL;
    output = NULL;
    for(;;) {
        int option_index = 0;
        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"format", required_argument, 0, 'f'},
            {"output", required_argument, 0, OPTION_OUTPUT},
            {"backing-chain", no_argument, 0, OPTION_BACKING_CHAIN},
            {"object", required_argument, 0, OPTION_OBJECT},
            {"image-opts", no_argument, 0, OPTION_IMAGE_OPTS},
            {"force-share", no_argument, 0, 'U'},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, ":f:hU",
                        long_options, &option_index);
        if (c == -1) {
            break;
        }
        switch(c) {
        case ':':
            missing_argument(argv[optind - 1]);
            break;
        case '?':
            unrecognized_option(argv[optind - 1]);
            break;
        case 'h':
            help();
            break;
        case 'f':
            fmt = optarg;
            break;
        case 'U':
            force_share = true;
            break;
        case OPTION_OUTPUT:
            output = optarg;
            break;
        case OPTION_BACKING_CHAIN:
            chain = true;
            break;
        case OPTION_OBJECT: {
            QemuOpts *opts;
            opts = qemu_opts_parse_noisily(&qemu_object_opts,
                                           optarg, true);
            if (!opts) {
                return 1;
            }
        }   break;
        case OPTION_IMAGE_OPTS:
            image_opts = true;
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

    if (qemu_opts_foreach(&qemu_object_opts,
                          user_creatable_add_opts_foreach,
                          qemu_img_object_print_help, &error_fatal)) {
        return 1;
    }

    list = collect_image_info_list(image_opts, filename, fmt, chain,
                                   force_share);
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

static int dump_map_entry(OutputFormat output_format, MapEntry *e,
                          MapEntry *next)
{
    switch (output_format) {
    case OFORMAT_HUMAN:
        if (e->data && !e->has_offset) {
            error_report("File contains external, encrypted or compressed clusters.");
            return -1;
        }
        if (e->data && !e->zero) {
            printf("%#-16"PRIx64"%#-16"PRIx64"%#-16"PRIx64"%s\n",
                   e->start, e->length,
                   e->has_offset ? e->offset : 0,
                   e->has_filename ? e->filename : "");
        }
        /* This format ignores the distinction between 0, ZERO and ZERO|DATA.
         * Modify the flags here to allow more coalescing.
         */
        if (next && (!next->data || next->zero)) {
            next->data = false;
            next->zero = true;
        }
        break;
    case OFORMAT_JSON:
        printf("{ \"start\": %"PRId64", \"length\": %"PRId64","
               " \"depth\": %"PRId64", \"zero\": %s, \"data\": %s",
               e->start, e->length, e->depth,
               e->zero ? "true" : "false",
               e->data ? "true" : "false");
        if (e->has_offset) {
            printf(", \"offset\": %"PRId64"", e->offset);
        }
        putchar('}');

        if (next) {
            puts(",");
        }
        break;
    }
    return 0;
}

static int get_block_status(BlockDriverState *bs, int64_t offset,
                            int64_t bytes, MapEntry *e)
{
    int ret;
    int depth;
    BlockDriverState *file;
    bool has_offset;
    int64_t map;
    char *filename = NULL;

    /* As an optimization, we could cache the current range of unallocated
     * clusters in each file of the chain, and avoid querying the same
     * range repeatedly.
     */

    depth = 0;
    for (;;) {
        ret = bdrv_block_status(bs, offset, bytes, &bytes, &map, &file);
        if (ret < 0) {
            return ret;
        }
        assert(bytes);
        if (ret & (BDRV_BLOCK_ZERO|BDRV_BLOCK_DATA)) {
            break;
        }
        bs = backing_bs(bs);
        if (bs == NULL) {
            ret = 0;
            break;
        }

        depth++;
    }

    has_offset = !!(ret & BDRV_BLOCK_OFFSET_VALID);

    if (file && has_offset) {
        bdrv_refresh_filename(file);
        filename = file->filename;
    }

    *e = (MapEntry) {
        .start = offset,
        .length = bytes,
        .data = !!(ret & BDRV_BLOCK_DATA),
        .zero = !!(ret & BDRV_BLOCK_ZERO),
        .offset = map,
        .has_offset = has_offset,
        .depth = depth,
        .has_filename = filename,
        .filename = filename,
    };

    return 0;
}

static inline bool entry_mergeable(const MapEntry *curr, const MapEntry *next)
{
    if (curr->length == 0) {
        return false;
    }
    if (curr->zero != next->zero ||
        curr->data != next->data ||
        curr->depth != next->depth ||
        curr->has_filename != next->has_filename ||
        curr->has_offset != next->has_offset) {
        return false;
    }
    if (curr->has_filename && strcmp(curr->filename, next->filename)) {
        return false;
    }
    if (curr->has_offset && curr->offset + curr->length != next->offset) {
        return false;
    }
    return true;
}

static int img_map(int argc, char **argv)
{
    int c;
    OutputFormat output_format = OFORMAT_HUMAN;
    BlockBackend *blk;
    BlockDriverState *bs;
    const char *filename, *fmt, *output;
    int64_t length;
    MapEntry curr = { .length = 0 }, next;
    int ret = 0;
    bool image_opts = false;
    bool force_share = false;
    int64_t start_offset = 0;
    int64_t max_length = -1;

    fmt = NULL;
    output = NULL;
    for (;;) {
        int option_index = 0;
        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"format", required_argument, 0, 'f'},
            {"output", required_argument, 0, OPTION_OUTPUT},
            {"object", required_argument, 0, OPTION_OBJECT},
            {"image-opts", no_argument, 0, OPTION_IMAGE_OPTS},
            {"force-share", no_argument, 0, 'U'},
            {"start-offset", required_argument, 0, 's'},
            {"max-length", required_argument, 0, 'l'},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, ":f:s:l:hU",
                        long_options, &option_index);
        if (c == -1) {
            break;
        }
        switch (c) {
        case ':':
            missing_argument(argv[optind - 1]);
            break;
        case '?':
            unrecognized_option(argv[optind - 1]);
            break;
        case 'h':
            help();
            break;
        case 'f':
            fmt = optarg;
            break;
        case 'U':
            force_share = true;
            break;
        case OPTION_OUTPUT:
            output = optarg;
            break;
        case 's':
            start_offset = cvtnum("start offset", optarg);
            if (start_offset < 0) {
                return 1;
            }
            break;
        case 'l':
            max_length = cvtnum("max length", optarg);
            if (max_length < 0) {
                return 1;
            }
            break;
        case OPTION_OBJECT: {
            QemuOpts *opts;
            opts = qemu_opts_parse_noisily(&qemu_object_opts,
                                           optarg, true);
            if (!opts) {
                return 1;
            }
        }   break;
        case OPTION_IMAGE_OPTS:
            image_opts = true;
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

    if (qemu_opts_foreach(&qemu_object_opts,
                          user_creatable_add_opts_foreach,
                          qemu_img_object_print_help, &error_fatal)) {
        return 1;
    }

    blk = img_open(image_opts, filename, fmt, 0, false, false, force_share);
    if (!blk) {
        return 1;
    }
    bs = blk_bs(blk);

    if (output_format == OFORMAT_HUMAN) {
        printf("%-16s%-16s%-16s%s\n", "Offset", "Length", "Mapped to", "File");
    } else if (output_format == OFORMAT_JSON) {
        putchar('[');
    }

    length = blk_getlength(blk);
    if (length < 0) {
        error_report("Failed to get size for '%s'", filename);
        return 1;
    }
    if (max_length != -1) {
        length = MIN(start_offset + max_length, length);
    }

    curr.start = start_offset;
    while (curr.start + curr.length < length) {
        int64_t offset = curr.start + curr.length;
        int64_t n = length - offset;

        ret = get_block_status(bs, offset, n, &next);
        if (ret < 0) {
            error_report("Could not read file metadata: %s", strerror(-ret));
            goto out;
        }

        if (entry_mergeable(&curr, &next)) {
            curr.length += next.length;
            continue;
        }

        if (curr.length > 0) {
            ret = dump_map_entry(output_format, &curr, &next);
            if (ret < 0) {
                goto out;
            }
        }
        curr = next;
    }

    ret = dump_map_entry(output_format, &curr, NULL);
    if (output_format == OFORMAT_JSON) {
        puts("]");
    }

out:
    blk_unref(blk);
    return ret < 0;
}

#define SNAPSHOT_LIST   1
#define SNAPSHOT_CREATE 2
#define SNAPSHOT_APPLY  3
#define SNAPSHOT_DELETE 4

static int img_snapshot(int argc, char **argv)
{
    BlockBackend *blk;
    BlockDriverState *bs;
    QEMUSnapshotInfo sn;
    char *filename, *snapshot_name = NULL;
    int c, ret = 0, bdrv_oflags;
    int action = 0;
    qemu_timeval tv;
    bool quiet = false;
    Error *err = NULL;
    bool image_opts = false;
    bool force_share = false;

    bdrv_oflags = BDRV_O_RDWR;
    /* Parse commandline parameters */
    for(;;) {
        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"object", required_argument, 0, OPTION_OBJECT},
            {"image-opts", no_argument, 0, OPTION_IMAGE_OPTS},
            {"force-share", no_argument, 0, 'U'},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, ":la:c:d:hqU",
                        long_options, NULL);
        if (c == -1) {
            break;
        }
        switch(c) {
        case ':':
            missing_argument(argv[optind - 1]);
            break;
        case '?':
            unrecognized_option(argv[optind - 1]);
            break;
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
        case 'U':
            force_share = true;
            break;
        case OPTION_OBJECT: {
            QemuOpts *opts;
            opts = qemu_opts_parse_noisily(&qemu_object_opts,
                                           optarg, true);
            if (!opts) {
                return 1;
            }
        }   break;
        case OPTION_IMAGE_OPTS:
            image_opts = true;
            break;
        }
    }

    if (optind != argc - 1) {
        error_exit("Expecting one image file name");
    }
    filename = argv[optind++];

    if (qemu_opts_foreach(&qemu_object_opts,
                          user_creatable_add_opts_foreach,
                          qemu_img_object_print_help, &error_fatal)) {
        return 1;
    }

    /* Open the image */
    blk = img_open(image_opts, filename, NULL, bdrv_oflags, false, quiet,
                   force_share);
    if (!blk) {
        return 1;
    }
    bs = blk_bs(blk);

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
        ret = bdrv_snapshot_goto(bs, snapshot_name, &err);
        if (ret) {
            error_reportf_err(err, "Could not apply snapshot '%s': ",
                              snapshot_name);
        }
        break;

    case SNAPSHOT_DELETE:
        ret = bdrv_snapshot_find(bs, &sn, snapshot_name);
        if (ret < 0) {
            error_report("Could not delete snapshot '%s': snapshot not "
                         "found", snapshot_name);
            ret = 1;
        } else {
            ret = bdrv_snapshot_delete(bs, sn.id_str, sn.name, &err);
            if (ret < 0) {
                error_reportf_err(err, "Could not delete snapshot '%s': ",
                                  snapshot_name);
                ret = 1;
            }
        }
        break;
    }

    /* Cleanup */
    blk_unref(blk);
    if (ret) {
        return 1;
    }
    return 0;
}

static int img_rebase(int argc, char **argv)
{
    BlockBackend *blk = NULL, *blk_old_backing = NULL, *blk_new_backing = NULL;
    uint8_t *buf_old = NULL;
    uint8_t *buf_new = NULL;
    BlockDriverState *bs = NULL, *prefix_chain_bs = NULL;
    char *filename;
    const char *fmt, *cache, *src_cache, *out_basefmt, *out_baseimg;
    int c, flags, src_flags, ret;
    bool writethrough, src_writethrough;
    int unsafe = 0;
    bool force_share = false;
    int progress = 0;
    bool quiet = false;
    Error *local_err = NULL;
    bool image_opts = false;

    /* Parse commandline parameters */
    fmt = NULL;
    cache = BDRV_DEFAULT_CACHE;
    src_cache = BDRV_DEFAULT_CACHE;
    out_baseimg = NULL;
    out_basefmt = NULL;
    for(;;) {
        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"object", required_argument, 0, OPTION_OBJECT},
            {"image-opts", no_argument, 0, OPTION_IMAGE_OPTS},
            {"force-share", no_argument, 0, 'U'},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, ":hf:F:b:upt:T:qU",
                        long_options, NULL);
        if (c == -1) {
            break;
        }
        switch(c) {
        case ':':
            missing_argument(argv[optind - 1]);
            break;
        case '?':
            unrecognized_option(argv[optind - 1]);
            break;
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
        case 'T':
            src_cache = optarg;
            break;
        case 'q':
            quiet = true;
            break;
        case OPTION_OBJECT: {
            QemuOpts *opts;
            opts = qemu_opts_parse_noisily(&qemu_object_opts,
                                           optarg, true);
            if (!opts) {
                return 1;
            }
        }   break;
        case OPTION_IMAGE_OPTS:
            image_opts = true;
            break;
        case 'U':
            force_share = true;
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

    if (qemu_opts_foreach(&qemu_object_opts,
                          user_creatable_add_opts_foreach,
                          qemu_img_object_print_help, &error_fatal)) {
        return 1;
    }

    qemu_progress_init(progress, 2.0);
    qemu_progress_print(0, 100);

    flags = BDRV_O_RDWR | (unsafe ? BDRV_O_NO_BACKING : 0);
    ret = bdrv_parse_cache_mode(cache, &flags, &writethrough);
    if (ret < 0) {
        error_report("Invalid cache option: %s", cache);
        goto out;
    }

    src_flags = 0;
    ret = bdrv_parse_cache_mode(src_cache, &src_flags, &src_writethrough);
    if (ret < 0) {
        error_report("Invalid source cache option: %s", src_cache);
        goto out;
    }

    /* The source files are opened read-only, don't care about WCE */
    assert((src_flags & BDRV_O_RDWR) == 0);
    (void) src_writethrough;

    /*
     * Open the images.
     *
     * Ignore the old backing file for unsafe rebase in case we want to correct
     * the reference to a renamed or moved backing file.
     */
    blk = img_open(image_opts, filename, fmt, flags, writethrough, quiet,
                   false);
    if (!blk) {
        ret = -1;
        goto out;
    }
    bs = blk_bs(blk);

    if (out_basefmt != NULL) {
        if (bdrv_find_format(out_basefmt) == NULL) {
            error_report("Invalid format name: '%s'", out_basefmt);
            ret = -1;
            goto out;
        }
    }

    /* For safe rebasing we need to compare old and new backing file */
    if (!unsafe) {
        QDict *options = NULL;
        BlockDriverState *base_bs = backing_bs(bs);

        if (base_bs) {
            blk_old_backing = blk_new(qemu_get_aio_context(),
                                      BLK_PERM_CONSISTENT_READ,
                                      BLK_PERM_ALL);
            ret = blk_insert_bs(blk_old_backing, base_bs,
                                &local_err);
            if (ret < 0) {
                error_reportf_err(local_err,
                                  "Could not reuse old backing file '%s': ",
                                  base_bs->filename);
                goto out;
            }
        } else {
            blk_old_backing = NULL;
        }

        if (out_baseimg[0]) {
            const char *overlay_filename;
            char *out_real_path;

            options = qdict_new();
            if (out_basefmt) {
                qdict_put_str(options, "driver", out_basefmt);
            }
            if (force_share) {
                qdict_put_bool(options, BDRV_OPT_FORCE_SHARE, true);
            }

            bdrv_refresh_filename(bs);
            overlay_filename = bs->exact_filename[0] ? bs->exact_filename
                                                     : bs->filename;
            out_real_path =
                bdrv_get_full_backing_filename_from_filename(overlay_filename,
                                                             out_baseimg,
                                                             &local_err);
            if (local_err) {
                qobject_unref(options);
                error_reportf_err(local_err,
                                  "Could not resolve backing filename: ");
                ret = -1;
                goto out;
            }

            /*
             * Find out whether we rebase an image on top of a previous image
             * in its chain.
             */
            prefix_chain_bs = bdrv_find_backing_image(bs, out_real_path);
            if (prefix_chain_bs) {
                qobject_unref(options);
                g_free(out_real_path);

                blk_new_backing = blk_new(qemu_get_aio_context(),
                                          BLK_PERM_CONSISTENT_READ,
                                          BLK_PERM_ALL);
                ret = blk_insert_bs(blk_new_backing, prefix_chain_bs,
                                    &local_err);
                if (ret < 0) {
                    error_reportf_err(local_err,
                                      "Could not reuse backing file '%s': ",
                                      out_baseimg);
                    goto out;
                }
            } else {
                blk_new_backing = blk_new_open(out_real_path, NULL,
                                               options, src_flags, &local_err);
                g_free(out_real_path);
                if (!blk_new_backing) {
                    error_reportf_err(local_err,
                                      "Could not open new backing file '%s': ",
                                      out_baseimg);
                    ret = -1;
                    goto out;
                }
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
        int64_t size;
        int64_t old_backing_size = 0;
        int64_t new_backing_size = 0;
        uint64_t offset;
        int64_t n;
        float local_progress = 0;

        buf_old = blk_blockalign(blk, IO_BUF_SIZE);
        buf_new = blk_blockalign(blk, IO_BUF_SIZE);

        size = blk_getlength(blk);
        if (size < 0) {
            error_report("Could not get size of '%s': %s",
                         filename, strerror(-size));
            ret = -1;
            goto out;
        }
        if (blk_old_backing) {
            old_backing_size = blk_getlength(blk_old_backing);
            if (old_backing_size < 0) {
                char backing_name[PATH_MAX];

                bdrv_get_backing_filename(bs, backing_name,
                                          sizeof(backing_name));
                error_report("Could not get size of '%s': %s",
                             backing_name, strerror(-old_backing_size));
                ret = -1;
                goto out;
            }
        }
        if (blk_new_backing) {
            new_backing_size = blk_getlength(blk_new_backing);
            if (new_backing_size < 0) {
                error_report("Could not get size of '%s': %s",
                             out_baseimg, strerror(-new_backing_size));
                ret = -1;
                goto out;
            }
        }

        if (size != 0) {
            local_progress = (float)100 / (size / MIN(size, IO_BUF_SIZE));
        }

        for (offset = 0; offset < size; offset += n) {
            bool buf_old_is_zero = false;

            /* How many bytes can we handle with the next read? */
            n = MIN(IO_BUF_SIZE, size - offset);

            /* If the cluster is allocated, we don't need to take action */
            ret = bdrv_is_allocated(bs, offset, n, &n);
            if (ret < 0) {
                error_report("error while reading image metadata: %s",
                             strerror(-ret));
                goto out;
            }
            if (ret) {
                continue;
            }

            if (prefix_chain_bs) {
                /*
                 * If cluster wasn't changed since prefix_chain, we don't need
                 * to take action
                 */
                ret = bdrv_is_allocated_above(backing_bs(bs), prefix_chain_bs,
                                              false, offset, n, &n);
                if (ret < 0) {
                    error_report("error while reading image metadata: %s",
                                 strerror(-ret));
                    goto out;
                }
                if (!ret) {
                    continue;
                }
            }

            /*
             * Read old and new backing file and take into consideration that
             * backing files may be smaller than the COW image.
             */
            if (offset >= old_backing_size) {
                memset(buf_old, 0, n);
                buf_old_is_zero = true;
            } else {
                if (offset + n > old_backing_size) {
                    n = old_backing_size - offset;
                }

                ret = blk_pread(blk_old_backing, offset, buf_old, n);
                if (ret < 0) {
                    error_report("error while reading from old backing file");
                    goto out;
                }
            }

            if (offset >= new_backing_size || !blk_new_backing) {
                memset(buf_new, 0, n);
            } else {
                if (offset + n > new_backing_size) {
                    n = new_backing_size - offset;
                }

                ret = blk_pread(blk_new_backing, offset, buf_new, n);
                if (ret < 0) {
                    error_report("error while reading from new backing file");
                    goto out;
                }
            }

            /* If they differ, we need to write to the COW file */
            uint64_t written = 0;

            while (written < n) {
                int64_t pnum;

                if (compare_buffers(buf_old + written, buf_new + written,
                                    n - written, &pnum))
                {
                    if (buf_old_is_zero) {
                        ret = blk_pwrite_zeroes(blk, offset + written, pnum, 0);
                    } else {
                        ret = blk_pwrite(blk, offset + written,
                                         buf_old + written, pnum, 0);
                    }
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
    }

    /*
     * Change the backing file. All clusters that are different from the old
     * backing file are overwritten in the COW file now, so the visible content
     * doesn't change when we switch the backing file.
     */
    if (out_baseimg && *out_baseimg) {
        ret = bdrv_change_backing_file(bs, out_baseimg, out_basefmt, true);
    } else {
        ret = bdrv_change_backing_file(bs, NULL, NULL, false);
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
        blk_unref(blk_old_backing);
        blk_unref(blk_new_backing);
    }
    qemu_vfree(buf_old);
    qemu_vfree(buf_new);

    blk_unref(blk);
    if (ret) {
        return 1;
    }
    return 0;
}

static int img_resize(int argc, char **argv)
{
    Error *err = NULL;
    int c, ret, relative;
    const char *filename, *fmt, *size;
    int64_t n, total_size, current_size;
    bool quiet = false;
    BlockBackend *blk = NULL;
    PreallocMode prealloc = PREALLOC_MODE_OFF;
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
    bool image_opts = false;
    bool shrink = false;

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
        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"object", required_argument, 0, OPTION_OBJECT},
            {"image-opts", no_argument, 0, OPTION_IMAGE_OPTS},
            {"preallocation", required_argument, 0, OPTION_PREALLOCATION},
            {"shrink", no_argument, 0, OPTION_SHRINK},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, ":f:hq",
                        long_options, NULL);
        if (c == -1) {
            break;
        }
        switch(c) {
        case ':':
            missing_argument(argv[optind - 1]);
            break;
        case '?':
            unrecognized_option(argv[optind - 1]);
            break;
        case 'h':
            help();
            break;
        case 'f':
            fmt = optarg;
            break;
        case 'q':
            quiet = true;
            break;
        case OPTION_OBJECT: {
            QemuOpts *opts;
            opts = qemu_opts_parse_noisily(&qemu_object_opts,
                                           optarg, true);
            if (!opts) {
                return 1;
            }
        }   break;
        case OPTION_IMAGE_OPTS:
            image_opts = true;
            break;
        case OPTION_PREALLOCATION:
            prealloc = qapi_enum_parse(&PreallocMode_lookup, optarg,
                                       PREALLOC_MODE__MAX, NULL);
            if (prealloc == PREALLOC_MODE__MAX) {
                error_report("Invalid preallocation mode '%s'", optarg);
                return 1;
            }
            break;
        case OPTION_SHRINK:
            shrink = true;
            break;
        }
    }
    if (optind != argc - 1) {
        error_exit("Expecting image file name and size");
    }
    filename = argv[optind++];

    if (qemu_opts_foreach(&qemu_object_opts,
                          user_creatable_add_opts_foreach,
                          qemu_img_object_print_help, &error_fatal)) {
        return 1;
    }

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
    if (!qemu_opt_set(param, BLOCK_OPT_SIZE, size, &err)) {
        error_report_err(err);
        ret = -1;
        qemu_opts_del(param);
        goto out;
    }
    n = qemu_opt_get_size(param, BLOCK_OPT_SIZE, 0);
    qemu_opts_del(param);

    blk = img_open(image_opts, filename, fmt,
                   BDRV_O_RDWR | BDRV_O_RESIZE, false, quiet,
                   false);
    if (!blk) {
        ret = -1;
        goto out;
    }

    current_size = blk_getlength(blk);
    if (current_size < 0) {
        error_report("Failed to inquire current image length: %s",
                     strerror(-current_size));
        ret = -1;
        goto out;
    }

    if (relative) {
        total_size = current_size + n * relative;
    } else {
        total_size = n;
    }
    if (total_size <= 0) {
        error_report("New image size must be positive");
        ret = -1;
        goto out;
    }

    if (total_size <= current_size && prealloc != PREALLOC_MODE_OFF) {
        error_report("Preallocation can only be used for growing images");
        ret = -1;
        goto out;
    }

    if (total_size < current_size && !shrink) {
        error_report("Use the --shrink option to perform a shrink operation.");
        warn_report("Shrinking an image will delete all data beyond the "
                    "shrunken image's end. Before performing such an "
                    "operation, make sure there is no important data there.");
        ret = -1;
        goto out;
    }

    /*
     * The user expects the image to have the desired size after
     * resizing, so pass @exact=true.  It is of no use to report
     * success when the image has not actually been resized.
     */
    ret = blk_truncate(blk, total_size, true, prealloc, 0, &err);
    if (!ret) {
        qprintf(quiet, "Image resized.\n");
    } else {
        error_report_err(err);
    }
out:
    blk_unref(blk);
    if (ret) {
        return 1;
    }
    return 0;
}

static void amend_status_cb(BlockDriverState *bs,
                            int64_t offset, int64_t total_work_size,
                            void *opaque)
{
    qemu_progress_print(100.f * offset / total_work_size, 0);
}

static int print_amend_option_help(const char *format)
{
    BlockDriver *drv;

    /* Find driver and parse its options */
    drv = bdrv_find_format(format);
    if (!drv) {
        error_report("Unknown file format '%s'", format);
        return 1;
    }

    if (!drv->bdrv_amend_options) {
        error_report("Format driver '%s' does not support option amendment",
                     format);
        return 1;
    }

    /* Every driver supporting amendment must have amend_opts */
    assert(drv->amend_opts);

    printf("Amend options for '%s':\n", format);
    qemu_opts_print_help(drv->amend_opts, false);
    return 0;
}

static int img_amend(int argc, char **argv)
{
    Error *err = NULL;
    int c, ret = 0;
    char *options = NULL;
    QemuOptsList *amend_opts = NULL;
    QemuOpts *opts = NULL;
    const char *fmt = NULL, *filename, *cache;
    int flags;
    bool writethrough;
    bool quiet = false, progress = false;
    BlockBackend *blk = NULL;
    BlockDriverState *bs = NULL;
    bool image_opts = false;
    bool force = false;

    cache = BDRV_DEFAULT_CACHE;
    for (;;) {
        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"object", required_argument, 0, OPTION_OBJECT},
            {"image-opts", no_argument, 0, OPTION_IMAGE_OPTS},
            {"force", no_argument, 0, OPTION_FORCE},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, ":ho:f:t:pq",
                        long_options, NULL);
        if (c == -1) {
            break;
        }

        switch (c) {
        case ':':
            missing_argument(argv[optind - 1]);
            break;
        case '?':
            unrecognized_option(argv[optind - 1]);
            break;
        case 'h':
            help();
            break;
        case 'o':
            if (accumulate_options(&options, optarg) < 0) {
                ret = -1;
                goto out_no_progress;
            }
            break;
        case 'f':
            fmt = optarg;
            break;
        case 't':
            cache = optarg;
            break;
        case 'p':
            progress = true;
            break;
        case 'q':
            quiet = true;
            break;
        case OPTION_OBJECT:
            opts = qemu_opts_parse_noisily(&qemu_object_opts,
                                           optarg, true);
            if (!opts) {
                ret = -1;
                goto out_no_progress;
            }
            break;
        case OPTION_IMAGE_OPTS:
            image_opts = true;
            break;
        case OPTION_FORCE:
            force = true;
            break;
        }
    }

    if (!options) {
        error_exit("Must specify options (-o)");
    }

    if (qemu_opts_foreach(&qemu_object_opts,
                          user_creatable_add_opts_foreach,
                          qemu_img_object_print_help, &error_fatal)) {
        ret = -1;
        goto out_no_progress;
    }

    if (quiet) {
        progress = false;
    }
    qemu_progress_init(progress, 1.0);

    filename = (optind == argc - 1) ? argv[argc - 1] : NULL;
    if (fmt && has_help_option(options)) {
        /* If a format is explicitly specified (and possibly no filename is
         * given), print option help here */
        ret = print_amend_option_help(fmt);
        goto out;
    }

    if (optind != argc - 1) {
        error_report("Expecting one image file name");
        ret = -1;
        goto out;
    }

    flags = BDRV_O_RDWR;
    ret = bdrv_parse_cache_mode(cache, &flags, &writethrough);
    if (ret < 0) {
        error_report("Invalid cache option: %s", cache);
        goto out;
    }

    blk = img_open(image_opts, filename, fmt, flags, writethrough, quiet,
                   false);
    if (!blk) {
        ret = -1;
        goto out;
    }
    bs = blk_bs(blk);

    fmt = bs->drv->format_name;

    if (has_help_option(options)) {
        /* If the format was auto-detected, print option help here */
        ret = print_amend_option_help(fmt);
        goto out;
    }

    if (!bs->drv->bdrv_amend_options) {
        error_report("Format driver '%s' does not support option amendment",
                     fmt);
        ret = -1;
        goto out;
    }

    /* Every driver supporting amendment must have amend_opts */
    assert(bs->drv->amend_opts);

    amend_opts = qemu_opts_append(amend_opts, bs->drv->amend_opts);
    opts = qemu_opts_create(amend_opts, NULL, 0, &error_abort);
    if (!qemu_opts_do_parse(opts, options, NULL, &err)) {
        /* Try to parse options using the create options */
        amend_opts = qemu_opts_append(amend_opts, bs->drv->create_opts);
        qemu_opts_del(opts);
        opts = qemu_opts_create(amend_opts, NULL, 0, &error_abort);
        if (qemu_opts_do_parse(opts, options, NULL, NULL)) {
            error_append_hint(&err,
                              "This option is only supported for image creation\n");
        }

        error_report_err(err);
        ret = -1;
        goto out;
    }

    /* In case the driver does not call amend_status_cb() */
    qemu_progress_print(0.f, 0);
    ret = bdrv_amend_options(bs, opts, &amend_status_cb, NULL, force, &err);
    qemu_progress_print(100.f, 0);
    if (ret < 0) {
        error_report_err(err);
        goto out;
    }

out:
    qemu_progress_end();

out_no_progress:
    blk_unref(blk);
    qemu_opts_del(opts);
    qemu_opts_free(amend_opts);
    g_free(options);

    if (ret) {
        return 1;
    }
    return 0;
}

typedef struct BenchData {
    BlockBackend *blk;
    uint64_t image_size;
    bool write;
    int bufsize;
    int step;
    int nrreq;
    int n;
    int flush_interval;
    bool drain_on_flush;
    uint8_t *buf;
    QEMUIOVector *qiov;

    int in_flight;
    bool in_flush;
    uint64_t offset;
} BenchData;

static void bench_undrained_flush_cb(void *opaque, int ret)
{
    if (ret < 0) {
        error_report("Failed flush request: %s", strerror(-ret));
        exit(EXIT_FAILURE);
    }
}

static void bench_cb(void *opaque, int ret)
{
    BenchData *b = opaque;
    BlockAIOCB *acb;

    if (ret < 0) {
        error_report("Failed request: %s", strerror(-ret));
        exit(EXIT_FAILURE);
    }

    if (b->in_flush) {
        /* Just finished a flush with drained queue: Start next requests */
        assert(b->in_flight == 0);
        b->in_flush = false;
    } else if (b->in_flight > 0) {
        int remaining = b->n - b->in_flight;

        b->n--;
        b->in_flight--;

        /* Time for flush? Drain queue if requested, then flush */
        if (b->flush_interval && remaining % b->flush_interval == 0) {
            if (!b->in_flight || !b->drain_on_flush) {
                BlockCompletionFunc *cb;

                if (b->drain_on_flush) {
                    b->in_flush = true;
                    cb = bench_cb;
                } else {
                    cb = bench_undrained_flush_cb;
                }

                acb = blk_aio_flush(b->blk, cb, b);
                if (!acb) {
                    error_report("Failed to issue flush request");
                    exit(EXIT_FAILURE);
                }
            }
            if (b->drain_on_flush) {
                return;
            }
        }
    }

    while (b->n > b->in_flight && b->in_flight < b->nrreq) {
        int64_t offset = b->offset;
        /* blk_aio_* might look for completed I/Os and kick bench_cb
         * again, so make sure this operation is counted by in_flight
         * and b->offset is ready for the next submission.
         */
        b->in_flight++;
        b->offset += b->step;
        b->offset %= b->image_size;
        if (b->write) {
            acb = blk_aio_pwritev(b->blk, offset, b->qiov, 0, bench_cb, b);
        } else {
            acb = blk_aio_preadv(b->blk, offset, b->qiov, 0, bench_cb, b);
        }
        if (!acb) {
            error_report("Failed to issue request");
            exit(EXIT_FAILURE);
        }
    }
}

static int img_bench(int argc, char **argv)
{
    int c, ret = 0;
    const char *fmt = NULL, *filename;
    bool quiet = false;
    bool image_opts = false;
    bool is_write = false;
    int count = 75000;
    int depth = 64;
    int64_t offset = 0;
    size_t bufsize = 4096;
    int pattern = 0;
    size_t step = 0;
    int flush_interval = 0;
    bool drain_on_flush = true;
    int64_t image_size;
    BlockBackend *blk = NULL;
    BenchData data = {};
    int flags = 0;
    bool writethrough = false;
    struct timeval t1, t2;
    int i;
    bool force_share = false;
    size_t buf_size;

    for (;;) {
        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"flush-interval", required_argument, 0, OPTION_FLUSH_INTERVAL},
            {"image-opts", no_argument, 0, OPTION_IMAGE_OPTS},
            {"pattern", required_argument, 0, OPTION_PATTERN},
            {"no-drain", no_argument, 0, OPTION_NO_DRAIN},
            {"force-share", no_argument, 0, 'U'},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, ":hc:d:f:ni:o:qs:S:t:wU", long_options,
                        NULL);
        if (c == -1) {
            break;
        }

        switch (c) {
        case ':':
            missing_argument(argv[optind - 1]);
            break;
        case '?':
            unrecognized_option(argv[optind - 1]);
            break;
        case 'h':
            help();
            break;
        case 'c':
        {
            unsigned long res;

            if (qemu_strtoul(optarg, NULL, 0, &res) < 0 || res > INT_MAX) {
                error_report("Invalid request count specified");
                return 1;
            }
            count = res;
            break;
        }
        case 'd':
        {
            unsigned long res;

            if (qemu_strtoul(optarg, NULL, 0, &res) < 0 || res > INT_MAX) {
                error_report("Invalid queue depth specified");
                return 1;
            }
            depth = res;
            break;
        }
        case 'f':
            fmt = optarg;
            break;
        case 'n':
            flags |= BDRV_O_NATIVE_AIO;
            break;
        case 'i':
            ret = bdrv_parse_aio(optarg, &flags);
            if (ret < 0) {
                error_report("Invalid aio option: %s", optarg);
                ret = -1;
                goto out;
            }
            break;
        case 'o':
        {
            offset = cvtnum("offset", optarg);
            if (offset < 0) {
                return 1;
            }
            break;
        }
            break;
        case 'q':
            quiet = true;
            break;
        case 's':
        {
            int64_t sval;

            sval = cvtnum_full("buffer size", optarg, 0, INT_MAX);
            if (sval < 0) {
                return 1;
            }

            bufsize = sval;
            break;
        }
        case 'S':
        {
            int64_t sval;

            sval = cvtnum_full("step_size", optarg, 0, INT_MAX);
            if (sval < 0) {
                return 1;
            }

            step = sval;
            break;
        }
        case 't':
            ret = bdrv_parse_cache_mode(optarg, &flags, &writethrough);
            if (ret < 0) {
                error_report("Invalid cache mode");
                ret = -1;
                goto out;
            }
            break;
        case 'w':
            flags |= BDRV_O_RDWR;
            is_write = true;
            break;
        case 'U':
            force_share = true;
            break;
        case OPTION_PATTERN:
        {
            unsigned long res;

            if (qemu_strtoul(optarg, NULL, 0, &res) < 0 || res > 0xff) {
                error_report("Invalid pattern byte specified");
                return 1;
            }
            pattern = res;
            break;
        }
        case OPTION_FLUSH_INTERVAL:
        {
            unsigned long res;

            if (qemu_strtoul(optarg, NULL, 0, &res) < 0 || res > INT_MAX) {
                error_report("Invalid flush interval specified");
                return 1;
            }
            flush_interval = res;
            break;
        }
        case OPTION_NO_DRAIN:
            drain_on_flush = false;
            break;
        case OPTION_IMAGE_OPTS:
            image_opts = true;
            break;
        }
    }

    if (optind != argc - 1) {
        error_exit("Expecting one image file name");
    }
    filename = argv[argc - 1];

    if (!is_write && flush_interval) {
        error_report("--flush-interval is only available in write tests");
        ret = -1;
        goto out;
    }
    if (flush_interval && flush_interval < depth) {
        error_report("Flush interval can't be smaller than depth");
        ret = -1;
        goto out;
    }

    blk = img_open(image_opts, filename, fmt, flags, writethrough, quiet,
                   force_share);
    if (!blk) {
        ret = -1;
        goto out;
    }

    image_size = blk_getlength(blk);
    if (image_size < 0) {
        ret = image_size;
        goto out;
    }

    data = (BenchData) {
        .blk            = blk,
        .image_size     = image_size,
        .bufsize        = bufsize,
        .step           = step ?: bufsize,
        .nrreq          = depth,
        .n              = count,
        .offset         = offset,
        .write          = is_write,
        .flush_interval = flush_interval,
        .drain_on_flush = drain_on_flush,
    };
    printf("Sending %d %s requests, %d bytes each, %d in parallel "
           "(starting at offset %" PRId64 ", step size %d)\n",
           data.n, data.write ? "write" : "read", data.bufsize, data.nrreq,
           data.offset, data.step);
    if (flush_interval) {
        printf("Sending flush every %d requests\n", flush_interval);
    }

    buf_size = data.nrreq * data.bufsize;
    data.buf = blk_blockalign(blk, buf_size);
    memset(data.buf, pattern, data.nrreq * data.bufsize);

    blk_register_buf(blk, data.buf, buf_size);

    data.qiov = g_new(QEMUIOVector, data.nrreq);
    for (i = 0; i < data.nrreq; i++) {
        qemu_iovec_init(&data.qiov[i], 1);
        qemu_iovec_add(&data.qiov[i],
                       data.buf + i * data.bufsize, data.bufsize);
    }

    gettimeofday(&t1, NULL);
    bench_cb(&data, 0);

    while (data.n > 0) {
        main_loop_wait(false);
    }
    gettimeofday(&t2, NULL);

    printf("Run completed in %3.3f seconds.\n",
           (t2.tv_sec - t1.tv_sec)
           + ((double)(t2.tv_usec - t1.tv_usec) / 1000000));

out:
    if (data.buf) {
        blk_unregister_buf(blk, data.buf);
    }
    qemu_vfree(data.buf);
    blk_unref(blk);

    if (ret) {
        return 1;
    }
    return 0;
}

enum ImgBitmapAct {
    BITMAP_ADD,
    BITMAP_REMOVE,
    BITMAP_CLEAR,
    BITMAP_ENABLE,
    BITMAP_DISABLE,
    BITMAP_MERGE,
};
typedef struct ImgBitmapAction {
    enum ImgBitmapAct act;
    const char *src; /* only used for merge */
    QSIMPLEQ_ENTRY(ImgBitmapAction) next;
} ImgBitmapAction;

static int img_bitmap(int argc, char **argv)
{
    Error *err = NULL;
    int c, ret = 1;
    QemuOpts *opts = NULL;
    const char *fmt = NULL, *src_fmt = NULL, *src_filename = NULL;
    const char *filename, *bitmap;
    BlockBackend *blk = NULL, *src = NULL;
    BlockDriverState *bs = NULL, *src_bs = NULL;
    bool image_opts = false;
    int64_t granularity = 0;
    bool add = false, merge = false;
    QSIMPLEQ_HEAD(, ImgBitmapAction) actions;
    ImgBitmapAction *act, *act_next;
    const char *op;

    QSIMPLEQ_INIT(&actions);

    for (;;) {
        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"object", required_argument, 0, OPTION_OBJECT},
            {"image-opts", no_argument, 0, OPTION_IMAGE_OPTS},
            {"add", no_argument, 0, OPTION_ADD},
            {"remove", no_argument, 0, OPTION_REMOVE},
            {"clear", no_argument, 0, OPTION_CLEAR},
            {"enable", no_argument, 0, OPTION_ENABLE},
            {"disable", no_argument, 0, OPTION_DISABLE},
            {"merge", required_argument, 0, OPTION_MERGE},
            {"granularity", required_argument, 0, 'g'},
            {"source-file", required_argument, 0, 'b'},
            {"source-format", required_argument, 0, 'F'},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, ":b:f:F:g:h", long_options, NULL);
        if (c == -1) {
            break;
        }

        switch (c) {
        case ':':
            missing_argument(argv[optind - 1]);
            break;
        case '?':
            unrecognized_option(argv[optind - 1]);
            break;
        case 'h':
            help();
            break;
        case 'b':
            src_filename = optarg;
            break;
        case 'f':
            fmt = optarg;
            break;
        case 'F':
            src_fmt = optarg;
            break;
        case 'g':
            granularity = cvtnum("granularity", optarg);
            if (granularity < 0) {
                return 1;
            }
            break;
        case OPTION_ADD:
            act = g_new0(ImgBitmapAction, 1);
            act->act = BITMAP_ADD;
            QSIMPLEQ_INSERT_TAIL(&actions, act, next);
            add = true;
            break;
        case OPTION_REMOVE:
            act = g_new0(ImgBitmapAction, 1);
            act->act = BITMAP_REMOVE;
            QSIMPLEQ_INSERT_TAIL(&actions, act, next);
            break;
        case OPTION_CLEAR:
            act = g_new0(ImgBitmapAction, 1);
            act->act = BITMAP_CLEAR;
            QSIMPLEQ_INSERT_TAIL(&actions, act, next);
            break;
        case OPTION_ENABLE:
            act = g_new0(ImgBitmapAction, 1);
            act->act = BITMAP_ENABLE;
            QSIMPLEQ_INSERT_TAIL(&actions, act, next);
            break;
        case OPTION_DISABLE:
            act = g_new0(ImgBitmapAction, 1);
            act->act = BITMAP_DISABLE;
            QSIMPLEQ_INSERT_TAIL(&actions, act, next);
            break;
        case OPTION_MERGE:
            act = g_new0(ImgBitmapAction, 1);
            act->act = BITMAP_MERGE;
            act->src = optarg;
            QSIMPLEQ_INSERT_TAIL(&actions, act, next);
            merge = true;
            break;
        case OPTION_OBJECT:
            opts = qemu_opts_parse_noisily(&qemu_object_opts, optarg, true);
            if (!opts) {
                goto out;
            }
            break;
        case OPTION_IMAGE_OPTS:
            image_opts = true;
            break;
        }
    }

    if (qemu_opts_foreach(&qemu_object_opts,
                          user_creatable_add_opts_foreach,
                          qemu_img_object_print_help, &error_fatal)) {
        goto out;
    }

    if (QSIMPLEQ_EMPTY(&actions)) {
        error_report("Need at least one of --add, --remove, --clear, "
                     "--enable, --disable, or --merge");
        goto out;
    }

    if (granularity && !add) {
        error_report("granularity only supported with --add");
        goto out;
    }
    if (src_fmt && !src_filename) {
        error_report("-F only supported with -b");
        goto out;
    }
    if (src_filename && !merge) {
        error_report("Merge bitmap source file only supported with "
                     "--merge");
        goto out;
    }

    if (optind != argc - 2) {
        error_report("Expecting filename and bitmap name");
        goto out;
    }

    filename = argv[optind];
    bitmap = argv[optind + 1];

    blk = img_open(image_opts, filename, fmt, BDRV_O_RDWR, false, false,
                   false);
    if (!blk) {
        goto out;
    }
    bs = blk_bs(blk);
    if (src_filename) {
        src = img_open(false, src_filename, src_fmt, 0, false, false, false);
        if (!src) {
            goto out;
        }
        src_bs = blk_bs(src);
    } else {
        src_bs = bs;
    }

    QSIMPLEQ_FOREACH_SAFE(act, &actions, next, act_next) {
        switch (act->act) {
        case BITMAP_ADD:
            qmp_block_dirty_bitmap_add(bs->node_name, bitmap,
                                       !!granularity, granularity, true, true,
                                       false, false, &err);
            op = "add";
            break;
        case BITMAP_REMOVE:
            qmp_block_dirty_bitmap_remove(bs->node_name, bitmap, &err);
            op = "remove";
            break;
        case BITMAP_CLEAR:
            qmp_block_dirty_bitmap_clear(bs->node_name, bitmap, &err);
            op = "clear";
            break;
        case BITMAP_ENABLE:
            qmp_block_dirty_bitmap_enable(bs->node_name, bitmap, &err);
            op = "enable";
            break;
        case BITMAP_DISABLE:
            qmp_block_dirty_bitmap_disable(bs->node_name, bitmap, &err);
            op = "disable";
            break;
        case BITMAP_MERGE:
            do_dirty_bitmap_merge(bs->node_name, bitmap, src_bs->node_name,
                                  act->src, &err);
            op = "merge";
            break;
        default:
            g_assert_not_reached();
        }

        if (err) {
            error_reportf_err(err, "Operation %s on bitmap %s failed: ",
                              op, bitmap);
            goto out;
        }
        g_free(act);
    }

    ret = 0;

 out:
    blk_unref(src);
    blk_unref(blk);
    qemu_opts_del(opts);
    return ret;
}

#define C_BS      01
#define C_COUNT   02
#define C_IF      04
#define C_OF      010
#define C_SKIP    020

struct DdInfo {
    unsigned int flags;
    int64_t count;
};

struct DdIo {
    int bsz;    /* Block size */
    char *filename;
    uint8_t *buf;
    int64_t offset;
};

struct DdOpts {
    const char *name;
    int (*f)(const char *, struct DdIo *, struct DdIo *, struct DdInfo *);
    unsigned int flag;
};

static int img_dd_bs(const char *arg,
                     struct DdIo *in, struct DdIo *out,
                     struct DdInfo *dd)
{
    int64_t res;

    res = cvtnum_full("bs", arg, 1, INT_MAX);

    if (res < 0) {
        return 1;
    }
    in->bsz = out->bsz = res;

    return 0;
}

static int img_dd_count(const char *arg,
                        struct DdIo *in, struct DdIo *out,
                        struct DdInfo *dd)
{
    dd->count = cvtnum("count", arg);

    if (dd->count < 0) {
        return 1;
    }

    return 0;
}

static int img_dd_if(const char *arg,
                     struct DdIo *in, struct DdIo *out,
                     struct DdInfo *dd)
{
    in->filename = g_strdup(arg);

    return 0;
}

static int img_dd_of(const char *arg,
                     struct DdIo *in, struct DdIo *out,
                     struct DdInfo *dd)
{
    out->filename = g_strdup(arg);

    return 0;
}

static int img_dd_skip(const char *arg,
                       struct DdIo *in, struct DdIo *out,
                       struct DdInfo *dd)
{
    in->offset = cvtnum("skip", arg);

    if (in->offset < 0) {
        return 1;
    }

    return 0;
}

static int img_dd(int argc, char **argv)
{
    int ret = 0;
    char *arg = NULL;
    char *tmp;
    BlockDriver *drv = NULL, *proto_drv = NULL;
    BlockBackend *blk1 = NULL, *blk2 = NULL;
    QemuOpts *opts = NULL;
    QemuOptsList *create_opts = NULL;
    Error *local_err = NULL;
    bool image_opts = false;
    int c, i;
    const char *out_fmt = "raw";
    const char *fmt = NULL;
    int64_t size = 0;
    int64_t block_count = 0, out_pos, in_pos;
    bool force_share = false;
    struct DdInfo dd = {
        .flags = 0,
        .count = 0,
    };
    struct DdIo in = {
        .bsz = 512, /* Block size is by default 512 bytes */
        .filename = NULL,
        .buf = NULL,
        .offset = 0
    };
    struct DdIo out = {
        .bsz = 512,
        .filename = NULL,
        .buf = NULL,
        .offset = 0
    };

    const struct DdOpts options[] = {
        { "bs", img_dd_bs, C_BS },
        { "count", img_dd_count, C_COUNT },
        { "if", img_dd_if, C_IF },
        { "of", img_dd_of, C_OF },
        { "skip", img_dd_skip, C_SKIP },
        { NULL, NULL, 0 }
    };
    const struct option long_options[] = {
        { "help", no_argument, 0, 'h'},
        { "object", required_argument, 0, OPTION_OBJECT},
        { "image-opts", no_argument, 0, OPTION_IMAGE_OPTS},
        { "force-share", no_argument, 0, 'U'},
        { 0, 0, 0, 0 }
    };

    while ((c = getopt_long(argc, argv, ":hf:O:U", long_options, NULL))) {
        if (c == EOF) {
            break;
        }
        switch (c) {
        case 'O':
            out_fmt = optarg;
            break;
        case 'f':
            fmt = optarg;
            break;
        case ':':
            missing_argument(argv[optind - 1]);
            break;
        case '?':
            unrecognized_option(argv[optind - 1]);
            break;
        case 'h':
            help();
            break;
        case 'U':
            force_share = true;
            break;
        case OPTION_OBJECT:
            if (!qemu_opts_parse_noisily(&qemu_object_opts, optarg, true)) {
                ret = -1;
                goto out;
            }
            break;
        case OPTION_IMAGE_OPTS:
            image_opts = true;
            break;
        }
    }

    for (i = optind; i < argc; i++) {
        int j;
        arg = g_strdup(argv[i]);

        tmp = strchr(arg, '=');
        if (tmp == NULL) {
            error_report("unrecognized operand %s", arg);
            ret = -1;
            goto out;
        }

        *tmp++ = '\0';

        for (j = 0; options[j].name != NULL; j++) {
            if (!strcmp(arg, options[j].name)) {
                break;
            }
        }
        if (options[j].name == NULL) {
            error_report("unrecognized operand %s", arg);
            ret = -1;
            goto out;
        }

        if (options[j].f(tmp, &in, &out, &dd) != 0) {
            ret = -1;
            goto out;
        }
        dd.flags |= options[j].flag;
        g_free(arg);
        arg = NULL;
    }

    if (!(dd.flags & C_IF && dd.flags & C_OF)) {
        error_report("Must specify both input and output files");
        ret = -1;
        goto out;
    }

    if (qemu_opts_foreach(&qemu_object_opts,
                          user_creatable_add_opts_foreach,
                          qemu_img_object_print_help, &error_fatal)) {
        ret = -1;
        goto out;
    }

    blk1 = img_open(image_opts, in.filename, fmt, 0, false, false,
                    force_share);

    if (!blk1) {
        ret = -1;
        goto out;
    }

    drv = bdrv_find_format(out_fmt);
    if (!drv) {
        error_report("Unknown file format");
        ret = -1;
        goto out;
    }
    proto_drv = bdrv_find_protocol(out.filename, true, &local_err);

    if (!proto_drv) {
        error_report_err(local_err);
        ret = -1;
        goto out;
    }
    if (!drv->create_opts) {
        error_report("Format driver '%s' does not support image creation",
                     drv->format_name);
        ret = -1;
        goto out;
    }
    if (!proto_drv->create_opts) {
        error_report("Protocol driver '%s' does not support image creation",
                     proto_drv->format_name);
        ret = -1;
        goto out;
    }
    create_opts = qemu_opts_append(create_opts, drv->create_opts);
    create_opts = qemu_opts_append(create_opts, proto_drv->create_opts);

    opts = qemu_opts_create(create_opts, NULL, 0, &error_abort);

    size = blk_getlength(blk1);
    if (size < 0) {
        error_report("Failed to get size for '%s'", in.filename);
        ret = -1;
        goto out;
    }

    if (dd.flags & C_COUNT && dd.count <= INT64_MAX / in.bsz &&
        dd.count * in.bsz < size) {
        size = dd.count * in.bsz;
    }

    /* Overflow means the specified offset is beyond input image's size */
    if (dd.flags & C_SKIP && (in.offset > INT64_MAX / in.bsz ||
                              size < in.bsz * in.offset)) {
        qemu_opt_set_number(opts, BLOCK_OPT_SIZE, 0, &error_abort);
    } else {
        qemu_opt_set_number(opts, BLOCK_OPT_SIZE,
                            size - in.bsz * in.offset, &error_abort);
    }

    ret = bdrv_create(drv, out.filename, opts, &local_err);
    if (ret < 0) {
        error_reportf_err(local_err,
                          "%s: error while creating output image: ",
                          out.filename);
        ret = -1;
        goto out;
    }

    /* TODO, we can't honour --image-opts for the target,
     * since it needs to be given in a format compatible
     * with the bdrv_create() call above which does not
     * support image-opts style.
     */
    blk2 = img_open_file(out.filename, NULL, out_fmt, BDRV_O_RDWR,
                         false, false, false);

    if (!blk2) {
        ret = -1;
        goto out;
    }

    if (dd.flags & C_SKIP && (in.offset > INT64_MAX / in.bsz ||
                              size < in.offset * in.bsz)) {
        /* We give a warning if the skip option is bigger than the input
         * size and create an empty output disk image (i.e. like dd(1)).
         */
        error_report("%s: cannot skip to specified offset", in.filename);
        in_pos = size;
    } else {
        in_pos = in.offset * in.bsz;
    }

    in.buf = g_new(uint8_t, in.bsz);

    for (out_pos = 0; in_pos < size; block_count++) {
        int in_ret, out_ret;

        if (in_pos + in.bsz > size) {
            in_ret = blk_pread(blk1, in_pos, in.buf, size - in_pos);
        } else {
            in_ret = blk_pread(blk1, in_pos, in.buf, in.bsz);
        }
        if (in_ret < 0) {
            error_report("error while reading from input image file: %s",
                         strerror(-in_ret));
            ret = -1;
            goto out;
        }
        in_pos += in_ret;

        out_ret = blk_pwrite(blk2, out_pos, in.buf, in_ret, 0);

        if (out_ret < 0) {
            error_report("error while writing to output image file: %s",
                         strerror(-out_ret));
            ret = -1;
            goto out;
        }
        out_pos += out_ret;
    }

out:
    g_free(arg);
    qemu_opts_del(opts);
    qemu_opts_free(create_opts);
    blk_unref(blk1);
    blk_unref(blk2);
    g_free(in.filename);
    g_free(out.filename);
    g_free(in.buf);
    g_free(out.buf);

    if (ret) {
        return 1;
    }
    return 0;
}

static void dump_json_block_measure_info(BlockMeasureInfo *info)
{
    QString *str;
    QObject *obj;
    Visitor *v = qobject_output_visitor_new(&obj);

    visit_type_BlockMeasureInfo(v, NULL, &info, &error_abort);
    visit_complete(v, &obj);
    str = qobject_to_json_pretty(obj);
    assert(str != NULL);
    printf("%s\n", qstring_get_str(str));
    qobject_unref(obj);
    visit_free(v);
    qobject_unref(str);
}

static int img_measure(int argc, char **argv)
{
    static const struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"image-opts", no_argument, 0, OPTION_IMAGE_OPTS},
        {"object", required_argument, 0, OPTION_OBJECT},
        {"output", required_argument, 0, OPTION_OUTPUT},
        {"size", required_argument, 0, OPTION_SIZE},
        {"force-share", no_argument, 0, 'U'},
        {0, 0, 0, 0}
    };
    OutputFormat output_format = OFORMAT_HUMAN;
    BlockBackend *in_blk = NULL;
    BlockDriver *drv;
    const char *filename = NULL;
    const char *fmt = NULL;
    const char *out_fmt = "raw";
    char *options = NULL;
    char *snapshot_name = NULL;
    bool force_share = false;
    QemuOpts *opts = NULL;
    QemuOpts *object_opts = NULL;
    QemuOpts *sn_opts = NULL;
    QemuOptsList *create_opts = NULL;
    bool image_opts = false;
    uint64_t img_size = UINT64_MAX;
    BlockMeasureInfo *info = NULL;
    Error *local_err = NULL;
    int ret = 1;
    int c;

    while ((c = getopt_long(argc, argv, "hf:O:o:l:U",
                            long_options, NULL)) != -1) {
        switch (c) {
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
        case 'o':
            if (accumulate_options(&options, optarg) < 0) {
                goto out;
            }
            break;
        case 'l':
            if (strstart(optarg, SNAPSHOT_OPT_BASE, NULL)) {
                sn_opts = qemu_opts_parse_noisily(&internal_snapshot_opts,
                                                  optarg, false);
                if (!sn_opts) {
                    error_report("Failed in parsing snapshot param '%s'",
                                 optarg);
                    goto out;
                }
            } else {
                snapshot_name = optarg;
            }
            break;
        case 'U':
            force_share = true;
            break;
        case OPTION_OBJECT:
            object_opts = qemu_opts_parse_noisily(&qemu_object_opts,
                                                  optarg, true);
            if (!object_opts) {
                goto out;
            }
            break;
        case OPTION_IMAGE_OPTS:
            image_opts = true;
            break;
        case OPTION_OUTPUT:
            if (!strcmp(optarg, "json")) {
                output_format = OFORMAT_JSON;
            } else if (!strcmp(optarg, "human")) {
                output_format = OFORMAT_HUMAN;
            } else {
                error_report("--output must be used with human or json "
                             "as argument.");
                goto out;
            }
            break;
        case OPTION_SIZE:
        {
            int64_t sval;

            sval = cvtnum("image size", optarg);
            if (sval < 0) {
                goto out;
            }
            img_size = (uint64_t)sval;
        }
        break;
        }
    }

    if (qemu_opts_foreach(&qemu_object_opts,
                          user_creatable_add_opts_foreach,
                          qemu_img_object_print_help, &error_fatal)) {
        goto out;
    }

    if (argc - optind > 1) {
        error_report("At most one filename argument is allowed.");
        goto out;
    } else if (argc - optind == 1) {
        filename = argv[optind];
    }

    if (!filename && (image_opts || fmt || snapshot_name || sn_opts)) {
        error_report("--image-opts, -f, and -l require a filename argument.");
        goto out;
    }
    if (filename && img_size != UINT64_MAX) {
        error_report("--size N cannot be used together with a filename.");
        goto out;
    }
    if (!filename && img_size == UINT64_MAX) {
        error_report("Either --size N or one filename must be specified.");
        goto out;
    }

    if (filename) {
        in_blk = img_open(image_opts, filename, fmt, 0,
                          false, false, force_share);
        if (!in_blk) {
            goto out;
        }

        if (sn_opts) {
            bdrv_snapshot_load_tmp(blk_bs(in_blk),
                    qemu_opt_get(sn_opts, SNAPSHOT_OPT_ID),
                    qemu_opt_get(sn_opts, SNAPSHOT_OPT_NAME),
                    &local_err);
        } else if (snapshot_name != NULL) {
            bdrv_snapshot_load_tmp_by_id_or_name(blk_bs(in_blk),
                    snapshot_name, &local_err);
        }
        if (local_err) {
            error_reportf_err(local_err, "Failed to load snapshot: ");
            goto out;
        }
    }

    drv = bdrv_find_format(out_fmt);
    if (!drv) {
        error_report("Unknown file format '%s'", out_fmt);
        goto out;
    }
    if (!drv->create_opts) {
        error_report("Format driver '%s' does not support image creation",
                     drv->format_name);
        goto out;
    }

    create_opts = qemu_opts_append(create_opts, drv->create_opts);
    create_opts = qemu_opts_append(create_opts, bdrv_file.create_opts);
    opts = qemu_opts_create(create_opts, NULL, 0, &error_abort);
    if (options) {
        if (!qemu_opts_do_parse(opts, options, NULL, &local_err)) {
            error_report_err(local_err);
            error_report("Invalid options for file format '%s'", out_fmt);
            goto out;
        }
    }
    if (img_size != UINT64_MAX) {
        qemu_opt_set_number(opts, BLOCK_OPT_SIZE, img_size, &error_abort);
    }

    info = bdrv_measure(drv, opts, in_blk ? blk_bs(in_blk) : NULL, &local_err);
    if (local_err) {
        error_report_err(local_err);
        goto out;
    }

    if (output_format == OFORMAT_HUMAN) {
        printf("required size: %" PRIu64 "\n", info->required);
        printf("fully allocated size: %" PRIu64 "\n", info->fully_allocated);
        if (info->has_bitmaps) {
            printf("bitmaps size: %" PRIu64 "\n", info->bitmaps);
        }
    } else {
        dump_json_block_measure_info(info);
    }

    ret = 0;

out:
    qapi_free_BlockMeasureInfo(info);
    qemu_opts_del(object_opts);
    qemu_opts_del(opts);
    qemu_opts_del(sn_opts);
    qemu_opts_free(create_opts);
    g_free(options);
    blk_unref(in_blk);
    return ret;
}

static const img_cmd_t img_cmds[] = {
#define DEF(option, callback, arg_string)        \
    { option, callback },
#include "qemu-img-cmds.h"
#undef DEF
    { NULL, NULL, },
};

int main(int argc, char **argv)
{
    const img_cmd_t *cmd;
    const char *cmdname;
    Error *local_error = NULL;
    char *trace_file = NULL;
    int c;
    static const struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 'V'},
        {"trace", required_argument, NULL, 'T'},
        {0, 0, 0, 0}
    };

#ifdef CONFIG_POSIX
    signal(SIGPIPE, SIG_IGN);
#endif

    error_init(argv[0]);
    module_call_init(MODULE_INIT_TRACE);
    qemu_init_exec_dir(argv[0]);

    if (qemu_init_main_loop(&local_error)) {
        error_report_err(local_error);
        exit(EXIT_FAILURE);
    }

    qcrypto_init(&error_fatal);

    module_call_init(MODULE_INIT_QOM);
    bdrv_init();
    if (argc < 2) {
        error_exit("Not enough arguments");
    }

    qemu_add_opts(&qemu_object_opts);
    qemu_add_opts(&qemu_source_opts);
    qemu_add_opts(&qemu_trace_opts);

    while ((c = getopt_long(argc, argv, "+:hVT:", long_options, NULL)) != -1) {
        switch (c) {
        case ':':
            missing_argument(argv[optind - 1]);
            return 0;
        case '?':
            unrecognized_option(argv[optind - 1]);
            return 0;
        case 'h':
            help();
            return 0;
        case 'V':
            printf(QEMU_IMG_VERSION);
            return 0;
        case 'T':
            g_free(trace_file);
            trace_file = trace_opt_parse(optarg);
            break;
        }
    }

    cmdname = argv[optind];

    /* reset getopt_long scanning */
    argc -= optind;
    if (argc < 1) {
        return 0;
    }
    argv += optind;
    qemu_reset_optind();

    if (!trace_init_backends()) {
        exit(1);
    }
    trace_init_file(trace_file);
    qemu_set_log(LOG_TRACE);

    /* find the command */
    for (cmd = img_cmds; cmd->name != NULL; cmd++) {
        if (!strcmp(cmdname, cmd->name)) {
            return cmd->handler(argc, argv);
        }
    }

    /* not found */
    error_exit("Command not found: %s", cmdname);
}

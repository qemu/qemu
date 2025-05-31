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

#include "qemu/help-texts.h"
#include "qemu/qemu-progress.h"
#include "qemu-version.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-block-core.h"
#include "qapi/qapi-visit-block-core.h"
#include "qapi/qobject-output-visitor.h"
#include "qobject/qjson.h"
#include "qobject/qdict.h"
#include "qemu/cutils.h"
#include "qemu/config-file.h"
#include "qemu/option.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/sockets.h"
#include "qemu/units.h"
#include "qemu/memalign.h"
#include "qom/object_interfaces.h"
#include "system/block-backend.h"
#include "block/block_int.h"
#include "block/blockjob.h"
#include "block/dirty-bitmap.h"
#include "block/qapi.h"
#include "crypto/init.h"
#include "trace/control.h"
#include "qemu/throttle.h"
#include "block/throttle-groups.h"

#define QEMU_IMG_VERSION "qemu-img version " QEMU_FULL_VERSION \
                          "\n" QEMU_COPYRIGHT "\n"

typedef struct img_cmd_t {
    const char *name;
    int (*handler)(const struct img_cmd_t *ccmd, int argc, char **argv);
    const char *description;
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
    OPTION_SKIP_BROKEN = 277,
};

typedef enum OutputFormat {
    OFORMAT_JSON,
    OFORMAT_HUMAN,
} OutputFormat;

/* Default to cache=writeback as data integrity is not important for qemu-img */
#define BDRV_DEFAULT_CACHE "writeback"

static G_NORETURN
void tryhelp(const char *argv0)
{
    error_printf("Try '%s --help' for more information\n", argv0);
    exit(EXIT_FAILURE);
}

static G_NORETURN G_GNUC_PRINTF(2, 3)
void error_exit(const char *argv0, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    error_vreport(fmt, ap);
    va_end(ap);

    tryhelp(argv0);
}

/*
 * Print --help output for a command and exit.
 * @syntax and @description are multi-line with trailing EOL
 * (to allow easy extending of the text)
 * @syntax has each subsequent line indented by 8 chars.
 * @description is indented by 2 chars for argument on each own line,
 * and with 5 chars for argument description (like -h arg below).
 */
static G_NORETURN
void cmd_help(const img_cmd_t *ccmd,
              const char *syntax, const char *arguments)
{
    printf(
"Usage:\n"
"  %s %s %s\n"
"%s.\n"
"\n"
"Arguments:\n"
"  -h, --help\n"
"     print this help and exit\n"
"%s\n",
           "qemu-img", ccmd->name, syntax, ccmd->description, arguments);
    exit(EXIT_SUCCESS);
}

static OutputFormat parse_output_format(const char *argv0, const char *arg)
{
    if (!strcmp(arg, "json")) {
        return OFORMAT_JSON;
    } else if (!strcmp(arg, "human")) {
        return OFORMAT_HUMAN;
    } else {
        error_exit(argv0, "--output expects 'human' or 'json', not '%s'", arg);
    }
}

/*
 * Is @list safe for accumulate_options()?
 * It is when multiple of them can be joined together separated by ','.
 * To make that work, @list must not start with ',' (or else a
 * separating ',' preceding it gets escaped), and it must not end with
 * an odd number of ',' (or else a separating ',' following it gets
 * escaped), or be empty (or else a separating ',' preceding it can
 * escape a separating ',' following it).
 * 
 */
static bool is_valid_option_list(const char *list)
{
    size_t len = strlen(list);
    size_t i;

    if (!list[0] || list[0] == ',') {
        return false;
    }

    for (i = len; i > 0 && list[i - 1] == ','; i--) {
    }
    if ((len - i) % 2) {
        return false;
    }

    return true;
}

static int accumulate_options(char **options, char *list)
{
    char *new_options;

    if (!is_valid_option_list(list)) {
        error_report("Invalid option list: %s", list);
        return -1;
    }

    if (!*options) {
        *options = g_strdup(list);
    } else {
        new_options = g_strdup_printf("%s,%s", *options, list);
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

static int G_GNUC_PRINTF(2, 3) qprintf(bool quiet, const char *fmt, ...)
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

    if (blk) {
        blk_set_force_allow_inactivate(blk);
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

static int64_t cvtnum_full(const char *name, const char *value,
                           bool is_size, int64_t min, int64_t max)
{
    int err;
    uint64_t res;

    err = is_size ? qemu_strtosz(value, NULL, &res) :
                    qemu_strtou64(value, NULL, 0, &res);
    if (err < 0 && err != -ERANGE) {
        error_report("Invalid %s specified: '%s'", name, value);
        return err;
    }
    if (err == -ERANGE || res > max || res < min) {
        error_report("Invalid %s specified. Must be between %" PRId64
                     " and %" PRId64 ".", name, min, max);
        return -ERANGE;
    }
    return res;
}

static int64_t cvtnum(const char *name, const char *value, bool is_size)
{
    return cvtnum_full(name, value, is_size, 0, INT64_MAX);
}

static int img_create(const img_cmd_t *ccmd, int argc, char **argv)
{
    int c;
    int64_t img_size = -1;
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
            {"format", required_argument, 0, 'f'},
            {"options", required_argument, 0, 'o'},
            {"backing", required_argument, 0, 'b'},
            {"backing-format", required_argument, 0, 'B'}, /* was -F in 10.0 */
            {"backing-unsafe", no_argument, 0, 'u'},
            {"quiet", no_argument, 0, 'q'},
            {"object", required_argument, 0, OPTION_OBJECT},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, "hf:o:b:F:B:uq",
                        long_options, NULL);
        if (c == -1) {
            break;
        }
        switch(c) {
        case 'h':
            cmd_help(ccmd, "[-f FMT] [-o FMT_OPTS]\n"
"        [-b BACKING_FILE [-B BACKING_FMT]] [-u]\n"
"        [-q] [--object OBJDEF] FILE [SIZE]\n"
,
"  -f, --format FMT\n"
"     specifies the format of the new image (default: raw)\n"
"  -o, --options FMT_OPTS\n"
"     format-specific options (specify '-o help' for help)\n"
"  -b, --backing BACKING_FILE\n"
"     create target image to be a CoW on top of BACKING_FILE\n"
"  -B, --backing-format BACKING_FMT (was -F in <= 10.0)\n"
"     specifies the format of BACKING_FILE (default: probing is used)\n"
"  -u, --backing-unsafe\n"
"     do not fail if BACKING_FILE can not be read\n"
"  -q, --quiet\n"
"     quiet mode (produce only error messages if any)\n"
"  --object OBJDEF\n"
"     defines QEMU user-creatable object\n"
"  FILE\n"
"     name of the image file to create (will be overritten if already exists)\n"
"  SIZE[bKMGTPE]\n"
"     image size with optional multiplier suffix (powers of 1024)\n"
"     (required unless BACKING_FILE is specified)\n"
);
            break;
        case 'f':
            fmt = optarg;
            break;
        case 'o':
            if (accumulate_options(&options, optarg) < 0) {
                goto fail;
            }
            break;
        case 'b':
            base_filename = optarg;
            break;
        case 'F': /* <=10.0 */
        case 'B':
            base_fmt = optarg;
            break;
        case 'u':
            flags |= BDRV_O_NO_BACKING;
            break;
        case 'q':
            quiet = true;
            break;
        case OPTION_OBJECT:
            user_creatable_process_cmdline(optarg);
            break;
        default:
            tryhelp(argv[0]);
        }
    }

    /* Get the filename */
    filename = (optind < argc) ? argv[optind] : NULL;
    if (options && has_help_option(options)) {
        g_free(options);
        return print_block_option_help(filename, fmt);
    }

    if (optind >= argc) {
        error_exit(argv[0], "Expecting image file name");
    }
    optind++;

    /* Get image size, if specified */
    if (optind < argc) {
        img_size = cvtnum("image size", argv[optind++], true);
        if (img_size < 0) {
            goto fail;
        }
    }
    if (optind != argc) {
        error_exit(argv[0], "Unexpected argument: %s", argv[optind]);
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
    GString *str;
    QObject *obj;
    Visitor *v = qobject_output_visitor_new(&obj);

    visit_type_ImageCheck(v, NULL, &check, &error_abort);
    visit_complete(v, &obj);
    str = qobject_to_json_pretty(obj, true);
    assert(str != NULL);
    qprintf(quiet, "%s\n", str->str);
    qobject_unref(obj);
    visit_free(v);
    g_string_free(str, true);
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
static int img_check(const img_cmd_t *ccmd, int argc, char **argv)
{
    int c, ret;
    OutputFormat output_format = OFORMAT_HUMAN;
    const char *filename, *fmt, *cache;
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
    cache = BDRV_DEFAULT_CACHE;

    for(;;) {
        int option_index = 0;
        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"format", required_argument, 0, 'f'},
            {"image-opts", no_argument, 0, OPTION_IMAGE_OPTS},
            {"cache", required_argument, 0, 'T'},
            {"repair", required_argument, 0, 'r'},
            {"force-share", no_argument, 0, 'U'},
            {"output", required_argument, 0, OPTION_OUTPUT},
            {"quiet", no_argument, 0, 'q'},
            {"object", required_argument, 0, OPTION_OBJECT},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, "hf:T:r:Uq",
                        long_options, &option_index);
        if (c == -1) {
            break;
        }
        switch(c) {
        case 'h':
            cmd_help(ccmd, "[-f FMT | --image-opts] [-T CACHE_MODE] [-r leaks|all]\n"
"        [-U] [--output human|json] [-q] [--object OBJDEF] FILE\n"
,
"  -f, --format FMT\n"
"     specifies the format of the image explicitly (default: probing is used)\n"
"  --image-opts\n"
"     treat FILE as an option string (key=value,..), not a file name\n"
"     (incompatible with -f|--format)\n"
"  -T, --cache CACHE_MODE\n" /* why not -t ? */
"     cache mode (default: " BDRV_DEFAULT_CACHE ")\n"
"  -r, --repair leaks|all\n"
"     repair errors of the given category in the image (image will be\n"
"     opened in read-write mode, incompatible with -U|--force-share)\n"
"  -U, --force-share\n"
"     open image in shared mode for concurrent access\n"
"  --output human|json\n"
"     output format (default: human)\n"
"  -q, --quiet\n"
"     quiet mode (produce only error messages if any)\n"
"  --object OBJDEF\n"
"     defines QEMU user-creatable object\n"
"  FILE\n"
"     name of the image file, or an option string (key=value,..)\n"
"     with --image-opts, to operate on\n"
);
            break;
        case 'f':
            fmt = optarg;
            break;
        case OPTION_IMAGE_OPTS:
            image_opts = true;
            break;
        case 'T':
            cache = optarg;
            break;
        case 'r':
            flags |= BDRV_O_RDWR;

            if (!strcmp(optarg, "leaks")) {
                fix = BDRV_FIX_LEAKS;
            } else if (!strcmp(optarg, "all")) {
                fix = BDRV_FIX_LEAKS | BDRV_FIX_ERRORS;
            } else {
                error_exit(argv[0],
                           "--repair (-r) expects 'leaks' or 'all', not '%s'",
                           optarg);
            }
            break;
        case 'U':
            force_share = true;
            break;
        case OPTION_OUTPUT:
            output_format = parse_output_format(argv[0], optarg);
            break;
        case 'q':
            quiet = true;
            break;
        case OPTION_OBJECT:
            user_creatable_process_cmdline(optarg);
            break;
        default:
            tryhelp(argv[0]);
        }
    }
    if (optind != argc - 1) {
        error_exit(argv[0], "Expecting one image file name");
    }
    filename = argv[optind++];

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
    uint64_t progress_current, progress_total;
    AioContext *aio_context = block_job_get_aio_context(job);
    int ret = 0;

    job_lock();
    job_ref_locked(&job->job);
    do {
        float progress = 0.0f;
        job_unlock();
        aio_poll(aio_context, true);

        progress_get_snapshot(&job->job.progress, &progress_current,
                              &progress_total);
        if (progress_total) {
            progress = (float)progress_current / progress_total * 100.f;
        }
        qemu_progress_print(progress, 0);
        job_lock();
    } while (!job_is_ready_locked(&job->job) &&
             !job_is_completed_locked(&job->job));

    if (!job_is_completed_locked(&job->job)) {
        ret = job_complete_sync_locked(&job->job, errp);
    } else {
        ret = job->job.ret;
    }
    job_unref_locked(&job->job);
    job_unlock();

    /* publish completion progress only when success */
    if (!ret) {
        qemu_progress_print(100.f, 0);
    }
}

static int img_commit(const img_cmd_t *ccmd, int argc, char **argv)
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
    int64_t rate_limit = 0;

    fmt = NULL;
    cache = BDRV_DEFAULT_CACHE;
    base = NULL;
    for(;;) {
        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"format", required_argument, 0, 'f'},
            {"image-opts", no_argument, 0, OPTION_IMAGE_OPTS},
            {"cache", required_argument, 0, 't'},
            {"drop", no_argument, 0, 'd'},
            {"base", required_argument, 0, 'b'},
            {"rate-limit", required_argument, 0, 'r'},
            {"progress", no_argument, 0, 'p'},
            {"quiet", no_argument, 0, 'q'},
            {"object", required_argument, 0, OPTION_OBJECT},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, "hf:t:db:r:pq",
                        long_options, NULL);
        if (c == -1) {
            break;
        }
        switch(c) {
        case 'h':
            cmd_help(ccmd, "[-f FMT | --image-opts] [-t CACHE_MODE] [-b BASE_IMG]\n"
"        [-d] [-r RATE] [-q] [--object OBJDEF] FILE\n"
,
"  -f, --format FMT\n"
"     specify FILE image format explicitly (default: probing is used)\n"
"  --image-opts\n"
"     treat FILE as an option string (key=value,..), not a file name\n"
"     (incompatible with -f|--format)\n"
"  -t, --cache CACHE_MODE image cache mode (default: " BDRV_DEFAULT_CACHE ")\n"
"  -d, --drop\n"
"     skip emptying FILE on completion\n"
"  -b, --base BASE_IMG\n"
"     image in the backing chain to commit change to\n"
"     (default: immediate backing file; implies --drop)\n"
"  -r, --rate-limit RATE\n"
"     I/O rate limit, in bytes per second\n"
"  -p, --progress\n"
"     display progress information\n"
"  -q, --quiet\n"
"     quiet mode (produce only error messages if any)\n"
"  --object OBJDEF\n"
"     defines QEMU user-creatable object\n"
"  FILE\n"
"     name of the image file, or an option string (key=value,..)\n"
"     with --image-opts, to operate on\n"
);
            break;
        case 'f':
            fmt = optarg;
            break;
        case OPTION_IMAGE_OPTS:
            image_opts = true;
            break;
        case 't':
            cache = optarg;
            break;
        case 'd':
            drop = true;
            break;
        case 'b':
            base = optarg;
            /* -b implies -d */
            drop = true;
            break;
        case 'r':
            rate_limit = cvtnum("rate limit", optarg, true);
            if (rate_limit < 0) {
                return 1;
            }
            break;
        case 'p':
            progress = true;
            break;
        case 'q':
            quiet = true;
            break;
        case OPTION_OBJECT:
            user_creatable_process_cmdline(optarg);
            break;
        default:
            tryhelp(argv[0]);
        }
    }

    /* Progress is not shown in Quiet mode */
    if (quiet) {
        progress = false;
    }

    if (optind != argc - 1) {
        error_exit(argv[0], "Expecting one image file name");
    }
    filename = argv[optind++];

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

    bdrv_graph_rdlock_main_loop();
    if (base) {
        base_bs = bdrv_find_backing_image(bs, base);
        if (!base_bs) {
            error_setg(&local_err,
                       "Did not find '%s' in the backing chain of '%s'",
                       base, filename);
            bdrv_graph_rdunlock_main_loop();
            goto done;
        }
    } else {
        /* This is different from QMP, which by default uses the deepest file in
         * the backing chain (i.e., the very base); however, the traditional
         * behavior of qemu-img commit is using the immediate backing file. */
        base_bs = bdrv_backing_chain_next(bs);
        if (!base_bs) {
            error_setg(&local_err, "Image does not have a backing file");
            bdrv_graph_rdunlock_main_loop();
            goto done;
        }
    }
    bdrv_graph_rdunlock_main_loop();

    cbi = (CommonBlockJobCBInfo){
        .errp = &local_err,
        .bs   = bs,
    };

    commit_active_start("commit", bs, base_bs, JOB_DEFAULT, rate_limit,
                        BLOCKDEV_ON_ERROR_REPORT, NULL, common_block_job_cb,
                        &cbi, false, &local_err);
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

    /*
     * Manually inactivate the image first because this way we can know whether
     * an error occurred. blk_unref() doesn't tell us about failures.
     */
    ret = bdrv_inactivate_all();
    if (ret < 0 && !local_err) {
        error_setg_errno(&local_err, -ret, "Error while closing the image");
    }
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
 * that the request will at least end aligned and consecutive requests will
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
    is_zero = buffer_is_zero(buf, BDRV_SECTOR_SIZE);
    for(i = 1; i < n; i++) {
        buf += BDRV_SECTOR_SIZE;
        if (is_zero != buffer_is_zero(buf, BDRV_SECTOR_SIZE)) {
            break;
        }
    }

    if (i == n) {
        /*
         * The whole buf is the same.
         * No reason to split it into chunks, so return now.
         */
        *pnum = i;
        return !is_zero;
    }

    tail = (sector_num + i) & (alignment - 1);
    if (tail) {
        if (is_zero && i <= tail) {
            /*
             * For sure next sector after i is data, and it will rewrite this
             * tail anyway due to RMW. So, let's just write data now.
             */
            is_zero = false;
        }
        if (!is_zero) {
            /* If possible, align up end offset of allocated areas. */
            i += alignment - tail;
            i = MIN(i, n);
        } else {
            /*
             * For sure next sector after i is data, and it will rewrite this
             * tail anyway due to RMW. Better is avoid RMW and write zeroes up
             * to aligned bound.
             */
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
 * Compares two buffers chunk by chunk, where @chsize is the chunk size.
 * If @chsize is 0, default chunk size of BDRV_SECTOR_SIZE is used.
 * Returns 0 if the first chunk of each buffer matches, non-zero otherwise.
 *
 * @pnum is set to the size of the buffer prefix aligned to @chsize that
 * has the same matching status as the first chunk.
 */
static int compare_buffers(const uint8_t *buf1, const uint8_t *buf2,
                           int64_t bytes, uint64_t chsize, int64_t *pnum)
{
    bool res;
    int64_t i;

    assert(bytes > 0);

    if (!chsize) {
        chsize = BDRV_SECTOR_SIZE;
    }
    i = MIN(bytes, chsize);

    res = !!memcmp(buf1, buf2, i);
    while (i < bytes) {
        int64_t len = MIN(bytes - i, chsize);

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

    ret = blk_pread(blk, offset, bytes, buffer, 0);
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
 * 0 - Images are identical or the requested help was printed
 * 1 - Images differ
 * >1 - Error occurred
 */
static int img_compare(const img_cmd_t *ccmd, int argc, char **argv)
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
            {"a-format", required_argument, 0, 'f'},
            {"b-format", required_argument, 0, 'F'},
            {"image-opts", no_argument, 0, OPTION_IMAGE_OPTS},
            {"strict", no_argument, 0, 's'},
            {"cache", required_argument, 0, 'T'},
            {"force-share", no_argument, 0, 'U'},
            {"progress", no_argument, 0, 'p'},
            {"quiet", no_argument, 0, 'q'},
            {"object", required_argument, 0, OPTION_OBJECT},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, "hf:F:sT:Upq",
                        long_options, NULL);
        if (c == -1) {
            break;
        }
        switch (c) {
        case 'h':
            cmd_help(ccmd,
"[[-f FMT] [-F FMT] | --image-opts] [-s] [-T CACHE]\n"
"        [-U] [-p] [-q] [--object OBJDEF] FILE1 FILE2\n"
,
"  -f, --a-format FMT\n"
"     specify FILE1 image format explicitly (default: probing is used)\n"
"  -F, --b-format FMT\n"
"     specify FILE2 image format explicitly (default: probing is used)\n"
"  --image-opts\n"
"     treat FILE1 and FILE2 as option strings (key=value,..), not file names\n"
"     (incompatible with -f|--a-format and -F|--b-format)\n"
"  -s, --strict\n"
"     strict mode, also check if sizes are equal\n"
"  -T, --cache CACHE_MODE\n"
"     images caching mode (default: " BDRV_DEFAULT_CACHE ")\n"
"  -U, --force-share\n"
"     open images in shared mode for concurrent access\n"
"  -p, --progress\n"
"     display progress information\n"
"  -q, --quiet\n"
"     quiet mode (produce only error messages if any)\n"
"  --object OBJDEF\n"
"     defines QEMU user-creatable object\n"
"  FILE1, FILE2\n"
"     names of the image files, or option strings (key=value,..)\n"
"     with --image-opts, to compare\n"
);
            break;
        case 'f':
            fmt1 = optarg;
            break;
        case 'F':
            fmt2 = optarg;
            break;
        case OPTION_IMAGE_OPTS:
            image_opts = true;
            break;
        case 's':
            strict = true;
            break;
        case 'T':
            cache = optarg;
            break;
        case 'U':
            force_share = true;
            break;
        case 'p':
            progress = true;
            break;
        case 'q':
            quiet = true;
            break;
        case OPTION_OBJECT:
            user_creatable_process_cmdline(optarg);
            break;
        default:
            tryhelp(argv[0]);
        }
    }

    /* Progress is not shown in Quiet mode */
    if (quiet) {
        progress = false;
    }


    if (optind != argc - 2) {
        error_exit(argv[0], "Expecting two image file names");
    }
    filename1 = argv[optind++];
    filename2 = argv[optind++];

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
                ret = blk_pread(blk1, offset, chunk, buf1, 0);
                if (ret < 0) {
                    error_report("Error while reading offset %" PRId64
                                 " of %s: %s",
                                 offset, filename1, strerror(-ret));
                    ret = 4;
                    goto out;
                }
                ret = blk_pread(blk2, offset, chunk, buf2, 0);
                if (ret < 0) {
                    error_report("Error while reading offset %" PRId64
                                 " of %s: %s",
                                 offset, filename2, strerror(-ret));
                    ret = 4;
                    goto out;
                }
                ret = compare_buffers(buf1, buf2, chunk, 0, &pnum);
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
    return ret;
}

/* Convenience wrapper around qmp_block_dirty_bitmap_merge */
static void do_dirty_bitmap_merge(const char *dst_node, const char *dst_name,
                                  const char *src_node, const char *src_name,
                                  Error **errp)
{
    BlockDirtyBitmapOrStr *merge_src;
    BlockDirtyBitmapOrStrList *list = NULL;

    merge_src = g_new0(BlockDirtyBitmapOrStr, 1);
    merge_src->type = QTYPE_QDICT;
    merge_src->u.external.node = g_strdup(src_node);
    merge_src->u.external.name = g_strdup(src_name);
    QAPI_LIST_PREPEND(list, merge_src);
    qmp_block_dirty_bitmap_merge(dst_node, dst_name, list, errp);
    qapi_free_BlockDirtyBitmapOrStrList(list);
}

enum ImgConvertBlockStatus {
    BLK_DATA,
    BLK_ZERO,
    BLK_BACKING_FILE,
};

#define MAX_COROUTINES 16
#define CONVERT_THROTTLE_GROUP "img_convert"

typedef struct ImgConvertState {
    BlockBackend **src;
    int64_t *src_sectors;
    int *src_alignment;
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

static int coroutine_mixed_fn GRAPH_RDLOCK
convert_iteration_sectors(ImgConvertState *s, int64_t sector_num)
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
        int tail;
        BlockDriverState *src_bs = blk_bs(s->src[src_cur]);
        BlockDriverState *base;

        if (s->target_has_backing) {
            base = bdrv_cow_bs(bdrv_skip_filters(src_bs));
        } else {
            base = NULL;
        }

        do {
            count = n * BDRV_SECTOR_SIZE;

            ret = bdrv_block_status_above(src_bs, base, offset, count, &count,
                                          NULL, NULL);

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

        /*
         * Avoid that s->sector_next_status becomes unaligned to the source
         * request alignment and/or cluster size to avoid unnecessary read
         * cycles.
         */
        tail = (sector_num - src_cur_offset + n) % s->src_alignment[src_cur];
        if (n > tail) {
            n -= tail;
        }

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
        WITH_GRAPH_RDLOCK_GUARD() {
            n = convert_iteration_sectors(s, s->sector_num);
        }
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
                WITH_GRAPH_RDLOCK_GUARD() {
                    ret = convert_co_copy_range(s, sector_num, n);
                }
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
        bdrv_graph_rdlock_main_loop();
        s->has_zero_init = bdrv_has_zero_init(blk_bs(s->target));
        bdrv_graph_rdunlock_main_loop();
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
        bdrv_graph_rdlock_main_loop();
        n = convert_iteration_sectors(s, sector_num);
        bdrv_graph_rdunlock_main_loop();
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
        ret = blk_pwrite_compressed(s->target, 0, 0, NULL);
        if (ret < 0) {
            return ret;
        }
    }

    return s->ret;
}

/* Check that bitmaps can be copied, or output an error */
static int convert_check_bitmaps(BlockDriverState *src, bool skip_broken)
{
    BdrvDirtyBitmap *bm;

    if (!bdrv_supports_persistent_dirty_bitmap(src)) {
        error_report("Source lacks bitmap support");
        return -1;
    }
    FOR_EACH_DIRTY_BITMAP(src, bm) {
        if (!bdrv_dirty_bitmap_get_persistence(bm)) {
            continue;
        }
        if (!skip_broken && bdrv_dirty_bitmap_inconsistent(bm)) {
            error_report("Cannot copy inconsistent bitmap '%s'",
                         bdrv_dirty_bitmap_name(bm));
            error_printf("Try --skip-broken-bitmaps, or "
                         "use 'qemu-img bitmap --remove' to delete it\n");
            return -1;
        }
    }
    return 0;
}

static int convert_copy_bitmaps(BlockDriverState *src, BlockDriverState *dst,
                                bool skip_broken)
{
    BdrvDirtyBitmap *bm;
    Error *err = NULL;

    FOR_EACH_DIRTY_BITMAP(src, bm) {
        const char *name;

        if (!bdrv_dirty_bitmap_get_persistence(bm)) {
            continue;
        }
        name = bdrv_dirty_bitmap_name(bm);
        if (skip_broken && bdrv_dirty_bitmap_inconsistent(bm)) {
            warn_report("Skipping inconsistent bitmap '%s'", name);
            continue;
        }
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
            qmp_block_dirty_bitmap_remove(dst->node_name, name, NULL);
            return -1;
        }
    }

    return 0;
}

#define MAX_BUF_SECTORS 32768

static void set_rate_limit(BlockBackend *blk, int64_t rate_limit)
{
    ThrottleConfig cfg;

    throttle_config_init(&cfg);
    cfg.buckets[THROTTLE_BPS_WRITE].avg = rate_limit;

    blk_io_limits_enable(blk, CONVERT_THROTTLE_GROUP);
    blk_set_io_limits(blk, &cfg);
}

static int img_convert(const img_cmd_t *ccmd, int argc, char **argv)
{
    int c, bs_i, flags, src_flags = BDRV_O_NO_SHARE;
    const char *fmt = NULL, *out_fmt = NULL, *cache = "unsafe",
               *src_cache = BDRV_DEFAULT_CACHE, *out_baseimg = NULL,
               *out_filename, *out_baseimg_param, *snapshot_name = NULL,
               *backing_fmt = NULL;
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
    bool skip_broken = false;
    int64_t rate_limit = 0;

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
            {"source-format", required_argument, 0, 'f'},
            /*
             * XXX: historic --image-opts acts on source file only,
             * it seems better to have it affect both source and target,
             * and have separate --source-image-opts for source,
             * but this might break existing setups.
             */
            {"image-opts", no_argument, 0, OPTION_IMAGE_OPTS},
            {"source-cache", required_argument, 0, 'T'},
            {"snapshot", required_argument, 0, 'l'},
            {"bitmaps", no_argument, 0, OPTION_BITMAPS},
            {"skip-broken-bitmaps", no_argument, 0, OPTION_SKIP_BROKEN},
            {"salvage", no_argument, 0, OPTION_SALVAGE},
            {"target-format", required_argument, 0, 'O'},
            {"target-image-opts", no_argument, 0, OPTION_TARGET_IMAGE_OPTS},
            {"target-format-options", required_argument, 0, 'o'},
            {"target-cache", required_argument, 0, 't'},
            {"backing", required_argument, 0, 'b'},
            {"backing-format", required_argument, 0, 'F'},
            {"sparse-size", required_argument, 0, 'S'},
            {"no-create", no_argument, 0, 'n'},
            {"target-is-zero", no_argument, 0, OPTION_TARGET_IS_ZERO},
            {"force-share", no_argument, 0, 'U'},
            {"rate-limit", required_argument, 0, 'r'},
            {"parallel", required_argument, 0, 'm'},
            {"oob-writes", no_argument, 0, 'W'},
            {"copy-range-offloading", no_argument, 0, 'C'},
            {"progress", no_argument, 0, 'p'},
            {"quiet", no_argument, 0, 'q'},
            {"object", required_argument, 0, OPTION_OBJECT},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, "hf:O:b:B:CcF:o:l:S:pt:T:nm:WUr:q",
                        long_options, NULL);
        if (c == -1) {
            break;
        }
        switch (c) {
        case 'h':
            cmd_help(ccmd, "[-f SRC_FMT | --image-opts] [-T SRC_CACHE]\n"
"        [-l SNAPSHOT] [--bitmaps [--skip-broken-bitmaps]] [--salvage]\n"
"        [-O TGT_FMT | --target-image-opts] [-o TGT_FMT_OPTS] [-t TGT_CACHE]\n"
"        [-b BACKING_FILE [-F BACKING_FMT]] [-S SPARSE_SIZE]\n"
"        [-n] [--target-is-zero] [-c]\n"
"        [-U] [-r RATE] [-m NUM_PARALLEL] [-W] [-C] [-p] [-q] [--object OBJDEF]\n"
"        SRC_FILE [SRC_FILE2...] TGT_FILE\n"
,
"  -f, --source-format SRC_FMT\n"
"     specify format of all SRC_FILEs explicitly (default: probing is used)\n"
"  --image-opts\n"
"     treat each SRC_FILE as an option string (key=value,...), not a file name\n"
"     (incompatible with -f|--source-format)\n"
"  -T, --source-cache SRC_CACHE\n"
"     source image(s) cache mode (" BDRV_DEFAULT_CACHE ")\n"
"  -l, --snapshot SNAPSHOT\n"
"     specify source snapshot\n"
"  --bitmaps\n"
"     also copy any persistent bitmaps present in source\n"
"  --skip-broken-bitmaps\n"
"     skip (do not error out) any broken bitmaps\n"
"  --salvage\n"
"     ignore errors on input (convert unreadable areas to zeros)\n"
"  -O, --target-format TGT_FMT\n"
"     specify TGT_FILE image format (default: raw)\n"
"  --target-image-opts\n"
"     treat TGT_FILE as an option string (key=value,...), not a file name\n"
"     (incompatible with -O|--target-format)\n"
"  -o, --target-format-options TGT_FMT_OPTS\n"
"     TGT_FMT-specific options\n"
"  -t, --target-cache TGT_CACHE\n"
"     cache mode when opening output image (default: unsafe)\n"
"  -b, --backing BACKING_FILE (was -B in <= 10.0)\n"
"     create target image to be a CoW on top of BACKING_FILE\n"
"  -F, --backing-format BACKING_FMT\n" /* -B used for -b in <=10.0 */
"     specify BACKING_FILE image format explicitly (default: probing is used)\n"
"  -S, --sparse-size SPARSE_SIZE[bkKMGTPE]\n"
"     specify number of consecutive zero bytes to treat as a gap on output\n"
"     (rounded down to nearest 512 bytes), with optional multiplier suffix\n"
"  -n, --no-create\n"
"     omit target volume creation (e.g. on rbd)\n"
"  --target-is-zero\n"
"     indicates that the target volume is pre-zeroed\n"
"  -c, --compress\n"
"     create compressed output image (qcow and qcow2 formats only)\n"
"  -U, --force-share\n"
"     open images in shared mode for concurrent access\n"
"  -r, --rate-limit RATE\n"
"     I/O rate limit, in bytes per second\n"
"  -m, --parallel NUM_PARALLEL\n"
"     specify parallelism (default: 8)\n"
"  -C, --copy-range-offloading\n"
"     try to use copy offloading\n"
"  -W, --oob-writes\n"
"     enable out-of-order writes to improve performance\n"
"  -p, --progress\n"
"     display progress information\n"
"  -q, --quiet\n"
"     quiet mode (produce only error messages if any)\n"
"  --object OBJDEF\n"
"     defines QEMU user-creatable object\n"
"  SRC_FILE...\n"
"     one or more source image file names,\n"
"     or option strings (key=value,..) with --source-image-opts\n"
"  TGT_FILE\n"
"     target (output) image file name,\n"
"     or option string (key=value,..) with --target-image-opts\n"
);
            break;
        case 'f':
            fmt = optarg;
            break;
        case OPTION_IMAGE_OPTS:
            image_opts = true;
            break;
        case 'T':
            src_cache = optarg;
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
        case OPTION_BITMAPS:
            bitmaps = true;
            break;
        case OPTION_SKIP_BROKEN:
            skip_broken = true;
            break;
        case OPTION_SALVAGE:
            s.salvage = true;
            break;
         case 'O':
            out_fmt = optarg;
            break;
        case OPTION_TARGET_IMAGE_OPTS:
            tgt_image_opts = true;
            break;
        case 'o':
            if (accumulate_options(&options, optarg) < 0) {
                goto fail_getopt;
            }
            break;
        case 't':
            cache = optarg;
            break;
        case 'B': /* <=10.0 */
        case 'b':
            out_baseimg = optarg;
            break;
        case 'F': /* can't use -B as it used as -b in <=10.0 */
            backing_fmt = optarg;
            break;
        case 'S':
        {
            int64_t sval;

            sval = cvtnum("buffer size for sparse output", optarg, true);
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
        case 'n':
            skip_create = true;
            break;
        case OPTION_TARGET_IS_ZERO:
            /*
             * The user asserting that the target is blank has the
             * same effect as the target driver supporting zero
             * initialisation.
             */
            s.has_zero_init = true;
            break;
        case 'c':
            s.compressed = true;
            break;
        case 'U':
            force_share = true;
            break;
        case 'r':
            rate_limit = cvtnum("rate limit", optarg, true);
            if (rate_limit < 0) {
                goto fail_getopt;
            }
            break;
        case 'm':
            s.num_coroutines = cvtnum_full("number of coroutines", optarg,
                                           false, 1, MAX_COROUTINES);
            if (s.num_coroutines < 0) {
                goto fail_getopt;
            }
            break;
        case 'W':
            s.wr_in_order = false;
            break;
        case 'C':
            s.copy_range = true;
            break;
        case 'p':
            progress = true;
            break;
        case 'q':
            s.quiet = true;
            break;
        case OPTION_OBJECT:
            user_creatable_process_cmdline(optarg);
            break;
        default:
            tryhelp(argv[0]);
        }
    }

    if (!out_fmt && !tgt_image_opts) {
        out_fmt = "raw";
    }

    if (skip_broken && !bitmaps) {
        error_report("Use of --skip-broken-bitmaps requires --bitmaps");
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
    s.src_alignment = g_new(int, s.src_num);

    for (bs_i = 0; bs_i < s.src_num; bs_i++) {
        BlockDriverState *src_bs;
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
        src_bs = blk_bs(s.src[bs_i]);
        s.src_alignment[bs_i] = DIV_ROUND_UP(src_bs->bl.request_alignment,
                                             BDRV_SECTOR_SIZE);
        if (!bdrv_get_info(src_bs, &bdi)) {
            s.src_alignment[bs_i] = MAX(s.src_alignment[bs_i],
                                        bdi.cluster_size / BDRV_SECTOR_SIZE);
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

        qemu_opt_set_number(opts, BLOCK_OPT_SIZE,
                            s.total_sectors * BDRV_SECTOR_SIZE, &error_abort);
        ret = add_old_style_options(out_fmt, opts, out_baseimg, backing_fmt);
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
            error_report("Use of backing file requires explicit "
                         "backing format");
            ret = -1;
            goto out;
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
        ret = convert_check_bitmaps(blk_bs(s.src[0]), skip_broken);
        if (ret < 0) {
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

    if (flags & BDRV_O_NOCACHE) {
        /*
         * If we open the target with O_DIRECT, it may be necessary to
         * extend its size to align to the physical sector size.
         */
        flags |= BDRV_O_RESIZE;
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
        bdrv_graph_rdlock_main_loop();
        s.target_backing_sectors =
            bdrv_nb_sectors(bdrv_backing_chain_next(out_bs));
        bdrv_graph_rdunlock_main_loop();
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

    if (rate_limit) {
        set_rate_limit(s.target, rate_limit);
    }

    ret = convert_do_copy(&s);

    /* Now copy the bitmaps */
    if (bitmaps && ret == 0) {
        ret = convert_copy_bitmaps(blk_bs(s.src[0]), out_bs, skip_broken);
    }

out:
    if (!ret) {
        qemu_progress_print(100, 0);
    }
    qemu_progress_end();
    qemu_opts_del(opts);
    qemu_opts_free(create_opts);
    qobject_unref(open_opts);
    blk_unref(s.target);
    if (s.src) {
        for (bs_i = 0; bs_i < s.src_num; bs_i++) {
            blk_unref(s.src[bs_i]);
        }
        g_free(s.src);
    }
    g_free(s.src_sectors);
    g_free(s.src_alignment);
fail_getopt:
    qemu_opts_del(sn_opts);
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

static void dump_json_block_graph_info_list(BlockGraphInfoList *list)
{
    GString *str;
    QObject *obj;
    Visitor *v = qobject_output_visitor_new(&obj);

    visit_type_BlockGraphInfoList(v, NULL, &list, &error_abort);
    visit_complete(v, &obj);
    str = qobject_to_json_pretty(obj, true);
    assert(str != NULL);
    printf("%s\n", str->str);
    qobject_unref(obj);
    visit_free(v);
    g_string_free(str, true);
}

static void dump_json_block_graph_info(BlockGraphInfo *info)
{
    GString *str;
    QObject *obj;
    Visitor *v = qobject_output_visitor_new(&obj);

    visit_type_BlockGraphInfo(v, NULL, &info, &error_abort);
    visit_complete(v, &obj);
    str = qobject_to_json_pretty(obj, true);
    assert(str != NULL);
    printf("%s\n", str->str);
    qobject_unref(obj);
    visit_free(v);
    g_string_free(str, true);
}

static void dump_human_image_info(BlockGraphInfo *info, int indentation,
                                  const char *path)
{
    BlockChildInfoList *children_list;

    bdrv_node_info_dump(qapi_BlockGraphInfo_base(info), indentation,
                        info->children == NULL);

    for (children_list = info->children; children_list;
         children_list = children_list->next)
    {
        BlockChildInfo *child = children_list->value;
        g_autofree char *child_path = NULL;

        printf("%*sChild node '%s%s':\n",
               indentation * 4, "", path, child->name);
        child_path = g_strdup_printf("%s%s/", path, child->name);
        dump_human_image_info(child->info, indentation + 1, child_path);
    }
}

static void dump_human_image_info_list(BlockGraphInfoList *list)
{
    BlockGraphInfoList *elem;
    bool delim = false;

    for (elem = list; elem; elem = elem->next) {
        if (delim) {
            printf("\n");
        }
        delim = true;

        dump_human_image_info(elem->value, 0, "/");
    }
}

static gboolean str_equal_func(gconstpointer a, gconstpointer b)
{
    return strcmp(a, b) == 0;
}

/**
 * Open an image file chain and return an BlockGraphInfoList
 *
 * @filename: topmost image filename
 * @fmt: topmost image format (may be NULL to autodetect)
 * @chain: true  - enumerate entire backing file chain
 *         false - only topmost image file
 *
 * Returns a list of BlockNodeInfo objects or NULL if there was an error
 * opening an image file.  If there was an error a message will have been
 * printed to stderr.
 */
static BlockGraphInfoList *collect_image_info_list(bool image_opts,
                                                   const char *filename,
                                                   const char *fmt,
                                                   bool chain, bool force_share)
{
    BlockGraphInfoList *head = NULL;
    BlockGraphInfoList **tail = &head;
    GHashTable *filenames;
    Error *err = NULL;

    filenames = g_hash_table_new_full(g_str_hash, str_equal_func, NULL, NULL);

    while (filename) {
        BlockBackend *blk;
        BlockDriverState *bs;
        BlockGraphInfo *info;

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

        /*
         * Note that the returned BlockGraphInfo object will not have
         * information about this image's backing node, because we have opened
         * it with BDRV_O_NO_BACKING.  Printing this object will therefore not
         * duplicate the backing chain information that we obtain by walking
         * the chain manually here.
         */
        bdrv_graph_rdlock_main_loop();
        bdrv_query_block_graph_info(bs, &info, &err);
        bdrv_graph_rdunlock_main_loop();

        if (err) {
            error_report_err(err);
            blk_unref(blk);
            goto err;
        }

        QAPI_LIST_APPEND(tail, info);

        blk_unref(blk);

        /* Clear parameters that only apply to the topmost image */
        filename = fmt = NULL;
        image_opts = false;

        if (chain) {
            if (info->full_backing_filename) {
                filename = info->full_backing_filename;
            } else if (info->backing_filename) {
                error_report("Could not determine absolute backing filename,"
                             " but backing filename '%s' present",
                             info->backing_filename);
                goto err;
            }
            if (info->backing_filename_format) {
                fmt = info->backing_filename_format;
            }
        }
    }
    g_hash_table_destroy(filenames);
    return head;

err:
    qapi_free_BlockGraphInfoList(head);
    g_hash_table_destroy(filenames);
    return NULL;
}

static int img_info(const img_cmd_t *ccmd, int argc, char **argv)
{
    int c;
    OutputFormat output_format = OFORMAT_HUMAN;
    bool chain = false;
    const char *filename, *fmt;
    BlockGraphInfoList *list;
    bool image_opts = false;
    bool force_share = false;

    fmt = NULL;
    for(;;) {
        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"format", required_argument, 0, 'f'},
            {"image-opts", no_argument, 0, OPTION_IMAGE_OPTS},
            {"backing-chain", no_argument, 0, OPTION_BACKING_CHAIN},
            {"force-share", no_argument, 0, 'U'},
            {"output", required_argument, 0, OPTION_OUTPUT},
            {"object", required_argument, 0, OPTION_OBJECT},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, "hf:U", long_options, NULL);
        if (c == -1) {
            break;
        }
        switch(c) {
        case 'h':
            cmd_help(ccmd, "[-f FMT | --image-opts] [--backing-chain] [-U]\n"
"        [--output human|json] [--object OBJDEF] FILE\n"
,
"  -f, --format FMT\n"
"     specify FILE image format explicitly (default: probing is used)\n"
"  --image-opts\n"
"     treat FILE as an option string (key=value,..), not a file name\n"
"     (incompatible with -f|--format)\n"
"  --backing-chain\n"
"     display information about the backing chain for copy-on-write overlays\n"
"  -U, --force-share\n"
"     open image in shared mode for concurrent access\n"
"  --output human|json\n"
"     specify output format (default: human)\n"
"  --object OBJDEF\n"
"     defines QEMU user-creatable object\n"
"  FILE\n"
"     name of the image file, or option string (key=value,..)\n"
"     with --image-opts, to operate on\n"
);
            break;
        case 'f':
            fmt = optarg;
            break;
        case OPTION_IMAGE_OPTS:
            image_opts = true;
            break;
        case OPTION_BACKING_CHAIN:
            chain = true;
            break;
        case 'U':
            force_share = true;
            break;
        case OPTION_OUTPUT:
            output_format = parse_output_format(argv[0], optarg);
            break;
        case OPTION_OBJECT:
            user_creatable_process_cmdline(optarg);
            break;
        default:
            tryhelp(argv[0]);
        }
    }
    if (optind != argc - 1) {
        error_exit(argv[0], "Expecting one image file name");
    }
    filename = argv[optind++];

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
            dump_json_block_graph_info_list(list);
        } else {
            dump_json_block_graph_info(list->value);
        }
        break;
    }

    qapi_free_BlockGraphInfoList(list);
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
                   e->filename ?: "");
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
               " \"depth\": %"PRId64", \"present\": %s, \"zero\": %s,"
               " \"data\": %s, \"compressed\": %s",
               e->start, e->length, e->depth,
               e->present ? "true" : "false",
               e->zero ? "true" : "false",
               e->data ? "true" : "false",
               e->compressed ? "true" : "false");
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

    GLOBAL_STATE_CODE();
    GRAPH_RDLOCK_GUARD_MAINLOOP();

    /* As an optimization, we could cache the current range of unallocated
     * clusters in each file of the chain, and avoid querying the same
     * range repeatedly.
     */

    depth = 0;
    for (;;) {
        bs = bdrv_skip_filters(bs);
        ret = bdrv_block_status(bs, offset, bytes, &bytes, &map, &file);
        if (ret < 0) {
            return ret;
        }
        assert(bytes);
        if (ret & (BDRV_BLOCK_ZERO|BDRV_BLOCK_DATA)) {
            break;
        }
        bs = bdrv_cow_bs(bs);
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
        .compressed = !!(ret & BDRV_BLOCK_COMPRESSED),
        .offset = map,
        .has_offset = has_offset,
        .depth = depth,
        .present = !!(ret & BDRV_BLOCK_ALLOCATED),
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
        curr->compressed != next->compressed ||
        curr->depth != next->depth ||
        curr->present != next->present ||
        !curr->filename != !next->filename ||
        curr->has_offset != next->has_offset) {
        return false;
    }
    if (curr->filename && strcmp(curr->filename, next->filename)) {
        return false;
    }
    if (curr->has_offset && curr->offset + curr->length != next->offset) {
        return false;
    }
    return true;
}

static int img_map(const img_cmd_t *ccmd, int argc, char **argv)
{
    int c;
    OutputFormat output_format = OFORMAT_HUMAN;
    BlockBackend *blk;
    BlockDriverState *bs;
    const char *filename, *fmt;
    int64_t length;
    MapEntry curr = { .length = 0 }, next;
    int ret = 0;
    bool image_opts = false;
    bool force_share = false;
    int64_t start_offset = 0;
    int64_t max_length = -1;

    fmt = NULL;
    for (;;) {
        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"format", required_argument, 0, 'f'},
            {"image-opts", no_argument, 0, OPTION_IMAGE_OPTS},
            {"start-offset", required_argument, 0, 's'},
            {"max-length", required_argument, 0, 'l'},
            {"force-share", no_argument, 0, 'U'},
            {"output", required_argument, 0, OPTION_OUTPUT},
            {"object", required_argument, 0, OPTION_OBJECT},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, "hf:s:l:U",
                        long_options, NULL);
        if (c == -1) {
            break;
        }
        switch (c) {
        case 'h':
            cmd_help(ccmd, "[-f FMT | --image-opts]\n"
"        [--start-offset OFFSET] [--max-length LENGTH]\n"
"        [--output human|json] [-U] [--object OBJDEF] FILE\n"
,
"  -f, --format FMT\n"
"     specify FILE image format explicitly (default: probing is used)\n"
"  --image-opts\n"
"     treat FILE as an option string (key=value,..), not a file name\n"
"     (incompatible with -f|--format)\n"
"  -s, --start-offset OFFSET\n"
"     start at the given OFFSET in the image, not at the beginning\n"
"  -l, --max-length LENGTH\n"
"     process at most LENGTH bytes instead of up to the end of the image\n"
"  --output human|json\n"
"     specify output format name (default: human)\n"
"  -U, --force-share\n"
"     open image in shared mode for concurrent access\n"
"  --object OBJDEF\n"
"     defines QEMU user-creatable object\n"
"  FILE\n"
"     the image file name, or option string (key=value,..)\n"
"     with --image-opts, to operate on\n"
);
            break;
        case 'f':
            fmt = optarg;
            break;
        case OPTION_IMAGE_OPTS:
            image_opts = true;
            break;
        case 's':
            start_offset = cvtnum("start offset", optarg, true);
            if (start_offset < 0) {
                return 1;
            }
            break;
        case 'l':
            max_length = cvtnum("max length", optarg, true);
            if (max_length < 0) {
                return 1;
            }
            break;
        case OPTION_OUTPUT:
            output_format = parse_output_format(argv[0], optarg);
            break;
        case 'U':
            force_share = true;
            break;
        case OPTION_OBJECT:
            user_creatable_process_cmdline(optarg);
            break;
        default:
            tryhelp(argv[0]);
        }
    }
    if (optind != argc - 1) {
        error_exit(argv[0], "Expecting one image file name");
    }
    filename = argv[optind];

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

/* the same as options */
#define SNAPSHOT_LIST   'l'
#define SNAPSHOT_CREATE 'c'
#define SNAPSHOT_APPLY  'a'
#define SNAPSHOT_DELETE 'd'

static int img_snapshot(const img_cmd_t *ccmd, int argc, char **argv)
{
    BlockBackend *blk;
    BlockDriverState *bs;
    QEMUSnapshotInfo sn;
    char *filename, *fmt = NULL, *snapshot_name = NULL;
    int c, ret = 0;
    int action = 0;
    bool quiet = false;
    Error *err = NULL;
    bool image_opts = false;
    bool force_share = false;
    int64_t rt;

    /* Parse commandline parameters */
    for(;;) {
        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"format", required_argument, 0, 'f'},
            {"image-opts", no_argument, 0, OPTION_IMAGE_OPTS},
            {"list", no_argument, 0, SNAPSHOT_LIST},
            {"apply", required_argument, 0, SNAPSHOT_APPLY},
            {"create", required_argument, 0, SNAPSHOT_CREATE},
            {"delete", required_argument, 0, SNAPSHOT_DELETE},
            {"force-share", no_argument, 0, 'U'},
            {"quiet", no_argument, 0, 'q'},
            {"object", required_argument, 0, OPTION_OBJECT},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, "hf:la:c:d:Uq",
                        long_options, NULL);
        if (c == -1) {
            break;
        }
        switch(c) {
        case 'h':
            cmd_help(ccmd, "[-f FMT | --image-opts] [-l | -a|-c|-d SNAPSHOT]\n"
"        [-U] [-q] [--object OBJDEF] FILE\n"
,
"  -f, --format FMT\n"
"     specify FILE format explicitly (default: probing is used)\n"
"  --image-opts\n"
"     treat FILE as an option string (key=value,..), not a file name\n"
"     (incompatible with -f|--format)\n"
"  -l, --list\n"
"     list snapshots in FILE (default action if no -l|-c|-a|-d is given)\n"
"  -c, --create SNAPSHOT\n"
"     create named snapshot\n"
"  -a, --apply SNAPSHOT\n"
"     apply named snapshot to the base\n"
"  -d, --delete SNAPSHOT\n"
"     delete named snapshot\n"
"  (only one of -l|-c|-a|-d can be specified)\n"
"  -U, --force-share\n"
"     open image in shared mode for concurrent access\n"
"  -q, --quiet\n"
"     quiet mode (produce only error messages if any)\n"
"  --object OBJDEF\n"
"     defines QEMU user-creatable object\n"
"  FILE\n"
"     name of the image file, or option string (key=value,..)\n"
"     with --image-opts) to operate on\n"
);
            break;
        case 'f':
            fmt = optarg;
            break;
        case OPTION_IMAGE_OPTS:
            image_opts = true;
            break;
        case SNAPSHOT_LIST:
        case SNAPSHOT_APPLY:
        case SNAPSHOT_CREATE:
        case SNAPSHOT_DELETE:
            if (action) {
                error_exit(argv[0], "Cannot mix '-l', '-a', '-c', '-d'");
                return 0;
            }
            action = c;
            snapshot_name = optarg;
            break;
        case 'U':
            force_share = true;
            break;
        case 'q':
            quiet = true;
            break;
        case OPTION_OBJECT:
            user_creatable_process_cmdline(optarg);
            break;
        default:
            tryhelp(argv[0]);
        }
    }

    if (optind != argc - 1) {
        error_exit(argv[0], "Expecting one image file name");
    }
    filename = argv[optind++];

    if (!action) {
        action = SNAPSHOT_LIST;
    }

    /* Open the image */
    blk = img_open(image_opts, filename, fmt,
                   action == SNAPSHOT_LIST ? 0 : BDRV_O_RDWR,
                   false, quiet, force_share);
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

        rt = g_get_real_time();
        sn.date_sec = rt / G_USEC_PER_SEC;
        sn.date_nsec = (rt % G_USEC_PER_SEC) * 1000;

        bdrv_graph_rdlock_main_loop();
        ret = bdrv_snapshot_create(bs, &sn);
        bdrv_graph_rdunlock_main_loop();

        if (ret) {
            error_report("Could not create snapshot '%s': %s",
                snapshot_name, strerror(-ret));
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
        bdrv_drain_all_begin();
        bdrv_graph_rdlock_main_loop();
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
        bdrv_graph_rdunlock_main_loop();
        bdrv_drain_all_end();
        break;
    }

    /* Cleanup */
    blk_unref(blk);
    if (ret) {
        return 1;
    }
    return 0;
}

static int img_rebase(const img_cmd_t *ccmd, int argc, char **argv)
{
    BlockBackend *blk = NULL, *blk_old_backing = NULL, *blk_new_backing = NULL;
    uint8_t *buf_old = NULL;
    uint8_t *buf_new = NULL;
    BlockDriverState *bs = NULL, *prefix_chain_bs = NULL;
    BlockDriverState *unfiltered_bs, *unfiltered_bs_cow;
    BlockDriverInfo bdi = {0};
    char *filename;
    const char *fmt, *cache, *src_cache, *out_basefmt, *out_baseimg;
    int c, flags, src_flags, ret;
    BdrvRequestFlags write_flags = 0;
    bool writethrough, src_writethrough;
    int unsafe = 0;
    bool force_share = false;
    int progress = 0;
    bool quiet = false;
    bool compress = false;
    Error *local_err = NULL;
    bool image_opts = false;
    int64_t write_align;

    /* Parse commandline parameters */
    fmt = NULL;
    cache = BDRV_DEFAULT_CACHE;
    src_cache = BDRV_DEFAULT_CACHE;
    out_baseimg = NULL;
    out_basefmt = NULL;
    for(;;) {
        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"format", required_argument, 0, 'f'},
            {"image-opts", no_argument, 0, OPTION_IMAGE_OPTS},
            {"cache", required_argument, 0, 't'},
            {"compress", no_argument, 0, 'c'},
            {"backing", required_argument, 0, 'b'},
            {"backing-format", required_argument, 0, 'B'},
            {"backing-cache", required_argument, 0, 'T'},
            {"backing-unsafe", no_argument, 0, 'u'},
            {"force-share", no_argument, 0, 'U'},
            {"progress", no_argument, 0, 'p'},
            {"quiet", no_argument, 0, 'q'},
            {"object", required_argument, 0, OPTION_OBJECT},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, "hf:t:cb:F:B:T:uUpq",
                        long_options, NULL);
        if (c == -1) {
            break;
        }
        switch (c) {
        case 'h':
            cmd_help(ccmd, "[-f FMT | --image-opts] [-t CACHE]\n"
"        [-b BACKING_FILE [-B BACKING_FMT] [-T BACKING_CACHE]] [-u]\n"
"        [-c] [-U] [-p] [-q] [--object OBJDEF] FILE\n"
,
"  -f, --format FMT\n"
"     specify FILE format explicitly (default: probing is used)\n"
"  --image-opts\n"
"     treat FILE as an option string (key=value,..), not a file name\n"
"     (incompatible with -f|--format)\n"
"  -t, --cache CACHE\n"
"     cache mode for FILE (default: " BDRV_DEFAULT_CACHE ")\n"
"  -b, --backing BACKING_FILE|\"\"\n"
"     rebase onto this file (specify empty name for no backing file)\n"
"  -B, --backing-format BACKING_FMT (was -F in <=10.0)\n"
"     specify format for BACKING_FILE explicitly (default: probing is used)\n"
"  -T, --backing-cache CACHE\n"
"     BACKING_FILE cache mode (default: " BDRV_DEFAULT_CACHE ")\n"
"  -u, --backing-unsafe\n"
"     do not fail if BACKING_FILE can not be read\n"
"  -c, --compress\n"
"     compress image (when image supports this)\n"
"  -U, --force-share\n"
"     open image in shared mode for concurrent access\n"
"  -p, --progress\n"
"     display progress information\n"
"  -q, --quiet\n"
"     quiet mode (produce only error messages if any)\n"
"  --object OBJDEF\n"
"     defines QEMU user-creatable object\n"
"  FILE\n"
"     name of the image file, or option string (key=value,..)\n"
"     with --image-opts, to operate on\n"
);
            return 0;
        case 'f':
            fmt = optarg;
            break;
        case OPTION_IMAGE_OPTS:
            image_opts = true;
            break;
        case 't':
            cache = optarg;
            break;
        case 'b':
            out_baseimg = optarg;
            break;
        case 'F': /* <=10.0 */
        case 'B':
            out_basefmt = optarg;
            break;
        case 'u':
            unsafe = 1;
            break;
        case 'c':
            compress = true;
            break;
        case 'U':
            force_share = true;
            break;
        case 'p':
            progress = 1;
            break;
        case 'T':
            src_cache = optarg;
            break;
        case 'q':
            quiet = true;
            break;
        case OPTION_OBJECT:
            user_creatable_process_cmdline(optarg);
            break;
        default:
            tryhelp(argv[0]);
        }
    }

    if (quiet) {
        progress = 0;
    }

    if (optind != argc - 1) {
        error_exit(argv[0], "Expecting one image file name");
    }
    if (!unsafe && !out_baseimg) {
        error_exit(argv[0],
                   "Must specify backing file (-b) or use unsafe mode (-u)");
    }
    filename = argv[optind++];

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

    bdrv_graph_rdlock_main_loop();
    unfiltered_bs = bdrv_skip_filters(bs);
    unfiltered_bs_cow = bdrv_cow_bs(unfiltered_bs);
    bdrv_graph_rdunlock_main_loop();

    if (compress && !block_driver_can_compress(unfiltered_bs->drv)) {
        error_report("Compression not supported for this file format");
        ret = -1;
        goto out;
    } else if (compress) {
        write_flags |= BDRV_REQ_WRITE_COMPRESSED;
    }

    if (out_basefmt != NULL) {
        if (bdrv_find_format(out_basefmt) == NULL) {
            error_report("Invalid format name: '%s'", out_basefmt);
            ret = -1;
            goto out;
        }
    }

    /*
     * We need overlay subcluster size (or cluster size in case writes are
     * compressed) to make sure write requests are aligned.
     */
    ret = bdrv_get_info(unfiltered_bs, &bdi);
    if (ret < 0) {
        error_report("could not get block driver info");
        goto out;
    } else if (bdi.subcluster_size == 0) {
        bdi.cluster_size = bdi.subcluster_size = 1;
    }

    write_align = compress ? bdi.cluster_size : bdi.subcluster_size;

    /* For safe rebasing we need to compare old and new backing file */
    if (!unsafe) {
        QDict *options = NULL;
        BlockDriverState *base_bs;

        bdrv_graph_rdlock_main_loop();
        base_bs = bdrv_cow_bs(unfiltered_bs);
        bdrv_graph_rdunlock_main_loop();

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

            bdrv_graph_rdlock_main_loop();
            bdrv_refresh_filename(bs);
            bdrv_graph_rdunlock_main_loop();
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
        int64_t n, n_old = 0, n_new = 0;
        float local_progress = 0;

        if (blk_old_backing && bdrv_opt_mem_align(blk_bs(blk_old_backing)) >
            bdrv_opt_mem_align(blk_bs(blk))) {
            buf_old = blk_blockalign(blk_old_backing, IO_BUF_SIZE);
        } else {
            buf_old = blk_blockalign(blk, IO_BUF_SIZE);
        }
        buf_new = blk_blockalign(blk_new_backing, IO_BUF_SIZE);

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
            bool old_backing_eof = false;
            int64_t n_alloc;

            /* How many bytes can we handle with the next read? */
            n = MIN(IO_BUF_SIZE, size - offset);

            /* If the cluster is allocated, we don't need to take action */
            ret = bdrv_is_allocated(unfiltered_bs, offset, n, &n);
            if (ret < 0) {
                error_report("error while reading image metadata: %s",
                             strerror(-ret));
                goto out;
            }
            if (ret) {
                continue;
            }

            if (prefix_chain_bs) {
                uint64_t bytes = n;

                /*
                 * If cluster wasn't changed since prefix_chain, we don't need
                 * to take action
                 */
                ret = bdrv_is_allocated_above(unfiltered_bs_cow,
                                              prefix_chain_bs, false,
                                              offset, n, &n);
                if (ret < 0) {
                    error_report("error while reading image metadata: %s",
                                 strerror(-ret));
                    goto out;
                }
                if (!ret && n) {
                    continue;
                }
                if (!n) {
                    /*
                     * If we've reached EOF of the old backing, it means that
                     * offsets beyond the old backing size were read as zeroes.
                     * Now we will need to explicitly zero the cluster in
                     * order to preserve that state after the rebase.
                     */
                    n = bytes;
                }
            }

            /*
             * At this point we know that the region [offset; offset + n)
             * is unallocated within the target image.  This region might be
             * unaligned to the target image's (sub)cluster boundaries, as
             * old backing may have smaller clusters (or have subclusters).
             * We extend it to the aligned boundaries to avoid CoW on
             * partial writes in blk_pwrite(),
             */
            n += offset - QEMU_ALIGN_DOWN(offset, write_align);
            offset = QEMU_ALIGN_DOWN(offset, write_align);
            n += QEMU_ALIGN_UP(offset + n, write_align) - (offset + n);
            n = MIN(n, size - offset);
            assert(!bdrv_is_allocated(unfiltered_bs, offset, n, &n_alloc) &&
                   n_alloc == n);

            /*
             * Much like with the target image, we'll try to read as much
             * of the old and new backings as we can.
             */
            n_old = MIN(n, MAX(0, old_backing_size - (int64_t) offset));
            n_new = MIN(n, MAX(0, new_backing_size - (int64_t) offset));

            /*
             * Read old and new backing file and take into consideration that
             * backing files may be smaller than the COW image.
             */
            memset(buf_old + n_old, 0, n - n_old);
            if (!n_old) {
                old_backing_eof = true;
            } else {
                ret = blk_pread(blk_old_backing, offset, n_old, buf_old, 0);
                if (ret < 0) {
                    error_report("error while reading from old backing file");
                    goto out;
                }
            }

            memset(buf_new + n_new, 0, n - n_new);
            if (n_new) {
                ret = blk_pread(blk_new_backing, offset, n_new, buf_new, 0);
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
                                    n - written, write_align, &pnum))
                {
                    if (old_backing_eof) {
                        ret = blk_pwrite_zeroes(blk, offset + written, pnum, 0);
                    } else {
                        assert(written + pnum <= IO_BUF_SIZE);
                        ret = blk_pwrite(blk, offset + written, pnum,
                                         buf_old + written, write_flags);
                    }
                    if (ret < 0) {
                        error_report("Error while writing to COW image: %s",
                            strerror(-ret));
                        goto out;
                    }
                }

                written += pnum;
                if (offset + written >= old_backing_size) {
                    old_backing_eof = true;
                }
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
        ret = bdrv_change_backing_file(unfiltered_bs, out_baseimg, out_basefmt,
                                       true);
    } else {
        ret = bdrv_change_backing_file(unfiltered_bs, NULL, NULL, false);
    }

    if (ret == -ENOSPC) {
        error_report("Could not change the backing file to '%s': No "
                     "space left in the file header", out_baseimg);
    } else if (ret == -EINVAL && out_baseimg && !out_basefmt) {
        error_report("Could not change the backing file to '%s': backing "
                     "format must be specified", out_baseimg);
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

static int img_resize(const img_cmd_t *ccmd, int argc, char **argv)
{
    Error *err = NULL;
    int c, ret, relative;
    const char *filename = NULL, *fmt = NULL, *size = NULL;
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

    /* Parse getopt arguments */
    for(;;) {
        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"format", required_argument, 0, 'f'},
            {"image-opts", no_argument, 0, OPTION_IMAGE_OPTS},
            {"preallocation", required_argument, 0, OPTION_PREALLOCATION},
            {"shrink", no_argument, 0, OPTION_SHRINK},
            {"quiet", no_argument, 0, 'q'},
            {"object", required_argument, 0, OPTION_OBJECT},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, "-hf:q",
                        long_options, NULL);
        if (c == -1) {
            break;
        }
        switch(c) {
        case 'h':
            cmd_help(ccmd, "[-f FMT | --image-opts] [--preallocation PREALLOC] [--shrink]\n"
"        [-q] [--object OBJDEF] FILE [+-]SIZE[bkKMGTPE]\n"
,
"  -f, --format FMT\n"
"     specify FILE format explicitly (default: probing is used)\n"
"  --image-opts\n"
"     treat FILE as an option string (key=value,...), not a file name\n"
"     (incompatible with -f|--format)\n"
"  --shrink\n"
"     allow operation when the new size is smaller than the original\n"
"  --preallocation PREALLOC\n"
"     specify FMT-specific preallocation type for the new areas\n"
"  -q, --quiet\n"
"     quiet mode (produce only error messages if any)\n"
"  --object OBJDEF\n"
"     defines QEMU user-creatable object\n"
"  FILE\n"
"     name of the image file, or option string (key=value,..)\n"
"     with --image-opts, to operate on\n"
"  [+-]SIZE[bkKMGTPE]\n"
"     new image size or amount by which to shrink (-)/grow (+),\n"
"     with optional multiplier suffix (powers of 1024, default is bytes)\n"
);
            return 0;
        case 'f':
            fmt = optarg;
            break;
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
        case 'q':
            quiet = true;
            break;
        case OPTION_OBJECT:
            user_creatable_process_cmdline(optarg);
            break;
        case 1: /* a non-optional argument */
            if (!filename) {
                filename = optarg;
                /* see if we have -size (number) next to filename */
                if (optind < argc) {
                    size = argv[optind];
                    if (size[0] == '-' && size[1] >= '0' && size[1] <= '9') {
                        ++optind;
                    } else {
                        size = NULL;
                    }
                }
            } else if (!size) {
                size = optarg;
            } else {
                error_exit(argv[0], "Extra argument(s) in command line");
            }
            break;
        default:
            tryhelp(argv[0]);
        }
    }
    if (!filename && optind < argc) {
        filename = argv[optind++];
    }
    if (!size && optind < argc) {
        size = argv[optind++];
    }
    if (!filename || !size || optind < argc) {
        error_exit(argv[0], "Expecting image file name and size");
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

    GRAPH_RDLOCK_GUARD_MAINLOOP();

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

static int img_amend(const img_cmd_t *ccmd, int argc, char **argv)
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
            {"options", required_argument, 0, 'o'},
            {"format", required_argument, 0, 'f'},
            {"image-opts", no_argument, 0, OPTION_IMAGE_OPTS},
            {"cache", required_argument, 0, 't'},
            {"force", no_argument, 0, OPTION_FORCE},
            {"progress", no_argument, 0, 'p'},
            {"quiet", no_argument, 0, 'q'},
            {"object", required_argument, 0, OPTION_OBJECT},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, "ho:f:t:pq",
                        long_options, NULL);
        if (c == -1) {
            break;
        }

        switch (c) {
        case 'h':
            cmd_help(ccmd, "-o FMT_OPTS [-f FMT | --image-opts]\n"
"        [-t CACHE] [--force] [-p] [-q] [--object OBJDEF] FILE\n"
,
"  -o, --options FMT_OPTS\n"
"     FMT-specfic format options (required)\n"
"  -f, --format FMT\n"
"     specify FILE format explicitly (default: probing is used)\n"
"  --image-opts\n"
"     treat FILE as an option string (key=value,..), not a file name\n"
"     (incompatible with -f|--format)\n"
"  -t, --cache CACHE\n"
"     cache mode for FILE (default: " BDRV_DEFAULT_CACHE ")\n"
"  --force\n"
"     allow certain unsafe operations\n"
"  -p, --progres\n"
"     show operation progress\n"
"  -q, --quiet\n"
"     quiet mode (produce only error messages if any)\n"
"  --object OBJDEF\n"
"     defines QEMU user-creatable object\n"
"  FILE\n"
"     name of the image file, or option string (key=value,..)\n"
"     with --image-opts, to operate on\n"
);
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
        case OPTION_IMAGE_OPTS:
            image_opts = true;
            break;
        case 't':
            cache = optarg;
            break;
        case OPTION_FORCE:
            force = true;
            break;
        case 'p':
            progress = true;
            break;
        case 'q':
            quiet = true;
            break;
        case OPTION_OBJECT:
            user_creatable_process_cmdline(optarg);
            break;
        default:
            tryhelp(argv[0]);
        }
    }

    if (!options) {
        error_exit(argv[0], "Must specify options (-o)");
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

    bdrv_graph_rdlock_main_loop();
    if (!bs->drv->bdrv_amend_options) {
        error_report("Format driver '%s' does not support option amendment",
                     fmt);
        bdrv_graph_rdunlock_main_loop();
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

        bdrv_graph_rdunlock_main_loop();
        error_report_err(err);
        ret = -1;
        goto out;
    }

    /* In case the driver does not call amend_status_cb() */
    qemu_progress_print(0.f, 0);
    ret = bdrv_amend_options(bs, opts, &amend_status_cb, NULL, force, &err);
    qemu_progress_print(100.f, 0);
    bdrv_graph_rdunlock_main_loop();

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
        if (b->image_size <= b->bufsize) {
            b->offset = 0;
        } else {
            b->offset %= b->image_size - b->bufsize;
        }
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

static int img_bench(const img_cmd_t *ccmd, int argc, char **argv)
{
    int c, ret = 0;
    const char *fmt = NULL, *filename;
    bool quiet = false;
    bool image_opts = false;
    bool is_write = false;
    int count = 75000;
    int depth = 64;
    int64_t offset = 0;
    ssize_t bufsize = 4096;
    int pattern = 0;
    ssize_t step = 0;
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
    size_t buf_size = 0;

    for (;;) {
        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"format", required_argument, 0, 'f'},
            {"image-opts", no_argument, 0, OPTION_IMAGE_OPTS},
            {"cache", required_argument, 0, 't'},
            {"count", required_argument, 0, 'c'},
            {"depth", required_argument, 0, 'd'},
            {"offset", required_argument, 0, 'o'},
            {"buffer-size", required_argument, 0, 's'},
            {"step-size", required_argument, 0, 'S'},
            {"write", no_argument, 0, 'w'},
            {"pattern", required_argument, 0, OPTION_PATTERN},
            {"flush-interval", required_argument, 0, OPTION_FLUSH_INTERVAL},
            {"no-drain", no_argument, 0, OPTION_NO_DRAIN},
            {"aio", required_argument, 0, 'i'},
            {"native", no_argument, 0, 'n'},
            {"force-share", no_argument, 0, 'U'},
            {"quiet", no_argument, 0, 'q'},
            {"object", required_argument, 0, OPTION_OBJECT},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, "hf:t:c:d:o:s:S:wi:nUq",
                        long_options, NULL);
        if (c == -1) {
            break;
        }

        switch (c) {
        case 'h':
            cmd_help(ccmd, "[-f FMT | --image-opts] [-t CACHE]\n"
"        [-c COUNT] [-d DEPTH] [-o OFFSET] [-s BUFFER_SIZE] [-S STEP_SIZE]\n"
"        [-w [--pattern PATTERN] [--flush-interval INTERVAL [--no-drain]]]\n"
"        [-i AIO] [-n] [-U] [-q] FILE\n"
,
"  -f, --format FMT\n"
"     specify FILE format explicitly\n"
"  --image-opts\n"
"     indicates that FILE is a complete image specification\n"
"     instead of a file name (incompatible with --format)\n"
"  -t, --cache CACHE\n"
"     cache mode for FILE (default: " BDRV_DEFAULT_CACHE ")\n"
"  -c, --count COUNT\n"
"     number of I/O requests to perform\n"
"  -d, --depth DEPTH\n"
"     number of requests to perform in parallel\n"
"  -o, --offset OFFSET\n"
"     start first request at this OFFSET\n"
"  -s, --buffer-size BUFFER_SIZE[bkKMGTPE]\n"
"     size of each I/O request, with optional multiplier suffix\n"
"     (powers of 1024, default is 4K)\n"
"  -S, --step-size STEP_SIZE[bkKMGTPE]\n"
"     each next request offset increment, with optional multiplier suffix\n"
"     (powers of 1024, default is the same as BUFFER_SIZE)\n"
"  -w, --write\n"
"     perform write test (default is read)\n"
"  --pattern PATTERN\n"
"     write this pattern byte instead of zero\n"
"  --flush-interval FLUSH_INTERVAL\n"
"     issue flush after this number of requests\n"
"  --no-drain\n"
"     do not wait when flushing pending requests\n"
"  -i, --aio AIO\n"
"     async-io backend (threads, native, io_uring)\n"
"  -n, --native\n"
"     use native AIO backend if possible\n"
"  -U, --force-share\n"
"     open images in shared mode for concurrent access\n"
"  -q, --quiet\n"
"     quiet mode (produce only error messages if any)\n"
"  --object OBJDEF\n"
"     defines QEMU user-creatable object\n"
"  FILE\n"
"     name of the image file, or option string (key=value,..)\n"
"     with --image-opts, to operate on\n"
);
            break;
        case 'f':
            fmt = optarg;
            break;
        case OPTION_IMAGE_OPTS:
            image_opts = true;
            break;
        case 't':
            ret = bdrv_parse_cache_mode(optarg, &flags, &writethrough);
            if (ret < 0) {
                error_report("Invalid cache mode");
                ret = -1;
                goto out;
            }
            break;
        case 'c':
            count = cvtnum_full("request count", optarg, false, 1, INT_MAX);
            if (count < 0) {
                return 1;
            }
            break;
        case 'd':
            depth = cvtnum_full("queue depth", optarg, false, 1, INT_MAX);
            if (depth < 0) {
                return 1;
            }
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
            offset = cvtnum("offset", optarg, true);
            if (offset < 0) {
                return 1;
            }
            break;
        case 's':
            bufsize = cvtnum_full("buffer size", optarg, true, 1, INT_MAX);
            if (bufsize < 0) {
                return 1;
            }
            break;
        case 'S':
            step = cvtnum_full("step size", optarg, true, 0, INT_MAX);
            if (step < 0) {
                return 1;
            }
            break;
        case 'w':
            flags |= BDRV_O_RDWR;
            is_write = true;
            break;
        case OPTION_PATTERN:
            pattern = cvtnum_full("pattern byte", optarg, false, 0, 0xff);
            if (pattern < 0) {
                return 1;
            }
            break;
        case OPTION_FLUSH_INTERVAL:
            flush_interval = cvtnum_full("flush interval", optarg,
                                         false, 0, INT_MAX);
            if (flush_interval < 0) {
                return 1;
            }
            break;
        case OPTION_NO_DRAIN:
            drain_on_flush = false;
            break;
        case 'U':
            force_share = true;
            break;
        case 'q':
            quiet = true;
            break;
        case OPTION_OBJECT:
            user_creatable_process_cmdline(optarg);
            break;
        default:
            tryhelp(argv[0]);
        }
    }

    if (optind != argc - 1) {
        error_exit(argv[0], "Expecting one image file name");
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

    blk_register_buf(blk, data.buf, buf_size, &error_fatal);

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
        blk_unregister_buf(blk, data.buf, buf_size);
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

static int img_bitmap(const img_cmd_t *ccmd, int argc, char **argv)
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
    int inactivate_ret;

    QSIMPLEQ_INIT(&actions);

    for (;;) {
        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"format", required_argument, 0, 'f'},
            {"image-opts", no_argument, 0, OPTION_IMAGE_OPTS},
            {"add", no_argument, 0, OPTION_ADD},
            {"granularity", required_argument, 0, 'g'},
            {"remove", no_argument, 0, OPTION_REMOVE},
            {"clear", no_argument, 0, OPTION_CLEAR},
            {"enable", no_argument, 0, OPTION_ENABLE},
            {"disable", no_argument, 0, OPTION_DISABLE},
            {"merge", required_argument, 0, OPTION_MERGE},
            {"source-file", required_argument, 0, 'b'},
            {"source-format", required_argument, 0, 'F'},
            {"object", required_argument, 0, OPTION_OBJECT},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, "hf:g:b:F:",
                        long_options, NULL);
        if (c == -1) {
            break;
        }

        switch (c) {
        case 'h':
            cmd_help(ccmd, "[-f FMT | --image-opts]\n"
"        ( --add [-g SIZE] | --remove | --clear | --enable | --disable |\n"
"          --merge SOURCE [-b SRC_FILE [-F SRC_FMT]] )..\n"
"        [--object OBJDEF] FILE BITMAP\n"
,
"  -f, --format FMT\n"
"     specify FILE format explicitly (default: probing is used)\n"
"  --image-opts\n"
"     treat FILE as an option string (key=value,..), not a file name\n"
"     (incompatible with -f|--format)\n"
"  --add\n"
"     creates BITMAP in FILE, enables to record future edits\n"
"  -g, --granularity SIZE[bKMGTPE]\n"
"     sets non-default granularity for the bitmap being added,\n"
"     with optional multiplier suffix (in powers of 1024)\n"
"  --remove\n"
"     removes BITMAP from FILE\n"
"  --clear\n"
"     clears BITMAP in FILE\n"
"  --enable, --disable\n"
"     starts and stops recording future edits to BITMAP in FILE\n"
"  --merge SOURCE\n"
"     merges contents of the SOURCE bitmap into BITMAP in FILE\n"
"  -b, --source-file SRC_FILE\n"
"     select alternative source file for --merge\n"
"  -F, --source-format SRC_FMT\n"
"     specify format for SRC_FILE explicitly\n"
"  --object OBJDEF\n"
"     defines QEMU user-creatable object\n"
"  FILE\n"
"     name of the image file, or option string (key=value,..)\n"
"     with --image-opts, to operate on\n"
"  BITMAP\n"
"     name of the bitmap to add, remove, clear, enable, disable or merge to\n"
);
            break;
        case 'f':
            fmt = optarg;
            break;
        case OPTION_IMAGE_OPTS:
            image_opts = true;
            break;
        case OPTION_ADD:
            act = g_new0(ImgBitmapAction, 1);
            act->act = BITMAP_ADD;
            QSIMPLEQ_INSERT_TAIL(&actions, act, next);
            add = true;
            break;
        case 'g':
            granularity = cvtnum("granularity", optarg, true);
            if (granularity < 0) {
                return 1;
            }
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
        case 'b':
            src_filename = optarg;
            break;
        case 'F':
            src_fmt = optarg;
            break;
        case OPTION_OBJECT:
            user_creatable_process_cmdline(optarg);
            break;
        default:
            tryhelp(argv[0]);
        }
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

    /*
     * No need to open backing chains; we will be manipulating bitmaps
     * directly in this image without reference to image contents.
     */
    blk = img_open(image_opts, filename, fmt, BDRV_O_RDWR | BDRV_O_NO_BACKING,
                   false, false, false);
    if (!blk) {
        goto out;
    }
    bs = blk_bs(blk);
    if (src_filename) {
        src = img_open(false, src_filename, src_fmt, BDRV_O_NO_BACKING,
                       false, false, false);
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
    /*
     * Manually inactivate the images first because this way we can know whether
     * an error occurred. blk_unref() doesn't tell us about failures.
     */
    inactivate_ret = bdrv_inactivate_all();
    if (inactivate_ret < 0) {
        error_report("Error while closing the image: %s", strerror(-inactivate_ret));
        ret = 1;
    }

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

    res = cvtnum_full("bs", arg, true, 1, INT_MAX);

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
    dd->count = cvtnum("count", arg, true);

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
    in->offset = cvtnum("skip", arg, true);

    if (in->offset < 0) {
        return 1;
    }

    return 0;
}

static int img_dd(const img_cmd_t *ccmd, int argc, char **argv)
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
    int64_t out_pos, in_pos;
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
        { "format", required_argument, 0, 'f'},
        { "image-opts", no_argument, 0, OPTION_IMAGE_OPTS},
        { "output-format", required_argument, 0, 'O'},
        { "force-share", no_argument, 0, 'U'},
        { "object", required_argument, 0, OPTION_OBJECT},
        { 0, 0, 0, 0 }
    };

    while ((c = getopt_long(argc, argv, "hf:O:U", long_options, NULL))) {
        if (c == EOF) {
            break;
        }
        switch (c) {
        case 'h':
            cmd_help(ccmd, "[-f FMT|--image-opts] [-O OUTPUT_FMT] [-U]\n"
"        [--object OBJDEF] [bs=BLOCK_SIZE] [count=BLOCKS] if=INPUT of=OUTPUT\n"
,
"  -f, --format FMT\n"
"     specify format for INPUT explicitly (default: probing is used)\n"
"  --image-opts\n"
"     treat INPUT as an option string (key=value,..), not a file name\n"
"     (incompatible with -f|--format)\n"
"  -O, --output-format OUTPUT_FMT\n"
"     format of the OUTPUT (default: raw)\n"
"  -U, --force-share\n"
"     open images in shared mode for concurrent access\n"
"  --object OBJDEF\n"
"     defines QEMU user-creatable object\n"
"  bs=BLOCK_SIZE[bKMGTP]\n"
"     size of the I/O block, with optional multiplier suffix (powers of 1024)\n"
"     (default: 512)\n"
"  count=COUNT\n"
"     number of blocks to convert (default whole INPUT)\n"
"  if=INPUT\n"
"     name of the file, or option string (key=value,..)\n"
"     with --image-opts, to use for input\n"
"  of=OUTPUT\n"
"     output file name to create (will be overridden if alrady exists)\n"
);
            break;
        case 'f':
            fmt = optarg;
            break;
        case OPTION_IMAGE_OPTS:
            image_opts = true;
            break;
        case 'O':
            out_fmt = optarg;
            break;
        case 'U':
            force_share = true;
            break;
        case OPTION_OBJECT:
            user_creatable_process_cmdline(optarg);
            break;
        default:
            tryhelp(argv[0]);
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

    for (out_pos = 0; in_pos < size; ) {
        int bytes = (in_pos + in.bsz > size) ? size - in_pos : in.bsz;

        ret = blk_pread(blk1, in_pos, bytes, in.buf, 0);
        if (ret < 0) {
            error_report("error while reading from input image file: %s",
                         strerror(-ret));
            goto out;
        }
        in_pos += bytes;

        ret = blk_pwrite(blk2, out_pos, bytes, in.buf, 0);
        if (ret < 0) {
            error_report("error while writing to output image file: %s",
                         strerror(-ret));
            goto out;
        }
        out_pos += bytes;
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
    GString *str;
    QObject *obj;
    Visitor *v = qobject_output_visitor_new(&obj);

    visit_type_BlockMeasureInfo(v, NULL, &info, &error_abort);
    visit_complete(v, &obj);
    str = qobject_to_json_pretty(obj, true);
    assert(str != NULL);
    printf("%s\n", str->str);
    qobject_unref(obj);
    visit_free(v);
    g_string_free(str, true);
}

static int img_measure(const img_cmd_t *ccmd, int argc, char **argv)
{
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
    int64_t img_size = -1;
    BlockMeasureInfo *info = NULL;
    Error *local_err = NULL;
    int ret = 1;
    int c;

    static const struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"source-format", required_argument, 0, 'f'}, /* img_convert */
        {"format", required_argument, 0, 'f'},
        {"image-opts", no_argument, 0, OPTION_IMAGE_OPTS},
        {"source-image-opts", no_argument, 0, OPTION_IMAGE_OPTS}, /* img_convert */
        {"snapshot", required_argument, 0, 'l'},
        {"target-format", required_argument, 0, 'O'},
        {"target-format-options", required_argument, 0, 'o'}, /* img_convert */
        {"options", required_argument, 0, 'o'},
        {"force-share", no_argument, 0, 'U'},
        {"output", required_argument, 0, OPTION_OUTPUT},
        {"object", required_argument, 0, OPTION_OBJECT},
        {"size", required_argument, 0, 's'},
        {0, 0, 0, 0}
    };

    while ((c = getopt_long(argc, argv, "hf:l:O:o:Us:",
                            long_options, NULL)) != -1) {
        switch (c) {
        case 'h':
            cmd_help(ccmd, "[-f FMT|--image-opts] [-l SNAPSHOT]\n"
"       [-O TARGET_FMT] [-o TARGET_FMT_OPTS] [--output human|json]\n"
"       [--object OBJDEF] (--size SIZE | FILE)\n"
,
"  -f, --format\n"
"     specify format of FILE explicitly (default: probing is used)\n"
"  --image-opts\n"
"     indicates that FILE is a complete image specification\n"
"     instead of a file name (incompatible with --format)\n"
"  -l, --snapshot SNAPSHOT\n"
"     use this snapshot in FILE as source\n"
"  -O, --target-format TARGET_FMT\n"
"     desired target/output image format (default: raw)\n"
"  -o TARGET_FMT_OPTS\n"
"     options specific to TARGET_FMT\n"
"  --output human|json\n"
"     output format (default: human)\n"
"  -U, --force-share\n"
"     open images in shared mode for concurrent access\n"
"  --object OBJDEF\n"
"     defines QEMU user-creatable object\n"
"  -s, --size SIZE[bKMGTPE]\n"
"     measure file size for given image size,\n"
"     with optional multiplier suffix (powers of 1024)\n"
"  FILE\n"
"     measure file size required to convert from FILE (either a file name\n"
"     or an option string (key=value,..) with --image-options)\n"
);
            break;
        case 'f':
            fmt = optarg;
            break;
        case OPTION_IMAGE_OPTS:
            image_opts = true;
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
        case 'O':
            out_fmt = optarg;
            break;
        case 'o':
            if (accumulate_options(&options, optarg) < 0) {
                goto out;
            }
            break;
        case 'U':
            force_share = true;
            break;
        case OPTION_OUTPUT:
            output_format = parse_output_format(argv[0], optarg);
            break;
        case OPTION_OBJECT:
            user_creatable_process_cmdline(optarg);
            break;
        case 's':
            img_size = cvtnum("image size", optarg, true);
            if (img_size < 0) {
                goto out;
            }
            break;
        default:
            tryhelp(argv[0]);
        }
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
    if (filename && img_size != -1) {
        error_report("--size N cannot be used together with a filename.");
        goto out;
    }
    if (!filename && img_size == -1) {
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
    if (img_size != -1) {
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
    { "amend", img_amend,
      "Update format-specific options of the image" },
    { "bench", img_bench,
      "Run a simple image benchmark" },
    { "bitmap", img_bitmap,
      "Perform modifications of the persistent bitmap in the image" },
    { "check", img_check,
      "Check basic image integrity" },
    { "commit", img_commit,
      "Commit image to its backing file" },
    { "compare", img_compare,
      "Check if two images have the same contents" },
    { "convert", img_convert,
      "Copy one or more images to another with optional format conversion" },
    { "create", img_create,
      "Create and format a new image file" },
    { "dd", img_dd,
      "Copy input to output with optional format conversion" },
    { "info", img_info,
      "Display information about the image" },
    { "map", img_map,
      "Dump image metadata" },
    { "measure", img_measure,
      "Calculate the file size required for a new image" },
    { "rebase", img_rebase,
      "Change the backing file of the image" },
    { "resize", img_resize,
      "Resize the image" },
    { "snapshot", img_snapshot,
      "List or manipulate snapshots in the image" },
    { NULL, NULL, },
};

static void format_print(void *opaque, const char *name)
{
    int *np = opaque;
    if (*np + strlen(name) > 75) {
        printf("\n ");
        *np = 1;
    }
    *np += printf(" %s", name);
}

int main(int argc, char **argv)
{
    const img_cmd_t *cmd;
    const char *cmdname;
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

    socket_init();
    error_init(argv[0]);
    module_call_init(MODULE_INIT_TRACE);
    qemu_init_exec_dir(argv[0]);

    qemu_init_main_loop(&error_fatal);

    qcrypto_init(&error_fatal);

    module_call_init(MODULE_INIT_QOM);
    bdrv_init();

    qemu_add_opts(&qemu_source_opts);
    qemu_add_opts(&qemu_trace_opts);

    while ((c = getopt_long(argc, argv, "+hVT:", long_options, NULL)) != -1) {
        switch (c) {
        case 'h':
            printf(
QEMU_IMG_VERSION
"QEMU disk image utility.  Usage:\n"
"\n"
"  qemu-img [standard options] COMMAND [--help | command options]\n"
"\n"
"Standard options:\n"
"  -h, --help\n"
"     display this help and exit\n"
"  -V, --version\n"
"     display version info and exit\n"
"  -T,--trace TRACE\n"
"     specify tracing options:\n"
"        [[enable=]<pattern>][,events=<file>][,file=<file>]\n"
"\n"
"Recognized commands (run qemu-img COMMAND --help for command-specific help):\n\n");
            for (cmd = img_cmds; cmd->name != NULL; cmd++) {
                printf("  %s - %s\n", cmd->name, cmd->description);
            }
            printf("\nSupported image formats:\n");
            c = 99; /* force a newline */
            bdrv_iterate_format(format_print, &c, false);
            if (c) {
                printf("\n");
            }
            printf("\n" QEMU_HELP_BOTTOM "\n");
            return 0;
        case 'V':
            printf(QEMU_IMG_VERSION);
            return 0;
        case 'T':
            trace_opt_parse(optarg);
            break;
        default:
            tryhelp(argv[0]);
        }
    }

    if (optind >= argc) {
        error_exit(argv[0], "Not enough arguments");
    }

    cmdname = argv[optind];

    if (!trace_init_backends()) {
        exit(1);
    }
    trace_init_file();
    qemu_set_log(LOG_TRACE, &error_fatal);

    /* find the command */
    for (cmd = img_cmds; cmd->name != NULL; cmd++) {
        if (!strcmp(cmdname, cmd->name)) {
            g_autofree char *argv0 = g_strdup_printf("%s %s", argv[0], cmdname);
            /* reset options and getopt processing (incl return order) */
            argv += optind;
            argc -= optind;
            qemu_reset_optind();
            argv[0] = argv0;
            return cmd->handler(cmd, argc, argv);
        }
    }

    /* not found */
    error_exit(argv[0], "Command not found: %s", cmdname);
}

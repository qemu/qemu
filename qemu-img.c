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
#include "qemu-common.h"
#include "qemu-option.h"
#include "qemu-error.h"
#include "osdep.h"
#include "sysemu.h"
#include "block_int.h"
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#endif

typedef struct img_cmd_t {
    const char *name;
    int (*handler)(int argc, char **argv);
} img_cmd_t;

/* Default to cache=writeback as data integrity is not important for qemu-tcg. */
#define BDRV_O_FLAGS BDRV_O_CACHE_WB
#define BDRV_DEFAULT_CACHE "writeback"

static void format_print(void *opaque, const char *name)
{
    printf(" %s", name);
}

/* Please keep in synch with qemu-img.texi */
static void help(void)
{
    const char *help_msg =
           "qemu-img version " QEMU_VERSION ", Copyright (c) 2004-2008 Fabrice Bellard\n"
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
           "    options are: 'none', 'writeback' (default), 'writethrough', 'directsync'\n"
           "    and 'unsafe'\n"
           "  'size' is the disk image size in bytes. Optional suffixes\n"
           "    'k' or 'K' (kilobyte, 1024), 'M' (megabyte, 1024k), 'G' (gigabyte, 1024M)\n"
           "    and T (terabyte, 1024G) are supported. 'b' is ignored.\n"
           "  'output_filename' is the destination disk image filename\n"
           "  'output_fmt' is the destination format\n"
           "  'options' is a comma separated list of format specific options in a\n"
           "    name=value format. Use -o ? for an overview of the options supported by the\n"
           "    used format\n"
           "  '-c' indicates that target image must be compressed (qcow format only)\n"
           "  '-u' enables unsafe rebasing. It is assumed that old and new backing file\n"
           "       match exactly. The image doesn't need a working backing file before\n"
           "       rebasing in this case (useful for renaming the backing file)\n"
           "  '-h' with or without a command shows this help and lists the supported formats\n"
           "  '-p' show progress of command (only certain commands)\n"
           "  '-S' indicates the consecutive number of bytes that must contain only zeros\n"
           "       for qemu-img to create a sparse image during conversion\n"
           "\n"
           "Parameters to snapshot subcommand:\n"
           "  'snapshot' is the name of the snapshot to create, apply or delete\n"
           "  '-a' applies a snapshot (revert disk to saved state)\n"
           "  '-c' creates a snapshot\n"
           "  '-d' deletes a snapshot\n"
           "  '-l' lists all snapshots in the given image\n";

    printf("%s\nSupported formats:", help_msg);
    bdrv_iterate_format(format_print, NULL);
    printf("\n");
    exit(1);
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

    proto_drv = bdrv_find_protocol(filename);
    if (!proto_drv) {
        error_report("Unknown protocol '%s'", filename);
        return 1;
    }

    create_options = append_option_parameters(create_options,
                                              drv->create_options);
    create_options = append_option_parameters(create_options,
                                              proto_drv->create_options);
    print_option_help(create_options);
    free_option_parameters(create_options);
    return 0;
}

static BlockDriverState *bdrv_new_open(const char *filename,
                                       const char *fmt,
                                       int flags)
{
    BlockDriverState *bs;
    BlockDriver *drv;
    char password[256];
    int ret;

    bs = bdrv_new("image");

    if (fmt) {
        drv = bdrv_find_format(fmt);
        if (!drv) {
            error_report("Unknown file format '%s'", fmt);
            goto fail;
        }
    } else {
        drv = NULL;
    }

    ret = bdrv_open(bs, filename, flags, drv);
    if (ret < 0) {
        error_report("Could not open '%s': %s", filename, strerror(-ret));
        goto fail;
    }

    if (bdrv_is_encrypted(bs)) {
        printf("Disk image '%s' is encrypted.\n", filename);
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
    if (bs) {
        bdrv_delete(bs);
    }
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
    int c, ret = 0;
    uint64_t img_size = -1;
    const char *fmt = "raw";
    const char *base_fmt = NULL;
    const char *filename;
    const char *base_filename = NULL;
    char *options = NULL;

    for(;;) {
        c = getopt(argc, argv, "F:b:f:he6o:");
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
            return 1;
        case '6':
            error_report("option -6 is deprecated, please use \'-o "
                  "compat6\' instead!");
            return 1;
        case 'o':
            options = optarg;
            break;
        }
    }

    /* Get the filename */
    if (optind >= argc) {
        help();
    }
    filename = argv[optind++];

    /* Get image size, if specified */
    if (optind < argc) {
        int64_t sval;
        char *end;
        sval = strtosz_suffix(argv[optind++], &end, STRTOSZ_DEFSUFFIX_B);
        if (sval < 0 || *end) {
            error_report("Invalid image size specified! You may use k, M, G or "
                  "T suffixes for ");
            error_report("kilobytes, megabytes, gigabytes and terabytes.");
            ret = -1;
            goto out;
        }
        img_size = (uint64_t)sval;
    }

    if (options && !strcmp(options, "?")) {
        ret = print_block_option_help(filename, fmt);
        goto out;
    }

    ret = bdrv_img_create(filename, fmt, base_filename, base_fmt,
                          options, img_size, BDRV_O_FLAGS);
out:
    if (ret) {
        return 1;
    }
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
    const char *filename, *fmt;
    BlockDriverState *bs;
    BdrvCheckResult result;

    fmt = NULL;
    for(;;) {
        c = getopt(argc, argv, "f:h");
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
        }
    }
    if (optind >= argc) {
        help();
    }
    filename = argv[optind++];

    bs = bdrv_new_open(filename, fmt, BDRV_O_FLAGS);
    if (!bs) {
        return 1;
    }
    ret = bdrv_check(bs, &result);

    if (ret == -ENOTSUP) {
        error_report("This image format does not support checks");
        bdrv_delete(bs);
        return 1;
    }

    if (!(result.corruptions || result.leaks || result.check_errors)) {
        printf("No errors were found on the image.\n");
    } else {
        if (result.corruptions) {
            printf("\n%d errors were found on the image.\n"
                "Data may be corrupted, or further writes to the image "
                "may corrupt it.\n",
                result.corruptions);
        }

        if (result.leaks) {
            printf("\n%d leaked clusters were found on the image.\n"
                "This means waste of disk space, but no harm to data.\n",
                result.leaks);
        }

        if (result.check_errors) {
            printf("\n%d internal errors have occurred during the check.\n",
                result.check_errors);
        }
    }

    bdrv_delete(bs);

    if (ret < 0 || result.check_errors) {
        printf("\nAn error has occurred during the check: %s\n"
            "The check is not complete and may have missed error.\n",
            strerror(-ret));
        return 1;
    }

    if (result.corruptions) {
        return 2;
    } else if (result.leaks) {
        return 3;
    } else {
        return 0;
    }
}

static int img_commit(int argc, char **argv)
{
    int c, ret, flags;
    const char *filename, *fmt, *cache;
    BlockDriverState *bs;

    fmt = NULL;
    cache = BDRV_DEFAULT_CACHE;
    for(;;) {
        c = getopt(argc, argv, "f:ht:");
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
        }
    }
    if (optind >= argc) {
        help();
    }
    filename = argv[optind++];

    flags = BDRV_O_RDWR;
    ret = bdrv_parse_cache_flags(cache, &flags);
    if (ret < 0) {
        error_report("Invalid cache option: %s", cache);
        return -1;
    }

    bs = bdrv_new_open(filename, fmt, flags);
    if (!bs) {
        return 1;
    }
    ret = bdrv_commit(bs);
    switch(ret) {
    case 0:
        printf("Image committed.\n");
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

    bdrv_delete(bs);
    if (ret) {
        return 1;
    }
    return 0;
}

/*
 * Checks whether the sector is not a zero sector.
 *
 * Attention! The len must be a multiple of 4 * sizeof(long) due to
 * restriction of optimizations in this function.
 */
static int is_not_zero(const uint8_t *sector, int len)
{
    /*
     * Use long as the biggest available internal data type that fits into the
     * CPU register and unroll the loop to smooth out the effect of memory
     * latency.
     */

    int i;
    long d0, d1, d2, d3;
    const long * const data = (const long *) sector;

    len /= sizeof(long);

    for(i = 0; i < len; i += 4) {
        d0 = data[i + 0];
        d1 = data[i + 1];
        d2 = data[i + 2];
        d3 = data[i + 3];

        if (d0 || d1 || d2 || d3) {
            return 1;
        }
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
    int v, i;

    if (n <= 0) {
        *pnum = 0;
        return 0;
    }
    v = is_not_zero(buf, 512);
    for(i = 1; i < n; i++) {
        buf += 512;
        if (v != is_not_zero(buf, 512))
            break;
    }
    *pnum = i;
    return v;
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

static int img_convert(int argc, char **argv)
{
    int c, ret = 0, n, n1, bs_n, bs_i, compress, cluster_size, cluster_sectors;
    int progress = 0, flags;
    const char *fmt, *out_fmt, *cache, *out_baseimg, *out_filename;
    BlockDriver *drv, *proto_drv;
    BlockDriverState **bs = NULL, *out_bs = NULL;
    int64_t total_sectors, nb_sectors, sector_num, bs_offset;
    uint64_t bs_sectors;
    uint8_t * buf = NULL;
    const uint8_t *buf1;
    BlockDriverInfo bdi;
    QEMUOptionParameter *param = NULL, *create_options = NULL;
    QEMUOptionParameter *out_baseimg_param;
    char *options = NULL;
    const char *snapshot_name = NULL;
    float local_progress;
    int min_sparse = 8; /* Need at least 4k of zeros for sparse detection */

    fmt = NULL;
    out_fmt = "raw";
    cache = "unsafe";
    out_baseimg = NULL;
    compress = 0;
    for(;;) {
        c = getopt(argc, argv, "f:O:B:s:hce6o:pS:t:");
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
            return 1;
        case '6':
            error_report("option -6 is deprecated, please use \'-o "
                  "compat6\' instead!");
            return 1;
        case 'o':
            options = optarg;
            break;
        case 's':
            snapshot_name = optarg;
            break;
        case 'S':
        {
            int64_t sval;
            char *end;
            sval = strtosz_suffix(optarg, &end, STRTOSZ_DEFSUFFIX_B);
            if (sval < 0 || *end) {
                error_report("Invalid minimum zero buffer size for sparse output specified");
                return 1;
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
        }
    }

    bs_n = argc - optind - 1;
    if (bs_n < 1) {
        help();
    }

    out_filename = argv[argc - 1];

    if (options && !strcmp(options, "?")) {
        ret = print_block_option_help(out_filename, out_fmt);
        goto out;
    }

    if (bs_n > 1 && out_baseimg) {
        error_report("-B makes no sense when concatenating multiple input "
                     "images");
        ret = -1;
        goto out;
    }
        
    qemu_progress_init(progress, 2.0);
    qemu_progress_print(0, 100);

    bs = g_malloc0(bs_n * sizeof(BlockDriverState *));

    total_sectors = 0;
    for (bs_i = 0; bs_i < bs_n; bs_i++) {
        bs[bs_i] = bdrv_new_open(argv[optind + bs_i], fmt, BDRV_O_FLAGS);
        if (!bs[bs_i]) {
            error_report("Could not open '%s'", argv[optind + bs_i]);
            ret = -1;
            goto out;
        }
        bdrv_get_geometry(bs[bs_i], &bs_sectors);
        total_sectors += bs_sectors;
    }

    if (snapshot_name != NULL) {
        if (bs_n > 1) {
            error_report("No support for concatenating multiple snapshot");
            ret = -1;
            goto out;
        }
        if (bdrv_snapshot_load_tmp(bs[0], snapshot_name) < 0) {
            error_report("Failed to load snapshot");
            ret = -1;
            goto out;
        }
    }

    /* Find driver and parse its options */
    drv = bdrv_find_format(out_fmt);
    if (!drv) {
        error_report("Unknown file format '%s'", out_fmt);
        ret = -1;
        goto out;
    }

    proto_drv = bdrv_find_protocol(out_filename);
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

    /* Create the new image */
    ret = bdrv_create(drv, out_filename, param);
    if (ret < 0) {
        if (ret == -ENOTSUP) {
            error_report("Formatting not supported for file format '%s'",
                         out_fmt);
        } else if (ret == -EFBIG) {
            error_report("The image size is too large for file format '%s'",
                         out_fmt);
        } else {
            error_report("%s: error while converting %s: %s",
                         out_filename, out_fmt, strerror(-ret));
        }
        goto out;
    }

    flags = BDRV_O_RDWR;
    ret = bdrv_parse_cache_flags(cache, &flags);
    if (ret < 0) {
        error_report("Invalid cache option: %s", cache);
        return -1;
    }

    out_bs = bdrv_new_open(out_filename, out_fmt, flags);
    if (!out_bs) {
        ret = -1;
        goto out;
    }

    bs_i = 0;
    bs_offset = 0;
    bdrv_get_geometry(bs[0], &bs_sectors);
    buf = qemu_blockalign(out_bs, IO_BUF_SIZE);

    if (compress) {
        ret = bdrv_get_info(out_bs, &bdi);
        if (ret < 0) {
            error_report("could not get block driver info");
            goto out;
        }
        cluster_size = bdi.cluster_size;
        if (cluster_size <= 0 || cluster_size > IO_BUF_SIZE) {
            error_report("invalid cluster size");
            ret = -1;
            goto out;
        }
        cluster_sectors = cluster_size >> 9;
        sector_num = 0;

        nb_sectors = total_sectors;
        local_progress = (float)100 /
            (nb_sectors / MIN(nb_sectors, cluster_sectors));

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

            if (n < cluster_sectors) {
                memset(buf + n * 512, 0, cluster_size - n * 512);
            }
            if (is_not_zero(buf, cluster_size)) {
                ret = bdrv_write_compressed(out_bs, sector_num, buf,
                                            cluster_sectors);
                if (ret != 0) {
                    error_report("error while compressing sector %" PRId64
                                 ": %s", sector_num, strerror(-ret));
                    goto out;
                }
            }
            sector_num += n;
            qemu_progress_print(local_progress, 100);
        }
        /* signal EOF to align */
        bdrv_write_compressed(out_bs, 0, NULL, 0);
    } else {
        int has_zero_init = bdrv_has_zero_init(out_bs);

        sector_num = 0; // total number of sectors converted so far
        nb_sectors = total_sectors - sector_num;
        local_progress = (float)100 /
            (nb_sectors / MIN(nb_sectors, IO_BUF_SIZE / 512));

        for(;;) {
            nb_sectors = total_sectors - sector_num;
            if (nb_sectors <= 0) {
                break;
            }
            if (nb_sectors >= (IO_BUF_SIZE / 512)) {
                n = (IO_BUF_SIZE / 512);
            } else {
                n = nb_sectors;
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

            if (n > bs_offset + bs_sectors - sector_num) {
                n = bs_offset + bs_sectors - sector_num;
            }

            if (has_zero_init) {
                /* If the output image is being created as a copy on write image,
                   assume that sectors which are unallocated in the input image
                   are present in both the output's and input's base images (no
                   need to copy them). */
                if (out_baseimg) {
                    if (!bdrv_is_allocated(bs[bs_i], sector_num - bs_offset,
                                           n, &n1)) {
                        sector_num += n1;
                        continue;
                    }
                    /* The next 'n1' sectors are allocated in the input image. Copy
                       only those as they may be followed by unallocated sectors. */
                    n = n1;
                }
            } else {
                n1 = n;
            }

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
                /* If the output image is being created as a copy on write image,
                   copy all sectors even the ones containing only NUL bytes,
                   because they may differ from the sectors in the base image.

                   If the output is to a host device, we also write out
                   sectors that are entirely 0, since whatever data was
                   already there is garbage, not 0s. */
                if (!has_zero_init || out_baseimg ||
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
            qemu_progress_print(local_progress, 100);
        }
    }
out:
    qemu_progress_end();
    free_option_parameters(create_options);
    free_option_parameters(param);
    qemu_vfree(buf);
    if (out_bs) {
        bdrv_delete(out_bs);
    }
    if (bs) {
        for (bs_i = 0; bs_i < bs_n; bs_i++) {
            if (bs[bs_i]) {
                bdrv_delete(bs[bs_i]);
            }
        }
        g_free(bs);
    }
    if (ret) {
        return 1;
    }
    return 0;
}


static void dump_snapshots(BlockDriverState *bs)
{
    QEMUSnapshotInfo *sn_tab, *sn;
    int nb_sns, i;
    char buf[256];

    nb_sns = bdrv_snapshot_list(bs, &sn_tab);
    if (nb_sns <= 0)
        return;
    printf("Snapshot list:\n");
    printf("%s\n", bdrv_snapshot_dump(buf, sizeof(buf), NULL));
    for(i = 0; i < nb_sns; i++) {
        sn = &sn_tab[i];
        printf("%s\n", bdrv_snapshot_dump(buf, sizeof(buf), sn));
    }
    g_free(sn_tab);
}

static int img_info(int argc, char **argv)
{
    int c;
    const char *filename, *fmt;
    BlockDriverState *bs;
    char fmt_name[128], size_buf[128], dsize_buf[128];
    uint64_t total_sectors;
    int64_t allocated_size;
    char backing_filename[1024];
    char backing_filename2[1024];
    BlockDriverInfo bdi;

    fmt = NULL;
    for(;;) {
        c = getopt(argc, argv, "f:h");
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
        }
    }
    if (optind >= argc) {
        help();
    }
    filename = argv[optind++];

    bs = bdrv_new_open(filename, fmt, BDRV_O_FLAGS | BDRV_O_NO_BACKING);
    if (!bs) {
        return 1;
    }
    bdrv_get_format(bs, fmt_name, sizeof(fmt_name));
    bdrv_get_geometry(bs, &total_sectors);
    get_human_readable_size(size_buf, sizeof(size_buf), total_sectors * 512);
    allocated_size = bdrv_get_allocated_file_size(bs);
    if (allocated_size < 0) {
        snprintf(dsize_buf, sizeof(dsize_buf), "unavailable");
    } else {
        get_human_readable_size(dsize_buf, sizeof(dsize_buf),
                                allocated_size);
    }
    printf("image: %s\n"
           "file format: %s\n"
           "virtual size: %s (%" PRId64 " bytes)\n"
           "disk size: %s\n",
           filename, fmt_name, size_buf,
           (total_sectors * 512),
           dsize_buf);
    if (bdrv_is_encrypted(bs)) {
        printf("encrypted: yes\n");
    }
    if (bdrv_get_info(bs, &bdi) >= 0) {
        if (bdi.cluster_size != 0) {
            printf("cluster_size: %d\n", bdi.cluster_size);
        }
    }
    bdrv_get_backing_filename(bs, backing_filename, sizeof(backing_filename));
    if (backing_filename[0] != '\0') {
        path_combine(backing_filename2, sizeof(backing_filename2),
                     filename, backing_filename);
        printf("backing file: %s (actual path: %s)\n",
               backing_filename,
               backing_filename2);
    }
    dump_snapshots(bs);
    bdrv_delete(bs);
    return 0;
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

    bdrv_oflags = BDRV_O_FLAGS | BDRV_O_RDWR;
    /* Parse commandline parameters */
    for(;;) {
        c = getopt(argc, argv, "la:c:d:h");
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
                help();
                return 0;
            }
            action = SNAPSHOT_LIST;
            bdrv_oflags &= ~BDRV_O_RDWR; /* no need for RW */
            break;
        case 'a':
            if (action) {
                help();
                return 0;
            }
            action = SNAPSHOT_APPLY;
            snapshot_name = optarg;
            break;
        case 'c':
            if (action) {
                help();
                return 0;
            }
            action = SNAPSHOT_CREATE;
            snapshot_name = optarg;
            break;
        case 'd':
            if (action) {
                help();
                return 0;
            }
            action = SNAPSHOT_DELETE;
            snapshot_name = optarg;
            break;
        }
    }

    if (optind >= argc) {
        help();
    }
    filename = argv[optind++];

    /* Open the image */
    bs = bdrv_new_open(filename, NULL, bdrv_oflags);
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
        ret = bdrv_snapshot_delete(bs, snapshot_name);
        if (ret) {
            error_report("Could not delete snapshot '%s': %d (%s)",
                snapshot_name, ret, strerror(-ret));
        }
        break;
    }

    /* Cleanup */
    bdrv_delete(bs);
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

    /* Parse commandline parameters */
    fmt = NULL;
    cache = BDRV_DEFAULT_CACHE;
    out_baseimg = NULL;
    out_basefmt = NULL;
    for(;;) {
        c = getopt(argc, argv, "uhf:F:b:pt:");
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
        }
    }

    if ((optind >= argc) || (!unsafe && !out_baseimg)) {
        help();
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
    bs = bdrv_new_open(filename, fmt, flags);
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

        bs_old_backing = bdrv_new("old_backing");
        bdrv_get_backing_filename(bs, backing_name, sizeof(backing_name));
        ret = bdrv_open(bs_old_backing, backing_name, BDRV_O_FLAGS,
                        old_backing_drv);
        if (ret) {
            error_report("Could not open old backing file '%s'", backing_name);
            goto out;
        }

        bs_new_backing = bdrv_new("new_backing");
        ret = bdrv_open(bs_new_backing, out_baseimg, BDRV_O_FLAGS,
                        new_backing_drv);
        if (ret) {
            error_report("Could not open new backing file '%s'", out_baseimg);
            goto out;
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
        uint64_t sector;
        int n;
        uint8_t * buf_old;
        uint8_t * buf_new;
        float local_progress;

        buf_old = qemu_blockalign(bs, IO_BUF_SIZE);
        buf_new = qemu_blockalign(bs, IO_BUF_SIZE);

        bdrv_get_geometry(bs, &num_sectors);

        local_progress = (float)100 /
            (num_sectors / MIN(num_sectors, IO_BUF_SIZE / 512));
        for (sector = 0; sector < num_sectors; sector += n) {

            /* How many sectors can we handle with the next read? */
            if (sector + (IO_BUF_SIZE / 512) <= num_sectors) {
                n = (IO_BUF_SIZE / 512);
            } else {
                n = num_sectors - sector;
            }

            /* If the cluster is allocated, we don't need to take action */
            ret = bdrv_is_allocated(bs, sector, n, &n);
            if (ret) {
                continue;
            }

            /* Read old and new backing file */
            ret = bdrv_read(bs_old_backing, sector, buf_old, n);
            if (ret < 0) {
                error_report("error while reading from old backing file");
                goto out;
            }
            ret = bdrv_read(bs_new_backing, sector, buf_new, n);
            if (ret < 0) {
                error_report("error while reading from new backing file");
                goto out;
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
    ret = bdrv_change_backing_file(bs, out_baseimg, out_basefmt);
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
            bdrv_delete(bs_old_backing);
        }
        if (bs_new_backing != NULL) {
            bdrv_delete(bs_new_backing);
        }
    }

    bdrv_delete(bs);
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
    BlockDriverState *bs = NULL;
    QEMUOptionParameter *param;
    QEMUOptionParameter resize_options[] = {
        {
            .name = BLOCK_OPT_SIZE,
            .type = OPT_SIZE,
            .help = "Virtual disk size"
        },
        { NULL }
    };

    /* Remove size from argv manually so that negative numbers are not treated
     * as options by getopt. */
    if (argc < 3) {
        help();
        return 1;
    }

    size = argv[--argc];

    /* Parse getopt arguments */
    fmt = NULL;
    for(;;) {
        c = getopt(argc, argv, "f:h");
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
        }
    }
    if (optind >= argc) {
        help();
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
    param = parse_option_parameters("", resize_options, NULL);
    if (set_option_parameter(param, BLOCK_OPT_SIZE, size)) {
        /* Error message already printed when size parsing fails */
        ret = -1;
        goto out;
    }
    n = get_option_parameter(param, BLOCK_OPT_SIZE)->value.n;
    free_option_parameters(param);

    bs = bdrv_new_open(filename, fmt, BDRV_O_FLAGS | BDRV_O_RDWR);
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
        printf("Image resized.\n");
        break;
    case -ENOTSUP:
        error_report("This image format does not support resize");
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
        bdrv_delete(bs);
    }
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

    error_set_progname(argv[0]);

    bdrv_init();
    if (argc < 2)
        help();
    cmdname = argv[1];
    argc--; argv++;

    /* find the command */
    for(cmd = img_cmds; cmd->name != NULL; cmd++) {
        if (!strcmp(cmdname, cmd->name)) {
            return cmd->handler(argc, argv);
        }
    }

    /* not found */
    help();
    return 0;
}

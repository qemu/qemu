/*
 * Copyright (c) 2010-2011 IBM
 *
 * Authors:
 *        Chunqiang Tang <ctang@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

/*=============================================================================
 *  A short description: this module implements a fully automated testing tool
 *  for block device drivers. It works with block/sim.c.
 *=============================================================================
 */

#include <sys/time.h>
#include <sys/types.h>
#include <getopt.h>

#include "qemu-common.h"
#include "qemu/timer.h"
#include "block/block_int.h"
#include "block/fvd-ext.h"
#include "block/blksim.h"

#define die(format,...) \
    do { \
        fprintf (stderr, "%s:%d --- ", __FILE__, __LINE__); \
        fprintf (stderr, format, ##__VA_ARGS__); \
        exit (-1);\
    } while(0)

typedef enum { OP_NULL = 0, OP_READ, OP_WRITE, OP_FLUSH } op_type_t;
const char *op_type_str[] = { "NULL ", "READ ", "WRITE", "FLUSH" };

typedef struct CompareFullCB {
    QEMUIOVector qiov;
    struct iovec iov;
    int64_t sector_num;
    int nb_sectors;
    int max_nb_sectors;
    uint8_t *truth_buf;
} CompareFullCB;

typedef struct RandomIO {
    QEMUIOVector qiov;
    int64_t sector_num;
    int nb_sectors;
    uint8_t *truth_buf;
    uint8_t *test_buf;
    op_type_t type;
    int tester;
    int64_t uuid;
    int allow_cancel;
    BlockDriverAIOCB *acb;
} RandomIO;

static char *progname;
static BlockDriverState *bs;
static int fd;
static int64_t total_sectors;
static int64_t io_size = 262144;
static int verify_write = TRUE;
static int parallel = 1;
static int max_iov = 10;
static int64_t round = 10;
static int64_t finished_round = 0;
static RandomIO *testers = NULL;
static double fail_prob = 0;
static double cancel_prob = 0;
static double flush_prob = 0;
static int64_t rand_time = 1000;
static int64_t test_uuid = 0;
static int instant_qemubh = FALSE;

static void rand_io_cb (void *opaque, int ret);
static void perform_next_io (RandomIO * r);

int64_t qemu_get_clock (QEMUClock * clock)
{
    return sim_get_time ();
}

void timer_mod(QEMUTimer *ts, int64_t expire_time)
{
    sim_mod_timer (ts, expire_time);
}

QEMUTimer *qemu_new_timer (QEMUClock * clock, QEMUTimerCB * cb, void *opaque)
{
    return sim_new_timer (cb, opaque);
}

void timer_free(QEMUTimer *ts)
{
    sim_free_timer (ts);
}

void timer_del(QEMUTimer *ts)
{
    sim_del_timer (ts);
}

QEMUBH *qemu_bh_new (QEMUBHFunc * cb, void *opaque)
{
    return sim_new_timer (cb, opaque);
}

int qemu_bh_poll (void)
{
    return 0;
}

void qemu_bh_schedule (QEMUBH * bh)
{
    if (instant_qemubh) {
        sim_mod_timer (bh, -1);        /* Run this bh next. */
    } else {
        sim_mod_timer (bh, sim_get_time ());
    }
}

void qemu_bh_cancel (QEMUBH * bh)
{
    sim_del_timer (bh);
}

void qemu_bh_delete (QEMUBH * bh)
{
    sim_free_timer (bh);
}

static void usage (void)
{
    printf ("%s [--help]\n"
            "\t--truth=<truth_img>\n"
            "\t--test=<img_to_test>\n"
            "\t[--format=<test_img_fmt>]\n"
            "\t[--round=<#d>]\n"
            "\t[--instant_qemubh=<true|false>]\n"
            "\t[--fail_prob=<#f>]\n"
            "\t[--cancel_prob=<#f>]\n"
            "\t[--flush_prob=<#f>]\n"
            "\t[--io_size=<#d>]\n"
            "\t[--verify_write=[true|false]]\n"
            "\t[--parallel=[#d]\n"
            "\t[--max_iov=[#d]\n"
            "\t[--compare_before=[true|false]]\n"
            "\t[--compare_after=[true|false]]\n" "\n", progname);
    exit (1);
}

static int truth_io (void *buf, int64_t sector_num, int nb_sectors, int do_read)
{
    off_t offset = sector_num * 512;
    size_t size = nb_sectors * 512;

    while (size > 0) {
        int r;
        if (do_read) {
            r = pread (fd, buf, size, offset);
        } else {
            r = pwrite (fd, buf, size, offset);
        }
        if (r >= 0) {
            size -= r;
            offset += r;
            buf = (void *) (((char *) buf) + r);
        } else if (errno != EINTR) {
            perror ("io");
            die ("I/O error on the truth file.\n");
            return -1;
        }
    }

    return 0;
}

static int verify (uint8_t * truth_buf, uint8_t * test_buf,
                   int64_t sector_num, int nb_sectors)
{
    int i;
    for (i = 0; i < nb_sectors; i++) {
        int64_t offset = i * 512;
        if (memcmp (&truth_buf[offset], &test_buf[offset], 512) != 0) {
            int j;
            printf ("Sector %" PRId64 " differs\n", sector_num + i);
            QDEBUG ("Sector %" PRId64 " differs\n", sector_num + i);
            for (j = 0; j < 512; j++) {
                if (truth_buf[offset + j] == test_buf[offset + j]) {
                    QDEBUG ("%02d: %02X  %02X\n", j, truth_buf[offset + j],
                            test_buf[offset + j]);
                } else {
                    QDEBUG ("%02d: %02X  %02X   ***\n", j,
                            truth_buf[offset + j], test_buf[offset + j]);
                }
            }

            fprintf (stderr, "Pause process %d for debugging...\n", getpid ());
            fgetc (stdin);

            return -1;
        }
    }

    return 0;
}

static void compare_full_images_cb (void *opaque, int ret)
{
    CompareFullCB *cf = opaque;

    if (ret) {
        /* Failed. Retry the operation. */
        bdrv_aio_readv (bs, cf->sector_num, &cf->qiov, cf->nb_sectors,
                        compare_full_images_cb, cf);
        return;
    }

    truth_io (cf->truth_buf, cf->sector_num, cf->nb_sectors, TRUE);
    verify (cf->truth_buf, cf->iov.iov_base, cf->sector_num, cf->nb_sectors);

    cf->sector_num += cf->nb_sectors;
    if (cf->sector_num >= total_sectors) {
        /* Finished. */
        free (cf->truth_buf);
        qemu_vfree (cf->iov.iov_base);
        g_free(cf);
        return;
    }

    /* Read more data to compare. */
    if (cf->sector_num + cf->max_nb_sectors > total_sectors) {
        cf->nb_sectors = total_sectors - cf->sector_num;
    } else {
        cf->nb_sectors = cf->max_nb_sectors;
    }
    cf->iov.iov_len = cf->nb_sectors * 512;
    qemu_iovec_init_external (&cf->qiov, &cf->iov, 1);
    if (!bdrv_aio_readv (bs, cf->sector_num, &cf->qiov,
                         cf->nb_sectors, compare_full_images_cb, cf)) {
        die ("bdrv_aio_readv\n");
    }
}

static int compare_full_images (void)
{
    CompareFullCB *cf;
    int old_copy_on_read = FALSE;

    printf ("Performing a full comparison of the truth image and "
            "the test image...\n");

    if (!strncmp (bs->drv->format_name, "fvd", 3)) {
        /* Disable copy-on-read when scanning through the entire image. */
        old_copy_on_read = fvd_get_copy_on_read (bs);
        fvd_set_copy_on_read (bs, FALSE);
    }

    cf = g_malloc(sizeof(CompareFullCB));
    cf->max_nb_sectors = 1048576L / 512;
    cf->nb_sectors = MIN (cf->max_nb_sectors, total_sectors);
    if (posix_memalign ((void **) &cf->truth_buf, 512,
                        cf->max_nb_sectors * 512) != 0) {
        die ("posix_memalign");
    }
    cf->iov.iov_base = qemu_blockalign (bs, cf->max_nb_sectors * 512);
    cf->iov.iov_len = cf->nb_sectors * 512;
    cf->sector_num = 0;
    qemu_iovec_init_external (&cf->qiov, &cf->iov, 1);
    if (!bdrv_aio_readv (bs, cf->sector_num, &cf->qiov,
                         cf->nb_sectors, compare_full_images_cb, cf)) {
        die ("bdrv_aio_readv\n");
    }

    sim_all_tasks ();

    if (!strncmp (bs->drv->format_name, "fvd", 3)) {
        fvd_set_copy_on_read (bs, old_copy_on_read);
    }

    return 0;
}

static inline int64_t rand64 (void)
{
    int64_t f1 = random ();
    int64_t f2 = random ();
    int64_t f3 = (f1 << 32) | f2;
    return f3 >= 0 ? f3 : -f3;
}

static int check_conflict (RandomIO * r)
{
    int i;

    for (i = 0; i < parallel; i++) {
        RandomIO *s = &testers[i];
        if (s == r || s->type == OP_FLUSH ||
            (r->type == OP_READ && s->type == OP_READ)) {
            continue;
        }

        if ((r->sector_num <= s->sector_num &&
             s->sector_num < r->sector_num + r->nb_sectors) ||
            (s->sector_num <= r->sector_num &&
             r->sector_num < s->sector_num + s->nb_sectors)) {
            return 1;        /* Conflict. */
        }
    }

    return 0;        /* No confict. */
}

/* Return FALSE if the submitted request is cancelled. */
static int submit_rand_io (RandomIO * r)
{
    BlockDriverAIOCB *acb = NULL;

    QDEBUG ("TESTER %03d:  %s  test%" PRIX64 " sector_num=%" PRId64
            " nb_sectors=%d niov=%d\n", r->tester, op_type_str[r->type],
            r->uuid, r->sector_num, r->nb_sectors, r->qiov.niov);
    printf ("TESTER %03d:  %s  sector_num=%" PRId64 " nb_sectors=%d niov=%d\n",
            r->tester, op_type_str[r->type], r->sector_num, r->nb_sectors,
            r->qiov.niov);

    int ret;
    if (fail_prob <= 0) {
        ret = 0;
    } else if (random () / (double) RAND_MAX <= fail_prob) {
        ret = -EIO;
    } else {
        ret = 0;
    }

    /* This affects whether this request will fail or not. */
    sim_set_disk_io_return_code (ret);

    switch (r->type) {
    case OP_READ:
        if (!(acb = bdrv_aio_readv (bs, r->sector_num, &r->qiov, r->nb_sectors,
                             rand_io_cb, r))) {
            die ("bdrv_aio_readv\n");
        }
        break;
    case OP_WRITE:
        if (!(acb = bdrv_aio_writev (bs, r->sector_num, &r->qiov, r->nb_sectors,
                              rand_io_cb, r))) {
            die ("bdrv_aio_writev\n");
        }
        break;
    case OP_FLUSH:
        if (!(acb = bdrv_aio_flush (bs, rand_io_cb, r))) {
            die ("bdrv_aio_flush\n");
        }
        break;
    case OP_NULL:
        die ("OP_NULL");
        break;
    }

    sim_set_disk_io_return_code (0);        /* Reset to no failure state. */

    if (r->allow_cancel && cancel_prob > 0 &&
                random () / (double) RAND_MAX <= cancel_prob) {
        QDEBUG ("TESTER %03d:  cancel %s test%" PRIX64 " sector_num=%" PRId64
                " nb_sectors=%d niov=%d\n", r->tester, op_type_str[r->type],
                r->uuid, r->sector_num, r->nb_sectors, r->qiov.niov);
        printf ("TESTER %03d:  cancel %s sector_num=%" PRId64
                " nb_sectors=%d niov=%d\n", r->tester, op_type_str[r->type],
                r->sector_num, r->nb_sectors, r->qiov.niov);
        bdrv_aio_cancel (acb);
        return FALSE;
    } else {
        return TRUE;
    }
}

static void prepare_read_write (RandomIO * r)
{
    /* Do a READ or WRITE? */
    if (random () % 2) {
        r->type = OP_READ;
    } else {
        r->type = OP_WRITE;
    }

    /* Find the next region to perform io. */
    do {
        if (parallel <= 1 || (random () % 2 == 0)) {
            /* Perform a random I/O. */
            r->sector_num = rand64 () % total_sectors;
        } else {
            /* Perform an I/O next to a currently ongoing I/O. */
            int id;
            do {
                id = random () % parallel;
            } while (id == r->tester);

            RandomIO *p = &testers[id];
            r->sector_num =
                p->sector_num + 2 * io_size - rand64 () % (4 * io_size);
            if (r->sector_num < 0) {
                r->sector_num = 0;
            } else if (r->sector_num >= total_sectors) {
                r->sector_num = total_sectors - 1;
            }
        }

        r->nb_sectors = 1 + rand64 () % io_size;
        if (r->sector_num + r->nb_sectors > total_sectors) {
            r->nb_sectors = total_sectors - r->sector_num;
        }
    } while (check_conflict (r));

    if (r->type == OP_WRITE) {
        /* Fill test_buf with random data. */
        int i, j;
        for (i = 0; i < r->nb_sectors; i++) {
            const uint64_t TEST_MAGIC = 0x0123456789ABCDEFULL;
            /* This first 8 bytes of the sector stores the current testing
             * round. The next 8 bytes store a magic number.  This info helps
             * debugging. */
            uint64_t *p = (uint64_t *) & r->test_buf[i * 512];
            *p = r->uuid;
            cpu_to_be64s (p);
            p++;
            *p = TEST_MAGIC;
            cpu_to_be64s (p);

            /* The rest of the sector are filled with random data. */
            uint32_t *q = (uint32_t *) (p + 1);
            int n = (512 - 2 * sizeof (uint64_t)) / sizeof (uint32_t);
            for (j = 0; j < n; j++) {
                *q++ = random ();
            }
        }
    }

    /* Determine the number of iov. */
    int niov = 0;
    uint8_t *p = r->test_buf;
    int left = r->nb_sectors;
    do {
        if (niov == max_iov - 1) {
            r->qiov.iov[niov].iov_len = left * 512;
            r->qiov.iov[niov].iov_base = p;
            niov++;
            break;
        }

        int nb = 1 + random () % left;
        r->qiov.iov[niov].iov_len = nb * 512;
        r->qiov.iov[niov].iov_base = p;
        p += r->qiov.iov[niov].iov_len;
        left -= nb;
        niov++;
    } while (left > 0);

    qemu_iovec_init_external (&r->qiov, r->qiov.iov, niov);
}

static void perform_next_io (RandomIO * r)
{
    if (finished_round >= round) {
        return;
    }

    finished_round++;
    r->allow_cancel = TRUE;

    do {
        r->uuid = test_uuid++;

        if (flush_prob > 0 && random () / (double) RAND_MAX < flush_prob) {
            r->type = OP_FLUSH;
        } else {
            prepare_read_write (r);
        }
    } while (!submit_rand_io (r));
}

static void rand_io_cb (void *opaque, int ret)
{
    RandomIO *r = opaque;

    if (ret) {
        if (fail_prob <= 0) {
            fprintf (stderr, "Request %s sector_num=%" PRId64
                     " nb_sectors=%d failed while fail_prob=0. "
                     "Pause for debugging...\n",
                     op_type_str[r->type], r->sector_num, r->nb_sectors);
            fgetc (stdin);
        } else {
            /* Failed. Retry the operation. */
            QDEBUG ("TESTER %03d:  retry %s  test%" PRIX64 " sector_num=%"
                    PRId64 " nb_sectors=%d niov=%d\n",
                    r->tester, op_type_str[r->type], r->uuid,
                    r->sector_num, r->nb_sectors, r->qiov.niov);
            if (!submit_rand_io (r)) {
                perform_next_io (r);
            }
            return;
        }
    } else {
        QDEBUG ("TESTER %03d:  finished %s  test%" PRIX64 " sector_num=%"PRId64
                " nb_sectors=%d niov=%d\n", r->tester, op_type_str[r->type],
                r->uuid, r->sector_num, r->nb_sectors, r->qiov.niov);
    }

    switch (r->type) {
    case OP_FLUSH:
        perform_next_io (r);
        return;

    case OP_READ:
        truth_io (r->truth_buf, r->sector_num, r->nb_sectors, TRUE);
        verify (r->truth_buf, r->test_buf, r->sector_num, r->nb_sectors);
        perform_next_io (r);
        return;

    case OP_WRITE:
        truth_io (r->test_buf, r->sector_num, r->nb_sectors, FALSE);
        if (verify_write) {
            /* Perform a read for the same data. */
            r->type = OP_READ;

            /* To verify the write, this read cannot be cancelled. */
            r->allow_cancel = FALSE;
            r->qiov.niov = 1;
            r->qiov.iov[0].iov_len = r->qiov.size;
            memset (r->test_buf, 0xA5, r->qiov.size); /* Fill in garbage. */
            submit_rand_io (r);
        } else {
            perform_next_io (r);
        }
        return;

    case OP_NULL:
        die ("OP_NULL");
        return;
    }
}

static int read_bool (const char *arg)
{
    int val = TRUE;
    if (strcmp (optarg, "true") == 0) {
        val = TRUE;
    } else if (strcmp (optarg, "false") == 0) {
        val = FALSE;
    } else {
        printf ("%s is neither 'true' nor 'false'\n", arg);
        usage ();
    }

    return val;
}


static void perform_test(const char *truth_file, const char *test_file,
                         const char *format, int compare_before,
                         int compare_after)
{
    int flags, i;

    bs = bdrv_new ("hda");
    if (!bs) {
        die ("bdrv_new failed\n");
    }

    BlockDriver *drv = NULL;
    if (format) {
        drv = bdrv_find_format (format);
        if (!drv) {
            die ("Found no driver for format '%s'.\n", format);
        }
    }

    flags = BDRV_O_RDWR | BDRV_O_CACHE_WB;

    if (bdrv_open (bs, test_file, flags, drv) < 0) {
        die ("Failed to open '%s'\n", test_file);
    }

    fd = open (truth_file, O_RDWR | O_LARGEFILE, 0);
    if (fd < 0) {
        perror ("open");
        die ("Failed to open '%s'\n", truth_file);
    }

    int64_t l0 = lseek (fd, 0, SEEK_END);
    int64_t l1 = bdrv_getlength (bs);
    if (l0 < 0 || l1 < 0 || l0 < l1) {
        die ("Mismatch: truth image %s length %" PRId64 ", test image %s "
             "length %" PRId64 "\n", truth_file, l0, test_file, l1);
    }

    total_sectors = l1 / 512;
    if (total_sectors <= 1) {
        die ("Total sectors: %" PRId64 "\n", total_sectors);
    }

    io_size /= 512;
    if (io_size <= 0) {
        io_size = 1;
    } else if (io_size > total_sectors / 2) {
        io_size = total_sectors / 2;
    }

    if (compare_before) {
        if (compare_full_images ()) {
            die ("The original two files do not match.\n");
        }
    }

    if (round > 0) {
        /* Create testers. */
        testers = g_malloc(sizeof(RandomIO) * parallel);
        for (i = 0; i < parallel; i++) {
            RandomIO *r = &testers[i];
            r->test_buf = qemu_blockalign (bs, io_size * 512);
            if (posix_memalign ((void **) &r->truth_buf, 512, io_size * 512)) {
                die ("posix_memalign");
            }
            r->qiov.iov = g_malloc(sizeof(struct iovec) * max_iov);
            r->sector_num = 0;
            r->nb_sectors = 0;
            r->type = OP_READ;
            r->tester = i;
        }
        for (i = 0; i < parallel; i++) {
            perform_next_io (&testers[i]);
        }
    }

    sim_all_tasks ();        /* Run tests. */

    if (round > 0) {
        /* Create testers. */
        if (compare_after) {
            if (compare_full_images ()) {
                die ("The two files do not match after I/O operations.\n");
            }
        }

        for (i = 0; i < parallel; i++) {
            RandomIO *r = &testers[i];
            qemu_vfree (r->test_buf);
            free (r->truth_buf);
            g_free(r->qiov.iov);
        }
        g_free(testers);
    }

    printf ("Test process %d finished successfully\n", getpid ());

    int fvd = (strncmp (bs->drv->format_name, "fvd", 3) == 0);
    bdrv_delete (bs);
    if (fvd) {
        fvd_check_memory_usage ();
    }
    close (fd);
}

int main (int argc, char **argv)
{
    int c;
    const char *truth_file = NULL;
    const char *test_file = NULL;
    const char *format = NULL;
    int compare_before = FALSE;
    int compare_after = TRUE;
    int seed = 0;

    const struct option lopt[] = {
        {"help", 0, 0, 'h'},
        {"seed", 1, 0, 'd'},
        {"truth", 1, 0, 'b'},
        {"test", 1, 0, 't'},
        {"format", 1, 0, 'f'},
        {"rand_time", 1, 0, 'n'},
        {"fail_prob", 1, 0, 'u'},
        {"cancel_prob", 1, 0, 'c'},
        {"flush_prob", 1, 0, 'w'},
        {"round", 1, 0, 'r'},
        {"parallel", 1, 0, 'p'},
        {"compare_before", 1, 0, 'm'},
        {"verify_write", 1, 0, 'v'},
        {"compare_after", 1, 0, 'a'},
        {"max_iov", 1, 0, 'i'},
        {"io_size", 1, 0, 's'},
        {"instant_qemubh", 1, 0, 'q'},
        {NULL, 0, NULL, 0}
    };

    progname = basename (argv[0]);

    while ((c = getopt_long (argc, argv, "hc:u:p:q:i:f:d:b:t:r:m:v:a:s:",
                             lopt, NULL)) != -1) {
        switch (c) {
        case 'h':
            usage ();
            return 0;

        case 'q':
            instant_qemubh = read_bool (optarg);
            break;

        case 'w':
            flush_prob = atof (optarg);
            break;

        case 'c':
            cancel_prob = atof (optarg);
            break;

        case 'u':
            fail_prob = atof (optarg);
            break;

        case 'n':
            rand_time = atoll (optarg);
            break;

        case 'i':
            max_iov = atoi (optarg);
            break;

        case 'p':
            parallel = atoi (optarg);
            break;

        case 'v':
            verify_write = read_bool (optarg);
            break;

        case 'm':
            compare_before = read_bool (optarg);
            break;

        case 'a':
            compare_after = read_bool (optarg);
            break;

        case 'd':
            seed = atoll (optarg);
            break;

        case 'f':
            format = optarg;
            break;

        case 'b':
            truth_file = optarg;
            break;

        case 't':
            test_file = optarg;
            break;

        case 's':
            io_size = atoll (optarg);
            break;

        case 'r':
            round = atoll (optarg);
            break;

        default:
            usage ();
            return 1;
        }
    }

    if (!truth_file || !test_file) {
        usage ();
        return 1;
    }

    if (parallel <= 0) {
        parallel = 1;
    }
    srandom (seed);
    /* Convince FVD this is not in a qemu-tool. */
    in_qemu_tool = false;
    enable_block_sim (FALSE /*no print */ , rand_time);
    fvd_enable_host_crash_test ();
    bdrv_init ();
    perform_test (truth_file, test_file, format, compare_before, compare_after);
    return 0;
}

/*
 * Copyright (c) 2010-2011 IBM
 *
 * Authors:
 *         Chunqiang Tang <ctang@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

/*=============================================================================
 *  A short description: this module implements a simulated block device
 *  driver "blksim". It works with qemu-io and qemu-test to perform testing,
 *  allowing changing the  order of disk I/O and callback activities to test
 *  rare race conditions. See qemu-test.c, qemu-io.c, and qemu-io-sim.c.
 *============================================================================*/

#include <sys/vfs.h>
#include <sys/mman.h>
#include <pthread.h>
#include <execinfo.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>
#include "block_int.h"
#include "osdep.h"
#include "qemu-option.h"
#include "qemu-timer.h"
#include "block.h"
#include "qemu-queue.h"
#include "qemu-common.h"
#include "block/blksim.h"
#include "block/fvd-ext.h"

typedef enum {
    SIM_NULL,
    SIM_READ,
    SIM_WRITE,
    SIM_FLUSH,
    SIM_READ_CALLBACK,
    SIM_WRITE_CALLBACK,
    SIM_FLUSH_CALLBACK,
    SIM_TIMER
} sim_op_t;

static void sim_aio_cancel (BlockDriverAIOCB * acb);
static int64_t sim_uuid = 0;
static int64_t current_time = 0;
static int64_t rand_time = 0;
static int interactive_print = FALSE;
struct SimAIOCB;

/*
 * Note: disk_io_return_code, set_disk_io_return_code(), and insert_task() work
 * together to ensure that multiple subrequests triggered by the same
 * outtermost request either succeed together or fail together. This behavior
 * is required by qemu-test.  Here is one example of problems caused by
 * departuring from this behavior.  Consider a write request that generates
 * two subrequests, w1 and w2. If w1 succeeds but w2 fails, the data will not
 * be written into qemu-test's "truth image" but the part of the data handled
 * by w1 will be written into qemu-test's "test image". As a result, their
 * contents diverge can automated testing cannot continue.
 */
static int disk_io_return_code = 0;

typedef struct BDRVSimState {
    int fd;
} BDRVSimState;

typedef struct SimAIOCB {
    BlockDriverAIOCB common;
    int64_t uuid;
    sim_op_t op;
    int64_t sector_num;
    QEMUIOVector *qiov;
    int nb_sectors;
    int ret;
    int64_t time;
    struct SimAIOCB *next;
    struct SimAIOCB *prev;

} SimAIOCB;

static AIOPool sim_aio_pool = {
    .aiocb_size = sizeof (SimAIOCB),
    .cancel = sim_aio_cancel,
};

static SimAIOCB head = {
    .uuid = -1,
    .time = (int64_t) (9223372036854775807ULL),
    .op = SIM_NULL,
    .next = &head,
    .prev = &head,
};

/* Debug a specific task.*/
#if 1
# define CHECK_TASK(acb) do { } while (0)
#else
static inline void CHECK_TASK (int64_t uuid)
{
    if (uuid == 19LL) {
        printf ("CHECK_TASK pause for task %" PRId64 "\n", uuid);
    }
}
#endif

/* do_io() should never fail. A failure indicates a bug in the upper layer
 * block device driver, or failure in the real hardware. */
static int do_io (BlockDriverState * bs, int64_t sector_num, uint8_t * buf,
                  int nb_sectors, int do_read)
{
    BDRVSimState *s = bs->opaque;
    size_t size = nb_sectors * 512;
    int ret;

    if (lseek (s->fd, sector_num * 512, SEEK_SET) < 0) {
        fprintf (stderr, "Error: lseek %s sector_num=%" PRId64 ". "
                 "Pause process %d for debugging...\n",
                 bs->filename, sector_num, getpid ());
        fgetc (stdin);
    }

    while (size > 0) {

        if (do_read) {
            ret = read (s->fd, buf, size);
            if (ret == 0) {
                fprintf (stderr,
                         "Error: read beyond the size of %s sector_num=%" PRId64
                         " nb_sectors=%d. Pause process %d for debugging...\n",
                         bs->filename, sector_num, nb_sectors, getpid ());
                fgetc (stdin);
            }
        } else {
            ret = write (s->fd, buf, size);
        }

        if (ret >= 0) {
            size -= ret;
            buf += ret;
        } else if (errno != EINTR) {
            fprintf (stderr, "Error: %s %s sector_num=%" PRId64
                     " nb_sectors=%d. Pause process %d for debugging...\n",
                     do_read ? "READ" : "WRITE", bs->filename, sector_num,
                     nb_sectors, getpid ());
            fgetc (stdin);
            return -errno;
        }
    }

    return 0;
}

static int sim_read (BlockDriverState * bs, int64_t sector_num, uint8_t * buf,
                     int nb_sectors)
{
    return do_io (bs, sector_num, buf, nb_sectors, TRUE);
}

static int sim_write (BlockDriverState * bs, int64_t sector_num,
                      const uint8_t * buf, int nb_sectors)
{
    return do_io (bs, sector_num, (uint8_t *) buf, nb_sectors, FALSE);
}

static void insert_in_list (SimAIOCB * acb)
{
    int64_t new_id = sim_uuid++;
    CHECK_TASK (new_id);
    acb->uuid = new_id;

    if (rand_time <= 0) {
        /* Working with qemu-io.c and not doing delay randomization.
         * Insert it to the tail. */
        acb->time = 0;
        acb->prev = head.prev;
        acb->next = &head;
        head.prev->next = acb;
        head.prev = acb;
        return;
    }

    if (acb->time >= 0) {
        /* Introduce a random delay to better trigger rare race conditions. */
        acb->time += random () % rand_time;
    }

    /* Find the position to insert. The list is sorted in ascending time. */
    SimAIOCB *p = head.next;
    while (1) {
        if (p->time > acb->time) {
            break;
        }
        if (p->time == acb->time && (random () % 2 == 0)) {
            break;
        }
        p = p->next;
    }

    /* Insert acb before p. */
    acb->next = p;
    acb->prev = p->prev;
    p->prev->next = acb;
    p->prev = acb;
}

/* Debug problems related to reusing task objects. Problem already solved.*/
#if 1
# define my_qemu_aio_get qemu_aio_get
# define my_qemu_aio_release qemu_aio_release

#else
static SimAIOCB *search_task_list (SimAIOCB * acb)
{
    SimAIOCB *p;
    for (p = head.next; p != &head; p = p->next) {
        if (p == acb) {
            return p;
        }
    }

    return NULL;
}

static inline void *my_qemu_aio_get (AIOPool * pool, BlockDriverState * bs,
                                     BlockDriverCompletionFunc * cb,
                                     void *opaque)
{
    SimAIOCB *acb = (SimAIOCB *) qemu_aio_get (&sim_aio_pool, bs, cb, opaque);
    QDEBUG ("SIM: qemu_aio_get reuse old task%" PRId64 "\n", acb->uuid);
    ASSERT (!search_task_list (acb));
    return acb;
}

static inline void my_qemu_aio_release (SimAIOCB * acb)
{
    QDEBUG ("SIM: qemu_aio_release task%" PRId64 "\n", acb->uuid);
    qemu_aio_release (acb);
}
#endif

static BlockDriverAIOCB *insert_task (int op, BlockDriverState * bs,
                                      int64_t sector_num, QEMUIOVector * qiov,
                                      int nb_sectors,
                                      BlockDriverCompletionFunc * cb,
                                      void *opaque)
{
    SimAIOCB *acb = my_qemu_aio_get (&sim_aio_pool, bs, cb, opaque);
    if (!acb) {
        return NULL;
    }

    acb->op = op;
    acb->sector_num = sector_num;
    acb->qiov = qiov;
    acb->nb_sectors = nb_sectors;
    acb->ret = disk_io_return_code;
    acb->time = current_time;
    insert_in_list (acb);

    if (interactive_print) {
        if (op == SIM_READ) {
            printf ("Added READ uuid=%" PRId64 "  filename=%s  sector_num=%"
                    PRId64 "  nb_sectors=%d\n", acb->uuid,
                    acb->common.bs->filename, acb->sector_num, acb->nb_sectors);
        } else if (op == SIM_WRITE) {
            printf ("Added WRITE uuid=%" PRId64 "  filename=%s  sector_num=%"
                    PRId64 "  nb_sectors=%d\n", acb->uuid,
                    acb->common.bs->filename, acb->sector_num, acb->nb_sectors);
        } else {
            fprintf (stderr, "Unknown op %d\n", op);
            exit (1);
        }
    }

    return &acb->common;
}

static void insert_aio_callback (SimAIOCB * acb)
{
    acb->time = current_time;
    insert_in_list (acb);

    if (acb->op == SIM_FLUSH) {
        acb->op = SIM_FLUSH_CALLBACK;
        if (interactive_print) {
            printf ("Added FLUSH_CALLBACK uuid=%" PRId64 "  filename=%s\n",
                    acb->uuid, acb->common.bs->filename);
        }
    } else if (acb->op == SIM_READ) {
        acb->op = SIM_READ_CALLBACK;
        if (interactive_print) {
            printf ("Added READ_CALLBACK uuid=%" PRId64
                    "  filename=%s  sector_num=%" PRId64 "  nb_sectors=%d\n",
                    acb->uuid, acb->common.bs->filename, acb->sector_num,
                    acb->nb_sectors);
        }
    } else if (acb->op == SIM_WRITE) {
        acb->op = SIM_WRITE_CALLBACK;
        if (interactive_print) {
            printf ("Added WRITE_CALLBACK uuid=%" PRId64
                    "  filename=%s  sector_num=%" PRId64 "  nb_sectors=%d\n",
                    acb->uuid, acb->common.bs->filename, acb->sector_num,
                    acb->nb_sectors);
        }
    } else {
        fprintf (stderr, "Wrong op %d\n", acb->op);
        exit (1);
    }
}

void sim_list_tasks (void)
{
    SimAIOCB *acb;

    for (acb = head.next; acb != &head; acb = acb->next) {
        if (acb->op == SIM_READ) {
            printf ("uuid=%" PRId64 "  READ           file=%s  sector_num=%"
                    PRIu64 "  nb_sectors=%d\n", acb->uuid,
                    acb->common.bs->filename, acb->sector_num, acb->nb_sectors);
        } else if (acb->op == SIM_WRITE) {
            printf ("uuid=%" PRId64 "  WRITE          file=%s  sector_num=%"
                    PRIu64 "  nb_sectors=%d\n", acb->uuid,
                    acb->common.bs->filename, acb->sector_num, acb->nb_sectors);
        } else if (acb->op == SIM_READ_CALLBACK) {
            printf ("uuid=%" PRId64 "  CALLBACK READ  file=%s  sector_num=%"
                    PRIu64 "  nb_sectors=%d\n", acb->uuid,
                    acb->common.bs->filename, acb->sector_num, acb->nb_sectors);
        } else if (acb->op == SIM_WRITE_CALLBACK) {
            printf ("uuid=%" PRId64 "  CALLBACK WRITE file=%s  sector_num=%"
                    PRIu64 "  nb_sectors=%d\n", acb->uuid,
                    acb->common.bs->filename, acb->sector_num, acb->nb_sectors);
        } else {
            fprintf (stderr, "Wrong OP %d\n", acb->op);
            exit (1);
        }
    }
}

static inline void sim_callback (SimAIOCB * acb)
{
    ASSERT (disk_io_return_code == 0);
    FVD_DEBUG_ACB (acb->common.opaque);
    acb->common.cb (acb->common.opaque, acb->ret);
}

int64_t sim_get_time (void)
{
    return current_time;
}

void *sim_new_timer (void *cb, void *opaque)
{
    SimAIOCB *acb = my_qemu_aio_get (&sim_aio_pool, NULL, cb, opaque);
    acb->op = SIM_TIMER;
    acb->prev = NULL;
    return acb;
}

void sim_mod_timer (void *ts, int64_t expire_time)
{
    SimAIOCB *acb = ts;

    if (acb->prev) {
        /* Remove it first. */
        acb->next->prev = acb->prev;
        acb->prev->next = acb->next;
    }
    acb->time = expire_time;
    insert_in_list (acb);
}

void sim_free_timer (void *ts)
{
    SimAIOCB *acb = ts;
    CHECK_TASK (acb->uuid);
    my_qemu_aio_release (acb);
}

void sim_del_timer (void *ts)
{
    SimAIOCB *acb = ts;

    CHECK_TASK (acb->uuid);
    if (acb->prev) {
        /* Remove it from the list. */
        acb->next->prev = acb->prev;
        acb->prev->next = acb->next;

        /* Mark it as not in list. */
        acb->prev = NULL;
    }
}

void sim_set_disk_io_return_code (int ret)
{
    disk_io_return_code = ret;
}

static void sim_task_by_acb (SimAIOCB * acb)
{
    CHECK_TASK (acb->uuid);

    /* Remove it from the list. */
    acb->next->prev = acb->prev;
    acb->prev->next = acb->next;
    acb->prev = NULL;        /* Indicate that it is no longer in the list. */

    if (acb->time > current_time) {
        current_time = acb->time;
    }

    if (acb->op == SIM_TIMER) {
        QDEBUG ("SIM: execute task%" PRId64 " time=%" PRId64 " TIMER \n",
                acb->uuid, acb->time);

        FVD_DEBUG_ACB (acb->common.opaque);
        ((QEMUTimerCB *) acb->common.cb) (acb->common.opaque);
        return;
    }

    BlockDriverState *bs = acb->common.bs;

    if (acb->op == SIM_READ) {
        QDEBUG ("SIM: execute task%" PRId64 " time=%" PRId64
                " READ sector_num=%" PRId64 " nb_sectors=%d\n",
                acb->uuid, acb->time, acb->sector_num, acb->nb_sectors);

        if (acb->ret == 0) {
            if (acb->qiov->niov == 1) {
                if (sim_read
                    (bs, acb->sector_num, acb->qiov->iov->iov_base,
                     acb->nb_sectors) != 0) {
                    fprintf (stderr, "Error in reading %s sector_num=%" PRId64
                             " nb_sectors=%d\n", acb->common.bs->filename,
                             acb->sector_num, acb->nb_sectors);
                    exit (1);
                }
            } else {
                uint8_t *buf =
                    qemu_blockalign (acb->common.bs, acb->qiov->size);
                if (sim_read (bs, acb->sector_num, buf, acb->nb_sectors) != 0) {
                    fprintf (stderr, "Error in reading %s sector_num=%" PRId64
                             " nb_sectors=%d\n", acb->common.bs->filename,
                             acb->sector_num, acb->nb_sectors);
                    exit (1);
                }
                qemu_iovec_from_buffer (acb->qiov, buf, acb->qiov->size);
                qemu_vfree (buf);
            }
        }

        insert_aio_callback (acb);
    } else if (acb->op == SIM_WRITE) {
        QDEBUG ("SIM: execute task%" PRId64 " time=%" PRId64
                " WRITE sector_num=%" PRId64 " nb_sectors=%d\n",
                acb->uuid, acb->time, acb->sector_num, acb->nb_sectors);

        if (acb->ret == 0) {
            if (acb->qiov->niov == 1) {
                if (sim_write
                    (bs, acb->sector_num, acb->qiov->iov->iov_base,
                     acb->nb_sectors) != 0) {
                    fprintf (stderr, "Error in writing %s sector_num=%" PRId64
                             " nb_sectors=%d\n", acb->common.bs->filename,
                             acb->sector_num, acb->nb_sectors);
                    exit (1);
                }
            } else {
                uint8_t *buf = qemu_blockalign (acb->common.bs,
                                                acb->qiov->size);
                qemu_iovec_to_buffer (acb->qiov, buf);
                if (sim_write (bs, acb->sector_num, buf, acb->nb_sectors)!= 0) {
                    fprintf (stderr, "Error in writing %s sector_num=%" PRId64
                             " nb_sectors=%d\n", acb->common.bs->filename,
                             acb->sector_num, acb->nb_sectors);
                    exit (1);
                }
                qemu_vfree (buf);
            }
        }

        insert_aio_callback (acb);
    } else if (acb->op == SIM_FLUSH) {
        QDEBUG ("SIM: execute task%" PRId64 " time=%" PRId64 " FLUSH\n",
                acb->uuid, acb->time);
        /* Skip real flushing to speed up simulation:
         *         if (ret == 0) { * fdatasync (s->fd); } */
        insert_aio_callback (acb);
    } else if (acb->op == SIM_WRITE_CALLBACK || acb->op == SIM_READ_CALLBACK
               || acb->op == SIM_FLUSH_CALLBACK) {
        QDEBUG ("SIM: execute task%" PRId64 " time=%" PRId64 " CALLBACK\n",
                acb->uuid, acb->time);
        sim_callback (acb);
        CHECK_TASK (acb->uuid);
        my_qemu_aio_release (acb);
    } else {
        fprintf (stderr, "Unknown op %d\n", acb->op);
        exit (1);
    }
}

int sim_task_by_uuid (int64_t uuid)
{
    SimAIOCB *acb;

    for (acb = head.next; acb != &head; acb = acb->next) {
        if (acb->uuid == uuid) {
            sim_task_by_acb (acb);
            return 0;
        }
    }

    return -1;
}

int sim_all_tasks (void)
{
    int n = 0;

    while (1) {
        SimAIOCB *acb = head.next;
        if (acb == &head) {
            return n;
        }

        sim_task_by_acb (acb);
        n++;
    }
}

static BlockDriverAIOCB *sim_aio_readv (BlockDriverState * bs,
                                        int64_t sector_num,
                                        QEMUIOVector * qiov,
                                        int nb_sectors,
                                        BlockDriverCompletionFunc * cb,
                                        void *opaque)
{
    return insert_task (SIM_READ, bs, sector_num, qiov, nb_sectors, cb, opaque);
}

static BlockDriverAIOCB *sim_aio_writev (BlockDriverState * bs,
                                         int64_t sector_num,
                                         QEMUIOVector * qiov,
                                         int nb_sectors,
                                         BlockDriverCompletionFunc * cb,
                                         void *opaque)
{
    return insert_task (SIM_WRITE, bs, sector_num, qiov, nb_sectors, cb,
                        opaque);
}

static BlockDriverAIOCB *sim_aio_flush (BlockDriverState * bs,
                                        BlockDriverCompletionFunc * cb,
                                        void *opaque)
{
    return insert_task (SIM_FLUSH, bs, 0, NULL, 0, cb, opaque);
}

static void sim_aio_cancel (BlockDriverAIOCB * blockacb)
{
    SimAIOCB *acb = container_of (blockacb, SimAIOCB, common);

    CHECK_TASK (acb->uuid);

    if (acb->prev) {
        acb->next->prev = acb->prev;
        acb->prev->next = acb->next;
        acb->prev = NULL;
        my_qemu_aio_release (acb);
    } else {
        ASSERT (FALSE);        /* Cancel a task not in the list. */
    }
}

static int sim_probe (const uint8_t * buf, int buf_size, const char *filename)
{
    /* Return a score higher than RAW so that the image will be openned using
     * the 'sim' format. */
    return 2;
}

static int sim_open (BlockDriverState * bs, const char *filename,
                     int bdrv_flags)
{
    BDRVSimState *s = bs->opaque;
    int open_flags = O_BINARY | O_LARGEFILE;

    if ((bdrv_flags & BDRV_O_RDWR)) {
        open_flags |= O_RDWR;
    } else {
        open_flags |= O_RDONLY;
    }

    if ((bdrv_flags & BDRV_O_NOCACHE)) {
        open_flags |= O_DIRECT;
    } else if (!(bdrv_flags & BDRV_O_CACHE_WB)) {
        open_flags |= O_DSYNC;
    }

    /* Parse the "blksim:" prefix */
    if (!strncmp(filename, "blksim:", strlen("blksim:"))) {
        filename += strlen("blksim:");
    }

    s->fd = open (filename, open_flags);
    if (s->fd < 0)
        return -1;

    int64_t len = lseek (s->fd, 0, SEEK_END);
    if (len >= 0) {
        bs->total_sectors = len / 512;
    } else {
        bs->total_sectors = 0;
    }

    bs->growable = 1;
    return 0;
}

static void sim_close (BlockDriverState * bs)
{
    BDRVSimState *s = bs->opaque;
    close (s->fd);
}

static int sim_flush (BlockDriverState * bs)
{
    /*
     * Skip real flushing to speed up simulation.
         * BDRVSimState *s = bs->opaque;
         * fdatasync (s->fd);
     */
    return 0;
}

static int sim_has_zero_init (BlockDriverState * bs)
{
    struct stat buf;

    if (stat (bs->filename, &buf) != 0) {
        fprintf (stderr, "Failed to stat() %s\n", bs->filename);
        exit (1);
    }

    if (S_ISBLK (buf.st_mode) || S_ISCHR (buf.st_mode)) {
        return 0;
    }

    return 1;
}

static int sim_truncate (BlockDriverState * bs, int64_t offset)
{
    BDRVSimState *s = bs->opaque;
    return ftruncate (s->fd, offset);
}

BlockDriver bdrv_sim = {
    .format_name = "blksim",
    .protocol_name = "blksim",
    .instance_size = sizeof (BDRVSimState),
    .bdrv_probe = sim_probe,
    .bdrv_file_open = sim_open,
    .bdrv_close = sim_close,
    .bdrv_co_flush_to_disk = sim_flush,
    .bdrv_read = sim_read,
    .bdrv_write = sim_write,
    .bdrv_aio_readv = sim_aio_readv,
    .bdrv_aio_writev = sim_aio_writev,
    .bdrv_aio_flush = sim_aio_flush,
    .bdrv_has_zero_init = sim_has_zero_init,
    .bdrv_truncate = sim_truncate,
};

void enable_block_sim (int print, int64_t _rand_time)
{
    BlockDriver *drv = bdrv_find_format ("blksim");
    if (!drv) {
        bdrv_register (&bdrv_sim);
    }
    interactive_print = print;
    rand_time = _rand_time;
}

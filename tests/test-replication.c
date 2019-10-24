/*
 * Block replication tests
 *
 * Copyright (c) 2016 FUJITSU LIMITED
 * Author: Changlong Xie <xiecl.fnst@cn.fujitsu.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "qapi/error.h"
#include "qapi/qmp/qdict.h"
#include "qemu/option.h"
#include "qemu/main-loop.h"
#include "replication.h"
#include "block/block_int.h"
#include "block/qdict.h"
#include "sysemu/block-backend.h"

#define IMG_SIZE (64 * 1024 * 1024)

/* primary */
#define P_ID "primary-id"
static char p_local_disk[] = "/tmp/p_local_disk.XXXXXX";

/* secondary */
#define S_ID "secondary-id"
#define S_LOCAL_DISK_ID "secondary-local-disk-id"
static char s_local_disk[] = "/tmp/s_local_disk.XXXXXX";
static char s_active_disk[] = "/tmp/s_active_disk.XXXXXX";
static char s_hidden_disk[] = "/tmp/s_hidden_disk.XXXXXX";

/* FIXME: steal from blockdev.c */
QemuOptsList qemu_drive_opts = {
    .name = "drive",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_drive_opts.head),
    .desc = {
        { /* end of list */ }
    },
};

#define NOT_DONE 0x7fffffff

static void blk_rw_done(void *opaque, int ret)
{
    *(int *)opaque = ret;
}

static void test_blk_read(BlockBackend *blk, long pattern,
                          int64_t pattern_offset, int64_t pattern_count,
                          int64_t offset, int64_t count,
                          bool expect_failed)
{
    void *pattern_buf = NULL;
    QEMUIOVector qiov;
    void *cmp_buf = NULL;
    int async_ret = NOT_DONE;

    if (pattern) {
        cmp_buf = g_malloc(pattern_count);
        memset(cmp_buf, pattern, pattern_count);
    }

    pattern_buf = g_malloc(count);
    if (pattern) {
        memset(pattern_buf, pattern, count);
    } else {
        memset(pattern_buf, 0x00, count);
    }

    qemu_iovec_init(&qiov, 1);
    qemu_iovec_add(&qiov, pattern_buf, count);

    blk_aio_preadv(blk, offset, &qiov, 0, blk_rw_done, &async_ret);
    while (async_ret == NOT_DONE) {
        main_loop_wait(false);
    }

    if (expect_failed) {
        g_assert(async_ret != 0);
    } else {
        g_assert(async_ret == 0);
        if (pattern) {
            g_assert(memcmp(pattern_buf + pattern_offset,
                            cmp_buf, pattern_count) <= 0);
        }
    }

    g_free(pattern_buf);
    g_free(cmp_buf);
    qemu_iovec_destroy(&qiov);
}

static void test_blk_write(BlockBackend *blk, long pattern, int64_t offset,
                           int64_t count, bool expect_failed)
{
    void *pattern_buf = NULL;
    QEMUIOVector qiov;
    int async_ret = NOT_DONE;

    pattern_buf = g_malloc(count);
    if (pattern) {
        memset(pattern_buf, pattern, count);
    } else {
        memset(pattern_buf, 0x00, count);
    }

    qemu_iovec_init(&qiov, 1);
    qemu_iovec_add(&qiov, pattern_buf, count);

    blk_aio_pwritev(blk, offset, &qiov, 0, blk_rw_done, &async_ret);
    while (async_ret == NOT_DONE) {
        main_loop_wait(false);
    }

    if (expect_failed) {
        g_assert(async_ret != 0);
    } else {
        g_assert(async_ret == 0);
    }

    g_free(pattern_buf);
    qemu_iovec_destroy(&qiov);
}

/*
 * Create a uniquely-named empty temporary file.
 */
static void make_temp(char *template)
{
    int fd;

    fd = mkstemp(template);
    g_assert(fd >= 0);
    close(fd);
}

static void prepare_imgs(void)
{
    Error *local_err = NULL;

    make_temp(p_local_disk);
    make_temp(s_local_disk);
    make_temp(s_active_disk);
    make_temp(s_hidden_disk);

    /* Primary */
    bdrv_img_create(p_local_disk, "qcow2", NULL, NULL, NULL, IMG_SIZE,
                    BDRV_O_RDWR, true, &local_err);
    g_assert(!local_err);

    /* Secondary */
    bdrv_img_create(s_local_disk, "qcow2", NULL, NULL, NULL, IMG_SIZE,
                    BDRV_O_RDWR, true, &local_err);
    g_assert(!local_err);
    bdrv_img_create(s_active_disk, "qcow2", NULL, NULL, NULL, IMG_SIZE,
                    BDRV_O_RDWR, true, &local_err);
    g_assert(!local_err);
    bdrv_img_create(s_hidden_disk, "qcow2", NULL, NULL, NULL, IMG_SIZE,
                    BDRV_O_RDWR, true, &local_err);
    g_assert(!local_err);
}

static void cleanup_imgs(void)
{
    /* Primary */
    unlink(p_local_disk);

    /* Secondary */
    unlink(s_local_disk);
    unlink(s_active_disk);
    unlink(s_hidden_disk);
}

static BlockBackend *start_primary(void)
{
    BlockBackend *blk;
    QemuOpts *opts;
    QDict *qdict;
    Error *local_err = NULL;
    char *cmdline;

    cmdline = g_strdup_printf("driver=replication,mode=primary,node-name=xxx,"
                              "file.driver=qcow2,file.file.filename=%s,"
                              "file.file.locking=off"
                              , p_local_disk);
    opts = qemu_opts_parse_noisily(&qemu_drive_opts, cmdline, false);
    g_free(cmdline);

    qdict = qemu_opts_to_qdict(opts, NULL);
    qdict_set_default_str(qdict, BDRV_OPT_CACHE_DIRECT, "off");
    qdict_set_default_str(qdict, BDRV_OPT_CACHE_NO_FLUSH, "off");

    blk = blk_new_open(NULL, NULL, qdict, BDRV_O_RDWR, &local_err);
    g_assert(blk);
    g_assert(!local_err);

    monitor_add_blk(blk, P_ID, &local_err);
    g_assert(!local_err);

    qemu_opts_del(opts);

    return blk;
}

static void teardown_primary(void)
{
    BlockBackend *blk;
    AioContext *ctx;

    /* remove P_ID */
    blk = blk_by_name(P_ID);
    assert(blk);

    ctx = blk_get_aio_context(blk);
    aio_context_acquire(ctx);
    monitor_remove_blk(blk);
    blk_unref(blk);
    aio_context_release(ctx);
}

static void test_primary_read(void)
{
    BlockBackend *blk;

    blk = start_primary();

    /* read from 0 to IMG_SIZE */
    test_blk_read(blk, 0, 0, IMG_SIZE, 0, IMG_SIZE, true);

    teardown_primary();
}

static void test_primary_write(void)
{
    BlockBackend *blk;

    blk = start_primary();

    /* write from 0 to IMG_SIZE */
    test_blk_write(blk, 0, 0, IMG_SIZE, true);

    teardown_primary();
}

static void test_primary_start(void)
{
    BlockBackend *blk = NULL;
    Error *local_err = NULL;

    blk = start_primary();

    replication_start_all(REPLICATION_MODE_PRIMARY, &local_err);
    g_assert(!local_err);

    /* read from 0 to IMG_SIZE */
    test_blk_read(blk, 0, 0, IMG_SIZE, 0, IMG_SIZE, true);

    /* write 0x22 from 0 to IMG_SIZE */
    test_blk_write(blk, 0x22, 0, IMG_SIZE, false);

    teardown_primary();
}

static void test_primary_stop(void)
{
    Error *local_err = NULL;
    bool failover = true;

    start_primary();

    replication_start_all(REPLICATION_MODE_PRIMARY, &local_err);
    g_assert(!local_err);

    replication_stop_all(failover, &local_err);
    g_assert(!local_err);

    teardown_primary();
}

static void test_primary_do_checkpoint(void)
{
    Error *local_err = NULL;

    start_primary();

    replication_start_all(REPLICATION_MODE_PRIMARY, &local_err);
    g_assert(!local_err);

    replication_do_checkpoint_all(&local_err);
    g_assert(!local_err);

    teardown_primary();
}

static void test_primary_get_error_all(void)
{
    Error *local_err = NULL;

    start_primary();

    replication_start_all(REPLICATION_MODE_PRIMARY, &local_err);
    g_assert(!local_err);

    replication_get_error_all(&local_err);
    g_assert(!local_err);

    teardown_primary();
}

static BlockBackend *start_secondary(void)
{
    QemuOpts *opts;
    QDict *qdict;
    BlockBackend *blk;
    char *cmdline;
    Error *local_err = NULL;

    /* add s_local_disk and forge S_LOCAL_DISK_ID */
    cmdline = g_strdup_printf("file.filename=%s,driver=qcow2,"
                              "file.locking=off",
                              s_local_disk);
    opts = qemu_opts_parse_noisily(&qemu_drive_opts, cmdline, false);
    g_free(cmdline);

    qdict = qemu_opts_to_qdict(opts, NULL);
    qdict_set_default_str(qdict, BDRV_OPT_CACHE_DIRECT, "off");
    qdict_set_default_str(qdict, BDRV_OPT_CACHE_NO_FLUSH, "off");

    blk = blk_new_open(NULL, NULL, qdict, BDRV_O_RDWR, &local_err);
    assert(blk);
    monitor_add_blk(blk, S_LOCAL_DISK_ID, &local_err);
    g_assert(!local_err);

    /* format s_local_disk with pattern "0x11" */
    test_blk_write(blk, 0x11, 0, IMG_SIZE, false);

    qemu_opts_del(opts);

    /* add S_(ACTIVE/HIDDEN)_DISK and forge S_ID */
    cmdline = g_strdup_printf("driver=replication,mode=secondary,top-id=%s,"
                              "file.driver=qcow2,file.file.filename=%s,"
                              "file.file.locking=off,"
                              "file.backing.driver=qcow2,"
                              "file.backing.file.filename=%s,"
                              "file.backing.file.locking=off,"
                              "file.backing.backing=%s"
                              , S_ID, s_active_disk, s_hidden_disk
                              , S_LOCAL_DISK_ID);
    opts = qemu_opts_parse_noisily(&qemu_drive_opts, cmdline, false);
    g_free(cmdline);

    qdict = qemu_opts_to_qdict(opts, NULL);
    qdict_set_default_str(qdict, BDRV_OPT_CACHE_DIRECT, "off");
    qdict_set_default_str(qdict, BDRV_OPT_CACHE_NO_FLUSH, "off");

    blk = blk_new_open(NULL, NULL, qdict, BDRV_O_RDWR, &local_err);
    assert(blk);
    monitor_add_blk(blk, S_ID, &local_err);
    g_assert(!local_err);

    qemu_opts_del(opts);

    return blk;
}

static void teardown_secondary(void)
{
    /* only need to destroy two BBs */
    BlockBackend *blk;
    AioContext *ctx;

    /* remove S_LOCAL_DISK_ID */
    blk = blk_by_name(S_LOCAL_DISK_ID);
    assert(blk);

    ctx = blk_get_aio_context(blk);
    aio_context_acquire(ctx);
    monitor_remove_blk(blk);
    blk_unref(blk);
    aio_context_release(ctx);

    /* remove S_ID */
    blk = blk_by_name(S_ID);
    assert(blk);

    ctx = blk_get_aio_context(blk);
    aio_context_acquire(ctx);
    monitor_remove_blk(blk);
    blk_unref(blk);
    aio_context_release(ctx);
}

static void test_secondary_read(void)
{
    BlockBackend *blk;

    blk = start_secondary();

    /* read from 0 to IMG_SIZE */
    test_blk_read(blk, 0, 0, IMG_SIZE, 0, IMG_SIZE, true);

    teardown_secondary();
}

static void test_secondary_write(void)
{
    BlockBackend *blk;

    blk = start_secondary();

    /* write from 0 to IMG_SIZE */
    test_blk_write(blk, 0, 0, IMG_SIZE, true);

    teardown_secondary();
}

static void test_secondary_start(void)
{
    BlockBackend *top_blk, *local_blk;
    Error *local_err = NULL;
    bool failover = true;

    top_blk = start_secondary();
    replication_start_all(REPLICATION_MODE_SECONDARY, &local_err);
    g_assert(!local_err);

    /* read from s_local_disk (0, IMG_SIZE) */
    test_blk_read(top_blk, 0x11, 0, IMG_SIZE, 0, IMG_SIZE, false);

    /* write 0x22 to s_local_disk (IMG_SIZE / 2, IMG_SIZE) */
    local_blk = blk_by_name(S_LOCAL_DISK_ID);
    test_blk_write(local_blk, 0x22, IMG_SIZE / 2, IMG_SIZE / 2, false);

    /* replication will backup s_local_disk to s_hidden_disk */
    test_blk_read(top_blk, 0x11, IMG_SIZE / 2,
                  IMG_SIZE / 2, 0, IMG_SIZE, false);

    /* write 0x33 to s_active_disk (0, IMG_SIZE / 2) */
    test_blk_write(top_blk, 0x33, 0, IMG_SIZE / 2, false);

    /* read from s_active_disk (0, IMG_SIZE/2) */
    test_blk_read(top_blk, 0x33, 0, IMG_SIZE / 2,
                  0, IMG_SIZE / 2, false);

    /* unblock top_bs */
    replication_stop_all(failover, &local_err);
    g_assert(!local_err);

    teardown_secondary();
}


static void test_secondary_stop(void)
{
    BlockBackend *top_blk, *local_blk;
    Error *local_err = NULL;
    bool failover = true;

    top_blk = start_secondary();
    replication_start_all(REPLICATION_MODE_SECONDARY, &local_err);
    g_assert(!local_err);

    /* write 0x22 to s_local_disk (IMG_SIZE / 2, IMG_SIZE) */
    local_blk = blk_by_name(S_LOCAL_DISK_ID);
    test_blk_write(local_blk, 0x22, IMG_SIZE / 2, IMG_SIZE / 2, false);

    /* replication will backup s_local_disk to s_hidden_disk */
    test_blk_read(top_blk, 0x11, IMG_SIZE / 2,
                  IMG_SIZE / 2, 0, IMG_SIZE, false);

    /* write 0x33 to s_active_disk (0, IMG_SIZE / 2) */
    test_blk_write(top_blk, 0x33, 0, IMG_SIZE / 2, false);

    /* do active commit */
    replication_stop_all(failover, &local_err);
    g_assert(!local_err);

    /* read from s_local_disk (0, IMG_SIZE / 2) */
    test_blk_read(top_blk, 0x33, 0, IMG_SIZE / 2,
                  0, IMG_SIZE / 2, false);


    /* read from s_local_disk (IMG_SIZE / 2, IMG_SIZE) */
    test_blk_read(top_blk, 0x22, IMG_SIZE / 2,
                  IMG_SIZE / 2, 0, IMG_SIZE, false);

    teardown_secondary();
}

static void test_secondary_continuous_replication(void)
{
    BlockBackend *top_blk, *local_blk;
    Error *local_err = NULL;

    top_blk = start_secondary();
    replication_start_all(REPLICATION_MODE_SECONDARY, &local_err);
    g_assert(!local_err);

    /* write 0x22 to s_local_disk (IMG_SIZE / 2, IMG_SIZE) */
    local_blk = blk_by_name(S_LOCAL_DISK_ID);
    test_blk_write(local_blk, 0x22, IMG_SIZE / 2, IMG_SIZE / 2, false);

    /* replication will backup s_local_disk to s_hidden_disk */
    test_blk_read(top_blk, 0x11, IMG_SIZE / 2,
                  IMG_SIZE / 2, 0, IMG_SIZE, false);

    /* write 0x33 to s_active_disk (0, IMG_SIZE / 2) */
    test_blk_write(top_blk, 0x33, 0, IMG_SIZE / 2, false);

    /* do failover (active commit) */
    replication_stop_all(true, &local_err);
    g_assert(!local_err);

    /* it should ignore all requests from now on */

    /* start after failover */
    replication_start_all(REPLICATION_MODE_PRIMARY, &local_err);
    g_assert(!local_err);

    /* checkpoint */
    replication_do_checkpoint_all(&local_err);
    g_assert(!local_err);

    /* stop */
    replication_stop_all(true, &local_err);
    g_assert(!local_err);

    /* read from s_local_disk (0, IMG_SIZE / 2) */
    test_blk_read(top_blk, 0x33, 0, IMG_SIZE / 2,
                  0, IMG_SIZE / 2, false);


    /* read from s_local_disk (IMG_SIZE / 2, IMG_SIZE) */
    test_blk_read(top_blk, 0x22, IMG_SIZE / 2,
                  IMG_SIZE / 2, 0, IMG_SIZE, false);

    teardown_secondary();
}

static void test_secondary_do_checkpoint(void)
{
    BlockBackend *top_blk, *local_blk;
    Error *local_err = NULL;
    bool failover = true;

    top_blk = start_secondary();
    replication_start_all(REPLICATION_MODE_SECONDARY, &local_err);
    g_assert(!local_err);

    /* write 0x22 to s_local_disk (IMG_SIZE / 2, IMG_SIZE) */
    local_blk = blk_by_name(S_LOCAL_DISK_ID);
    test_blk_write(local_blk, 0x22, IMG_SIZE / 2,
                   IMG_SIZE / 2, false);

    /* replication will backup s_local_disk to s_hidden_disk */
    test_blk_read(top_blk, 0x11, IMG_SIZE / 2,
                  IMG_SIZE / 2, 0, IMG_SIZE, false);

    replication_do_checkpoint_all(&local_err);
    g_assert(!local_err);

    /* after checkpoint, read pattern 0x22 from s_local_disk */
    test_blk_read(top_blk, 0x22, IMG_SIZE / 2,
                  IMG_SIZE / 2, 0, IMG_SIZE, false);

    /* unblock top_bs */
    replication_stop_all(failover, &local_err);
    g_assert(!local_err);

    teardown_secondary();
}

static void test_secondary_get_error_all(void)
{
    Error *local_err = NULL;
    bool failover = true;

    start_secondary();
    replication_start_all(REPLICATION_MODE_SECONDARY, &local_err);
    g_assert(!local_err);

    replication_get_error_all(&local_err);
    g_assert(!local_err);

    /* unblock top_bs */
    replication_stop_all(failover, &local_err);
    g_assert(!local_err);

    teardown_secondary();
}

static void sigabrt_handler(int signo)
{
    cleanup_imgs();
}

static void setup_sigabrt_handler(void)
{
    struct sigaction sigact;

    sigact = (struct sigaction) {
        .sa_handler = sigabrt_handler,
        .sa_flags = SA_RESETHAND,
    };
    sigemptyset(&sigact.sa_mask);
    sigaction(SIGABRT, &sigact, NULL);
}

int main(int argc, char **argv)
{
    int ret;
    qemu_init_main_loop(&error_fatal);
    bdrv_init();

    g_test_init(&argc, &argv, NULL);
    setup_sigabrt_handler();

    prepare_imgs();

    /* Primary */
    g_test_add_func("/replication/primary/read",    test_primary_read);
    g_test_add_func("/replication/primary/write",   test_primary_write);
    g_test_add_func("/replication/primary/start",   test_primary_start);
    g_test_add_func("/replication/primary/stop",    test_primary_stop);
    g_test_add_func("/replication/primary/do_checkpoint",
                    test_primary_do_checkpoint);
    g_test_add_func("/replication/primary/get_error_all",
                    test_primary_get_error_all);

    /* Secondary */
    g_test_add_func("/replication/secondary/read",  test_secondary_read);
    g_test_add_func("/replication/secondary/write", test_secondary_write);
    g_test_add_func("/replication/secondary/start", test_secondary_start);
    g_test_add_func("/replication/secondary/stop",  test_secondary_stop);
    g_test_add_func("/replication/secondary/continuous_replication",
                    test_secondary_continuous_replication);
    g_test_add_func("/replication/secondary/do_checkpoint",
                    test_secondary_do_checkpoint);
    g_test_add_func("/replication/secondary/get_error_all",
                    test_secondary_get_error_all);

    ret = g_test_run();

    cleanup_imgs();

    return ret;
}

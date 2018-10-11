/*
 * Image locking tests
 *
 * Copyright (c) 2018 Red Hat Inc.
 *
 * Author: Fam Zheng <famz@redhat.com>
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
#include "block/block.h"
#include "sysemu/block-backend.h"
#include "qapi/error.h"
#include "qapi/qmp/qdict.h"

static BlockBackend *open_image(const char *path,
                                uint64_t perm, uint64_t shared_perm,
                                Error **errp)
{
    Error *local_err = NULL;
    BlockBackend *blk;
    QDict *options = qdict_new();

    qdict_put_str(options, "driver", "raw");
    blk = blk_new_open(path, NULL, options, BDRV_O_RDWR, &local_err);
    if (blk) {
        g_assert_null(local_err);
        if (blk_set_perm(blk, perm, shared_perm, errp)) {
            blk_unref(blk);
            blk = NULL;
        }
    } else {
        error_propagate(errp, local_err);
    }
    return blk;
}

static void check_locked_bytes(int fd, uint64_t perm_locks,
                               uint64_t shared_perm_locks)
{
    int i;

    if (!perm_locks && !shared_perm_locks) {
        g_assert(!qemu_lock_fd_test(fd, 0, 0, true));
        return;
    }
    for (i = 0; (1ULL << i) <= BLK_PERM_ALL; i++) {
        uint64_t bit = (1ULL << i);
        bool perm_expected = !!(bit & perm_locks);
        bool shared_perm_expected = !!(bit & shared_perm_locks);
        g_assert_cmpint(perm_expected, ==,
                        !!qemu_lock_fd_test(fd, 100 + i, 1, true));
        g_assert_cmpint(shared_perm_expected, ==,
                        !!qemu_lock_fd_test(fd, 200 + i, 1, true));
    }
}

static void test_image_locking_basic(void)
{
    BlockBackend *blk1, *blk2, *blk3;
    char img_path[] = "/tmp/qtest.XXXXXX";
    uint64_t perm, shared_perm;

    int fd = mkstemp(img_path);
    assert(fd >= 0);

    perm = BLK_PERM_WRITE | BLK_PERM_CONSISTENT_READ;
    shared_perm = BLK_PERM_ALL;
    blk1 = open_image(img_path, perm, shared_perm, &error_abort);
    g_assert(blk1);

    check_locked_bytes(fd, perm, ~shared_perm);

    /* compatible perm between blk1 and blk2 */
    blk2 = open_image(img_path, perm | BLK_PERM_RESIZE, shared_perm, NULL);
    g_assert(blk2);
    check_locked_bytes(fd, perm | BLK_PERM_RESIZE, ~shared_perm);

    /* incompatible perm with already open blk1 and blk2 */
    blk3 = open_image(img_path, perm, BLK_PERM_WRITE_UNCHANGED, NULL);
    g_assert_null(blk3);

    blk_unref(blk2);

    /* Check that extra bytes in blk2 are correctly unlocked */
    check_locked_bytes(fd, perm, ~shared_perm);

    blk_unref(blk1);

    /* Image is unused, no lock there */
    check_locked_bytes(fd, 0, 0);
    blk3 = open_image(img_path, perm, BLK_PERM_WRITE_UNCHANGED, &error_abort);
    g_assert(blk3);
    blk_unref(blk3);
    close(fd);
    unlink(img_path);
}

static void test_set_perm_abort(void)
{
    BlockBackend *blk1, *blk2;
    char img_path[] = "/tmp/qtest.XXXXXX";
    uint64_t perm, shared_perm;
    int r;
    int fd = mkstemp(img_path);
    assert(fd >= 0);

    perm = BLK_PERM_WRITE | BLK_PERM_CONSISTENT_READ;
    shared_perm = BLK_PERM_ALL;
    blk1 = open_image(img_path, perm, shared_perm, &error_abort);
    g_assert(blk1);

    blk2 = open_image(img_path, perm, shared_perm, &error_abort);
    g_assert(blk2);

    check_locked_bytes(fd, perm, ~shared_perm);

    /* A failed blk_set_perm mustn't change perm status (locked bytes) */
    r = blk_set_perm(blk2, perm | BLK_PERM_RESIZE, BLK_PERM_WRITE_UNCHANGED,
                     NULL);
    g_assert_cmpint(r, !=, 0);
    check_locked_bytes(fd, perm, ~shared_perm);
    blk_unref(blk1);
    blk_unref(blk2);
}

int main(int argc, char **argv)
{
    bdrv_init();
    qemu_init_main_loop(&error_abort);

    g_test_init(&argc, &argv, NULL);

    if (qemu_has_ofd_lock()) {
        g_test_add_func("/image-locking/basic", test_image_locking_basic);
        g_test_add_func("/image-locking/set-perm-abort", test_set_perm_abort);
    }

    return g_test_run();
}

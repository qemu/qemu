/*
 * Test block device write threshold
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "block/block_int.h"
#include "block/write-threshold.h"


static void test_threshold_not_trigger(void)
{
    uint64_t threshold = 4 * 1024 * 1024;
    BlockDriverState bs;

    memset(&bs, 0, sizeof(bs));

    bdrv_write_threshold_set(&bs, threshold);
    bdrv_write_threshold_check_write(&bs, 1024, 1024);
    g_assert_cmpuint(bdrv_write_threshold_get(&bs), ==, threshold);
}


static void test_threshold_trigger(void)
{
    uint64_t threshold = 4 * 1024 * 1024;
    BlockDriverState bs;

    memset(&bs, 0, sizeof(bs));

    bdrv_write_threshold_set(&bs, threshold);
    bdrv_write_threshold_check_write(&bs, threshold - 1024, 2 * 1024);
    g_assert_cmpuint(bdrv_write_threshold_get(&bs), ==, 0);
}


int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/write-threshold/not-trigger", test_threshold_not_trigger);
    g_test_add_func("/write-threshold/trigger", test_threshold_trigger);

    return g_test_run();
}

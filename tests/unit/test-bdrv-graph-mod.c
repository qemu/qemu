/*
 * Block node graph modifications tests
 *
 * Copyright (c) 2019 Virtuozzo International GmbH. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/main-loop.h"
#include "block/block_int.h"
#include "sysemu/block-backend.h"

static BlockDriver bdrv_pass_through = {
    .format_name = "pass-through",
    .bdrv_child_perm = bdrv_default_perms,
};

static void no_perm_default_perms(BlockDriverState *bs, BdrvChild *c,
                                         BdrvChildRole role,
                                         BlockReopenQueue *reopen_queue,
                                         uint64_t perm, uint64_t shared,
                                         uint64_t *nperm, uint64_t *nshared)
{
    *nperm = 0;
    *nshared = BLK_PERM_ALL;
}

static BlockDriver bdrv_no_perm = {
    .format_name = "no-perm",
    .bdrv_child_perm = no_perm_default_perms,
};

static BlockDriverState *no_perm_node(const char *name)
{
    return bdrv_new_open_driver(&bdrv_no_perm, name, BDRV_O_RDWR, &error_abort);
}

static BlockDriverState *pass_through_node(const char *name)
{
    return bdrv_new_open_driver(&bdrv_pass_through, name,
                                BDRV_O_RDWR, &error_abort);
}

/*
 * test_update_perm_tree
 *
 * When checking node for a possibility to update permissions, it's subtree
 * should be correctly checked too. New permissions for each node should be
 * calculated and checked in context of permissions of other nodes. If we
 * check new permissions of the node only in context of old permissions of
 * its neighbors, we can finish up with wrong permission graph.
 *
 * This test firstly create the following graph:
 *                                +--------+
 *                                |  root  |
 *                                +--------+
 *                                    |
 *                                    | perm: write, read
 *                                    | shared: except write
 *                                    v
 *  +-------------------+           +----------------+
 *  | passtrough filter |---------->|  null-co node  |
 *  +-------------------+           +----------------+
 *
 *
 * and then, tries to append filter under node. Expected behavior: fail.
 * Otherwise we'll get the following picture, with two BdrvChild'ren, having
 * write permission to one node, without actually sharing it.
 *
 *                     +--------+
 *                     |  root  |
 *                     +--------+
 *                         |
 *                         | perm: write, read
 *                         | shared: except write
 *                         v
 *                +-------------------+
 *                | passtrough filter |
 *                +-------------------+
 *                       |   |
 *     perm: write, read |   | perm: write, read
 *  shared: except write |   | shared: except write
 *                       v   v
 *                +----------------+
 *                |  null co node  |
 *                +----------------+
 */
static void test_update_perm_tree(void)
{
    int ret;

    BlockBackend *root = blk_new(qemu_get_aio_context(),
                                 BLK_PERM_WRITE | BLK_PERM_CONSISTENT_READ,
                                 BLK_PERM_ALL & ~BLK_PERM_WRITE);
    BlockDriverState *bs = no_perm_node("node");
    BlockDriverState *filter = pass_through_node("filter");

    blk_insert_bs(root, bs, &error_abort);

    bdrv_attach_child(filter, bs, "child", &child_of_bds,
                      BDRV_CHILD_FILTERED | BDRV_CHILD_PRIMARY, &error_abort);

    ret = bdrv_append(filter, bs, NULL);
    g_assert_cmpint(ret, <, 0);

    blk_unref(root);
}

/*
 * test_should_update_child
 *
 * Test that bdrv_replace_node, and concretely should_update_child
 * do the right thing, i.e. not creating loops on the graph.
 *
 * The test does the following:
 * 1. initial graph:
 *
 *   +------+          +--------+
 *   | root |          | filter |
 *   +------+          +--------+
 *      |                  |
 *  root|            target|
 *      v                  v
 *   +------+          +--------+
 *   | node |<---------| target |
 *   +------+  backing +--------+
 *
 * 2. Append @filter above @node. If should_update_child works correctly,
 * it understands, that backing child of @target should not be updated,
 * as it will create a loop on node graph. Resulting picture should
 * be the left one, not the right:
 *
 *     +------+                            +------+
 *     | root |                            | root |
 *     +------+                            +------+
 *        |                                   |
 *    root|                               root|
 *        v                                   v
 *    +--------+   target                 +--------+   target
 *    | filter |--------------+           | filter |--------------+
 *    +--------+              |           +--------+              |
 *        |                   |               |  ^                v
 * backing|                   |        backing|  |           +--------+
 *        v                   v               |  +-----------| target |
 *     +------+          +--------+           v      backing +--------+
 *     | node |<---------| target |        +------+
 *     +------+  backing +--------+        | node |
 *                                         +------+
 *
 *    (good picture)                       (bad picture)
 *
 */
static void test_should_update_child(void)
{
    BlockBackend *root = blk_new(qemu_get_aio_context(), 0, BLK_PERM_ALL);
    BlockDriverState *bs = no_perm_node("node");
    BlockDriverState *filter = no_perm_node("filter");
    BlockDriverState *target = no_perm_node("target");

    blk_insert_bs(root, bs, &error_abort);

    bdrv_set_backing_hd(target, bs, &error_abort);

    g_assert(target->backing->bs == bs);
    bdrv_attach_child(filter, target, "target", &child_of_bds,
                      BDRV_CHILD_DATA, &error_abort);
    bdrv_append(filter, bs, &error_abort);
    g_assert(target->backing->bs == bs);

    bdrv_unref(bs);
    blk_unref(root);
}

int main(int argc, char *argv[])
{
    bdrv_init();
    qemu_init_main_loop(&error_abort);

    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/bdrv-graph-mod/update-perm-tree", test_update_perm_tree);
    g_test_add_func("/bdrv-graph-mod/should-update-child",
                    test_should_update_child);

    return g_test_run();
}

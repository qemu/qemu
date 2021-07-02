/*
 * Block node graph modifications tests
 *
 * Copyright (c) 2019-2021 Virtuozzo International GmbH. All rights reserved.
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
    .supports_backing = true,
    .bdrv_child_perm = no_perm_default_perms,
};

static void exclusive_write_perms(BlockDriverState *bs, BdrvChild *c,
                                  BdrvChildRole role,
                                  BlockReopenQueue *reopen_queue,
                                  uint64_t perm, uint64_t shared,
                                  uint64_t *nperm, uint64_t *nshared)
{
    *nperm = BLK_PERM_WRITE;
    *nshared = BLK_PERM_ALL & ~BLK_PERM_WRITE;
}

static BlockDriver bdrv_exclusive_writer = {
    .format_name = "exclusive-writer",
    .bdrv_child_perm = exclusive_write_perms,
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

static BlockDriverState *exclusive_writer_node(const char *name)
{
    return bdrv_new_open_driver(&bdrv_exclusive_writer, name,
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

    bdrv_unref(filter);
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

    bdrv_unref(filter);
    bdrv_unref(bs);
    blk_unref(root);
}

/*
 * test_parallel_exclusive_write
 *
 * Check that when we replace node, old permissions of the node being removed
 * doesn't break the replacement.
 */
static void test_parallel_exclusive_write(void)
{
    BlockDriverState *top = exclusive_writer_node("top");
    BlockDriverState *base = no_perm_node("base");
    BlockDriverState *fl1 = pass_through_node("fl1");
    BlockDriverState *fl2 = pass_through_node("fl2");

    /*
     * bdrv_attach_child() eats child bs reference, so we need two @base
     * references for two filters:
     */
    bdrv_ref(base);

    bdrv_attach_child(top, fl1, "backing", &child_of_bds, BDRV_CHILD_DATA,
                      &error_abort);
    bdrv_attach_child(fl1, base, "backing", &child_of_bds, BDRV_CHILD_FILTERED,
                      &error_abort);
    bdrv_attach_child(fl2, base, "backing", &child_of_bds, BDRV_CHILD_FILTERED,
                      &error_abort);

    bdrv_replace_node(fl1, fl2, &error_abort);

    bdrv_unref(fl2);
    bdrv_unref(top);
}

static void write_to_file_perms(BlockDriverState *bs, BdrvChild *c,
                                     BdrvChildRole role,
                                     BlockReopenQueue *reopen_queue,
                                     uint64_t perm, uint64_t shared,
                                     uint64_t *nperm, uint64_t *nshared)
{
    if (bs->file && c == bs->file) {
        *nperm = BLK_PERM_WRITE;
        *nshared = BLK_PERM_ALL & ~BLK_PERM_WRITE;
    } else {
        *nperm = 0;
        *nshared = BLK_PERM_ALL;
    }
}

static BlockDriver bdrv_write_to_file = {
    .format_name = "tricky-perm",
    .bdrv_child_perm = write_to_file_perms,
};


/*
 * The following test shows that topological-sort order is required for
 * permission update, simple DFS is not enough.
 *
 * Consider the block driver which has two filter children: one active
 * with exclusive write access and one inactive with no specific
 * permissions.
 *
 * And, these two children has a common base child, like this:
 *
 * ┌─────┐     ┌──────┐
 * │ fl2 │ ◀── │ top  │
 * └─────┘     └──────┘
 *   │           │
 *   │           │ w
 *   │           ▼
 *   │         ┌──────┐
 *   │         │ fl1  │
 *   │         └──────┘
 *   │           │
 *   │           │ w
 *   │           ▼
 *   │         ┌──────┐
 *   └───────▶ │ base │
 *             └──────┘
 *
 * So, exclusive write is propagated.
 *
 * Assume, we want to make fl2 active instead of fl1.
 * So, we set some option for top driver and do permission update.
 *
 * With simple DFS, if permission update goes first through
 * top->fl1->base branch it will succeed: it firstly drop exclusive write
 * permissions and than apply them for another BdrvChildren.
 * But if permission update goes first through top->fl2->base branch it
 * will fail, as when we try to update fl2->base child, old not yet
 * updated fl1->base child will be in conflict.
 *
 * With topological-sort order we always update parents before children, so fl1
 * and fl2 are both updated when we update base and there is no conflict.
 */
static void test_parallel_perm_update(void)
{
    BlockDriverState *top = no_perm_node("top");
    BlockDriverState *tricky =
            bdrv_new_open_driver(&bdrv_write_to_file, "tricky", BDRV_O_RDWR,
                                 &error_abort);
    BlockDriverState *base = no_perm_node("base");
    BlockDriverState *fl1 = pass_through_node("fl1");
    BlockDriverState *fl2 = pass_through_node("fl2");
    BdrvChild *c_fl1, *c_fl2;

    /*
     * bdrv_attach_child() eats child bs reference, so we need two @base
     * references for two filters:
     */
    bdrv_ref(base);

    bdrv_attach_child(top, tricky, "file", &child_of_bds, BDRV_CHILD_DATA,
                      &error_abort);
    c_fl1 = bdrv_attach_child(tricky, fl1, "first", &child_of_bds,
                              BDRV_CHILD_FILTERED, &error_abort);
    c_fl2 = bdrv_attach_child(tricky, fl2, "second", &child_of_bds,
                              BDRV_CHILD_FILTERED, &error_abort);
    bdrv_attach_child(fl1, base, "backing", &child_of_bds, BDRV_CHILD_FILTERED,
                      &error_abort);
    bdrv_attach_child(fl2, base, "backing", &child_of_bds, BDRV_CHILD_FILTERED,
                      &error_abort);

    /* Select fl1 as first child to be active */
    tricky->file = c_fl1;
    bdrv_child_refresh_perms(top, top->children.lh_first, &error_abort);

    assert(c_fl1->perm & BLK_PERM_WRITE);
    assert(!(c_fl2->perm & BLK_PERM_WRITE));

    /* Now, try to switch active child and update permissions */
    tricky->file = c_fl2;
    bdrv_child_refresh_perms(top, top->children.lh_first, &error_abort);

    assert(c_fl2->perm & BLK_PERM_WRITE);
    assert(!(c_fl1->perm & BLK_PERM_WRITE));

    /* Switch once more, to not care about real child order in the list */
    tricky->file = c_fl1;
    bdrv_child_refresh_perms(top, top->children.lh_first, &error_abort);

    assert(c_fl1->perm & BLK_PERM_WRITE);
    assert(!(c_fl2->perm & BLK_PERM_WRITE));

    bdrv_unref(top);
}

/*
 * It's possible that filter required permissions allows to insert it to backing
 * chain, like:
 *
 *  1.  [top] -> [filter] -> [base]
 *
 * but doesn't allow to add it as a branch:
 *
 *  2.  [filter] --\
 *                 v
 *      [top] -> [base]
 *
 * So, inserting such filter should do all graph modifications and only then
 * update permissions. If we try to go through intermediate state [2] and update
 * permissions on it we'll fail.
 *
 * Let's check that bdrv_append() can append such a filter.
 */
static void test_append_greedy_filter(void)
{
    BlockDriverState *top = exclusive_writer_node("top");
    BlockDriverState *base = no_perm_node("base");
    BlockDriverState *fl = exclusive_writer_node("fl1");

    bdrv_attach_child(top, base, "backing", &child_of_bds, BDRV_CHILD_COW,
                      &error_abort);

    bdrv_append(fl, base, &error_abort);
    bdrv_unref(fl);
    bdrv_unref(top);
}

int main(int argc, char *argv[])
{
    bdrv_init();
    qemu_init_main_loop(&error_abort);

    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/bdrv-graph-mod/update-perm-tree", test_update_perm_tree);
    g_test_add_func("/bdrv-graph-mod/should-update-child",
                    test_should_update_child);
    g_test_add_func("/bdrv-graph-mod/parallel-perm-update",
                    test_parallel_perm_update);
    g_test_add_func("/bdrv-graph-mod/parallel-exclusive-write",
                    test_parallel_exclusive_write);
    g_test_add_func("/bdrv-graph-mod/append-greedy-filter",
                    test_append_greedy_filter);

    return g_test_run();
}

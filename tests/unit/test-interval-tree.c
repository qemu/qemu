/*
 * Test interval trees
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/interval-tree.h"

static IntervalTreeNode nodes[20];
static IntervalTreeRoot root;

static void rand_interval(IntervalTreeNode *n, uint64_t start, uint64_t last)
{
    gint32 s_ofs, l_ofs, l_max;

    if (last - start > INT32_MAX) {
        l_max = INT32_MAX;
    } else {
        l_max = last - start;
    }
    s_ofs = g_test_rand_int_range(0, l_max);
    l_ofs = g_test_rand_int_range(s_ofs, l_max);

    n->start = start + s_ofs;
    n->last = start + l_ofs;
}

static void test_empty(void)
{
    g_assert(root.rb_root.rb_node == NULL);
    g_assert(root.rb_leftmost == NULL);
    g_assert(interval_tree_iter_first(&root, 0, UINT64_MAX) == NULL);
}

static void test_find_one_point(void)
{
    /* Create a tree of a single node, which is the point [1,1]. */
    nodes[0].start = 1;
    nodes[0].last = 1;

    interval_tree_insert(&nodes[0], &root);

    g_assert(interval_tree_iter_first(&root, 0, 9) == &nodes[0]);
    g_assert(interval_tree_iter_next(&nodes[0], 0, 9) == NULL);
    g_assert(interval_tree_iter_first(&root, 0, 0) == NULL);
    g_assert(interval_tree_iter_next(&nodes[0], 0, 0) == NULL);
    g_assert(interval_tree_iter_first(&root, 0, 1) == &nodes[0]);
    g_assert(interval_tree_iter_first(&root, 1, 1) == &nodes[0]);
    g_assert(interval_tree_iter_first(&root, 1, 2) == &nodes[0]);
    g_assert(interval_tree_iter_first(&root, 2, 2) == NULL);

    interval_tree_remove(&nodes[0], &root);
    g_assert(root.rb_root.rb_node == NULL);
    g_assert(root.rb_leftmost == NULL);
}

static void test_find_two_point(void)
{
    IntervalTreeNode *find0, *find1;

    /* Create a tree of a two nodes, which are both the point [1,1]. */
    nodes[0].start = 1;
    nodes[0].last = 1;
    nodes[1] = nodes[0];

    interval_tree_insert(&nodes[0], &root);
    interval_tree_insert(&nodes[1], &root);

    find0 = interval_tree_iter_first(&root, 0, 9);
    g_assert(find0 == &nodes[0] || find0 == &nodes[1]);

    find1 = interval_tree_iter_next(find0, 0, 9);
    g_assert(find1 == &nodes[0] || find1 == &nodes[1]);
    g_assert(find0 != find1);

    interval_tree_remove(&nodes[1], &root);

    g_assert(interval_tree_iter_first(&root, 0, 9) == &nodes[0]);
    g_assert(interval_tree_iter_next(&nodes[0], 0, 9) == NULL);

    interval_tree_remove(&nodes[0], &root);
}

static void test_find_one_range(void)
{
    /* Create a tree of a single node, which is the range [1,8]. */
    nodes[0].start = 1;
    nodes[0].last = 8;

    interval_tree_insert(&nodes[0], &root);

    g_assert(interval_tree_iter_first(&root, 0, 9) == &nodes[0]);
    g_assert(interval_tree_iter_next(&nodes[0], 0, 9) == NULL);
    g_assert(interval_tree_iter_first(&root, 0, 0) == NULL);
    g_assert(interval_tree_iter_first(&root, 0, 1) == &nodes[0]);
    g_assert(interval_tree_iter_first(&root, 1, 1) == &nodes[0]);
    g_assert(interval_tree_iter_first(&root, 4, 6) == &nodes[0]);
    g_assert(interval_tree_iter_first(&root, 8, 8) == &nodes[0]);
    g_assert(interval_tree_iter_first(&root, 9, 9) == NULL);

    interval_tree_remove(&nodes[0], &root);
}

static void test_find_one_range_many(void)
{
    int i;

    /*
     * Create a tree of many nodes in [0,99] and [200,299],
     * but only one node with exactly [110,190].
     */
    nodes[0].start = 110;
    nodes[0].last = 190;

    for (i = 1; i < ARRAY_SIZE(nodes) / 2; ++i) {
        rand_interval(&nodes[i], 0, 99);
    }
    for (; i < ARRAY_SIZE(nodes); ++i) {
        rand_interval(&nodes[i], 200, 299);
    }

    for (i = 0; i < ARRAY_SIZE(nodes); ++i) {
        interval_tree_insert(&nodes[i], &root);
    }

    /* Test that we find exactly the one node. */
    g_assert(interval_tree_iter_first(&root, 100, 199) == &nodes[0]);
    g_assert(interval_tree_iter_next(&nodes[0], 100, 199) == NULL);
    g_assert(interval_tree_iter_first(&root, 100, 109) == NULL);
    g_assert(interval_tree_iter_first(&root, 100, 110) == &nodes[0]);
    g_assert(interval_tree_iter_first(&root, 111, 120) == &nodes[0]);
    g_assert(interval_tree_iter_first(&root, 111, 199) == &nodes[0]);
    g_assert(interval_tree_iter_first(&root, 190, 199) == &nodes[0]);
    g_assert(interval_tree_iter_first(&root, 192, 199) == NULL);

    /*
     * Test that if there are multiple matches, we return the one
     * with the minimal start.
     */
    g_assert(interval_tree_iter_first(&root, 100, 300) == &nodes[0]);

    /* Test that we don't find it after it is removed. */
    interval_tree_remove(&nodes[0], &root);
    g_assert(interval_tree_iter_first(&root, 100, 199) == NULL);

    for (i = 1; i < ARRAY_SIZE(nodes); ++i) {
        interval_tree_remove(&nodes[i], &root);
    }
}

static void test_find_many_range(void)
{
    IntervalTreeNode *find;
    int i, n;

    n = g_test_rand_int_range(ARRAY_SIZE(nodes) / 3, ARRAY_SIZE(nodes) / 2);

    /*
     * Create a fair few nodes in [2000,2999], with the others
     * distributed around.
     */
    for (i = 0; i < n; ++i) {
        rand_interval(&nodes[i], 2000, 2999);
    }
    for (; i < ARRAY_SIZE(nodes) * 2 / 3; ++i) {
        rand_interval(&nodes[i], 1000, 1899);
    }
    for (; i < ARRAY_SIZE(nodes); ++i) {
        rand_interval(&nodes[i], 3100, 3999);
    }

    for (i = 0; i < ARRAY_SIZE(nodes); ++i) {
        interval_tree_insert(&nodes[i], &root);
    }

    /* Test that we find all of the nodes. */
    find = interval_tree_iter_first(&root, 2000, 2999);
    for (i = 0; find != NULL; i++) {
        find = interval_tree_iter_next(find, 2000, 2999);
    }
    g_assert_cmpint(i, ==, n);

    g_assert(interval_tree_iter_first(&root,    0,  999) == NULL);
    g_assert(interval_tree_iter_first(&root, 1900, 1999) == NULL);
    g_assert(interval_tree_iter_first(&root, 3000, 3099) == NULL);
    g_assert(interval_tree_iter_first(&root, 4000, UINT64_MAX) == NULL);

    for (i = 0; i < ARRAY_SIZE(nodes); ++i) {
        interval_tree_remove(&nodes[i], &root);
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/interval-tree/empty", test_empty);
    g_test_add_func("/interval-tree/find-one-point", test_find_one_point);
    g_test_add_func("/interval-tree/find-two-point", test_find_two_point);
    g_test_add_func("/interval-tree/find-one-range", test_find_one_range);
    g_test_add_func("/interval-tree/find-one-range-many",
                    test_find_one_range_many);
    g_test_add_func("/interval-tree/find-many-range", test_find_many_range);

    return g_test_run();
}

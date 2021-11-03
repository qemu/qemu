/*
 * IOVA tree implementation based on GTree.
 *
 * Copyright 2018 Red Hat, Inc.
 *
 * Authors:
 *  Peter Xu <peterx@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 */

#include "qemu/osdep.h"
#include "qemu/iova-tree.h"

struct IOVATree {
    GTree *tree;
};

static int iova_tree_compare(gconstpointer a, gconstpointer b, gpointer data)
{
    const DMAMap *m1 = a, *m2 = b;

    if (m1->iova > m2->iova + m2->size) {
        return 1;
    }

    if (m1->iova + m1->size < m2->iova) {
        return -1;
    }

    /* Overlapped */
    return 0;
}

IOVATree *iova_tree_new(void)
{
    IOVATree *iova_tree = g_new0(IOVATree, 1);

    /* We don't have values actually, no need to free */
    iova_tree->tree = g_tree_new_full(iova_tree_compare, NULL, g_free, NULL);

    return iova_tree;
}

const DMAMap *iova_tree_find(const IOVATree *tree, const DMAMap *map)
{
    return g_tree_lookup(tree->tree, map);
}

const DMAMap *iova_tree_find_address(const IOVATree *tree, hwaddr iova)
{
    const DMAMap map = { .iova = iova, .size = 0 };

    return iova_tree_find(tree, &map);
}

static inline void iova_tree_insert_internal(GTree *gtree, DMAMap *range)
{
    /* Key and value are sharing the same range data */
    g_tree_insert(gtree, range, range);
}

int iova_tree_insert(IOVATree *tree, const DMAMap *map)
{
    DMAMap *new;

    if (map->iova + map->size < map->iova || map->perm == IOMMU_NONE) {
        return IOVA_ERR_INVALID;
    }

    /* We don't allow to insert range that overlaps with existings */
    if (iova_tree_find(tree, map)) {
        return IOVA_ERR_OVERLAP;
    }

    new = g_new0(DMAMap, 1);
    memcpy(new, map, sizeof(*new));
    iova_tree_insert_internal(tree->tree, new);

    return IOVA_OK;
}

static gboolean iova_tree_traverse(gpointer key, gpointer value,
                                gpointer data)
{
    iova_tree_iterator iterator = data;
    DMAMap *map = key;

    g_assert(key == value);

    return iterator(map);
}

void iova_tree_foreach(IOVATree *tree, iova_tree_iterator iterator)
{
    g_tree_foreach(tree->tree, iova_tree_traverse, iterator);
}

int iova_tree_remove(IOVATree *tree, const DMAMap *map)
{
    const DMAMap *overlap;

    while ((overlap = iova_tree_find(tree, map))) {
        g_tree_remove(tree->tree, overlap);
    }

    return IOVA_OK;
}

void iova_tree_destroy(IOVATree *tree)
{
    g_tree_destroy(tree->tree);
    g_free(tree);
}

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

/* Args to pass to iova_tree_alloc foreach function. */
struct IOVATreeAllocArgs {
    /* Size of the desired allocation */
    size_t new_size;

    /* The minimum address allowed in the allocation */
    hwaddr iova_begin;

    /* Map at the left of the hole, can be NULL if "this" is first one */
    const DMAMap *prev;

    /* Map at the right of the hole, can be NULL if "prev" is the last one */
    const DMAMap *this;

    /* If found, we fill in the IOVA here */
    hwaddr iova_result;

    /* Whether have we found a valid IOVA */
    bool iova_found;
};

typedef struct IOVATreeFindIOVAArgs {
    const DMAMap *needle;
    const DMAMap *result;
} IOVATreeFindIOVAArgs;

/**
 * Iterate args to the next hole
 *
 * @args: The alloc arguments
 * @next: The next mapping in the tree. Can be NULL to signal the last one
 */
static void iova_tree_alloc_args_iterate(struct IOVATreeAllocArgs *args,
                                         const DMAMap *next)
{
    args->prev = args->this;
    args->this = next;
}

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

static gboolean iova_tree_find_address_iterator(gpointer key, gpointer value,
                                                gpointer data)
{
    const DMAMap *map = key;
    IOVATreeFindIOVAArgs *args = data;
    const DMAMap *needle;

    g_assert(key == value);

    needle = args->needle;
    if (map->translated_addr + map->size < needle->translated_addr ||
        needle->translated_addr + needle->size < map->translated_addr) {
        return false;
    }

    args->result = map;
    return true;
}

const DMAMap *iova_tree_find_iova(const IOVATree *tree, const DMAMap *map)
{
    IOVATreeFindIOVAArgs args = {
        .needle = map,
    };

    g_tree_foreach(tree->tree, iova_tree_find_address_iterator, &args);
    return args.result;
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

void iova_tree_remove(IOVATree *tree, DMAMap map)
{
    const DMAMap *overlap;

    while ((overlap = iova_tree_find(tree, &map))) {
        g_tree_remove(tree->tree, overlap);
    }
}

/**
 * Try to find an unallocated IOVA range between prev and this elements.
 *
 * @args: Arguments to allocation
 *
 * Cases:
 *
 * (1) !prev, !this: No entries allocated, always succeed
 *
 * (2) !prev, this: We're iterating at the 1st element.
 *
 * (3) prev, !this: We're iterating at the last element.
 *
 * (4) prev, this: this is the most common case, we'll try to find a hole
 * between "prev" and "this" mapping.
 *
 * Note that this function assumes the last valid iova is HWADDR_MAX, but it
 * searches linearly so it's easy to discard the result if it's not the case.
 */
static void iova_tree_alloc_map_in_hole(struct IOVATreeAllocArgs *args)
{
    const DMAMap *prev = args->prev, *this = args->this;
    uint64_t hole_start, hole_last;

    if (this && this->iova + this->size < args->iova_begin) {
        return;
    }

    hole_start = MAX(prev ? prev->iova + prev->size + 1 : 0, args->iova_begin);
    hole_last = this ? this->iova : HWADDR_MAX;

    if (hole_last - hole_start > args->new_size) {
        args->iova_result = hole_start;
        args->iova_found = true;
    }
}

/**
 * Foreach dma node in the tree, compare if there is a hole with its previous
 * node (or minimum iova address allowed) and the node.
 *
 * @key: Node iterating
 * @value: Node iterating
 * @pargs: Struct to communicate with the outside world
 *
 * Return: false to keep iterating, true if needs break.
 */
static gboolean iova_tree_alloc_traverse(gpointer key, gpointer value,
                                         gpointer pargs)
{
    struct IOVATreeAllocArgs *args = pargs;
    DMAMap *node = value;

    assert(key == value);

    iova_tree_alloc_args_iterate(args, node);
    iova_tree_alloc_map_in_hole(args);
    return args->iova_found;
}

int iova_tree_alloc_map(IOVATree *tree, DMAMap *map, hwaddr iova_begin,
                        hwaddr iova_last)
{
    struct IOVATreeAllocArgs args = {
        .new_size = map->size,
        .iova_begin = iova_begin,
    };

    if (unlikely(iova_last < iova_begin)) {
        return IOVA_ERR_INVALID;
    }

    /*
     * Find a valid hole for the mapping
     *
     * Assuming low iova_begin, so no need to do a binary search to
     * locate the first node.
     *
     * TODO: Replace all this with g_tree_node_first/next/last when available
     * (from glib since 2.68). To do it with g_tree_foreach complicates the
     * code a lot.
     *
     */
    g_tree_foreach(tree->tree, iova_tree_alloc_traverse, &args);
    if (!args.iova_found) {
        /*
         * Either tree is empty or the last hole is still not checked.
         * g_tree_foreach does not compare (last, iova_last] range, so we check
         * it here.
         */
        iova_tree_alloc_args_iterate(&args, NULL);
        iova_tree_alloc_map_in_hole(&args);
    }

    if (!args.iova_found || args.iova_result + map->size > iova_last) {
        return IOVA_ERR_NOMEM;
    }

    map->iova = args.iova_result;
    return iova_tree_insert(tree, map);
}

void iova_tree_destroy(IOVATree *tree)
{
    g_tree_destroy(tree->tree);
    g_free(tree);
}

static int gpa_tree_compare(gconstpointer a, gconstpointer b, gpointer data)
{
    const DMAMap *m1 = a, *m2 = b;

    if (m1->translated_addr > m2->translated_addr + m2->size) {
        return 1;
    }

    if (m1->translated_addr + m1->size < m2->translated_addr) {
        return -1;
    }

    /* Overlapped */
    return 0;
}

IOVATree *gpa_tree_new(void)
{
    IOVATree *gpa_tree = g_new0(IOVATree, 1);

    gpa_tree->tree = g_tree_new_full(gpa_tree_compare, NULL, g_free, NULL);

    return gpa_tree;
}

int gpa_tree_insert(IOVATree *tree, const DMAMap *map)
{
    DMAMap *new;

    if (map->translated_addr + map->size < map->translated_addr ||
        map->perm == IOMMU_NONE) {
        return IOVA_ERR_INVALID;
    }

    /* We don't allow inserting ranges that overlap with existing ones */
    if (iova_tree_find(tree, map)) {
        return IOVA_ERR_OVERLAP;
    }

    new = g_new0(DMAMap, 1);
    memcpy(new, map, sizeof(*new));
    iova_tree_insert_internal(tree->tree, new);

    return IOVA_OK;
}

/*
 * An very simplified iova tree implementation based on GTree.
 *
 * Copyright 2018 Red Hat, Inc.
 *
 * Authors:
 *  Peter Xu <peterx@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 */
#ifndef IOVA_TREE_H
#define IOVA_TREE_H

/*
 * Currently the iova tree will only allow to keep ranges
 * information, and no extra user data is allowed for each element.  A
 * benefit is that we can merge adjacent ranges internally within the
 * tree.  It can save a lot of memory when the ranges are splitted but
 * mostly continuous.
 *
 * Note that current implementation does not provide any thread
 * protections.  Callers of the iova tree should be responsible
 * for the thread safety issue.
 */

#include "exec/memory.h"
#include "exec/hwaddr.h"

#define  IOVA_OK           (0)
#define  IOVA_ERR_INVALID  (-1) /* Invalid parameters */
#define  IOVA_ERR_OVERLAP  (-2) /* IOVA range overlapped */

typedef struct IOVATree IOVATree;
typedef struct DMAMap {
    hwaddr iova;
    hwaddr translated_addr;
    hwaddr size;                /* Inclusive */
    IOMMUAccessFlags perm;
} QEMU_PACKED DMAMap;
typedef gboolean (*iova_tree_iterator)(DMAMap *map);

/**
 * iova_tree_new:
 *
 * Create a new iova tree.
 *
 * Returns: the tree pointer when succeeded, or NULL if error.
 */
IOVATree *iova_tree_new(void);

/**
 * iova_tree_insert:
 *
 * @tree: the iova tree to insert
 * @map: the mapping to insert
 *
 * Insert an iova range to the tree.  If there is overlapped
 * ranges, IOVA_ERR_OVERLAP will be returned.
 *
 * Return: 0 if succeeded, or <0 if error.
 */
int iova_tree_insert(IOVATree *tree, DMAMap *map);

/**
 * iova_tree_remove:
 *
 * @tree: the iova tree to remove range from
 * @map: the map range to remove
 *
 * Remove mappings from the tree that are covered by the map range
 * provided.  The range does not need to be exactly what has inserted,
 * all the mappings that are included in the provided range will be
 * removed from the tree.  Here map->translated_addr is meaningless.
 *
 * Return: 0 if succeeded, or <0 if error.
 */
int iova_tree_remove(IOVATree *tree, DMAMap *map);

/**
 * iova_tree_find:
 *
 * @tree: the iova tree to search from
 * @map: the mapping to search
 *
 * Search for a mapping in the iova tree that overlaps with the
 * mapping range specified.  Only the first found mapping will be
 * returned.
 *
 * Return: DMAMap pointer if found, or NULL if not found.  Note that
 * the returned DMAMap pointer is maintained internally.  User should
 * only read the content but never modify or free the content.  Also,
 * user is responsible to make sure the pointer is valid (say, no
 * concurrent deletion in progress).
 */
DMAMap *iova_tree_find(IOVATree *tree, DMAMap *map);

/**
 * iova_tree_find_address:
 *
 * @tree: the iova tree to search from
 * @iova: the iova address to find
 *
 * Similar to iova_tree_find(), but it tries to find mapping with
 * range iova=iova & size=0.
 *
 * Return: same as iova_tree_find().
 */
DMAMap *iova_tree_find_address(IOVATree *tree, hwaddr iova);

/**
 * iova_tree_foreach:
 *
 * @tree: the iova tree to iterate on
 * @iterator: the interator for the mappings, return true to stop
 *
 * Iterate over the iova tree.
 *
 * Return: 1 if found any overlap, 0 if not, <0 if error.
 */
void iova_tree_foreach(IOVATree *tree, iova_tree_iterator iterator);

/**
 * iova_tree_destroy:
 *
 * @tree: the iova tree to destroy
 *
 * Destroy an existing iova tree.
 *
 * Return: None.
 */
void iova_tree_destroy(IOVATree *tree);

#endif

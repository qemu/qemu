/*
 * vhost software live migration iova tree
 *
 * SPDX-FileCopyrightText: Red Hat, Inc. 2021
 * SPDX-FileContributor: Author: Eugenio Pérez <eperezma@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_VIRTIO_VHOST_IOVA_TREE_H
#define HW_VIRTIO_VHOST_IOVA_TREE_H

#include "qemu/iova-tree.h"
#include "exec/memory.h"

typedef struct VhostIOVATree VhostIOVATree;

VhostIOVATree *vhost_iova_tree_new(uint64_t iova_first, uint64_t iova_last);
void vhost_iova_tree_delete(VhostIOVATree *iova_tree);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(VhostIOVATree, vhost_iova_tree_delete);

const DMAMap *vhost_iova_tree_find_iova(const VhostIOVATree *iova_tree,
                                        const DMAMap *map);
int vhost_iova_tree_map_alloc(VhostIOVATree *iova_tree, DMAMap *map);
void vhost_iova_tree_remove(VhostIOVATree *iova_tree, DMAMap map);

#endif

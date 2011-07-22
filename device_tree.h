/*
 * Header with function prototypes to help device tree manipulation using
 * libfdt. It also provides functions to read entries from device tree proc
 * interface.
 *
 * Copyright 2008 IBM Corporation.
 * Authors: Jerone Young <jyoung5@us.ibm.com>
 *          Hollis Blanchard <hollisb@us.ibm.com>
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 *
 */

#ifndef __DEVICE_TREE_H__
#define __DEVICE_TREE_H__

void *load_device_tree(const char *filename_path, int *sizep);

int qemu_devtree_setprop(void *fdt, const char *node_path,
                         const char *property, void *val_array, int size);
int qemu_devtree_setprop_cell(void *fdt, const char *node_path,
                              const char *property, uint32_t val);
int qemu_devtree_setprop_string(void *fdt, const char *node_path,
                                const char *property, const char *string);
int qemu_devtree_nop_node(void *fdt, const char *node_path);
int qemu_devtree_add_subnode(void *fdt, const char *name);

#endif /* __DEVICE_TREE_H__ */

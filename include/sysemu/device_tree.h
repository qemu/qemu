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

void *create_device_tree(int *sizep);
void *load_device_tree(const char *filename_path, int *sizep);

int qemu_devtree_setprop(void *fdt, const char *node_path,
                         const char *property, const void *val_array, int size);
int qemu_devtree_setprop_cell(void *fdt, const char *node_path,
                              const char *property, uint32_t val);
int qemu_devtree_setprop_u64(void *fdt, const char *node_path,
                             const char *property, uint64_t val);
int qemu_devtree_setprop_string(void *fdt, const char *node_path,
                                const char *property, const char *string);
int qemu_devtree_setprop_phandle(void *fdt, const char *node_path,
                                 const char *property,
                                 const char *target_node_path);
const void *qemu_devtree_getprop(void *fdt, const char *node_path,
                                 const char *property, int *lenp);
uint32_t qemu_devtree_getprop_cell(void *fdt, const char *node_path,
                                   const char *property);
uint32_t qemu_devtree_get_phandle(void *fdt, const char *path);
uint32_t qemu_devtree_alloc_phandle(void *fdt);
int qemu_devtree_nop_node(void *fdt, const char *node_path);
int qemu_devtree_add_subnode(void *fdt, const char *name);

#define qemu_devtree_setprop_cells(fdt, node_path, property, ...)             \
    do {                                                                      \
        uint32_t qdt_tmp[] = { __VA_ARGS__ };                                 \
        int i;                                                                \
                                                                              \
        for (i = 0; i < ARRAY_SIZE(qdt_tmp); i++) {                           \
            qdt_tmp[i] = cpu_to_be32(qdt_tmp[i]);                             \
        }                                                                     \
        qemu_devtree_setprop(fdt, node_path, property, qdt_tmp,               \
                             sizeof(qdt_tmp));                                \
    } while (0)

void qemu_devtree_dumpdtb(void *fdt, int size);

#endif /* __DEVICE_TREE_H__ */

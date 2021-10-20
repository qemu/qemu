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

#ifndef DEVICE_TREE_H
#define DEVICE_TREE_H

void *create_device_tree(int *sizep);
void *load_device_tree(const char *filename_path, int *sizep);
#ifdef CONFIG_LINUX
/**
 * load_device_tree_from_sysfs: reads the device tree information in the
 * /proc/device-tree directory and return the corresponding binary blob
 * buffer pointer. Asserts in case of error.
 */
void *load_device_tree_from_sysfs(void);
#endif

/**
 * qemu_fdt_node_path: return the paths of nodes matching a given
 * name and compat string
 * @fdt: pointer to the dt blob
 * @name: node name
 * @compat: compatibility string
 * @errp: handle to an error object
 *
 * returns a newly allocated NULL-terminated array of node paths.
 * Use g_strfreev() to free it. If one or more nodes were found, the
 * array contains the path of each node and the last element equals to
 * NULL. If there is no error but no matching node was found, the
 * returned array contains a single element equal to NULL. If an error
 * was encountered when parsing the blob, the function returns NULL
 *
 * @name may be NULL to wildcard names and only match compatibility
 * strings.
 */
char **qemu_fdt_node_path(void *fdt, const char *name, const char *compat,
                          Error **errp);

/**
 * qemu_fdt_node_unit_path: return the paths of nodes matching a given
 * node-name, ie. node-name and node-name@unit-address
 * @fdt: pointer to the dt blob
 * @name: node name
 * @errp: handle to an error object
 *
 * returns a newly allocated NULL-terminated array of node paths.
 * Use g_strfreev() to free it. If one or more nodes were found, the
 * array contains the path of each node and the last element equals to
 * NULL. If there is no error but no matching node was found, the
 * returned array contains a single element equal to NULL. If an error
 * was encountered when parsing the blob, the function returns NULL
 */
char **qemu_fdt_node_unit_path(void *fdt, const char *name, Error **errp);

int qemu_fdt_setprop(void *fdt, const char *node_path,
                     const char *property, const void *val, int size);
int qemu_fdt_setprop_cell(void *fdt, const char *node_path,
                          const char *property, uint32_t val);
int qemu_fdt_setprop_u64(void *fdt, const char *node_path,
                         const char *property, uint64_t val);
int qemu_fdt_setprop_string(void *fdt, const char *node_path,
                            const char *property, const char *string);

/**
 * qemu_fdt_setprop_string_array: set a string array property
 *
 * @fdt: pointer to the dt blob
 * @name: node name
 * @prop: property array
 * @array: pointer to an array of string pointers
 * @len: length of array
 *
 * assigns a string array to a property. This function converts and
 * array of strings to a sequential string with \0 separators before
 * setting the property.
 */
int qemu_fdt_setprop_string_array(void *fdt, const char *node_path,
                                  const char *prop, char **array, int len);

int qemu_fdt_setprop_phandle(void *fdt, const char *node_path,
                             const char *property,
                             const char *target_node_path);
/**
 * qemu_fdt_getprop: retrieve the value of a given property
 * @fdt: pointer to the device tree blob
 * @node_path: node path
 * @property: name of the property to find
 * @lenp: fdt error if any or length of the property on success
 * @errp: handle to an error object
 *
 * returns a pointer to the property on success and NULL on failure
 */
const void *qemu_fdt_getprop(void *fdt, const char *node_path,
                             const char *property, int *lenp,
                             Error **errp);
/**
 * qemu_fdt_getprop_cell: retrieve the value of a given 4 byte property
 * @fdt: pointer to the device tree blob
 * @node_path: node path
 * @property: name of the property to find
 * @lenp: fdt error if any or -EINVAL if the property size is different from
 *        4 bytes, or 4 (expected length of the property) upon success.
 * @errp: handle to an error object
 *
 * returns the property value on success
 */
uint32_t qemu_fdt_getprop_cell(void *fdt, const char *node_path,
                               const char *property, int *lenp,
                               Error **errp);
uint32_t qemu_fdt_get_phandle(void *fdt, const char *path);
uint32_t qemu_fdt_alloc_phandle(void *fdt);
int qemu_fdt_nop_node(void *fdt, const char *node_path);
int qemu_fdt_add_subnode(void *fdt, const char *name);
int qemu_fdt_add_path(void *fdt, const char *path);

#define qemu_fdt_setprop_cells(fdt, node_path, property, ...)                 \
    do {                                                                      \
        uint32_t qdt_tmp[] = { __VA_ARGS__ };                                 \
        int i;                                                                \
                                                                              \
        for (i = 0; i < ARRAY_SIZE(qdt_tmp); i++) {                           \
            qdt_tmp[i] = cpu_to_be32(qdt_tmp[i]);                             \
        }                                                                     \
        qemu_fdt_setprop(fdt, node_path, property, qdt_tmp,                   \
                         sizeof(qdt_tmp));                                    \
    } while (0)

void qemu_fdt_dumpdtb(void *fdt, int size);

/**
 * qemu_fdt_setprop_sized_cells_from_array:
 * @fdt: device tree blob
 * @node_path: node to set property on
 * @property: property to set
 * @numvalues: number of values
 * @values: array of number-of-cells, value pairs
 *
 * Set the specified property on the specified node in the device tree
 * to be an array of cells. The values of the cells are specified via
 * the values list, which alternates between "number of cells used by
 * this value" and "value".
 * number-of-cells must be either 1 or 2 (other values will result in
 * an error being returned). If a value is too large to fit in the
 * number of cells specified for it, an error is returned.
 *
 * This function is useful because device tree nodes often have cell arrays
 * which are either lists of addresses or lists of address,size tuples, but
 * the number of cells used for each element vary depending on the
 * #address-cells and #size-cells properties of their parent node.
 * If you know all your cell elements are one cell wide you can use the
 * simpler qemu_fdt_setprop_cells(). If you're not setting up the
 * array programmatically, qemu_fdt_setprop_sized_cells may be more
 * convenient.
 *
 * Return value: 0 on success, <0 on error.
 */
int qemu_fdt_setprop_sized_cells_from_array(void *fdt,
                                            const char *node_path,
                                            const char *property,
                                            int numvalues,
                                            uint64_t *values);

/**
 * qemu_fdt_setprop_sized_cells:
 * @fdt: device tree blob
 * @node_path: node to set property on
 * @property: property to set
 * @...: list of number-of-cells, value pairs
 *
 * Set the specified property on the specified node in the device tree
 * to be an array of cells. The values of the cells are specified via
 * the variable arguments, which alternates between "number of cells
 * used by this value" and "value".
 *
 * This is a convenience wrapper for the function
 * qemu_fdt_setprop_sized_cells_from_array().
 *
 * Return value: 0 on success, <0 on error.
 */
#define qemu_fdt_setprop_sized_cells(fdt, node_path, property, ...)       \
    ({                                                                    \
        uint64_t qdt_tmp[] = { __VA_ARGS__ };                             \
        qemu_fdt_setprop_sized_cells_from_array(fdt, node_path,           \
                                                property,                 \
                                                ARRAY_SIZE(qdt_tmp) / 2,  \
                                                qdt_tmp);                 \
    })

#define FDT_PCI_RANGE_RELOCATABLE          0x80000000
#define FDT_PCI_RANGE_PREFETCHABLE         0x40000000
#define FDT_PCI_RANGE_ALIASED              0x20000000
#define FDT_PCI_RANGE_TYPE_MASK            0x03000000
#define FDT_PCI_RANGE_MMIO_64BIT           0x03000000
#define FDT_PCI_RANGE_MMIO                 0x02000000
#define FDT_PCI_RANGE_IOPORT               0x01000000
#define FDT_PCI_RANGE_CONFIG               0x00000000

#endif /* DEVICE_TREE_H */

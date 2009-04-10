/*
 * Functions to help device tree manipulation using libfdt.
 * It also provides functions to read entries from device tree proc
 * interface.
 *
 * Copyright 2008 IBM Corporation.
 * Authors: Jerone Young <jyoung5@us.ibm.com>
 *          Hollis Blanchard <hollisb@us.ibm.com>
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 *
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#include "config.h"
#include "qemu-common.h"
#include "sysemu.h"
#include "device_tree.h"

#include <libfdt.h>

void *load_device_tree(const char *filename_path, int *sizep)
{
    int dt_size;
    int dt_file_load_size;
    int new_dt_size;
    int ret;
    void *fdt = NULL;

    *sizep = 0;
    dt_size = get_image_size(filename_path);
    if (dt_size < 0) {
        printf("Unable to get size of device tree file '%s'\n",
            filename_path);
        goto fail;
    }

    /* Expand to 2x size to give enough room for manipulation.  */
    dt_size *= 2;
    /* First allocate space in qemu for device tree */
    fdt = qemu_mallocz(dt_size);

    dt_file_load_size = load_image(filename_path, fdt);
    if (dt_file_load_size < 0) {
        printf("Unable to open device tree file '%s'\n",
               filename_path);
        goto fail;
    }

    ret = fdt_open_into(fdt, fdt, dt_size);
    if (ret) {
        printf("Unable to copy device tree in memory\n");
        goto fail;
    }

    /* Check sanity of device tree */
    if (fdt_check_header(fdt)) {
        printf ("Device tree file loaded into memory is invalid: %s\n",
            filename_path);
        goto fail;
    }
    *sizep = dt_size;
    return fdt;

fail:
    qemu_free(fdt);
    return NULL;
}

int qemu_devtree_setprop(void *fdt, const char *node_path,
                         const char *property, uint32_t *val_array, int size)
{
    int offset;

    offset = fdt_path_offset(fdt, node_path);
    if (offset < 0)
        return offset;

    return fdt_setprop(fdt, offset, property, val_array, size);
}

int qemu_devtree_setprop_cell(void *fdt, const char *node_path,
                              const char *property, uint32_t val)
{
    int offset;

    offset = fdt_path_offset(fdt, node_path);
    if (offset < 0)
        return offset;

    return fdt_setprop_cell(fdt, offset, property, val);
}

int qemu_devtree_setprop_string(void *fdt, const char *node_path,
                                const char *property, const char *string)
{
    int offset;

    offset = fdt_path_offset(fdt, node_path);
    if (offset < 0)
        return offset;

    return fdt_setprop_string(fdt, offset, property, string);
}

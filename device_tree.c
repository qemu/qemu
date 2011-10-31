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
#include "device_tree.h"
#include "hw/loader.h"

#include <libfdt.h>

void *load_device_tree(const char *filename_path, int *sizep)
{
    int dt_size;
    int dt_file_load_size;
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
    dt_size += 10000;
    dt_size *= 2;
    /* First allocate space in qemu for device tree */
    fdt = g_malloc0(dt_size);

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
    g_free(fdt);
    return NULL;
}

static int findnode_nofail(void *fdt, const char *node_path)
{
    int offset;

    offset = fdt_path_offset(fdt, node_path);
    if (offset < 0) {
        fprintf(stderr, "%s Couldn't find node %s: %s\n", __func__, node_path,
                fdt_strerror(offset));
        exit(1);
    }

    return offset;
}

int qemu_devtree_setprop(void *fdt, const char *node_path,
                         const char *property, void *val_array, int size)
{
    int r;

    r = fdt_setprop(fdt, findnode_nofail(fdt, node_path), property, val_array, size);
    if (r < 0) {
        fprintf(stderr, "%s: Couldn't set %s/%s: %s\n", __func__, node_path,
                property, fdt_strerror(r));
        exit(1);
    }

    return r;
}

int qemu_devtree_setprop_cell(void *fdt, const char *node_path,
                              const char *property, uint32_t val)
{
    int r;

    r = fdt_setprop_cell(fdt, findnode_nofail(fdt, node_path), property, val);
    if (r < 0) {
        fprintf(stderr, "%s: Couldn't set %s/%s = %#08x: %s\n", __func__,
                node_path, property, val, fdt_strerror(r));
        exit(1);
    }

    return r;
}

int qemu_devtree_setprop_string(void *fdt, const char *node_path,
                                const char *property, const char *string)
{
    int r;

    r = fdt_setprop_string(fdt, findnode_nofail(fdt, node_path), property, string);
    if (r < 0) {
        fprintf(stderr, "%s: Couldn't set %s/%s = %s: %s\n", __func__,
                node_path, property, string, fdt_strerror(r));
        exit(1);
    }

    return r;
}

int qemu_devtree_nop_node(void *fdt, const char *node_path)
{
    int r;

    r = fdt_nop_node(fdt, findnode_nofail(fdt, node_path));
    if (r < 0) {
        fprintf(stderr, "%s: Couldn't nop node %s: %s\n", __func__, node_path,
                fdt_strerror(r));
        exit(1);
    }

    return r;
}

int qemu_devtree_add_subnode(void *fdt, const char *name)
{
    char *dupname = g_strdup(name);
    char *basename = strrchr(dupname, '/');
    int retval;

    if (!basename) {
        g_free(dupname);
        return -1;
    }

    basename[0] = '\0';
    basename++;

    retval = fdt_add_subnode(fdt, findnode_nofail(fdt, dupname), basename);
    if (retval < 0) {
        fprintf(stderr, "FDT: Failed to create subnode %s: %s\n", name,
                fdt_strerror(retval));
        exit(1);
    }

    g_free(dupname);
    return retval;
}

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

#include "qemu/osdep.h"

#ifdef CONFIG_LINUX
#include <dirent.h>
#endif

#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/option.h"
#include "qemu/bswap.h"
#include "qemu/cutils.h"
#include "qemu/guest-random.h"
#include "system/device_tree.h"
#include "hw/loader.h"
#include "hw/boards.h"
#include "qemu/config-file.h"
#include "qapi/qapi-commands-machine.h"
#include "qobject/qdict.h"
#include "monitor/hmp.h"

#include <libfdt.h>

#define FDT_MAX_SIZE  0x100000

void *create_device_tree(int *sizep)
{
    void *fdt;
    int ret;

    *sizep = FDT_MAX_SIZE;
    fdt = g_malloc0(FDT_MAX_SIZE);
    ret = fdt_create(fdt, FDT_MAX_SIZE);
    if (ret < 0) {
        goto fail;
    }
    ret = fdt_finish_reservemap(fdt);
    if (ret < 0) {
        goto fail;
    }
    ret = fdt_begin_node(fdt, "");
    if (ret < 0) {
        goto fail;
    }
    ret = fdt_end_node(fdt);
    if (ret < 0) {
        goto fail;
    }
    ret = fdt_finish(fdt);
    if (ret < 0) {
        goto fail;
    }
    ret = fdt_open_into(fdt, fdt, *sizep);
    if (ret) {
        error_report("%s: Unable to copy device tree into memory: %s",
                     __func__, fdt_strerror(ret));
        exit(1);
    }

    return fdt;
fail:
    error_report("%s Couldn't create dt: %s", __func__, fdt_strerror(ret));
    exit(1);
}

void *load_device_tree(const char *filename_path, int *sizep)
{
    int dt_size;
    int dt_file_load_size;
    int ret;
    void *fdt = NULL;

    *sizep = 0;
    dt_size = get_image_size(filename_path);
    if (dt_size < 0) {
        error_report("Unable to get size of device tree file '%s'",
                     filename_path);
        goto fail;
    }
    if (dt_size > INT_MAX / 2 - 10000) {
        error_report("Device tree file '%s' is too large", filename_path);
        goto fail;
    }

    /* Expand to 2x size to give enough room for manipulation.  */
    dt_size += 10000;
    dt_size *= 2;
    /* First allocate space in qemu for device tree */
    fdt = g_malloc0(dt_size);

    dt_file_load_size = load_image_size(filename_path, fdt, dt_size);
    if (dt_file_load_size < 0) {
        error_report("Unable to open device tree file '%s'",
                     filename_path);
        goto fail;
    }

    ret = fdt_open_into(fdt, fdt, dt_size);
    if (ret) {
        error_report("%s: Unable to copy device tree into memory: %s",
                     __func__, fdt_strerror(ret));
        goto fail;
    }

    /* Check sanity of device tree */
    if (fdt_check_header(fdt)) {
        error_report("Device tree file loaded into memory is invalid: %s",
                     filename_path);
        goto fail;
    }
    *sizep = dt_size;
    return fdt;

fail:
    g_free(fdt);
    return NULL;
}

#ifdef CONFIG_LINUX

#define SYSFS_DT_BASEDIR "/proc/device-tree"

/**
 * read_fstree: this function is inspired from dtc read_fstree
 * @fdt: preallocated fdt blob buffer, to be populated
 * @dirname: directory to scan under SYSFS_DT_BASEDIR
 * the search is recursive and the tree is searched down to the
 * leaves (property files).
 *
 * the function asserts in case of error
 */
static void read_fstree(void *fdt, const char *dirname)
{
    DIR *d;
    struct dirent *de;
    struct stat st;
    const char *root_dir = SYSFS_DT_BASEDIR;
    const char *parent_node;

    if (strstr(dirname, root_dir) != dirname) {
        error_report("%s: %s must be searched within %s",
                     __func__, dirname, root_dir);
        exit(1);
    }
    parent_node = &dirname[strlen(SYSFS_DT_BASEDIR)];

    d = opendir(dirname);
    if (!d) {
        error_report("%s cannot open %s", __func__, dirname);
        exit(1);
    }

    while ((de = readdir(d)) != NULL) {
        char *tmpnam;

        if (!g_strcmp0(de->d_name, ".")
            || !g_strcmp0(de->d_name, "..")) {
            continue;
        }

        tmpnam = g_strdup_printf("%s/%s", dirname, de->d_name);

        if (lstat(tmpnam, &st) < 0) {
            error_report("%s cannot lstat %s", __func__, tmpnam);
            exit(1);
        }

        if (S_ISREG(st.st_mode)) {
            gchar *val;
            gsize len;

            if (!g_file_get_contents(tmpnam, &val, &len, NULL)) {
                error_report("%s not able to extract info from %s",
                             __func__, tmpnam);
                exit(1);
            }

            if (strlen(parent_node) > 0) {
                qemu_fdt_setprop(fdt, parent_node,
                                 de->d_name, val, len);
            } else {
                qemu_fdt_setprop(fdt, "/", de->d_name, val, len);
            }
            g_free(val);
        } else if (S_ISDIR(st.st_mode)) {
            char *node_name;

            node_name = g_strdup_printf("%s/%s",
                                        parent_node, de->d_name);
            qemu_fdt_add_subnode(fdt, node_name);
            g_free(node_name);
            read_fstree(fdt, tmpnam);
        }

        g_free(tmpnam);
    }

    closedir(d);
}

/* load_device_tree_from_sysfs: extract the dt blob from host sysfs */
void *load_device_tree_from_sysfs(void)
{
    void *host_fdt;
    int host_fdt_size;

    host_fdt = create_device_tree(&host_fdt_size);
    read_fstree(host_fdt, SYSFS_DT_BASEDIR);
    if (fdt_check_header(host_fdt)) {
        error_report("%s host device tree extracted into memory is invalid",
                     __func__);
        exit(1);
    }
    return host_fdt;
}

#endif /* CONFIG_LINUX */

static int findnode_nofail(void *fdt, const char *node_path)
{
    int offset;

    offset = fdt_path_offset(fdt, node_path);
    if (offset < 0) {
        error_report("%s Couldn't find node %s: %s", __func__, node_path,
                     fdt_strerror(offset));
        exit(1);
    }

    return offset;
}

char **qemu_fdt_node_unit_path(void *fdt, const char *name, Error **errp)
{
    char *prefix =  g_strdup_printf("%s@", name);
    unsigned int path_len = 16, n = 0;
    GSList *path_list = NULL, *iter;
    const char *iter_name;
    int offset, len, ret;
    char **path_array;

    offset = fdt_next_node(fdt, -1, NULL);

    while (offset >= 0) {
        iter_name = fdt_get_name(fdt, offset, &len);
        if (!iter_name) {
            offset = len;
            break;
        }
        if (!strcmp(iter_name, name) || g_str_has_prefix(iter_name, prefix)) {
            char *path;

            path = g_malloc(path_len);
            while ((ret = fdt_get_path(fdt, offset, path, path_len))
                  == -FDT_ERR_NOSPACE) {
                path_len += 16;
                path = g_realloc(path, path_len);
            }
            path_list = g_slist_prepend(path_list, path);
            n++;
        }
        offset = fdt_next_node(fdt, offset, NULL);
    }
    g_free(prefix);

    if (offset < 0 && offset != -FDT_ERR_NOTFOUND) {
        error_setg(errp, "%s: abort parsing dt for %s node units: %s",
                   __func__, name, fdt_strerror(offset));
        for (iter = path_list; iter; iter = iter->next) {
            g_free(iter->data);
        }
        g_slist_free(path_list);
        return NULL;
    }

    path_array = g_new(char *, n + 1);
    path_array[n--] = NULL;

    for (iter = path_list; iter; iter = iter->next) {
        path_array[n--] = iter->data;
    }

    g_slist_free(path_list);

    return path_array;
}

char **qemu_fdt_node_path(void *fdt, const char *name, const char *compat,
                          Error **errp)
{
    int offset, len, ret;
    const char *iter_name;
    unsigned int path_len = 16, n = 0;
    GSList *path_list = NULL, *iter;
    char **path_array;

    offset = fdt_node_offset_by_compatible(fdt, -1, compat);

    while (offset >= 0) {
        iter_name = fdt_get_name(fdt, offset, &len);
        if (!iter_name) {
            offset = len;
            break;
        }
        if (!name || !strcmp(iter_name, name)) {
            char *path;

            path = g_malloc(path_len);
            while ((ret = fdt_get_path(fdt, offset, path, path_len))
                  == -FDT_ERR_NOSPACE) {
                path_len += 16;
                path = g_realloc(path, path_len);
            }
            path_list = g_slist_prepend(path_list, path);
            n++;
        }
        offset = fdt_node_offset_by_compatible(fdt, offset, compat);
    }

    if (offset < 0 && offset != -FDT_ERR_NOTFOUND) {
        error_setg(errp, "%s: abort parsing dt for %s/%s: %s",
                   __func__, name, compat, fdt_strerror(offset));
        for (iter = path_list; iter; iter = iter->next) {
            g_free(iter->data);
        }
        g_slist_free(path_list);
        return NULL;
    }

    path_array = g_new(char *, n + 1);
    path_array[n--] = NULL;

    for (iter = path_list; iter; iter = iter->next) {
        path_array[n--] = iter->data;
    }

    g_slist_free(path_list);

    return path_array;
}

int qemu_fdt_setprop(void *fdt, const char *node_path,
                     const char *property, const void *val, int size)
{
    int r;

    r = fdt_setprop(fdt, findnode_nofail(fdt, node_path), property, val, size);
    if (r < 0) {
        error_report("%s: Couldn't set %s/%s: %s", __func__, node_path,
                     property, fdt_strerror(r));
        exit(1);
    }

    return r;
}

int qemu_fdt_setprop_cell(void *fdt, const char *node_path,
                          const char *property, uint32_t val)
{
    int r;

    r = fdt_setprop_cell(fdt, findnode_nofail(fdt, node_path), property, val);
    if (r < 0) {
        error_report("%s: Couldn't set %s/%s = %#08x: %s", __func__,
                     node_path, property, val, fdt_strerror(r));
        exit(1);
    }

    return r;
}

int qemu_fdt_setprop_u64(void *fdt, const char *node_path,
                         const char *property, uint64_t val)
{
    val = cpu_to_be64(val);
    return qemu_fdt_setprop(fdt, node_path, property, &val, sizeof(val));
}

int qemu_fdt_setprop_string(void *fdt, const char *node_path,
                            const char *property, const char *string)
{
    int r;

    r = fdt_setprop_string(fdt, findnode_nofail(fdt, node_path), property, string);
    if (r < 0) {
        error_report("%s: Couldn't set %s/%s = %s: %s", __func__,
                     node_path, property, string, fdt_strerror(r));
        exit(1);
    }

    return r;
}

/*
 * libfdt doesn't allow us to add string arrays directly but they are
 * test a series of null terminated strings with a length. We build
 * the string up here so we can calculate the final length.
 */
int qemu_fdt_setprop_string_array(void *fdt, const char *node_path,
                                  const char *prop, char **array, int len)
{
    int ret, i, total_len = 0;
    char *str, *p;
    for (i = 0; i < len; i++) {
        total_len += strlen(array[i]) + 1;
    }
    p = str = g_malloc0(total_len);
    for (i = 0; i < len; i++) {
        int offset = strlen(array[i]) + 1;
        pstrcpy(p, offset, array[i]);
        p += offset;
    }

    ret = qemu_fdt_setprop(fdt, node_path, prop, str, total_len);
    g_free(str);
    return ret;
}

const void *qemu_fdt_getprop(void *fdt, const char *node_path,
                             const char *property, int *lenp, Error **errp)
{
    int len;
    const void *r;

    if (!lenp) {
        lenp = &len;
    }
    r = fdt_getprop(fdt, findnode_nofail(fdt, node_path), property, lenp);
    if (!r) {
        error_setg(errp, "%s: Couldn't get %s/%s: %s", __func__,
                  node_path, property, fdt_strerror(*lenp));
    }
    return r;
}

uint32_t qemu_fdt_getprop_cell(void *fdt, const char *node_path,
                               const char *property, int *lenp, Error **errp)
{
    int len;
    const uint32_t *p;

    if (!lenp) {
        lenp = &len;
    }
    p = qemu_fdt_getprop(fdt, node_path, property, lenp, errp);
    if (!p) {
        return 0;
    } else if (*lenp != 4) {
        error_setg(errp, "%s: %s/%s not 4 bytes long (not a cell?)",
                   __func__, node_path, property);
        *lenp = -EINVAL;
        return 0;
    }
    return be32_to_cpu(*p);
}

uint32_t qemu_fdt_get_phandle(void *fdt, const char *path)
{
    uint32_t r;

    r = fdt_get_phandle(fdt, findnode_nofail(fdt, path));
    if (r == 0) {
        error_report("%s: Couldn't get phandle for %s: %s", __func__,
                     path, fdt_strerror(r));
        exit(1);
    }

    return r;
}

int qemu_fdt_setprop_phandle(void *fdt, const char *node_path,
                             const char *property,
                             const char *target_node_path)
{
    uint32_t phandle = qemu_fdt_get_phandle(fdt, target_node_path);
    return qemu_fdt_setprop_cell(fdt, node_path, property, phandle);
}

uint32_t qemu_fdt_alloc_phandle(void *fdt)
{
    static int phandle = 0x0;

    /*
     * We need to find out if the user gave us special instruction at
     * which phandle id to start allocating phandles.
     */
    if (!phandle) {
        phandle = machine_phandle_start(current_machine);
    }

    if (!phandle) {
        /*
         * None or invalid phandle given on the command line, so fall back to
         * default starting point.
         */
        phandle = 0x8000;
    }

    return phandle++;
}

int qemu_fdt_nop_node(void *fdt, const char *node_path)
{
    int r;

    r = fdt_nop_node(fdt, findnode_nofail(fdt, node_path));
    if (r < 0) {
        error_report("%s: Couldn't nop node %s: %s", __func__, node_path,
                     fdt_strerror(r));
        exit(1);
    }

    return r;
}

int qemu_fdt_add_subnode(void *fdt, const char *name)
{
    char *dupname = g_strdup(name);
    char *basename = strrchr(dupname, '/');
    int retval;
    int parent = 0;

    if (!basename) {
        g_free(dupname);
        return -1;
    }

    basename[0] = '\0';
    basename++;

    if (dupname[0]) {
        parent = findnode_nofail(fdt, dupname);
    }

    retval = fdt_add_subnode(fdt, parent, basename);
    if (retval < 0) {
        error_report("%s: Failed to create subnode %s: %s",
                     __func__, name, fdt_strerror(retval));
        exit(1);
    }

    g_free(dupname);
    return retval;
}

/*
 * qemu_fdt_add_path: Like qemu_fdt_add_subnode(), but will add
 * all missing subnodes from the given path.
 */
int qemu_fdt_add_path(void *fdt, const char *path)
{
    const char *name;
    int namelen, retval;
    int parent = 0;

    if (path[0] != '/') {
        return -1;
    }

    do {
        name = path + 1;
        path = strchr(name, '/');
        namelen = path != NULL ? path - name : strlen(name);

        retval = fdt_subnode_offset_namelen(fdt, parent, name, namelen);
        if (retval < 0 && retval != -FDT_ERR_NOTFOUND) {
            error_report("%s: Unexpected error in finding subnode %.*s: %s",
                         __func__, namelen, name, fdt_strerror(retval));
            exit(1);
        } else if (retval == -FDT_ERR_NOTFOUND) {
            retval = fdt_add_subnode_namelen(fdt, parent, name, namelen);
            if (retval < 0) {
                error_report("%s: Failed to create subnode %.*s: %s",
                             __func__, namelen, name, fdt_strerror(retval));
                exit(1);
            }
        }

        parent = retval;
    } while (path);

    return retval;
}

int qemu_fdt_setprop_sized_cells_from_array(void *fdt,
                                            const char *node_path,
                                            const char *property,
                                            int numvalues,
                                            uint64_t *values)
{
    uint32_t *propcells;
    uint64_t value;
    int cellnum, vnum, ncells;
    uint32_t hival;
    int ret;

    propcells = g_new0(uint32_t, numvalues * 2);

    cellnum = 0;
    for (vnum = 0; vnum < numvalues; vnum++) {
        ncells = values[vnum * 2];
        if (ncells != 1 && ncells != 2) {
            ret = -1;
            goto out;
        }
        value = values[vnum * 2 + 1];
        hival = cpu_to_be32(value >> 32);
        if (ncells > 1) {
            propcells[cellnum++] = hival;
        } else if (hival != 0) {
            ret = -1;
            goto out;
        }
        propcells[cellnum++] = cpu_to_be32(value);
    }

    ret = qemu_fdt_setprop(fdt, node_path, property, propcells,
                           cellnum * sizeof(uint32_t));
out:
    g_free(propcells);
    return ret;
}

void qmp_dumpdtb(const char *filename, Error **errp)
{
    ERRP_GUARD();

    g_autoptr(GError) err = NULL;
    uint32_t size;

    if (!current_machine->fdt) {
        error_setg(errp, "This machine doesn't have an FDT");
        error_append_hint(errp,
                          "(Perhaps it doesn't support FDT at all, or perhaps "
                          "you need to provide an FDT with the -fdt option?)\n");
        return;
    }

    size = fdt_totalsize(current_machine->fdt);

    g_assert(size > 0);

    if (!g_file_set_contents(filename, current_machine->fdt, size, &err)) {
        error_setg(errp, "Error saving FDT to file %s: %s",
                   filename, err->message);
    }
}

void qemu_fdt_randomize_seeds(void *fdt)
{
    int noffset, poffset, len;
    const char *name;
    uint8_t *data;

    for (noffset = fdt_next_node(fdt, 0, NULL);
         noffset >= 0;
         noffset = fdt_next_node(fdt, noffset, NULL)) {
        for (poffset = fdt_first_property_offset(fdt, noffset);
             poffset >= 0;
             poffset = fdt_next_property_offset(fdt, poffset)) {
            data = (uint8_t *)fdt_getprop_by_offset(fdt, poffset, &name, &len);
            if (!data || strcmp(name, "rng-seed"))
                continue;
            qemu_guest_getrandom_nofail(data, len);
        }
    }
}

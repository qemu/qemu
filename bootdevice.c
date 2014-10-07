/*
 * QEMU Boot Device Implement
 *
 * Copyright (c) 2014 HUAWEI TECHNOLOGIES CO.,LTD.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "sysemu/sysemu.h"
#include "qapi/visitor.h"
#include "qemu/error-report.h"

typedef struct FWBootEntry FWBootEntry;

struct FWBootEntry {
    QTAILQ_ENTRY(FWBootEntry) link;
    int32_t bootindex;
    DeviceState *dev;
    char *suffix;
};

static QTAILQ_HEAD(, FWBootEntry) fw_boot_order =
    QTAILQ_HEAD_INITIALIZER(fw_boot_order);

void check_boot_index(int32_t bootindex, Error **errp)
{
    FWBootEntry *i;

    if (bootindex >= 0) {
        QTAILQ_FOREACH(i, &fw_boot_order, link) {
            if (i->bootindex == bootindex) {
                error_setg(errp, "The bootindex %d has already been used",
                           bootindex);
                return;
            }
        }
    }
}

void del_boot_device_path(DeviceState *dev, const char *suffix)
{
    FWBootEntry *i;

    if (dev == NULL) {
        return;
    }

    QTAILQ_FOREACH(i, &fw_boot_order, link) {
        if ((!suffix || !g_strcmp0(i->suffix, suffix)) &&
             i->dev == dev) {
            QTAILQ_REMOVE(&fw_boot_order, i, link);
            g_free(i->suffix);
            g_free(i);

            break;
        }
    }
}

void add_boot_device_path(int32_t bootindex, DeviceState *dev,
                          const char *suffix)
{
    FWBootEntry *node, *i;

    if (bootindex < 0) {
        del_boot_device_path(dev, suffix);
        return;
    }

    assert(dev != NULL || suffix != NULL);

    del_boot_device_path(dev, suffix);

    node = g_malloc0(sizeof(FWBootEntry));
    node->bootindex = bootindex;
    node->suffix = g_strdup(suffix);
    node->dev = dev;

    QTAILQ_FOREACH(i, &fw_boot_order, link) {
        if (i->bootindex == bootindex) {
            error_report("Two devices with same boot index %d", bootindex);
            exit(1);
        } else if (i->bootindex < bootindex) {
            continue;
        }
        QTAILQ_INSERT_BEFORE(i, node, link);
        return;
    }
    QTAILQ_INSERT_TAIL(&fw_boot_order, node, link);
}

DeviceState *get_boot_device(uint32_t position)
{
    uint32_t counter = 0;
    FWBootEntry *i = NULL;
    DeviceState *res = NULL;

    if (!QTAILQ_EMPTY(&fw_boot_order)) {
        QTAILQ_FOREACH(i, &fw_boot_order, link) {
            if (counter == position) {
                res = i->dev;
                break;
            }
            counter++;
        }
    }
    return res;
}

/*
 * This function returns null terminated string that consist of new line
 * separated device paths.
 *
 * memory pointed by "size" is assigned total length of the array in bytes
 *
 */
char *get_boot_devices_list(size_t *size, bool ignore_suffixes)
{
    FWBootEntry *i;
    size_t total = 0;
    char *list = NULL;

    QTAILQ_FOREACH(i, &fw_boot_order, link) {
        char *devpath = NULL, *bootpath;
        size_t len;

        if (i->dev) {
            devpath = qdev_get_fw_dev_path(i->dev);
            assert(devpath);
        }

        if (i->suffix && !ignore_suffixes && devpath) {
            size_t bootpathlen = strlen(devpath) + strlen(i->suffix) + 1;

            bootpath = g_malloc(bootpathlen);
            snprintf(bootpath, bootpathlen, "%s%s", devpath, i->suffix);
            g_free(devpath);
        } else if (devpath) {
            bootpath = devpath;
        } else if (!ignore_suffixes) {
            assert(i->suffix);
            bootpath = g_strdup(i->suffix);
        } else {
            bootpath = g_strdup("");
        }

        if (total) {
            list[total-1] = '\n';
        }
        len = strlen(bootpath) + 1;
        list = g_realloc(list, total + len);
        memcpy(&list[total], bootpath, len);
        total += len;
        g_free(bootpath);
    }

    *size = total;

    if (boot_strict && *size > 0) {
        list[total-1] = '\n';
        list = g_realloc(list, total + 5);
        memcpy(&list[total], "HALT", 5);
        *size = total + 5;
    }
    return list;
}

typedef struct {
    int32_t *bootindex;
    const char *suffix;
    DeviceState *dev;
} BootIndexProperty;

static void device_get_bootindex(Object *obj, Visitor *v, void *opaque,
                                 const char *name, Error **errp)
{
    BootIndexProperty *prop = opaque;
    visit_type_int32(v, prop->bootindex, name, errp);
}

static void device_set_bootindex(Object *obj, Visitor *v, void *opaque,
                                 const char *name, Error **errp)
{
    BootIndexProperty *prop = opaque;
    int32_t boot_index;
    Error *local_err = NULL;

    visit_type_int32(v, &boot_index, name, &local_err);
    if (local_err) {
        goto out;
    }
    /* check whether bootindex is present in fw_boot_order list  */
    check_boot_index(boot_index, &local_err);
    if (local_err) {
        goto out;
    }
    /* change bootindex to a new one */
    *prop->bootindex = boot_index;

    add_boot_device_path(*prop->bootindex, prop->dev, prop->suffix);

out:
    if (local_err) {
        error_propagate(errp, local_err);
    }
}

static void property_release_bootindex(Object *obj, const char *name,
                                       void *opaque)

{
    BootIndexProperty *prop = opaque;

    del_boot_device_path(prop->dev, prop->suffix);
    g_free(prop);
}

void device_add_bootindex_property(Object *obj, int32_t *bootindex,
                                   const char *name, const char *suffix,
                                   DeviceState *dev, Error **errp)
{
    Error *local_err = NULL;
    BootIndexProperty *prop = g_malloc0(sizeof(*prop));

    prop->bootindex = bootindex;
    prop->suffix = suffix;
    prop->dev = dev;

    object_property_add(obj, name, "int32",
                        device_get_bootindex,
                        device_set_bootindex,
                        property_release_bootindex,
                        prop, &local_err);

    if (local_err) {
        error_propagate(errp, local_err);
        g_free(prop);
        return;
    }
    /* initialize devices' bootindex property to -1 */
    object_property_set_int(obj, -1, name, NULL);
}

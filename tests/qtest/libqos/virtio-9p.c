/*
 * libqos driver framework
 *
 * Copyright (c) 2018 Emanuele Giuseppe Esposito <e.emanuelegiuseppe@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

/*
 * Not so fast! You might want to read the 9p developer docs first:
 * https://wiki.qemu.org/Documentation/9p
 */

#include "qemu/osdep.h"
#include "../libqtest.h"
#include "qemu/module.h"
#include "standard-headers/linux/virtio_ids.h"
#include "virtio-9p.h"
#include "qgraph.h"

static QGuestAllocator *alloc;
static char *local_test_path;

/* Concatenates the passed 2 paths. Returned result must be freed. */
static char *concat_path(const char* a, const char* b)
{
    return g_build_filename(a, b, NULL);
}

void virtio_9p_create_local_test_dir(void)
{
    g_assert(local_test_path == NULL);
    struct stat st;
    g_autofree char *pwd = g_get_current_dir();
    /*
     * template gets cached into local_test_path and freed in
     * virtio_9p_remove_local_test_dir().
     */
    char *template = concat_path(pwd, "qtest-9p-local-XXXXXX");

    local_test_path = g_mkdtemp(template);
    if (!local_test_path) {
        g_test_message("g_mkdtemp('%s') failed: %s", template, strerror(errno));
    }

    g_assert(local_test_path != NULL);

    /* ensure test directory exists now ... */
    g_assert(stat(local_test_path, &st) == 0);
    /* ... and is actually a directory */
    g_assert((st.st_mode & S_IFMT) == S_IFDIR);
}

void virtio_9p_remove_local_test_dir(void)
{
    g_assert(local_test_path != NULL);
    g_autofree char *cmd = g_strdup_printf("rm -fr '%s'\n", local_test_path);
    int res = system(cmd);
    if (res < 0) {
        /* ignore error, dummy check to prevent compiler error */
    }
    g_free(local_test_path);
    local_test_path = NULL;
}

char *virtio_9p_test_path(const char *path)
{
    g_assert(local_test_path);
    return concat_path(local_test_path, path);
}

static void virtio_9p_cleanup(QVirtio9P *interface)
{
    qvirtqueue_cleanup(interface->vdev->bus, interface->vq, alloc);
}

static void virtio_9p_setup(QVirtio9P *interface)
{
    uint64_t features;

    features = qvirtio_get_features(interface->vdev);
    features &= ~(QVIRTIO_F_BAD_FEATURE | (1ull << VIRTIO_RING_F_EVENT_IDX));
    qvirtio_set_features(interface->vdev, features);

    interface->vq = qvirtqueue_setup(interface->vdev, alloc, 0);
    qvirtio_set_driver_ok(interface->vdev);
}

/* virtio-9p-device */
static void virtio_9p_device_destructor(QOSGraphObject *obj)
{
    QVirtio9PDevice *v_9p = (QVirtio9PDevice *) obj;
    QVirtio9P *v9p = &v_9p->v9p;

    virtio_9p_cleanup(v9p);
}

static void virtio_9p_device_start_hw(QOSGraphObject *obj)
{
    QVirtio9PDevice *v_9p = (QVirtio9PDevice *) obj;
    QVirtio9P *v9p = &v_9p->v9p;

    virtio_9p_setup(v9p);
}

static void *virtio_9p_get_driver(QVirtio9P *v_9p,
                                         const char *interface)
{
    if (!g_strcmp0(interface, "virtio-9p")) {
        return v_9p;
    }
    if (!g_strcmp0(interface, "virtio")) {
        return v_9p->vdev;
    }

    fprintf(stderr, "%s not present in virtio-9p-device\n", interface);
    g_assert_not_reached();
}

static void *virtio_9p_device_get_driver(void *object, const char *interface)
{
    QVirtio9PDevice *v_9p = object;
    return virtio_9p_get_driver(&v_9p->v9p, interface);
}

static void *virtio_9p_device_create(void *virtio_dev,
                                     QGuestAllocator *t_alloc,
                                     void *addr)
{
    QVirtio9PDevice *virtio_device = g_new0(QVirtio9PDevice, 1);
    QVirtio9P *interface = &virtio_device->v9p;

    interface->vdev = virtio_dev;
    alloc = t_alloc;

    virtio_device->obj.destructor = virtio_9p_device_destructor;
    virtio_device->obj.get_driver = virtio_9p_device_get_driver;
    virtio_device->obj.start_hw = virtio_9p_device_start_hw;

    return &virtio_device->obj;
}

/* virtio-9p-pci */
static void virtio_9p_pci_destructor(QOSGraphObject *obj)
{
    QVirtio9PPCI *v9_pci = (QVirtio9PPCI *) obj;
    QVirtio9P *interface = &v9_pci->v9p;
    QOSGraphObject *pci_vobj =  &v9_pci->pci_vdev.obj;

    virtio_9p_cleanup(interface);
    qvirtio_pci_destructor(pci_vobj);
}

static void virtio_9p_pci_start_hw(QOSGraphObject *obj)
{
    QVirtio9PPCI *v9_pci = (QVirtio9PPCI *) obj;
    QVirtio9P *interface = &v9_pci->v9p;
    QOSGraphObject *pci_vobj =  &v9_pci->pci_vdev.obj;

    qvirtio_pci_start_hw(pci_vobj);
    virtio_9p_setup(interface);
}

static void *virtio_9p_pci_get_driver(void *object, const char *interface)
{
    QVirtio9PPCI *v_9p = object;
    if (!g_strcmp0(interface, "pci-device")) {
        return v_9p->pci_vdev.pdev;
    }
    return virtio_9p_get_driver(&v_9p->v9p, interface);
}

static void *virtio_9p_pci_create(void *pci_bus, QGuestAllocator *t_alloc,
                                  void *addr)
{
    QVirtio9PPCI *v9_pci = g_new0(QVirtio9PPCI, 1);
    QVirtio9P *interface = &v9_pci->v9p;
    QOSGraphObject *obj = &v9_pci->pci_vdev.obj;

    virtio_pci_init(&v9_pci->pci_vdev, pci_bus, addr);
    interface->vdev = &v9_pci->pci_vdev.vdev;
    alloc = t_alloc;

    g_assert_cmphex(interface->vdev->device_type, ==, VIRTIO_ID_9P);

    obj->destructor = virtio_9p_pci_destructor;
    obj->start_hw = virtio_9p_pci_start_hw;
    obj->get_driver = virtio_9p_pci_get_driver;

    return obj;
}

/**
 * Performs regular expression based search and replace on @a haystack.
 *
 * @param haystack - input string to be parsed, result of replacement is
 *                   stored back to @a haystack
 * @param pattern - the regular expression pattern for scanning @a haystack
 * @param replace_fmt - matches of supplied @a pattern are replaced by this,
 *                      if necessary glib printf format can be used to add
 *                      variable arguments of this function to this
 *                      replacement string
 */
G_GNUC_PRINTF(3, 4)
static void regex_replace(GString *haystack, const char *pattern,
                          const char *replace_fmt, ...)
{
    g_autoptr(GRegex) regex = NULL;
    g_autofree char *replace = NULL, *s = NULL;
    va_list argp;

    va_start(argp, replace_fmt);
    replace = g_strdup_vprintf(replace_fmt, argp);
    va_end(argp);

    regex = g_regex_new(pattern, 0, 0, NULL);
    s = g_regex_replace(regex, haystack->str, -1, 0, replace, 0, NULL);
    g_string_assign(haystack, s);
}

void virtio_9p_assign_local_driver(GString *cmd_line, const char *args)
{
    g_assert_nonnull(local_test_path);

    /* replace 'synth' driver by 'local' driver */
    regex_replace(cmd_line, "-fsdev synth,", "-fsdev local,");

    /* append 'path=...' to '-fsdev ...' group */
    regex_replace(cmd_line, "(-fsdev \\w[^ ]*)", "\\1,path='%s'",
                  local_test_path);

    if (!args) {
        return;
    }

    /* append passed args to '-fsdev ...' group */
    regex_replace(cmd_line, "(-fsdev \\w[^ ]*)", "\\1,%s", args);
}

static void virtio_9p_register_nodes(void)
{
    const char *str_simple = "fsdev=fsdev0,mount_tag=" MOUNT_TAG;
    const char *str_addr = "fsdev=fsdev0,addr=04.0,mount_tag=" MOUNT_TAG;

    QPCIAddress addr = {
        .devfn = QPCI_DEVFN(4, 0),
    };

    QOSGraphEdgeOptions opts = {
        .before_cmd_line = "-fsdev synth,id=fsdev0",
    };

    /* virtio-9p-device */
    opts.extra_device_opts = str_simple,
    qos_node_create_driver("virtio-9p-device", virtio_9p_device_create);
    qos_node_consumes("virtio-9p-device", "virtio-bus", &opts);
    qos_node_produces("virtio-9p-device", "virtio");
    qos_node_produces("virtio-9p-device", "virtio-9p");

    /* virtio-9p-pci */
    opts.extra_device_opts = str_addr;
    add_qpci_address(&opts, &addr);
    qos_node_create_driver("virtio-9p-pci", virtio_9p_pci_create);
    qos_node_consumes("virtio-9p-pci", "pci-bus", &opts);
    qos_node_produces("virtio-9p-pci", "pci-device");
    qos_node_produces("virtio-9p-pci", "virtio");
    qos_node_produces("virtio-9p-pci", "virtio-9p");

}

libqos_init(virtio_9p_register_nodes);

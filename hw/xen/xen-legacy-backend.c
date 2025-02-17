/*
 *  xen backend driver infrastructure
 *  (c) 2008 Gerd Hoffmann <kraxel@redhat.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; under version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 *  Contributions after 2012-01-13 are licensed under the terms of the
 *  GNU GPL, version 2 or (at your option) any later version.
 */

/*
 * TODO: add some xenbus / xenstore concepts overview here.
 */

#include "qemu/osdep.h"

#include "hw/sysbus.h"
#include "hw/boards.h"
#include "hw/qdev-properties.h"
#include "qemu/main-loop.h"
#include "qapi/error.h"
#include "hw/xen/xen-legacy-backend.h"
#include "hw/xen/xen_pvdev.h"
#include "monitor/qdev.h"

DeviceState *xen_sysdev;
BusState *xen_sysbus;

/* ------------------------------------------------------------- */

/* public */
struct qemu_xs_handle *xenstore;
const char *xen_protocol;

/* private */
static int debug;

int xenstore_write_be_str(struct XenLegacyDevice *xendev, const char *node,
                          const char *val)
{
    return xenstore_write_str(xendev->be, node, val);
}

int xenstore_write_be_int(struct XenLegacyDevice *xendev, const char *node,
                          int ival)
{
    return xenstore_write_int(xendev->be, node, ival);
}

int xenstore_write_be_int64(struct XenLegacyDevice *xendev, const char *node,
                            int64_t ival)
{
    return xenstore_write_int64(xendev->be, node, ival);
}

char *xenstore_read_be_str(struct XenLegacyDevice *xendev, const char *node)
{
    return xenstore_read_str(xendev->be, node);
}

int xenstore_read_be_int(struct XenLegacyDevice *xendev, const char *node,
                         int *ival)
{
    return xenstore_read_int(xendev->be, node, ival);
}

char *xenstore_read_fe_str(struct XenLegacyDevice *xendev, const char *node)
{
    return xenstore_read_str(xendev->fe, node);
}

int xenstore_read_fe_int(struct XenLegacyDevice *xendev, const char *node,
                         int *ival)
{
    return xenstore_read_int(xendev->fe, node, ival);
}

int xenstore_read_fe_uint64(struct XenLegacyDevice *xendev, const char *node,
                            uint64_t *uval)
{
    return xenstore_read_uint64(xendev->fe, node, uval);
}

/* ------------------------------------------------------------- */

int xen_be_set_state(struct XenLegacyDevice *xendev, enum xenbus_state state)
{
    int rc;

    rc = xenstore_write_be_int(xendev, "state", state);
    if (rc < 0) {
        return rc;
    }
    xen_pv_printf(xendev, 1, "backend state: %s -> %s\n",
                  xenbus_strstate(xendev->be_state), xenbus_strstate(state));
    xendev->be_state = state;
    return 0;
}

void xen_be_set_max_grant_refs(struct XenLegacyDevice *xendev,
                               unsigned int nr_refs)
{
    assert(xendev->ops->flags & DEVOPS_FLAG_NEED_GNTDEV);

    if (qemu_xen_gnttab_set_max_grants(xendev->gnttabdev, nr_refs)) {
        xen_pv_printf(xendev, 0, "xengnttab_set_max_grants failed: %s\n",
                      strerror(errno));
    }
}

void *xen_be_map_grant_refs(struct XenLegacyDevice *xendev, uint32_t *refs,
                            unsigned int nr_refs, int prot)
{
    void *ptr;

    assert(xendev->ops->flags & DEVOPS_FLAG_NEED_GNTDEV);

    ptr = qemu_xen_gnttab_map_refs(xendev->gnttabdev, nr_refs, xen_domid, refs,
                                   prot);
    if (!ptr) {
        xen_pv_printf(xendev, 0,
                      "xengnttab_map_domain_grant_refs failed: %s\n",
                      strerror(errno));
    }

    return ptr;
}

void xen_be_unmap_grant_refs(struct XenLegacyDevice *xendev, void *ptr,
                             uint32_t *refs, unsigned int nr_refs)
{
    assert(xendev->ops->flags & DEVOPS_FLAG_NEED_GNTDEV);

    if (qemu_xen_gnttab_unmap(xendev->gnttabdev, ptr, refs, nr_refs)) {
        xen_pv_printf(xendev, 0, "xengnttab_unmap failed: %s\n",
                      strerror(errno));
    }
}

/*
 * get xen backend device, allocate a new one if it doesn't exist.
 */
static struct XenLegacyDevice *xen_be_get_xendev(const char *type, int dom,
                                                 int dev,
                                                 const struct XenDevOps *ops)
{
    struct XenLegacyDevice *xendev;

    xendev = xen_pv_find_xendev(type, dom, dev);
    if (xendev) {
        return xendev;
    }

    /* init new xendev */
    xendev = g_malloc0(ops->size);
    object_initialize(xendev, ops->size, TYPE_XENBACKEND);
    OBJECT(xendev)->free = g_free;
    qdev_set_id(DEVICE(xendev), g_strdup_printf("xen-%s-%d", type, dev),
                &error_fatal);
    qdev_realize(DEVICE(xendev), xen_sysbus, &error_fatal);
    object_unref(OBJECT(xendev));

    xendev->type  = type;
    xendev->dom   = dom;
    xendev->dev   = dev;
    xendev->ops   = ops;

    snprintf(xendev->be, sizeof(xendev->be), "backend/%s/%d/%d",
             xendev->type, xendev->dom, xendev->dev);
    snprintf(xendev->name, sizeof(xendev->name), "%s-%d",
             xendev->type, xendev->dev);

    xendev->debug      = debug;
    xendev->local_port = -1;

    xendev->evtchndev = qemu_xen_evtchn_open();
    if (xendev->evtchndev == NULL) {
        xen_pv_printf(NULL, 0, "can't open evtchn device\n");
        qdev_unplug(DEVICE(xendev), NULL);
        return NULL;
    }
    qemu_set_cloexec(qemu_xen_evtchn_fd(xendev->evtchndev));

    xen_pv_insert_xendev(xendev);

    if (xendev->ops->alloc) {
        xendev->ops->alloc(xendev);
    }

    return xendev;
}


/*
 * Sync internal data structures on xenstore updates.
 * Node specifies the changed field.  node = NULL means
 * update all fields (used for initialization).
 */
static void xen_be_backend_changed(struct XenLegacyDevice *xendev,
                                   const char *node)
{
    if (node == NULL  ||  strcmp(node, "online") == 0) {
        if (xenstore_read_be_int(xendev, "online", &xendev->online) == -1) {
            xendev->online = 0;
        }
    }

    if (node) {
        xen_pv_printf(xendev, 2, "backend update: %s\n", node);
        if (xendev->ops->backend_changed) {
            xendev->ops->backend_changed(xendev, node);
        }
    }
}

static void xen_be_frontend_changed(struct XenLegacyDevice *xendev,
                                    const char *node)
{
    int fe_state;

    if (node == NULL  ||  strcmp(node, "state") == 0) {
        if (xenstore_read_fe_int(xendev, "state", &fe_state) == -1) {
            fe_state = XenbusStateUnknown;
        }
        if (xendev->fe_state != fe_state) {
            xen_pv_printf(xendev, 1, "frontend state: %s -> %s\n",
                          xenbus_strstate(xendev->fe_state),
                          xenbus_strstate(fe_state));
        }
        xendev->fe_state = fe_state;
    }
    if (node == NULL  ||  strcmp(node, "protocol") == 0) {
        g_free(xendev->protocol);
        xendev->protocol = xenstore_read_fe_str(xendev, "protocol");
        if (xendev->protocol) {
            xen_pv_printf(xendev, 1, "frontend protocol: %s\n",
                          xendev->protocol);
        }
    }

    if (node) {
        xen_pv_printf(xendev, 2, "frontend update: %s\n", node);
        if (xendev->ops->frontend_changed) {
            xendev->ops->frontend_changed(xendev, node);
        }
    }
}

static void xenstore_update_fe(void *opaque, const char *watch)
{
    struct XenLegacyDevice *xendev = opaque;
    const char *node;
    unsigned int len;

    len = strlen(xendev->fe);
    if (strncmp(xendev->fe, watch, len) != 0) {
        return;
    }
    if (watch[len] != '/') {
        return;
    }
    node = watch + len + 1;

    xen_be_frontend_changed(xendev, node);
    xen_be_check_state(xendev);
}

/* ------------------------------------------------------------- */
/* Check for possible state transitions and perform them.        */

/*
 * Initial xendev setup.  Read frontend path, register watch for it.
 * Should succeed once xend finished setting up the backend device.
 *
 * Also sets initial state (-> Initializing) when done.  Which
 * only affects the xendev->be_state variable as xenbus should
 * already be put into that state by xend.
 */
static int xen_be_try_setup(struct XenLegacyDevice *xendev)
{
    int be_state;

    if (xenstore_read_be_int(xendev, "state", &be_state) == -1) {
        xen_pv_printf(xendev, 0, "reading backend state failed\n");
        return -1;
    }

    if (be_state != XenbusStateInitialising) {
        xen_pv_printf(xendev, 0, "initial backend state is wrong (%s)\n",
                      xenbus_strstate(be_state));
        return -1;
    }

    xendev->fe = xenstore_read_be_str(xendev, "frontend");
    if (xendev->fe == NULL) {
        xen_pv_printf(xendev, 0, "reading frontend path failed\n");
        return -1;
    }

    /* setup frontend watch */
    xendev->watch = qemu_xen_xs_watch(xenstore, xendev->fe, xenstore_update_fe,
                                      xendev);
    if (!xendev->watch) {
        xen_pv_printf(xendev, 0, "watching frontend path (%s) failed\n",
                      xendev->fe);
        return -1;
    }
    xen_be_set_state(xendev, XenbusStateInitialising);

    xen_be_backend_changed(xendev, NULL);
    xen_be_frontend_changed(xendev, NULL);
    return 0;
}

/*
 * Try initialize xendev.  Prepare everything the backend can do
 * without synchronizing with the frontend.  Fakes hotplug-status.  No
 * hotplug involved here because this is about userspace drivers, thus
 * there are kernel backend devices which could invoke hotplug.
 *
 * Goes to InitWait on success.
 */
static int xen_be_try_init(struct XenLegacyDevice *xendev)
{
    int rc = 0;

    if (!xendev->online) {
        xen_pv_printf(xendev, 1, "not online\n");
        return -1;
    }

    if (xendev->ops->init) {
        rc = xendev->ops->init(xendev);
    }
    if (rc != 0) {
        xen_pv_printf(xendev, 1, "init() failed\n");
        return rc;
    }

    xenstore_write_be_str(xendev, "hotplug-status", "connected");
    xen_be_set_state(xendev, XenbusStateInitWait);
    return 0;
}

/*
 * Try to initialise xendev.  Depends on the frontend being ready
 * for it (shared ring and evtchn info in xenstore, state being
 * Initialised or Connected).
 *
 * Goes to Connected on success.
 */
static int xen_be_try_initialise(struct XenLegacyDevice *xendev)
{
    int rc = 0;

    if (xendev->fe_state != XenbusStateInitialised  &&
        xendev->fe_state != XenbusStateConnected) {
        if (xendev->ops->flags & DEVOPS_FLAG_IGNORE_STATE) {
            xen_pv_printf(xendev, 2, "frontend not ready, ignoring\n");
        } else {
            xen_pv_printf(xendev, 2, "frontend not ready (yet)\n");
            return -1;
        }
    }

    if (xendev->ops->flags & DEVOPS_FLAG_NEED_GNTDEV) {
        xendev->gnttabdev = qemu_xen_gnttab_open();
        if (xendev->gnttabdev == NULL) {
            xen_pv_printf(NULL, 0, "can't open gnttab device\n");
            return -1;
        }
    } else {
        xendev->gnttabdev = NULL;
    }

    if (xendev->ops->initialise) {
        rc = xendev->ops->initialise(xendev);
    }
    if (rc != 0) {
        xen_pv_printf(xendev, 0, "initialise() failed\n");
        return rc;
    }

    xen_be_set_state(xendev, XenbusStateConnected);
    return 0;
}

/*
 * Try to let xendev know that it is connected.  Depends on the
 * frontend being Connected.  Note that this may be called more
 * than once since the backend state is not modified.
 */
static void xen_be_try_connected(struct XenLegacyDevice *xendev)
{
    if (!xendev->ops->connected) {
        return;
    }

    if (xendev->fe_state != XenbusStateConnected) {
        if (xendev->ops->flags & DEVOPS_FLAG_IGNORE_STATE) {
            xen_pv_printf(xendev, 2, "frontend not ready, ignoring\n");
        } else {
            xen_pv_printf(xendev, 2, "frontend not ready (yet)\n");
            return;
        }
    }

    xendev->ops->connected(xendev);
}

/*
 * Teardown connection.
 *
 * Goes to Closed when done.
 */
static void xen_be_disconnect(struct XenLegacyDevice *xendev,
                              enum xenbus_state state)
{
    if (xendev->be_state != XenbusStateClosing &&
        xendev->be_state != XenbusStateClosed  &&
        xendev->ops->disconnect) {
        xendev->ops->disconnect(xendev);
    }
    if (xendev->gnttabdev) {
        qemu_xen_gnttab_close(xendev->gnttabdev);
        xendev->gnttabdev = NULL;
    }
    if (xendev->be_state != state) {
        xen_be_set_state(xendev, state);
    }
}

/*
 * Try to reset xendev, for reconnection by another frontend instance.
 */
static int xen_be_try_reset(struct XenLegacyDevice *xendev)
{
    if (xendev->fe_state != XenbusStateInitialising) {
        return -1;
    }

    xen_pv_printf(xendev, 1, "device reset (for re-connect)\n");
    xen_be_set_state(xendev, XenbusStateInitialising);
    return 0;
}

/*
 * state change dispatcher function
 */
void xen_be_check_state(struct XenLegacyDevice *xendev)
{
    int rc = 0;

    /* frontend may request shutdown from almost anywhere */
    if (xendev->fe_state == XenbusStateClosing ||
        xendev->fe_state == XenbusStateClosed) {
        xen_be_disconnect(xendev, xendev->fe_state);
        return;
    }

    /* check for possible backend state transitions */
    for (;;) {
        switch (xendev->be_state) {
        case XenbusStateUnknown:
            rc = xen_be_try_setup(xendev);
            break;
        case XenbusStateInitialising:
            rc = xen_be_try_init(xendev);
            break;
        case XenbusStateInitWait:
            rc = xen_be_try_initialise(xendev);
            break;
        case XenbusStateConnected:
            /* xendev->be_state doesn't change */
            xen_be_try_connected(xendev);
            rc = -1;
            break;
        case XenbusStateClosed:
            rc = xen_be_try_reset(xendev);
            break;
        default:
            rc = -1;
        }
        if (rc != 0) {
            break;
        }
    }
}

/* ------------------------------------------------------------- */

struct xenstore_be {
    const char *type;
    int dom;
    const struct XenDevOps *ops;
};

static void xenstore_update_be(void *opaque, const char *watch)
{
    struct xenstore_be *be = opaque;
    struct XenLegacyDevice *xendev;
    char path[XEN_BUFSIZE], *bepath;
    unsigned int len, dev;

    len = snprintf(path, sizeof(path), "backend/%s/%d", be->type, be->dom);
    if (strncmp(path, watch, len) != 0) {
        return;
    }
    if (sscanf(watch + len, "/%u/%255s", &dev, path) != 2) {
        strcpy(path, "");
        if (sscanf(watch + len, "/%u", &dev) != 1) {
            dev = -1;
        }
    }
    if (dev == -1) {
        return;
    }

    xendev = xen_be_get_xendev(be->type, be->dom, dev, be->ops);
    if (xendev != NULL) {
        bepath = qemu_xen_xs_read(xenstore, 0, xendev->be, &len);
        if (bepath == NULL) {
            xen_pv_del_xendev(xendev);
        } else {
            free(bepath);
            xen_be_backend_changed(xendev, path);
            xen_be_check_state(xendev);
        }
    }
}

static int xenstore_scan(const char *type, int dom, const struct XenDevOps *ops)
{
    struct XenLegacyDevice *xendev;
    char path[XEN_BUFSIZE];
    struct xenstore_be *be = g_new0(struct xenstore_be, 1);
    char **dev = NULL;
    unsigned int cdev, j;

    /* setup watch */
    be->type = type;
    be->dom = dom;
    be->ops = ops;
    snprintf(path, sizeof(path), "backend/%s/%d", type, dom);
    if (!qemu_xen_xs_watch(xenstore, path, xenstore_update_be, be)) {
        xen_pv_printf(NULL, 0, "xen be: watching backend path (%s) failed\n",
                      path);
        return -1;
    }

    /* look for backends */
    dev = qemu_xen_xs_directory(xenstore, 0, path, &cdev);
    if (!dev) {
        return 0;
    }
    for (j = 0; j < cdev; j++) {
        xendev = xen_be_get_xendev(type, dom, atoi(dev[j]), ops);
        if (xendev == NULL) {
            continue;
        }
        xen_be_check_state(xendev);
    }
    free(dev);
    return 0;
}

/* -------------------------------------------------------------------- */

static void xen_set_dynamic_sysbus(void)
{
    Object *machine = qdev_get_machine();
    ObjectClass *oc = object_get_class(machine);
    MachineClass *mc = MACHINE_CLASS(oc);

    machine_class_allow_dynamic_sysbus_dev(mc, TYPE_XENSYSDEV);
}

void xen_be_init(void)
{
    xenstore = qemu_xen_xs_open();
    if (!xenstore) {
        xen_pv_printf(NULL, 0, "can't connect to xenstored\n");
        exit(1);
    }

    if (xen_evtchn_ops == NULL || xen_gnttab_ops == NULL) {
        xen_pv_printf(NULL, 0, "Xen operations not set up\n");
        exit(1);
    }

    xen_sysdev = qdev_new(TYPE_XENSYSDEV);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(xen_sysdev), &error_fatal);
    xen_sysbus = qbus_new(TYPE_XENSYSBUS, xen_sysdev, "xen-sysbus");
    qbus_set_bus_hotplug_handler(xen_sysbus);

    xen_set_dynamic_sysbus();
}

int xen_be_register(const char *type, const struct XenDevOps *ops)
{
    char path[50];

    snprintf(path, sizeof(path), "device-model/%u/backends/%s", xen_domid,
             type);
    xenstore_mkdir(path, XS_PERM_NONE);

    return xenstore_scan(type, xen_domid, ops);
}

int xen_be_bind_evtchn(struct XenLegacyDevice *xendev)
{
    if (xendev->local_port != -1) {
        return 0;
    }
    xendev->local_port = qemu_xen_evtchn_bind_interdomain
        (xendev->evtchndev, xendev->dom, xendev->remote_port);
    if (xendev->local_port == -1) {
        xen_pv_printf(xendev, 0, "xenevtchn_bind_interdomain failed\n");
        return -1;
    }
    xen_pv_printf(xendev, 2, "bind evtchn port %d\n", xendev->local_port);
    qemu_set_fd_handler(qemu_xen_evtchn_fd(xendev->evtchndev),
                        xen_pv_evtchn_event, NULL, xendev);
    return 0;
}


static void xendev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->bus_type = TYPE_XENSYSBUS;
}

static const TypeInfo xendev_type_info = {
    .name          = TYPE_XENBACKEND,
    .parent        = TYPE_DYNAMIC_SYS_BUS_DEVICE,
    .class_init    = xendev_class_init,
    .instance_size = sizeof(XenLegacyDevice),
};

static void xen_sysbus_class_init(ObjectClass *klass, void *data)
{
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(klass);

    hc->unplug = qdev_simple_device_unplug_cb;
}

static const TypeInfo xensysbus_info = {
    .name       = TYPE_XENSYSBUS,
    .parent     = TYPE_BUS,
    .class_init = xen_sysbus_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_HOTPLUG_HANDLER },
        { }
    }
};

static const TypeInfo xensysdev_info = {
    .name          = TYPE_XENSYSDEV,
    .parent        = TYPE_SYS_BUS_DEVICE,
};

static void xenbe_register_types(void)
{
    type_register_static(&xensysbus_info);
    type_register_static(&xensysdev_info);
    type_register_static(&xendev_type_info);
}

type_init(xenbe_register_types)

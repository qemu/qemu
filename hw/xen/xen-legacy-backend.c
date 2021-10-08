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
struct xs_handle *xenstore;
const char *xen_protocol;

/* private */
static bool xen_feature_grant_copy;
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

    if (xengnttab_set_max_grants(xendev->gnttabdev, nr_refs)) {
        xen_pv_printf(xendev, 0, "xengnttab_set_max_grants failed: %s\n",
                      strerror(errno));
    }
}

void *xen_be_map_grant_refs(struct XenLegacyDevice *xendev, uint32_t *refs,
                            unsigned int nr_refs, int prot)
{
    void *ptr;

    assert(xendev->ops->flags & DEVOPS_FLAG_NEED_GNTDEV);

    ptr = xengnttab_map_domain_grant_refs(xendev->gnttabdev, nr_refs,
                                          xen_domid, refs, prot);
    if (!ptr) {
        xen_pv_printf(xendev, 0,
                      "xengnttab_map_domain_grant_refs failed: %s\n",
                      strerror(errno));
    }

    return ptr;
}

void xen_be_unmap_grant_refs(struct XenLegacyDevice *xendev, void *ptr,
                             unsigned int nr_refs)
{
    assert(xendev->ops->flags & DEVOPS_FLAG_NEED_GNTDEV);

    if (xengnttab_unmap(xendev->gnttabdev, ptr, nr_refs)) {
        xen_pv_printf(xendev, 0, "xengnttab_unmap failed: %s\n",
                      strerror(errno));
    }
}

static int compat_copy_grant_refs(struct XenLegacyDevice *xendev,
                                  bool to_domain,
                                  XenGrantCopySegment segs[],
                                  unsigned int nr_segs)
{
    uint32_t *refs = g_new(uint32_t, nr_segs);
    int prot = to_domain ? PROT_WRITE : PROT_READ;
    void *pages;
    unsigned int i;

    for (i = 0; i < nr_segs; i++) {
        XenGrantCopySegment *seg = &segs[i];

        refs[i] = to_domain ?
            seg->dest.foreign.ref : seg->source.foreign.ref;
    }

    pages = xengnttab_map_domain_grant_refs(xendev->gnttabdev, nr_segs,
                                            xen_domid, refs, prot);
    if (!pages) {
        xen_pv_printf(xendev, 0,
                      "xengnttab_map_domain_grant_refs failed: %s\n",
                      strerror(errno));
        g_free(refs);
        return -1;
    }

    for (i = 0; i < nr_segs; i++) {
        XenGrantCopySegment *seg = &segs[i];
        void *page = pages + (i * XC_PAGE_SIZE);

        if (to_domain) {
            memcpy(page + seg->dest.foreign.offset, seg->source.virt,
                   seg->len);
        } else {
            memcpy(seg->dest.virt, page + seg->source.foreign.offset,
                   seg->len);
        }
    }

    if (xengnttab_unmap(xendev->gnttabdev, pages, nr_segs)) {
        xen_pv_printf(xendev, 0, "xengnttab_unmap failed: %s\n",
                      strerror(errno));
    }

    g_free(refs);
    return 0;
}

int xen_be_copy_grant_refs(struct XenLegacyDevice *xendev,
                           bool to_domain,
                           XenGrantCopySegment segs[],
                           unsigned int nr_segs)
{
    xengnttab_grant_copy_segment_t *xengnttab_segs;
    unsigned int i;
    int rc;

    assert(xendev->ops->flags & DEVOPS_FLAG_NEED_GNTDEV);

    if (!xen_feature_grant_copy) {
        return compat_copy_grant_refs(xendev, to_domain, segs, nr_segs);
    }

    xengnttab_segs = g_new0(xengnttab_grant_copy_segment_t, nr_segs);

    for (i = 0; i < nr_segs; i++) {
        XenGrantCopySegment *seg = &segs[i];
        xengnttab_grant_copy_segment_t *xengnttab_seg = &xengnttab_segs[i];

        if (to_domain) {
            xengnttab_seg->flags = GNTCOPY_dest_gref;
            xengnttab_seg->dest.foreign.domid = xen_domid;
            xengnttab_seg->dest.foreign.ref = seg->dest.foreign.ref;
            xengnttab_seg->dest.foreign.offset = seg->dest.foreign.offset;
            xengnttab_seg->source.virt = seg->source.virt;
        } else {
            xengnttab_seg->flags = GNTCOPY_source_gref;
            xengnttab_seg->source.foreign.domid = xen_domid;
            xengnttab_seg->source.foreign.ref = seg->source.foreign.ref;
            xengnttab_seg->source.foreign.offset =
                seg->source.foreign.offset;
            xengnttab_seg->dest.virt = seg->dest.virt;
        }

        xengnttab_seg->len = seg->len;
    }

    rc = xengnttab_grant_copy(xendev->gnttabdev, nr_segs, xengnttab_segs);

    if (rc) {
        xen_pv_printf(xendev, 0, "xengnttab_copy failed: %s\n",
                      strerror(errno));
    }

    for (i = 0; i < nr_segs; i++) {
        xengnttab_grant_copy_segment_t *xengnttab_seg =
            &xengnttab_segs[i];

        if (xengnttab_seg->status != GNTST_okay) {
            xen_pv_printf(xendev, 0, "segment[%u] status: %d\n", i,
                          xengnttab_seg->status);
            rc = -1;
        }
    }

    g_free(xengnttab_segs);
    return rc;
}

/*
 * get xen backend device, allocate a new one if it doesn't exist.
 */
static struct XenLegacyDevice *xen_be_get_xendev(const char *type, int dom,
                                                 int dev,
                                                 struct XenDevOps *ops)
{
    struct XenLegacyDevice *xendev;

    xendev = xen_pv_find_xendev(type, dom, dev);
    if (xendev) {
        return xendev;
    }

    /* init new xendev */
    xendev = g_malloc0(ops->size);
    object_initialize(&xendev->qdev, ops->size, TYPE_XENBACKEND);
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

    xendev->evtchndev = xenevtchn_open(NULL, 0);
    if (xendev->evtchndev == NULL) {
        xen_pv_printf(NULL, 0, "can't open evtchn device\n");
        qdev_unplug(DEVICE(xendev), NULL);
        return NULL;
    }
    qemu_set_cloexec(xenevtchn_fd(xendev->evtchndev));

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
    char token[XEN_BUFSIZE];
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
    snprintf(token, sizeof(token), "fe:%p", xendev);
    if (!xs_watch(xenstore, xendev->fe, token)) {
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
        xendev->gnttabdev = xengnttab_open(NULL, 0);
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
        xengnttab_close(xendev->gnttabdev);
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

static int xenstore_scan(const char *type, int dom, struct XenDevOps *ops)
{
    struct XenLegacyDevice *xendev;
    char path[XEN_BUFSIZE], token[XEN_BUFSIZE];
    char **dev = NULL;
    unsigned int cdev, j;

    /* setup watch */
    snprintf(token, sizeof(token), "be:%p:%d:%p", type, dom, ops);
    snprintf(path, sizeof(path), "backend/%s/%d", type, dom);
    if (!xs_watch(xenstore, path, token)) {
        xen_pv_printf(NULL, 0, "xen be: watching backend path (%s) failed\n",
                      path);
        return -1;
    }

    /* look for backends */
    dev = xs_directory(xenstore, 0, path, &cdev);
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

void xenstore_update_be(char *watch, char *type, int dom,
                        struct XenDevOps *ops)
{
    struct XenLegacyDevice *xendev;
    char path[XEN_BUFSIZE], *bepath;
    unsigned int len, dev;

    len = snprintf(path, sizeof(path), "backend/%s/%d", type, dom);
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

    xendev = xen_be_get_xendev(type, dom, dev, ops);
    if (xendev != NULL) {
        bepath = xs_read(xenstore, 0, xendev->be, &len);
        if (bepath == NULL) {
            xen_pv_del_xendev(xendev);
        } else {
            free(bepath);
            xen_be_backend_changed(xendev, path);
            xen_be_check_state(xendev);
        }
    }
}

void xenstore_update_fe(char *watch, struct XenLegacyDevice *xendev)
{
    char *node;
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
/* -------------------------------------------------------------------- */

int xen_be_init(void)
{
    xengnttab_handle *gnttabdev;

    xenstore = xs_daemon_open();
    if (!xenstore) {
        xen_pv_printf(NULL, 0, "can't connect to xenstored\n");
        return -1;
    }

    qemu_set_fd_handler(xs_fileno(xenstore), xenstore_update, NULL, NULL);

    if (xen_xc == NULL || xen_fmem == NULL) {
        /* Check if xen_init() have been called */
        goto err;
    }

    gnttabdev = xengnttab_open(NULL, 0);
    if (gnttabdev != NULL) {
        if (xengnttab_grant_copy(gnttabdev, 0, NULL) == 0) {
            xen_feature_grant_copy = true;
        }
        xengnttab_close(gnttabdev);
    }

    xen_sysdev = qdev_new(TYPE_XENSYSDEV);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(xen_sysdev), &error_fatal);
    xen_sysbus = qbus_new(TYPE_XENSYSBUS, xen_sysdev, "xen-sysbus");
    qbus_set_bus_hotplug_handler(xen_sysbus);

    return 0;

err:
    qemu_set_fd_handler(xs_fileno(xenstore), NULL, NULL, NULL);
    xs_daemon_close(xenstore);
    xenstore = NULL;

    return -1;
}

static void xen_set_dynamic_sysbus(void)
{
    Object *machine = qdev_get_machine();
    ObjectClass *oc = object_get_class(machine);
    MachineClass *mc = MACHINE_CLASS(oc);

    machine_class_allow_dynamic_sysbus_dev(mc, TYPE_XENSYSDEV);
}

int xen_be_register(const char *type, struct XenDevOps *ops)
{
    char path[50];
    int rc;

    if (ops->backend_register) {
        rc = ops->backend_register();
        if (rc) {
            return rc;
        }
    }

    snprintf(path, sizeof(path), "device-model/%u/backends/%s", xen_domid,
             type);
    xenstore_mkdir(path, XS_PERM_NONE);

    return xenstore_scan(type, xen_domid, ops);
}

void xen_be_register_common(void)
{
    xen_set_dynamic_sysbus();

    xen_be_register("console", &xen_console_ops);
    xen_be_register("vkbd", &xen_kbdmouse_ops);
#ifdef CONFIG_VIRTFS
    xen_be_register("9pfs", &xen_9pfs_ops);
#endif
#ifdef CONFIG_USB_LIBUSB
    xen_be_register("qusb", &xen_usb_ops);
#endif
}

int xen_be_bind_evtchn(struct XenLegacyDevice *xendev)
{
    if (xendev->local_port != -1) {
        return 0;
    }
    xendev->local_port = xenevtchn_bind_interdomain
        (xendev->evtchndev, xendev->dom, xendev->remote_port);
    if (xendev->local_port == -1) {
        xen_pv_printf(xendev, 0, "xenevtchn_bind_interdomain failed\n");
        return -1;
    }
    xen_pv_printf(xendev, 2, "bind evtchn port %d\n", xendev->local_port);
    qemu_set_fd_handler(xenevtchn_fd(xendev->evtchndev),
                        xen_pv_evtchn_event, NULL, xendev);
    return 0;
}


static Property xendev_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void xendev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, xendev_properties);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    /* xen-backend devices can be plugged/unplugged dynamically */
    dc->user_creatable = true;
    dc->bus_type = TYPE_XENSYSBUS;
}

static const TypeInfo xendev_type_info = {
    .name          = TYPE_XENBACKEND,
    .parent        = TYPE_DEVICE,
    .class_init    = xendev_class_init,
    .instance_size = sizeof(struct XenLegacyDevice),
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

static Property xen_sysdev_properties[] = {
    {/* end of property list */},
};

static void xen_sysdev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, xen_sysdev_properties);
}

static const TypeInfo xensysdev_info = {
    .name          = TYPE_XENSYSDEV,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SysBusDevice),
    .class_init    = xen_sysdev_class_init,
};

static void xenbe_register_types(void)
{
    type_register_static(&xensysbus_info);
    type_register_static(&xensysdev_info);
    type_register_static(&xendev_type_info);
}

type_init(xenbe_register_types)

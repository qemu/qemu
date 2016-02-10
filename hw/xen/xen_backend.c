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
#include <sys/mman.h>
#include <sys/signal.h>

#include "hw/hw.h"
#include "sysemu/char.h"
#include "qemu/log.h"
#include "hw/xen/xen_backend.h"

#include <xen/grant_table.h>

/* ------------------------------------------------------------- */

/* public */
xc_interface *xen_xc = NULL;
xenforeignmemory_handle *xen_fmem = NULL;
struct xs_handle *xenstore = NULL;
const char *xen_protocol;

/* private */
static QTAILQ_HEAD(XenDeviceHead, XenDevice) xendevs = QTAILQ_HEAD_INITIALIZER(xendevs);
static int debug = 0;

/* ------------------------------------------------------------- */

int xenstore_write_str(const char *base, const char *node, const char *val)
{
    char abspath[XEN_BUFSIZE];

    snprintf(abspath, sizeof(abspath), "%s/%s", base, node);
    if (!xs_write(xenstore, 0, abspath, val, strlen(val))) {
        return -1;
    }
    return 0;
}

char *xenstore_read_str(const char *base, const char *node)
{
    char abspath[XEN_BUFSIZE];
    unsigned int len;
    char *str, *ret = NULL;

    snprintf(abspath, sizeof(abspath), "%s/%s", base, node);
    str = xs_read(xenstore, 0, abspath, &len);
    if (str != NULL) {
        /* move to qemu-allocated memory to make sure
         * callers can savely g_free() stuff. */
        ret = g_strdup(str);
        free(str);
    }
    return ret;
}

int xenstore_write_int(const char *base, const char *node, int ival)
{
    char val[12];

    snprintf(val, sizeof(val), "%d", ival);
    return xenstore_write_str(base, node, val);
}

int xenstore_write_int64(const char *base, const char *node, int64_t ival)
{
    char val[21];

    snprintf(val, sizeof(val), "%"PRId64, ival);
    return xenstore_write_str(base, node, val);
}

int xenstore_read_int(const char *base, const char *node, int *ival)
{
    char *val;
    int rc = -1;

    val = xenstore_read_str(base, node);
    if (val && 1 == sscanf(val, "%d", ival)) {
        rc = 0;
    }
    g_free(val);
    return rc;
}

int xenstore_read_uint64(const char *base, const char *node, uint64_t *uval)
{
    char *val;
    int rc = -1;

    val = xenstore_read_str(base, node);
    if (val && 1 == sscanf(val, "%"SCNu64, uval)) {
        rc = 0;
    }
    g_free(val);
    return rc;
}

int xenstore_write_be_str(struct XenDevice *xendev, const char *node, const char *val)
{
    return xenstore_write_str(xendev->be, node, val);
}

int xenstore_write_be_int(struct XenDevice *xendev, const char *node, int ival)
{
    return xenstore_write_int(xendev->be, node, ival);
}

int xenstore_write_be_int64(struct XenDevice *xendev, const char *node, int64_t ival)
{
    return xenstore_write_int64(xendev->be, node, ival);
}

char *xenstore_read_be_str(struct XenDevice *xendev, const char *node)
{
    return xenstore_read_str(xendev->be, node);
}

int xenstore_read_be_int(struct XenDevice *xendev, const char *node, int *ival)
{
    return xenstore_read_int(xendev->be, node, ival);
}

char *xenstore_read_fe_str(struct XenDevice *xendev, const char *node)
{
    return xenstore_read_str(xendev->fe, node);
}

int xenstore_read_fe_int(struct XenDevice *xendev, const char *node, int *ival)
{
    return xenstore_read_int(xendev->fe, node, ival);
}

int xenstore_read_fe_uint64(struct XenDevice *xendev, const char *node, uint64_t *uval)
{
    return xenstore_read_uint64(xendev->fe, node, uval);
}

/* ------------------------------------------------------------- */

const char *xenbus_strstate(enum xenbus_state state)
{
    static const char *const name[] = {
        [ XenbusStateUnknown      ] = "Unknown",
        [ XenbusStateInitialising ] = "Initialising",
        [ XenbusStateInitWait     ] = "InitWait",
        [ XenbusStateInitialised  ] = "Initialised",
        [ XenbusStateConnected    ] = "Connected",
        [ XenbusStateClosing      ] = "Closing",
        [ XenbusStateClosed       ] = "Closed",
    };
    return (state < ARRAY_SIZE(name)) ? name[state] : "INVALID";
}

int xen_be_set_state(struct XenDevice *xendev, enum xenbus_state state)
{
    int rc;

    rc = xenstore_write_be_int(xendev, "state", state);
    if (rc < 0) {
        return rc;
    }
    xen_be_printf(xendev, 1, "backend state: %s -> %s\n",
                  xenbus_strstate(xendev->be_state), xenbus_strstate(state));
    xendev->be_state = state;
    return 0;
}

/* ------------------------------------------------------------- */

struct XenDevice *xen_be_find_xendev(const char *type, int dom, int dev)
{
    struct XenDevice *xendev;

    QTAILQ_FOREACH(xendev, &xendevs, next) {
        if (xendev->dom != dom) {
            continue;
        }
        if (xendev->dev != dev) {
            continue;
        }
        if (strcmp(xendev->type, type) != 0) {
            continue;
        }
        return xendev;
    }
    return NULL;
}

/*
 * get xen backend device, allocate a new one if it doesn't exist.
 */
static struct XenDevice *xen_be_get_xendev(const char *type, int dom, int dev,
                                           struct XenDevOps *ops)
{
    struct XenDevice *xendev;

    xendev = xen_be_find_xendev(type, dom, dev);
    if (xendev) {
        return xendev;
    }

    /* init new xendev */
    xendev = g_malloc0(ops->size);
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
        xen_be_printf(NULL, 0, "can't open evtchn device\n");
        g_free(xendev);
        return NULL;
    }
    fcntl(xenevtchn_fd(xendev->evtchndev), F_SETFD, FD_CLOEXEC);

    if (ops->flags & DEVOPS_FLAG_NEED_GNTDEV) {
        xendev->gnttabdev = xengnttab_open(NULL, 0);
        if (xendev->gnttabdev == NULL) {
            xen_be_printf(NULL, 0, "can't open gnttab device\n");
            xenevtchn_close(xendev->evtchndev);
            g_free(xendev);
            return NULL;
        }
    } else {
        xendev->gnttabdev = NULL;
    }

    QTAILQ_INSERT_TAIL(&xendevs, xendev, next);

    if (xendev->ops->alloc) {
        xendev->ops->alloc(xendev);
    }

    return xendev;
}

/*
 * release xen backend device.
 */
static struct XenDevice *xen_be_del_xendev(int dom, int dev)
{
    struct XenDevice *xendev, *xnext;

    /*
     * This is pretty much like QTAILQ_FOREACH(xendev, &xendevs, next) but
     * we save the next pointer in xnext because we might free xendev.
     */
    xnext = xendevs.tqh_first;
    while (xnext) {
        xendev = xnext;
        xnext = xendev->next.tqe_next;

        if (xendev->dom != dom) {
            continue;
        }
        if (xendev->dev != dev && dev != -1) {
            continue;
        }

        if (xendev->ops->free) {
            xendev->ops->free(xendev);
        }

        if (xendev->fe) {
            char token[XEN_BUFSIZE];
            snprintf(token, sizeof(token), "fe:%p", xendev);
            xs_unwatch(xenstore, xendev->fe, token);
            g_free(xendev->fe);
        }

        if (xendev->evtchndev != NULL) {
            xenevtchn_close(xendev->evtchndev);
        }
        if (xendev->gnttabdev != NULL) {
            xengnttab_close(xendev->gnttabdev);
        }

        QTAILQ_REMOVE(&xendevs, xendev, next);
        g_free(xendev);
    }
    return NULL;
}

/*
 * Sync internal data structures on xenstore updates.
 * Node specifies the changed field.  node = NULL means
 * update all fields (used for initialization).
 */
static void xen_be_backend_changed(struct XenDevice *xendev, const char *node)
{
    if (node == NULL  ||  strcmp(node, "online") == 0) {
        if (xenstore_read_be_int(xendev, "online", &xendev->online) == -1) {
            xendev->online = 0;
        }
    }

    if (node) {
        xen_be_printf(xendev, 2, "backend update: %s\n", node);
        if (xendev->ops->backend_changed) {
            xendev->ops->backend_changed(xendev, node);
        }
    }
}

static void xen_be_frontend_changed(struct XenDevice *xendev, const char *node)
{
    int fe_state;

    if (node == NULL  ||  strcmp(node, "state") == 0) {
        if (xenstore_read_fe_int(xendev, "state", &fe_state) == -1) {
            fe_state = XenbusStateUnknown;
        }
        if (xendev->fe_state != fe_state) {
            xen_be_printf(xendev, 1, "frontend state: %s -> %s\n",
                          xenbus_strstate(xendev->fe_state),
                          xenbus_strstate(fe_state));
        }
        xendev->fe_state = fe_state;
    }
    if (node == NULL  ||  strcmp(node, "protocol") == 0) {
        g_free(xendev->protocol);
        xendev->protocol = xenstore_read_fe_str(xendev, "protocol");
        if (xendev->protocol) {
            xen_be_printf(xendev, 1, "frontend protocol: %s\n", xendev->protocol);
        }
    }

    if (node) {
        xen_be_printf(xendev, 2, "frontend update: %s\n", node);
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
static int xen_be_try_setup(struct XenDevice *xendev)
{
    char token[XEN_BUFSIZE];
    int be_state;

    if (xenstore_read_be_int(xendev, "state", &be_state) == -1) {
        xen_be_printf(xendev, 0, "reading backend state failed\n");
        return -1;
    }

    if (be_state != XenbusStateInitialising) {
        xen_be_printf(xendev, 0, "initial backend state is wrong (%s)\n",
                      xenbus_strstate(be_state));
        return -1;
    }

    xendev->fe = xenstore_read_be_str(xendev, "frontend");
    if (xendev->fe == NULL) {
        xen_be_printf(xendev, 0, "reading frontend path failed\n");
        return -1;
    }

    /* setup frontend watch */
    snprintf(token, sizeof(token), "fe:%p", xendev);
    if (!xs_watch(xenstore, xendev->fe, token)) {
        xen_be_printf(xendev, 0, "watching frontend path (%s) failed\n",
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
static int xen_be_try_init(struct XenDevice *xendev)
{
    int rc = 0;

    if (!xendev->online) {
        xen_be_printf(xendev, 1, "not online\n");
        return -1;
    }

    if (xendev->ops->init) {
        rc = xendev->ops->init(xendev);
    }
    if (rc != 0) {
        xen_be_printf(xendev, 1, "init() failed\n");
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
static int xen_be_try_initialise(struct XenDevice *xendev)
{
    int rc = 0;

    if (xendev->fe_state != XenbusStateInitialised  &&
        xendev->fe_state != XenbusStateConnected) {
        if (xendev->ops->flags & DEVOPS_FLAG_IGNORE_STATE) {
            xen_be_printf(xendev, 2, "frontend not ready, ignoring\n");
        } else {
            xen_be_printf(xendev, 2, "frontend not ready (yet)\n");
            return -1;
        }
    }

    if (xendev->ops->initialise) {
        rc = xendev->ops->initialise(xendev);
    }
    if (rc != 0) {
        xen_be_printf(xendev, 0, "initialise() failed\n");
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
static void xen_be_try_connected(struct XenDevice *xendev)
{
    if (!xendev->ops->connected) {
        return;
    }

    if (xendev->fe_state != XenbusStateConnected) {
        if (xendev->ops->flags & DEVOPS_FLAG_IGNORE_STATE) {
            xen_be_printf(xendev, 2, "frontend not ready, ignoring\n");
        } else {
            xen_be_printf(xendev, 2, "frontend not ready (yet)\n");
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
static void xen_be_disconnect(struct XenDevice *xendev, enum xenbus_state state)
{
    if (xendev->be_state != XenbusStateClosing &&
        xendev->be_state != XenbusStateClosed  &&
        xendev->ops->disconnect) {
        xendev->ops->disconnect(xendev);
    }
    if (xendev->be_state != state) {
        xen_be_set_state(xendev, state);
    }
}

/*
 * Try to reset xendev, for reconnection by another frontend instance.
 */
static int xen_be_try_reset(struct XenDevice *xendev)
{
    if (xendev->fe_state != XenbusStateInitialising) {
        return -1;
    }

    xen_be_printf(xendev, 1, "device reset (for re-connect)\n");
    xen_be_set_state(xendev, XenbusStateInitialising);
    return 0;
}

/*
 * state change dispatcher function
 */
void xen_be_check_state(struct XenDevice *xendev)
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
    struct XenDevice *xendev;
    char path[XEN_BUFSIZE], token[XEN_BUFSIZE];
    char **dev = NULL;
    unsigned int cdev, j;

    /* setup watch */
    snprintf(token, sizeof(token), "be:%p:%d:%p", type, dom, ops);
    snprintf(path, sizeof(path), "backend/%s/%d", type, dom);
    if (!xs_watch(xenstore, path, token)) {
        xen_be_printf(NULL, 0, "xen be: watching backend path (%s) failed\n", path);
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

static void xenstore_update_be(char *watch, char *type, int dom,
                               struct XenDevOps *ops)
{
    struct XenDevice *xendev;
    char path[XEN_BUFSIZE], *bepath;
    unsigned int len, dev;

    len = snprintf(path, sizeof(path), "backend/%s/%d", type, dom);
    if (strncmp(path, watch, len) != 0) {
        return;
    }
    if (sscanf(watch+len, "/%u/%255s", &dev, path) != 2) {
        strcpy(path, "");
        if (sscanf(watch+len, "/%u", &dev) != 1) {
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
            xen_be_del_xendev(dom, dev);
        } else {
            free(bepath);
            xen_be_backend_changed(xendev, path);
            xen_be_check_state(xendev);
        }
    }
}

static void xenstore_update_fe(char *watch, struct XenDevice *xendev)
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

static void xenstore_update(void *unused)
{
    char **vec = NULL;
    intptr_t type, ops, ptr;
    unsigned int dom, count;

    vec = xs_read_watch(xenstore, &count);
    if (vec == NULL) {
        goto cleanup;
    }

    if (sscanf(vec[XS_WATCH_TOKEN], "be:%" PRIxPTR ":%d:%" PRIxPTR,
               &type, &dom, &ops) == 3) {
        xenstore_update_be(vec[XS_WATCH_PATH], (void*)type, dom, (void*)ops);
    }
    if (sscanf(vec[XS_WATCH_TOKEN], "fe:%" PRIxPTR, &ptr) == 1) {
        xenstore_update_fe(vec[XS_WATCH_PATH], (void*)ptr);
    }

cleanup:
    free(vec);
}

static void xen_be_evtchn_event(void *opaque)
{
    struct XenDevice *xendev = opaque;
    evtchn_port_t port;

    port = xenevtchn_pending(xendev->evtchndev);
    if (port != xendev->local_port) {
        xen_be_printf(xendev, 0,
                      "xenevtchn_pending returned %d (expected %d)\n",
                      port, xendev->local_port);
        return;
    }
    xenevtchn_unmask(xendev->evtchndev, port);

    if (xendev->ops->event) {
        xendev->ops->event(xendev);
    }
}

/* -------------------------------------------------------------------- */

int xen_be_init(void)
{
    xenstore = xs_daemon_open();
    if (!xenstore) {
        xen_be_printf(NULL, 0, "can't connect to xenstored\n");
        return -1;
    }

    qemu_set_fd_handler(xs_fileno(xenstore), xenstore_update, NULL, NULL);

    if (xen_xc == NULL || xen_fmem == NULL) {
        /* Check if xen_init() have been called */
        goto err;
    }
    return 0;

err:
    qemu_set_fd_handler(xs_fileno(xenstore), NULL, NULL, NULL);
    xs_daemon_close(xenstore);
    xenstore = NULL;

    return -1;
}

int xen_be_register(const char *type, struct XenDevOps *ops)
{
    return xenstore_scan(type, xen_domid, ops);
}

int xen_be_bind_evtchn(struct XenDevice *xendev)
{
    if (xendev->local_port != -1) {
        return 0;
    }
    xendev->local_port = xenevtchn_bind_interdomain
        (xendev->evtchndev, xendev->dom, xendev->remote_port);
    if (xendev->local_port == -1) {
        xen_be_printf(xendev, 0, "xenevtchn_bind_interdomain failed\n");
        return -1;
    }
    xen_be_printf(xendev, 2, "bind evtchn port %d\n", xendev->local_port);
    qemu_set_fd_handler(xenevtchn_fd(xendev->evtchndev),
                        xen_be_evtchn_event, NULL, xendev);
    return 0;
}

void xen_be_unbind_evtchn(struct XenDevice *xendev)
{
    if (xendev->local_port == -1) {
        return;
    }
    qemu_set_fd_handler(xenevtchn_fd(xendev->evtchndev), NULL, NULL, NULL);
    xenevtchn_unbind(xendev->evtchndev, xendev->local_port);
    xen_be_printf(xendev, 2, "unbind evtchn port %d\n", xendev->local_port);
    xendev->local_port = -1;
}

int xen_be_send_notify(struct XenDevice *xendev)
{
    return xenevtchn_notify(xendev->evtchndev, xendev->local_port);
}

/*
 * msg_level:
 *  0 == errors (stderr + logfile).
 *  1 == informative debug messages (logfile only).
 *  2 == noisy debug messages (logfile only).
 *  3 == will flood your log (logfile only).
 */
void xen_be_printf(struct XenDevice *xendev, int msg_level, const char *fmt, ...)
{
    va_list args;

    if (xendev) {
        if (msg_level > xendev->debug) {
            return;
        }
        qemu_log("xen be: %s: ", xendev->name);
        if (msg_level == 0) {
            fprintf(stderr, "xen be: %s: ", xendev->name);
        }
    } else {
        if (msg_level > debug) {
            return;
        }
        qemu_log("xen be core: ");
        if (msg_level == 0) {
            fprintf(stderr, "xen be core: ");
        }
    }
    va_start(args, fmt);
    qemu_log_vprintf(fmt, args);
    va_end(args);
    if (msg_level == 0) {
        va_start(args, fmt);
        vfprintf(stderr, fmt, args);
        va_end(args);
    }
    qemu_log_flush();
}

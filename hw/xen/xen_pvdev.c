/*
 * Xen para-virtualization device
 *
 *  (c) 2008 Gerd Hoffmann <kraxel@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/qdev-core.h"
#include "hw/xen/xen_backend.h"
#include "hw/xen/xen_pvdev.h"

/* private */
static int debug;

struct xs_dirs {
    char *xs_dir;
    QTAILQ_ENTRY(xs_dirs) list;
};

static QTAILQ_HEAD(xs_dirs_head, xs_dirs) xs_cleanup =
    QTAILQ_HEAD_INITIALIZER(xs_cleanup);

static QTAILQ_HEAD(XenDeviceHead, XenDevice) xendevs =
    QTAILQ_HEAD_INITIALIZER(xendevs);

/* ------------------------------------------------------------- */

static void xenstore_cleanup_dir(char *dir)
{
    struct xs_dirs *d;

    d = g_malloc(sizeof(*d));
    d->xs_dir = dir;
    QTAILQ_INSERT_TAIL(&xs_cleanup, d, list);
}

void xen_config_cleanup(void)
{
    struct xs_dirs *d;

    QTAILQ_FOREACH(d, &xs_cleanup, list) {
        xs_rm(xenstore, 0, d->xs_dir);
    }
}

int xenstore_mkdir(char *path, int p)
{
    struct xs_permissions perms[2] = {
        {
            .id    = 0, /* set owner: dom0 */
        }, {
            .id    = xen_domid,
            .perms = p,
        }
    };

    if (!xs_mkdir(xenstore, 0, path)) {
        xen_pv_printf(NULL, 0, "xs_mkdir %s: failed\n", path);
        return -1;
    }
    xenstore_cleanup_dir(g_strdup(path));

    if (!xs_set_permissions(xenstore, 0, path, perms, 2)) {
        xen_pv_printf(NULL, 0, "xs_set_permissions %s: failed\n", path);
        return -1;
    }
    return 0;
}

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

void xenstore_update(void *unused)
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
        xenstore_update_be(vec[XS_WATCH_PATH], (void *)type, dom, (void*)ops);
    }
    if (sscanf(vec[XS_WATCH_TOKEN], "fe:%" PRIxPTR, &ptr) == 1) {
        xenstore_update_fe(vec[XS_WATCH_PATH], (void *)ptr);
    }

cleanup:
    free(vec);
}

const char *xenbus_strstate(enum xenbus_state state)
{
    static const char *const name[] = {
        [XenbusStateUnknown]       = "Unknown",
        [XenbusStateInitialising]  = "Initialising",
        [XenbusStateInitWait]      = "InitWait",
        [XenbusStateInitialised]   = "Initialised",
        [XenbusStateConnected]     = "Connected",
        [XenbusStateClosing]       = "Closing",
        [XenbusStateClosed]        = "Closed",
    };
    return (state < ARRAY_SIZE(name)) ? name[state] : "INVALID";
}

/*
 * msg_level:
 *  0 == errors (stderr + logfile).
 *  1 == informative debug messages (logfile only).
 *  2 == noisy debug messages (logfile only).
 *  3 == will flood your log (logfile only).
 */
void xen_pv_printf(struct XenDevice *xendev, int msg_level,
                   const char *fmt, ...)
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

void xen_pv_evtchn_event(void *opaque)
{
    struct XenDevice *xendev = opaque;
    evtchn_port_t port;

    port = xenevtchn_pending(xendev->evtchndev);
    if (port != xendev->local_port) {
        xen_pv_printf(xendev, 0,
                      "xenevtchn_pending returned %d (expected %d)\n",
                      port, xendev->local_port);
        return;
    }
    xenevtchn_unmask(xendev->evtchndev, port);

    if (xendev->ops->event) {
        xendev->ops->event(xendev);
    }
}

void xen_pv_unbind_evtchn(struct XenDevice *xendev)
{
    if (xendev->local_port == -1) {
        return;
    }
    qemu_set_fd_handler(xenevtchn_fd(xendev->evtchndev), NULL, NULL, NULL);
    xenevtchn_unbind(xendev->evtchndev, xendev->local_port);
    xen_pv_printf(xendev, 2, "unbind evtchn port %d\n", xendev->local_port);
    xendev->local_port = -1;
}

int xen_pv_send_notify(struct XenDevice *xendev)
{
    return xenevtchn_notify(xendev->evtchndev, xendev->local_port);
}

/* ------------------------------------------------------------- */

struct XenDevice *xen_pv_find_xendev(const char *type, int dom, int dev)
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
 * release xen backend device.
 */
void xen_pv_del_xendev(struct XenDevice *xendev)
{
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

    qdev_unplug(&xendev->qdev, NULL);
}

void xen_pv_insert_xendev(struct XenDevice *xendev)
{
    QTAILQ_INSERT_TAIL(&xendevs, xendev, next);
}

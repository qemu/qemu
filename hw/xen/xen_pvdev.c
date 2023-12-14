/*
 * Xen para-virtualization device
 *
 *  (c) 2008 Gerd Hoffmann <kraxel@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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
#include "qemu/main-loop.h"
#include "hw/qdev-core.h"
#include "hw/xen/xen-legacy-backend.h"
#include "hw/xen/xen_pvdev.h"

/* private */
static int debug;

struct xs_dirs {
    char *xs_dir;
    QTAILQ_ENTRY(xs_dirs) list;
};

static QTAILQ_HEAD(, xs_dirs) xs_cleanup =
    QTAILQ_HEAD_INITIALIZER(xs_cleanup);

static QTAILQ_HEAD(, XenLegacyDevice) xendevs =
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
        qemu_xen_xs_destroy(xenstore, 0, d->xs_dir);
    }
}

int xenstore_mkdir(char *path, int p)
{
    if (!qemu_xen_xs_create(xenstore, 0, 0, xen_domid, p, path)) {
        xen_pv_printf(NULL, 0, "xs_mkdir %s: failed\n", path);
        return -1;
    }
    xenstore_cleanup_dir(g_strdup(path));
    return 0;
}

int xenstore_write_str(const char *base, const char *node, const char *val)
{
    char abspath[XEN_BUFSIZE];

    snprintf(abspath, sizeof(abspath), "%s/%s", base, node);
    if (!qemu_xen_xs_write(xenstore, 0, abspath, val, strlen(val))) {
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
    str = qemu_xen_xs_read(xenstore, 0, abspath, &len);
    if (str != NULL) {
        /* move to qemu-allocated memory to make sure
         * callers can safely g_free() stuff. */
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
G_GNUC_PRINTF(3, 0)
static void xen_pv_output_msg(struct XenLegacyDevice *xendev,
                              FILE *f, const char *fmt, va_list args)
{
    if (xendev) {
        fprintf(f, "xen be: %s: ", xendev->name);
    } else {
        fprintf(f, "xen be core: ");
    }
    vfprintf(f, fmt, args);
}

void xen_pv_printf(struct XenLegacyDevice *xendev, int msg_level,
                   const char *fmt, ...)
{
    FILE *logfile;
    va_list args;

    if (msg_level > (xendev ? xendev->debug : debug)) {
        return;
    }

    logfile = qemu_log_trylock();
    if (logfile) {
        va_start(args, fmt);
        xen_pv_output_msg(xendev, logfile, fmt, args);
        va_end(args);
        qemu_log_unlock(logfile);
    }

    if (msg_level == 0) {
        va_start(args, fmt);
        xen_pv_output_msg(xendev, stderr, fmt, args);
        va_end(args);
    }
}

void xen_pv_evtchn_event(void *opaque)
{
    struct XenLegacyDevice *xendev = opaque;
    evtchn_port_t port;

    port = qemu_xen_evtchn_pending(xendev->evtchndev);
    if (port != xendev->local_port) {
        xen_pv_printf(xendev, 0,
                      "xenevtchn_pending returned %d (expected %d)\n",
                      port, xendev->local_port);
        return;
    }
    qemu_xen_evtchn_unmask(xendev->evtchndev, port);

    if (xendev->ops->event) {
        xendev->ops->event(xendev);
    }
}

void xen_pv_unbind_evtchn(struct XenLegacyDevice *xendev)
{
    if (xendev->local_port == -1) {
        return;
    }
    qemu_set_fd_handler(qemu_xen_evtchn_fd(xendev->evtchndev), NULL, NULL, NULL);
    qemu_xen_evtchn_unbind(xendev->evtchndev, xendev->local_port);
    xen_pv_printf(xendev, 2, "unbind evtchn port %d\n", xendev->local_port);
    xendev->local_port = -1;
}

int xen_pv_send_notify(struct XenLegacyDevice *xendev)
{
    return qemu_xen_evtchn_notify(xendev->evtchndev, xendev->local_port);
}

/* ------------------------------------------------------------- */

struct XenLegacyDevice *xen_pv_find_xendev(const char *type, int dom, int dev)
{
    struct XenLegacyDevice *xendev;

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
void xen_pv_del_xendev(struct XenLegacyDevice *xendev)
{
    if (xendev->ops->free) {
        xendev->ops->free(xendev);
    }

    if (xendev->fe) {
        qemu_xen_xs_unwatch(xenstore, xendev->watch);
        g_free(xendev->fe);
    }

    if (xendev->evtchndev != NULL) {
        qemu_xen_evtchn_close(xendev->evtchndev);
    }
    if (xendev->gnttabdev != NULL) {
        qemu_xen_gnttab_close(xendev->gnttabdev);
    }

    QTAILQ_REMOVE(&xendevs, xendev, next);

    qdev_unplug(&xendev->qdev, NULL);
}

void xen_pv_insert_xendev(struct XenLegacyDevice *xendev)
{
    QTAILQ_INSERT_TAIL(&xendevs, xendev, next);
}

/*
 *  Copyright (C) International Business Machines  Corp., 2005
 *  Author(s): Anthony Liguori <aliguori@us.ibm.com>
 *
 *  Copyright (C) Red Hat 2007
 *
 *  Xen Console
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
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include <sys/select.h>
#include <termios.h>

#include "qapi/error.h"
#include "system/system.h"
#include "chardev/char-fe.h"
#include "hw/xen/xen-backend.h"
#include "hw/xen/xen-bus-helper.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/xen/interface/io/console.h"
#include "hw/xen/interface/io/xs_wire.h"
#include "hw/xen/interface/grant_table.h"
#include "hw/i386/kvm/xen_primary_console.h"
#include "trace.h"

struct buffer {
    uint8_t *data;
    size_t consumed;
    size_t size;
    size_t capacity;
    size_t max_capacity;
};

struct XenConsole {
    struct XenDevice  xendev;  /* must be first */
    XenEventChannel   *event_channel;
    int               dev;
    struct buffer     buffer;
    char              *fe_path;
    unsigned int      ring_ref;
    void              *sring;
    CharFrontend       chr;
    int               backlog;
};

#define TYPE_XEN_CONSOLE_DEVICE "xen-console"
OBJECT_DECLARE_SIMPLE_TYPE(XenConsole, XEN_CONSOLE_DEVICE)

static bool buffer_append(XenConsole *con)
{
    struct buffer *buffer = &con->buffer;
    XENCONS_RING_IDX cons, prod, size;
    struct xencons_interface *intf = con->sring;

    cons = intf->out_cons;
    prod = intf->out_prod;
    xen_mb();

    size = prod - cons;
    if ((size == 0) || (size > sizeof(intf->out)))
        return false;

    if ((buffer->capacity - buffer->size) < size) {
        buffer->capacity += (size + 1024);
        buffer->data = g_realloc(buffer->data, buffer->capacity);
    }

    while (cons != prod)
        buffer->data[buffer->size++] = intf->out[
            MASK_XENCONS_IDX(cons++, intf->out)];

    xen_mb();
    intf->out_cons = cons;
    xen_device_notify_event_channel(XEN_DEVICE(con), con->event_channel, NULL);

    if (buffer->max_capacity &&
        buffer->size > buffer->max_capacity) {
        /* Discard the middle of the data. */

        size_t over = buffer->size - buffer->max_capacity;
        uint8_t *maxpos = buffer->data + buffer->max_capacity;

        memmove(maxpos - over, maxpos, over);
        buffer->data = g_realloc(buffer->data, buffer->max_capacity);
        buffer->size = buffer->capacity = buffer->max_capacity;

        if (buffer->consumed > buffer->max_capacity - over)
            buffer->consumed = buffer->max_capacity - over;
    }
    return true;
}

static void buffer_advance(struct buffer *buffer, size_t len)
{
    buffer->consumed += len;
    if (buffer->consumed == buffer->size) {
        buffer->consumed = 0;
        buffer->size = 0;
    }
}

static int ring_free_bytes(XenConsole *con)
{
    struct xencons_interface *intf = con->sring;
    XENCONS_RING_IDX cons, prod, space;

    cons = intf->in_cons;
    prod = intf->in_prod;
    xen_mb();

    space = prod - cons;
    if (space > sizeof(intf->in))
        return 0; /* ring is screwed: ignore it */

    return (sizeof(intf->in) - space);
}

static int xencons_can_receive(void *opaque)
{
    XenConsole *con = opaque;
    return ring_free_bytes(con);
}

static void xencons_receive(void *opaque, const uint8_t *buf, int len)
{
    XenConsole *con = opaque;
    struct xencons_interface *intf = con->sring;
    XENCONS_RING_IDX prod;
    int i, max;

    max = ring_free_bytes(con);
    /* The can_receive() func limits this, but check again anyway */
    if (max < len)
        len = max;

    prod = intf->in_prod;
    for (i = 0; i < len; i++) {
        intf->in[MASK_XENCONS_IDX(prod++, intf->in)] =
            buf[i];
    }
    xen_wmb();
    intf->in_prod = prod;
    xen_device_notify_event_channel(XEN_DEVICE(con), con->event_channel, NULL);
}

static bool xencons_send(XenConsole *con)
{
    ssize_t len, size;

    size = con->buffer.size - con->buffer.consumed;
    if (qemu_chr_fe_backend_connected(&con->chr)) {
        len = qemu_chr_fe_write(&con->chr,
                                con->buffer.data + con->buffer.consumed,
                                size);
    } else {
        len = size;
    }
    if (len < 1) {
        if (!con->backlog) {
            con->backlog = 1;
        }
    } else {
        buffer_advance(&con->buffer, len);
        if (con->backlog && len == size) {
            con->backlog = 0;
        }
    }
    return len > 0;
}

/* -------------------------------------------------------------------- */

static bool con_event(void *_xendev)
{
    XenConsole *con = XEN_CONSOLE_DEVICE(_xendev);
    bool done_something;

    if (xen_device_backend_get_state(&con->xendev) != XenbusStateConnected) {
        return false;
    }

    done_something = buffer_append(con);

    if (con->buffer.size - con->buffer.consumed) {
        done_something |= xencons_send(con);
    }
    return done_something;
}

/* -------------------------------------------------------------------- */

static bool xen_console_connect(XenDevice *xendev, Error **errp)
{
    ERRP_GUARD();
    XenConsole *con = XEN_CONSOLE_DEVICE(xendev);
    unsigned int port, limit;

    if (xen_device_frontend_scanf(xendev, "ring-ref", "%u",
                                  &con->ring_ref) != 1) {
        error_setg(errp, "failed to read ring-ref");
        return false;
    }

    if (xen_device_frontend_scanf(xendev, "port", "%u", &port) != 1) {
        error_setg(errp, "failed to read remote port");
        return false;
    }

    if (xen_device_frontend_scanf(xendev, "limit", "%u", &limit) == 1) {
        con->buffer.max_capacity = limit;
    }

    con->event_channel = xen_device_bind_event_channel(xendev, port,
                                                       con_event,
                                                       con,
                                                       errp);
    if (!con->event_channel) {
        return false;
    }

    switch (con->dev) {
    case 0:
        /*
         * The primary console is special. For real Xen the ring-ref is
         * actually a GFN which needs to be mapped as foreignmem.
         */
        if (xen_mode != XEN_EMULATE) {
            xen_pfn_t mfn = (xen_pfn_t)con->ring_ref;
            con->sring = qemu_xen_foreignmem_map(xendev->frontend_id, NULL,
                                                 PROT_READ | PROT_WRITE,
                                                 1, &mfn, NULL);
            if (!con->sring) {
                error_setg(errp, "failed to map console page");
                return false;
            }
            break;
        }

        /*
         * For Xen emulation, we still follow the convention of ring-ref
         * holding the GFN, but we map the fixed GNTTAB_RESERVED_CONSOLE
         * grant ref because there is no implementation of foreignmem
         * operations for emulated mode. The emulation code which handles
         * the guest-side page and event channel also needs to be informed
         * of the backend event channel port, in order to reconnect to it
         * after a soft reset.
         */
        xen_primary_console_set_be_port(
            xen_event_channel_get_local_port(con->event_channel));
        con->ring_ref = GNTTAB_RESERVED_CONSOLE;
        /* fallthrough */
    default:
        con->sring = xen_device_map_grant_refs(xendev,
                                               &con->ring_ref, 1,
                                               PROT_READ | PROT_WRITE,
                                               errp);
        if (!con->sring) {
            error_prepend(errp, "failed to map console grant ref: ");
            return false;
        }
        break;
    }

    trace_xen_console_connect(con->dev, con->ring_ref, port,
                              con->buffer.max_capacity);

    qemu_chr_fe_set_handlers(&con->chr, xencons_can_receive,
                             xencons_receive, NULL, NULL, con, NULL,
                             true);
    return true;
}

static void xen_console_disconnect(XenDevice *xendev, Error **errp)
{
    XenConsole *con = XEN_CONSOLE_DEVICE(xendev);

    trace_xen_console_disconnect(con->dev);

    qemu_chr_fe_set_handlers(&con->chr, NULL, NULL, NULL, NULL,
                             con, NULL, true);

    if (con->event_channel) {
        xen_device_unbind_event_channel(xendev, con->event_channel,
                                        errp);
        con->event_channel = NULL;

        if (xen_mode == XEN_EMULATE && !con->dev) {
            xen_primary_console_set_be_port(0);
        }
    }

    if (con->sring) {
        if (!con->dev && xen_mode != XEN_EMULATE) {
            qemu_xen_foreignmem_unmap(con->sring, 1);
        } else {
            xen_device_unmap_grant_refs(xendev, con->sring,
                                        &con->ring_ref, 1, errp);
        }
        con->sring = NULL;
    }
}

static void xen_console_frontend_changed(XenDevice *xendev,
                                         enum xenbus_state frontend_state,
                                         Error **errp)
{
    ERRP_GUARD();
    enum xenbus_state backend_state = xen_device_backend_get_state(xendev);

    switch (frontend_state) {
    case XenbusStateInitialised:
    case XenbusStateConnected:
        if (backend_state == XenbusStateConnected) {
            break;
        }

        xen_console_disconnect(xendev, errp);
        if (*errp) {
            break;
        }

        if (!xen_console_connect(xendev, errp)) {
            xen_device_backend_set_state(xendev, XenbusStateClosing);
            break;
        }

        xen_device_backend_set_state(xendev, XenbusStateConnected);
        break;

    case XenbusStateClosing:
        xen_device_backend_set_state(xendev, XenbusStateClosing);
        break;

    case XenbusStateClosed:
    case XenbusStateUnknown:
        xen_console_disconnect(xendev, errp);
        if (*errp) {
            break;
        }

        xen_device_backend_set_state(xendev, XenbusStateClosed);
        break;

    default:
        break;
    }
}

static char *xen_console_get_name(XenDevice *xendev, Error **errp)
{
    XenConsole *con = XEN_CONSOLE_DEVICE(xendev);

    if (con->dev == -1) {
        XenBus *xenbus = XEN_BUS(qdev_get_parent_bus(DEVICE(xendev)));
        int idx = (xen_mode == XEN_EMULATE) ? 0 : 1;
        Error *local_err = NULL;
        char *value;

        /* Theoretically we could go up to INT_MAX here but that's overkill */
        while (idx < 100) {
            if (!idx) {
                value = xs_node_read(xenbus->xsh, XBT_NULL, NULL, &local_err,
                                     "/local/domain/%u/console",
                                     xendev->frontend_id);
            } else {
                value = xs_node_read(xenbus->xsh, XBT_NULL, NULL, &local_err,
                                     "/local/domain/%u/device/console/%u",
                                     xendev->frontend_id, idx);
            }
            if (!value) {
                if (errno == ENOENT) {
                    con->dev = idx;
                    error_free(local_err);
                    goto found;
                }
                error_propagate(errp, local_err);
                return NULL;
            }
            free(value);
            idx++;
        }
        error_setg(errp, "cannot find device index for console device");
        return NULL;
    }
 found:
    return g_strdup_printf("%u", con->dev);
}

static void xen_console_unrealize(XenDevice *xendev)
{
    XenConsole *con = XEN_CONSOLE_DEVICE(xendev);

    trace_xen_console_unrealize(con->dev);

    /* Disconnect from the frontend in case this has not already happened */
    xen_console_disconnect(xendev, NULL);

    qemu_chr_fe_deinit(&con->chr, false);
}

static void xen_console_realize(XenDevice *xendev, Error **errp)
{
    ERRP_GUARD();
    XenConsole *con = XEN_CONSOLE_DEVICE(xendev);
    Chardev *cs = qemu_chr_fe_get_driver(&con->chr);
    unsigned int u;

    if (!cs) {
        error_setg(errp, "no backing character device");
        return;
    }

    if (con->dev == -1) {
        error_setg(errp, "no device index provided");
        return;
    }

    /*
     * The Xen primary console is special. The ring-ref is actually a GFN to
     * be mapped directly as foreignmem (not a grant ref), and the guest port
     * was allocated *for* the guest by the toolstack. The guest gets these
     * through HVMOP_get_param and can use the console long before it's got
     * XenStore up and running. We cannot create those for a true Xen guest,
     * but we can for Xen emulation.
     */
    if (!con->dev) {
        if (xen_mode == XEN_EMULATE) {
            xen_primary_console_create();
        } else if (xen_device_frontend_scanf(xendev, "ring-ref", "%u", &u)
                   != 1 ||
                   xen_device_frontend_scanf(xendev, "port", "%u", &u) != 1) {
            error_setg(errp, "cannot create primary Xen console");
            return;
        }
    }

    trace_xen_console_realize(con->dev, object_get_typename(OBJECT(cs)));

    if (CHARDEV_IS_PTY(cs)) {
        /* Strip the leading 'pty:' */
        xen_device_frontend_printf(xendev, "tty", "%s", cs->filename + 4);
    }

    /* No normal PV driver initialization for the primary console under Xen */
    if (!con->dev && xen_mode != XEN_EMULATE) {
        xen_console_connect(xendev, errp);
    }
}

static char *console_frontend_path(struct qemu_xs_handle *xenstore,
                                   unsigned int dom_id, unsigned int dev)
{
    if (!dev) {
        return g_strdup_printf("/local/domain/%u/console", dom_id);
    } else {
        return g_strdup_printf("/local/domain/%u/device/console/%u", dom_id,
                               dev);
    }
}

static char *xen_console_get_frontend_path(XenDevice *xendev, Error **errp)
{
    XenConsole *con = XEN_CONSOLE_DEVICE(xendev);
    XenBus *xenbus = XEN_BUS(qdev_get_parent_bus(DEVICE(xendev)));
    char *ret = console_frontend_path(xenbus->xsh, xendev->frontend_id,
                                      con->dev);

    if (!ret) {
        error_setg(errp, "failed to create frontend path");
    }
    return ret;
}


static const Property xen_console_properties[] = {
    DEFINE_PROP_CHR("chardev", XenConsole, chr),
    DEFINE_PROP_INT32("idx", XenConsole, dev, -1),
};

static void xen_console_class_init(ObjectClass *class, const void *data)
{
    DeviceClass *dev_class = DEVICE_CLASS(class);
    XenDeviceClass *xendev_class = XEN_DEVICE_CLASS(class);

    xendev_class->backend = "console";
    xendev_class->device = "console";
    xendev_class->get_name = xen_console_get_name;
    xendev_class->realize = xen_console_realize;
    xendev_class->frontend_changed = xen_console_frontend_changed;
    xendev_class->unrealize = xen_console_unrealize;
    xendev_class->get_frontend_path = xen_console_get_frontend_path;

    device_class_set_props(dev_class, xen_console_properties);
}

static const TypeInfo xen_console_type_info = {
    .name = TYPE_XEN_CONSOLE_DEVICE,
    .parent = TYPE_XEN_DEVICE,
    .instance_size = sizeof(XenConsole),
    .class_init = xen_console_class_init,
};

static void xen_console_register_types(void)
{
    type_register_static(&xen_console_type_info);
}

type_init(xen_console_register_types)

/* Called to instantiate a XenConsole when the backend is detected. */
static void xen_console_device_create(XenBackendInstance *backend,
                                      QDict *opts, Error **errp)
{
    ERRP_GUARD();
    XenBus *xenbus = xen_backend_get_bus(backend);
    const char *name = xen_backend_get_name(backend);
    unsigned long number;
    char *fe = NULL, *type = NULL, *output = NULL;
    char label[32];
    XenDevice *xendev = NULL;
    XenConsole *con;
    Chardev *cd = NULL;
    struct qemu_xs_handle *xsh = xenbus->xsh;

    if (qemu_strtoul(name, NULL, 10, &number) || number > INT_MAX) {
        error_setg(errp, "failed to parse name '%s'", name);
        goto fail;
    }

    trace_xen_console_device_create(number);

    fe = console_frontend_path(xsh, xen_domid, number);
    if (fe == NULL) {
        error_setg(errp, "failed to generate frontend path");
        goto fail;
    }

    type = xs_node_read(xsh, XBT_NULL, NULL, errp, "%s/%s", fe, "type");
    if (!type) {
        error_prepend(errp, "failed to read console device type: ");
        goto fail;
    }

    if (strcmp(type, "ioemu")) {
        error_setg(errp, "declining to handle console type '%s'",
                   type);
        goto fail;
    }

    xendev = XEN_DEVICE(qdev_new(TYPE_XEN_CONSOLE_DEVICE));
    con = XEN_CONSOLE_DEVICE(xendev);

    con->dev = number;

    snprintf(label, sizeof(label), "xencons%ld", number);

    output = xs_node_read(xsh, XBT_NULL, NULL, errp, "%s/%s", fe, "output");
    if (output) {
        /*
         * FIXME: sure we want to support implicit
         * muxed monitors here?
         */
        cd = qemu_chr_new_mux_mon(label, output, NULL);
        if (!cd) {
            error_setg(errp, "console: No valid chardev found at '%s': ",
                       output);
            goto fail;
        }
    } else if (errno != ENOENT) {
        error_prepend(errp, "console: No valid chardev found: ");
        goto fail;
    } else {
        error_free(*errp);
        *errp = NULL;

        if (number) {
            cd = serial_hd(number);
            if (!cd) {
                error_setg(errp, "console: No serial device #%ld found",
                           number);
                goto fail;
            }
        } else {
            /* No 'output' node on primary console: use null. */
            cd = qemu_chr_new(label, "null", NULL);
            if (!cd) {
                error_setg(errp, "console: failed to create null device");
                goto fail;
            }
        }
    }

    if (!qemu_chr_fe_init(&con->chr, cd, errp)) {
        error_prepend(errp, "console: failed to initialize backing chardev: ");
        goto fail;
    }

    if (qdev_realize_and_unref(DEVICE(xendev), BUS(xenbus), errp)) {
        xen_backend_set_device(backend, xendev);
        goto done;
    }

    error_prepend(errp, "realization of console device %lu failed: ",
                  number);

 fail:
    if (xendev) {
        object_unparent(OBJECT(xendev));
    }
 done:
    g_free(fe);
    free(type);
    free(output);
}

static void xen_console_device_destroy(XenBackendInstance *backend,
                                       Error **errp)
{
    ERRP_GUARD();
    XenDevice *xendev = xen_backend_get_device(backend);
    XenConsole *con = XEN_CONSOLE_DEVICE(xendev);

    trace_xen_console_device_destroy(con->dev);

    object_unparent(OBJECT(xendev));
}

static const XenBackendInfo xen_console_backend_info  = {
    .type = "console",
    .create = xen_console_device_create,
    .destroy = xen_console_device_destroy,
};

static void xen_console_register_backend(void)
{
    xen_backend_register(&xen_console_backend_info);
}

xen_backend_init(xen_console_register_backend);

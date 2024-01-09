/*
 * QEMU Xen emulation: Shared/overlay pages support
 *
 * Copyright © 2022 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Authors: David Woodhouse <dwmw2@infradead.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "qemu/host-utils.h"
#include "qemu/module.h"
#include "qemu/main-loop.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qom/object.h"
#include "migration/vmstate.h"

#include "hw/sysbus.h"
#include "hw/xen/xen.h"
#include "hw/xen/xen_backend_ops.h"
#include "xen_overlay.h"
#include "xen_evtchn.h"
#include "xen_primary_console.h"
#include "xen_xenstore.h"

#include "sysemu/kvm.h"
#include "sysemu/kvm_xen.h"

#include "trace.h"

#include "xenstore_impl.h"

#include "hw/xen/interface/io/xs_wire.h"
#include "hw/xen/interface/event_channel.h"
#include "hw/xen/interface/grant_table.h"

#define TYPE_XEN_XENSTORE "xen-xenstore"
OBJECT_DECLARE_SIMPLE_TYPE(XenXenstoreState, XEN_XENSTORE)

#define ENTRIES_PER_FRAME_V1 (XEN_PAGE_SIZE / sizeof(grant_entry_v1_t))
#define ENTRIES_PER_FRAME_V2 (XEN_PAGE_SIZE / sizeof(grant_entry_v2_t))

#define XENSTORE_HEADER_SIZE ((unsigned int)sizeof(struct xsd_sockmsg))

struct XenXenstoreState {
    /*< private >*/
    SysBusDevice busdev;
    /*< public >*/

    XenstoreImplState *impl;
    GList *watch_events; /* for the guest */

    MemoryRegion xenstore_page;
    struct xenstore_domain_interface *xs;
    uint8_t req_data[XENSTORE_HEADER_SIZE + XENSTORE_PAYLOAD_MAX];
    uint8_t rsp_data[XENSTORE_HEADER_SIZE + XENSTORE_PAYLOAD_MAX];
    uint32_t req_offset;
    uint32_t rsp_offset;
    bool rsp_pending;
    bool fatal_error;

    evtchn_port_t guest_port;
    evtchn_port_t be_port;
    struct xenevtchn_handle *eh;

    uint8_t *impl_state;
    uint32_t impl_state_size;

    struct xengntdev_handle *gt;
    void *granted_xs;
};

struct XenXenstoreState *xen_xenstore_singleton;

static void xen_xenstore_event(void *opaque);
static void fire_watch_cb(void *opaque, const char *path, const char *token);

static struct xenstore_backend_ops emu_xenstore_backend_ops;

static void G_GNUC_PRINTF (4, 5) relpath_printf(XenXenstoreState *s,
                                                GList *perms,
                                                const char *relpath,
                                                const char *fmt, ...)
{
    gchar *abspath;
    gchar *value;
    va_list args;
    GByteArray *data;
    int err;

    abspath = g_strdup_printf("/local/domain/%u/%s", xen_domid, relpath);
    va_start(args, fmt);
    value = g_strdup_vprintf(fmt, args);
    va_end(args);

    data = g_byte_array_new_take((void *)value, strlen(value));

    err = xs_impl_write(s->impl, DOMID_QEMU, XBT_NULL, abspath, data);
    assert(!err);

    g_byte_array_unref(data);

    err = xs_impl_set_perms(s->impl, DOMID_QEMU, XBT_NULL, abspath, perms);
    assert(!err);

    g_free(abspath);
}

static void xen_xenstore_realize(DeviceState *dev, Error **errp)
{
    XenXenstoreState *s = XEN_XENSTORE(dev);
    GList *perms;

    if (xen_mode != XEN_EMULATE) {
        error_setg(errp, "Xen xenstore support is for Xen emulation");
        return;
    }
    memory_region_init_ram(&s->xenstore_page, OBJECT(dev), "xen:xenstore_page",
                           XEN_PAGE_SIZE, &error_abort);
    memory_region_set_enabled(&s->xenstore_page, true);
    s->xs = memory_region_get_ram_ptr(&s->xenstore_page);
    memset(s->xs, 0, XEN_PAGE_SIZE);

    /* We can't map it this early as KVM isn't ready */
    xen_xenstore_singleton = s;

    s->eh = xen_be_evtchn_open();
    if (!s->eh) {
        error_setg(errp, "Xenstore evtchn port init failed");
        return;
    }
    aio_set_fd_handler(qemu_get_aio_context(), xen_be_evtchn_fd(s->eh),
                       xen_xenstore_event, NULL, NULL, NULL, s);

    s->impl = xs_impl_create(xen_domid);

    /* Populate the default nodes */

    /* Nodes owned by 'dom0' but readable by the guest */
    perms = g_list_append(NULL, xs_perm_as_string(XS_PERM_NONE, DOMID_QEMU));
    perms = g_list_append(perms, xs_perm_as_string(XS_PERM_READ, xen_domid));

    relpath_printf(s, perms, "", "%s", "");

    relpath_printf(s, perms, "domid", "%u", xen_domid);

    relpath_printf(s, perms, "control/platform-feature-xs_reset_watches", "%u", 1);
    relpath_printf(s, perms, "control/platform-feature-multiprocessor-suspend", "%u", 1);

    relpath_printf(s, perms, "platform/acpi", "%u", 1);
    relpath_printf(s, perms, "platform/acpi_s3", "%u", 1);
    relpath_printf(s, perms, "platform/acpi_s4", "%u", 1);
    relpath_printf(s, perms, "platform/acpi_laptop_slate", "%u", 0);

    g_list_free_full(perms, g_free);

    /* Nodes owned by the guest */
    perms = g_list_append(NULL, xs_perm_as_string(XS_PERM_NONE, xen_domid));

    relpath_printf(s, perms, "attr", "%s", "");

    relpath_printf(s, perms, "control/shutdown", "%s", "");
    relpath_printf(s, perms, "control/feature-poweroff", "%u", 1);
    relpath_printf(s, perms, "control/feature-reboot", "%u", 1);
    relpath_printf(s, perms, "control/feature-suspend", "%u", 1);
    relpath_printf(s, perms, "control/feature-s3", "%u", 1);
    relpath_printf(s, perms, "control/feature-s4", "%u", 1);

    relpath_printf(s, perms, "data", "%s", "");
    relpath_printf(s, perms, "device", "%s", "");
    relpath_printf(s, perms, "drivers", "%s", "");
    relpath_printf(s, perms, "error", "%s", "");
    relpath_printf(s, perms, "feature", "%s", "");

    g_list_free_full(perms, g_free);

    xen_xenstore_ops = &emu_xenstore_backend_ops;
}

static bool xen_xenstore_is_needed(void *opaque)
{
    return xen_mode == XEN_EMULATE;
}

static int xen_xenstore_pre_save(void *opaque)
{
    XenXenstoreState *s = opaque;
    GByteArray *save;

    if (s->eh) {
        s->guest_port = xen_be_evtchn_get_guest_port(s->eh);
    }

    g_free(s->impl_state);
    save = xs_impl_serialize(s->impl);
    s->impl_state = save->data;
    s->impl_state_size = save->len;
    g_byte_array_free(save, false);

    return 0;
}

static int xen_xenstore_post_load(void *opaque, int ver)
{
    XenXenstoreState *s = opaque;
    GByteArray *save;
    int ret;

    /*
     * As qemu/dom0, rebind to the guest's port. The Windows drivers may
     * unbind the XenStore evtchn and rebind to it, having obtained the
     * "remote" port through EVTCHNOP_status. In the case that migration
     * occurs while it's unbound, the "remote" port needs to be the same
     * as before so that the guest can find it, but should remain unbound.
     */
    if (s->guest_port) {
        int be_port = xen_be_evtchn_bind_interdomain(s->eh, xen_domid,
                                                     s->guest_port);
        if (be_port < 0) {
            return be_port;
        }
        s->be_port = be_port;
    }

    save = g_byte_array_new_take(s->impl_state, s->impl_state_size);
    s->impl_state = NULL;
    s->impl_state_size = 0;

    ret = xs_impl_deserialize(s->impl, save, xen_domid, fire_watch_cb, s);
    return ret;
}

static const VMStateDescription xen_xenstore_vmstate = {
    .name = "xen_xenstore",
    .unmigratable = 1, /* The PV back ends don't migrate yet */
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = xen_xenstore_is_needed,
    .pre_save = xen_xenstore_pre_save,
    .post_load = xen_xenstore_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_ARRAY(req_data, XenXenstoreState,
                            sizeof_field(XenXenstoreState, req_data)),
        VMSTATE_UINT8_ARRAY(rsp_data, XenXenstoreState,
                            sizeof_field(XenXenstoreState, rsp_data)),
        VMSTATE_UINT32(req_offset, XenXenstoreState),
        VMSTATE_UINT32(rsp_offset, XenXenstoreState),
        VMSTATE_BOOL(rsp_pending, XenXenstoreState),
        VMSTATE_UINT32(guest_port, XenXenstoreState),
        VMSTATE_BOOL(fatal_error, XenXenstoreState),
        VMSTATE_UINT32(impl_state_size, XenXenstoreState),
        VMSTATE_VARRAY_UINT32_ALLOC(impl_state, XenXenstoreState,
                                    impl_state_size, 0,
                                    vmstate_info_uint8, uint8_t),
        VMSTATE_END_OF_LIST()
    }
};

static void xen_xenstore_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = xen_xenstore_realize;
    dc->vmsd = &xen_xenstore_vmstate;
}

static const TypeInfo xen_xenstore_info = {
    .name          = TYPE_XEN_XENSTORE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XenXenstoreState),
    .class_init    = xen_xenstore_class_init,
};

void xen_xenstore_create(void)
{
    DeviceState *dev = sysbus_create_simple(TYPE_XEN_XENSTORE, -1, NULL);

    xen_xenstore_singleton = XEN_XENSTORE(dev);

    /*
     * Defer the init (xen_xenstore_reset()) until KVM is set up and the
     * overlay page can be mapped.
     */
}

static void xen_xenstore_register_types(void)
{
    type_register_static(&xen_xenstore_info);
}

type_init(xen_xenstore_register_types)

uint16_t xen_xenstore_get_port(void)
{
    XenXenstoreState *s = xen_xenstore_singleton;
    if (!s) {
        return 0;
    }
    return s->guest_port;
}

static bool req_pending(XenXenstoreState *s)
{
    struct xsd_sockmsg *req = (struct xsd_sockmsg *)s->req_data;

    return s->req_offset == XENSTORE_HEADER_SIZE + req->len;
}

static void reset_req(XenXenstoreState *s)
{
    memset(s->req_data, 0, sizeof(s->req_data));
    s->req_offset = 0;
}

static void reset_rsp(XenXenstoreState *s)
{
    s->rsp_pending = false;

    memset(s->rsp_data, 0, sizeof(s->rsp_data));
    s->rsp_offset = 0;
}

static void xs_error(XenXenstoreState *s, unsigned int id,
                     xs_transaction_t tx_id, int errnum)
{
    struct xsd_sockmsg *rsp = (struct xsd_sockmsg *)s->rsp_data;
    const char *errstr = NULL;

    for (unsigned int i = 0; i < ARRAY_SIZE(xsd_errors); i++) {
        const struct xsd_errors *xsd_error = &xsd_errors[i];

        if (xsd_error->errnum == errnum) {
            errstr = xsd_error->errstring;
            break;
        }
    }
    assert(errstr);

    trace_xenstore_error(id, tx_id, errstr);

    rsp->type = XS_ERROR;
    rsp->req_id = id;
    rsp->tx_id = tx_id;
    rsp->len = (uint32_t)strlen(errstr) + 1;

    memcpy(&rsp[1], errstr, rsp->len);
}

static void xs_ok(XenXenstoreState *s, unsigned int type, unsigned int req_id,
                  xs_transaction_t tx_id)
{
    struct xsd_sockmsg *rsp = (struct xsd_sockmsg *)s->rsp_data;
    const char *okstr = "OK";

    rsp->type = type;
    rsp->req_id = req_id;
    rsp->tx_id = tx_id;
    rsp->len = (uint32_t)strlen(okstr) + 1;

    memcpy(&rsp[1], okstr, rsp->len);
}

/*
 * The correct request and response formats are documented in xen.git:
 * docs/misc/xenstore.txt. A summary is given below for convenience.
 * The '|' symbol represents a NUL character.
 *
 * ---------- Database read, write and permissions operations ----------
 *
 * READ                    <path>|                 <value|>
 * WRITE                   <path>|<value|>
 *         Store and read the octet string <value> at <path>.
 *         WRITE creates any missing parent paths, with empty values.
 *
 * MKDIR                   <path>|
 *         Ensures that the <path> exists, by necessary by creating
 *         it and any missing parents with empty values.  If <path>
 *         or any parent already exists, its value is left unchanged.
 *
 * RM                      <path>|
 *         Ensures that the <path> does not exist, by deleting
 *         it and all of its children.  It is not an error if <path> does
 *         not exist, but it _is_ an error if <path>'s immediate parent
 *         does not exist either.
 *
 * DIRECTORY               <path>|                 <child-leaf-name>|*
 *         Gives a list of the immediate children of <path>, as only the
 *         leafnames.  The resulting children are each named
 *         <path>/<child-leaf-name>.
 *
 * DIRECTORY_PART          <path>|<offset>         <gencnt>|<child-leaf-name>|*
 *         Same as DIRECTORY, but to be used for children lists longer than
 *         XENSTORE_PAYLOAD_MAX. Input are <path> and the byte offset into
 *         the list of children to return. Return values are the generation
 *         count <gencnt> of the node (to be used to ensure the node hasn't
 *         changed between two reads: <gencnt> being the same for multiple
 *         reads guarantees the node hasn't changed) and the list of children
 *         starting at the specified <offset> of the complete list.
 *
 * GET_PERMS               <path>|                 <perm-as-string>|+
 * SET_PERMS               <path>|<perm-as-string>|+?
 *         <perm-as-string> is one of the following
 *                 w<domid>        write only
 *                 r<domid>        read only
 *                 b<domid>        both read and write
 *                 n<domid>        no access
 *         See https://wiki.xen.org/wiki/XenBus section
 *         `Permissions' for details of the permissions system.
 *         It is possible to set permissions for the special watch paths
 *         "@introduceDomain" and "@releaseDomain" to enable receiving those
 *         watches in unprivileged domains.
 *
 * ---------- Watches ----------
 *
 * WATCH                   <wpath>|<token>|?
 *         Adds a watch.
 *
 *         When a <path> is modified (including path creation, removal,
 *         contents change or permissions change) this generates an event
 *         on the changed <path>.  Changes made in transactions cause an
 *         event only if and when committed.  Each occurring event is
 *         matched against all the watches currently set up, and each
 *         matching watch results in a WATCH_EVENT message (see below).
 *
 *         The event's path matches the watch's <wpath> if it is an child
 *         of <wpath>.
 *
 *         <wpath> can be a <path> to watch or @<wspecial>.  In the
 *         latter case <wspecial> may have any syntax but it matches
 *         (according to the rules above) only the following special
 *         events which are invented by xenstored:
 *             @introduceDomain    occurs on INTRODUCE
 *             @releaseDomain      occurs on any domain crash or
 *                                 shutdown, and also on RELEASE
 *                                 and domain destruction
 *         <wspecial> events are sent to privileged callers or explicitly
 *         via SET_PERMS enabled domains only.
 *
 *         When a watch is first set up it is triggered once straight
 *         away, with <path> equal to <wpath>.  Watches may be triggered
 *         spuriously.  The tx_id in a WATCH request is ignored.
 *
 *         Watches are supposed to be restricted by the permissions
 *         system but in practice the implementation is imperfect.
 *         Applications should not rely on being sent a notification for
 *         paths that they cannot read; however, an application may rely
 *         on being sent a watch when a path which it _is_ able to read
 *         is deleted even if that leaves only a nonexistent unreadable
 *         parent.  A notification may omitted if a node's permissions
 *         are changed so as to make it unreadable, in which case future
 *         notifications may be suppressed (and if the node is later made
 *         readable, some notifications may have been lost).
 *
 * WATCH_EVENT                                     <epath>|<token>|
 *         Unsolicited `reply' generated for matching modification events
 *         as described above.  req_id and tx_id are both 0.
 *
 *         <epath> is the event's path, ie the actual path that was
 *         modified; however if the event was the recursive removal of an
 *         parent of <wpath>, <epath> is just
 *         <wpath> (rather than the actual path which was removed).  So
 *         <epath> is a child of <wpath>, regardless.
 *
 *         Iff <wpath> for the watch was specified as a relative pathname,
 *         the <epath> path will also be relative (with the same base,
 *         obviously).
 *
 * UNWATCH                 <wpath>|<token>|?
 *
 * RESET_WATCHES           |
 *         Reset all watches and transactions of the caller.
 *
 * ---------- Transactions ----------
 *
 * TRANSACTION_START       |                       <transid>|
 *         <transid> is an opaque uint32_t allocated by xenstored
 *         represented as unsigned decimal.  After this, transaction may
 *         be referenced by using <transid> (as 32-bit binary) in the
 *         tx_id request header field.  When transaction is started whole
 *         db is copied; reads and writes happen on the copy.
 *         It is not legal to send non-0 tx_id in TRANSACTION_START.
 *
 * TRANSACTION_END         T|
 * TRANSACTION_END         F|
 *         tx_id must refer to existing transaction.  After this
 *         request the tx_id is no longer valid and may be reused by
 *         xenstore.  If F, the transaction is discarded.  If T,
 *         it is committed: if there were any other intervening writes
 *         then our END gets get EAGAIN.
 *
 *         The plan is that in the future only intervening `conflicting'
 *         writes cause EAGAIN, meaning only writes or other commits
 *         which changed paths which were read or written in the
 *         transaction at hand.
 *
 */

static void xs_read(XenXenstoreState *s, unsigned int req_id,
                    xs_transaction_t tx_id, uint8_t *req_data, unsigned int len)
{
    const char *path = (const char *)req_data;
    struct xsd_sockmsg *rsp = (struct xsd_sockmsg *)s->rsp_data;
    uint8_t *rsp_data = (uint8_t *)&rsp[1];
    g_autoptr(GByteArray) data = g_byte_array_new();
    int err;

    if (len == 0 || req_data[len - 1] != '\0') {
        xs_error(s, req_id, tx_id, EINVAL);
        return;
    }

    trace_xenstore_read(tx_id, path);
    err = xs_impl_read(s->impl, xen_domid, tx_id, path, data);
    if (err) {
        xs_error(s, req_id, tx_id, err);
        return;
    }

    rsp->type = XS_READ;
    rsp->req_id = req_id;
    rsp->tx_id = tx_id;
    rsp->len = 0;

    len = data->len;
    if (len > XENSTORE_PAYLOAD_MAX) {
        xs_error(s, req_id, tx_id, E2BIG);
        return;
    }

    memcpy(&rsp_data[rsp->len], data->data, len);
    rsp->len += len;
}

static void xs_write(XenXenstoreState *s, unsigned int req_id,
                     xs_transaction_t tx_id, uint8_t *req_data,
                     unsigned int len)
{
    g_autoptr(GByteArray) data = g_byte_array_new();
    const char *path;
    int err;

    if (len == 0) {
        xs_error(s, req_id, tx_id, EINVAL);
        return;
    }

    path = (const char *)req_data;

    while (len--) {
        if (*req_data++ == '\0') {
            break;
        }
        if (len == 0) {
            xs_error(s, req_id, tx_id, EINVAL);
            return;
        }
    }

    g_byte_array_append(data, req_data, len);

    trace_xenstore_write(tx_id, path);
    err = xs_impl_write(s->impl, xen_domid, tx_id, path, data);
    if (err) {
        xs_error(s, req_id, tx_id, err);
        return;
    }

    xs_ok(s, XS_WRITE, req_id, tx_id);
}

static void xs_mkdir(XenXenstoreState *s, unsigned int req_id,
                     xs_transaction_t tx_id, uint8_t *req_data,
                     unsigned int len)
{
    g_autoptr(GByteArray) data = g_byte_array_new();
    const char *path;
    int err;

    if (len == 0 || req_data[len - 1] != '\0') {
        xs_error(s, req_id, tx_id, EINVAL);
        return;
    }

    path = (const char *)req_data;

    trace_xenstore_mkdir(tx_id, path);
    err = xs_impl_read(s->impl, xen_domid, tx_id, path, data);
    if (err == ENOENT) {
        err = xs_impl_write(s->impl, xen_domid, tx_id, path, data);
    }

    if (!err) {
        xs_error(s, req_id, tx_id, err);
        return;
    }

    xs_ok(s, XS_MKDIR, req_id, tx_id);
}

static void xs_append_strings(XenXenstoreState *s, struct xsd_sockmsg *rsp,
                              GList *strings, unsigned int start, bool truncate)
{
    uint8_t *rsp_data = (uint8_t *)&rsp[1];
    GList *l;

    for (l = strings; l; l = l->next) {
        size_t len = strlen(l->data) + 1; /* Including the NUL termination */
        char *str = l->data;

        if (rsp->len + len > XENSTORE_PAYLOAD_MAX) {
            if (truncate) {
                len = XENSTORE_PAYLOAD_MAX - rsp->len;
                if (!len) {
                    return;
                }
            } else {
                xs_error(s, rsp->req_id, rsp->tx_id, E2BIG);
                return;
            }
        }

        if (start) {
            if (start >= len) {
                start -= len;
                continue;
            }

            str += start;
            len -= start;
            start = 0;
        }

        memcpy(&rsp_data[rsp->len], str, len);
        rsp->len += len;
    }
    /* XS_DIRECTORY_PART wants an extra NUL to indicate the end */
    if (truncate && rsp->len < XENSTORE_PAYLOAD_MAX) {
        rsp_data[rsp->len++] = '\0';
    }
}

static void xs_directory(XenXenstoreState *s, unsigned int req_id,
                         xs_transaction_t tx_id, uint8_t *req_data,
                         unsigned int len)
{
    struct xsd_sockmsg *rsp = (struct xsd_sockmsg *)s->rsp_data;
    GList *items = NULL;
    const char *path;
    int err;

    if (len == 0 || req_data[len - 1] != '\0') {
        xs_error(s, req_id, tx_id, EINVAL);
        return;
    }

    path = (const char *)req_data;

    trace_xenstore_directory(tx_id, path);
    err = xs_impl_directory(s->impl, xen_domid, tx_id, path, NULL, &items);
    if (err != 0) {
        xs_error(s, req_id, tx_id, err);
        return;
    }

    rsp->type = XS_DIRECTORY;
    rsp->req_id = req_id;
    rsp->tx_id = tx_id;
    rsp->len = 0;

    xs_append_strings(s, rsp, items, 0, false);

    g_list_free_full(items, g_free);
}

static void xs_directory_part(XenXenstoreState *s, unsigned int req_id,
                              xs_transaction_t tx_id, uint8_t *req_data,
                              unsigned int len)
{
    const char *offset_str, *path = (const char *)req_data;
    struct xsd_sockmsg *rsp = (struct xsd_sockmsg *)s->rsp_data;
    char *rsp_data = (char *)&rsp[1];
    uint64_t gencnt = 0;
    unsigned int offset;
    GList *items = NULL;
    int err;

    if (len == 0) {
        xs_error(s, req_id, tx_id, EINVAL);
        return;
    }

    while (len--) {
        if (*req_data++ == '\0') {
            break;
        }
        if (len == 0) {
            xs_error(s, req_id, tx_id, EINVAL);
            return;
        }
    }

    offset_str = (const char *)req_data;
    while (len--) {
        if (*req_data++ == '\0') {
            break;
        }
        if (len == 0) {
            xs_error(s, req_id, tx_id, EINVAL);
            return;
        }
    }

    if (len) {
        xs_error(s, req_id, tx_id, EINVAL);
        return;
    }

    if (qemu_strtoui(offset_str, NULL, 10, &offset) < 0) {
        xs_error(s, req_id, tx_id, EINVAL);
        return;
    }

    trace_xenstore_directory_part(tx_id, path, offset);
    err = xs_impl_directory(s->impl, xen_domid, tx_id, path, &gencnt, &items);
    if (err != 0) {
        xs_error(s, req_id, tx_id, err);
        return;
    }

    rsp->type = XS_DIRECTORY_PART;
    rsp->req_id = req_id;
    rsp->tx_id = tx_id;
    rsp->len = snprintf(rsp_data, XENSTORE_PAYLOAD_MAX, "%" PRIu64, gencnt) + 1;

    xs_append_strings(s, rsp, items, offset, true);

    g_list_free_full(items, g_free);
}

static void xs_transaction_start(XenXenstoreState *s, unsigned int req_id,
                                 xs_transaction_t tx_id, uint8_t *req_data,
                                 unsigned int len)
{
    struct xsd_sockmsg *rsp = (struct xsd_sockmsg *)s->rsp_data;
    char *rsp_data = (char *)&rsp[1];
    int err;

    if (len != 1 || req_data[0] != '\0') {
        xs_error(s, req_id, tx_id, EINVAL);
        return;
    }

    rsp->type = XS_TRANSACTION_START;
    rsp->req_id = req_id;
    rsp->tx_id = tx_id;
    rsp->len = 0;

    err = xs_impl_transaction_start(s->impl, xen_domid, &tx_id);
    if (err) {
        xs_error(s, req_id, tx_id, err);
        return;
    }

    trace_xenstore_transaction_start(tx_id);

    rsp->len = snprintf(rsp_data, XENSTORE_PAYLOAD_MAX, "%u", tx_id);
    assert(rsp->len < XENSTORE_PAYLOAD_MAX);
    rsp->len++;
}

static void xs_transaction_end(XenXenstoreState *s, unsigned int req_id,
                               xs_transaction_t tx_id, uint8_t *req_data,
                               unsigned int len)
{
    bool commit;
    int err;

    if (len != 2 || req_data[1] != '\0') {
        xs_error(s, req_id, tx_id, EINVAL);
        return;
    }

    switch (req_data[0]) {
    case 'T':
        commit = true;
        break;
    case 'F':
        commit = false;
        break;
    default:
        xs_error(s, req_id, tx_id, EINVAL);
        return;
    }

    trace_xenstore_transaction_end(tx_id, commit);
    err = xs_impl_transaction_end(s->impl, xen_domid, tx_id, commit);
    if (err) {
        xs_error(s, req_id, tx_id, err);
        return;
    }

    xs_ok(s, XS_TRANSACTION_END, req_id, tx_id);
}

static void xs_rm(XenXenstoreState *s, unsigned int req_id,
                  xs_transaction_t tx_id, uint8_t *req_data, unsigned int len)
{
    const char *path = (const char *)req_data;
    int err;

    if (len == 0 || req_data[len - 1] != '\0') {
        xs_error(s, req_id, tx_id, EINVAL);
        return;
    }

    trace_xenstore_rm(tx_id, path);
    err = xs_impl_rm(s->impl, xen_domid, tx_id, path);
    if (err) {
        xs_error(s, req_id, tx_id, err);
        return;
    }

    xs_ok(s, XS_RM, req_id, tx_id);
}

static void xs_get_perms(XenXenstoreState *s, unsigned int req_id,
                         xs_transaction_t tx_id, uint8_t *req_data,
                         unsigned int len)
{
    const char *path = (const char *)req_data;
    struct xsd_sockmsg *rsp = (struct xsd_sockmsg *)s->rsp_data;
    GList *perms = NULL;
    int err;

    if (len == 0 || req_data[len - 1] != '\0') {
        xs_error(s, req_id, tx_id, EINVAL);
        return;
    }

    trace_xenstore_get_perms(tx_id, path);
    err = xs_impl_get_perms(s->impl, xen_domid, tx_id, path, &perms);
    if (err) {
        xs_error(s, req_id, tx_id, err);
        return;
    }

    rsp->type = XS_GET_PERMS;
    rsp->req_id = req_id;
    rsp->tx_id = tx_id;
    rsp->len = 0;

    xs_append_strings(s, rsp, perms, 0, false);

    g_list_free_full(perms, g_free);
}

static void xs_set_perms(XenXenstoreState *s, unsigned int req_id,
                         xs_transaction_t tx_id, uint8_t *req_data,
                         unsigned int len)
{
    const char *path = (const char *)req_data;
    uint8_t *perm;
    GList *perms = NULL;
    int err;

    if (len == 0) {
        xs_error(s, req_id, tx_id, EINVAL);
        return;
    }

    while (len--) {
        if (*req_data++ == '\0') {
            break;
        }
        if (len == 0) {
            xs_error(s, req_id, tx_id, EINVAL);
            return;
        }
    }

    perm = req_data;
    while (len--) {
        if (*req_data++ == '\0') {
            perms = g_list_append(perms, perm);
            perm = req_data;
        }
    }

    /*
     * Note that there may be trailing garbage at the end of the buffer.
     * This is explicitly permitted by the '?' at the end of the definition:
     *
     *    SET_PERMS         <path>|<perm-as-string>|+?
     */

    trace_xenstore_set_perms(tx_id, path);
    err = xs_impl_set_perms(s->impl, xen_domid, tx_id, path, perms);
    g_list_free(perms);
    if (err) {
        xs_error(s, req_id, tx_id, err);
        return;
    }

    xs_ok(s, XS_SET_PERMS, req_id, tx_id);
}

static void xs_watch(XenXenstoreState *s, unsigned int req_id,
                     xs_transaction_t tx_id, uint8_t *req_data,
                     unsigned int len)
{
    const char *token, *path = (const char *)req_data;
    int err;

    if (len == 0) {
        xs_error(s, req_id, tx_id, EINVAL);
        return;
    }

    while (len--) {
        if (*req_data++ == '\0') {
            break;
        }
        if (len == 0) {
            xs_error(s, req_id, tx_id, EINVAL);
            return;
        }
    }

    token = (const char *)req_data;
    while (len--) {
        if (*req_data++ == '\0') {
            break;
        }
        if (len == 0) {
            xs_error(s, req_id, tx_id, EINVAL);
            return;
        }
    }

    /*
     * Note that there may be trailing garbage at the end of the buffer.
     * This is explicitly permitted by the '?' at the end of the definition:
     *
     *    WATCH             <wpath>|<token>|?
     */

    trace_xenstore_watch(path, token);
    err = xs_impl_watch(s->impl, xen_domid, path, token, fire_watch_cb, s);
    if (err) {
        xs_error(s, req_id, tx_id, err);
        return;
    }

    xs_ok(s, XS_WATCH, req_id, tx_id);
}

static void xs_unwatch(XenXenstoreState *s, unsigned int req_id,
                       xs_transaction_t tx_id, uint8_t *req_data,
                       unsigned int len)
{
    const char *token, *path = (const char *)req_data;
    int err;

    if (len == 0) {
        xs_error(s, req_id, tx_id, EINVAL);
        return;
    }

    while (len--) {
        if (*req_data++ == '\0') {
            break;
        }
        if (len == 0) {
            xs_error(s, req_id, tx_id, EINVAL);
            return;
        }
    }

    token = (const char *)req_data;
    while (len--) {
        if (*req_data++ == '\0') {
            break;
        }
        if (len == 0) {
            xs_error(s, req_id, tx_id, EINVAL);
            return;
        }
    }

    trace_xenstore_unwatch(path, token);
    err = xs_impl_unwatch(s->impl, xen_domid, path, token, fire_watch_cb, s);
    if (err) {
        xs_error(s, req_id, tx_id, err);
        return;
    }

    xs_ok(s, XS_UNWATCH, req_id, tx_id);
}

static void xs_reset_watches(XenXenstoreState *s, unsigned int req_id,
                             xs_transaction_t tx_id, uint8_t *req_data,
                             unsigned int len)
{
    if (len == 0 || req_data[len - 1] != '\0') {
        xs_error(s, req_id, tx_id, EINVAL);
        return;
    }

    trace_xenstore_reset_watches();
    xs_impl_reset_watches(s->impl, xen_domid);

    xs_ok(s, XS_RESET_WATCHES, req_id, tx_id);
}

static void xs_priv(XenXenstoreState *s, unsigned int req_id,
                    xs_transaction_t tx_id, uint8_t *data,
                    unsigned int len)
{
    xs_error(s, req_id, tx_id, EACCES);
}

static void xs_unimpl(XenXenstoreState *s, unsigned int req_id,
                      xs_transaction_t tx_id, uint8_t *data,
                      unsigned int len)
{
    xs_error(s, req_id, tx_id, ENOSYS);
}

typedef void (*xs_impl)(XenXenstoreState *s, unsigned int req_id,
                        xs_transaction_t tx_id, uint8_t *data,
                        unsigned int len);

struct xsd_req {
    const char *name;
    xs_impl fn;
};
#define XSD_REQ(_type, _fn)                           \
    [_type] = { .name = #_type, .fn = _fn }

struct xsd_req xsd_reqs[] = {
    XSD_REQ(XS_READ, xs_read),
    XSD_REQ(XS_WRITE, xs_write),
    XSD_REQ(XS_MKDIR, xs_mkdir),
    XSD_REQ(XS_DIRECTORY, xs_directory),
    XSD_REQ(XS_DIRECTORY_PART, xs_directory_part),
    XSD_REQ(XS_TRANSACTION_START, xs_transaction_start),
    XSD_REQ(XS_TRANSACTION_END, xs_transaction_end),
    XSD_REQ(XS_RM, xs_rm),
    XSD_REQ(XS_GET_PERMS, xs_get_perms),
    XSD_REQ(XS_SET_PERMS, xs_set_perms),
    XSD_REQ(XS_WATCH, xs_watch),
    XSD_REQ(XS_UNWATCH, xs_unwatch),
    XSD_REQ(XS_CONTROL, xs_priv),
    XSD_REQ(XS_INTRODUCE, xs_priv),
    XSD_REQ(XS_RELEASE, xs_priv),
    XSD_REQ(XS_IS_DOMAIN_INTRODUCED, xs_priv),
    XSD_REQ(XS_RESUME, xs_priv),
    XSD_REQ(XS_SET_TARGET, xs_priv),
    XSD_REQ(XS_RESET_WATCHES, xs_reset_watches),
};

static void process_req(XenXenstoreState *s)
{
    struct xsd_sockmsg *req = (struct xsd_sockmsg *)s->req_data;
    xs_impl handler = NULL;

    assert(req_pending(s));
    assert(!s->rsp_pending);

    if (req->type < ARRAY_SIZE(xsd_reqs)) {
        handler = xsd_reqs[req->type].fn;
    }
    if (!handler) {
        handler = &xs_unimpl;
    }

    handler(s, req->req_id, req->tx_id, (uint8_t *)&req[1], req->len);

    s->rsp_pending = true;
    reset_req(s);
}

static unsigned int copy_from_ring(XenXenstoreState *s, uint8_t *ptr,
                                   unsigned int len)
{
    if (!len) {
        return 0;
    }

    XENSTORE_RING_IDX prod = qatomic_read(&s->xs->req_prod);
    XENSTORE_RING_IDX cons = qatomic_read(&s->xs->req_cons);
    unsigned int copied = 0;

    /* Ensure the ring contents don't cross the req_prod access. */
    smp_rmb();

    while (len) {
        unsigned int avail = prod - cons;
        unsigned int offset = MASK_XENSTORE_IDX(cons);
        unsigned int copylen = avail;

        if (avail > XENSTORE_RING_SIZE) {
            error_report("XenStore ring handling error");
            s->fatal_error = true;
            break;
        } else if (avail == 0) {
            break;
        }

        if (copylen > len) {
            copylen = len;
        }
        if (copylen > XENSTORE_RING_SIZE - offset) {
            copylen = XENSTORE_RING_SIZE - offset;
        }

        memcpy(ptr, &s->xs->req[offset], copylen);
        copied += copylen;

        ptr += copylen;
        len -= copylen;

        cons += copylen;
    }

    /*
     * Not sure this ever mattered except on Alpha, but this barrier
     * is to ensure that the update to req_cons is globally visible
     * only after we have consumed all the data from the ring, and we
     * don't end up seeing data written to the ring *after* the other
     * end sees the update and writes more to the ring. Xen's own
     * xenstored has the same barrier here (although with no comment
     * at all, obviously, because it's Xen code).
     */
    smp_mb();

    qatomic_set(&s->xs->req_cons, cons);

    return copied;
}

static unsigned int copy_to_ring(XenXenstoreState *s, uint8_t *ptr,
                                 unsigned int len)
{
    if (!len) {
        return 0;
    }

    XENSTORE_RING_IDX cons = qatomic_read(&s->xs->rsp_cons);
    XENSTORE_RING_IDX prod = qatomic_read(&s->xs->rsp_prod);
    unsigned int copied = 0;

    /*
     * This matches the barrier in copy_to_ring() (or the guest's
     * equivalent) between writing the data to the ring and updating
     * rsp_prod. It protects against the pathological case (which
     * again I think never happened except on Alpha) where our
     * subsequent writes to the ring could *cross* the read of
     * rsp_cons and the guest could see the new data when it was
     * intending to read the old.
     */
    smp_mb();

    while (len) {
        unsigned int avail = cons + XENSTORE_RING_SIZE - prod;
        unsigned int offset = MASK_XENSTORE_IDX(prod);
        unsigned int copylen = len;

        if (avail > XENSTORE_RING_SIZE) {
            error_report("XenStore ring handling error");
            s->fatal_error = true;
            break;
        } else if (avail == 0) {
            break;
        }

        if (copylen > avail) {
            copylen = avail;
        }
        if (copylen > XENSTORE_RING_SIZE - offset) {
            copylen = XENSTORE_RING_SIZE - offset;
        }


        memcpy(&s->xs->rsp[offset], ptr, copylen);
        copied += copylen;

        ptr += copylen;
        len -= copylen;

        prod += copylen;
    }

    /* Ensure the ring contents are seen before rsp_prod update. */
    smp_wmb();

    qatomic_set(&s->xs->rsp_prod, prod);

    return copied;
}

static unsigned int get_req(XenXenstoreState *s)
{
    unsigned int copied = 0;

    if (s->fatal_error) {
        return 0;
    }

    assert(!req_pending(s));

    if (s->req_offset < XENSTORE_HEADER_SIZE) {
        void *ptr = s->req_data + s->req_offset;
        unsigned int len = XENSTORE_HEADER_SIZE;
        unsigned int copylen = copy_from_ring(s, ptr, len);

        copied += copylen;
        s->req_offset += copylen;
    }

    if (s->req_offset >= XENSTORE_HEADER_SIZE) {
        struct xsd_sockmsg *req = (struct xsd_sockmsg *)s->req_data;

        if (req->len > (uint32_t)XENSTORE_PAYLOAD_MAX) {
            error_report("Illegal XenStore request");
            s->fatal_error = true;
            return 0;
        }

        void *ptr = s->req_data + s->req_offset;
        unsigned int len = XENSTORE_HEADER_SIZE + req->len - s->req_offset;
        unsigned int copylen = copy_from_ring(s, ptr, len);

        copied += copylen;
        s->req_offset += copylen;
    }

    return copied;
}

static unsigned int put_rsp(XenXenstoreState *s)
{
    if (s->fatal_error) {
        return 0;
    }

    assert(s->rsp_pending);

    struct xsd_sockmsg *rsp = (struct xsd_sockmsg *)s->rsp_data;
    assert(s->rsp_offset < XENSTORE_HEADER_SIZE + rsp->len);

    void *ptr = s->rsp_data + s->rsp_offset;
    unsigned int len = XENSTORE_HEADER_SIZE + rsp->len - s->rsp_offset;
    unsigned int copylen = copy_to_ring(s, ptr, len);

    s->rsp_offset += copylen;

    /* Have we produced a complete response? */
    if (s->rsp_offset == XENSTORE_HEADER_SIZE + rsp->len) {
        reset_rsp(s);
    }

    return copylen;
}

static void deliver_watch(XenXenstoreState *s, const char *path,
                          const char *token)
{
    struct xsd_sockmsg *rsp = (struct xsd_sockmsg *)s->rsp_data;
    uint8_t *rsp_data = (uint8_t *)&rsp[1];
    unsigned int len;

    assert(!s->rsp_pending);

    trace_xenstore_watch_event(path, token);

    rsp->type = XS_WATCH_EVENT;
    rsp->req_id = 0;
    rsp->tx_id = 0;
    rsp->len = 0;

    len = strlen(path);

    /* XENSTORE_ABS/REL_PATH_MAX should ensure there can be no overflow */
    assert(rsp->len + len < XENSTORE_PAYLOAD_MAX);

    memcpy(&rsp_data[rsp->len], path, len);
    rsp->len += len;
    rsp_data[rsp->len] = '\0';
    rsp->len++;

    len = strlen(token);
    /*
     * It is possible for the guest to have chosen a token that will
     * not fit (along with the patch) into a watch event. We have no
     * choice but to drop the event if this is the case.
     */
    if (rsp->len + len >= XENSTORE_PAYLOAD_MAX) {
        return;
    }

    memcpy(&rsp_data[rsp->len], token, len);
    rsp->len += len;
    rsp_data[rsp->len] = '\0';
    rsp->len++;

    s->rsp_pending = true;
}

struct watch_event {
    char *path;
    char *token;
};

static void free_watch_event(struct watch_event *ev)
{
    if (ev) {
        g_free(ev->path);
        g_free(ev->token);
        g_free(ev);
    }
}

static void queue_watch(XenXenstoreState *s, const char *path,
                        const char *token)
{
    struct watch_event *ev = g_new0(struct watch_event, 1);

    ev->path = g_strdup(path);
    ev->token = g_strdup(token);

    s->watch_events = g_list_append(s->watch_events, ev);
}

static void fire_watch_cb(void *opaque, const char *path, const char *token)
{
    XenXenstoreState *s = opaque;

    assert(bql_locked());

    /*
     * If there's a response pending, we obviously can't scribble over
     * it. But if there's a request pending, it has dibs on the buffer
     * too.
     *
     * In the common case of a watch firing due to backend activity
     * when the ring was otherwise idle, we should be able to copy the
     * strings directly into the rsp_data and thence the actual ring,
     * without needing to perform any allocations and queue them.
     */
    if (s->rsp_pending || req_pending(s)) {
        queue_watch(s, path, token);
    } else {
        deliver_watch(s, path, token);
        /*
         * Attempt to queue the message into the actual ring, and send
         * the event channel notification if any bytes are copied.
         */
        if (s->rsp_pending && put_rsp(s) > 0) {
            xen_be_evtchn_notify(s->eh, s->be_port);
        }
    }
}

static void process_watch_events(XenXenstoreState *s)
{
    struct watch_event *ev = s->watch_events->data;

    deliver_watch(s, ev->path, ev->token);

    s->watch_events = g_list_remove(s->watch_events, ev);
    free_watch_event(ev);
}

static void xen_xenstore_event(void *opaque)
{
    XenXenstoreState *s = opaque;
    evtchn_port_t port = xen_be_evtchn_pending(s->eh);
    unsigned int copied_to, copied_from;
    bool processed, notify = false;

    if (port != s->be_port) {
        return;
    }

    /* We know this is a no-op. */
    xen_be_evtchn_unmask(s->eh, port);

    do {
        copied_to = copied_from = 0;
        processed = false;

        if (!s->rsp_pending && s->watch_events) {
            process_watch_events(s);
        }

        if (s->rsp_pending) {
            copied_to = put_rsp(s);
        }

        if (!req_pending(s)) {
            copied_from = get_req(s);
        }

        if (req_pending(s) && !s->rsp_pending && !s->watch_events) {
            process_req(s);
            processed = true;
        }

        notify |= copied_to || copied_from;
    } while (copied_to || copied_from || processed);

    if (notify) {
        xen_be_evtchn_notify(s->eh, s->be_port);
    }
}

static void alloc_guest_port(XenXenstoreState *s)
{
    struct evtchn_alloc_unbound alloc = {
        .dom = DOMID_SELF,
        .remote_dom = DOMID_QEMU,
    };

    if (!xen_evtchn_alloc_unbound_op(&alloc)) {
        s->guest_port = alloc.port;
    }
}

int xen_xenstore_reset(void)
{
    XenXenstoreState *s = xen_xenstore_singleton;
    int console_port;
    GList *perms;
    int err;

    if (!s) {
        return -ENOTSUP;
    }

    s->req_offset = s->rsp_offset = 0;
    s->rsp_pending = false;

    if (!memory_region_is_mapped(&s->xenstore_page)) {
        uint64_t gpa = XEN_SPECIAL_PFN(XENSTORE) << TARGET_PAGE_BITS;
        xen_overlay_do_map_page(&s->xenstore_page, gpa);
    }

    alloc_guest_port(s);

    /*
     * As qemu/dom0, bind to the guest's port. For incoming migration, this
     * will be unbound as the guest's evtchn table is overwritten. We then
     * rebind to the correct guest port in xen_xenstore_post_load().
     */
    err = xen_be_evtchn_bind_interdomain(s->eh, xen_domid, s->guest_port);
    if (err < 0) {
        return err;
    }
    s->be_port = err;

    /* Create frontend store nodes */
    perms = g_list_append(NULL, xs_perm_as_string(XS_PERM_NONE, DOMID_QEMU));
    perms = g_list_append(perms, xs_perm_as_string(XS_PERM_READ, xen_domid));

    relpath_printf(s, perms, "store/port", "%u", s->guest_port);
    relpath_printf(s, perms, "store/ring-ref", "%lu",
                   XEN_SPECIAL_PFN(XENSTORE));

    console_port = xen_primary_console_get_port();
    if (console_port) {
        relpath_printf(s, perms, "console/ring-ref", "%lu",
                       XEN_SPECIAL_PFN(CONSOLE));
        relpath_printf(s, perms, "console/port", "%u", console_port);
        relpath_printf(s, perms, "console/state", "%u", XenbusStateInitialised);
    }

    g_list_free_full(perms, g_free);

    /*
     * We don't actually access the guest's page through the grant, because
     * this isn't real Xen, and we can just use the page we gave it in the
     * first place. Map the grant anyway, mostly for cosmetic purposes so
     * it *looks* like it's in use in the guest-visible grant table.
     */
    s->gt = qemu_xen_gnttab_open();
    uint32_t xs_gntref = GNTTAB_RESERVED_XENSTORE;
    s->granted_xs = qemu_xen_gnttab_map_refs(s->gt, 1, xen_domid, &xs_gntref,
                                             PROT_READ | PROT_WRITE);

    return 0;
}

struct qemu_xs_handle {
    XenstoreImplState *impl;
    GList *watches;
    QEMUBH *watch_bh;
};

struct qemu_xs_watch {
    struct qemu_xs_handle *h;
    char *path;
    xs_watch_fn fn;
    void *opaque;
    GList *events;
};

static char *xs_be_get_domain_path(struct qemu_xs_handle *h, unsigned int domid)
{
    return g_strdup_printf("/local/domain/%u", domid);
}

static char **xs_be_directory(struct qemu_xs_handle *h, xs_transaction_t t,
                              const char *path, unsigned int *num)
{
    GList *items = NULL, *l;
    unsigned int i = 0;
    char **items_ret;
    int err;

    err = xs_impl_directory(h->impl, DOMID_QEMU, t, path, NULL, &items);
    if (err) {
        errno = err;
        return NULL;
    }

    items_ret = g_new0(char *, g_list_length(items) + 1);
    *num = 0;
    for (l = items; l; l = l->next) {
        items_ret[i++] = l->data;
        (*num)++;
    }
    g_list_free(items);
    return items_ret;
}

static void *xs_be_read(struct qemu_xs_handle *h, xs_transaction_t t,
                        const char *path, unsigned int *len)
{
    GByteArray *data = g_byte_array_new();
    bool free_segment = false;
    int err;

    err = xs_impl_read(h->impl, DOMID_QEMU, t, path, data);
    if (err) {
        free_segment = true;
        errno = err;
    } else {
        if (len) {
            *len = data->len;
        }
        /* The xen-bus-helper code expects to get NUL terminated string! */
        g_byte_array_append(data, (void *)"", 1);
    }

    return g_byte_array_free(data, free_segment);
}

static bool xs_be_write(struct qemu_xs_handle *h, xs_transaction_t t,
                        const char *path, const void *data, unsigned int len)
{
    GByteArray *gdata = g_byte_array_new();
    int err;

    g_byte_array_append(gdata, data, len);
    err = xs_impl_write(h->impl, DOMID_QEMU, t, path, gdata);
    g_byte_array_unref(gdata);
    if (err) {
        errno = err;
        return false;
    }
    return true;
}

static bool xs_be_create(struct qemu_xs_handle *h, xs_transaction_t t,
                         unsigned int owner, unsigned int domid,
                         unsigned int perms, const char *path)
{
    g_autoptr(GByteArray) data = g_byte_array_new();
    GList *perms_list = NULL;
    int err;

    /* mkdir does this */
    err = xs_impl_read(h->impl, DOMID_QEMU, t, path, data);
    if (err == ENOENT) {
        err = xs_impl_write(h->impl, DOMID_QEMU, t, path, data);
    }
    if (err) {
        errno = err;
        return false;
    }

    perms_list = g_list_append(perms_list,
                               xs_perm_as_string(XS_PERM_NONE, owner));
    perms_list = g_list_append(perms_list,
                               xs_perm_as_string(perms, domid));

    err = xs_impl_set_perms(h->impl, DOMID_QEMU, t, path, perms_list);
    g_list_free_full(perms_list, g_free);
    if (err) {
        errno = err;
        return false;
    }
    return true;
}

static bool xs_be_destroy(struct qemu_xs_handle *h, xs_transaction_t t,
                          const char *path)
{
    int err = xs_impl_rm(h->impl, DOMID_QEMU, t, path);
    if (err) {
        errno = err;
        return false;
    }
    return true;
}

static void be_watch_bh(void *_h)
{
    struct qemu_xs_handle *h = _h;
    GList *l;

    for (l = h->watches; l; l = l->next) {
        struct qemu_xs_watch *w = l->data;

        while (w->events) {
            struct watch_event *ev = w->events->data;

            w->fn(w->opaque, ev->path);

            w->events = g_list_remove(w->events, ev);
            free_watch_event(ev);
        }
    }
}

static void xs_be_watch_cb(void *opaque, const char *path, const char *token)
{
    struct watch_event *ev = g_new0(struct watch_event, 1);
    struct qemu_xs_watch *w = opaque;

    /* We don't care about the token */
    ev->path = g_strdup(path);
    w->events = g_list_append(w->events, ev);

    qemu_bh_schedule(w->h->watch_bh);
}

static struct qemu_xs_watch *xs_be_watch(struct qemu_xs_handle *h,
                                         const char *path, xs_watch_fn fn,
                                         void *opaque)
{
    struct qemu_xs_watch *w = g_new0(struct qemu_xs_watch, 1);
    int err;

    w->h = h;
    w->fn = fn;
    w->opaque = opaque;

    err = xs_impl_watch(h->impl, DOMID_QEMU, path, NULL, xs_be_watch_cb, w);
    if (err) {
        errno = err;
        g_free(w);
        return NULL;
    }

    w->path = g_strdup(path);
    h->watches = g_list_append(h->watches, w);
    return w;
}

static void xs_be_unwatch(struct qemu_xs_handle *h, struct qemu_xs_watch *w)
{
    xs_impl_unwatch(h->impl, DOMID_QEMU, w->path, NULL, xs_be_watch_cb, w);

    h->watches = g_list_remove(h->watches, w);
    g_list_free_full(w->events, (GDestroyNotify)free_watch_event);
    g_free(w->path);
    g_free(w);
}

static xs_transaction_t xs_be_transaction_start(struct qemu_xs_handle *h)
{
    unsigned int new_tx = XBT_NULL;
    int err = xs_impl_transaction_start(h->impl, DOMID_QEMU, &new_tx);
    if (err) {
        errno = err;
        return XBT_NULL;
    }
    return new_tx;
}

static bool xs_be_transaction_end(struct qemu_xs_handle *h, xs_transaction_t t,
                                  bool abort)
{
    int err = xs_impl_transaction_end(h->impl, DOMID_QEMU, t, !abort);
    if (err) {
        errno = err;
        return false;
    }
    return true;
}

static struct qemu_xs_handle *xs_be_open(void)
{
    XenXenstoreState *s = xen_xenstore_singleton;
    struct qemu_xs_handle *h;

    if (!s || !s->impl) {
        errno = -ENOSYS;
        return NULL;
    }

    h = g_new0(struct qemu_xs_handle, 1);
    h->impl = s->impl;

    h->watch_bh = aio_bh_new(qemu_get_aio_context(), be_watch_bh, h);

    return h;
}

static void xs_be_close(struct qemu_xs_handle *h)
{
    while (h->watches) {
        struct qemu_xs_watch *w = h->watches->data;
        xs_be_unwatch(h, w);
    }

    qemu_bh_delete(h->watch_bh);
    g_free(h);
}

static struct xenstore_backend_ops emu_xenstore_backend_ops = {
    .open = xs_be_open,
    .close = xs_be_close,
    .get_domain_path = xs_be_get_domain_path,
    .directory = xs_be_directory,
    .read = xs_be_read,
    .write = xs_be_write,
    .create = xs_be_create,
    .destroy = xs_be_destroy,
    .watch = xs_be_watch,
    .unwatch = xs_be_unwatch,
    .transaction_start = xs_be_transaction_start,
    .transaction_end = xs_be_transaction_end,
};

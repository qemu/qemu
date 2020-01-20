/*
 * QEMU dbus-vmstate
 *
 * Copyright (C) 2019 Red Hat Inc
 *
 * Authors:
 *  Marc-André Lureau <marcandre.lureau@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/dbus.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qom/object_interfaces.h"
#include "qapi/qmp/qerror.h"
#include "migration/vmstate.h"
#include "trace.h"

typedef struct DBusVMState DBusVMState;
typedef struct DBusVMStateClass DBusVMStateClass;

#define TYPE_DBUS_VMSTATE "dbus-vmstate"
#define DBUS_VMSTATE(obj)                                \
    OBJECT_CHECK(DBusVMState, (obj), TYPE_DBUS_VMSTATE)
#define DBUS_VMSTATE_GET_CLASS(obj)                              \
    OBJECT_GET_CLASS(DBusVMStateClass, (obj), TYPE_DBUS_VMSTATE)
#define DBUS_VMSTATE_CLASS(klass)                                    \
    OBJECT_CLASS_CHECK(DBusVMStateClass, (klass), TYPE_DBUS_VMSTATE)

struct DBusVMStateClass {
    ObjectClass parent_class;
};

struct DBusVMState {
    Object parent;

    GDBusConnection *bus;
    char *dbus_addr;
    char *id_list;

    uint32_t data_size;
    uint8_t *data;
};

static const GDBusPropertyInfo vmstate_property_info[] = {
    { -1, (char *) "Id", (char *) "s",
      G_DBUS_PROPERTY_INFO_FLAGS_READABLE, NULL },
};

static const GDBusPropertyInfo * const vmstate_property_info_pointers[] = {
    &vmstate_property_info[0],
    NULL
};

static const GDBusInterfaceInfo vmstate1_interface_info = {
    -1,
    (char *) "org.qemu.VMState1",
    (GDBusMethodInfo **) NULL,
    (GDBusSignalInfo **) NULL,
    (GDBusPropertyInfo **) &vmstate_property_info_pointers,
    NULL,
};

#define DBUS_VMSTATE_SIZE_LIMIT (1 * MiB)

static GHashTable *
get_id_list_set(DBusVMState *self)
{
    g_auto(GStrv) ids = NULL;
    g_autoptr(GHashTable) set = NULL;
    int i;

    if (!self->id_list) {
        return NULL;
    }

    ids = g_strsplit(self->id_list, ",", -1);
    set = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    for (i = 0; ids[i]; i++) {
        g_hash_table_add(set, ids[i]);
        ids[i] = NULL;
    }

    return g_steal_pointer(&set);
}

static GHashTable *
dbus_get_proxies(DBusVMState *self, GError **err)
{
    g_autoptr(GHashTable) proxies = NULL;
    g_autoptr(GHashTable) ids = NULL;
    g_auto(GStrv) names = NULL;
    Error *error = NULL;
    size_t i;

    ids = get_id_list_set(self);
    proxies = g_hash_table_new_full(g_str_hash, g_str_equal,
                                    g_free, g_object_unref);

    names = qemu_dbus_get_queued_owners(self->bus, "org.qemu.VMState1", &error);
    if (!names) {
        g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED, "%s",
                    error_get_pretty(error));
        error_free(error);
        return NULL;
    }

    for (i = 0; names[i]; i++) {
        g_autoptr(GDBusProxy) proxy = NULL;
        g_autoptr(GVariant) result = NULL;
        g_autofree char *id = NULL;
        size_t size;

        proxy = g_dbus_proxy_new_sync(self->bus, G_DBUS_PROXY_FLAGS_NONE,
                    (GDBusInterfaceInfo *) &vmstate1_interface_info,
                    names[i],
                    "/org/qemu/VMState1",
                    "org.qemu.VMState1",
                    NULL, err);
        if (!proxy) {
            return NULL;
        }

        result = g_dbus_proxy_get_cached_property(proxy, "Id");
        if (!result) {
            g_set_error_literal(err, G_IO_ERROR, G_IO_ERROR_FAILED,
                                "VMState Id property is missing.");
            return NULL;
        }

        id = g_variant_dup_string(result, &size);
        if (ids && !g_hash_table_remove(ids, id)) {
            g_clear_pointer(&id, g_free);
            g_clear_object(&proxy);
            continue;
        }
        if (size == 0 || size >= 256) {
            g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "VMState Id '%s' is invalid.", id);
            return NULL;
        }

        if (!g_hash_table_insert(proxies, id, proxy)) {
            g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "Duplicated VMState Id '%s'", id);
            return NULL;
        }
        id = NULL;
        proxy = NULL;

        g_clear_pointer(&result, g_variant_unref);
    }

    if (ids) {
        g_autofree char **left = NULL;

        left = (char **)g_hash_table_get_keys_as_array(ids, NULL);
        if (*left) {
            g_autofree char *leftids = g_strjoinv(",", left);
            g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "Required VMState Id are missing: %s", leftids);
            return NULL;
        }
    }

    return g_steal_pointer(&proxies);
}

static int
dbus_load_state_proxy(GDBusProxy *proxy, const uint8_t *data, size_t size)
{
    g_autoptr(GError) err = NULL;
    g_autoptr(GVariant) result = NULL;
    g_autoptr(GVariant) value = NULL;

    value = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,
                                      data, size, sizeof(char));
    result = g_dbus_proxy_call_sync(proxy, "Load",
                                    g_variant_new("(@ay)",
                                                  g_steal_pointer(&value)),
                                    G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                    -1, NULL, &err);
    if (!result) {
        error_report("%s: Failed to Load: %s", __func__, err->message);
        return -1;
    }

    return 0;
}

static int dbus_vmstate_post_load(void *opaque, int version_id)
{
    DBusVMState *self = DBUS_VMSTATE(opaque);
    g_autoptr(GInputStream) m = NULL;
    g_autoptr(GDataInputStream) s = NULL;
    g_autoptr(GError) err = NULL;
    g_autoptr(GHashTable) proxies = NULL;
    uint32_t nelem;

    trace_dbus_vmstate_post_load(version_id);

    proxies = dbus_get_proxies(self, &err);
    if (!proxies) {
        error_report("%s: Failed to get proxies: %s", __func__, err->message);
        return -1;
    }

    m = g_memory_input_stream_new_from_data(self->data, self->data_size, NULL);
    s = g_data_input_stream_new(m);
    g_data_input_stream_set_byte_order(s, G_DATA_STREAM_BYTE_ORDER_BIG_ENDIAN);

    nelem = g_data_input_stream_read_uint32(s, NULL, &err);
    if (err) {
        goto error;
    }

    while (nelem > 0) {
        GDBusProxy *proxy = NULL;
        uint32_t len;
        gsize bytes_read, avail;
        char id[256];

        len = g_data_input_stream_read_uint32(s, NULL, &err);
        if (err) {
            goto error;
        }
        if (len >= 256) {
            error_report("%s: Invalid DBus vmstate proxy name %u",
                         __func__, len);
            return -1;
        }
        if (!g_input_stream_read_all(G_INPUT_STREAM(s), id, len,
                                     &bytes_read, NULL, &err)) {
            goto error;
        }
        g_return_val_if_fail(bytes_read == len, -1);
        id[len] = 0;

        trace_dbus_vmstate_loading(id);

        proxy = g_hash_table_lookup(proxies, id);
        if (!proxy) {
            error_report("%s: Failed to find proxy Id '%s'", __func__, id);
            return -1;
        }

        len = g_data_input_stream_read_uint32(s, NULL, &err);
        avail = g_buffered_input_stream_get_available(
            G_BUFFERED_INPUT_STREAM(s));

        if (len > DBUS_VMSTATE_SIZE_LIMIT || len > avail) {
            error_report("%s: Invalid vmstate size: %u", __func__, len);
            return -1;
        }

        if (dbus_load_state_proxy(proxy,
                g_buffered_input_stream_peek_buffer(G_BUFFERED_INPUT_STREAM(s),
                                                    NULL),
                len) < 0) {
            error_report("%s: Failed to restore Id '%s'", __func__, id);
            return -1;
        }

        if (!g_seekable_seek(G_SEEKABLE(s), len, G_SEEK_CUR, NULL, &err)) {
            goto error;
        }

        nelem -= 1;
    }

    return 0;

error:
    error_report("%s: Failed to read from stream: %s", __func__, err->message);
    return -1;
}

static void
dbus_save_state_proxy(gpointer key,
                      gpointer value,
                      gpointer user_data)
{
    GDataOutputStream *s = user_data;
    const char *id = key;
    GDBusProxy *proxy = value;
    g_autoptr(GVariant) result = NULL;
    g_autoptr(GVariant) child = NULL;
    g_autoptr(GError) err = NULL;
    const uint8_t *data;
    gsize size;

    trace_dbus_vmstate_saving(id);

    result = g_dbus_proxy_call_sync(proxy, "Save",
                                    NULL, G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                    -1, NULL, &err);
    if (!result) {
        error_report("%s: Failed to Save: %s", __func__, err->message);
        return;
    }

    child = g_variant_get_child_value(result, 0);
    data = g_variant_get_fixed_array(child, &size, sizeof(char));
    if (!data) {
        error_report("%s: Failed to Save: not a byte array", __func__);
        return;
    }
    if (size > DBUS_VMSTATE_SIZE_LIMIT) {
        error_report("%s: Too large vmstate data to save: %zu",
                     __func__, (size_t)size);
        return;
    }

    if (!g_data_output_stream_put_uint32(s, strlen(id), NULL, &err) ||
        !g_data_output_stream_put_string(s, id, NULL, &err) ||
        !g_data_output_stream_put_uint32(s, size, NULL, &err) ||
        !g_output_stream_write_all(G_OUTPUT_STREAM(s),
                                   data, size, NULL, NULL, &err)) {
        error_report("%s: Failed to write to stream: %s",
                     __func__, err->message);
    }
}

static int dbus_vmstate_pre_save(void *opaque)
{
    DBusVMState *self = DBUS_VMSTATE(opaque);
    g_autoptr(GOutputStream) m = NULL;
    g_autoptr(GDataOutputStream) s = NULL;
    g_autoptr(GHashTable) proxies = NULL;
    g_autoptr(GError) err = NULL;

    trace_dbus_vmstate_pre_save();

    proxies = dbus_get_proxies(self, &err);
    if (!proxies) {
        error_report("%s: Failed to get proxies: %s", __func__, err->message);
        return -1;
    }

    m = g_memory_output_stream_new_resizable();
    s = g_data_output_stream_new(m);
    g_data_output_stream_set_byte_order(s, G_DATA_STREAM_BYTE_ORDER_BIG_ENDIAN);

    if (!g_data_output_stream_put_uint32(s, g_hash_table_size(proxies),
                                         NULL, &err)) {
        error_report("%s: Failed to write to stream: %s",
                     __func__, err->message);
        return -1;
    }

    g_hash_table_foreach(proxies, dbus_save_state_proxy, s);

    if (g_memory_output_stream_get_size(G_MEMORY_OUTPUT_STREAM(m))
        > UINT32_MAX) {
        error_report("%s: DBus vmstate buffer is too large", __func__);
        return -1;
    }

    if (!g_output_stream_close(G_OUTPUT_STREAM(m), NULL, &err)) {
        error_report("%s: Failed to close stream: %s", __func__, err->message);
        return -1;
    }

    g_free(self->data);
    self->data_size =
        g_memory_output_stream_get_size(G_MEMORY_OUTPUT_STREAM(m));
    self->data =
        g_memory_output_stream_steal_data(G_MEMORY_OUTPUT_STREAM(m));

    return 0;
}

static const VMStateDescription dbus_vmstate = {
    .name = TYPE_DBUS_VMSTATE,
    .version_id = 0,
    .pre_save = dbus_vmstate_pre_save,
    .post_load = dbus_vmstate_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(data_size, DBusVMState),
        VMSTATE_VBUFFER_ALLOC_UINT32(data, DBusVMState, 0, 0, data_size),
        VMSTATE_END_OF_LIST()
    }
};

static void
dbus_vmstate_complete(UserCreatable *uc, Error **errp)
{
    DBusVMState *self = DBUS_VMSTATE(uc);
    g_autoptr(GError) err = NULL;

    if (!object_resolve_path_type("", TYPE_DBUS_VMSTATE, NULL)) {
        error_setg(errp, "There is already an instance of %s",
                   TYPE_DBUS_VMSTATE);
        return;
    }

    if (!self->dbus_addr) {
        error_setg(errp, QERR_MISSING_PARAMETER, "addr");
        return;
    }

    self->bus = g_dbus_connection_new_for_address_sync(self->dbus_addr,
                    G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                    G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
                    NULL, NULL, &err);
    if (err) {
        error_setg(errp, "failed to connect to DBus: '%s'", err->message);
        return;
    }

    if (vmstate_register(VMSTATE_IF(self), VMSTATE_INSTANCE_ID_ANY,
                         &dbus_vmstate, self) < 0) {
        error_setg(errp, "Failed to register vmstate");
    }
}

static void
dbus_vmstate_finalize(Object *o)
{
    DBusVMState *self = DBUS_VMSTATE(o);

    vmstate_unregister(VMSTATE_IF(self), &dbus_vmstate, self);

    g_clear_object(&self->bus);
    g_free(self->dbus_addr);
    g_free(self->id_list);
    g_free(self->data);
}

static char *
get_dbus_addr(Object *o, Error **errp)
{
    DBusVMState *self = DBUS_VMSTATE(o);

    return g_strdup(self->dbus_addr);
}

static void
set_dbus_addr(Object *o, const char *str, Error **errp)
{
    DBusVMState *self = DBUS_VMSTATE(o);

    g_free(self->dbus_addr);
    self->dbus_addr = g_strdup(str);
}

static char *
get_id_list(Object *o, Error **errp)
{
    DBusVMState *self = DBUS_VMSTATE(o);

    return g_strdup(self->id_list);
}

static void
set_id_list(Object *o, const char *str, Error **errp)
{
    DBusVMState *self = DBUS_VMSTATE(o);

    g_free(self->id_list);
    self->id_list = g_strdup(str);
}

static char *
dbus_vmstate_get_id(VMStateIf *vmif)
{
    return g_strdup(TYPE_DBUS_VMSTATE);
}

static void
dbus_vmstate_class_init(ObjectClass *oc, void *data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(oc);
    VMStateIfClass *vc = VMSTATE_IF_CLASS(oc);

    ucc->complete = dbus_vmstate_complete;
    vc->get_id = dbus_vmstate_get_id;

    object_class_property_add_str(oc, "addr",
                                  get_dbus_addr, set_dbus_addr,
                                  &error_abort);
    object_class_property_add_str(oc, "id-list",
                                  get_id_list, set_id_list,
                                  &error_abort);
}

static const TypeInfo dbus_vmstate_info = {
    .name = TYPE_DBUS_VMSTATE,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(DBusVMState),
    .instance_finalize = dbus_vmstate_finalize,
    .class_size = sizeof(DBusVMStateClass),
    .class_init = dbus_vmstate_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { TYPE_VMSTATE_IF },
        { }
    }
};

static void
register_types(void)
{
    type_register_static(&dbus_vmstate_info);
}

type_init(register_types);

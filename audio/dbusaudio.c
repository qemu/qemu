/*
 * QEMU DBus audio
 *
 * Copyright (c) 2021 Red Hat, Inc.
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

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/host-utils.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/dbus.h"

#include <gio/gunixfdlist.h>
#include "ui/dbus-display1.h"

#define AUDIO_CAP "dbus"
#include "audio.h"
#include "audio_int.h"
#include "trace.h"

#define DBUS_DISPLAY1_AUDIO_PATH DBUS_DISPLAY1_ROOT "/Audio"

#define DBUS_AUDIO_NSAMPLES 1024 /* could be configured? */

typedef struct DBusAudio {
    GDBusObjectManagerServer *server;
    GDBusObjectSkeleton *audio;
    QemuDBusDisplay1Audio *iface;
    GHashTable *out_listeners;
    GHashTable *in_listeners;
} DBusAudio;

typedef struct DBusVoiceOut {
    HWVoiceOut hw;
    bool enabled;
    RateCtl rate;

    void *buf;
    size_t buf_pos;
    size_t buf_size;

    bool has_volume;
    Volume volume;
} DBusVoiceOut;

typedef struct DBusVoiceIn {
    HWVoiceIn hw;
    bool enabled;
    RateCtl rate;

    bool has_volume;
    Volume volume;
} DBusVoiceIn;

static void *dbus_get_buffer_out(HWVoiceOut *hw, size_t *size)
{
    DBusVoiceOut *vo = container_of(hw, DBusVoiceOut, hw);

    if (!vo->buf) {
        vo->buf_size = hw->samples * hw->info.bytes_per_frame;
        vo->buf = g_malloc(vo->buf_size);
        vo->buf_pos = 0;
    }

    *size = MIN(vo->buf_size - vo->buf_pos, *size);
    *size = audio_rate_get_bytes(&vo->rate, &hw->info, *size);

    return vo->buf + vo->buf_pos;

}

static size_t dbus_put_buffer_out(HWVoiceOut *hw, void *buf, size_t size)
{
    DBusAudio *da = (DBusAudio *)hw->s->drv_opaque;
    DBusVoiceOut *vo = container_of(hw, DBusVoiceOut, hw);
    GHashTableIter iter;
    QemuDBusDisplay1AudioOutListener *listener = NULL;
    g_autoptr(GBytes) bytes = NULL;
    g_autoptr(GVariant) v_data = NULL;

    assert(buf == vo->buf + vo->buf_pos && vo->buf_pos + size <= vo->buf_size);
    vo->buf_pos += size;

    trace_dbus_audio_put_buffer_out(size);

    if (vo->buf_pos < vo->buf_size) {
        return size;
    }

    bytes = g_bytes_new_take(g_steal_pointer(&vo->buf), vo->buf_size);
    v_data = g_variant_new_from_bytes(G_VARIANT_TYPE("ay"), bytes, TRUE);
    g_variant_ref_sink(v_data);

    g_hash_table_iter_init(&iter, da->out_listeners);
    while (g_hash_table_iter_next(&iter, NULL, (void **)&listener)) {
        qemu_dbus_display1_audio_out_listener_call_write(
            listener,
            (uintptr_t)hw,
            v_data,
            G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
    }

    return size;
}

#if HOST_BIG_ENDIAN
#define AUDIO_HOST_BE TRUE
#else
#define AUDIO_HOST_BE FALSE
#endif

static void
dbus_init_out_listener(QemuDBusDisplay1AudioOutListener *listener,
                       HWVoiceOut *hw)
{
    qemu_dbus_display1_audio_out_listener_call_init(
        listener,
        (uintptr_t)hw,
        hw->info.bits,
        hw->info.is_signed,
        hw->info.is_float,
        hw->info.freq,
        hw->info.nchannels,
        hw->info.bytes_per_frame,
        hw->info.bytes_per_second,
        hw->info.swap_endianness ? !AUDIO_HOST_BE : AUDIO_HOST_BE,
        G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
}

static int
dbus_init_out(HWVoiceOut *hw, struct audsettings *as, void *drv_opaque)
{
    DBusAudio *da = (DBusAudio *)hw->s->drv_opaque;
    DBusVoiceOut *vo = container_of(hw, DBusVoiceOut, hw);
    GHashTableIter iter;
    QemuDBusDisplay1AudioOutListener *listener = NULL;

    audio_pcm_init_info(&hw->info, as);
    hw->samples = DBUS_AUDIO_NSAMPLES;
    audio_rate_start(&vo->rate);

    g_hash_table_iter_init(&iter, da->out_listeners);
    while (g_hash_table_iter_next(&iter, NULL, (void **)&listener)) {
        dbus_init_out_listener(listener, hw);
    }
    return 0;
}

static void
dbus_fini_out(HWVoiceOut *hw)
{
    DBusAudio *da = (DBusAudio *)hw->s->drv_opaque;
    DBusVoiceOut *vo = container_of(hw, DBusVoiceOut, hw);
    GHashTableIter iter;
    QemuDBusDisplay1AudioOutListener *listener = NULL;

    g_hash_table_iter_init(&iter, da->out_listeners);
    while (g_hash_table_iter_next(&iter, NULL, (void **)&listener)) {
        qemu_dbus_display1_audio_out_listener_call_fini(
            listener,
            (uintptr_t)hw,
            G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
    }

    g_clear_pointer(&vo->buf, g_free);
}

static void
dbus_enable_out(HWVoiceOut *hw, bool enable)
{
    DBusAudio *da = (DBusAudio *)hw->s->drv_opaque;
    DBusVoiceOut *vo = container_of(hw, DBusVoiceOut, hw);
    GHashTableIter iter;
    QemuDBusDisplay1AudioOutListener *listener = NULL;

    vo->enabled = enable;
    if (enable) {
        audio_rate_start(&vo->rate);
    }

    g_hash_table_iter_init(&iter, da->out_listeners);
    while (g_hash_table_iter_next(&iter, NULL, (void **)&listener)) {
        qemu_dbus_display1_audio_out_listener_call_set_enabled(
            listener, (uintptr_t)hw, enable,
            G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
    }
}

static void
dbus_volume_out_listener(HWVoiceOut *hw,
                         QemuDBusDisplay1AudioOutListener *listener)
{
    DBusVoiceOut *vo = container_of(hw, DBusVoiceOut, hw);
    Volume *vol = &vo->volume;
    g_autoptr(GBytes) bytes = NULL;
    GVariant *v_vol = NULL;

    if (!vo->has_volume) {
        return;
    }

    assert(vol->channels < sizeof(vol->vol));
    bytes = g_bytes_new(vol->vol, vol->channels);
    v_vol = g_variant_new_from_bytes(G_VARIANT_TYPE("ay"), bytes, TRUE);
    qemu_dbus_display1_audio_out_listener_call_set_volume(
        listener, (uintptr_t)hw, vol->mute, v_vol,
        G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
}

static void
dbus_volume_out(HWVoiceOut *hw, Volume *vol)
{
    DBusAudio *da = (DBusAudio *)hw->s->drv_opaque;
    DBusVoiceOut *vo = container_of(hw, DBusVoiceOut, hw);
    GHashTableIter iter;
    QemuDBusDisplay1AudioOutListener *listener = NULL;

    vo->has_volume = true;
    vo->volume = *vol;

    g_hash_table_iter_init(&iter, da->out_listeners);
    while (g_hash_table_iter_next(&iter, NULL, (void **)&listener)) {
        dbus_volume_out_listener(hw, listener);
    }
}

static void
dbus_init_in_listener(QemuDBusDisplay1AudioInListener *listener, HWVoiceIn *hw)
{
    qemu_dbus_display1_audio_in_listener_call_init(
        listener,
        (uintptr_t)hw,
        hw->info.bits,
        hw->info.is_signed,
        hw->info.is_float,
        hw->info.freq,
        hw->info.nchannels,
        hw->info.bytes_per_frame,
        hw->info.bytes_per_second,
        hw->info.swap_endianness ? !AUDIO_HOST_BE : AUDIO_HOST_BE,
        G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
}

static int
dbus_init_in(HWVoiceIn *hw, struct audsettings *as, void *drv_opaque)
{
    DBusAudio *da = (DBusAudio *)hw->s->drv_opaque;
    DBusVoiceIn *vo = container_of(hw, DBusVoiceIn, hw);
    GHashTableIter iter;
    QemuDBusDisplay1AudioInListener *listener = NULL;

    audio_pcm_init_info(&hw->info, as);
    hw->samples = DBUS_AUDIO_NSAMPLES;
    audio_rate_start(&vo->rate);

    g_hash_table_iter_init(&iter, da->in_listeners);
    while (g_hash_table_iter_next(&iter, NULL, (void **)&listener)) {
        dbus_init_in_listener(listener, hw);
    }
    return 0;
}

static void
dbus_fini_in(HWVoiceIn *hw)
{
    DBusAudio *da = (DBusAudio *)hw->s->drv_opaque;
    GHashTableIter iter;
    QemuDBusDisplay1AudioInListener *listener = NULL;

    g_hash_table_iter_init(&iter, da->in_listeners);
    while (g_hash_table_iter_next(&iter, NULL, (void **)&listener)) {
        qemu_dbus_display1_audio_in_listener_call_fini(
            listener,
            (uintptr_t)hw,
            G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
    }
}

static void
dbus_volume_in_listener(HWVoiceIn *hw,
                         QemuDBusDisplay1AudioInListener *listener)
{
    DBusVoiceIn *vo = container_of(hw, DBusVoiceIn, hw);
    Volume *vol = &vo->volume;
    g_autoptr(GBytes) bytes = NULL;
    GVariant *v_vol = NULL;

    if (!vo->has_volume) {
        return;
    }

    assert(vol->channels < sizeof(vol->vol));
    bytes = g_bytes_new(vol->vol, vol->channels);
    v_vol = g_variant_new_from_bytes(G_VARIANT_TYPE("ay"), bytes, TRUE);
    qemu_dbus_display1_audio_in_listener_call_set_volume(
        listener, (uintptr_t)hw, vol->mute, v_vol,
        G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
}

static void
dbus_volume_in(HWVoiceIn *hw, Volume *vol)
{
    DBusAudio *da = (DBusAudio *)hw->s->drv_opaque;
    DBusVoiceIn *vo = container_of(hw, DBusVoiceIn, hw);
    GHashTableIter iter;
    QemuDBusDisplay1AudioInListener *listener = NULL;

    vo->has_volume = true;
    vo->volume = *vol;

    g_hash_table_iter_init(&iter, da->in_listeners);
    while (g_hash_table_iter_next(&iter, NULL, (void **)&listener)) {
        dbus_volume_in_listener(hw, listener);
    }
}

static size_t
dbus_read(HWVoiceIn *hw, void *buf, size_t size)
{
    DBusAudio *da = (DBusAudio *)hw->s->drv_opaque;
    /* DBusVoiceIn *vo = container_of(hw, DBusVoiceIn, hw); */
    GHashTableIter iter;
    QemuDBusDisplay1AudioInListener *listener = NULL;

    trace_dbus_audio_read(size);

    /* size = audio_rate_get_bytes(&vo->rate, &hw->info, size); */

    g_hash_table_iter_init(&iter, da->in_listeners);
    while (g_hash_table_iter_next(&iter, NULL, (void **)&listener)) {
        g_autoptr(GVariant) v_data = NULL;
        const char *data;
        gsize n = 0;

        if (qemu_dbus_display1_audio_in_listener_call_read_sync(
                listener,
                (uintptr_t)hw,
                size,
                G_DBUS_CALL_FLAGS_NONE, -1,
                &v_data, NULL, NULL)) {
            data = g_variant_get_fixed_array(v_data, &n, 1);
            g_warn_if_fail(n <= size);
            size = MIN(n, size);
            memcpy(buf, data, size);
            break;
        }
    }

    return size;
}

static void
dbus_enable_in(HWVoiceIn *hw, bool enable)
{
    DBusAudio *da = (DBusAudio *)hw->s->drv_opaque;
    DBusVoiceIn *vo = container_of(hw, DBusVoiceIn, hw);
    GHashTableIter iter;
    QemuDBusDisplay1AudioInListener *listener = NULL;

    vo->enabled = enable;
    if (enable) {
        audio_rate_start(&vo->rate);
    }

    g_hash_table_iter_init(&iter, da->in_listeners);
    while (g_hash_table_iter_next(&iter, NULL, (void **)&listener)) {
        qemu_dbus_display1_audio_in_listener_call_set_enabled(
            listener, (uintptr_t)hw, enable,
            G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
    }
}

static void *
dbus_audio_init(Audiodev *dev)
{
    DBusAudio *da = g_new0(DBusAudio, 1);

    da->out_listeners = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                g_free, g_object_unref);
    da->in_listeners = g_hash_table_new_full(g_str_hash, g_str_equal,
                                               g_free, g_object_unref);
    return da;
}

static void
dbus_audio_fini(void *opaque)
{
    DBusAudio *da = opaque;

    if (da->server) {
        g_dbus_object_manager_server_unexport(da->server,
                                              DBUS_DISPLAY1_AUDIO_PATH);
    }
    g_clear_object(&da->audio);
    g_clear_object(&da->iface);
    g_clear_pointer(&da->in_listeners, g_hash_table_unref);
    g_clear_pointer(&da->out_listeners, g_hash_table_unref);
    g_clear_object(&da->server);
    g_free(da);
}

static void
listener_out_vanished_cb(GDBusConnection *connection,
                         gboolean remote_peer_vanished,
                         GError *error,
                         DBusAudio *da)
{
    char *name = g_object_get_data(G_OBJECT(connection), "name");

    g_hash_table_remove(da->out_listeners, name);
}

static void
listener_in_vanished_cb(GDBusConnection *connection,
                        gboolean remote_peer_vanished,
                        GError *error,
                        DBusAudio *da)
{
    char *name = g_object_get_data(G_OBJECT(connection), "name");

    g_hash_table_remove(da->in_listeners, name);
}

static gboolean
dbus_audio_register_listener(AudioState *s,
                             GDBusMethodInvocation *invocation,
                             GUnixFDList *fd_list,
                             GVariant *arg_listener,
                             bool out)
{
    DBusAudio *da = s->drv_opaque;
    const char *sender = g_dbus_method_invocation_get_sender(invocation);
    g_autoptr(GDBusConnection) listener_conn = NULL;
    g_autoptr(GError) err = NULL;
    g_autoptr(GSocket) socket = NULL;
    g_autoptr(GSocketConnection) socket_conn = NULL;
    g_autofree char *guid = g_dbus_generate_guid();
    GHashTable *listeners = out ? da->out_listeners : da->in_listeners;
    GObject *listener;
    int fd;

    trace_dbus_audio_register(sender, out ? "out" : "in");

    if (g_hash_table_contains(listeners, sender)) {
        g_dbus_method_invocation_return_error(invocation,
                                              DBUS_DISPLAY_ERROR,
                                              DBUS_DISPLAY_ERROR_INVALID,
                                              "`%s` is already registered!",
                                              sender);
        return DBUS_METHOD_INVOCATION_HANDLED;
    }

    fd = g_unix_fd_list_get(fd_list, g_variant_get_handle(arg_listener), &err);
    if (err) {
        g_dbus_method_invocation_return_error(invocation,
                                              DBUS_DISPLAY_ERROR,
                                              DBUS_DISPLAY_ERROR_FAILED,
                                              "Couldn't get peer fd: %s",
                                              err->message);
        return DBUS_METHOD_INVOCATION_HANDLED;
    }

    socket = g_socket_new_from_fd(fd, &err);
    if (err) {
        g_dbus_method_invocation_return_error(invocation,
                                              DBUS_DISPLAY_ERROR,
                                              DBUS_DISPLAY_ERROR_FAILED,
                                              "Couldn't make a socket: %s",
                                              err->message);
        return DBUS_METHOD_INVOCATION_HANDLED;
    }
    socket_conn = g_socket_connection_factory_create_connection(socket);
    if (out) {
        qemu_dbus_display1_audio_complete_register_out_listener(
            da->iface, invocation, NULL);
    } else {
        qemu_dbus_display1_audio_complete_register_in_listener(
            da->iface, invocation, NULL);
    }

    listener_conn =
        g_dbus_connection_new_sync(
            G_IO_STREAM(socket_conn),
            guid,
            G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_SERVER,
            NULL, NULL, &err);
    if (err) {
        error_report("Failed to setup peer connection: %s", err->message);
        return DBUS_METHOD_INVOCATION_HANDLED;
    }

    listener = out ?
        G_OBJECT(qemu_dbus_display1_audio_out_listener_proxy_new_sync(
            listener_conn,
            G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
            NULL,
            "/org/qemu/Display1/AudioOutListener",
            NULL,
            &err)) :
        G_OBJECT(qemu_dbus_display1_audio_in_listener_proxy_new_sync(
            listener_conn,
            G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
            NULL,
            "/org/qemu/Display1/AudioInListener",
            NULL,
            &err));
    if (!listener) {
        error_report("Failed to setup proxy: %s", err->message);
        return DBUS_METHOD_INVOCATION_HANDLED;
    }

    if (out) {
        HWVoiceOut *hw;

        QLIST_FOREACH(hw, &s->hw_head_out, entries) {
            DBusVoiceOut *vo = container_of(hw, DBusVoiceOut, hw);
            QemuDBusDisplay1AudioOutListener *l =
                QEMU_DBUS_DISPLAY1_AUDIO_OUT_LISTENER(listener);

            dbus_init_out_listener(l, hw);
            qemu_dbus_display1_audio_out_listener_call_set_enabled(
                l, (uintptr_t)hw, vo->enabled,
                G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
        }
    } else {
        HWVoiceIn *hw;

        QLIST_FOREACH(hw, &s->hw_head_in, entries) {
            DBusVoiceIn *vo = container_of(hw, DBusVoiceIn, hw);
            QemuDBusDisplay1AudioInListener *l =
                QEMU_DBUS_DISPLAY1_AUDIO_IN_LISTENER(listener);

            dbus_init_in_listener(
                QEMU_DBUS_DISPLAY1_AUDIO_IN_LISTENER(listener), hw);
            qemu_dbus_display1_audio_in_listener_call_set_enabled(
                l, (uintptr_t)hw, vo->enabled,
                G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
        }
    }

    g_object_set_data_full(G_OBJECT(listener_conn), "name",
                           g_strdup(sender), g_free);
    g_hash_table_insert(listeners, g_strdup(sender), listener);
    g_object_connect(listener_conn,
                     "signal::closed",
                     out ? listener_out_vanished_cb : listener_in_vanished_cb,
                     da,
                     NULL);

    return DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
dbus_audio_register_out_listener(AudioState *s,
                                 GDBusMethodInvocation *invocation,
                                 GUnixFDList *fd_list,
                                 GVariant *arg_listener)
{
    return dbus_audio_register_listener(s, invocation,
                                        fd_list, arg_listener, true);

}

static gboolean
dbus_audio_register_in_listener(AudioState *s,
                                GDBusMethodInvocation *invocation,
                                GUnixFDList *fd_list,
                                GVariant *arg_listener)
{
    return dbus_audio_register_listener(s, invocation,
                                        fd_list, arg_listener, false);
}

static void
dbus_audio_set_server(AudioState *s, GDBusObjectManagerServer *server)
{
    DBusAudio *da = s->drv_opaque;

    g_assert(da);
    g_assert(!da->server);

    da->server = g_object_ref(server);

    da->audio = g_dbus_object_skeleton_new(DBUS_DISPLAY1_AUDIO_PATH);
    da->iface = qemu_dbus_display1_audio_skeleton_new();
    g_object_connect(da->iface,
                     "swapped-signal::handle-register-in-listener",
                     dbus_audio_register_in_listener, s,
                     "swapped-signal::handle-register-out-listener",
                     dbus_audio_register_out_listener, s,
                     NULL);

    g_dbus_object_skeleton_add_interface(G_DBUS_OBJECT_SKELETON(da->audio),
                                         G_DBUS_INTERFACE_SKELETON(da->iface));
    g_dbus_object_manager_server_export(da->server, da->audio);
}

static struct audio_pcm_ops dbus_pcm_ops = {
    .init_out = dbus_init_out,
    .fini_out = dbus_fini_out,
    .write    = audio_generic_write,
    .get_buffer_out = dbus_get_buffer_out,
    .put_buffer_out = dbus_put_buffer_out,
    .enable_out = dbus_enable_out,
    .volume_out = dbus_volume_out,

    .init_in  = dbus_init_in,
    .fini_in  = dbus_fini_in,
    .read     = dbus_read,
    .run_buffer_in = audio_generic_run_buffer_in,
    .enable_in = dbus_enable_in,
    .volume_in = dbus_volume_in,
};

static struct audio_driver dbus_audio_driver = {
    .name            = "dbus",
    .descr           = "Timer based audio exposed with DBus interface",
    .init            = dbus_audio_init,
    .fini            = dbus_audio_fini,
    .set_dbus_server = dbus_audio_set_server,
    .pcm_ops         = &dbus_pcm_ops,
    .can_be_default  = 1,
    .max_voices_out  = INT_MAX,
    .max_voices_in   = INT_MAX,
    .voice_size_out  = sizeof(DBusVoiceOut),
    .voice_size_in   = sizeof(DBusVoiceIn)
};

static void register_audio_dbus(void)
{
    audio_driver_register(&dbus_audio_driver);
}
type_init(register_audio_dbus);

module_dep("ui-dbus")

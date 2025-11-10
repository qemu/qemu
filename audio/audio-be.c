/* SPDX-License-Identifier: MIT */

#include "qemu/osdep.h"
#include "qemu/audio.h"
#include "qemu/audio-capture.h"
#include "qapi/error.h"

bool audio_be_check(AudioBackend **be, Error **errp)
{
    assert(be != NULL);

    if (!*be) {
        *be = audio_get_default_audio_be(errp);
        if (!*be) {
            return false;
        }
    }

    return true;
}

SWVoiceIn *audio_be_open_in(
    AudioBackend *be,
    SWVoiceIn *sw,
    const char *name,
    void *callback_opaque,
    audio_callback_fn callback_fn,
    const struct audsettings *as)
{
    AudioBackendClass *klass = AUDIO_BACKEND_GET_CLASS(be);

    return klass->open_in(be, sw, name, callback_opaque, callback_fn, as);
}

SWVoiceOut *audio_be_open_out(
    AudioBackend *be,
    SWVoiceOut *sw,
    const char *name,
    void *callback_opaque,
    audio_callback_fn callback_fn,
    const struct audsettings *as)
{
    AudioBackendClass *klass = AUDIO_BACKEND_GET_CLASS(be);

    return klass->open_out(be, sw, name, callback_opaque, callback_fn, as);
}

void audio_be_close_out(AudioBackend *be, SWVoiceOut *sw)
{
    AudioBackendClass *klass = AUDIO_BACKEND_GET_CLASS(be);

    return klass->close_out(be, sw);
}

void audio_be_close_in(AudioBackend *be, SWVoiceIn *sw)
{
    AudioBackendClass *klass = AUDIO_BACKEND_GET_CLASS(be);

    return klass->close_in(be, sw);
}

bool audio_be_is_active_out(AudioBackend *be, SWVoiceOut *sw)
{
    AudioBackendClass *klass = AUDIO_BACKEND_GET_CLASS(be);

    return klass->is_active_out(be, sw);
}

bool audio_be_is_active_in(AudioBackend *be, SWVoiceIn *sw)
{
    AudioBackendClass *klass = AUDIO_BACKEND_GET_CLASS(be);

    return klass->is_active_in(be, sw);
}

size_t audio_be_write(AudioBackend *be, SWVoiceOut *sw, void *buf, size_t size)
{
    AudioBackendClass *klass = AUDIO_BACKEND_GET_CLASS(be);

    return klass->write(be, sw, buf, size);
}

size_t audio_be_read(AudioBackend *be, SWVoiceIn *sw, void *buf, size_t size)
{
    AudioBackendClass *klass = AUDIO_BACKEND_GET_CLASS(be);

    return klass->read(be, sw, buf, size);
}

int audio_be_get_buffer_size_out(AudioBackend *be, SWVoiceOut *sw)
{
    AudioBackendClass *klass = AUDIO_BACKEND_GET_CLASS(be);

    return klass->get_buffer_size_out(be, sw);
}

void audio_be_set_active_out(AudioBackend *be, SWVoiceOut *sw, bool on)
{
    AudioBackendClass *klass = AUDIO_BACKEND_GET_CLASS(be);

    return klass->set_active_out(be, sw, on);
}

void audio_be_set_active_in(AudioBackend *be, SWVoiceIn *sw, bool on)
{
    AudioBackendClass *klass = AUDIO_BACKEND_GET_CLASS(be);

    return klass->set_active_in(be, sw, on);
}

void audio_be_set_volume_out(AudioBackend *be, SWVoiceOut *sw, Volume *vol)
{
    AudioBackendClass *klass = AUDIO_BACKEND_GET_CLASS(be);

    klass->set_volume_out(be, sw, vol);
}

void audio_be_set_volume_in(AudioBackend *be, SWVoiceIn *sw, Volume *vol)
{
    AudioBackendClass *klass = AUDIO_BACKEND_GET_CLASS(be);

    klass->set_volume_in(be, sw, vol);
}

CaptureVoiceOut *audio_be_add_capture(
    AudioBackend *be,
    struct audsettings *as,
    struct audio_capture_ops *ops,
    void *cb_opaque)
{
    AudioBackendClass *klass = AUDIO_BACKEND_GET_CLASS(be);

    return klass->add_capture(be, as, ops, cb_opaque);
}

void audio_be_del_capture(AudioBackend *be, CaptureVoiceOut *cap, void *cb_opaque)
{
    AudioBackendClass *klass = AUDIO_BACKEND_GET_CLASS(be);

    klass->del_capture(be, cap, cb_opaque);
}

#ifdef CONFIG_GIO
bool audio_be_can_set_dbus_server(AudioBackend *be)
{
    AudioBackendClass *klass = AUDIO_BACKEND_GET_CLASS(be);

    return klass->set_dbus_server != NULL;
}

bool audio_be_set_dbus_server(AudioBackend *be,
                              GDBusObjectManagerServer *server,
                              bool p2p,
                              Error **errp)
{
    AudioBackendClass *klass = AUDIO_BACKEND_GET_CLASS(be);

    if (!audio_be_can_set_dbus_server(be)) {
        error_setg(errp, "Audiodev '%s' is not compatible with DBus",
                   audio_be_get_id(be));
        return false;
    }

    return klass->set_dbus_server(be, server, p2p, errp);
}
#endif

const char *audio_be_get_id(AudioBackend *be)
{
    if (be) {
        return AUDIO_BACKEND_GET_CLASS(be)->get_id(be);
    } else {
        return "";
    }
}

AudioBackend *audio_be_new(Audiodev *dev, Error **errp)
{
    const char *drvname = AudiodevDriver_str(dev->driver);
    g_autofree char *type = g_strconcat("audio-", drvname, NULL);
    AudioBackend *be = AUDIO_BACKEND(object_new(type));

    if (!be) {
        error_setg(errp, "Unknown audio driver `%s'", drvname);
        return NULL;
    }

    if (!AUDIO_BACKEND_GET_CLASS(be)->realize(be, dev, errp)) {
        object_unref(OBJECT(be));
        return NULL;
    }

    return be;
}

static const TypeInfo audio_types[] = {
    {
        .name = TYPE_AUDIO_BACKEND,
        .parent = TYPE_OBJECT,
        .instance_size = sizeof(AudioBackend),
        .abstract = true,
        .class_size = sizeof(AudioBackendClass),
    },
};

DEFINE_TYPES(audio_types)

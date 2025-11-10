/* SPDX-License-Identifier: MIT */

#include "qemu/osdep.h"
#include "qemu/audio.h"
#include "qemu/help_option.h"
#include "qapi/clone-visitor.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qapi-visit-audio.h"
#include "qapi/qapi-commands-audio.h"
#include "qobject/qdict.h"
#include "system/system.h"

/* Order of CONFIG_AUDIO_DRIVERS is import.
   The 1st one is the one used by default, that is the reason
    that we generate the list.
*/
const char *audio_prio_list[] = {
#ifdef CONFIG_GIO
    "dbus",
#endif
    "spice",
    CONFIG_AUDIO_DRIVERS
    "none",
    NULL
};

typedef struct AudiodevListEntry {
    Audiodev *dev;
    QSIMPLEQ_ENTRY(AudiodevListEntry) next;
} AudiodevListEntry;

typedef QSIMPLEQ_HEAD(, AudiodevListEntry) AudiodevListHead;

static AudiodevListHead audiodevs =
    QSIMPLEQ_HEAD_INITIALIZER(audiodevs);
static AudiodevListHead default_audiodevs =
    QSIMPLEQ_HEAD_INITIALIZER(default_audiodevs);

static AudioBackendClass *audio_be_class_by_name(const char *name)
{
    g_autofree char *tname = g_strconcat("audio-", name, NULL);
    ObjectClass *oc = module_object_class_by_name(tname);

    if (!oc || !object_class_dynamic_cast(oc, TYPE_AUDIO_BACKEND)) {
        return NULL;
    }

    return AUDIO_BACKEND_CLASS(oc);
}

static AudioBackend *default_audio_be;

static Object *get_audiodevs_root(void)
{
    return object_get_container("audiodevs");
}

void audio_cleanup(void)
{
    default_audio_be = NULL;

    object_unparent(get_audiodevs_root());
}

void audio_create_default_audiodevs(void)
{
    for (int i = 0; audio_prio_list[i]; i++) {
        if (audio_be_class_by_name(audio_prio_list[i]) != NULL) {
            QDict *dict = qdict_new();
            Audiodev *dev = NULL;
            Visitor *v;

            qdict_put_str(dict, "driver", audio_prio_list[i]);
            qdict_put_str(dict, "id", "#default");

            v = qobject_input_visitor_new_keyval(QOBJECT(dict));
            qobject_unref(dict);
            visit_type_Audiodev(v, NULL, &dev, &error_fatal);
            visit_free(v);

            audio_add_default_audiodev(dev, &error_abort);
        }
    }
}

/*
 * if we have dev, this function was called because of an -audiodev argument =>
 *   initialize a new state with it
 * if dev == NULL => legacy implicit initialization, return the already created
 *   state or create a new one
 */
static AudioBackend *audio_init(Audiodev *dev, Error **errp)
{
    AudioBackend *be;

    if (dev) {
        be = audio_be_new(dev, errp);
        if (!be) {
            return NULL;
        }
    } else {
        assert(!default_audio_be);
        for (;;) {
            AudiodevListEntry *e = QSIMPLEQ_FIRST(&default_audiodevs);
            if (!e) {
                error_setg(errp, "no default audio driver available");
                return NULL;
            }
            QSIMPLEQ_REMOVE_HEAD(&default_audiodevs, next);
            be = audio_be_new(e->dev, NULL);
            g_free(e);
            if (be) {
                break;
            }
        }
    }

    if (!object_property_try_add_child(get_audiodevs_root(),
                                       audio_be_get_id(be), OBJECT(be), errp)) {
        object_unref(be);
        return NULL;
    }
    object_unref(be);
    return be;
}

AudioBackend *audio_get_default_audio_be(Error **errp)
{
    if (!default_audio_be) {
        default_audio_be = audio_init(NULL, errp);
        if (!default_audio_be) {
            if (!QSIMPLEQ_EMPTY(&audiodevs)) {
                error_append_hint(errp, "Perhaps you wanted to use -audio or set audiodev=%s?\n",
                                  QSIMPLEQ_FIRST(&audiodevs)->dev->id);
            }
        }
    }

    return default_audio_be;
}

void audio_help(void)
{
    int i;

    printf("Available audio drivers:\n");

    for (i = 0; i < AUDIODEV_DRIVER__MAX; i++) {
        const char *name = AudiodevDriver_str(i);
        AudioBackendClass *be = audio_be_class_by_name(name);

        if (be) {
            printf("%s\n", name);
        }
    }
}

void audio_parse_option(const char *opt)
{
    Audiodev *dev = NULL;

    if (is_help_option(opt)) {
        audio_help();
        exit(EXIT_SUCCESS);
    }
    Visitor *v = qobject_input_visitor_new_str(opt, "driver", &error_fatal);
    visit_type_Audiodev(v, NULL, &dev, &error_fatal);
    visit_free(v);

    audio_add_audiodev(dev);
}

static void audio_create_pdos(Audiodev *dev)
{
    switch (dev->driver) {
#define CASE(DRIVER, driver, pdo_name)                              \
    case AUDIODEV_DRIVER_##DRIVER:                                  \
        if (!dev->u.driver.in) {                                    \
            dev->u.driver.in = g_malloc0(                           \
                sizeof(Audiodev##pdo_name##PerDirectionOptions));   \
        }                                                           \
        if (!dev->u.driver.out) {                                   \
            dev->u.driver.out = g_malloc0(                          \
                sizeof(Audiodev##pdo_name##PerDirectionOptions));   \
        }                                                           \
        break

        CASE(NONE, none, );
#ifdef CONFIG_AUDIO_ALSA
        CASE(ALSA, alsa, Alsa);
#endif
#ifdef CONFIG_AUDIO_COREAUDIO
        CASE(COREAUDIO, coreaudio, Coreaudio);
#endif
#ifdef CONFIG_DBUS_DISPLAY
        CASE(DBUS, dbus, );
#endif
#ifdef CONFIG_AUDIO_DSOUND
        CASE(DSOUND, dsound, );
#endif
#ifdef CONFIG_AUDIO_JACK
        CASE(JACK, jack, Jack);
#endif
#ifdef CONFIG_AUDIO_OSS
        CASE(OSS, oss, Oss);
#endif
#ifdef CONFIG_AUDIO_PA
        CASE(PA, pa, Pa);
#endif
#ifdef CONFIG_AUDIO_PIPEWIRE
        CASE(PIPEWIRE, pipewire, Pipewire);
#endif
#ifdef CONFIG_AUDIO_SDL
        CASE(SDL, sdl, Sdl);
#endif
#ifdef CONFIG_AUDIO_SNDIO
        CASE(SNDIO, sndio, );
#endif
#ifdef CONFIG_SPICE
        CASE(SPICE, spice, );
#endif
        CASE(WAV, wav, );

    case AUDIODEV_DRIVER__MAX:
        abort();
    };
}

static void audio_validate_per_direction_opts(
    AudiodevPerDirectionOptions *pdo, Error **errp)
{
    if (!pdo->has_mixing_engine) {
        pdo->has_mixing_engine = true;
        pdo->mixing_engine = true;
    }
    if (!pdo->has_fixed_settings) {
        pdo->has_fixed_settings = true;
        pdo->fixed_settings = pdo->mixing_engine;
    }
    if (!pdo->fixed_settings &&
        (pdo->has_frequency || pdo->has_channels || pdo->has_format)) {
        error_setg(errp,
                   "You can't use frequency, channels or format with fixed-settings=off");
        return;
    }
    if (!pdo->mixing_engine && pdo->fixed_settings) {
        error_setg(errp, "You can't use fixed-settings without mixeng");
        return;
    }

    if (!pdo->has_frequency) {
        pdo->has_frequency = true;
        pdo->frequency = 44100;
    }
    if (!pdo->has_channels) {
        pdo->has_channels = true;
        pdo->channels = 2;
    }
    if (!pdo->has_voices) {
        pdo->has_voices = true;
        pdo->voices = pdo->mixing_engine ? 1 : INT_MAX;
    }
    if (!pdo->has_format) {
        pdo->has_format = true;
        pdo->format = AUDIO_FORMAT_S16;
    }
}

static AudiodevPerDirectionOptions *audio_get_pdo_out(Audiodev *dev)
{
    switch (dev->driver) {
    case AUDIODEV_DRIVER_NONE:
        return dev->u.none.out;
#ifdef CONFIG_AUDIO_ALSA
    case AUDIODEV_DRIVER_ALSA:
        return qapi_AudiodevAlsaPerDirectionOptions_base(dev->u.alsa.out);
#endif
#ifdef CONFIG_AUDIO_COREAUDIO
    case AUDIODEV_DRIVER_COREAUDIO:
        return qapi_AudiodevCoreaudioPerDirectionOptions_base(
            dev->u.coreaudio.out);
#endif
#ifdef CONFIG_DBUS_DISPLAY
    case AUDIODEV_DRIVER_DBUS:
        return dev->u.dbus.out;
#endif
#ifdef CONFIG_AUDIO_DSOUND
    case AUDIODEV_DRIVER_DSOUND:
        return dev->u.dsound.out;
#endif
#ifdef CONFIG_AUDIO_JACK
    case AUDIODEV_DRIVER_JACK:
        return qapi_AudiodevJackPerDirectionOptions_base(dev->u.jack.out);
#endif
#ifdef CONFIG_AUDIO_OSS
    case AUDIODEV_DRIVER_OSS:
        return qapi_AudiodevOssPerDirectionOptions_base(dev->u.oss.out);
#endif
#ifdef CONFIG_AUDIO_PA
    case AUDIODEV_DRIVER_PA:
        return qapi_AudiodevPaPerDirectionOptions_base(dev->u.pa.out);
#endif
#ifdef CONFIG_AUDIO_PIPEWIRE
    case AUDIODEV_DRIVER_PIPEWIRE:
        return qapi_AudiodevPipewirePerDirectionOptions_base(dev->u.pipewire.out);
#endif
#ifdef CONFIG_AUDIO_SDL
    case AUDIODEV_DRIVER_SDL:
        return qapi_AudiodevSdlPerDirectionOptions_base(dev->u.sdl.out);
#endif
#ifdef CONFIG_AUDIO_SNDIO
    case AUDIODEV_DRIVER_SNDIO:
        return dev->u.sndio.out;
#endif
#ifdef CONFIG_SPICE
    case AUDIODEV_DRIVER_SPICE:
        return dev->u.spice.out;
#endif
    case AUDIODEV_DRIVER_WAV:
        return dev->u.wav.out;

    case AUDIODEV_DRIVER__MAX:
        break;
    }
    abort();
}

static AudiodevPerDirectionOptions *audio_get_pdo_in(Audiodev *dev)
{
    switch (dev->driver) {
    case AUDIODEV_DRIVER_NONE:
        return dev->u.none.in;
#ifdef CONFIG_AUDIO_ALSA
    case AUDIODEV_DRIVER_ALSA:
        return qapi_AudiodevAlsaPerDirectionOptions_base(dev->u.alsa.in);
#endif
#ifdef CONFIG_AUDIO_COREAUDIO
    case AUDIODEV_DRIVER_COREAUDIO:
        return qapi_AudiodevCoreaudioPerDirectionOptions_base(
            dev->u.coreaudio.in);
#endif
#ifdef CONFIG_DBUS_DISPLAY
    case AUDIODEV_DRIVER_DBUS:
        return dev->u.dbus.in;
#endif
#ifdef CONFIG_AUDIO_DSOUND
    case AUDIODEV_DRIVER_DSOUND:
        return dev->u.dsound.in;
#endif
#ifdef CONFIG_AUDIO_JACK
    case AUDIODEV_DRIVER_JACK:
        return qapi_AudiodevJackPerDirectionOptions_base(dev->u.jack.in);
#endif
#ifdef CONFIG_AUDIO_OSS
    case AUDIODEV_DRIVER_OSS:
        return qapi_AudiodevOssPerDirectionOptions_base(dev->u.oss.in);
#endif
#ifdef CONFIG_AUDIO_PA
    case AUDIODEV_DRIVER_PA:
        return qapi_AudiodevPaPerDirectionOptions_base(dev->u.pa.in);
#endif
#ifdef CONFIG_AUDIO_PIPEWIRE
    case AUDIODEV_DRIVER_PIPEWIRE:
        return qapi_AudiodevPipewirePerDirectionOptions_base(dev->u.pipewire.in);
#endif
#ifdef CONFIG_AUDIO_SDL
    case AUDIODEV_DRIVER_SDL:
        return qapi_AudiodevSdlPerDirectionOptions_base(dev->u.sdl.in);
#endif
#ifdef CONFIG_AUDIO_SNDIO
    case AUDIODEV_DRIVER_SNDIO:
        return dev->u.sndio.in;
#endif
#ifdef CONFIG_SPICE
    case AUDIODEV_DRIVER_SPICE:
        return dev->u.spice.in;
#endif
    case AUDIODEV_DRIVER_WAV:
        return dev->u.wav.in;

    case AUDIODEV_DRIVER__MAX:
        break;
    }
    abort();
}

static void audio_validate_opts(Audiodev *dev, Error **errp)
{
    Error *err = NULL;

    audio_create_pdos(dev);

    audio_validate_per_direction_opts(audio_get_pdo_in(dev), &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    audio_validate_per_direction_opts(audio_get_pdo_out(dev), &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    if (!dev->has_timer_period) {
        dev->has_timer_period = true;
        dev->timer_period = 10000; /* 100Hz -> 10ms */
    }
}

void audio_add_audiodev(Audiodev *dev)
{
    AudiodevListEntry *e;

    audio_validate_opts(dev, &error_fatal);

    e = g_new0(AudiodevListEntry, 1);
    e->dev = dev;
    QSIMPLEQ_INSERT_TAIL(&audiodevs, e, next);
}

void audio_add_default_audiodev(Audiodev *dev, Error **errp)
{
    AudiodevListEntry *e;

    audio_validate_opts(dev, errp);

    e = g_new0(AudiodevListEntry, 1);
    e->dev = dev;
    QSIMPLEQ_INSERT_TAIL(&default_audiodevs, e, next);
}

void audio_init_audiodevs(void)
{
    AudiodevListEntry *e;

    QSIMPLEQ_FOREACH(e, &audiodevs, next) {
        audio_init(QAPI_CLONE(Audiodev, e->dev), &error_fatal);
    }
}

AudioBackend *audio_be_by_name(const char *name, Error **errp)
{
    Object *obj = object_resolve_path_component(get_audiodevs_root(), name);

    if (!obj) {
        error_setg(errp, "audiodev '%s' not found", name);
        return NULL;
    } else {
        return AUDIO_BACKEND(obj);
    }
}

const char *audio_application_name(void)
{
    const char *vm_name;

    vm_name = qemu_get_vm_name();
    return vm_name ? vm_name : "qemu";
}

AudiodevList *qmp_query_audiodevs(Error **errp)
{
    AudiodevList *ret = NULL;
    AudiodevListEntry *e;
    QSIMPLEQ_FOREACH(e, &audiodevs, next) {
        QAPI_LIST_PREPEND(ret, QAPI_CLONE(Audiodev, e->dev));
    }
    return ret;
}

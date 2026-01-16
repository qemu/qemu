/*
 * Copyright (C) 2010 Red Hat, Inc.
 *
 * maintained by Gerd Hoffmann <kraxel@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "ui/qemu-spice.h"
#include "qom/object.h"

#define AUDIO_CAP "spice"
#include "qemu/audio.h"
#include "audio_int.h"

#define TYPE_AUDIO_SPICE "audio-spice"
OBJECT_DECLARE_SIMPLE_TYPE(AudioSpice, AUDIO_SPICE)

static AudioBackendClass *audio_spice_parent_class;

struct AudioSpice {
    AudioMixengBackend parent_obj;
};

static bool spice_audio_realize(AudioBackend *abe, Audiodev *dev, Error **errp)
{
    if (!using_spice) {
        error_setg(errp, "Cannot use spice audio without -spice");
        qapi_free_Audiodev(dev);
        return false;
    }

    return audio_spice_parent_class->realize(abe, dev, errp);
}

#if SPICE_INTERFACE_PLAYBACK_MAJOR > 1 || SPICE_INTERFACE_PLAYBACK_MINOR >= 3
#define LINE_OUT_SAMPLES (480 * 4)
#else
#define LINE_OUT_SAMPLES (256 * 4)
#endif

#if SPICE_INTERFACE_RECORD_MAJOR > 2 || SPICE_INTERFACE_RECORD_MINOR >= 3
#define LINE_IN_SAMPLES (480 * 4)
#else
#define LINE_IN_SAMPLES (256 * 4)
#endif

typedef struct SpiceVoiceOut {
    HWVoiceOut            hw;
    SpicePlaybackInstance sin;
    RateCtl               rate;
    int                   active;
    uint32_t              *frame;
    uint32_t              fpos;
    uint32_t              fsize;
} SpiceVoiceOut;

typedef struct SpiceVoiceIn {
    HWVoiceIn             hw;
    SpiceRecordInstance   sin;
    RateCtl               rate;
    int                   active;
} SpiceVoiceIn;

static const SpicePlaybackInterface playback_sif = {
    .base.type          = SPICE_INTERFACE_PLAYBACK,
    .base.description   = "playback",
    .base.major_version = SPICE_INTERFACE_PLAYBACK_MAJOR,
    .base.minor_version = SPICE_INTERFACE_PLAYBACK_MINOR,
};

static const SpiceRecordInterface record_sif = {
    .base.type          = SPICE_INTERFACE_RECORD,
    .base.description   = "record",
    .base.major_version = SPICE_INTERFACE_RECORD_MAJOR,
    .base.minor_version = SPICE_INTERFACE_RECORD_MINOR,
};

/* playback */

static int line_out_init(HWVoiceOut *hw, struct audsettings *as)
{
    SpiceVoiceOut *out = container_of (hw, SpiceVoiceOut, hw);
    struct audsettings settings;

#if SPICE_INTERFACE_PLAYBACK_MAJOR > 1 || SPICE_INTERFACE_PLAYBACK_MINOR >= 3
    settings.freq       = spice_server_get_best_playback_rate(NULL);
#else
    settings.freq       = SPICE_INTERFACE_PLAYBACK_FREQ;
#endif
    settings.nchannels  = SPICE_INTERFACE_PLAYBACK_CHAN;
    settings.fmt        = AUDIO_FORMAT_S16;
    settings.endianness = HOST_BIG_ENDIAN;

    audio_pcm_init_info (&hw->info, &settings);
    hw->samples = LINE_OUT_SAMPLES;
    out->active = 0;

    out->sin.base.sif = &playback_sif.base;
    qemu_spice.add_interface(&out->sin.base);
#if SPICE_INTERFACE_PLAYBACK_MAJOR > 1 || SPICE_INTERFACE_PLAYBACK_MINOR >= 3
    spice_server_set_playback_rate(&out->sin, settings.freq);
#endif
    return 0;
}

static void line_out_fini (HWVoiceOut *hw)
{
    SpiceVoiceOut *out = container_of (hw, SpiceVoiceOut, hw);

    spice_server_remove_interface (&out->sin.base);
}

static size_t line_out_get_free(HWVoiceOut *hw)
{
    SpiceVoiceOut *out = container_of(hw, SpiceVoiceOut, hw);

    return audio_rate_peek_bytes(&out->rate, &hw->info);
}

static void *line_out_get_buffer(HWVoiceOut *hw, size_t *size)
{
    SpiceVoiceOut *out = container_of(hw, SpiceVoiceOut, hw);

    if (!out->frame) {
        spice_server_playback_get_buffer(&out->sin, &out->frame, &out->fsize);
        out->fpos = 0;
    }

    if (out->frame) {
        *size = MIN((out->fsize - out->fpos) << 2, *size);
    }

    return out->frame + out->fpos;
}

static size_t line_out_put_buffer(HWVoiceOut *hw, void *buf, size_t size)
{
    SpiceVoiceOut *out = container_of(hw, SpiceVoiceOut, hw);

    audio_rate_add_bytes(&out->rate, size);

    if (buf) {
        assert(buf == out->frame + out->fpos && out->fpos <= out->fsize);
        out->fpos += size >> 2;

        if (out->fpos == out->fsize) { /* buffer full */
            spice_server_playback_put_samples(&out->sin, out->frame);
            out->frame = NULL;
        }
    }

    return size;
}

static void line_out_enable(HWVoiceOut *hw, bool enable)
{
    SpiceVoiceOut *out = container_of (hw, SpiceVoiceOut, hw);

    if (enable) {
        if (out->active) {
            return;
        }
        out->active = 1;
        audio_rate_start(&out->rate);
        spice_server_playback_start (&out->sin);
    } else {
        if (!out->active) {
            return;
        }
        out->active = 0;
        if (out->frame) {
            memset(out->frame + out->fpos, 0, (out->fsize - out->fpos) << 2);
            spice_server_playback_put_samples (&out->sin, out->frame);
            out->frame = NULL;
        }
        spice_server_playback_stop (&out->sin);
    }
}

#if ((SPICE_INTERFACE_PLAYBACK_MAJOR >= 1) && (SPICE_INTERFACE_PLAYBACK_MINOR >= 2))
static void line_out_volume(HWVoiceOut *hw, Volume *vol)
{
    SpiceVoiceOut *out = container_of(hw, SpiceVoiceOut, hw);
    uint16_t svol[2];

    assert(vol->channels == 2);
    svol[0] = vol->vol[0] * 257;
    svol[1] = vol->vol[1] * 257;
    spice_server_playback_set_volume(&out->sin, 2, svol);
    spice_server_playback_set_mute(&out->sin, vol->mute);
}
#endif

/* record */

static int line_in_init(HWVoiceIn *hw, struct audsettings *as)
{
    SpiceVoiceIn *in = container_of (hw, SpiceVoiceIn, hw);
    struct audsettings settings;

#if SPICE_INTERFACE_RECORD_MAJOR > 2 || SPICE_INTERFACE_RECORD_MINOR >= 3
    settings.freq       = spice_server_get_best_record_rate(NULL);
#else
    settings.freq       = SPICE_INTERFACE_RECORD_FREQ;
#endif
    settings.nchannels  = SPICE_INTERFACE_RECORD_CHAN;
    settings.fmt        = AUDIO_FORMAT_S16;
    settings.endianness = HOST_BIG_ENDIAN;

    audio_pcm_init_info (&hw->info, &settings);
    hw->samples = LINE_IN_SAMPLES;
    in->active = 0;

    in->sin.base.sif = &record_sif.base;
    qemu_spice.add_interface(&in->sin.base);
#if SPICE_INTERFACE_RECORD_MAJOR > 2 || SPICE_INTERFACE_RECORD_MINOR >= 3
    spice_server_set_record_rate(&in->sin, settings.freq);
#endif
    return 0;
}

static void line_in_fini (HWVoiceIn *hw)
{
    SpiceVoiceIn *in = container_of (hw, SpiceVoiceIn, hw);

    spice_server_remove_interface (&in->sin.base);
}

static size_t line_in_read(HWVoiceIn *hw, void *buf, size_t len)
{
    SpiceVoiceIn *in = container_of (hw, SpiceVoiceIn, hw);
    uint64_t to_read = audio_rate_get_bytes(&in->rate, &hw->info, len) >> 2;
    size_t ready = spice_server_record_get_samples(&in->sin, buf, to_read);

    /*
     * If the client didn't send new frames, it most likely disconnected.
     * Generate silence in this case to avoid a stalled audio stream.
     */
    if (ready == 0) {
        memset(buf, 0, to_read << 2);
        ready = to_read;
    }

    return ready << 2;
}

static void line_in_enable(HWVoiceIn *hw, bool enable)
{
    SpiceVoiceIn *in = container_of (hw, SpiceVoiceIn, hw);

    if (enable) {
        if (in->active) {
            return;
        }
        in->active = 1;
        audio_rate_start(&in->rate);
        spice_server_record_start (&in->sin);
    } else {
        if (!in->active) {
            return;
        }
        in->active = 0;
        spice_server_record_stop (&in->sin);
    }
}

#if ((SPICE_INTERFACE_RECORD_MAJOR >= 2) && (SPICE_INTERFACE_RECORD_MINOR >= 2))
static void line_in_volume(HWVoiceIn *hw, Volume *vol)
{
    SpiceVoiceIn *in = container_of(hw, SpiceVoiceIn, hw);
    uint16_t svol[2];

    assert(vol->channels == 2);
    svol[0] = vol->vol[0] * 257;
    svol[1] = vol->vol[1] * 257;
    spice_server_record_set_volume(&in->sin, 2, svol);
    spice_server_record_set_mute(&in->sin, vol->mute);
}
#endif

static struct audio_pcm_ops audio_callbacks = {
    .init_out = line_out_init,
    .fini_out = line_out_fini,
    .write    = audio_generic_write,
    .buffer_get_free = line_out_get_free,
    .get_buffer_out = line_out_get_buffer,
    .put_buffer_out = line_out_put_buffer,
    .enable_out = line_out_enable,
#if (SPICE_INTERFACE_PLAYBACK_MAJOR >= 1) && \
        (SPICE_INTERFACE_PLAYBACK_MINOR >= 2)
    .volume_out = line_out_volume,
#endif

    .init_in  = line_in_init,
    .fini_in  = line_in_fini,
    .read     = line_in_read,
    .run_buffer_in = audio_generic_run_buffer_in,
    .enable_in = line_in_enable,
#if ((SPICE_INTERFACE_RECORD_MAJOR >= 2) && (SPICE_INTERFACE_RECORD_MINOR >= 2))
    .volume_in = line_in_volume,
#endif
};

static void audio_spice_class_init(ObjectClass *klass, const void *data)
{
    AudioBackendClass *b = AUDIO_BACKEND_CLASS(klass);
    AudioMixengBackendClass *k = AUDIO_MIXENG_BACKEND_CLASS(klass);

    audio_spice_parent_class = AUDIO_BACKEND_CLASS(object_class_get_parent(klass));

    b->realize = spice_audio_realize;
    k->name = "spice";
    k->pcm_ops = &audio_callbacks;
    k->max_voices_out = 1;
    k->max_voices_in = 1;
    k->voice_size_out = sizeof(SpiceVoiceOut);
    k->voice_size_in = sizeof(SpiceVoiceIn);
}

static const TypeInfo audio_types[] = {
    {
        .name = TYPE_AUDIO_SPICE,
        .parent = TYPE_AUDIO_MIXENG_BACKEND,
        .instance_size = sizeof(AudioSpice),
        .class_init = audio_spice_class_init,
    },
};

DEFINE_TYPES(audio_types)
module_obj(TYPE_AUDIO_SPICE);

module_dep("ui-spice-core");

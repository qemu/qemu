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

#include "hw/hw.h"
#include "qemu-timer.h"
#include "ui/qemu-spice.h"

#define AUDIO_CAP "spice"
#include "audio.h"
#include "audio_int.h"

#define LINE_IN_SAMPLES 1024
#define LINE_OUT_SAMPLES 1024

typedef struct SpiceRateCtl {
    int64_t               start_ticks;
    int64_t               bytes_sent;
} SpiceRateCtl;

typedef struct SpiceVoiceOut {
    HWVoiceOut            hw;
    SpicePlaybackInstance sin;
    SpiceRateCtl          rate;
    int                   active;
    uint32_t              *frame;
    uint32_t              *fpos;
    uint32_t              fsize;
} SpiceVoiceOut;

typedef struct SpiceVoiceIn {
    HWVoiceIn             hw;
    SpiceRecordInstance   sin;
    SpiceRateCtl          rate;
    int                   active;
    uint32_t              samples[LINE_IN_SAMPLES];
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

static void *spice_audio_init (void)
{
    if (!using_spice) {
        return NULL;
    }
    return &spice_audio_init;
}

static void spice_audio_fini (void *opaque)
{
    /* nothing */
}

static void rate_start (SpiceRateCtl *rate)
{
    memset (rate, 0, sizeof (*rate));
    rate->start_ticks = qemu_get_clock (vm_clock);
}

static int rate_get_samples (struct audio_pcm_info *info, SpiceRateCtl *rate)
{
    int64_t now;
    int64_t ticks;
    int64_t bytes;
    int64_t samples;

    now = qemu_get_clock (vm_clock);
    ticks = now - rate->start_ticks;
    bytes = muldiv64 (ticks, info->bytes_per_second, get_ticks_per_sec ());
    samples = (bytes - rate->bytes_sent) >> info->shift;
    if (samples < 0 || samples > 65536) {
        fprintf (stderr, "Resetting rate control (%" PRId64 " samples)\n", samples);
        rate_start (rate);
        samples = 0;
    }
    rate->bytes_sent += samples << info->shift;
    return samples;
}

/* playback */

static int line_out_init (HWVoiceOut *hw, struct audsettings *as)
{
    SpiceVoiceOut *out = container_of (hw, SpiceVoiceOut, hw);
    struct audsettings settings;

    settings.freq       = SPICE_INTERFACE_PLAYBACK_FREQ;
    settings.nchannels  = SPICE_INTERFACE_PLAYBACK_CHAN;
    settings.fmt        = AUD_FMT_S16;
    settings.endianness = AUDIO_HOST_ENDIANNESS;

    audio_pcm_init_info (&hw->info, &settings);
    hw->samples = LINE_OUT_SAMPLES;
    out->active = 0;

    out->sin.base.sif = &playback_sif.base;
    qemu_spice_add_interface (&out->sin.base);
    return 0;
}

static void line_out_fini (HWVoiceOut *hw)
{
    SpiceVoiceOut *out = container_of (hw, SpiceVoiceOut, hw);

    spice_server_remove_interface (&out->sin.base);
}

static int line_out_run (HWVoiceOut *hw, int live)
{
    SpiceVoiceOut *out = container_of (hw, SpiceVoiceOut, hw);
    int rpos, decr;
    int samples;

    if (!live) {
        return 0;
    }

    decr = rate_get_samples (&hw->info, &out->rate);
    decr = audio_MIN (live, decr);

    samples = decr;
    rpos = hw->rpos;
    while (samples) {
        int left_till_end_samples = hw->samples - rpos;
        int len = audio_MIN (samples, left_till_end_samples);

        if (!out->frame) {
            spice_server_playback_get_buffer (&out->sin, &out->frame, &out->fsize);
            out->fpos = out->frame;
        }
        if (out->frame) {
            len = audio_MIN (len, out->fsize);
            hw->clip (out->fpos, hw->mix_buf + rpos, len);
            out->fsize -= len;
            out->fpos  += len;
            if (out->fsize == 0) {
                spice_server_playback_put_samples (&out->sin, out->frame);
                out->frame = out->fpos = NULL;
            }
        }
        rpos = (rpos + len) % hw->samples;
        samples -= len;
    }
    hw->rpos = rpos;
    return decr;
}

static int line_out_write (SWVoiceOut *sw, void *buf, int len)
{
    return audio_pcm_sw_write (sw, buf, len);
}

static int line_out_ctl (HWVoiceOut *hw, int cmd, ...)
{
    SpiceVoiceOut *out = container_of (hw, SpiceVoiceOut, hw);

    switch (cmd) {
    case VOICE_ENABLE:
        if (out->active) {
            break;
        }
        out->active = 1;
        rate_start (&out->rate);
        spice_server_playback_start (&out->sin);
        break;
    case VOICE_DISABLE:
        if (!out->active) {
            break;
        }
        out->active = 0;
        if (out->frame) {
            memset (out->fpos, 0, out->fsize << 2);
            spice_server_playback_put_samples (&out->sin, out->frame);
            out->frame = out->fpos = NULL;
        }
        spice_server_playback_stop (&out->sin);
        break;
    }
    return 0;
}

/* record */

static int line_in_init (HWVoiceIn *hw, struct audsettings *as)
{
    SpiceVoiceIn *in = container_of (hw, SpiceVoiceIn, hw);
    struct audsettings settings;

    settings.freq       = SPICE_INTERFACE_RECORD_FREQ;
    settings.nchannels  = SPICE_INTERFACE_RECORD_CHAN;
    settings.fmt        = AUD_FMT_S16;
    settings.endianness = AUDIO_HOST_ENDIANNESS;

    audio_pcm_init_info (&hw->info, &settings);
    hw->samples = LINE_IN_SAMPLES;
    in->active = 0;

    in->sin.base.sif = &record_sif.base;
    qemu_spice_add_interface (&in->sin.base);
    return 0;
}

static void line_in_fini (HWVoiceIn *hw)
{
    SpiceVoiceIn *in = container_of (hw, SpiceVoiceIn, hw);

    spice_server_remove_interface (&in->sin.base);
}

static int line_in_run (HWVoiceIn *hw)
{
    SpiceVoiceIn *in = container_of (hw, SpiceVoiceIn, hw);
    int num_samples;
    int ready;
    int len[2];
    uint64_t delta_samp;
    const uint32_t *samples;

    if (!(num_samples = hw->samples - audio_pcm_hw_get_live_in (hw))) {
        return 0;
    }

    delta_samp = rate_get_samples (&hw->info, &in->rate);
    num_samples = audio_MIN (num_samples, delta_samp);

    ready = spice_server_record_get_samples (&in->sin, in->samples, num_samples);
    samples = in->samples;
    if (ready == 0) {
        static const uint32_t silence[LINE_IN_SAMPLES];
        samples = silence;
        ready = LINE_IN_SAMPLES;
    }

    num_samples = audio_MIN (ready, num_samples);

    if (hw->wpos + num_samples > hw->samples) {
        len[0] = hw->samples - hw->wpos;
        len[1] = num_samples - len[0];
    } else {
        len[0] = num_samples;
        len[1] = 0;
    }

    hw->conv (hw->conv_buf + hw->wpos, samples, len[0]);

    if (len[1]) {
        hw->conv (hw->conv_buf, samples + len[0], len[1]);
    }

    hw->wpos = (hw->wpos + num_samples) % hw->samples;

    return num_samples;
}

static int line_in_read (SWVoiceIn *sw, void *buf, int size)
{
    return audio_pcm_sw_read (sw, buf, size);
}

static int line_in_ctl (HWVoiceIn *hw, int cmd, ...)
{
    SpiceVoiceIn *in = container_of (hw, SpiceVoiceIn, hw);

    switch (cmd) {
    case VOICE_ENABLE:
        if (in->active) {
            break;
        }
        in->active = 1;
        rate_start (&in->rate);
        spice_server_record_start (&in->sin);
        break;
    case VOICE_DISABLE:
        if (!in->active) {
            break;
        }
        in->active = 0;
        spice_server_record_stop (&in->sin);
        break;
    }
    return 0;
}

static struct audio_option audio_options[] = {
    { /* end of list */ },
};

static struct audio_pcm_ops audio_callbacks = {
    .init_out = line_out_init,
    .fini_out = line_out_fini,
    .run_out  = line_out_run,
    .write    = line_out_write,
    .ctl_out  = line_out_ctl,

    .init_in  = line_in_init,
    .fini_in  = line_in_fini,
    .run_in   = line_in_run,
    .read     = line_in_read,
    .ctl_in   = line_in_ctl,
};

struct audio_driver spice_audio_driver = {
    .name           = "spice",
    .descr          = "spice audio driver",
    .options        = audio_options,
    .init           = spice_audio_init,
    .fini           = spice_audio_fini,
    .pcm_ops        = &audio_callbacks,
    .max_voices_out = 1,
    .max_voices_in  = 1,
    .voice_size_out = sizeof (SpiceVoiceOut),
    .voice_size_in  = sizeof (SpiceVoiceIn),
};

void qemu_spice_audio_init (void)
{
    spice_audio_driver.can_be_default = 1;
}

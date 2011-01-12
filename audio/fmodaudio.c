/*
 * QEMU FMOD audio driver
 *
 * Copyright (c) 2004-2005 Vassili Karpov (malc)
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
#include <fmod.h>
#include <fmod_errors.h>
#include "qemu-common.h"
#include "audio.h"

#define AUDIO_CAP "fmod"
#include "audio_int.h"

typedef struct FMODVoiceOut {
    HWVoiceOut hw;
    unsigned int old_pos;
    FSOUND_SAMPLE *fmod_sample;
    int channel;
} FMODVoiceOut;

typedef struct FMODVoiceIn {
    HWVoiceIn hw;
    FSOUND_SAMPLE *fmod_sample;
} FMODVoiceIn;

static struct {
    const char *drvname;
    int nb_samples;
    int freq;
    int nb_channels;
    int bufsize;
    int broken_adc;
} conf = {
    .nb_samples  = 2048 * 2,
    .freq        = 44100,
    .nb_channels = 2,
};

static void GCC_FMT_ATTR (1, 2) fmod_logerr (const char *fmt, ...)
{
    va_list ap;

    va_start (ap, fmt);
    AUD_vlog (AUDIO_CAP, fmt, ap);
    va_end (ap);

    AUD_log (AUDIO_CAP, "Reason: %s\n",
             FMOD_ErrorString (FSOUND_GetError ()));
}

static void GCC_FMT_ATTR (2, 3) fmod_logerr2 (
    const char *typ,
    const char *fmt,
    ...
    )
{
    va_list ap;

    AUD_log (AUDIO_CAP, "Could not initialize %s\n", typ);

    va_start (ap, fmt);
    AUD_vlog (AUDIO_CAP, fmt, ap);
    va_end (ap);

    AUD_log (AUDIO_CAP, "Reason: %s\n",
             FMOD_ErrorString (FSOUND_GetError ()));
}

static int fmod_write (SWVoiceOut *sw, void *buf, int len)
{
    return audio_pcm_sw_write (sw, buf, len);
}

static void fmod_clear_sample (FMODVoiceOut *fmd)
{
    HWVoiceOut *hw = &fmd->hw;
    int status;
    void *p1 = 0, *p2 = 0;
    unsigned int len1 = 0, len2 = 0;

    status = FSOUND_Sample_Lock (
        fmd->fmod_sample,
        0,
        hw->samples << hw->info.shift,
        &p1,
        &p2,
        &len1,
        &len2
        );

    if (!status) {
        fmod_logerr ("Failed to lock sample\n");
        return;
    }

    if ((len1 & hw->info.align) || (len2 & hw->info.align)) {
        dolog ("Lock returned misaligned length %d, %d, alignment %d\n",
               len1, len2, hw->info.align + 1);
        goto fail;
    }

    if ((len1 + len2) - (hw->samples << hw->info.shift)) {
        dolog ("Lock returned incomplete length %d, %d\n",
               len1 + len2, hw->samples << hw->info.shift);
        goto fail;
    }

    audio_pcm_info_clear_buf (&hw->info, p1, hw->samples);

 fail:
    status = FSOUND_Sample_Unlock (fmd->fmod_sample, p1, p2, len1, len2);
    if (!status) {
        fmod_logerr ("Failed to unlock sample\n");
    }
}

static void fmod_write_sample (HWVoiceOut *hw, uint8_t *dst, int dst_len)
{
    int src_len1 = dst_len;
    int src_len2 = 0;
    int pos = hw->rpos + dst_len;
    struct st_sample *src1 = hw->mix_buf + hw->rpos;
    struct st_sample *src2 = NULL;

    if (pos > hw->samples) {
        src_len1 = hw->samples - hw->rpos;
        src2 = hw->mix_buf;
        src_len2 = dst_len - src_len1;
        pos = src_len2;
    }

    if (src_len1) {
        hw->clip (dst, src1, src_len1);
    }

    if (src_len2) {
        dst = advance (dst, src_len1 << hw->info.shift);
        hw->clip (dst, src2, src_len2);
    }

    hw->rpos = pos % hw->samples;
}

static int fmod_unlock_sample (FSOUND_SAMPLE *sample, void *p1, void *p2,
                               unsigned int blen1, unsigned int blen2)
{
    int status = FSOUND_Sample_Unlock (sample, p1, p2, blen1, blen2);
    if (!status) {
        fmod_logerr ("Failed to unlock sample\n");
        return -1;
    }
    return 0;
}

static int fmod_lock_sample (
    FSOUND_SAMPLE *sample,
    struct audio_pcm_info *info,
    int pos,
    int len,
    void **p1,
    void **p2,
    unsigned int *blen1,
    unsigned int *blen2
    )
{
    int status;

    status = FSOUND_Sample_Lock (
        sample,
        pos << info->shift,
        len << info->shift,
        p1,
        p2,
        blen1,
        blen2
        );

    if (!status) {
        fmod_logerr ("Failed to lock sample\n");
        return -1;
    }

    if ((*blen1 & info->align) || (*blen2 & info->align)) {
        dolog ("Lock returned misaligned length %d, %d, alignment %d\n",
               *blen1, *blen2, info->align + 1);

        fmod_unlock_sample (sample, *p1, *p2, *blen1, *blen2);

        *p1 = NULL - 1;
        *p2 = NULL - 1;
        *blen1 = ~0U;
        *blen2 = ~0U;
        return -1;
    }

    if (!*p1 && *blen1) {
        dolog ("warning: !p1 && blen1=%d\n", *blen1);
        *blen1 = 0;
    }

    if (!p2 && *blen2) {
        dolog ("warning: !p2 && blen2=%d\n", *blen2);
        *blen2 = 0;
    }

    return 0;
}

static int fmod_run_out (HWVoiceOut *hw, int live)
{
    FMODVoiceOut *fmd = (FMODVoiceOut *) hw;
    int decr;
    void *p1 = 0, *p2 = 0;
    unsigned int blen1 = 0, blen2 = 0;
    unsigned int len1 = 0, len2 = 0;

    if (!hw->pending_disable) {
        return 0;
    }

    decr = live;

    if (fmd->channel >= 0) {
        int len = decr;
        int old_pos = fmd->old_pos;
        int ppos = FSOUND_GetCurrentPosition (fmd->channel);

        if (ppos == old_pos || !ppos) {
            return 0;
        }

        if ((old_pos < ppos) && ((old_pos + len) > ppos)) {
            len = ppos - old_pos;
        }
        else {
            if ((old_pos > ppos) && ((old_pos + len) > (ppos + hw->samples))) {
                len = hw->samples - old_pos + ppos;
            }
        }
        decr = len;

        if (audio_bug (AUDIO_FUNC, decr < 0)) {
            dolog ("decr=%d live=%d ppos=%d old_pos=%d len=%d\n",
                   decr, live, ppos, old_pos, len);
            return 0;
        }
    }


    if (!decr) {
        return 0;
    }

    if (fmod_lock_sample (fmd->fmod_sample, &fmd->hw.info,
                          fmd->old_pos, decr,
                          &p1, &p2,
                          &blen1, &blen2)) {
        return 0;
    }

    len1 = blen1 >> hw->info.shift;
    len2 = blen2 >> hw->info.shift;
    ldebug ("%p %p %d %d %d %d\n", p1, p2, len1, len2, blen1, blen2);
    decr = len1 + len2;

    if (p1 && len1) {
        fmod_write_sample (hw, p1, len1);
    }

    if (p2 && len2) {
        fmod_write_sample (hw, p2, len2);
    }

    fmod_unlock_sample (fmd->fmod_sample, p1, p2, blen1, blen2);

    fmd->old_pos = (fmd->old_pos + decr) % hw->samples;
    return decr;
}

static int aud_to_fmodfmt (audfmt_e fmt, int stereo)
{
    int mode = FSOUND_LOOP_NORMAL;

    switch (fmt) {
    case AUD_FMT_S8:
        mode |= FSOUND_SIGNED | FSOUND_8BITS;
        break;

    case AUD_FMT_U8:
        mode |= FSOUND_UNSIGNED | FSOUND_8BITS;
        break;

    case AUD_FMT_S16:
        mode |= FSOUND_SIGNED | FSOUND_16BITS;
        break;

    case AUD_FMT_U16:
        mode |= FSOUND_UNSIGNED | FSOUND_16BITS;
        break;

    default:
        dolog ("Internal logic error: Bad audio format %d\n", fmt);
#ifdef DEBUG_FMOD
        abort ();
#endif
        mode |= FSOUND_8BITS;
    }
    mode |= stereo ? FSOUND_STEREO : FSOUND_MONO;
    return mode;
}

static void fmod_fini_out (HWVoiceOut *hw)
{
    FMODVoiceOut *fmd = (FMODVoiceOut *) hw;

    if (fmd->fmod_sample) {
        FSOUND_Sample_Free (fmd->fmod_sample);
        fmd->fmod_sample = 0;

        if (fmd->channel >= 0) {
            FSOUND_StopSound (fmd->channel);
        }
    }
}

static int fmod_init_out (HWVoiceOut *hw, struct audsettings *as)
{
    int bits16, mode, channel;
    FMODVoiceOut *fmd = (FMODVoiceOut *) hw;
    struct audsettings obt_as = *as;

    mode = aud_to_fmodfmt (as->fmt, as->nchannels == 2 ? 1 : 0);
    fmd->fmod_sample = FSOUND_Sample_Alloc (
        FSOUND_FREE,            /* index */
        conf.nb_samples,        /* length */
        mode,                   /* mode */
        as->freq,               /* freq */
        255,                    /* volume */
        128,                    /* pan */
        255                     /* priority */
        );

    if (!fmd->fmod_sample) {
        fmod_logerr2 ("DAC", "Failed to allocate FMOD sample\n");
        return -1;
    }

    channel = FSOUND_PlaySoundEx (FSOUND_FREE, fmd->fmod_sample, 0, 1);
    if (channel < 0) {
        fmod_logerr2 ("DAC", "Failed to start playing sound\n");
        FSOUND_Sample_Free (fmd->fmod_sample);
        return -1;
    }
    fmd->channel = channel;

    /* FMOD always operates on little endian frames? */
    obt_as.endianness = 0;
    audio_pcm_init_info (&hw->info, &obt_as);
    bits16 = (mode & FSOUND_16BITS) != 0;
    hw->samples = conf.nb_samples;
    return 0;
}

static int fmod_ctl_out (HWVoiceOut *hw, int cmd, ...)
{
    int status;
    FMODVoiceOut *fmd = (FMODVoiceOut *) hw;

    switch (cmd) {
    case VOICE_ENABLE:
        fmod_clear_sample (fmd);
        status = FSOUND_SetPaused (fmd->channel, 0);
        if (!status) {
            fmod_logerr ("Failed to resume channel %d\n", fmd->channel);
        }
        break;

    case VOICE_DISABLE:
        status = FSOUND_SetPaused (fmd->channel, 1);
        if (!status) {
            fmod_logerr ("Failed to pause channel %d\n", fmd->channel);
        }
        break;
    }
    return 0;
}

static int fmod_init_in (HWVoiceIn *hw, struct audsettings *as)
{
    int bits16, mode;
    FMODVoiceIn *fmd = (FMODVoiceIn *) hw;
    struct audsettings obt_as = *as;

    if (conf.broken_adc) {
        return -1;
    }

    mode = aud_to_fmodfmt (as->fmt, as->nchannels == 2 ? 1 : 0);
    fmd->fmod_sample = FSOUND_Sample_Alloc (
        FSOUND_FREE,            /* index */
        conf.nb_samples,        /* length */
        mode,                   /* mode */
        as->freq,               /* freq */
        255,                    /* volume */
        128,                    /* pan */
        255                     /* priority */
        );

    if (!fmd->fmod_sample) {
        fmod_logerr2 ("ADC", "Failed to allocate FMOD sample\n");
        return -1;
    }

    /* FMOD always operates on little endian frames? */
    obt_as.endianness = 0;
    audio_pcm_init_info (&hw->info, &obt_as);
    bits16 = (mode & FSOUND_16BITS) != 0;
    hw->samples = conf.nb_samples;
    return 0;
}

static void fmod_fini_in (HWVoiceIn *hw)
{
    FMODVoiceIn *fmd = (FMODVoiceIn *) hw;

    if (fmd->fmod_sample) {
        FSOUND_Record_Stop ();
        FSOUND_Sample_Free (fmd->fmod_sample);
        fmd->fmod_sample = 0;
    }
}

static int fmod_run_in (HWVoiceIn *hw)
{
    FMODVoiceIn *fmd = (FMODVoiceIn *) hw;
    int hwshift = hw->info.shift;
    int live, dead, new_pos, len;
    unsigned int blen1 = 0, blen2 = 0;
    unsigned int len1, len2;
    unsigned int decr;
    void *p1, *p2;

    live = audio_pcm_hw_get_live_in (hw);
    dead = hw->samples - live;
    if (!dead) {
        return 0;
    }

    new_pos = FSOUND_Record_GetPosition ();
    if (new_pos < 0) {
        fmod_logerr ("Could not get recording position\n");
        return 0;
    }

    len = audio_ring_dist (new_pos,  hw->wpos, hw->samples);
    if (!len) {
        return 0;
    }
    len = audio_MIN (len, dead);

    if (fmod_lock_sample (fmd->fmod_sample, &fmd->hw.info,
                          hw->wpos, len,
                          &p1, &p2,
                          &blen1, &blen2)) {
        return 0;
    }

    len1 = blen1 >> hwshift;
    len2 = blen2 >> hwshift;
    decr = len1 + len2;

    if (p1 && blen1) {
        hw->conv (hw->conv_buf + hw->wpos, p1, len1);
    }
    if (p2 && len2) {
        hw->conv (hw->conv_buf, p2, len2);
    }

    fmod_unlock_sample (fmd->fmod_sample, p1, p2, blen1, blen2);
    hw->wpos = (hw->wpos + decr) % hw->samples;
    return decr;
}

static struct {
    const char *name;
    int type;
} drvtab[] = {
    { .name = "none",   .type = FSOUND_OUTPUT_NOSOUND },
#ifdef _WIN32
    { .name = "winmm",  .type = FSOUND_OUTPUT_WINMM   },
    { .name = "dsound", .type = FSOUND_OUTPUT_DSOUND  },
    { .name = "a3d",    .type = FSOUND_OUTPUT_A3D     },
    { .name = "asio",   .type = FSOUND_OUTPUT_ASIO    },
#endif
#ifdef __linux__
    { .name = "oss",    .type = FSOUND_OUTPUT_OSS     },
    { .name = "alsa",   .type = FSOUND_OUTPUT_ALSA    },
    { .name = "esd",    .type = FSOUND_OUTPUT_ESD     },
#endif
#ifdef __APPLE__
    { .name = "mac",    .type = FSOUND_OUTPUT_MAC     },
#endif
#if 0
    { .name = "xbox",   .type = FSOUND_OUTPUT_XBOX    },
    { .name = "ps2",    .type = FSOUND_OUTPUT_PS2     },
    { .name = "gcube",  .type = FSOUND_OUTPUT_GC      },
#endif
    { .name = "none-realtime", .type = FSOUND_OUTPUT_NOSOUND_NONREALTIME }
};

static void *fmod_audio_init (void)
{
    size_t i;
    double ver;
    int status;
    int output_type = -1;
    const char *drv = conf.drvname;

    ver = FSOUND_GetVersion ();
    if (ver < FMOD_VERSION) {
        dolog ("Wrong FMOD version %f, need at least %f\n", ver, FMOD_VERSION);
        return NULL;
    }

#ifdef __linux__
    if (ver < 3.75) {
        dolog ("FMOD before 3.75 has bug preventing ADC from working\n"
               "ADC will be disabled.\n");
        conf.broken_adc = 1;
    }
#endif

    if (drv) {
        int found = 0;
        for (i = 0; i < ARRAY_SIZE (drvtab); i++) {
            if (!strcmp (drv, drvtab[i].name)) {
                output_type = drvtab[i].type;
                found = 1;
                break;
            }
        }
        if (!found) {
            dolog ("Unknown FMOD driver `%s'\n", drv);
            dolog ("Valid drivers:\n");
            for (i = 0; i < ARRAY_SIZE (drvtab); i++) {
                dolog ("  %s\n", drvtab[i].name);
            }
        }
    }

    if (output_type != -1) {
        status = FSOUND_SetOutput (output_type);
        if (!status) {
            fmod_logerr ("FSOUND_SetOutput(%d) failed\n", output_type);
            return NULL;
        }
    }

    if (conf.bufsize) {
        status = FSOUND_SetBufferSize (conf.bufsize);
        if (!status) {
            fmod_logerr ("FSOUND_SetBufferSize (%d) failed\n", conf.bufsize);
        }
    }

    status = FSOUND_Init (conf.freq, conf.nb_channels, 0);
    if (!status) {
        fmod_logerr ("FSOUND_Init failed\n");
        return NULL;
    }

    return &conf;
}

static int fmod_read (SWVoiceIn *sw, void *buf, int size)
{
    return audio_pcm_sw_read (sw, buf, size);
}

static int fmod_ctl_in (HWVoiceIn *hw, int cmd, ...)
{
    int status;
    FMODVoiceIn *fmd = (FMODVoiceIn *) hw;

    switch (cmd) {
    case VOICE_ENABLE:
        status = FSOUND_Record_StartSample (fmd->fmod_sample, 1);
        if (!status) {
            fmod_logerr ("Failed to start recording\n");
        }
        break;

    case VOICE_DISABLE:
        status = FSOUND_Record_Stop ();
        if (!status) {
            fmod_logerr ("Failed to stop recording\n");
        }
        break;
    }
    return 0;
}

static void fmod_audio_fini (void *opaque)
{
    (void) opaque;
    FSOUND_Close ();
}

static struct audio_option fmod_options[] = {
    {
        .name  = "DRV",
        .tag   = AUD_OPT_STR,
        .valp  = &conf.drvname,
        .descr = "FMOD driver"
    },
    {
        .name  = "FREQ",
        .tag   = AUD_OPT_INT,
        .valp  = &conf.freq,
        .descr = "Default frequency"
    },
    {
        .name  = "SAMPLES",
        .tag   = AUD_OPT_INT,
        .valp  = &conf.nb_samples,
        .descr = "Buffer size in samples"
    },
    {
        .name  = "CHANNELS",
        .tag   = AUD_OPT_INT,
        .valp  = &conf.nb_channels,
        .descr = "Number of default channels (1 - mono, 2 - stereo)"
    },
    {
        .name  = "BUFSIZE",
        .tag   = AUD_OPT_INT,
        .valp  = &conf.bufsize,
        .descr = "(undocumented)"
    },
    { /* End of list */ }
};

static struct audio_pcm_ops fmod_pcm_ops = {
    .init_out = fmod_init_out,
    .fini_out = fmod_fini_out,
    .run_out  = fmod_run_out,
    .write    = fmod_write,
    .ctl_out  = fmod_ctl_out,

    .init_in  = fmod_init_in,
    .fini_in  = fmod_fini_in,
    .run_in   = fmod_run_in,
    .read     = fmod_read,
    .ctl_in   = fmod_ctl_in
};

struct audio_driver fmod_audio_driver = {
    .name           = "fmod",
    .descr          = "FMOD 3.xx http://www.fmod.org",
    .options        = fmod_options,
    .init           = fmod_audio_init,
    .fini           = fmod_audio_fini,
    .pcm_ops        = &fmod_pcm_ops,
    .can_be_default = 1,
    .max_voices_out = INT_MAX,
    .max_voices_in  = INT_MAX,
    .voice_size_out = sizeof (FMODVoiceOut),
    .voice_size_in  = sizeof (FMODVoiceIn)
};

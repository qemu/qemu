/*
 * QEMU FMOD audio output driver
 * 
 * Copyright (c) 2004 Vassili Karpov (malc)
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
#include "vl.h"

#define AUDIO_CAP "fmod"
#include "audio/audio.h"
#include "audio/fmodaudio.h"

#define QC_FMOD_DRV "QEMU_FMOD_DRV"
#define QC_FMOD_FREQ "QEMU_FMOD_FREQ"
#define QC_FMOD_SAMPLES "QEMU_FMOD_SAMPLES"
#define QC_FMOD_CHANNELS "QEMU_FMOD_CHANNELS"
#define QC_FMOD_BUFSIZE "QEMU_FMOD_BUFSIZE"
#define QC_FMOD_THRESHOLD "QEMU_FMOD_THRESHOLD"

static struct {
    int nb_samples;
    int freq;
    int nb_channels;
    int bufsize;
    int threshold;
} conf = {
    2048,
    44100,
    1,
    0,
    128
};

#define errstr() FMOD_ErrorString (FSOUND_GetError ())

static int fmod_hw_write (SWVoice *sw, void *buf, int len)
{
    return pcm_hw_write (sw, buf, len);
}

static void fmod_clear_sample (FMODVoice *fmd)
{
    HWVoice *hw = &fmd->hw;
    int status;
    void *p1 = 0, *p2 = 0;
    unsigned int len1 = 0, len2 = 0;

    status = FSOUND_Sample_Lock (
        fmd->fmod_sample,
        0,
        hw->samples << hw->shift,
        &p1,
        &p2,
        &len1,
        &len2
        );

    if (!status) {
        dolog ("Failed to lock sample\nReason: %s\n", errstr ());
        return;
    }

    if ((len1 & hw->align) || (len2 & hw->align)) {
        dolog ("Locking sample returned unaligned length %d, %d\n",
               len1, len2);
        goto fail;
    }

    if (len1 + len2 != hw->samples << hw->shift) {
        dolog ("Locking sample returned incomplete length %d, %d\n",
               len1 + len2, hw->samples << hw->shift);
        goto fail;
    }
    pcm_hw_clear (hw, p1, hw->samples);

 fail:
    status = FSOUND_Sample_Unlock (fmd->fmod_sample, p1, p2, len1, len2);
    if (!status) {
        dolog ("Failed to unlock sample\nReason: %s\n", errstr ());
    }
}

static int fmod_write_sample (HWVoice *hw, uint8_t *dst, st_sample_t *src,
                              int src_size, int src_pos, int dst_len)
{
    int src_len1 = dst_len, src_len2 = 0, pos = src_pos + dst_len;
    st_sample_t *src1 = src + src_pos, *src2 = 0;

    if (src_pos + dst_len > src_size) {
        src_len1 = src_size - src_pos;
        src2 = src;
        src_len2 = dst_len - src_len1;
        pos = src_len2;
    }

    if (src_len1) {
        hw->clip (dst, src1, src_len1);
        memset (src1, 0, src_len1 * sizeof (st_sample_t));
        advance (dst, src_len1);
    }

    if (src_len2) {
        hw->clip (dst, src2, src_len2);
        memset (src2, 0, src_len2 * sizeof (st_sample_t));
    }
    return pos;
}

static int fmod_unlock_sample (FMODVoice *fmd, void *p1, void *p2,
                               unsigned int blen1, unsigned int blen2)
{
    int status = FSOUND_Sample_Unlock (fmd->fmod_sample, p1, p2, blen1, blen2);
    if (!status) {
        dolog ("Failed to unlock sample\nReason: %s\n", errstr ());
        return -1;
    }
    return 0;
}

static int fmod_lock_sample (FMODVoice *fmd, int pos, int len,
                             void **p1, void **p2,
                             unsigned int *blen1, unsigned int *blen2)
{
    HWVoice *hw = &fmd->hw;
    int status;

    status = FSOUND_Sample_Lock (
        fmd->fmod_sample,
        pos << hw->shift,
        len << hw->shift,
        p1,
        p2,
        blen1,
        blen2
        );

    if (!status) {
        dolog ("Failed to lock sample\nReason: %s\n", errstr ());
        return -1;
    }

    if ((*blen1 & hw->align) || (*blen2 & hw->align)) {
        dolog ("Locking sample returned unaligned length %d, %d\n",
               *blen1, *blen2);
        fmod_unlock_sample (fmd, *p1, *p2, *blen1, *blen2);
        return -1;
    }
    return 0;
}

static void fmod_hw_run (HWVoice *hw)
{
    FMODVoice *fmd = (FMODVoice *) hw;
    int rpos, live, decr;
    void *p1 = 0, *p2 = 0;
    unsigned int blen1 = 0, blen2 = 0;
    unsigned int len1 = 0, len2 = 0;
    int nb_active;

    live = pcm_hw_get_live2 (hw, &nb_active);
    if (live <= 0) {
        return;
    }

    if (!hw->pending_disable
        && nb_active
        && conf.threshold
        && live <= conf.threshold) {
        ldebug ("live=%d nb_active=%d\n", live, nb_active);
        return;
    }

    decr = live;

#if 1
    if (fmd->channel >= 0) {
        int pos2 = (fmd->old_pos + decr) % hw->samples;
        int pos = FSOUND_GetCurrentPosition (fmd->channel);

        if (fmd->old_pos < pos && pos2 >= pos) {
            decr = pos - fmd->old_pos - (pos2 == pos) - 1;
        }
        else if (fmd->old_pos > pos && pos2 >= pos && pos2 < fmd->old_pos) {
            decr = (hw->samples - fmd->old_pos) + pos - (pos2 == pos) - 1;
        }
/*         ldebug ("pos=%d pos2=%d old=%d live=%d decr=%d\n", */
/*                 pos, pos2, fmd->old_pos, live, decr); */
    }
#endif

    if (decr <= 0) {
        return;
    }

    if (fmod_lock_sample (fmd, fmd->old_pos, decr, &p1, &p2, &blen1, &blen2)) {
        return;
    }

    len1 = blen1 >> hw->shift;
    len2 = blen2 >> hw->shift;
    ldebug ("%p %p %d %d %d %d\n", p1, p2, len1, len2, blen1, blen2);
    decr = len1 + len2;
    rpos = hw->rpos;

    if (len1) {
        rpos = fmod_write_sample (hw, p1, hw->mix_buf, hw->samples, rpos, len1);
    }

    if (len2) {
        rpos = fmod_write_sample (hw, p2, hw->mix_buf, hw->samples, rpos, len2);
    }

    fmod_unlock_sample (fmd, p1, p2, blen1, blen2);

    pcm_hw_dec_live (hw, decr);
    hw->rpos = rpos % hw->samples;
    fmd->old_pos = (fmd->old_pos + decr) % hw->samples;
}

static int AUD_to_fmodfmt (audfmt_e fmt, int stereo)
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
        dolog ("Internal logic error: Bad audio format %d\nAborting\n", fmt);
        exit (EXIT_FAILURE);
    }
    mode |= stereo ? FSOUND_STEREO : FSOUND_MONO;
    return mode;
}

static void fmod_hw_fini (HWVoice *hw)
{
    FMODVoice *fmd = (FMODVoice *) hw;

    if (fmd->fmod_sample) {
        FSOUND_Sample_Free (fmd->fmod_sample);
        fmd->fmod_sample = 0;

        if (fmd->channel >= 0) {
            FSOUND_StopSound (fmd->channel);
        }
    }
}

static int fmod_hw_init (HWVoice *hw, int freq, int nchannels, audfmt_e fmt)
{
    int bits16, mode, channel;
    FMODVoice *fmd = (FMODVoice *) hw;

    mode = AUD_to_fmodfmt (fmt, nchannels == 2 ? 1 : 0);
    fmd->fmod_sample = FSOUND_Sample_Alloc (
        FSOUND_FREE,            /* index */
        conf.nb_samples,        /* length */
        mode,                   /* mode */
        freq,                   /* freq */
        255,                    /* volume */
        128,                    /* pan */
        255                     /* priority */
        );

    if (!fmd->fmod_sample) {
        dolog ("Failed to allocate FMOD sample\nReason: %s\n", errstr ());
        return -1;
    }

    channel = FSOUND_PlaySoundEx (FSOUND_FREE, fmd->fmod_sample, 0, 1);
    if (channel < 0) {
        dolog ("Failed to start playing sound\nReason: %s\n", errstr ());
        FSOUND_Sample_Free (fmd->fmod_sample);
        return -1;
    }
    fmd->channel = channel;

    hw->freq = freq;
    hw->fmt = fmt;
    hw->nchannels = nchannels;
    bits16 = fmt == AUD_FMT_U16 || fmt == AUD_FMT_S16;
    hw->bufsize = conf.nb_samples << (nchannels == 2) << bits16;
    return 0;
}

static int fmod_hw_ctl (HWVoice *hw, int cmd, ...)
{
    int status;
    FMODVoice *fmd = (FMODVoice *) hw;

    switch (cmd) {
    case VOICE_ENABLE:
        fmod_clear_sample (fmd);
        status = FSOUND_SetPaused (fmd->channel, 0);
        if (!status) {
            dolog ("Failed to resume channel %d\nReason: %s\n",
                   fmd->channel, errstr ());
        }
        break;

    case VOICE_DISABLE:
        status = FSOUND_SetPaused (fmd->channel, 1);
        if (!status) {
            dolog ("Failed to pause channel %d\nReason: %s\n",
                   fmd->channel, errstr ());
        }
        break;
    }
    return 0;
}

static struct {
    const char *name;
    int type;
} drvtab[] = {
    {"none", FSOUND_OUTPUT_NOSOUND},
#ifdef _WIN32
    {"winmm", FSOUND_OUTPUT_WINMM},
    {"dsound", FSOUND_OUTPUT_DSOUND},
    {"a3d", FSOUND_OUTPUT_A3D},
    {"asio", FSOUND_OUTPUT_ASIO},
#endif
#ifdef __linux__
    {"oss", FSOUND_OUTPUT_OSS},
    {"alsa", FSOUND_OUTPUT_ALSA},
    {"esd", FSOUND_OUTPUT_ESD},
#endif
#ifdef __APPLE__
    {"mac", FSOUND_OUTPUT_MAC},
#endif
#if 0
    {"xbox", FSOUND_OUTPUT_XBOX},
    {"ps2", FSOUND_OUTPUT_PS2},
    {"gcube", FSOUND_OUTPUT_GC},
#endif
    {"nort", FSOUND_OUTPUT_NOSOUND_NONREALTIME}
};

static void *fmod_audio_init (void)
{
    int i;
    double ver;
    int status;
    int output_type = -1;
    const char *drv = audio_get_conf_str (QC_FMOD_DRV, NULL);

    ver = FSOUND_GetVersion ();
    if (ver < FMOD_VERSION) {
        dolog ("Wrong FMOD version %f, need at least %f\n", ver, FMOD_VERSION);
        return NULL;
    }

    if (drv) {
        int found = 0;
        for (i = 0; i < sizeof (drvtab) / sizeof (drvtab[0]); i++) {
            if (!strcmp (drv, drvtab[i].name)) {
                output_type = drvtab[i].type;
                found = 1;
                break;
            }
        }
        if (!found) {
            dolog ("Unknown FMOD output driver `%s'\n", drv);
        }
    }

    if (output_type != -1) {
        status = FSOUND_SetOutput (output_type);
        if (!status) {
            dolog ("FSOUND_SetOutput(%d) failed\nReason: %s\n",
                   output_type, errstr ());
            return NULL;
        }
    }

    conf.freq = audio_get_conf_int (QC_FMOD_FREQ, conf.freq);
    conf.nb_samples = audio_get_conf_int (QC_FMOD_SAMPLES, conf.nb_samples);
    conf.nb_channels =
        audio_get_conf_int (QC_FMOD_CHANNELS,
                            (audio_state.nb_hw_voices > 1
                             ? audio_state.nb_hw_voices
                             : conf.nb_channels));
    conf.bufsize = audio_get_conf_int (QC_FMOD_BUFSIZE, conf.bufsize);
    conf.threshold = audio_get_conf_int (QC_FMOD_THRESHOLD, conf.threshold);

    if (conf.bufsize) {
        status = FSOUND_SetBufferSize (conf.bufsize);
        if (!status) {
            dolog ("FSOUND_SetBufferSize (%d) failed\nReason: %s\n",
                   conf.bufsize, errstr ());
        }
    }

    status = FSOUND_Init (conf.freq, conf.nb_channels, 0);
    if (!status) {
        dolog ("FSOUND_Init failed\nReason: %s\n", errstr ());
        return NULL;
    }

    return &conf;
}

static void fmod_audio_fini (void *opaque)
{
    FSOUND_Close ();
}

struct pcm_ops fmod_pcm_ops = {
    fmod_hw_init,
    fmod_hw_fini,
    fmod_hw_run,
    fmod_hw_write,
    fmod_hw_ctl
};

struct audio_output_driver fmod_output_driver = {
    "fmod",
    fmod_audio_init,
    fmod_audio_fini,
    &fmod_pcm_ops,
    1,
    INT_MAX,
    sizeof (FMODVoice)
};

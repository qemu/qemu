/*
 * QEMU WAV audio driver
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
#include "vl.h"

#define AUDIO_CAP "wav"
#include "audio_int.h"

typedef struct WAVVoiceOut {
    HWVoiceOut hw;
    QEMUFile *f;
    int64_t old_ticks;
    void *pcm_buf;
    int total_samples;
} WAVVoiceOut;

static struct {
    const char *wav_path;
} conf = {
    .wav_path = "qemu.wav"
};

static int wav_run_out (HWVoiceOut *hw)
{
    WAVVoiceOut *wav = (WAVVoiceOut *) hw;
    int rpos, live, decr, samples;
    uint8_t *dst;
    st_sample_t *src;
    int64_t now = qemu_get_clock (vm_clock);
    int64_t ticks = now - wav->old_ticks;
    int64_t bytes = (ticks * hw->info.bytes_per_second) / ticks_per_sec;

    if (bytes > INT_MAX) {
        samples = INT_MAX >> hw->info.shift;
    }
    else {
        samples = bytes >> hw->info.shift;
    }

    live = audio_pcm_hw_get_live_out (hw);
    if (!live) {
        return 0;
    }

    wav->old_ticks = now;
    decr = audio_MIN (live, samples);
    samples = decr;
    rpos = hw->rpos;
    while (samples) {
        int left_till_end_samples = hw->samples - rpos;
        int convert_samples = audio_MIN (samples, left_till_end_samples);

        src = hw->mix_buf + rpos;
        dst = advance (wav->pcm_buf, rpos << hw->info.shift);

        hw->clip (dst, src, convert_samples);
        qemu_put_buffer (wav->f, dst, convert_samples << hw->info.shift);
        mixeng_clear (src, convert_samples);

        rpos = (rpos + convert_samples) % hw->samples;
        samples -= convert_samples;
        wav->total_samples += convert_samples;
    }

    hw->rpos = rpos;
    return decr;
}

static int wav_write_out (SWVoiceOut *sw, void *buf, int len)
{
    return audio_pcm_sw_write (sw, buf, len);
}

/* VICE code: Store number as little endian. */
static void le_store (uint8_t *buf, uint32_t val, int len)
{
    int i;
    for (i = 0; i < len; i++) {
        buf[i] = (uint8_t) (val & 0xff);
        val >>= 8;
    }
}

static int wav_init_out (HWVoiceOut *hw, int freq, int nchannels, audfmt_e fmt)
{
    WAVVoiceOut *wav = (WAVVoiceOut *) hw;
    int bits16;
    uint8_t hdr[] = {
        0x52, 0x49, 0x46, 0x46, 0x00, 0x00, 0x00, 0x00, 0x57, 0x41, 0x56,
        0x45, 0x66, 0x6d, 0x74, 0x20, 0x10, 0x00, 0x00, 0x00, 0x01, 0x00,
        0x02, 0x00, 0x44, 0xac, 0x00, 0x00, 0x10, 0xb1, 0x02, 0x00, 0x04,
        0x00, 0x10, 0x00, 0x64, 0x61, 0x74, 0x61, 0x00, 0x00, 0x00, 0x00
    };

    freq = audio_state.fixed_freq_out;
    fmt = audio_state.fixed_fmt_out;
    nchannels = audio_state.fixed_channels_out;

    switch (fmt) {
    case AUD_FMT_S8:
    case AUD_FMT_U8:
        bits16 = 0;
        break;

    case AUD_FMT_S16:
    case AUD_FMT_U16:
        bits16 = 1;
        break;

    default:
        dolog ("Internal logic error bad format %d\n", fmt);
        return -1;
    }

    hdr[34] = bits16 ? 0x10 : 0x08;
    audio_pcm_init_info (
        &hw->info,
        freq,
        nchannels,
        bits16 ? AUD_FMT_S16 : AUD_FMT_U8,
        audio_need_to_swap_endian (0)
        );
    hw->bufsize = 4096;
    wav->pcm_buf = qemu_mallocz (hw->bufsize);
    if (!wav->pcm_buf) {
        dolog ("Can not initialize WAV buffer of %d bytes\n",
               hw->bufsize);
        return -1;
    }

    le_store (hdr + 22, hw->info.nchannels, 2);
    le_store (hdr + 24, hw->info.freq, 4);
    le_store (hdr + 28, hw->info.freq << (bits16 + (nchannels == 2)), 4);
    le_store (hdr + 32, 1 << (bits16 + (nchannels == 2)), 2);

    wav->f = fopen (conf.wav_path, "wb");
    if (!wav->f) {
        dolog ("Failed to open wave file `%s'\nReason: %s\n",
               conf.wav_path, strerror (errno));
        qemu_free (wav->pcm_buf);
        wav->pcm_buf = NULL;
        return -1;
    }

    qemu_put_buffer (wav->f, hdr, sizeof (hdr));
    return 0;
}

static void wav_fini_out (HWVoiceOut *hw)
{
    WAVVoiceOut *wav = (WAVVoiceOut *) hw;
    int stereo = hw->info.nchannels == 2;
    uint8_t rlen[4];
    uint8_t dlen[4];
    uint32_t rifflen = (wav->total_samples << stereo) + 36;
    uint32_t datalen = wav->total_samples << stereo;

    if (!wav->f || !hw->active) {
        return;
    }

    le_store (rlen, rifflen, 4);
    le_store (dlen, datalen, 4);

    qemu_fseek (wav->f, 4, SEEK_SET);
    qemu_put_buffer (wav->f, rlen, 4);

    qemu_fseek (wav->f, 32, SEEK_CUR);
    qemu_put_buffer (wav->f, dlen, 4);

    fclose (wav->f);
    wav->f = NULL;

    qemu_free (wav->pcm_buf);
    wav->pcm_buf = NULL;
}

static int wav_ctl_out (HWVoiceOut *hw, int cmd, ...)
{
    (void) hw;
    (void) cmd;
    return 0;
}

static void *wav_audio_init (void)
{
    return &conf;
}

static void wav_audio_fini (void *opaque)
{
    (void) opaque;
    ldebug ("wav_fini");
}

struct audio_option wav_options[] = {
    {"PATH", AUD_OPT_STR, &conf.wav_path,
     "Path to wave file", NULL, 0},
    {NULL, 0, NULL, NULL, NULL, 0}
};

struct audio_pcm_ops wav_pcm_ops = {
    wav_init_out,
    wav_fini_out,
    wav_run_out,
    wav_write_out,
    wav_ctl_out,

    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

struct audio_driver wav_audio_driver = {
    INIT_FIELD (name           = ) "wav",
    INIT_FIELD (descr          = )
    "WAV renderer http://wikipedia.org/wiki/WAV",
    INIT_FIELD (options        = ) wav_options,
    INIT_FIELD (init           = ) wav_audio_init,
    INIT_FIELD (fini           = ) wav_audio_fini,
    INIT_FIELD (pcm_ops        = ) &wav_pcm_ops,
    INIT_FIELD (can_be_default = ) 0,
    INIT_FIELD (max_voices_out = ) 1,
    INIT_FIELD (max_voices_in  = ) 0,
    INIT_FIELD (voice_size_out = ) sizeof (WAVVoiceOut),
    INIT_FIELD (voice_size_in  = ) 0
};

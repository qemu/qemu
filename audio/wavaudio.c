/*
 * QEMU WAV audio output driver
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
#include "vl.h"

#include "audio/audio_int.h"

typedef struct WAVVoice {
    HWVoice hw;
    QEMUFile *f;
    int64_t old_ticks;
    void *pcm_buf;
    int total_samples;
} WAVVoice;

#define dolog(...) AUD_log ("wav", __VA_ARGS__)
#ifdef DEBUG
#define ldebug(...) dolog (__VA_ARGS__)
#else
#define ldebug(...)
#endif

static struct {
    const char *wav_path;
} conf = {
    .wav_path = "qemu.wav"
};

static void wav_hw_run (HWVoice *hw)
{
    WAVVoice *wav = (WAVVoice *) hw;
    int rpos, live, decr, samples;
    uint8_t *dst;
    st_sample_t *src;
    int64_t now = qemu_get_clock (vm_clock);
    int64_t ticks = now - wav->old_ticks;
    int64_t bytes = (ticks * hw->bytes_per_second) / ticks_per_sec;

    if (bytes > INT_MAX)
        samples = INT_MAX >> hw->shift;
    else
        samples = bytes >> hw->shift;

    live = pcm_hw_get_live (hw);
    if (live <= 0)
        return;

    wav->old_ticks = now;
    decr = audio_MIN (live, samples);
    samples = decr;
    rpos = hw->rpos;
    while (samples) {
        int left_till_end_samples = hw->samples - rpos;
        int convert_samples = audio_MIN (samples, left_till_end_samples);

        src = advance (hw->mix_buf, rpos * sizeof (st_sample_t));
        dst = advance (wav->pcm_buf, rpos << hw->shift);

        hw->clip (dst, src, convert_samples);
        qemu_put_buffer (wav->f, dst, convert_samples << hw->shift);
        memset (src, 0, convert_samples * sizeof (st_sample_t));

        rpos = (rpos + convert_samples) % hw->samples;
        samples -= convert_samples;
        wav->total_samples += convert_samples;
    }

    pcm_hw_dec_live (hw, decr);
    hw->rpos = rpos;
}

static int wav_hw_write (SWVoice *sw, void *buf, int len)
{
    return pcm_hw_write (sw, buf, len);
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

static int wav_hw_init (HWVoice *hw, int freq, int nchannels, audfmt_e fmt)
{
    WAVVoice *wav = (WAVVoice *) hw;
    int bits16 = 0, stereo = audio_state.fixed_channels == 2;
    uint8_t hdr[] = {
        0x52, 0x49, 0x46, 0x46, 0x00, 0x00, 0x00, 0x00, 0x57, 0x41, 0x56,
        0x45, 0x66, 0x6d, 0x74, 0x20, 0x10, 0x00, 0x00, 0x00, 0x01, 0x00,
        0x02, 0x00, 0x44, 0xac, 0x00, 0x00, 0x10, 0xb1, 0x02, 0x00, 0x04,
        0x00, 0x10, 0x00, 0x64, 0x61, 0x74, 0x61, 0x00, 0x00, 0x00, 0x00
    };

    switch (audio_state.fixed_fmt) {
    case AUD_FMT_S8:
    case AUD_FMT_U8:
        break;

    case AUD_FMT_S16:
    case AUD_FMT_U16:
        bits16 = 1;
        break;
    }

    hdr[34] = bits16 ? 0x10 : 0x08;
    hw->freq = 44100;
    hw->nchannels = stereo ? 2 : 1;
    hw->fmt = bits16 ? AUD_FMT_S16 : AUD_FMT_U8;
    hw->bufsize = 4096;
    wav->pcm_buf = qemu_mallocz (hw->bufsize);
    if (!wav->pcm_buf)
        return -1;

    le_store (hdr + 22, hw->nchannels, 2);
    le_store (hdr + 24, hw->freq, 4);
    le_store (hdr + 28, hw->freq << (bits16 + stereo), 4);
    le_store (hdr + 32, 1 << (bits16 + stereo), 2);

    wav->f = fopen (conf.wav_path, "wb");
    if (!wav->f) {
        dolog ("failed to open wave file `%s'\nReason: %s\n",
               conf.wav_path, strerror (errno));
        qemu_free (wav->pcm_buf);
        wav->pcm_buf = NULL;
        return -1;
    }

    qemu_put_buffer (wav->f, hdr, sizeof (hdr));
    return 0;
}

static void wav_hw_fini (HWVoice *hw)
{
    WAVVoice *wav = (WAVVoice *) hw;
    int stereo = hw->nchannels == 2;
    uint8_t rlen[4];
    uint8_t dlen[4];
    uint32_t rifflen = (wav->total_samples << stereo) + 36;
    uint32_t datalen = wav->total_samples << stereo;

    if (!wav->f || !hw->active)
        return;

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

static int wav_hw_ctl (HWVoice *hw, int cmd, ...)
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
    ldebug ("wav_fini");
}

struct pcm_ops wav_pcm_ops = {
    wav_hw_init,
    wav_hw_fini,
    wav_hw_run,
    wav_hw_write,
    wav_hw_ctl
};

struct audio_output_driver wav_output_driver = {
    "wav",
    wav_audio_init,
    wav_audio_fini,
    &wav_pcm_ops,
    1,
    1,
    sizeof (WAVVoice)
};

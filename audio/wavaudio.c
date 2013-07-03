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
#include "hw/hw.h"
#include "qemu/timer.h"
#include "audio.h"

#define AUDIO_CAP "wav"
#include "audio_int.h"

typedef struct WAVVoiceOut {
    HWVoiceOut hw;
    FILE *f;
    int64_t old_ticks;
    void *pcm_buf;
    int total_samples;
} WAVVoiceOut;

static struct {
    struct audsettings settings;
    const char *wav_path;
} conf = {
    .settings.freq      = 44100,
    .settings.nchannels = 2,
    .settings.fmt       = AUD_FMT_S16,
    .wav_path           = "qemu.wav"
};

static int wav_run_out (HWVoiceOut *hw, int live)
{
    WAVVoiceOut *wav = (WAVVoiceOut *) hw;
    int rpos, decr, samples;
    uint8_t *dst;
    struct st_sample *src;
    int64_t now = qemu_get_clock_ns (vm_clock);
    int64_t ticks = now - wav->old_ticks;
    int64_t bytes =
        muldiv64 (ticks, hw->info.bytes_per_second, get_ticks_per_sec ());

    if (bytes > INT_MAX) {
        samples = INT_MAX >> hw->info.shift;
    }
    else {
        samples = bytes >> hw->info.shift;
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
        if (fwrite (dst, convert_samples << hw->info.shift, 1, wav->f) != 1) {
            dolog ("wav_run_out: fwrite of %d bytes failed\nReaons: %s\n",
                   convert_samples << hw->info.shift, strerror (errno));
        }

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

static int wav_init_out (HWVoiceOut *hw, struct audsettings *as)
{
    WAVVoiceOut *wav = (WAVVoiceOut *) hw;
    int bits16 = 0, stereo = 0;
    uint8_t hdr[] = {
        0x52, 0x49, 0x46, 0x46, 0x00, 0x00, 0x00, 0x00, 0x57, 0x41, 0x56,
        0x45, 0x66, 0x6d, 0x74, 0x20, 0x10, 0x00, 0x00, 0x00, 0x01, 0x00,
        0x02, 0x00, 0x44, 0xac, 0x00, 0x00, 0x10, 0xb1, 0x02, 0x00, 0x04,
        0x00, 0x10, 0x00, 0x64, 0x61, 0x74, 0x61, 0x00, 0x00, 0x00, 0x00
    };
    struct audsettings wav_as = conf.settings;

    (void) as;

    stereo = wav_as.nchannels == 2;
    switch (wav_as.fmt) {
    case AUD_FMT_S8:
    case AUD_FMT_U8:
        bits16 = 0;
        break;

    case AUD_FMT_S16:
    case AUD_FMT_U16:
        bits16 = 1;
        break;

    case AUD_FMT_S32:
    case AUD_FMT_U32:
        dolog ("WAVE files can not handle 32bit formats\n");
        return -1;
    }

    hdr[34] = bits16 ? 0x10 : 0x08;

    wav_as.endianness = 0;
    audio_pcm_init_info (&hw->info, &wav_as);

    hw->samples = 1024;
    wav->pcm_buf = audio_calloc (AUDIO_FUNC, hw->samples, 1 << hw->info.shift);
    if (!wav->pcm_buf) {
        dolog ("Could not allocate buffer (%d bytes)\n",
               hw->samples << hw->info.shift);
        return -1;
    }

    le_store (hdr + 22, hw->info.nchannels, 2);
    le_store (hdr + 24, hw->info.freq, 4);
    le_store (hdr + 28, hw->info.freq << (bits16 + stereo), 4);
    le_store (hdr + 32, 1 << (bits16 + stereo), 2);

    wav->f = fopen (conf.wav_path, "wb");
    if (!wav->f) {
        dolog ("Failed to open wave file `%s'\nReason: %s\n",
               conf.wav_path, strerror (errno));
        g_free (wav->pcm_buf);
        wav->pcm_buf = NULL;
        return -1;
    }

    if (fwrite (hdr, sizeof (hdr), 1, wav->f) != 1) {
        dolog ("wav_init_out: failed to write header\nReason: %s\n",
               strerror(errno));
        return -1;
    }
    return 0;
}

static void wav_fini_out (HWVoiceOut *hw)
{
    WAVVoiceOut *wav = (WAVVoiceOut *) hw;
    uint8_t rlen[4];
    uint8_t dlen[4];
    uint32_t datalen = wav->total_samples << hw->info.shift;
    uint32_t rifflen = datalen + 36;

    if (!wav->f) {
        return;
    }

    le_store (rlen, rifflen, 4);
    le_store (dlen, datalen, 4);

    if (fseek (wav->f, 4, SEEK_SET)) {
        dolog ("wav_fini_out: fseek to rlen failed\nReason: %s\n",
               strerror(errno));
        goto doclose;
    }
    if (fwrite (rlen, 4, 1, wav->f) != 1) {
        dolog ("wav_fini_out: failed to write rlen\nReason: %s\n",
               strerror (errno));
        goto doclose;
    }
    if (fseek (wav->f, 32, SEEK_CUR)) {
        dolog ("wav_fini_out: fseek to dlen failed\nReason: %s\n",
               strerror (errno));
        goto doclose;
    }
    if (fwrite (dlen, 4, 1, wav->f) != 1) {
        dolog ("wav_fini_out: failed to write dlen\nReaons: %s\n",
               strerror (errno));
        goto doclose;
    }

 doclose:
    if (fclose (wav->f))  {
        dolog ("wav_fini_out: fclose %p failed\nReason: %s\n",
               wav->f, strerror (errno));
    }
    wav->f = NULL;

    g_free (wav->pcm_buf);
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

static struct audio_option wav_options[] = {
    {
        .name  = "FREQUENCY",
        .tag   = AUD_OPT_INT,
        .valp  = &conf.settings.freq,
        .descr = "Frequency"
    },
    {
        .name  = "FORMAT",
        .tag   = AUD_OPT_FMT,
        .valp  = &conf.settings.fmt,
        .descr = "Format"
    },
    {
        .name  = "DAC_FIXED_CHANNELS",
        .tag   = AUD_OPT_INT,
        .valp  = &conf.settings.nchannels,
        .descr = "Number of channels (1 - mono, 2 - stereo)"
    },
    {
        .name  = "PATH",
        .tag   = AUD_OPT_STR,
        .valp  = &conf.wav_path,
        .descr = "Path to wave file"
    },
    { /* End of list */ }
};

static struct audio_pcm_ops wav_pcm_ops = {
    .init_out = wav_init_out,
    .fini_out = wav_fini_out,
    .run_out  = wav_run_out,
    .write    = wav_write_out,
    .ctl_out  = wav_ctl_out,
};

struct audio_driver wav_audio_driver = {
    .name           = "wav",
    .descr          = "WAV renderer http://wikipedia.org/wiki/WAV",
    .options        = wav_options,
    .init           = wav_audio_init,
    .fini           = wav_audio_fini,
    .pcm_ops        = &wav_pcm_ops,
    .can_be_default = 0,
    .max_voices_out = 1,
    .max_voices_in  = 0,
    .voice_size_out = sizeof (WAVVoiceOut),
    .voice_size_in  = 0
};

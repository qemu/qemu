/*
 * QEMU NULL audio output driver
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

typedef struct NoVoice {
    HWVoice hw;
    int64_t old_ticks;
} NoVoice;

#define dolog(...) AUD_log ("noaudio", __VA_ARGS__)
#ifdef DEBUG
#define ldebug(...) dolog (__VA_ARGS__)
#else
#define ldebug(...)
#endif

static void no_hw_run (HWVoice *hw)
{
    NoVoice *no = (NoVoice *) hw;
    int rpos, live, decr, samples;
    st_sample_t *src;
    int64_t now = qemu_get_clock (vm_clock);
    int64_t ticks = now - no->old_ticks;
    int64_t bytes = (ticks * hw->bytes_per_second) / ticks_per_sec;

    if (bytes > INT_MAX)
        samples = INT_MAX >> hw->shift;
    else
        samples = bytes >> hw->shift;

    live = pcm_hw_get_live (hw);
    if (live <= 0)
        return;

    no->old_ticks = now;
    decr = audio_MIN (live, samples);
    samples = decr;
    rpos = hw->rpos;
    while (samples) {
        int left_till_end_samples = hw->samples - rpos;
        int convert_samples = audio_MIN (samples, left_till_end_samples);

        src = advance (hw->mix_buf, rpos * sizeof (st_sample_t));
        memset (src, 0, convert_samples * sizeof (st_sample_t));

        rpos = (rpos + convert_samples) % hw->samples;
        samples -= convert_samples;
    }

    pcm_hw_dec_live (hw, decr);
    hw->rpos = rpos;
}

static int no_hw_write (SWVoice *sw, void *buf, int len)
{
    return pcm_hw_write (sw, buf, len);
}

static int no_hw_init (HWVoice *hw, int freq, int nchannels, audfmt_e fmt)
{
    hw->freq = freq;
    hw->nchannels = nchannels;
    hw->fmt = fmt;
    hw->bufsize = 4096;
    return 0;
}

static void no_hw_fini (HWVoice *hw)
{
    (void) hw;
}

static int no_hw_ctl (HWVoice *hw, int cmd, ...)
{
    (void) hw;
    (void) cmd;
    return 0;
}

static void *no_audio_init (void)
{
    return &no_audio_init;
}

static void no_audio_fini (void *opaque)
{
}

struct pcm_ops no_pcm_ops = {
    no_hw_init,
    no_hw_fini,
    no_hw_run,
    no_hw_write,
    no_hw_ctl
};

struct audio_output_driver no_output_driver = {
    "none",
    no_audio_init,
    no_audio_fini,
    &no_pcm_ops,
    1,
    1,
    sizeof (NoVoice)
};

/*
 * QEMU Timer based audio emulation
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
#include "qemu-common.h"
#include "audio.h"
#include "qemu-timer.h"

#define AUDIO_CAP "noaudio"
#include "audio_int.h"

typedef struct NoVoiceOut {
    HWVoiceOut hw;
    int64_t old_ticks;
} NoVoiceOut;

typedef struct NoVoiceIn {
    HWVoiceIn hw;
    int64_t old_ticks;
} NoVoiceIn;

static int no_run_out (HWVoiceOut *hw, int live)
{
    NoVoiceOut *no = (NoVoiceOut *) hw;
    int decr, samples;
    int64_t now;
    int64_t ticks;
    int64_t bytes;

    now = qemu_get_clock (vm_clock);
    ticks = now - no->old_ticks;
    bytes = muldiv64 (ticks, hw->info.bytes_per_second, get_ticks_per_sec ());
    bytes = audio_MIN (bytes, INT_MAX);
    samples = bytes >> hw->info.shift;

    no->old_ticks = now;
    decr = audio_MIN (live, samples);
    hw->rpos = (hw->rpos + decr) % hw->samples;
    return decr;
}

static int no_write (SWVoiceOut *sw, void *buf, int len)
{
    return audio_pcm_sw_write (sw, buf, len);
}

static int no_init_out (HWVoiceOut *hw, struct audsettings *as)
{
    audio_pcm_init_info (&hw->info, as);
    hw->samples = 1024;
    return 0;
}

static void no_fini_out (HWVoiceOut *hw)
{
    (void) hw;
}

static int no_ctl_out (HWVoiceOut *hw, int cmd, ...)
{
    (void) hw;
    (void) cmd;
    return 0;
}

static int no_init_in (HWVoiceIn *hw, struct audsettings *as)
{
    audio_pcm_init_info (&hw->info, as);
    hw->samples = 1024;
    return 0;
}

static void no_fini_in (HWVoiceIn *hw)
{
    (void) hw;
}

static int no_run_in (HWVoiceIn *hw)
{
    NoVoiceIn *no = (NoVoiceIn *) hw;
    int live = audio_pcm_hw_get_live_in (hw);
    int dead = hw->samples - live;
    int samples = 0;

    if (dead) {
        int64_t now = qemu_get_clock (vm_clock);
        int64_t ticks = now - no->old_ticks;
        int64_t bytes =
            muldiv64 (ticks, hw->info.bytes_per_second, get_ticks_per_sec ());

        no->old_ticks = now;
        bytes = audio_MIN (bytes, INT_MAX);
        samples = bytes >> hw->info.shift;
        samples = audio_MIN (samples, dead);
    }
    return samples;
}

static int no_read (SWVoiceIn *sw, void *buf, int size)
{
    /* use custom code here instead of audio_pcm_sw_read() to avoid
     * useless resampling/mixing */
    int samples = size >> sw->info.shift;
    int total = sw->hw->total_samples_captured - sw->total_hw_samples_acquired;
    int to_clear = audio_MIN (samples, total);
    sw->total_hw_samples_acquired += total;
    audio_pcm_info_clear_buf (&sw->info, buf, to_clear);
    return to_clear << sw->info.shift;
}

static int no_ctl_in (HWVoiceIn *hw, int cmd, ...)
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
    (void) opaque;
}

static struct audio_pcm_ops no_pcm_ops = {
    .init_out = no_init_out,
    .fini_out = no_fini_out,
    .run_out  = no_run_out,
    .write    = no_write,
    .ctl_out  = no_ctl_out,

    .init_in  = no_init_in,
    .fini_in  = no_fini_in,
    .run_in   = no_run_in,
    .read     = no_read,
    .ctl_in   = no_ctl_in
};

struct audio_driver no_audio_driver = {
    .name           = "none",
    .descr          = "Timer based audio emulation",
    .options        = NULL,
    .init           = no_audio_init,
    .fini           = no_audio_fini,
    .pcm_ops        = &no_pcm_ops,
    .can_be_default = 1,
    .max_voices_out = INT_MAX,
    .max_voices_in  = INT_MAX,
    .voice_size_out = sizeof (NoVoiceOut),
    .voice_size_in  = sizeof (NoVoiceIn)
};

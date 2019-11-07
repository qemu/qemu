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

#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "qemu/module.h"
#include "audio.h"
#include "qemu/timer.h"

#define AUDIO_CAP "noaudio"
#include "audio_int.h"

typedef struct NoVoiceOut {
    HWVoiceOut hw;
    RateCtl rate;
} NoVoiceOut;

typedef struct NoVoiceIn {
    HWVoiceIn hw;
    RateCtl rate;
} NoVoiceIn;

static size_t no_write(HWVoiceOut *hw, void *buf, size_t len)
{
    NoVoiceOut *no = (NoVoiceOut *) hw;
    return audio_rate_get_bytes(&hw->info, &no->rate, len);
}

static int no_init_out(HWVoiceOut *hw, struct audsettings *as, void *drv_opaque)
{
    NoVoiceOut *no = (NoVoiceOut *) hw;

    audio_pcm_init_info (&hw->info, as);
    hw->samples = 1024;
    audio_rate_start(&no->rate);
    return 0;
}

static void no_fini_out (HWVoiceOut *hw)
{
    (void) hw;
}

static void no_enable_out(HWVoiceOut *hw, bool enable)
{
    NoVoiceOut *no = (NoVoiceOut *) hw;

    if (enable) {
        audio_rate_start(&no->rate);
    }
}

static int no_init_in(HWVoiceIn *hw, struct audsettings *as, void *drv_opaque)
{
    NoVoiceIn *no = (NoVoiceIn *) hw;

    audio_pcm_init_info (&hw->info, as);
    hw->samples = 1024;
    audio_rate_start(&no->rate);
    return 0;
}

static void no_fini_in (HWVoiceIn *hw)
{
    (void) hw;
}

static size_t no_read(HWVoiceIn *hw, void *buf, size_t size)
{
    NoVoiceIn *no = (NoVoiceIn *) hw;
    int64_t bytes = audio_rate_get_bytes(&hw->info, &no->rate, size);

    audio_pcm_info_clear_buf(&hw->info, buf, bytes / hw->info.bytes_per_frame);
    return bytes;
}

static void no_enable_in(HWVoiceIn *hw, bool enable)
{
    NoVoiceIn *no = (NoVoiceIn *) hw;

    if (enable) {
        audio_rate_start(&no->rate);
    }
}

static void *no_audio_init(Audiodev *dev)
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
    .write    = no_write,
    .enable_out = no_enable_out,

    .init_in  = no_init_in,
    .fini_in  = no_fini_in,
    .read     = no_read,
    .enable_in = no_enable_in
};

static struct audio_driver no_audio_driver = {
    .name           = "none",
    .descr          = "Timer based audio emulation",
    .init           = no_audio_init,
    .fini           = no_audio_fini,
    .pcm_ops        = &no_pcm_ops,
    .can_be_default = 1,
    .max_voices_out = INT_MAX,
    .max_voices_in  = INT_MAX,
    .voice_size_out = sizeof (NoVoiceOut),
    .voice_size_in  = sizeof (NoVoiceIn)
};

static void register_audio_none(void)
{
    audio_driver_register(&no_audio_driver);
}
type_init(register_audio_none);

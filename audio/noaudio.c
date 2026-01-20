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
#include "qemu/module.h"
#include "qemu/audio.h"
#include "qom/object.h"

#include "audio_int.h"

#define TYPE_AUDIO_NONE "audio-none"
OBJECT_DECLARE_SIMPLE_TYPE(AudioNone, AUDIO_NONE)

struct AudioNone {
    AudioMixengBackend parent_obj;
};

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
    return audio_rate_get_bytes(&no->rate, &hw->info, len);
}

static int no_init_out(HWVoiceOut *hw, struct audsettings *as)
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

static int no_init_in(HWVoiceIn *hw, struct audsettings *as)
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
    int64_t bytes = audio_rate_get_bytes(&no->rate, &hw->info, size);

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

static void audio_none_class_init(ObjectClass *klass, const void *data)
{
    AudioMixengBackendClass *k = AUDIO_MIXENG_BACKEND_CLASS(klass);

    k->name = "none";
    k->max_voices_out = INT_MAX;
    k->max_voices_in = INT_MAX;
    k->voice_size_out = sizeof(NoVoiceOut);
    k->voice_size_in = sizeof(NoVoiceIn);

    k->init_out = no_init_out;
    k->fini_out = no_fini_out;
    k->write = no_write;
    k->buffer_get_free = audio_generic_buffer_get_free;
    k->run_buffer_out = audio_generic_run_buffer_out;
    k->enable_out = no_enable_out;

    k->init_in = no_init_in;
    k->fini_in = no_fini_in;
    k->read = no_read;
    k->run_buffer_in = audio_generic_run_buffer_in;
    k->enable_in = no_enable_in;
}

static const TypeInfo audio_types[] = {
    {
        .name = TYPE_AUDIO_NONE,
        .parent = TYPE_AUDIO_MIXENG_BACKEND,
        .instance_size = sizeof(AudioNone),
        .class_init = audio_none_class_init,
    },
};

DEFINE_TYPES(audio_types)
module_obj(TYPE_AUDIO_NONE);

/*
 * QEMU Audio subsystem header
 *
 * Copyright (c) 2005 Vassili Karpov (malc)
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

#ifdef DAC
#define TYPE out
#define HW glue (HWVoice, Out)
#define SW glue (SWVoice, Out)
#else
#define TYPE in
#define HW glue (HWVoice, In)
#define SW glue (SWVoice, In)
#endif

static void glue (audio_pcm_sw_fini_, TYPE) (SW *sw)
{
    glue (audio_pcm_sw_free_resources_, TYPE) (sw);
    if (sw->name) {
        qemu_free (sw->name);
        sw->name = NULL;
    }
}

static void glue (audio_pcm_hw_add_sw_, TYPE) (HW *hw, SW *sw)
{
    LIST_INSERT_HEAD (&hw->sw_head, sw, entries);
}

static void glue (audio_pcm_hw_del_sw_, TYPE) (SW *sw)
{
    LIST_REMOVE (sw, entries);
}

static void glue (audio_pcm_hw_fini_, TYPE) (HW *hw)
{
    if (hw->active) {
        glue (audio_pcm_hw_free_resources_ ,TYPE) (hw);
        glue (hw->pcm_ops->fini_, TYPE) (hw);
        memset (hw, 0, glue (audio_state.drv->voice_size_, TYPE));
    }
}

static void glue (audio_pcm_hw_gc_, TYPE) (HW *hw)
{
    if (!hw->sw_head.lh_first) {
        glue (audio_pcm_hw_fini_, TYPE) (hw);
    }
}

static HW *glue (audio_pcm_hw_find_any_, TYPE) (HW *hw)
{
    return hw ? hw->entries.le_next : glue (hw_head_, TYPE).lh_first;
}

static HW *glue (audio_pcm_hw_find_any_active_, TYPE) (HW *hw)
{
    while ((hw = glue (audio_pcm_hw_find_any_, TYPE) (hw))) {
        if (hw->active) {
            return hw;
        }
    }
    return NULL;
}

static HW *glue (audio_pcm_hw_find_any_active_enabled_, TYPE) (HW *hw)
{
    while ((hw = glue (audio_pcm_hw_find_any_, TYPE) (hw))) {
        if (hw->active && hw->enabled) {
            return hw;
        }
    }
    return NULL;
}

static HW *glue (audio_pcm_hw_find_any_passive_, TYPE) (HW *hw)
{
    while ((hw = glue (audio_pcm_hw_find_any_, TYPE) (hw))) {
        if (!hw->active) {
            return hw;
        }
    }
    return NULL;
}

static HW *glue (audio_pcm_hw_find_specific_, TYPE) (
    HW *hw,
    int freq,
    int nchannels,
    audfmt_e fmt
    )
{
    while ((hw = glue (audio_pcm_hw_find_any_active_, TYPE) (hw))) {
        if (audio_pcm_info_eq (&hw->info, freq, nchannels, fmt)) {
            return hw;
        }
    }
    return NULL;
}

static HW *glue (audio_pcm_hw_add_new_, TYPE) (
    int freq,
    int nchannels,
    audfmt_e fmt
    )
{
    HW *hw;

    hw = glue (audio_pcm_hw_find_any_passive_, TYPE) (NULL);
    if (hw) {
        hw->pcm_ops = audio_state.drv->pcm_ops;
        if (!hw->pcm_ops) {
            return NULL;
        }

        if (glue (audio_pcm_hw_init_, TYPE) (hw, freq, nchannels, fmt)) {
            glue (audio_pcm_hw_gc_, TYPE) (hw);
            return NULL;
        }
        else {
            return hw;
        }
    }

    return NULL;
}

static HW *glue (audio_pcm_hw_add_, TYPE) (
    int freq,
    int nchannels,
    audfmt_e fmt
    )
{
    HW *hw;

    if (glue (audio_state.greedy_, TYPE)) {
        hw = glue (audio_pcm_hw_add_new_, TYPE) (freq, nchannels, fmt);
        if (hw) {
            return hw;
        }
    }

    hw = glue (audio_pcm_hw_find_specific_, TYPE) (NULL, freq, nchannels, fmt);
    if (hw) {
        return hw;
    }

    hw = glue (audio_pcm_hw_add_new_, TYPE) (freq, nchannels, fmt);
    if (hw) {
        return hw;
    }

    return glue (audio_pcm_hw_find_any_active_, TYPE) (NULL);
}

static SW *glue (audio_pcm_create_voice_pair_, TYPE) (
    const char *name,
    int freq,
    int nchannels,
    audfmt_e fmt
    )
{
    SW *sw;
    HW *hw;
    int hw_freq = freq;
    int hw_nchannels = nchannels;
    int hw_fmt = fmt;

    if (glue (audio_state.fixed_settings_, TYPE)) {
        hw_freq = glue (audio_state.fixed_freq_, TYPE);
        hw_nchannels = glue (audio_state.fixed_channels_, TYPE);
        hw_fmt = glue (audio_state.fixed_fmt_, TYPE);
    }

    sw = qemu_mallocz (sizeof (*sw));
    if (!sw) {
        goto err1;
    }

    hw = glue (audio_pcm_hw_add_, TYPE) (hw_freq, hw_nchannels, hw_fmt);
    if (!hw) {
        goto err2;
    }

    glue (audio_pcm_hw_add_sw_, TYPE) (hw, sw);

    if (glue (audio_pcm_sw_init_, TYPE) (sw, hw, name, freq, nchannels, fmt)) {
        goto err3;
    }

    return sw;

err3:
    glue (audio_pcm_hw_del_sw_, TYPE) (sw);
    glue (audio_pcm_hw_gc_, TYPE) (hw);
err2:
    qemu_free (sw);
err1:
    return NULL;
}

void glue (AUD_close_, TYPE) (SW *sw)
{
    if (sw) {
        glue (audio_pcm_sw_fini_, TYPE) (sw);
        glue (audio_pcm_hw_del_sw_, TYPE) (sw);
        glue (audio_pcm_hw_gc_, TYPE) (sw->hw);
        qemu_free (sw);
    }
}

SW *glue (AUD_open_, TYPE) (
    SW *sw,
    const char *name,
    void *callback_opaque ,
    audio_callback_fn_t callback_fn,
    int freq,
    int nchannels,
    audfmt_e fmt
    )
{
#ifdef DAC
    int live = 0;
    SW *old_sw = NULL;
#endif

    if (!callback_fn) {
        dolog ("No callback specifed for voice `%s'\n", name);
        goto fail;
    }

    if (nchannels != 1 && nchannels != 2) {
        dolog ("Bogus channel count %d for voice `%s'\n", nchannels, name);
        goto fail;
    }

    if (!audio_state.drv) {
        dolog ("No audio driver defined\n");
        goto fail;
    }

    if (sw && audio_pcm_info_eq (&sw->info, freq, nchannels, fmt)) {
        return sw;
    }

#ifdef DAC
    if (audio_state.plive && sw && (!sw->active && !sw->empty)) {
        live = sw->total_hw_samples_mixed;

#ifdef DEBUG_PLIVE
        dolog ("Replacing voice %s with %d live samples\n", sw->name, live);
        dolog ("Old %s freq %d, bits %d, channels %d\n",
               sw->name, sw->info.freq, sw->info.bits, sw->info.nchannels);
        dolog ("New %s freq %d, bits %d, channels %d\n",
               name, freq, (fmt == AUD_FMT_S16 || fmt == AUD_FMT_U16) ? 16 : 8,
               nchannels);
#endif

        if (live) {
            old_sw = sw;
            old_sw->callback.fn = NULL;
            sw = NULL;
        }
    }
#endif

    if (!glue (audio_state.fixed_settings_, TYPE) && sw) {
        glue (AUD_close_, TYPE) (sw);
        sw = NULL;
    }

    if (sw) {
        HW *hw = sw->hw;

        if (!hw) {
            dolog ("Internal logic error voice %s has no hardware store\n",
                   name);
            goto fail;
        }

        if (glue (audio_pcm_sw_init_, TYPE) (
                sw,
                hw,
                name,
                freq,
                nchannels,
                fmt
                )) {
            goto fail;
        }
    }
    else {
        sw = glue (audio_pcm_create_voice_pair_, TYPE) (
            name,
            freq,
            nchannels,
            fmt);
        if (!sw) {
            dolog ("Failed to create voice %s\n", name);
            goto fail;
        }
    }

    if (sw) {
        sw->vol = nominal_volume;
        sw->callback.fn = callback_fn;
        sw->callback.opaque = callback_opaque;

#ifdef DAC
        if (live) {
            int mixed =
                (live << old_sw->info.shift)
                * old_sw->info.bytes_per_second
                / sw->info.bytes_per_second;

#ifdef DEBUG_PLIVE
            dolog ("Silence will be mixed %d\n", mixed);
#endif
            sw->total_hw_samples_mixed += mixed;
        }
#endif

#ifdef DEBUG_AUDIO
        dolog ("%s\n", name);
        audio_pcm_print_info ("hw", &sw->hw->info);
        audio_pcm_print_info ("sw", &sw->info);
#endif
    }

    return sw;

 fail:
    glue (AUD_close_, TYPE) (sw);
    return NULL;
}

int glue (AUD_is_active_, TYPE) (SW *sw)
{
    return sw ? sw->active : 0;
}

void glue (AUD_init_time_stamp_, TYPE) (SW *sw, QEMUAudioTimeStamp *ts)
{
    if (!sw) {
        return;
    }

    ts->old_ts = sw->hw->ts_helper;
}

uint64_t glue (AUD_time_stamp_get_elapsed_usec_, TYPE) (
    SW *sw,
    QEMUAudioTimeStamp *ts
    )
{
    uint64_t delta, cur_ts, old_ts;

    if (!sw) {
        return 0;
    }

    cur_ts = sw->hw->ts_helper;
    old_ts = ts->old_ts;
    /* dolog ("cur %lld old %lld\n", cur_ts, old_ts); */

    if (cur_ts >= old_ts) {
        delta = cur_ts - old_ts;
    }
    else {
        delta = UINT64_MAX - old_ts + cur_ts;
    }

    if (!delta) {
        return 0;
    }

    return (delta * sw->hw->info.freq) / 1000000;
}

#undef TYPE
#undef HW
#undef SW

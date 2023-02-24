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
#define NAME "playback"
#define HWBUF hw->mix_buf
#define TYPE out
#define HW HWVoiceOut
#define SW SWVoiceOut
#else
#define NAME "capture"
#define TYPE in
#define HW HWVoiceIn
#define SW SWVoiceIn
#define HWBUF hw->conv_buf
#endif

static void glue(audio_init_nb_voices_, TYPE)(AudioState *s,
                                              struct audio_driver *drv)
{
    int max_voices = glue (drv->max_voices_, TYPE);
    size_t voice_size = glue(drv->voice_size_, TYPE);

    if (glue (s->nb_hw_voices_, TYPE) > max_voices) {
        if (!max_voices) {
#ifdef DAC
            dolog ("Driver `%s' does not support " NAME "\n", drv->name);
#endif
        } else {
            dolog ("Driver `%s' does not support %d " NAME " voices, max %d\n",
                   drv->name,
                   glue (s->nb_hw_voices_, TYPE),
                   max_voices);
        }
        glue (s->nb_hw_voices_, TYPE) = max_voices;
    }

    if (audio_bug(__func__, !voice_size && max_voices)) {
        dolog ("drv=`%s' voice_size=0 max_voices=%d\n",
               drv->name, max_voices);
        glue (s->nb_hw_voices_, TYPE) = 0;
    }

    if (audio_bug(__func__, voice_size && !max_voices)) {
        dolog("drv=`%s' voice_size=%zu max_voices=0\n",
              drv->name, voice_size);
    }
}

static void glue (audio_pcm_hw_free_resources_, TYPE) (HW *hw)
{
    g_free(hw->buf_emul);
    g_free(HWBUF.buffer);
    HWBUF.buffer = NULL;
    HWBUF.size = 0;
}

static void glue(audio_pcm_hw_alloc_resources_, TYPE)(HW *hw)
{
    if (glue(audio_get_pdo_, TYPE)(hw->s->dev)->mixing_engine) {
        size_t samples = hw->samples;
        if (audio_bug(__func__, samples == 0)) {
            dolog("Attempted to allocate empty buffer\n");
        }

        HWBUF.buffer = g_new0(st_sample, samples);
        HWBUF.size = samples;
        HWBUF.pos = 0;
    } else {
        HWBUF.buffer = NULL;
        HWBUF.size = 0;
    }
}

static void glue (audio_pcm_sw_free_resources_, TYPE) (SW *sw)
{
    g_free (sw->buf);

    if (sw->rate) {
        st_rate_stop (sw->rate);
    }

    sw->buf = NULL;
    sw->rate = NULL;
}

static int glue (audio_pcm_sw_alloc_resources_, TYPE) (SW *sw)
{
    int samples;

    if (!glue(audio_get_pdo_, TYPE)(sw->s->dev)->mixing_engine) {
        return 0;
    }

#ifdef DAC
    samples = ((int64_t)sw->HWBUF.size << 32) / sw->ratio;
#else
    samples = (int64_t)sw->HWBUF.size * sw->ratio >> 32;
#endif
    if (audio_bug(__func__, samples < 0)) {
        dolog("Can not allocate buffer for `%s' (%d samples)\n",
              SW_NAME(sw), samples);
        return -1;
    }

    if (samples == 0) {
        HW *hw = sw->hw;
        size_t f_fe_min;

        /* f_fe_min = ceil(1 [frames] * f_be [Hz] / size_be [frames]) */
        f_fe_min = (hw->info.freq + HWBUF.size - 1) / HWBUF.size;
        qemu_log_mask(LOG_UNIMP,
                      AUDIO_CAP ": The guest selected a " NAME " sample rate"
                      " of %d Hz for %s. Only sample rates >= %zu Hz are"
                      " supported.\n",
                      sw->info.freq, sw->name, f_fe_min);
        return -1;
    }

    sw->buf = g_new0(st_sample, samples);

#ifdef DAC
    sw->rate = st_rate_start (sw->info.freq, sw->hw->info.freq);
#else
    sw->rate = st_rate_start (sw->hw->info.freq, sw->info.freq);
#endif

    return 0;
}

static int glue (audio_pcm_sw_init_, TYPE) (
    SW *sw,
    HW *hw,
    const char *name,
    struct audsettings *as
    )
{
    int err;

    audio_pcm_init_info (&sw->info, as);
    sw->hw = hw;
    sw->active = 0;
#ifdef DAC
    sw->ratio = ((int64_t) sw->hw->info.freq << 32) / sw->info.freq;
    sw->total_hw_samples_mixed = 0;
    sw->empty = 1;
#else
    sw->ratio = ((int64_t) sw->info.freq << 32) / sw->hw->info.freq;
#endif

    if (sw->info.is_float) {
#ifdef DAC
        sw->conv = mixeng_conv_float[sw->info.nchannels == 2];
#else
        sw->clip = mixeng_clip_float[sw->info.nchannels == 2];
#endif
    } else {
#ifdef DAC
        sw->conv = mixeng_conv
#else
        sw->clip = mixeng_clip
#endif
            [sw->info.nchannels == 2]
            [sw->info.is_signed]
            [sw->info.swap_endianness]
            [audio_bits_to_index(sw->info.bits)];
    }

    sw->name = g_strdup (name);
    err = glue (audio_pcm_sw_alloc_resources_, TYPE) (sw);
    if (err) {
        g_free (sw->name);
        sw->name = NULL;
    }
    return err;
}

static void glue (audio_pcm_sw_fini_, TYPE) (SW *sw)
{
    glue (audio_pcm_sw_free_resources_, TYPE) (sw);
    g_free (sw->name);
    sw->name = NULL;
}

static void glue (audio_pcm_hw_add_sw_, TYPE) (HW *hw, SW *sw)
{
    QLIST_INSERT_HEAD (&hw->sw_head, sw, entries);
}

static void glue (audio_pcm_hw_del_sw_, TYPE) (SW *sw)
{
    QLIST_REMOVE (sw, entries);
}

static void glue (audio_pcm_hw_gc_, TYPE) (HW **hwp)
{
    HW *hw = *hwp;
    AudioState *s = hw->s;

    if (!hw->sw_head.lh_first) {
#ifdef DAC
        audio_detach_capture(hw);
#endif
        QLIST_REMOVE(hw, entries);
        glue(hw->pcm_ops->fini_, TYPE) (hw);
        glue(s->nb_hw_voices_, TYPE) += 1;
        glue(audio_pcm_hw_free_resources_ , TYPE) (hw);
        g_free(hw);
        *hwp = NULL;
    }
}

static HW *glue(audio_pcm_hw_find_any_, TYPE)(AudioState *s, HW *hw)
{
    return hw ? hw->entries.le_next : glue (s->hw_head_, TYPE).lh_first;
}

static HW *glue(audio_pcm_hw_find_any_enabled_, TYPE)(AudioState *s, HW *hw)
{
    while ((hw = glue(audio_pcm_hw_find_any_, TYPE)(s, hw))) {
        if (hw->enabled) {
            return hw;
        }
    }
    return NULL;
}

static HW *glue(audio_pcm_hw_find_specific_, TYPE)(AudioState *s, HW *hw,
                                                   struct audsettings *as)
{
    while ((hw = glue(audio_pcm_hw_find_any_, TYPE)(s, hw))) {
        if (audio_pcm_info_eq (&hw->info, as)) {
            return hw;
        }
    }
    return NULL;
}

static HW *glue(audio_pcm_hw_add_new_, TYPE)(AudioState *s,
                                             struct audsettings *as)
{
    HW *hw;
    struct audio_driver *drv = s->drv;

    if (!glue (s->nb_hw_voices_, TYPE)) {
        return NULL;
    }

    if (audio_bug(__func__, !drv)) {
        dolog ("No host audio driver\n");
        return NULL;
    }

    if (audio_bug(__func__, !drv->pcm_ops)) {
        dolog ("Host audio driver without pcm_ops\n");
        return NULL;
    }

    /*
     * Since glue(s->nb_hw_voices_, TYPE) is != 0, glue(drv->voice_size_, TYPE)
     * is guaranteed to be != 0. See the audio_init_nb_voices_* functions.
     */
    hw = g_malloc0(glue(drv->voice_size_, TYPE));
    hw->s = s;
    hw->pcm_ops = drv->pcm_ops;

    QLIST_INIT (&hw->sw_head);
#ifdef DAC
    QLIST_INIT (&hw->cap_head);
#endif
    if (glue (hw->pcm_ops->init_, TYPE) (hw, as, s->drv_opaque)) {
        goto err0;
    }

    if (audio_bug(__func__, hw->samples <= 0)) {
        dolog("hw->samples=%zd\n", hw->samples);
        goto err1;
    }

    if (hw->info.is_float) {
#ifdef DAC
        hw->clip = mixeng_clip_float[hw->info.nchannels == 2];
#else
        hw->conv = mixeng_conv_float[hw->info.nchannels == 2];
#endif
    } else {
#ifdef DAC
        hw->clip = mixeng_clip
#else
        hw->conv = mixeng_conv
#endif
            [hw->info.nchannels == 2]
            [hw->info.is_signed]
            [hw->info.swap_endianness]
            [audio_bits_to_index(hw->info.bits)];
    }

    glue(audio_pcm_hw_alloc_resources_, TYPE)(hw);

    QLIST_INSERT_HEAD (&s->glue (hw_head_, TYPE), hw, entries);
    glue (s->nb_hw_voices_, TYPE) -= 1;
#ifdef DAC
    audio_attach_capture (hw);
#endif
    return hw;

 err1:
    glue (hw->pcm_ops->fini_, TYPE) (hw);
 err0:
    g_free (hw);
    return NULL;
}

AudiodevPerDirectionOptions *glue(audio_get_pdo_, TYPE)(Audiodev *dev)
{
    switch (dev->driver) {
    case AUDIODEV_DRIVER_NONE:
        return dev->u.none.TYPE;
#ifdef CONFIG_AUDIO_ALSA
    case AUDIODEV_DRIVER_ALSA:
        return qapi_AudiodevAlsaPerDirectionOptions_base(dev->u.alsa.TYPE);
#endif
#ifdef CONFIG_AUDIO_COREAUDIO
    case AUDIODEV_DRIVER_COREAUDIO:
        return qapi_AudiodevCoreaudioPerDirectionOptions_base(
            dev->u.coreaudio.TYPE);
#endif
#ifdef CONFIG_DBUS_DISPLAY
    case AUDIODEV_DRIVER_DBUS:
        return dev->u.dbus.TYPE;
#endif
#ifdef CONFIG_AUDIO_DSOUND
    case AUDIODEV_DRIVER_DSOUND:
        return dev->u.dsound.TYPE;
#endif
#ifdef CONFIG_AUDIO_JACK
    case AUDIODEV_DRIVER_JACK:
        return qapi_AudiodevJackPerDirectionOptions_base(dev->u.jack.TYPE);
#endif
#ifdef CONFIG_AUDIO_OSS
    case AUDIODEV_DRIVER_OSS:
        return qapi_AudiodevOssPerDirectionOptions_base(dev->u.oss.TYPE);
#endif
#ifdef CONFIG_AUDIO_PA
    case AUDIODEV_DRIVER_PA:
        return qapi_AudiodevPaPerDirectionOptions_base(dev->u.pa.TYPE);
#endif
#ifdef CONFIG_AUDIO_SDL
    case AUDIODEV_DRIVER_SDL:
        return qapi_AudiodevSdlPerDirectionOptions_base(dev->u.sdl.TYPE);
#endif
#ifdef CONFIG_AUDIO_SNDIO
    case AUDIODEV_DRIVER_SNDIO:
        return dev->u.sndio.TYPE;
#endif
#ifdef CONFIG_SPICE
    case AUDIODEV_DRIVER_SPICE:
        return dev->u.spice.TYPE;
#endif
    case AUDIODEV_DRIVER_WAV:
        return dev->u.wav.TYPE;

    case AUDIODEV_DRIVER__MAX:
        break;
    }
    abort();
}

static HW *glue(audio_pcm_hw_add_, TYPE)(AudioState *s, struct audsettings *as)
{
    HW *hw;
    AudiodevPerDirectionOptions *pdo = glue(audio_get_pdo_, TYPE)(s->dev);

    if (!pdo->mixing_engine || pdo->fixed_settings) {
        hw = glue(audio_pcm_hw_add_new_, TYPE)(s, as);
        if (!pdo->mixing_engine || hw) {
            return hw;
        }
    }

    hw = glue(audio_pcm_hw_find_specific_, TYPE)(s, NULL, as);
    if (hw) {
        return hw;
    }

    hw = glue(audio_pcm_hw_add_new_, TYPE)(s, as);
    if (hw) {
        return hw;
    }

    return glue(audio_pcm_hw_find_any_, TYPE)(s, NULL);
}

static SW *glue(audio_pcm_create_voice_pair_, TYPE)(
    AudioState *s,
    const char *sw_name,
    struct audsettings *as
    )
{
    SW *sw;
    HW *hw;
    struct audsettings hw_as;
    AudiodevPerDirectionOptions *pdo = glue(audio_get_pdo_, TYPE)(s->dev);

    if (pdo->fixed_settings) {
        hw_as = audiodev_to_audsettings(pdo);
    } else {
        hw_as = *as;
    }

    sw = g_new0(SW, 1);
    sw->s = s;

    hw = glue(audio_pcm_hw_add_, TYPE)(s, &hw_as);
    if (!hw) {
        dolog("Could not create a backend for voice `%s'\n", sw_name);
        goto err1;
    }

    glue (audio_pcm_hw_add_sw_, TYPE) (hw, sw);

    if (glue (audio_pcm_sw_init_, TYPE) (sw, hw, sw_name, as)) {
        goto err2;
    }

    return sw;

err2:
    glue (audio_pcm_hw_del_sw_, TYPE) (sw);
    glue (audio_pcm_hw_gc_, TYPE) (&hw);
err1:
    g_free(sw);
    return NULL;
}

static void glue (audio_close_, TYPE) (SW *sw)
{
    glue (audio_pcm_sw_fini_, TYPE) (sw);
    glue (audio_pcm_hw_del_sw_, TYPE) (sw);
    glue (audio_pcm_hw_gc_, TYPE) (&sw->hw);
    g_free (sw);
}

void glue (AUD_close_, TYPE) (QEMUSoundCard *card, SW *sw)
{
    if (sw) {
        if (audio_bug(__func__, !card)) {
            dolog ("card=%p\n", card);
            return;
        }

        glue (audio_close_, TYPE) (sw);
    }
}

SW *glue (AUD_open_, TYPE) (
    QEMUSoundCard *card,
    SW *sw,
    const char *name,
    void *callback_opaque ,
    audio_callback_fn callback_fn,
    struct audsettings *as
    )
{
    AudioState *s;
    AudiodevPerDirectionOptions *pdo;

    if (audio_bug(__func__, !card || !name || !callback_fn || !as)) {
        dolog ("card=%p name=%p callback_fn=%p as=%p\n",
               card, name, callback_fn, as);
        goto fail;
    }

    s = card->state;
    pdo = glue(audio_get_pdo_, TYPE)(s->dev);

    ldebug ("open %s, freq %d, nchannels %d, fmt %d\n",
            name, as->freq, as->nchannels, as->fmt);

    if (audio_bug(__func__, audio_validate_settings(as))) {
        audio_print_settings (as);
        goto fail;
    }

    if (audio_bug(__func__, !s->drv)) {
        dolog ("Can not open `%s' (no host audio driver)\n", name);
        goto fail;
    }

    if (sw && audio_pcm_info_eq (&sw->info, as)) {
        return sw;
    }

    if (!pdo->fixed_settings && sw) {
        glue (AUD_close_, TYPE) (card, sw);
        sw = NULL;
    }

    if (sw) {
        HW *hw = sw->hw;

        if (!hw) {
            dolog("Internal logic error: voice `%s' has no backend\n",
                  SW_NAME(sw));
            goto fail;
        }

        glue (audio_pcm_sw_fini_, TYPE) (sw);
        if (glue (audio_pcm_sw_init_, TYPE) (sw, hw, name, as)) {
            goto fail;
        }
    } else {
        sw = glue(audio_pcm_create_voice_pair_, TYPE)(s, name, as);
        if (!sw) {
            return NULL;
        }
    }

    sw->card = card;
    sw->vol = nominal_volume;
    sw->callback.fn = callback_fn;
    sw->callback.opaque = callback_opaque;

#ifdef DEBUG_AUDIO
    dolog ("%s\n", name);
    audio_pcm_print_info ("hw", &sw->hw->info);
    audio_pcm_print_info ("sw", &sw->info);
#endif

    return sw;

 fail:
    glue (AUD_close_, TYPE) (card, sw);
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

uint64_t glue (AUD_get_elapsed_usec_, TYPE) (SW *sw, QEMUAudioTimeStamp *ts)
{
    uint64_t delta, cur_ts, old_ts;

    if (!sw) {
        return 0;
    }

    cur_ts = sw->hw->ts_helper;
    old_ts = ts->old_ts;
    /* dolog ("cur %" PRId64 " old %" PRId64 "\n", cur_ts, old_ts); */

    if (cur_ts >= old_ts) {
        delta = cur_ts - old_ts;
    } else {
        delta = UINT64_MAX - old_ts + cur_ts;
    }

    if (!delta) {
        return 0;
    }

    return muldiv64 (delta, sw->hw->info.freq, 1000000);
}

#undef TYPE
#undef HW
#undef SW
#undef HWBUF
#undef NAME

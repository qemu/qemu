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

static void glue (audio_init_nb_voices_, TYPE) (struct audio_driver *drv)
{
    AudioState *s = &glob_audio_state;
    int max_voices = glue (drv->max_voices_, TYPE);
    int voice_size = glue (drv->voice_size_, TYPE);

    if (glue (s->nb_hw_voices_, TYPE) > max_voices) {
        if (!max_voices) {
#ifdef DAC
            dolog ("Driver `%s' does not support " NAME "\n", drv->name);
#endif
        }
        else {
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
        dolog ("drv=`%s' voice_size=%d max_voices=0\n",
               drv->name, voice_size);
    }
}

static void glue (audio_pcm_hw_free_resources_, TYPE) (HW *hw)
{
    g_free (HWBUF);
    HWBUF = NULL;
}

static int glue (audio_pcm_hw_alloc_resources_, TYPE) (HW *hw)
{
    HWBUF = audio_calloc(__func__, hw->samples, sizeof(struct st_sample));
    if (!HWBUF) {
        dolog ("Could not allocate " NAME " buffer (%d samples)\n",
               hw->samples);
        return -1;
    }

    return 0;
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

    samples = ((int64_t) sw->hw->samples << 32) / sw->ratio;

    sw->buf = audio_calloc(__func__, samples, sizeof(struct st_sample));
    if (!sw->buf) {
        dolog ("Could not allocate buffer for `%s' (%d samples)\n",
               SW_NAME (sw), samples);
        return -1;
    }

#ifdef DAC
    sw->rate = st_rate_start (sw->info.freq, sw->hw->info.freq);
#else
    sw->rate = st_rate_start (sw->hw->info.freq, sw->info.freq);
#endif
    if (!sw->rate) {
        g_free (sw->buf);
        sw->buf = NULL;
        return -1;
    }
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

#ifdef DAC
    sw->conv = mixeng_conv
#else
    sw->clip = mixeng_clip
#endif
        [sw->info.nchannels == 2]
        [sw->info.sign]
        [sw->info.swap_endianness]
        [audio_bits_to_index (sw->info.bits)];

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
    AudioState *s = &glob_audio_state;
    HW *hw = *hwp;

    if (!hw->sw_head.lh_first) {
#ifdef DAC
        audio_detach_capture (hw);
#endif
        QLIST_REMOVE (hw, entries);
        glue (hw->pcm_ops->fini_, TYPE) (hw);
        glue (s->nb_hw_voices_, TYPE) += 1;
        glue (audio_pcm_hw_free_resources_ ,TYPE) (hw);
        g_free (hw);
        *hwp = NULL;
    }
}

static HW *glue (audio_pcm_hw_find_any_, TYPE) (HW *hw)
{
    AudioState *s = &glob_audio_state;
    return hw ? hw->entries.le_next : glue (s->hw_head_, TYPE).lh_first;
}

static HW *glue (audio_pcm_hw_find_any_enabled_, TYPE) (HW *hw)
{
    while ((hw = glue (audio_pcm_hw_find_any_, TYPE) (hw))) {
        if (hw->enabled) {
            return hw;
        }
    }
    return NULL;
}

static HW *glue (audio_pcm_hw_find_specific_, TYPE) (
    HW *hw,
    struct audsettings *as
    )
{
    while ((hw = glue (audio_pcm_hw_find_any_, TYPE) (hw))) {
        if (audio_pcm_info_eq (&hw->info, as)) {
            return hw;
        }
    }
    return NULL;
}

static HW *glue (audio_pcm_hw_add_new_, TYPE) (struct audsettings *as)
{
    HW *hw;
    AudioState *s = &glob_audio_state;
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

    hw = audio_calloc(__func__, 1, glue(drv->voice_size_, TYPE));
    if (!hw) {
        dolog ("Can not allocate voice `%s' size %d\n",
               drv->name, glue (drv->voice_size_, TYPE));
        return NULL;
    }

    hw->pcm_ops = drv->pcm_ops;
    hw->ctl_caps = drv->ctl_caps;

    QLIST_INIT (&hw->sw_head);
#ifdef DAC
    QLIST_INIT (&hw->cap_head);
#endif
    if (glue (hw->pcm_ops->init_, TYPE) (hw, as, s->drv_opaque)) {
        goto err0;
    }

    if (audio_bug(__func__, hw->samples <= 0)) {
        dolog ("hw->samples=%d\n", hw->samples);
        goto err1;
    }

#ifdef DAC
    hw->clip = mixeng_clip
#else
    hw->conv = mixeng_conv
#endif
        [hw->info.nchannels == 2]
        [hw->info.sign]
        [hw->info.swap_endianness]
        [audio_bits_to_index (hw->info.bits)];

    if (glue (audio_pcm_hw_alloc_resources_, TYPE) (hw)) {
        goto err1;
    }

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
    case AUDIODEV_DRIVER_ALSA:
        return qapi_AudiodevAlsaPerDirectionOptions_base(dev->u.alsa.TYPE);
    case AUDIODEV_DRIVER_COREAUDIO:
        return qapi_AudiodevCoreaudioPerDirectionOptions_base(
            dev->u.coreaudio.TYPE);
    case AUDIODEV_DRIVER_DSOUND:
        return dev->u.dsound.TYPE;
    case AUDIODEV_DRIVER_OSS:
        return qapi_AudiodevOssPerDirectionOptions_base(dev->u.oss.TYPE);
    case AUDIODEV_DRIVER_PA:
        return qapi_AudiodevPaPerDirectionOptions_base(dev->u.pa.TYPE);
    case AUDIODEV_DRIVER_SDL:
        return dev->u.sdl.TYPE;
    case AUDIODEV_DRIVER_SPICE:
        return dev->u.spice.TYPE;
    case AUDIODEV_DRIVER_WAV:
        return dev->u.wav.TYPE;

    case AUDIODEV_DRIVER__MAX:
        break;
    }
    abort();
}

static HW *glue (audio_pcm_hw_add_, TYPE) (struct audsettings *as)
{
    HW *hw;
    AudioState *s = &glob_audio_state;
    AudiodevPerDirectionOptions *pdo = glue(audio_get_pdo_, TYPE)(s->dev);

    if (pdo->fixed_settings) {
        hw = glue (audio_pcm_hw_add_new_, TYPE) (as);
        if (hw) {
            return hw;
        }
    }

    hw = glue (audio_pcm_hw_find_specific_, TYPE) (NULL, as);
    if (hw) {
        return hw;
    }

    hw = glue (audio_pcm_hw_add_new_, TYPE) (as);
    if (hw) {
        return hw;
    }

    return glue (audio_pcm_hw_find_any_, TYPE) (NULL);
}

static SW *glue (audio_pcm_create_voice_pair_, TYPE) (
    const char *sw_name,
    struct audsettings *as
    )
{
    SW *sw;
    HW *hw;
    struct audsettings hw_as;
    AudioState *s = &glob_audio_state;
    AudiodevPerDirectionOptions *pdo = glue(audio_get_pdo_, TYPE)(s->dev);

    if (pdo->fixed_settings) {
        hw_as = audiodev_to_audsettings(pdo);
    }
    else {
        hw_as = *as;
    }

    sw = audio_calloc(__func__, 1, sizeof(*sw));
    if (!sw) {
        dolog ("Could not allocate soft voice `%s' (%zu bytes)\n",
               sw_name ? sw_name : "unknown", sizeof (*sw));
        goto err1;
    }

    hw = glue (audio_pcm_hw_add_, TYPE) (&hw_as);
    if (!hw) {
        goto err2;
    }

    glue (audio_pcm_hw_add_sw_, TYPE) (hw, sw);

    if (glue (audio_pcm_sw_init_, TYPE) (sw, hw, sw_name, as)) {
        goto err3;
    }

    return sw;

err3:
    glue (audio_pcm_hw_del_sw_, TYPE) (sw);
    glue (audio_pcm_hw_gc_, TYPE) (&hw);
err2:
    g_free (sw);
err1:
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
    AudioState *s = &glob_audio_state;
    AudiodevPerDirectionOptions *pdo = glue(audio_get_pdo_, TYPE)(s->dev);

    if (audio_bug(__func__, !card || !name || !callback_fn || !as)) {
        dolog ("card=%p name=%p callback_fn=%p as=%p\n",
               card, name, callback_fn, as);
        goto fail;
    }

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
            dolog ("Internal logic error voice `%s' has no hardware store\n",
                   SW_NAME (sw));
            goto fail;
        }

        glue (audio_pcm_sw_fini_, TYPE) (sw);
        if (glue (audio_pcm_sw_init_, TYPE) (sw, hw, name, as)) {
            goto fail;
        }
    }
    else {
        sw = glue (audio_pcm_create_voice_pair_, TYPE) (name, as);
        if (!sw) {
            dolog ("Failed to create voice `%s'\n", name);
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
    }
    else {
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

/*
 * QEMU SDL audio output driver
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
#include <SDL/SDL.h>
#include <SDL/SDL_thread.h>
#include "vl.h"

#define AUDIO_CAP "sdl"
#include "audio/audio.h"
#include "audio/sdlaudio.h"

#define QC_SDL_SAMPLES "QEMU_SDL_SAMPLES"

#define errstr() SDL_GetError ()

static struct {
    int nb_samples;
} conf = {
    1024
};

struct SDLAudioState {
    int exit;
    SDL_mutex *mutex;
    SDL_sem *sem;
    int initialized;
} glob_sdl;
typedef struct SDLAudioState SDLAudioState;

static void sdl_hw_run (HWVoice *hw)
{
    (void) hw;
}

static int sdl_lock (SDLAudioState *s)
{
    if (SDL_LockMutex (s->mutex)) {
        dolog ("SDL_LockMutex failed\nReason: %s\n", errstr ());
        return -1;
    }
    return 0;
}

static int sdl_unlock (SDLAudioState *s)
{
    if (SDL_UnlockMutex (s->mutex)) {
        dolog ("SDL_UnlockMutex failed\nReason: %s\n", errstr ());
        return -1;
    }
    return 0;
}

static int sdl_post (SDLAudioState *s)
{
    if (SDL_SemPost (s->sem)) {
        dolog ("SDL_SemPost failed\nReason: %s\n", errstr ());
        return -1;
    }
    return 0;
}

static int sdl_wait (SDLAudioState *s)
{
    if (SDL_SemWait (s->sem)) {
        dolog ("SDL_SemWait failed\nReason: %s\n", errstr ());
        return -1;
    }
    return 0;
}

static int sdl_unlock_and_post (SDLAudioState *s)
{
    if (sdl_unlock (s))
        return -1;

    return sdl_post (s);
}

static int sdl_hw_write (SWVoice *sw, void *buf, int len)
{
    int ret;
    SDLAudioState *s = &glob_sdl;
    sdl_lock (s);
    ret = pcm_hw_write (sw, buf, len);
    sdl_unlock_and_post (s);
    return ret;
}

static int AUD_to_sdlfmt (audfmt_e fmt, int *shift)
{
    *shift = 0;
    switch (fmt) {
    case AUD_FMT_S8: return AUDIO_S8;
    case AUD_FMT_U8: return AUDIO_U8;
    case AUD_FMT_S16: *shift = 1; return AUDIO_S16LSB;
    case AUD_FMT_U16: *shift = 1; return AUDIO_U16LSB;
    default:
        dolog ("Internal logic error: Bad audio format %d\nAborting\n", fmt);
        exit (EXIT_FAILURE);
    }
}

static int sdl_to_audfmt (int fmt)
{
    switch (fmt) {
    case AUDIO_S8: return AUD_FMT_S8;
    case AUDIO_U8: return AUD_FMT_U8;
    case AUDIO_S16LSB: return AUD_FMT_S16;
    case AUDIO_U16LSB: return AUD_FMT_U16;
    default:
        dolog ("Internal logic error: Unrecognized SDL audio format %d\n"
               "Aborting\n", fmt);
        exit (EXIT_FAILURE);
    }
}

static int sdl_open (SDL_AudioSpec *req, SDL_AudioSpec *obt)
{
    int status;

    status = SDL_OpenAudio (req, obt);
    if (status) {
        dolog ("SDL_OpenAudio failed\nReason: %s\n", errstr ());
    }
    return status;
}

static void sdl_close (SDLAudioState *s)
{
    if (s->initialized) {
        sdl_lock (s);
        s->exit = 1;
        sdl_unlock_and_post (s);
        SDL_PauseAudio (1);
        SDL_CloseAudio ();
        s->initialized = 0;
    }
}

static void sdl_callback (void *opaque, Uint8 *buf, int len)
{
    SDLVoice *sdl = opaque;
    SDLAudioState *s = &glob_sdl;
    HWVoice *hw = &sdl->hw;
    int samples = len >> hw->shift;

    if (s->exit) {
        return;
    }

    while (samples) {
        int to_mix, live, decr;

        /* dolog ("in callback samples=%d\n", samples); */
        sdl_wait (s);
        if (s->exit) {
            return;
        }

        sdl_lock (s);
        live = pcm_hw_get_live (hw);
        if (live <= 0)
            goto again;

        /* dolog ("in callback live=%d\n", live); */
        to_mix = audio_MIN (samples, live);
        decr = to_mix;
        while (to_mix) {
            int chunk = audio_MIN (to_mix, hw->samples - hw->rpos);
            st_sample_t *src = hw->mix_buf + hw->rpos;

            /* dolog ("in callback to_mix %d, chunk %d\n", to_mix, chunk); */
            hw->clip (buf, src, chunk);
            memset (src, 0, chunk * sizeof (st_sample_t));
            hw->rpos = (hw->rpos + chunk) % hw->samples;
            to_mix -= chunk;
            buf += chunk << hw->shift;
        }
        samples -= decr;
        pcm_hw_dec_live (hw, decr);

    again:
        sdl_unlock (s);
    }
    /* dolog ("done len=%d\n", len); */
}

static void sdl_hw_fini (HWVoice *hw)
{
    ldebug ("sdl_hw_fini %d fixed=%d\n",
             glob_sdl.initialized, audio_conf.fixed_format);
    sdl_close (&glob_sdl);
}

static int sdl_hw_init (HWVoice *hw, int freq, int nchannels, audfmt_e fmt)
{
    SDLVoice *sdl = (SDLVoice *) hw;
    SDLAudioState *s = &glob_sdl;
    SDL_AudioSpec req, obt;
    int shift;

    ldebug ("sdl_hw_init %d freq=%d fixed=%d\n",
            s->initialized, freq, audio_conf.fixed_format);

    if (nchannels != 2) {
        dolog ("Bogus channel count %d\n", nchannels);
        return -1;
    }

    req.freq = freq;
    req.format = AUD_to_sdlfmt (fmt, &shift);
    req.channels = nchannels;
    req.samples = conf.nb_samples;
    shift <<= nchannels == 2;

    req.callback = sdl_callback;
    req.userdata = sdl;

    if (sdl_open (&req, &obt))
        return -1;

    hw->freq = obt.freq;
    hw->fmt = sdl_to_audfmt (obt.format);
    hw->nchannels = obt.channels;
    hw->bufsize = obt.samples << shift;

    s->initialized = 1;
    s->exit = 0;
    SDL_PauseAudio (0);
    return 0;
}

static int sdl_hw_ctl (HWVoice *hw, int cmd, ...)
{
    (void) hw;

    switch (cmd) {
    case VOICE_ENABLE:
        SDL_PauseAudio (0);
        break;

    case VOICE_DISABLE:
        SDL_PauseAudio (1);
        break;
    }
    return 0;
}

static void *sdl_audio_init (void)
{
    SDLAudioState *s = &glob_sdl;
    conf.nb_samples = audio_get_conf_int (QC_SDL_SAMPLES, conf.nb_samples);

    if (SDL_InitSubSystem (SDL_INIT_AUDIO)) {
        dolog ("SDL failed to initialize audio subsystem\nReason: %s\n",
               errstr ());
        return NULL;
    }

    s->mutex = SDL_CreateMutex ();
    if (!s->mutex) {
        dolog ("Failed to create SDL mutex\nReason: %s\n", errstr ());
        SDL_QuitSubSystem (SDL_INIT_AUDIO);
        return NULL;
    }

    s->sem = SDL_CreateSemaphore (0);
    if (!s->sem) {
        dolog ("Failed to create SDL semaphore\nReason: %s\n", errstr ());
        SDL_DestroyMutex (s->mutex);
        SDL_QuitSubSystem (SDL_INIT_AUDIO);
        return NULL;
    }

    return s;
}

static void sdl_audio_fini (void *opaque)
{
    SDLAudioState *s = opaque;
    sdl_close (s);
    SDL_DestroySemaphore (s->sem);
    SDL_DestroyMutex (s->mutex);
    SDL_QuitSubSystem (SDL_INIT_AUDIO);
}

struct pcm_ops sdl_pcm_ops = {
    sdl_hw_init,
    sdl_hw_fini,
    sdl_hw_run,
    sdl_hw_write,
    sdl_hw_ctl
};

struct audio_output_driver sdl_output_driver = {
    "sdl",
    sdl_audio_init,
    sdl_audio_fini,
    &sdl_pcm_ops,
    1,
    1,
    sizeof (SDLVoice)
};

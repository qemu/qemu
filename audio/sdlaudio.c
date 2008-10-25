/*
 * QEMU SDL audio driver
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
#include <SDL.h>
#include <SDL_thread.h>
#include "qemu-common.h"
#include "audio.h"

#ifndef _WIN32
#ifdef __sun__
#define _POSIX_PTHREAD_SEMANTICS 1
#elif defined(__OpenBSD__)
#include <pthread.h>
#endif
#include <signal.h>
#endif

#define AUDIO_CAP "sdl"
#include "audio_int.h"

typedef struct SDLVoiceOut {
    HWVoiceOut hw;
    int live;
    int rpos;
    int decr;
} SDLVoiceOut;

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

static void GCC_FMT_ATTR (1, 2) sdl_logerr (const char *fmt, ...)
{
    va_list ap;

    va_start (ap, fmt);
    AUD_vlog (AUDIO_CAP, fmt, ap);
    va_end (ap);

    AUD_log (AUDIO_CAP, "Reason: %s\n", SDL_GetError ());
}

static int sdl_lock (SDLAudioState *s, const char *forfn)
{
    if (SDL_LockMutex (s->mutex)) {
        sdl_logerr ("SDL_LockMutex for %s failed\n", forfn);
        return -1;
    }
    return 0;
}

static int sdl_unlock (SDLAudioState *s, const char *forfn)
{
    if (SDL_UnlockMutex (s->mutex)) {
        sdl_logerr ("SDL_UnlockMutex for %s failed\n", forfn);
        return -1;
    }
    return 0;
}

static int sdl_post (SDLAudioState *s, const char *forfn)
{
    if (SDL_SemPost (s->sem)) {
        sdl_logerr ("SDL_SemPost for %s failed\n", forfn);
        return -1;
    }
    return 0;
}

static int sdl_wait (SDLAudioState *s, const char *forfn)
{
    if (SDL_SemWait (s->sem)) {
        sdl_logerr ("SDL_SemWait for %s failed\n", forfn);
        return -1;
    }
    return 0;
}

static int sdl_unlock_and_post (SDLAudioState *s, const char *forfn)
{
    if (sdl_unlock (s, forfn)) {
        return -1;
    }

    return sdl_post (s, forfn);
}

static int aud_to_sdlfmt (audfmt_e fmt, int *shift)
{
    switch (fmt) {
    case AUD_FMT_S8:
        *shift = 0;
        return AUDIO_S8;

    case AUD_FMT_U8:
        *shift = 0;
        return AUDIO_U8;

    case AUD_FMT_S16:
        *shift = 1;
        return AUDIO_S16LSB;

    case AUD_FMT_U16:
        *shift = 1;
        return AUDIO_U16LSB;

    default:
        dolog ("Internal logic error: Bad audio format %d\n", fmt);
#ifdef DEBUG_AUDIO
        abort ();
#endif
        return AUDIO_U8;
    }
}

static int sdl_to_audfmt (int sdlfmt, audfmt_e *fmt, int *endianess)
{
    switch (sdlfmt) {
    case AUDIO_S8:
        *endianess = 0;
        *fmt = AUD_FMT_S8;
        break;

    case AUDIO_U8:
        *endianess = 0;
        *fmt = AUD_FMT_U8;
        break;

    case AUDIO_S16LSB:
        *endianess = 0;
        *fmt = AUD_FMT_S16;
        break;

    case AUDIO_U16LSB:
        *endianess = 0;
        *fmt = AUD_FMT_U16;
        break;

    case AUDIO_S16MSB:
        *endianess = 1;
        *fmt = AUD_FMT_S16;
        break;

    case AUDIO_U16MSB:
        *endianess = 1;
        *fmt = AUD_FMT_U16;
        break;

    default:
        dolog ("Unrecognized SDL audio format %d\n", sdlfmt);
        return -1;
    }

    return 0;
}

static int sdl_open (SDL_AudioSpec *req, SDL_AudioSpec *obt)
{
    int status;
#ifndef _WIN32
    sigset_t new, old;

    /* Make sure potential threads created by SDL don't hog signals.  */
    sigfillset (&new);
    pthread_sigmask (SIG_BLOCK, &new, &old);
#endif

    status = SDL_OpenAudio (req, obt);
    if (status) {
        sdl_logerr ("SDL_OpenAudio failed\n");
    }

#ifndef _WIN32
    pthread_sigmask (SIG_SETMASK, &old, 0);
#endif
    return status;
}

static void sdl_close (SDLAudioState *s)
{
    if (s->initialized) {
        sdl_lock (s, "sdl_close");
        s->exit = 1;
        sdl_unlock_and_post (s, "sdl_close");
        SDL_PauseAudio (1);
        SDL_CloseAudio ();
        s->initialized = 0;
    }
}

static void sdl_callback (void *opaque, Uint8 *buf, int len)
{
    SDLVoiceOut *sdl = opaque;
    SDLAudioState *s = &glob_sdl;
    HWVoiceOut *hw = &sdl->hw;
    int samples = len >> hw->info.shift;

    if (s->exit) {
        return;
    }

    while (samples) {
        int to_mix, decr;

        /* dolog ("in callback samples=%d\n", samples); */
        sdl_wait (s, "sdl_callback");
        if (s->exit) {
            return;
        }

        if (sdl_lock (s, "sdl_callback")) {
            return;
        }

        if (audio_bug (AUDIO_FUNC, sdl->live < 0 || sdl->live > hw->samples)) {
            dolog ("sdl->live=%d hw->samples=%d\n",
                   sdl->live, hw->samples);
            return;
        }

        if (!sdl->live) {
            goto again;
        }

        /* dolog ("in callback live=%d\n", live); */
        to_mix = audio_MIN (samples, sdl->live);
        decr = to_mix;
        while (to_mix) {
            int chunk = audio_MIN (to_mix, hw->samples - hw->rpos);
            st_sample_t *src = hw->mix_buf + hw->rpos;

            /* dolog ("in callback to_mix %d, chunk %d\n", to_mix, chunk); */
            hw->clip (buf, src, chunk);
            sdl->rpos = (sdl->rpos + chunk) % hw->samples;
            to_mix -= chunk;
            buf += chunk << hw->info.shift;
        }
        samples -= decr;
        sdl->live -= decr;
        sdl->decr += decr;

    again:
        if (sdl_unlock (s, "sdl_callback")) {
            return;
        }
    }
    /* dolog ("done len=%d\n", len); */
}

static int sdl_write_out (SWVoiceOut *sw, void *buf, int len)
{
    return audio_pcm_sw_write (sw, buf, len);
}

static int sdl_run_out (HWVoiceOut *hw)
{
    int decr, live;
    SDLVoiceOut *sdl = (SDLVoiceOut *) hw;
    SDLAudioState *s = &glob_sdl;

    if (sdl_lock (s, "sdl_callback")) {
        return 0;
    }

    live = audio_pcm_hw_get_live_out (hw);

    if (sdl->decr > live) {
        ldebug ("sdl->decr %d live %d sdl->live %d\n",
                sdl->decr,
                live,
                sdl->live);
    }

    decr = audio_MIN (sdl->decr, live);
    sdl->decr -= decr;

    sdl->live = live - decr;
    hw->rpos = sdl->rpos;

    if (sdl->live > 0) {
        sdl_unlock_and_post (s, "sdl_callback");
    }
    else {
        sdl_unlock (s, "sdl_callback");
    }
    return decr;
}

static void sdl_fini_out (HWVoiceOut *hw)
{
    (void) hw;

    sdl_close (&glob_sdl);
}

static int sdl_init_out (HWVoiceOut *hw, audsettings_t *as)
{
    SDLVoiceOut *sdl = (SDLVoiceOut *) hw;
    SDLAudioState *s = &glob_sdl;
    SDL_AudioSpec req, obt;
    int shift;
    int endianess;
    int err;
    audfmt_e effective_fmt;
    audsettings_t obt_as;

    shift <<= as->nchannels == 2;

    req.freq = as->freq;
    req.format = aud_to_sdlfmt (as->fmt, &shift);
    req.channels = as->nchannels;
    req.samples = conf.nb_samples;
    req.callback = sdl_callback;
    req.userdata = sdl;

    if (sdl_open (&req, &obt)) {
        return -1;
    }

    err = sdl_to_audfmt (obt.format, &effective_fmt, &endianess);
    if (err) {
        sdl_close (s);
        return -1;
    }

    obt_as.freq = obt.freq;
    obt_as.nchannels = obt.channels;
    obt_as.fmt = effective_fmt;
    obt_as.endianness = endianess;

    audio_pcm_init_info (&hw->info, &obt_as);
    hw->samples = obt.samples;

    s->initialized = 1;
    s->exit = 0;
    SDL_PauseAudio (0);
    return 0;
}

static int sdl_ctl_out (HWVoiceOut *hw, int cmd, ...)
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

    if (SDL_InitSubSystem (SDL_INIT_AUDIO)) {
        sdl_logerr ("SDL failed to initialize audio subsystem\n");
        return NULL;
    }

    s->mutex = SDL_CreateMutex ();
    if (!s->mutex) {
        sdl_logerr ("Failed to create SDL mutex\n");
        SDL_QuitSubSystem (SDL_INIT_AUDIO);
        return NULL;
    }

    s->sem = SDL_CreateSemaphore (0);
    if (!s->sem) {
        sdl_logerr ("Failed to create SDL semaphore\n");
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

static struct audio_option sdl_options[] = {
    {"SAMPLES", AUD_OPT_INT, &conf.nb_samples,
     "Size of SDL buffer in samples", NULL, 0},
    {NULL, 0, NULL, NULL, NULL, 0}
};

static struct audio_pcm_ops sdl_pcm_ops = {
    sdl_init_out,
    sdl_fini_out,
    sdl_run_out,
    sdl_write_out,
    sdl_ctl_out,

    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

struct audio_driver sdl_audio_driver = {
    INIT_FIELD (name           = ) "sdl",
    INIT_FIELD (descr          = ) "SDL http://www.libsdl.org",
    INIT_FIELD (options        = ) sdl_options,
    INIT_FIELD (init           = ) sdl_audio_init,
    INIT_FIELD (fini           = ) sdl_audio_fini,
    INIT_FIELD (pcm_ops        = ) &sdl_pcm_ops,
    INIT_FIELD (can_be_default = ) 1,
    INIT_FIELD (max_voices_out = ) 1,
    INIT_FIELD (max_voices_in  = ) 0,
    INIT_FIELD (voice_size_out = ) sizeof (SDLVoiceOut),
    INIT_FIELD (voice_size_in  = ) 0
};

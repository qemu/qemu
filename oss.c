/*
 * QEMU OSS Audio output driver
 * 
 * Copyright (c) 2003 Vassili Karpov (malc)
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

#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* TODO: Graceful error handling */

#if defined(_WIN32)
#define USE_SDL_AUDIO
#endif

#define MIN(a, b) ((a)>(b)?(b):(a))
#define MAX(a, b) ((a)<(b)?(b):(a))

#define DEREF(x) (void)x
#define dolog(...) fprintf (stderr, "audio: " __VA_ARGS__)
#define ERRFail(...) do {                                               \
    int _errno = errno;                                                 \
    fprintf (stderr, "audio: " __VA_ARGS__);                            \
    fprintf (stderr, "\nsystem error: %s\n", strerror (_errno));        \
    abort ();                                                           \
} while (0)
#define Fail(...) do {                          \
    fprintf (stderr, "audio: " __VA_ARGS__);    \
    fprintf (stderr, "\n");                     \
    abort ();                                   \
} while (0)

#ifdef DEBUG_AUDIO
#define lwarn(...) fprintf (stderr, "audio: " __VA_ARGS__)
#define linfo(...) fprintf (stderr, "audio: " __VA_ARGS__)
#define ldebug(...) fprintf (stderr, "audio: " __VA_ARGS__)
#else
#define lwarn(...)
#define linfo(...)
#define ldebug(...)
#endif

static int get_conf_val (const char *key, int defval)
{
    int val = defval;
    char *strval;

    strval = getenv (key);
    if (strval) {
        val = atoi (strval);
    }

    return val;
}

static void copy_no_conversion (void *dst, void *src, int size)
{
    memcpy (dst, src, size);
}

static void copy_u16_to_s16 (void *dst, void *src, int size)
{
    int i;
    uint16_t *out, *in;

    out = dst;
    in = src;

    for (i = 0; i < size / 2; i++) {
        out[i] = in[i] + 0x8000;
    }
}

#ifdef USE_SDL_AUDIO
#include <SDL/SDL.h>
#include <SDL/SDL_thread.h>

static struct {
    int samples;
} conf = {
    .samples = 4096
};

typedef struct AudioState {
    int freq;
    int bits16;
    int nchannels;
    int rpos;
    int wpos;
    volatile int live;
    volatile int exit;
    int bytes_per_second;
    Uint8 *buf;
    int bufsize;
    int leftover;
    uint64_t old_ticks;
    SDL_AudioSpec spec;
    SDL_mutex *mutex;
    SDL_sem *sem;
    void (*copy_fn)(void *, void *, int);
} AudioState;

static AudioState sdl_audio;

void AUD_run (void)
{
}

static void own (AudioState *s)
{
    /* SDL_LockAudio (); */
    if (SDL_mutexP (s->mutex))
        dolog ("SDL_mutexP: %s\n", SDL_GetError ());
}

static void disown (AudioState *s)
{
    /* SDL_UnlockAudio (); */
    if (SDL_mutexV (s->mutex))
        dolog ("SDL_mutexV: %s\n", SDL_GetError ());
}

static void sem_wait (AudioState *s)
{
    if (SDL_SemWait (s->sem))
        dolog ("SDL_SemWait: %s\n", SDL_GetError ());
}

static void sem_post (AudioState *s)
{
    if (SDL_SemPost (s->sem))
        dolog ("SDL_SemPost: %s\n", SDL_GetError ());
}

static void audio_callback (void *data, Uint8 *stream, int len)
{
    int to_mix;
    AudioState *s = data;

    if (s->exit) return;
    while (len) {
        sem_wait (s);
        if (s->exit) return;
        own (s);
        to_mix = MIN (len, s->live);
        len -= to_mix;
        /* printf ("to_mix=%d len=%d live=%d\n", to_mix, len, s->live); */
        while (to_mix) {
            int chunk = MIN (to_mix, s->bufsize - s->rpos);
            /* SDL_MixAudio (stream, buf, chunk, SDL_MIX_MAXVOLUME); */
            memcpy (stream, s->buf + s->rpos, chunk);

            s->rpos += chunk;
            s->live -= chunk;

            stream += chunk;
            to_mix -= chunk;

            if (s->rpos == s->bufsize) s->rpos = 0;
        }
        disown (s);
    }
}

static void sem_zero (AudioState *s)
{
    int res;

    do {
        res = SDL_SemTryWait (s->sem);
        if (res < 0) {
            dolog ("SDL_SemTryWait: %s\n", SDL_GetError ());
            return;
        }
    } while (res != SDL_MUTEX_TIMEDOUT);
}

static void do_open (AudioState *s)
{
    int status;
    SDL_AudioSpec obtained;

    SDL_PauseAudio (1);
    if (s->buf) {
        s->exit = 1;
        sem_post (s);
        SDL_CloseAudio ();
        s->exit = 0;
        qemu_free (s->buf);
        s->buf = NULL;
        sem_zero (s);
    }

    s->bytes_per_second = (s->spec.freq << (s->spec.channels >> 1)) << s->bits16;
    s->spec.samples = conf.samples;
    s->spec.userdata = s;
    s->spec.callback = audio_callback;

    status = SDL_OpenAudio (&s->spec, &obtained);
    if (status < 0) {
        dolog ("SDL_OpenAudio: %s\n", SDL_GetError ());
        goto exit;
    }

    if (obtained.freq != s->spec.freq ||
        obtained.channels != s->spec.channels ||
        obtained.format != s->spec.format) {
        dolog ("Audio spec mismatch requested obtained\n"
               "freq                %5d    %5d\n"
               "channels            %5d    %5d\n"
               "fmt                 %5d    %5d\n",
               s->spec.freq, obtained.freq,
               s->spec.channels, obtained.channels,
               s->spec.format, obtained.format
            );
    }

    s->bufsize = obtained.size;
    s->buf = qemu_mallocz (s->bufsize);
    if (!s->buf) {
        dolog ("qemu_mallocz(%d)\n", s->bufsize);
        goto exit;
    }
    SDL_PauseAudio (0);

exit:
    s->rpos = 0;
    s->wpos = 0;
    s->live = 0;
}

int AUD_write (void *in_buf, int size)
{
    AudioState *s = &sdl_audio;
    int to_copy, temp;
    uint8_t *in, *out;

    own (s);
    to_copy = MIN (s->bufsize - s->live, size);

    temp = to_copy;

    in = in_buf;
    out = s->buf;

    while (temp) {
        int copy;

        copy = MIN (temp, s->bufsize - s->wpos);
        s->copy_fn (out + s->wpos, in, copy);

        s->wpos += copy;
        if (s->wpos == s->bufsize) {
            s->wpos = 0;
        }

        temp -= copy;
        in += copy;
        s->live += copy;
    }

    disown (s);
    sem_post (s);
    return to_copy;
}

static void maybe_open (AudioState *s, int req_freq, int req_nchannels,
                        audfmt_e req_fmt, int force_open)
{
    int sdl_fmt, bits16;

    switch (req_fmt) {
    case AUD_FMT_U8:
        bits16 = 0;
        sdl_fmt = AUDIO_U8;
        s->copy_fn = copy_no_conversion;
        break;

    case AUD_FMT_S8:
        fprintf (stderr, "audio: can not play 8bit signed\n");
        return;

    case AUD_FMT_S16:
        bits16 = 1;
        sdl_fmt = AUDIO_S16;
        s->copy_fn = copy_no_conversion;
        break;

    case AUD_FMT_U16:
        bits16 = 1;
        sdl_fmt = AUDIO_S16;
        s->copy_fn = copy_u16_to_s16;
        break;

    default:
        abort ();
    }

    if (force_open
        || (NULL == s->buf)
        || (sdl_fmt != s->spec.format)
        || (req_nchannels != s->spec.channels)
        || (req_freq != s->spec.freq)
        || (bits16 != s->bits16)) {

        s->spec.format = sdl_fmt;
        s->spec.channels = req_nchannels;
        s->spec.freq = req_freq;
        s->bits16 = bits16;
        do_open (s);
    }
}

void AUD_reset (int req_freq, int req_nchannels, audfmt_e req_fmt)
{
    AudioState *s = &sdl_audio;
    own (s);
    maybe_open (s, req_freq, req_nchannels, req_fmt, 0);
    disown (s);
}

void AUD_open (int req_freq, int req_nchannels, audfmt_e req_fmt)
{
    AudioState *s = &sdl_audio;
    own (s);
    maybe_open (s, req_freq, req_nchannels, req_fmt, 1);
    disown (s);
}

void AUD_adjust_estimate (int leftover)
{
    AudioState *s = &sdl_audio;
    own (s);
    s->leftover = leftover;
    disown (s);
}

int AUD_get_free (void)
{
    int free, elapsed;
    uint64_t ticks, delta;
    uint64_t ua_elapsed;
    uint64_t al_elapsed;
    AudioState *s = &sdl_audio;

    own (s);
    free = s->bufsize - s->live;

    if (0 == free) {
        disown (s);
        return 0;
    }

    elapsed = free;
    ticks = qemu_get_clock(rt_clock);
    delta = ticks - s->old_ticks;
    s->old_ticks = ticks;

    ua_elapsed = (delta * s->bytes_per_second) / 1000;
    al_elapsed = ua_elapsed & ~3ULL;

    ldebug ("tid elapsed %llu bytes\n", ua_elapsed);

    if (al_elapsed > (uint64_t) INT_MAX)
        elapsed = INT_MAX;
    else
        elapsed = al_elapsed;

    elapsed += s->leftover;
    disown (s);

    if (elapsed > free) {
        lwarn ("audio can not keep up elapsed %d free %d\n", elapsed, free);
        return free;
    }
    else {
        return elapsed;
    }
}

int AUD_get_live (void)
{
    int live;
    AudioState *s = &sdl_audio;

    own (s);
    live = s->live;
    disown (s);
    return live;
}

int AUD_get_buffer_size (void)
{
    int bufsize;
    AudioState *s = &sdl_audio;

    own (s);
    bufsize = s->bufsize;
    disown (s);
    return bufsize;
}

#define QC_SDL_NSAMPLES "QEMU_SDL_NSAMPLES"

static void cleanup (void)
{
    AudioState *s = &sdl_audio;
    own (s);
    s->exit = 1;
    sem_post (s);
    disown (s);
}

void AUD_init (void)
{
    AudioState *s = &sdl_audio;

    atexit (cleanup);
    SDL_InitSubSystem (SDL_INIT_AUDIO);
    s->mutex = SDL_CreateMutex ();
    if (!s->mutex) {
        dolog ("SDL_CreateMutex: %s\n", SDL_GetError ());
        return;
    }

    s->sem = SDL_CreateSemaphore (0);
    if (!s->sem) {
        dolog ("SDL_CreateSemaphore: %s\n", SDL_GetError ());
        return;
    }

    conf.samples = get_conf_val (QC_SDL_NSAMPLES, conf.samples);
}

#elif !defined(_WIN32) && !defined(__APPLE__)

#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>

/* http://www.df.lth.se/~john_e/gems/gem002d.html */
/* http://www.multi-platforms.com/Tips/PopCount.htm */
static inline uint32_t popcount (uint32_t u)
{
  u = ((u&0x55555555) + ((u>>1)&0x55555555));
  u = ((u&0x33333333) + ((u>>2)&0x33333333));
  u = ((u&0x0f0f0f0f) + ((u>>4)&0x0f0f0f0f));
  u = ((u&0x00ff00ff) + ((u>>8)&0x00ff00ff));
  u = ( u&0x0000ffff) + (u>>16);
  return u;
}

static inline uint32_t lsbindex (uint32_t u)
{
  return popcount ((u&-u)-1);
}

#define IOCTL(args) do {                        \
  int ret = ioctl args;                         \
  if (-1 == ret) {                              \
    ERRFail (#args);                            \
  }                                             \
  ldebug ("ioctl " #args " = %d\n", ret);       \
} while (0)

typedef struct AudioState {
    int fd;
    int freq;
    int bits16;
    int nchannels;
    int rpos;
    int wpos;
    int live;
    int oss_fmt;
    int bytes_per_second;
    int is_mapped;
    void *buf;
    int bufsize;
    int nfrags;
    int fragsize;
    int old_optr;
    int leftover;
    uint64_t old_ticks;
    void (*copy_fn)(void *, void *, int);
} AudioState;

static AudioState oss_audio = { .fd = -1 };

static struct {
    int try_mmap;
    int nfrags;
    int fragsize;
} conf = {
    .try_mmap = 0,
    .nfrags = 4,
    .fragsize = 4096
};

static enum {DONT, DSP, TID} est = DONT;

static void pab (AudioState *s, struct audio_buf_info *abinfo)
{
    DEREF (abinfo);

    ldebug ("fragments %d, fragstotal %d, fragsize %d, bytes %d\n"
            "rpos %d, wpos %d, live %d\n",
            abinfo->fragments,
            abinfo->fragstotal,
            abinfo->fragsize,
            abinfo->bytes,
            s->rpos, s->wpos, s->live);
}

static void do_open (AudioState *s)
{
    int mmmmssss;
    audio_buf_info abinfo;
    int fmt, freq, nchannels;

    if (s->buf) {
        if (s->is_mapped) {
            if (-1 == munmap (s->buf, s->bufsize)) {
            ERRFail ("failed to unmap audio buffer %p %d",
                         s->buf, s->bufsize);
        }
        }
        else {
            qemu_free (s->buf);
        }
        s->buf = NULL;
    }

    if (-1 != s->fd)
        close (s->fd);

    s->fd = open ("/dev/dsp", O_RDWR | O_NONBLOCK);
    if (-1 == s->fd) {
        ERRFail ("can not open /dev/dsp");
    }

    fmt = s->oss_fmt;
    freq = s->freq;
    nchannels = s->nchannels;

    IOCTL ((s->fd, SNDCTL_DSP_RESET, 1));
    IOCTL ((s->fd, SNDCTL_DSP_SAMPLESIZE, &fmt));
    IOCTL ((s->fd, SNDCTL_DSP_CHANNELS, &nchannels));
    IOCTL ((s->fd, SNDCTL_DSP_SPEED, &freq));
    IOCTL ((s->fd, SNDCTL_DSP_NONBLOCK));

    mmmmssss = (conf.nfrags << 16) | conf.fragsize;
    IOCTL ((s->fd, SNDCTL_DSP_SETFRAGMENT, &mmmmssss));

    if ((s->oss_fmt != fmt)
        || (s->nchannels != nchannels)
        || (s->freq != freq)) {
        Fail ("failed to set audio parameters\n"
              "parameter | requested value | obtained value\n"
              "format    |      %10d |     %10d\n"
              "channels  |      %10d |     %10d\n"
              "frequency |      %10d |     %10d\n",
              s->oss_fmt, fmt,
              s->nchannels, nchannels,
              s->freq, freq);
    }

    IOCTL ((s->fd, SNDCTL_DSP_GETOSPACE, &abinfo));

    s->nfrags = abinfo.fragstotal;
    s->fragsize = abinfo.fragsize;
    s->bufsize = s->nfrags * s->fragsize;
    s->old_optr = 0;

    s->bytes_per_second = (freq << (nchannels >> 1)) << s->bits16;

    linfo ("bytes per second %d\n", s->bytes_per_second);

    linfo ("fragments %d, fragstotal %d, fragsize %d, bytes %d, bufsize %d\n",
           abinfo.fragments,
           abinfo.fragstotal,
           abinfo.fragsize,
           abinfo.bytes,
           s->bufsize);

    s->buf = MAP_FAILED;
    s->is_mapped = 0;

    if (conf.try_mmap) {
        s->buf = mmap (NULL, s->bufsize, PROT_WRITE, MAP_SHARED, s->fd, 0);
        if (MAP_FAILED == s->buf) {
            int err;

            err = errno;
            dolog ("failed to mmap audio, size %d, fd %d\n"
                 "syserr: %s\n",
                   s->bufsize, s->fd, strerror (err));
        }
    else {
            est = TID;
            s->is_mapped = 1;
        }
    }

    if (MAP_FAILED == s->buf) {
        est = TID;
        s->buf = qemu_mallocz (s->bufsize);
        if (!s->buf) {
            ERRFail ("audio buf malloc failed, size %d", s->bufsize);
        }
    }

    s->rpos = 0;
    s->wpos = 0;
    s->live = 0;

    if (s->is_mapped) {
        int trig;

        trig = 0;
        IOCTL ((s->fd, SNDCTL_DSP_SETTRIGGER, &trig));
        trig = PCM_ENABLE_OUTPUT;
        IOCTL ((s->fd, SNDCTL_DSP_SETTRIGGER, &trig));
    }
}

static void maybe_open (AudioState *s, int req_freq, int req_nchannels,
                        audfmt_e req_fmt, int force_open)
{
    int oss_fmt, bits16;

    switch (req_fmt) {
    case AUD_FMT_U8:
        bits16 = 0;
        oss_fmt = AFMT_U8;
        s->copy_fn = copy_no_conversion;
        break;

    case AUD_FMT_S8:
        Fail ("can not play 8bit signed");

    case AUD_FMT_S16:
        bits16 = 1;
        oss_fmt = AFMT_S16_LE;
        s->copy_fn = copy_no_conversion;
        break;

    case AUD_FMT_U16:
        bits16 = 1;
        oss_fmt = AFMT_S16_LE;
        s->copy_fn = copy_u16_to_s16;
        break;

    default:
        abort ();
    }

    if (force_open
        || (-1 == s->fd)
        || (oss_fmt != s->oss_fmt)
        || (req_nchannels != s->nchannels)
        || (req_freq != s->freq)
        || (bits16 != s->bits16)) {
        s->oss_fmt = oss_fmt;
        s->nchannels = req_nchannels;
        s->freq = req_freq;
        s->bits16 = bits16;
        do_open (s);
    }
}

void AUD_reset (int req_freq, int req_nchannels, audfmt_e req_fmt)
{
    AudioState *s = &oss_audio;
    maybe_open (s, req_freq, req_nchannels, req_fmt, 0);
}

void AUD_open (int req_freq, int req_nchannels, audfmt_e req_fmt)
{
    AudioState *s = &oss_audio;
    maybe_open (s, req_freq, req_nchannels, req_fmt, 1);
}

int AUD_write (void *in_buf, int size)
{
    AudioState *s = &oss_audio;
    int to_copy, temp;
    uint8_t *in, *out;

    to_copy = MIN (s->bufsize - s->live, size);

    temp = to_copy;

    in = in_buf;
    out = s->buf;

    while (temp) {
        int copy;

        copy = MIN (temp, s->bufsize - s->wpos);
        s->copy_fn (out + s->wpos, in, copy);

        s->wpos += copy;
        if (s->wpos == s->bufsize) {
            s->wpos = 0;
        }

        temp -= copy;
        in += copy;
        s->live += copy;
    }

    return to_copy;
}

void AUD_run (void)
{
    int res;
    int bytes;
    struct audio_buf_info abinfo;
    AudioState *s = &oss_audio;

    if (0 == s->live)
        return;

    if (s->is_mapped) {
        count_info info;

        res = ioctl (s->fd, SNDCTL_DSP_GETOPTR, &info);
        if (res < 0) {
            int err;

            err = errno;
            lwarn ("SNDCTL_DSP_GETOPTR failed with %s\n", strerror (err));
            return;
        }

        if (info.ptr > s->old_optr) {
            bytes = info.ptr - s->old_optr;
        }
        else {
            bytes = s->bufsize + info.ptr - s->old_optr;
        }

        s->old_optr = info.ptr;
        s->live -= bytes;
        return;
    }

    res = ioctl (s->fd, SNDCTL_DSP_GETOSPACE, &abinfo);

    if (res < 0) {
        int err;

        err = errno;
        lwarn ("SNDCTL_DSP_GETOSPACE failed with %s\n", strerror (err));
        return;
    }

    bytes = abinfo.bytes;
    bytes = MIN (s->live, bytes);
#if 0
    bytes = (bytes / fragsize) * fragsize;
#endif

    while (bytes) {
        int left, play, written;

        left = s->bufsize - s->rpos;
        play = MIN (left, bytes);
        written = write (s->fd, (uint8_t *)s->buf + s->rpos, play);

        if (-1 == written) {
            if (EAGAIN == errno || EINTR == errno) {
                return;
            }
            else {
                ERRFail ("write audio");
            }
        }

        play = written;
        s->live -= play;
        s->rpos += play;
        bytes -= play;

        if (s->rpos == s->bufsize) {
            s->rpos = 0;
        }
    }
}

static int get_dsp_bytes (void)
{
    int res;
    struct count_info info;
    AudioState *s = &oss_audio;

    res = ioctl (s->fd, SNDCTL_DSP_GETOPTR, &info);
    if (-1 == res) {
        int err;

        err = errno;
        lwarn ("SNDCTL_DSP_GETOPTR failed with %s\n", strerror (err));
        return -1;
    }
    else {
        ldebug ("bytes %d\n", info.bytes);
        return info.bytes;
    }
}

void AUD_adjust_estimate (int leftover)
{
    AudioState *s = &oss_audio;
    s->leftover = leftover;
}

int AUD_get_free (void)
{
    int free, elapsed;
    AudioState *s = &oss_audio;

    free = s->bufsize - s->live;

    if (free <= 0)
        return 0;

    elapsed = free;
    switch (est) {
    case DONT:
        break;

    case DSP:
        {
            static int old_bytes;
            int bytes;

            bytes = get_dsp_bytes ();
            if (bytes <= 0)
                return free;

            elapsed = bytes - old_bytes;
            old_bytes = bytes;
            ldebug ("dsp elapsed %d bytes\n", elapsed);
            break;
        }

    case TID:
        {
            uint64_t ticks, delta;
            uint64_t ua_elapsed;
            uint64_t al_elapsed;

            ticks = qemu_get_clock(rt_clock);
            delta = ticks - s->old_ticks;
            s->old_ticks = ticks;

            ua_elapsed = (delta * s->bytes_per_second) / 1000;
            al_elapsed = ua_elapsed & ~3ULL;

            ldebug ("tid elapsed %llu bytes\n", ua_elapsed);

            if (al_elapsed > (uint64_t) INT_MAX)
                elapsed = INT_MAX;
            else
                elapsed = al_elapsed;

            elapsed += s->leftover;
        }
    }

    if (elapsed > free) {
        lwarn ("audio can not keep up elapsed %d free %d\n", elapsed, free);
        return free;
    }
    else {
        return elapsed;
    }
}

int AUD_get_live (void)
{
    AudioState *s = &oss_audio;
    return s->live;
}

int AUD_get_buffer_size (void)
{
    AudioState *s = &oss_audio;
    return s->bufsize;
}

#define QC_OSS_FRAGSIZE "QEMU_OSS_FRAGSIZE"
#define QC_OSS_NFRAGS "QEMU_OSS_NFRAGS"
#define QC_OSS_MMAP "QEMU_OSS_MMAP"

void AUD_init (void)
{
    int fsp;

    DEREF (pab);

    conf.fragsize = get_conf_val (QC_OSS_FRAGSIZE, conf.fragsize);
    conf.nfrags = get_conf_val (QC_OSS_NFRAGS, conf.nfrags);
    conf.try_mmap = get_conf_val (QC_OSS_MMAP, conf.try_mmap);

    fsp = conf.fragsize;
    if (0 != (fsp & (fsp - 1))) {
        Fail ("fragment size %d is not power of 2", fsp);
    }

    conf.fragsize = lsbindex (fsp);
}

#else

void AUD_run (void)
{
}

int AUD_write (void *in_buf, int size)
{
    return 0;
}

void AUD_reset (int rfreq, int rnchannels, audfmt_e rfmt)
{
}

void AUD_adjust_estimate (int _leftover)
{
}

int AUD_get_free (void)
{
    return 0;
}

int AUD_get_live (void)
{
    return 0;
}

int AUD_get_buffer_size (void)
{
    return 0;
}

void AUD_init (void)
{
}

#endif

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

#if !defined(_WIN32) && !defined(__APPLE__)
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <inttypes.h>
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

#define MIN(a, b) ((a)>(b)?(b):(a))
#define MAX(a, b) ((a)<(b)?(b):(a))

#define DEREF(x) (void)x
#define log(...) fprintf (stderr, "oss: " __VA_ARGS__)
#define ERRFail(...) do {                                       \
    int _errno = errno;                                         \
    fprintf (stderr, "oss: " __VA_ARGS__);                      \
    fprintf (stderr, "system error: %s\n", strerror (_errno));  \
    abort ();                                                   \
} while (0)
#define Fail(...) do {                          \
    fprintf (stderr, "oss: " __VA_ARGS__);      \
    fprintf (stderr, "\n");                     \
    abort ();                                   \
} while (0)

#ifdef DEBUG_OSS
#define lwarn(...) fprintf (stderr, "oss: " __VA_ARGS__)
#define linfo(...) fprintf (stderr, "oss: " __VA_ARGS__)
#define ldebug(...) fprintf (stderr, "oss: " __VA_ARGS__)
#else
#define lwarn(...)
#define linfo(...)
#define ldebug(...)
#endif


#define IOCTL(args) do {                        \
  int ret = ioctl args;                         \
  if (-1 == ret) {                              \
    ERRFail (#args);                            \
  }                                             \
  ldebug ("ioctl " #args " = %d\n", ret);       \
} while (0)

static struct {
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
} oss = { .fd = -1 };

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

static void pab (struct audio_buf_info *abinfo)
{
    DEREF (abinfo);

    ldebug ("fragments %d, fragstotal %d, fragsize %d, bytes %d\n"
            "rpos %d, wpos %d, live %d\n",
            abinfo->fragments,
            abinfo->fragstotal,
            abinfo->fragsize,
            abinfo->bytes,
            rpos, wpos, live);
}

static void do_open ()
{
    int mmmmssss;
    audio_buf_info abinfo;
    int fmt, freq, nchannels;

    if (oss.buf) {
        if (-1 == munmap (oss.buf, oss.bufsize)) {
            ERRFail ("failed to unmap audio buffer %p %d",
                     oss.buf, oss.bufsize);
        }
        oss.buf = NULL;
    }

    if (-1 != oss.fd)
        close (oss.fd);

    oss.fd = open ("/dev/dsp", O_RDWR | O_NONBLOCK);
    if (-1 == oss.fd) {
        ERRFail ("can not open /dev/dsp");
    }

    fmt = oss.oss_fmt;
    freq = oss.freq;
    nchannels = oss.nchannels;

    IOCTL ((oss.fd, SNDCTL_DSP_RESET, 1));
    IOCTL ((oss.fd, SNDCTL_DSP_SAMPLESIZE, &fmt));
    IOCTL ((oss.fd, SNDCTL_DSP_CHANNELS, &nchannels));
    IOCTL ((oss.fd, SNDCTL_DSP_SPEED, &freq));
    IOCTL ((oss.fd, SNDCTL_DSP_NONBLOCK));

    mmmmssss = (conf.nfrags << 16) | conf.fragsize;
    IOCTL ((oss.fd, SNDCTL_DSP_SETFRAGMENT, &mmmmssss));

    if ((oss.oss_fmt != fmt)
        || (oss.nchannels != nchannels)
        || (oss.freq != freq)) {
        Fail ("failed to set audio parameters\n"
              "parameter | requested value | obtained value\n"
              "format    |      %10d |     %10d\n"
              "channels  |      %10d |     %10d\n"
              "frequency |      %10d |     %10d\n",
              oss.oss_fmt, fmt,
              oss.nchannels, nchannels,
              oss.freq, freq);
    }

    IOCTL ((oss.fd, SNDCTL_DSP_GETOSPACE, &abinfo));

    oss.nfrags = abinfo.fragstotal;
    oss.fragsize = abinfo.fragsize;
    oss.bufsize = oss.nfrags * oss.fragsize;
    oss.old_optr = 0;

    oss.bytes_per_second = (freq << (nchannels >> 1)) << oss.bits16;

    linfo ("bytes per second %d\n", oss.bytes_per_second);

    linfo ("fragments %d, fragstotal %d, fragsize %d, bytes %d, bufsize %d\n",
           abinfo.fragments,
           abinfo.fragstotal,
           abinfo.fragsize,
           abinfo.bytes,
           oss.bufsize);

    oss.buf = MAP_FAILED;
    oss.is_mapped = 0;

    if (conf.try_mmap) {
        oss.buf = mmap (NULL, oss.bufsize, PROT_WRITE, MAP_SHARED, oss.fd, 0);
        if (MAP_FAILED == oss.buf) {
            int err;

            err = errno;
            log ("failed to mmap audio, size %d, fd %d\n"
                 "syserr: %s\n",
                 oss.bufsize, oss.fd, strerror (err));
        }
    else {
            est = TID;
            oss.is_mapped = 1;
        }
    }

    if (MAP_FAILED == oss.buf) {
        est = TID;
        oss.buf = mmap (NULL, oss.bufsize, PROT_READ | PROT_WRITE,
                        MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        if (MAP_FAILED == oss.buf) {
            ERRFail ("mmap audio buf, size %d", oss.bufsize);
        }
    }

    oss.rpos = 0;
    oss.wpos = 0;
    oss.live = 0;

    if (oss.is_mapped) {
        int trig;

        trig = 0;
        IOCTL ((oss.fd, SNDCTL_DSP_SETTRIGGER, &trig));
        trig = PCM_ENABLE_OUTPUT;
        IOCTL ((oss.fd, SNDCTL_DSP_SETTRIGGER, &trig));
    }
}

static void maybe_open (int req_freq, int req_nchannels,
                        audfmt_e req_fmt, int force_open)
{
    int oss_fmt, bits16;

    switch (req_fmt) {
    case AUD_FMT_U8:
        bits16 = 0;
        oss_fmt = AFMT_U8;
        oss.copy_fn = copy_no_conversion;
        break;

    case AUD_FMT_S8:
        Fail ("can not play 8bit signed");

    case AUD_FMT_S16:
        bits16 = 1;
        oss_fmt = AFMT_S16_LE;
        oss.copy_fn = copy_no_conversion;
        break;

    case AUD_FMT_U16:
        bits16 = 1;
        oss_fmt = AFMT_S16_LE;
        oss.copy_fn = copy_u16_to_s16;
        break;

    default:
        abort ();
    }

    if (force_open
        || (-1 == oss.fd)
        || (oss_fmt != oss.oss_fmt)
        || (req_nchannels != oss.nchannels)
        || (req_freq != oss.freq)
        || (bits16 != oss.bits16)) {
        oss.oss_fmt = oss_fmt;
        oss.nchannels = req_nchannels;
        oss.freq = req_freq;
        oss.bits16 = bits16;
        do_open ();
    }
}

void AUD_reset (int req_freq, int req_nchannels, audfmt_e req_fmt)
{
    maybe_open (req_freq, req_nchannels, req_fmt, 0);
}

void AUD_open (int req_freq, int req_nchannels, audfmt_e req_fmt)
{
    maybe_open (req_freq, req_nchannels, req_fmt, 1);
}

int AUD_write (void *in_buf, int size)
{
    int to_copy, temp;
    uint8_t *in, *out;

    to_copy = MIN (oss.bufsize - oss.live, size);

    temp = to_copy;

    in = in_buf;
    out = oss.buf;

    while (temp) {
        int copy;

        copy = MIN (temp, oss.bufsize - oss.wpos);
        oss.copy_fn (out + oss.wpos, in, copy);

        oss.wpos += copy;
        if (oss.wpos == oss.bufsize) {
            oss.wpos = 0;
        }

        temp -= copy;
        in += copy;
        oss.live += copy;
    }

    return to_copy;
}

void AUD_run (void)
{
    int res;
    int bytes;
    struct audio_buf_info abinfo;

    if (0 == oss.live)
        return;

    if (oss.is_mapped) {
        count_info info;

        res = ioctl (oss.fd, SNDCTL_DSP_GETOPTR, &info);
        if (-1 == res) {
            int err;

            err = errno;
            lwarn ("SNDCTL_DSP_GETOPTR failed with %s\n", strerror (err));
            return;
        }

        if (info.ptr > oss.old_optr) {
            bytes = info.ptr - oss.old_optr;
        }
        else {
            bytes = oss.bufsize + info.ptr - oss.old_optr;
        }

        oss.old_optr = info.ptr;
        oss.live -= bytes;
        return;
    }

    res = ioctl (oss.fd, SNDCTL_DSP_GETOSPACE, &abinfo);

    if (-1 == res) {
        int err;

        err = errno;
        lwarn ("SNDCTL_DSP_GETOSPACE failed with %s\n", strerror (err));
    }

    bytes = abinfo.bytes;
    bytes = MIN (oss.live, bytes);
#if 0
    bytes = (bytes / fragsize) * fragsize;
#endif

    while (bytes) {
        int left, play, written;

        left = oss.bufsize - oss.rpos;
        play = MIN (left, bytes);
        written = write (oss.fd, (void *) ((uint32_t) oss.buf + oss.rpos), play);

        if (-1 == written) {
            if (EAGAIN == errno || EINTR == errno) {
                return;
            }
            else {
                ERRFail ("write audio");
            }
        }

        play = written;
        oss.live -= play;
        oss.rpos += play;
        bytes -= play;

        if (oss.rpos == oss.bufsize) {
            oss.rpos = 0;
        }
    }
}

static int get_dsp_bytes (void)
{
    int res;
    struct count_info info;

    res = ioctl (oss.fd, SNDCTL_DSP_GETOPTR, &info);
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
    oss.leftover = leftover;
}

int AUD_get_free (void)
{
    int free, elapsed;

    free = oss.bufsize - oss.live;

    if (0 == free)
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
            delta = ticks - oss.old_ticks;
            oss.old_ticks = ticks;

            ua_elapsed = (delta * oss.bytes_per_second) / 1000;
            al_elapsed = ua_elapsed & ~3ULL;

            ldebug ("tid elapsed %llu bytes\n", ua_elapsed);

            if (al_elapsed > (uint64_t) INT_MAX)
                elapsed = INT_MAX;
            else
                elapsed = al_elapsed;

            elapsed += oss.leftover;
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
    return oss.live;
}

int AUD_get_buffer_size (void)
{
    return oss.bufsize;
}

#define QC_OSS_FRAGSIZE "QEMU_OSS_FRAGSIZE"
#define QC_OSS_NFRAGS "QEMU_OSS_NFRAGS"
#define QC_OSS_MMAP "QEMU_OSS_MMAP"

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

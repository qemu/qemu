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
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>

#include "vl.h"

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

static int audio_fd = -1;
static int freq;
static int conf_nfrags = 4;
static int conf_fragsize;
static int nfrags;
static int fragsize;
static int bufsize;
static int nchannels;
static int fmt;
static int rpos;
static int wpos;
static int atom;
static int live;
static int leftover;
static int bytes_per_second;
static void *buf;
static enum {DONT, DSP, TID} estimate = TID;

static void (*copy_fn)(void *, void *, int);

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

void AUD_reset (int rfreq, int rnchannels, audfmt_e rfmt)
{
    int fmt_;
    int bits16;

    if (-1 == audio_fd) {
        AUD_open (rfreq, rnchannels, rfmt);
        return;
    }

    switch (rfmt) {
    case AUD_FMT_U8:
        bits16 = 0;
        fmt_ = AFMT_U8;
        copy_fn = copy_no_conversion;
        atom = 1;
        break;

    case AUD_FMT_S8:
        Fail ("can not play 8bit signed");

    case AUD_FMT_S16:
        bits16 = 1;
        fmt_ = AFMT_S16_LE;
        copy_fn = copy_no_conversion;
        atom = 2;
        break;

    case AUD_FMT_U16:
        bits16 = 1;
        fmt_ = AFMT_S16_LE;
        copy_fn = copy_u16_to_s16;
        atom = 2;
        break;

    default:
        abort ();
    }

    if ((fmt_ == fmt) && (bits16 + 1 == nchannels) && (rfreq == freq))
        return;
    else {
        AUD_open (rfreq, rnchannels, rfmt);
    }
}

void AUD_open (int rfreq, int rnchannels, audfmt_e rfmt)
{
    int fmt_;
    int mmmmssss;
    struct audio_buf_info abinfo;
    int _fmt;
    int _freq;
    int _nchannels;
    int bits16;

    bits16 = 0;

    switch (rfmt) {
    case AUD_FMT_U8:
        bits16 = 0;
        fmt_ = AFMT_U8;
        copy_fn = copy_no_conversion;
        atom = 1;
        break;

    case AUD_FMT_S8:
        Fail ("can not play 8bit signed");

    case AUD_FMT_S16:
        bits16 = 1;
        fmt_ = AFMT_S16_LE;
        copy_fn = copy_no_conversion;
        atom = 2;
        break;

    case AUD_FMT_U16:
        bits16 = 1;
        fmt_ = AFMT_S16_LE;
        copy_fn = copy_u16_to_s16;
        atom = 2;
        break;

    default:
        abort ();
    }

    if (buf) {
        free (buf);
        buf = 0;
    }

    if (-1 != audio_fd)
        close (audio_fd);

    audio_fd = open ("/dev/dsp", O_WRONLY | O_NONBLOCK);
    if (-1 == audio_fd) {
        ERRFail ("can not open /dev/dsp");
    }

    _fmt = fmt_;
    _freq = rfreq;
    _nchannels = rnchannels;

    IOCTL ((audio_fd, SNDCTL_DSP_RESET, 1));
    IOCTL ((audio_fd, SNDCTL_DSP_SAMPLESIZE, &_fmt));
    IOCTL ((audio_fd, SNDCTL_DSP_CHANNELS, &_nchannels));
    IOCTL ((audio_fd, SNDCTL_DSP_SPEED, &_freq));
    IOCTL ((audio_fd, SNDCTL_DSP_NONBLOCK));

    /* from oss.pdf:

    The argument to this call is an integer encoded as 0xMMMMSSSS (in
    hex). The 16 least significant bits determine the fragment
    size. The size is 2^SSSS. For examp le SSSS=0008 gives fragment
    size of 256 bytes (2^8). The minimum is 16 bytes (SSSS=4) and the
    maximum is total_buffer_size/2. Some devices or processor
    architectures may require larger fragments - in this case the
    requested fragment size is automatically increased.

    So ahem... 4096 = 2^12, and grand total 0x0004000c
    */

    mmmmssss = (conf_nfrags << 16) | conf_fragsize;
    IOCTL ((audio_fd, SNDCTL_DSP_SETFRAGMENT, &mmmmssss));

    linfo ("_fmt = %d, fmt = %d\n"
           "_channels = %d, rnchannels = %d\n"
           "_freq = %d, freq = %d\n",
           _fmt, fmt_,
           _nchannels, rnchannels,
           _freq, rfreq);

    if (_fmt != fmt_) {
        Fail ("format %d != %d", _fmt, fmt_);
    }

    if (_nchannels != rnchannels) {
        Fail ("channels %d != %d", _nchannels, rnchannels);
    }

    if (_freq != rfreq) {
        Fail ("freq %d != %d", _freq, rfreq);
    }

    IOCTL ((audio_fd, SNDCTL_DSP_GETOSPACE, &abinfo));

    nfrags = abinfo.fragstotal;
    fragsize = abinfo.fragsize;
    freq = _freq;
    fmt = _fmt;
    nchannels = rnchannels;
    atom <<= nchannels >>  1;
    bufsize = nfrags * fragsize;

    bytes_per_second = (freq << (nchannels >> 1)) << bits16;

    linfo ("bytes per second %d\n", bytes_per_second);

    linfo ("fragments %d, fragstotal %d, fragsize %d, bytes %d, bufsize %d\n",
           abinfo.fragments,
           abinfo.fragstotal,
           abinfo.fragsize,
           abinfo.bytes,
           bufsize);

    if (NULL == buf) {
        buf = malloc (bufsize);
        if (NULL == buf) {
            abort ();
        }
    }

    rpos = 0;
    wpos = 0;
    live = 0;
}

int AUD_write (void *in_buf, int size)
{
    int to_copy, temp;
    uint8_t *in, *out;

    to_copy = MIN (bufsize - live, size);

    temp = to_copy;

    in = in_buf;
    out = buf;

    while (temp) {
        int copy;

        copy = MIN (temp, bufsize - wpos);
        copy_fn (out + wpos, in, copy);

        wpos += copy;
        if (wpos == bufsize) {
            wpos = 0;
        }

        temp -= copy;
        in += copy;
        live += copy;
    }

    return to_copy;
}

void AUD_run (void)
{
    int res;
    int bytes;
    struct audio_buf_info abinfo;

    if (0 == live)
        return;

    res = ioctl (audio_fd, SNDCTL_DSP_GETOSPACE, &abinfo);

    if (-1 == res) {
        int err;

        err = errno;
        lwarn ("SNDCTL_DSP_GETOSPACE failed with %s\n", strerror (err));
    }

    bytes = abinfo.bytes;
    bytes = MIN (live, bytes);
#if 0
    bytes = (bytes / fragsize) * fragsize;
#endif

    while (bytes) {
        int left, play, written;

        left = bufsize - rpos;
        play = MIN (left, bytes);
        written = write (audio_fd, (void *) ((uint32_t) buf + rpos), play);

        if (-1 == written) {
            if (EAGAIN == errno || EINTR == errno) {
                return;
            }
            else {
                ERRFail ("write audio");
            }
        }

        play = written;
        live -= play;
        rpos += play;
        bytes -= play;

        if (rpos == bufsize) {
            rpos = 0;
        }
    }
}

static int get_dsp_bytes (void)
{
    int res;
    struct count_info info;

    res = ioctl (audio_fd, SNDCTL_DSP_GETOPTR, &info);
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

void AUD_adjust_estimate (int _leftover)
{
    leftover = _leftover;
}

int AUD_get_free (void)
{
    int free, elapsed;

    free = bufsize - live;

    if (0 == free)
        return 0;

    elapsed = free;
    switch (estimate) {
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
            static uint64_t old_ticks;
            uint64_t ticks, delta;
            uint64_t ua_elapsed;
            uint64_t al_elapsed;

            ticks = qemu_get_clock(rt_clock);
            delta = ticks - old_ticks;
            old_ticks = ticks;

            ua_elapsed = (delta * bytes_per_second) / 1000;
            al_elapsed = ua_elapsed & ~3ULL;

            ldebug ("tid elapsed %llu bytes\n", ua_elapsed);

            if (al_elapsed > (uint64_t) INT_MAX)
                elapsed = INT_MAX;
            else
                elapsed = al_elapsed;

            elapsed += leftover;
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
    return live;
}

int AUD_get_buffer_size (void)
{
    return bufsize;
}

void AUD_init (void)
{
    int fsp;
    int _fragsize = 4096;

    DEREF (pab);

    fsp = _fragsize;
    if (0 != (fsp & (fsp - 1))) {
        Fail ("fragment size %d is not power of 2", fsp);
    }

    conf_fragsize = lsbindex (fsp);
}

/*
 * QEMU OSS audio driver
 *
 * Copyright (c) 2003-2005 Vassili Karpov (malc)
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
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#ifdef __OpenBSD__
#include <soundcard.h>
#else
#include <sys/soundcard.h>
#endif
#include "qemu-common.h"
#include "audio.h"

#define AUDIO_CAP "oss"
#include "audio_int.h"

typedef struct OSSVoiceOut {
    HWVoiceOut hw;
    void *pcm_buf;
    int fd;
    int nfrags;
    int fragsize;
    int mmapped;
    int old_optr;
} OSSVoiceOut;

typedef struct OSSVoiceIn {
    HWVoiceIn hw;
    void *pcm_buf;
    int fd;
    int nfrags;
    int fragsize;
    int old_optr;
} OSSVoiceIn;

static struct {
    int try_mmap;
    int nfrags;
    int fragsize;
    const char *devpath_out;
    const char *devpath_in;
    int debug;
} conf = {
    .try_mmap = 0,
    .nfrags = 4,
    .fragsize = 4096,
    .devpath_out = "/dev/dsp",
    .devpath_in = "/dev/dsp",
    .debug = 0
};

struct oss_params {
    int freq;
    audfmt_e fmt;
    int nchannels;
    int nfrags;
    int fragsize;
};

static void GCC_FMT_ATTR (2, 3) oss_logerr (int err, const char *fmt, ...)
{
    va_list ap;

    va_start (ap, fmt);
    AUD_vlog (AUDIO_CAP, fmt, ap);
    va_end (ap);

    AUD_log (AUDIO_CAP, "Reason: %s\n", strerror (err));
}

static void GCC_FMT_ATTR (3, 4) oss_logerr2 (
    int err,
    const char *typ,
    const char *fmt,
    ...
    )
{
    va_list ap;

    AUD_log (AUDIO_CAP, "Could not initialize %s\n", typ);

    va_start (ap, fmt);
    AUD_vlog (AUDIO_CAP, fmt, ap);
    va_end (ap);

    AUD_log (AUDIO_CAP, "Reason: %s\n", strerror (err));
}

static void oss_anal_close (int *fdp)
{
    int err = close (*fdp);
    if (err) {
        oss_logerr (errno, "Failed to close file(fd=%d)\n", *fdp);
    }
    *fdp = -1;
}

static int oss_write (SWVoiceOut *sw, void *buf, int len)
{
    return audio_pcm_sw_write (sw, buf, len);
}

static int aud_to_ossfmt (audfmt_e fmt)
{
    switch (fmt) {
    case AUD_FMT_S8:
        return AFMT_S8;

    case AUD_FMT_U8:
        return AFMT_U8;

    case AUD_FMT_S16:
        return AFMT_S16_LE;

    case AUD_FMT_U16:
        return AFMT_U16_LE;

    default:
        dolog ("Internal logic error: Bad audio format %d\n", fmt);
#ifdef DEBUG_AUDIO
        abort ();
#endif
        return AFMT_U8;
    }
}

static int oss_to_audfmt (int ossfmt, audfmt_e *fmt, int *endianness)
{
    switch (ossfmt) {
    case AFMT_S8:
        *endianness = 0;
        *fmt = AUD_FMT_S8;
        break;

    case AFMT_U8:
        *endianness = 0;
        *fmt = AUD_FMT_U8;
        break;

    case AFMT_S16_LE:
        *endianness = 0;
        *fmt = AUD_FMT_S16;
        break;

    case AFMT_U16_LE:
        *endianness = 0;
        *fmt = AUD_FMT_U16;
        break;

    case AFMT_S16_BE:
        *endianness = 1;
        *fmt = AUD_FMT_S16;
        break;

    case AFMT_U16_BE:
        *endianness = 1;
        *fmt = AUD_FMT_U16;
        break;

    default:
        dolog ("Unrecognized audio format %d\n", ossfmt);
        return -1;
    }

    return 0;
}

#if defined DEBUG_MISMATCHES || defined DEBUG
static void oss_dump_info (struct oss_params *req, struct oss_params *obt)
{
    dolog ("parameter | requested value | obtained value\n");
    dolog ("format    |      %10d |     %10d\n", req->fmt, obt->fmt);
    dolog ("channels  |      %10d |     %10d\n",
           req->nchannels, obt->nchannels);
    dolog ("frequency |      %10d |     %10d\n", req->freq, obt->freq);
    dolog ("nfrags    |      %10d |     %10d\n", req->nfrags, obt->nfrags);
    dolog ("fragsize  |      %10d |     %10d\n",
           req->fragsize, obt->fragsize);
}
#endif

static int oss_open (int in, struct oss_params *req,
                     struct oss_params *obt, int *pfd)
{
    int fd;
    int mmmmssss;
    audio_buf_info abinfo;
    int fmt, freq, nchannels;
    const char *dspname = in ? conf.devpath_in : conf.devpath_out;
    const char *typ = in ? "ADC" : "DAC";

    fd = open (dspname, (in ? O_RDONLY : O_WRONLY) | O_NONBLOCK);
    if (-1 == fd) {
        oss_logerr2 (errno, typ, "Failed to open `%s'\n", dspname);
        return -1;
    }

    freq = req->freq;
    nchannels = req->nchannels;
    fmt = req->fmt;

    if (ioctl (fd, SNDCTL_DSP_SAMPLESIZE, &fmt)) {
        oss_logerr2 (errno, typ, "Failed to set sample size %d\n", req->fmt);
        goto err;
    }

    if (ioctl (fd, SNDCTL_DSP_CHANNELS, &nchannels)) {
        oss_logerr2 (errno, typ, "Failed to set number of channels %d\n",
                     req->nchannels);
        goto err;
    }

    if (ioctl (fd, SNDCTL_DSP_SPEED, &freq)) {
        oss_logerr2 (errno, typ, "Failed to set frequency %d\n", req->freq);
        goto err;
    }

    if (ioctl (fd, SNDCTL_DSP_NONBLOCK, NULL)) {
        oss_logerr2 (errno, typ, "Failed to set non-blocking mode\n");
        goto err;
    }

    mmmmssss = (req->nfrags << 16) | lsbindex (req->fragsize);
    if (ioctl (fd, SNDCTL_DSP_SETFRAGMENT, &mmmmssss)) {
        oss_logerr2 (errno, typ, "Failed to set buffer length (%d, %d)\n",
                     req->nfrags, req->fragsize);
        goto err;
    }

    if (ioctl (fd, in ? SNDCTL_DSP_GETISPACE : SNDCTL_DSP_GETOSPACE, &abinfo)) {
        oss_logerr2 (errno, typ, "Failed to get buffer length\n");
        goto err;
    }

    if (!abinfo.fragstotal || !abinfo.fragsize) {
        AUD_log (AUDIO_CAP, "Returned bogus buffer information(%d, %d) for %s\n",
                 abinfo.fragstotal, abinfo.fragsize, typ);
        goto err;
    }

    obt->fmt = fmt;
    obt->nchannels = nchannels;
    obt->freq = freq;
    obt->nfrags = abinfo.fragstotal;
    obt->fragsize = abinfo.fragsize;
    *pfd = fd;

#ifdef DEBUG_MISMATCHES
    if ((req->fmt != obt->fmt) ||
        (req->nchannels != obt->nchannels) ||
        (req->freq != obt->freq) ||
        (req->fragsize != obt->fragsize) ||
        (req->nfrags != obt->nfrags)) {
        dolog ("Audio parameters mismatch\n");
        oss_dump_info (req, obt);
    }
#endif

#ifdef DEBUG
    oss_dump_info (req, obt);
#endif
    return 0;

 err:
    oss_anal_close (&fd);
    return -1;
}

static int oss_run_out (HWVoiceOut *hw)
{
    OSSVoiceOut *oss = (OSSVoiceOut *) hw;
    int err, rpos, live, decr;
    int samples;
    uint8_t *dst;
    struct st_sample *src;
    struct audio_buf_info abinfo;
    struct count_info cntinfo;
    int bufsize;

    live = audio_pcm_hw_get_live_out (hw);
    if (!live) {
        return 0;
    }

    bufsize = hw->samples << hw->info.shift;

    if (oss->mmapped) {
        int bytes;

        err = ioctl (oss->fd, SNDCTL_DSP_GETOPTR, &cntinfo);
        if (err < 0) {
            oss_logerr (errno, "SNDCTL_DSP_GETOPTR failed\n");
            return 0;
        }

        if (cntinfo.ptr == oss->old_optr) {
            if (abs (hw->samples - live) < 64) {
                dolog ("warning: Overrun\n");
            }
            return 0;
        }

        if (cntinfo.ptr > oss->old_optr) {
            bytes = cntinfo.ptr - oss->old_optr;
        }
        else {
            bytes = bufsize + cntinfo.ptr - oss->old_optr;
        }

        decr = audio_MIN (bytes >> hw->info.shift, live);
    }
    else {
        err = ioctl (oss->fd, SNDCTL_DSP_GETOSPACE, &abinfo);
        if (err < 0) {
            oss_logerr (errno, "SNDCTL_DSP_GETOPTR failed\n");
            return 0;
        }

        if (abinfo.bytes > bufsize) {
            if (conf.debug) {
                dolog ("warning: Invalid available size, size=%d bufsize=%d\n"
                       "please report your OS/audio hw to malc@pulsesoft.com\n",
                       abinfo.bytes, bufsize);
            }
            abinfo.bytes = bufsize;
        }

        if (abinfo.bytes < 0) {
            if (conf.debug) {
                dolog ("warning: Invalid available size, size=%d bufsize=%d\n",
                       abinfo.bytes, bufsize);
            }
            return 0;
        }

        decr = audio_MIN (abinfo.bytes >> hw->info.shift, live);
        if (!decr) {
            return 0;
        }
    }

    samples = decr;
    rpos = hw->rpos;
    while (samples) {
        int left_till_end_samples = hw->samples - rpos;
        int convert_samples = audio_MIN (samples, left_till_end_samples);

        src = hw->mix_buf + rpos;
        dst = advance (oss->pcm_buf, rpos << hw->info.shift);

        hw->clip (dst, src, convert_samples);
        if (!oss->mmapped) {
            int written;

            written = write (oss->fd, dst, convert_samples << hw->info.shift);
            /* XXX: follow errno recommendations ? */
            if (written == -1) {
                oss_logerr (
                    errno,
                    "Failed to write %d bytes of audio data from %p\n",
                    convert_samples << hw->info.shift,
                    dst
                    );
                continue;
            }

            if (written != convert_samples << hw->info.shift) {
                int wsamples = written >> hw->info.shift;
                int wbytes = wsamples << hw->info.shift;
                if (wbytes != written) {
                    dolog ("warning: Misaligned write %d (requested %d), "
                           "alignment %d\n",
                           wbytes, written, hw->info.align + 1);
                }
                decr -= wsamples;
                rpos = (rpos + wsamples) % hw->samples;
                break;
            }
        }

        rpos = (rpos + convert_samples) % hw->samples;
        samples -= convert_samples;
    }
    if (oss->mmapped) {
        oss->old_optr = cntinfo.ptr;
    }

    hw->rpos = rpos;
    return decr;
}

static void oss_fini_out (HWVoiceOut *hw)
{
    int err;
    OSSVoiceOut *oss = (OSSVoiceOut *) hw;

    ldebug ("oss_fini\n");
    oss_anal_close (&oss->fd);

    if (oss->pcm_buf) {
        if (oss->mmapped) {
            err = munmap (oss->pcm_buf, hw->samples << hw->info.shift);
            if (err) {
                oss_logerr (errno, "Failed to unmap buffer %p, size %d\n",
                            oss->pcm_buf, hw->samples << hw->info.shift);
            }
        }
        else {
            qemu_free (oss->pcm_buf);
        }
        oss->pcm_buf = NULL;
    }
}

static int oss_init_out (HWVoiceOut *hw, struct audsettings *as)
{
    OSSVoiceOut *oss = (OSSVoiceOut *) hw;
    struct oss_params req, obt;
    int endianness;
    int err;
    int fd;
    audfmt_e effective_fmt;
    struct audsettings obt_as;

    oss->fd = -1;

    req.fmt = aud_to_ossfmt (as->fmt);
    req.freq = as->freq;
    req.nchannels = as->nchannels;
    req.fragsize = conf.fragsize;
    req.nfrags = conf.nfrags;

    if (oss_open (0, &req, &obt, &fd)) {
        return -1;
    }

    err = oss_to_audfmt (obt.fmt, &effective_fmt, &endianness);
    if (err) {
        oss_anal_close (&fd);
        return -1;
    }

    obt_as.freq = obt.freq;
    obt_as.nchannels = obt.nchannels;
    obt_as.fmt = effective_fmt;
    obt_as.endianness = endianness;

    audio_pcm_init_info (&hw->info, &obt_as);
    oss->nfrags = obt.nfrags;
    oss->fragsize = obt.fragsize;

    if (obt.nfrags * obt.fragsize & hw->info.align) {
        dolog ("warning: Misaligned DAC buffer, size %d, alignment %d\n",
               obt.nfrags * obt.fragsize, hw->info.align + 1);
    }

    hw->samples = (obt.nfrags * obt.fragsize) >> hw->info.shift;

    oss->mmapped = 0;
    if (conf.try_mmap) {
        oss->pcm_buf = mmap (
            0,
            hw->samples << hw->info.shift,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            fd,
            0
            );
        if (oss->pcm_buf == MAP_FAILED) {
            oss_logerr (errno, "Failed to map %d bytes of DAC\n",
                        hw->samples << hw->info.shift);
        } else {
            int err;
            int trig = 0;
            if (ioctl (fd, SNDCTL_DSP_SETTRIGGER, &trig) < 0) {
                oss_logerr (errno, "SNDCTL_DSP_SETTRIGGER 0 failed\n");
            }
            else {
                trig = PCM_ENABLE_OUTPUT;
                if (ioctl (fd, SNDCTL_DSP_SETTRIGGER, &trig) < 0) {
                    oss_logerr (
                        errno,
                        "SNDCTL_DSP_SETTRIGGER PCM_ENABLE_OUTPUT failed\n"
                        );
                }
                else {
                    oss->mmapped = 1;
                }
            }

            if (!oss->mmapped) {
                err = munmap (oss->pcm_buf, hw->samples << hw->info.shift);
                if (err) {
                    oss_logerr (errno, "Failed to unmap buffer %p size %d\n",
                                oss->pcm_buf, hw->samples << hw->info.shift);
                }
            }
        }
    }

    if (!oss->mmapped) {
        oss->pcm_buf = audio_calloc (
            AUDIO_FUNC,
            hw->samples,
            1 << hw->info.shift
            );
        if (!oss->pcm_buf) {
            dolog (
                "Could not allocate DAC buffer (%d samples, each %d bytes)\n",
                hw->samples,
                1 << hw->info.shift
                );
            oss_anal_close (&fd);
            return -1;
        }
    }

    oss->fd = fd;
    return 0;
}

static int oss_ctl_out (HWVoiceOut *hw, int cmd, ...)
{
    int trig;
    OSSVoiceOut *oss = (OSSVoiceOut *) hw;

    if (!oss->mmapped) {
        return 0;
    }

    switch (cmd) {
    case VOICE_ENABLE:
        ldebug ("enabling voice\n");
        audio_pcm_info_clear_buf (&hw->info, oss->pcm_buf, hw->samples);
        trig = PCM_ENABLE_OUTPUT;
        if (ioctl (oss->fd, SNDCTL_DSP_SETTRIGGER, &trig) < 0) {
            oss_logerr (
                errno,
                "SNDCTL_DSP_SETTRIGGER PCM_ENABLE_OUTPUT failed\n"
                );
            return -1;
        }
        break;

    case VOICE_DISABLE:
        ldebug ("disabling voice\n");
        trig = 0;
        if (ioctl (oss->fd, SNDCTL_DSP_SETTRIGGER, &trig) < 0) {
            oss_logerr (errno, "SNDCTL_DSP_SETTRIGGER 0 failed\n");
            return -1;
        }
        break;
    }
    return 0;
}

static int oss_init_in (HWVoiceIn *hw, struct audsettings *as)
{
    OSSVoiceIn *oss = (OSSVoiceIn *) hw;
    struct oss_params req, obt;
    int endianness;
    int err;
    int fd;
    audfmt_e effective_fmt;
    struct audsettings obt_as;

    oss->fd = -1;

    req.fmt = aud_to_ossfmt (as->fmt);
    req.freq = as->freq;
    req.nchannels = as->nchannels;
    req.fragsize = conf.fragsize;
    req.nfrags = conf.nfrags;
    if (oss_open (1, &req, &obt, &fd)) {
        return -1;
    }

    err = oss_to_audfmt (obt.fmt, &effective_fmt, &endianness);
    if (err) {
        oss_anal_close (&fd);
        return -1;
    }

    obt_as.freq = obt.freq;
    obt_as.nchannels = obt.nchannels;
    obt_as.fmt = effective_fmt;
    obt_as.endianness = endianness;

    audio_pcm_init_info (&hw->info, &obt_as);
    oss->nfrags = obt.nfrags;
    oss->fragsize = obt.fragsize;

    if (obt.nfrags * obt.fragsize & hw->info.align) {
        dolog ("warning: Misaligned ADC buffer, size %d, alignment %d\n",
               obt.nfrags * obt.fragsize, hw->info.align + 1);
    }

    hw->samples = (obt.nfrags * obt.fragsize) >> hw->info.shift;
    oss->pcm_buf = audio_calloc (AUDIO_FUNC, hw->samples, 1 << hw->info.shift);
    if (!oss->pcm_buf) {
        dolog ("Could not allocate ADC buffer (%d samples, each %d bytes)\n",
               hw->samples, 1 << hw->info.shift);
        oss_anal_close (&fd);
        return -1;
    }

    oss->fd = fd;
    return 0;
}

static void oss_fini_in (HWVoiceIn *hw)
{
    OSSVoiceIn *oss = (OSSVoiceIn *) hw;

    oss_anal_close (&oss->fd);

    if (oss->pcm_buf) {
        qemu_free (oss->pcm_buf);
        oss->pcm_buf = NULL;
    }
}

static int oss_run_in (HWVoiceIn *hw)
{
    OSSVoiceIn *oss = (OSSVoiceIn *) hw;
    int hwshift = hw->info.shift;
    int i;
    int live = audio_pcm_hw_get_live_in (hw);
    int dead = hw->samples - live;
    size_t read_samples = 0;
    struct {
        int add;
        int len;
    } bufs[2] = {
        { hw->wpos, 0 },
        { 0, 0 }
    };

    if (!dead) {
        return 0;
    }

    if (hw->wpos + dead > hw->samples) {
        bufs[0].len = (hw->samples - hw->wpos) << hwshift;
        bufs[1].len = (dead - (hw->samples - hw->wpos)) << hwshift;
    }
    else {
        bufs[0].len = dead << hwshift;
    }


    for (i = 0; i < 2; ++i) {
        ssize_t nread;

        if (bufs[i].len) {
            void *p = advance (oss->pcm_buf, bufs[i].add << hwshift);
            nread = read (oss->fd, p, bufs[i].len);

            if (nread > 0) {
                if (nread & hw->info.align) {
                    dolog ("warning: Misaligned read %zd (requested %d), "
                           "alignment %d\n", nread, bufs[i].add << hwshift,
                           hw->info.align + 1);
                }
                read_samples += nread >> hwshift;
                hw->conv (hw->conv_buf + bufs[i].add, p, nread >> hwshift,
                          &nominal_volume);
            }

            if (bufs[i].len - nread) {
                if (nread == -1) {
                    switch (errno) {
                    case EINTR:
                    case EAGAIN:
                        break;
                    default:
                        oss_logerr (
                            errno,
                            "Failed to read %d bytes of audio (to %p)\n",
                            bufs[i].len, p
                            );
                        break;
                    }
                }
                break;
            }
        }
    }

    hw->wpos = (hw->wpos + read_samples) % hw->samples;
    return read_samples;
}

static int oss_read (SWVoiceIn *sw, void *buf, int size)
{
    return audio_pcm_sw_read (sw, buf, size);
}

static int oss_ctl_in (HWVoiceIn *hw, int cmd, ...)
{
    (void) hw;
    (void) cmd;
    return 0;
}

static void *oss_audio_init (void)
{
    return &conf;
}

static void oss_audio_fini (void *opaque)
{
    (void) opaque;
}

static struct audio_option oss_options[] = {
    {"FRAGSIZE", AUD_OPT_INT, &conf.fragsize,
     "Fragment size in bytes", NULL, 0},
    {"NFRAGS", AUD_OPT_INT, &conf.nfrags,
     "Number of fragments", NULL, 0},
    {"MMAP", AUD_OPT_BOOL, &conf.try_mmap,
     "Try using memory mapped access", NULL, 0},
    {"DAC_DEV", AUD_OPT_STR, &conf.devpath_out,
     "Path to DAC device", NULL, 0},
    {"ADC_DEV", AUD_OPT_STR, &conf.devpath_in,
     "Path to ADC device", NULL, 0},
    {"DEBUG", AUD_OPT_BOOL, &conf.debug,
     "Turn on some debugging messages", NULL, 0},
    {NULL, 0, NULL, NULL, NULL, 0}
};

static struct audio_pcm_ops oss_pcm_ops = {
    oss_init_out,
    oss_fini_out,
    oss_run_out,
    oss_write,
    oss_ctl_out,

    oss_init_in,
    oss_fini_in,
    oss_run_in,
    oss_read,
    oss_ctl_in
};

struct audio_driver oss_audio_driver = {
    INIT_FIELD (name           = ) "oss",
    INIT_FIELD (descr          = ) "OSS http://www.opensound.com",
    INIT_FIELD (options        = ) oss_options,
    INIT_FIELD (init           = ) oss_audio_init,
    INIT_FIELD (fini           = ) oss_audio_fini,
    INIT_FIELD (pcm_ops        = ) &oss_pcm_ops,
    INIT_FIELD (can_be_default = ) 1,
    INIT_FIELD (max_voices_out = ) INT_MAX,
    INIT_FIELD (max_voices_in  = ) INT_MAX,
    INIT_FIELD (voice_size_out = ) sizeof (OSSVoiceOut),
    INIT_FIELD (voice_size_in  = ) sizeof (OSSVoiceIn)
};

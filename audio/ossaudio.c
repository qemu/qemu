/*
 * QEMU OSS audio output driver
 * 
 * Copyright (c) 2003-2004 Vassili Karpov (malc)
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
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>
#include <assert.h>
#include "vl.h"

#include "audio/audio_int.h"

typedef struct OSSVoice {
    HWVoice hw;
    void *pcm_buf;
    int fd;
    int nfrags;
    int fragsize;
    int mmapped;
    int old_optr;
} OSSVoice;

#define dolog(...) AUD_log ("oss", __VA_ARGS__)
#ifdef DEBUG
#define ldebug(...) dolog (__VA_ARGS__)
#else
#define ldebug(...)
#endif

#define QC_OSS_FRAGSIZE "QEMU_OSS_FRAGSIZE"
#define QC_OSS_NFRAGS   "QEMU_OSS_NFRAGS"
#define QC_OSS_MMAP     "QEMU_OSS_MMAP"
#define QC_OSS_DEV      "QEMU_OSS_DEV"

#define errstr() strerror (errno)

static struct {
    int try_mmap;
    int nfrags;
    int fragsize;
    const char *dspname;
} conf = {
    .try_mmap = 0,
    .nfrags = 4,
    .fragsize = 4096,
    .dspname = "/dev/dsp"
};

struct oss_params {
    int freq;
    audfmt_e fmt;
    int nchannels;
    int nfrags;
    int fragsize;
};

static int oss_hw_write (SWVoice *sw, void *buf, int len)
{
    return pcm_hw_write (sw, buf, len);
}

static int AUD_to_ossfmt (audfmt_e fmt)
{
    switch (fmt) {
    case AUD_FMT_S8: return AFMT_S8;
    case AUD_FMT_U8: return AFMT_U8;
    case AUD_FMT_S16: return AFMT_S16_LE;
    case AUD_FMT_U16: return AFMT_U16_LE;
    default:
        dolog ("Internal logic error: Bad audio format %d\nAborting\n", fmt);
        exit (EXIT_FAILURE);
    }
}

static int oss_to_audfmt (int fmt)
{
    switch (fmt) {
    case AFMT_S8: return AUD_FMT_S8;
    case AFMT_U8: return AUD_FMT_U8;
    case AFMT_S16_LE: return AUD_FMT_S16;
    case AFMT_U16_LE: return AUD_FMT_U16;
    default:
        dolog ("Internal logic error: Unrecognized OSS audio format %d\n"
               "Aborting\n",
               fmt);
        exit (EXIT_FAILURE);
    }
}

#ifdef DEBUG_PCM
static void oss_dump_pcm_info (struct oss_params *req, struct oss_params *obt)
{
    dolog ("parameter | requested value | obtained value\n");
    dolog ("format    |      %10d |     %10d\n", req->fmt, obt->fmt);
    dolog ("channels  |      %10d |     %10d\n", req->nchannels, obt->nchannels);
    dolog ("frequency |      %10d |     %10d\n", req->freq, obt->freq);
    dolog ("nfrags    |      %10d |     %10d\n", req->nfrags, obt->nfrags);
    dolog ("fragsize  |      %10d |     %10d\n", req->fragsize, obt->fragsize);
}
#endif

static int oss_open (struct oss_params *req, struct oss_params *obt, int *pfd)
{
    int fd;
    int mmmmssss;
    audio_buf_info abinfo;
    int fmt, freq, nchannels;
    const char *dspname = conf.dspname;

    fd = open (dspname, O_RDWR | O_NONBLOCK);
    if (-1 == fd) {
        dolog ("Could not initialize audio hardware. Failed to open `%s':\n"
               "Reason:%s\n",
               dspname,
               errstr ());
        return -1;
    }

    freq = req->freq;
    nchannels = req->nchannels;
    fmt = req->fmt;

    if (ioctl (fd, SNDCTL_DSP_SAMPLESIZE, &fmt)) {
        dolog ("Could not initialize audio hardware\n"
               "Failed to set sample size\n"
               "Reason: %s\n",
               errstr ());
        goto err;
    }

    if (ioctl (fd, SNDCTL_DSP_CHANNELS, &nchannels)) {
        dolog ("Could not initialize audio hardware\n"
               "Failed to set number of channels\n"
               "Reason: %s\n",
               errstr ());
        goto err;
    }

    if (ioctl (fd, SNDCTL_DSP_SPEED, &freq)) {
        dolog ("Could not initialize audio hardware\n"
               "Failed to set frequency\n"
               "Reason: %s\n",
               errstr ());
        goto err;
    }

    if (ioctl (fd, SNDCTL_DSP_NONBLOCK)) {
        dolog ("Could not initialize audio hardware\n"
               "Failed to set non-blocking mode\n"
               "Reason: %s\n",
               errstr ());
        goto err;
    }

    mmmmssss = (req->nfrags << 16) | lsbindex (req->fragsize);
    if (ioctl (fd, SNDCTL_DSP_SETFRAGMENT, &mmmmssss)) {
        dolog ("Could not initialize audio hardware\n"
               "Failed to set buffer length (%d, %d)\n"
               "Reason:%s\n",
               conf.nfrags, conf.fragsize,
               errstr ());
        goto err;
    }

    if (ioctl (fd, SNDCTL_DSP_GETOSPACE, &abinfo)) {
        dolog ("Could not initialize audio hardware\n"
               "Failed to get buffer length\n"
               "Reason:%s\n",
               errstr ());
        goto err;
    }

    obt->fmt = fmt;
    obt->nchannels = nchannels;
    obt->freq = freq;
    obt->nfrags = abinfo.fragstotal;
    obt->fragsize = abinfo.fragsize;
    *pfd = fd;

    if ((req->fmt != obt->fmt) ||
        (req->nchannels != obt->nchannels) ||
        (req->freq != obt->freq) ||
        (req->fragsize != obt->fragsize) ||
        (req->nfrags != obt->nfrags)) {
#ifdef DEBUG_PCM
        dolog ("Audio parameters mismatch\n");
        oss_dump_pcm_info (req, obt);
#endif
    }

#ifdef DEBUG_PCM
    oss_dump_pcm_info (req, obt);
#endif
    return 0;

err:
    close (fd);
    return -1;
}

static void oss_hw_run (HWVoice *hw)
{
    OSSVoice *oss = (OSSVoice *) hw;
    int err, rpos, live, decr;
    int samples;
    uint8_t *dst;
    st_sample_t *src;
    struct audio_buf_info abinfo;
    struct count_info cntinfo;

    live = pcm_hw_get_live (hw);
    if (live <= 0)
        return;

    if (oss->mmapped) {
        int bytes;

        err = ioctl (oss->fd, SNDCTL_DSP_GETOPTR, &cntinfo);
        if (err < 0) {
            dolog ("SNDCTL_DSP_GETOPTR failed\nReason: %s\n", errstr ());
            return;
        }

        if (cntinfo.ptr == oss->old_optr) {
            if (abs (hw->samples - live) < 64)
                dolog ("overrun\n");
            return;
        }

        if (cntinfo.ptr > oss->old_optr) {
            bytes = cntinfo.ptr - oss->old_optr;
        }
        else {
            bytes = hw->bufsize + cntinfo.ptr - oss->old_optr;
        }

        decr = audio_MIN (bytes >> hw->shift, live);
    }
    else {
        err = ioctl (oss->fd, SNDCTL_DSP_GETOSPACE, &abinfo);
        if (err < 0) {
            dolog ("SNDCTL_DSP_GETOSPACE failed\nReason: %s\n", errstr ());
            return;
        }

        decr = audio_MIN (abinfo.bytes >> hw->shift, live);
        if (decr <= 0)
            return;
    }

    samples = decr;
    rpos = hw->rpos;
    while (samples) {
        int left_till_end_samples = hw->samples - rpos;
        int convert_samples = audio_MIN (samples, left_till_end_samples);

        src = advance (hw->mix_buf, rpos * sizeof (st_sample_t));
        dst = advance (oss->pcm_buf, rpos << hw->shift);

        hw->clip (dst, src, convert_samples);
        if (!oss->mmapped) {
            int written;

            written = write (oss->fd, dst, convert_samples << hw->shift);
            /* XXX: follow errno recommendations ? */
            if (written == -1) {
                dolog ("Failed to write audio\nReason: %s\n", errstr ());
                continue;
            }

            if (written != convert_samples << hw->shift) {
                int wsamples = written >> hw->shift;
                int wbytes = wsamples << hw->shift;
                if (wbytes != written) {
                    dolog ("Unaligned write %d, %d\n", wbytes, written);
                }
                memset (src, 0, wbytes);
                decr -= samples;
                rpos = (rpos + wsamples) % hw->samples;
                break;
            }
        }
        memset (src, 0, convert_samples * sizeof (st_sample_t));

        rpos = (rpos + convert_samples) % hw->samples;
        samples -= convert_samples;
    }
    if (oss->mmapped) {
        oss->old_optr = cntinfo.ptr;
    }

    pcm_hw_dec_live (hw, decr);
    hw->rpos = rpos;
}

static void oss_hw_fini (HWVoice *hw)
{
    int err;
    OSSVoice *oss = (OSSVoice *) hw;

    ldebug ("oss_hw_fini\n");
    err = close (oss->fd);
    if (err) {
        dolog ("Failed to close OSS descriptor\nReason: %s\n", errstr ());
    }
    oss->fd = -1;

    if (oss->pcm_buf) {
        if (oss->mmapped) {
            err = munmap (oss->pcm_buf, hw->bufsize);
            if (err) {
                dolog ("Failed to unmap OSS buffer\nReason: %s\n",
                       errstr ());
            }
        }
        else {
            qemu_free (oss->pcm_buf);
        }
        oss->pcm_buf = NULL;
    }
}

static int oss_hw_init (HWVoice *hw, int freq, int nchannels, audfmt_e fmt)
{
    OSSVoice *oss = (OSSVoice *) hw;
    struct oss_params req, obt;

    assert (!oss->fd);
    req.fmt = AUD_to_ossfmt (fmt);
    req.freq = freq;
    req.nchannels = nchannels;
    req.fragsize = conf.fragsize;
    req.nfrags = conf.nfrags;

    if (oss_open (&req, &obt, &oss->fd))
        return -1;

    hw->freq = obt.freq;
    hw->fmt = oss_to_audfmt (obt.fmt);
    hw->nchannels = obt.nchannels;

    oss->nfrags = obt.nfrags;
    oss->fragsize = obt.fragsize;
    hw->bufsize = obt.nfrags * obt.fragsize;

    oss->mmapped = 0;
    if (conf.try_mmap) {
        oss->pcm_buf = mmap (0, hw->bufsize, PROT_READ | PROT_WRITE,
                             MAP_SHARED, oss->fd, 0);
        if (oss->pcm_buf == MAP_FAILED) {
            dolog ("Failed to mmap OSS device\nReason: %s\n",
                   errstr ());
        } else {
            int err;
            int trig = 0;
            if (ioctl (oss->fd, SNDCTL_DSP_SETTRIGGER, &trig) < 0) {
                dolog ("SNDCTL_DSP_SETTRIGGER 0 failed\nReason: %s\n",
                       errstr ());
            }
            else {
                trig = PCM_ENABLE_OUTPUT;
                if (ioctl (oss->fd, SNDCTL_DSP_SETTRIGGER, &trig) < 0) {
                    dolog ("SNDCTL_DSP_SETTRIGGER PCM_ENABLE_OUTPUT failed\n"
                           "Reason: %s\n", errstr ());
                }
                else {
                    oss->mmapped = 1;
                }
            }

            if (!oss->mmapped) {
                err = munmap (oss->pcm_buf, hw->bufsize);
                if (err) {
                    dolog ("Failed to unmap OSS device\nReason: %s\n",
                           errstr ());
                }
            }
        }
    }

    if (!oss->mmapped) {
        oss->pcm_buf = qemu_mallocz (hw->bufsize);
        if (!oss->pcm_buf) {
            close (oss->fd);
            oss->fd = -1;
            return -1;
        }
    }

    return 0;
}

static int oss_hw_ctl (HWVoice *hw, int cmd, ...)
{
    int trig;
    OSSVoice *oss = (OSSVoice *) hw;

    if (!oss->mmapped)
        return 0;

    switch (cmd) {
    case VOICE_ENABLE:
        ldebug ("enabling voice\n");
        pcm_hw_clear (hw, oss->pcm_buf, hw->samples);
        trig = PCM_ENABLE_OUTPUT;
        if (ioctl (oss->fd, SNDCTL_DSP_SETTRIGGER, &trig) < 0) {
            dolog ("SNDCTL_DSP_SETTRIGGER PCM_ENABLE_OUTPUT failed\n"
                   "Reason: %s\n", errstr ());
            return -1;
        }
        break;

    case VOICE_DISABLE:
        ldebug ("disabling voice\n");
        trig = 0;
        if (ioctl (oss->fd, SNDCTL_DSP_SETTRIGGER, &trig) < 0) {
            dolog ("SNDCTL_DSP_SETTRIGGER 0 failed\nReason: %s\n",
                   errstr ());
            return -1;
        }
        break;
    }
    return 0;
}

static void *oss_audio_init (void)
{
    conf.fragsize = audio_get_conf_int (QC_OSS_FRAGSIZE, conf.fragsize);
    conf.nfrags = audio_get_conf_int (QC_OSS_NFRAGS, conf.nfrags);
    conf.try_mmap = audio_get_conf_int (QC_OSS_MMAP, conf.try_mmap);
    conf.dspname = audio_get_conf_str (QC_OSS_DEV, conf.dspname);
    return &conf;
}

static void oss_audio_fini (void *opaque)
{
}

struct pcm_ops oss_pcm_ops = {
    oss_hw_init,
    oss_hw_fini,
    oss_hw_run,
    oss_hw_write,
    oss_hw_ctl
};

struct audio_output_driver oss_output_driver = {
    "oss",
    oss_audio_init,
    oss_audio_fini,
    &oss_pcm_ops,
    1,
    INT_MAX,
    sizeof (OSSVoice)
};

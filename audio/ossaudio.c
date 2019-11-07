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

#include "qemu/osdep.h"
#include <sys/ioctl.h>
#include <sys/soundcard.h>
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/host-utils.h"
#include "audio.h"
#include "trace.h"

#define AUDIO_CAP "oss"
#include "audio_int.h"

#if defined OSS_GETVERSION && defined SNDCTL_DSP_POLICY
#define USE_DSP_POLICY
#endif

typedef struct OSSVoiceOut {
    HWVoiceOut hw;
    int fd;
    int nfrags;
    int fragsize;
    int mmapped;
    Audiodev *dev;
} OSSVoiceOut;

typedef struct OSSVoiceIn {
    HWVoiceIn hw;
    int fd;
    int nfrags;
    int fragsize;
    Audiodev *dev;
} OSSVoiceIn;

struct oss_params {
    int freq;
    int fmt;
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
    int err;

    qemu_set_fd_handler (*fdp, NULL, NULL, NULL);
    err = close (*fdp);
    if (err) {
        oss_logerr (errno, "Failed to close file(fd=%d)\n", *fdp);
    }
    *fdp = -1;
}

static void oss_helper_poll_out (void *opaque)
{
    AudioState *s = opaque;
    audio_run(s, "oss_poll_out");
}

static void oss_helper_poll_in (void *opaque)
{
    AudioState *s = opaque;
    audio_run(s, "oss_poll_in");
}

static void oss_poll_out (HWVoiceOut *hw)
{
    OSSVoiceOut *oss = (OSSVoiceOut *) hw;

    qemu_set_fd_handler(oss->fd, NULL, oss_helper_poll_out, hw->s);
}

static void oss_poll_in (HWVoiceIn *hw)
{
    OSSVoiceIn *oss = (OSSVoiceIn *) hw;

    qemu_set_fd_handler(oss->fd, oss_helper_poll_in, NULL, hw->s);
}

static int aud_to_ossfmt (AudioFormat fmt, int endianness)
{
    switch (fmt) {
    case AUDIO_FORMAT_S8:
        return AFMT_S8;

    case AUDIO_FORMAT_U8:
        return AFMT_U8;

    case AUDIO_FORMAT_S16:
        if (endianness) {
            return AFMT_S16_BE;
        }
        else {
            return AFMT_S16_LE;
        }

    case AUDIO_FORMAT_U16:
        if (endianness) {
            return AFMT_U16_BE;
        }
        else {
            return AFMT_U16_LE;
        }

    default:
        dolog ("Internal logic error: Bad audio format %d\n", fmt);
#ifdef DEBUG_AUDIO
        abort ();
#endif
        return AFMT_U8;
    }
}

static int oss_to_audfmt (int ossfmt, AudioFormat *fmt, int *endianness)
{
    switch (ossfmt) {
    case AFMT_S8:
        *endianness = 0;
        *fmt = AUDIO_FORMAT_S8;
        break;

    case AFMT_U8:
        *endianness = 0;
        *fmt = AUDIO_FORMAT_U8;
        break;

    case AFMT_S16_LE:
        *endianness = 0;
        *fmt = AUDIO_FORMAT_S16;
        break;

    case AFMT_U16_LE:
        *endianness = 0;
        *fmt = AUDIO_FORMAT_U16;
        break;

    case AFMT_S16_BE:
        *endianness = 1;
        *fmt = AUDIO_FORMAT_S16;
        break;

    case AFMT_U16_BE:
        *endianness = 1;
        *fmt = AUDIO_FORMAT_U16;
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

#ifdef USE_DSP_POLICY
static int oss_get_version (int fd, int *version, const char *typ)
{
    if (ioctl (fd, OSS_GETVERSION, &version)) {
#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
        /*
         * Looks like atm (20100109) FreeBSD knows OSS_GETVERSION
         * since 7.x, but currently only on the mixer device (or in
         * the Linuxolator), and in the native version that part of
         * the code is in fact never reached so the ioctl fails anyway.
         * Until this is fixed, just check the errno and if its what
         * FreeBSD's sound drivers return atm assume they are new enough.
         */
        if (errno == EINVAL) {
            *version = 0x040000;
            return 0;
        }
#endif
        oss_logerr2 (errno, typ, "Failed to get OSS version\n");
        return -1;
    }
    return 0;
}
#endif

static int oss_open(int in, struct oss_params *req, audsettings *as,
                    struct oss_params *obt, int *pfd, Audiodev *dev)
{
    AudiodevOssOptions *oopts = &dev->u.oss;
    AudiodevOssPerDirectionOptions *opdo = in ? oopts->in : oopts->out;
    int fd;
    int oflags = (oopts->has_exclusive && oopts->exclusive) ? O_EXCL : 0;
    audio_buf_info abinfo;
    int fmt, freq, nchannels;
    int setfragment = 1;
    const char *dspname = opdo->has_dev ? opdo->dev : "/dev/dsp";
    const char *typ = in ? "ADC" : "DAC";
#ifdef USE_DSP_POLICY
    int policy = oopts->has_dsp_policy ? oopts->dsp_policy : 5;
#endif

    /* Kludge needed to have working mmap on Linux */
    oflags |= (oopts->has_try_mmap && oopts->try_mmap) ?
        O_RDWR : (in ? O_RDONLY : O_WRONLY);

    fd = open (dspname, oflags | O_NONBLOCK);
    if (-1 == fd) {
        oss_logerr2 (errno, typ, "Failed to open `%s'\n", dspname);
        return -1;
    }

    freq = req->freq;
    nchannels = req->nchannels;
    fmt = req->fmt;
    req->nfrags = opdo->has_buffer_count ? opdo->buffer_count : 4;
    req->fragsize = audio_buffer_bytes(
        qapi_AudiodevOssPerDirectionOptions_base(opdo), as, 23220);

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

#ifdef USE_DSP_POLICY
    if (policy >= 0) {
        int version;

        if (!oss_get_version (fd, &version, typ)) {
            trace_oss_version(version);

            if (version >= 0x040000) {
                int policy2 = policy;
                if (ioctl(fd, SNDCTL_DSP_POLICY, &policy2)) {
                    oss_logerr2 (errno, typ,
                                 "Failed to set timing policy to %d\n",
                                 policy);
                    goto err;
                }
                setfragment = 0;
            }
        }
    }
#endif

    if (setfragment) {
        int mmmmssss = (req->nfrags << 16) | ctz32 (req->fragsize);
        if (ioctl (fd, SNDCTL_DSP_SETFRAGMENT, &mmmmssss)) {
            oss_logerr2 (errno, typ, "Failed to set buffer length (%d, %d)\n",
                         req->nfrags, req->fragsize);
            goto err;
        }
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

static size_t oss_get_available_bytes(OSSVoiceOut *oss)
{
    int err;
    struct count_info cntinfo;
    assert(oss->mmapped);

    err = ioctl(oss->fd, SNDCTL_DSP_GETOPTR, &cntinfo);
    if (err < 0) {
        oss_logerr(errno, "SNDCTL_DSP_GETOPTR failed\n");
        return 0;
    }

    return audio_ring_dist(cntinfo.ptr, oss->hw.pos_emul, oss->hw.size_emul);
}

static void *oss_get_buffer_out(HWVoiceOut *hw, size_t *size)
{
    OSSVoiceOut *oss = (OSSVoiceOut *) hw;
    if (oss->mmapped) {
        *size = MIN(oss_get_available_bytes(oss), hw->size_emul - hw->pos_emul);
        return hw->buf_emul + hw->pos_emul;
    } else {
        return audio_generic_get_buffer_out(hw, size);
    }
}

static size_t oss_put_buffer_out(HWVoiceOut *hw, void *buf, size_t size)
{
    OSSVoiceOut *oss = (OSSVoiceOut *) hw;
    if (oss->mmapped) {
        assert(buf == hw->buf_emul + hw->pos_emul && size < hw->size_emul);

        hw->pos_emul = (hw->pos_emul + size) % hw->size_emul;
        return size;
    } else {
        return audio_generic_put_buffer_out(hw, buf, size);
    }
}

static size_t oss_write(HWVoiceOut *hw, void *buf, size_t len)
{
    OSSVoiceOut *oss = (OSSVoiceOut *) hw;
    size_t pos;

    if (oss->mmapped) {
        size_t total_len;
        len = MIN(len, oss_get_available_bytes(oss));

        total_len = len;
        while (len) {
            size_t to_copy = MIN(len, hw->size_emul - hw->pos_emul);
            memcpy(hw->buf_emul + hw->pos_emul, buf, to_copy);

            hw->pos_emul = (hw->pos_emul + to_copy) % hw->pos_emul;
            buf += to_copy;
            len -= to_copy;
        }
        return total_len;
    }

    pos = 0;
    while (len) {
        ssize_t bytes_written;
        void *pcm = advance(buf, pos);

        bytes_written = write(oss->fd, pcm, len);
        if (bytes_written < 0) {
            if (errno != EAGAIN) {
                oss_logerr(errno, "failed to write %zu bytes\n",
                           len);
            }
            return pos;
        }

        pos += bytes_written;
        if (bytes_written < len) {
            break;
        }
        len -= bytes_written;
    }
    return pos;
}

static void oss_fini_out (HWVoiceOut *hw)
{
    int err;
    OSSVoiceOut *oss = (OSSVoiceOut *) hw;

    ldebug ("oss_fini\n");
    oss_anal_close (&oss->fd);

    if (oss->mmapped && hw->buf_emul) {
        err = munmap(hw->buf_emul, hw->size_emul);
        if (err) {
            oss_logerr(errno, "Failed to unmap buffer %p, size %zu\n",
                       hw->buf_emul, hw->size_emul);
        }
        hw->buf_emul = NULL;
    }
}

static int oss_init_out(HWVoiceOut *hw, struct audsettings *as,
                        void *drv_opaque)
{
    OSSVoiceOut *oss = (OSSVoiceOut *) hw;
    struct oss_params req, obt;
    int endianness;
    int err;
    int fd;
    AudioFormat effective_fmt;
    struct audsettings obt_as;
    Audiodev *dev = drv_opaque;
    AudiodevOssOptions *oopts = &dev->u.oss;

    oss->fd = -1;

    req.fmt = aud_to_ossfmt (as->fmt, as->endianness);
    req.freq = as->freq;
    req.nchannels = as->nchannels;

    if (oss_open(0, &req, as, &obt, &fd, dev)) {
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

    if (obt.nfrags * obt.fragsize % hw->info.bytes_per_frame) {
        dolog ("warning: Misaligned DAC buffer, size %d, alignment %d\n",
               obt.nfrags * obt.fragsize, hw->info.bytes_per_frame);
    }

    hw->samples = (obt.nfrags * obt.fragsize) / hw->info.bytes_per_frame;

    oss->mmapped = 0;
    if (oopts->has_try_mmap && oopts->try_mmap) {
        hw->size_emul = hw->samples * hw->info.bytes_per_frame;
        hw->buf_emul = mmap(
            NULL,
            hw->size_emul,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            fd,
            0
            );
        if (hw->buf_emul == MAP_FAILED) {
            oss_logerr(errno, "Failed to map %zu bytes of DAC\n",
                       hw->size_emul);
            hw->buf_emul = NULL;
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
                err = munmap(hw->buf_emul, hw->size_emul);
                if (err) {
                    oss_logerr(errno, "Failed to unmap buffer %p size %zu\n",
                               hw->buf_emul, hw->size_emul);
                }
                hw->buf_emul = NULL;
            }
        }
    }

    oss->fd = fd;
    oss->dev = dev;
    return 0;
}

static void oss_enable_out(HWVoiceOut *hw, bool enable)
{
    int trig;
    OSSVoiceOut *oss = (OSSVoiceOut *) hw;
    AudiodevOssPerDirectionOptions *opdo = oss->dev->u.oss.out;

    if (enable) {
        bool poll_mode = opdo->try_poll;

        ldebug("enabling voice\n");
        if (poll_mode) {
            oss_poll_out(hw);
            poll_mode = 0;
        }
        hw->poll_mode = poll_mode;

        if (!oss->mmapped) {
            return;
        }

        audio_pcm_info_clear_buf(&hw->info, hw->buf_emul, hw->mix_buf->size);
        trig = PCM_ENABLE_OUTPUT;
        if (ioctl(oss->fd, SNDCTL_DSP_SETTRIGGER, &trig) < 0) {
            oss_logerr(errno,
                       "SNDCTL_DSP_SETTRIGGER PCM_ENABLE_OUTPUT failed\n");
            return;
        }
    } else {
        if (hw->poll_mode) {
            qemu_set_fd_handler (oss->fd, NULL, NULL, NULL);
            hw->poll_mode = 0;
        }

        if (!oss->mmapped) {
            return;
        }

        ldebug ("disabling voice\n");
        trig = 0;
        if (ioctl (oss->fd, SNDCTL_DSP_SETTRIGGER, &trig) < 0) {
            oss_logerr (errno, "SNDCTL_DSP_SETTRIGGER 0 failed\n");
            return;
        }
    }
}

static int oss_init_in(HWVoiceIn *hw, struct audsettings *as, void *drv_opaque)
{
    OSSVoiceIn *oss = (OSSVoiceIn *) hw;
    struct oss_params req, obt;
    int endianness;
    int err;
    int fd;
    AudioFormat effective_fmt;
    struct audsettings obt_as;
    Audiodev *dev = drv_opaque;

    oss->fd = -1;

    req.fmt = aud_to_ossfmt (as->fmt, as->endianness);
    req.freq = as->freq;
    req.nchannels = as->nchannels;
    if (oss_open(1, &req, as, &obt, &fd, dev)) {
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

    if (obt.nfrags * obt.fragsize % hw->info.bytes_per_frame) {
        dolog ("warning: Misaligned ADC buffer, size %d, alignment %d\n",
               obt.nfrags * obt.fragsize, hw->info.bytes_per_frame);
    }

    hw->samples = (obt.nfrags * obt.fragsize) / hw->info.bytes_per_frame;

    oss->fd = fd;
    oss->dev = dev;
    return 0;
}

static void oss_fini_in (HWVoiceIn *hw)
{
    OSSVoiceIn *oss = (OSSVoiceIn *) hw;

    oss_anal_close (&oss->fd);
}

static size_t oss_read(HWVoiceIn *hw, void *buf, size_t len)
{
    OSSVoiceIn *oss = (OSSVoiceIn *) hw;
    size_t pos = 0;

    while (len) {
        ssize_t nread;

        void *dst = advance(buf, pos);
        nread = read(oss->fd, dst, len);

        if (nread == -1) {
            switch (errno) {
            case EINTR:
            case EAGAIN:
                break;
            default:
                oss_logerr(errno, "Failed to read %zu bytes of audio (to %p)\n",
                           len, dst);
                break;
            }
        }

        pos += nread;
        len -= nread;
    }

    return pos;
}

static void oss_enable_in(HWVoiceIn *hw, bool enable)
{
    OSSVoiceIn *oss = (OSSVoiceIn *) hw;
    AudiodevOssPerDirectionOptions *opdo = oss->dev->u.oss.out;

    if (enable) {
        bool poll_mode = opdo->try_poll;

        if (poll_mode) {
            oss_poll_in(hw);
            poll_mode = 0;
        }
        hw->poll_mode = poll_mode;
    } else {
        if (hw->poll_mode) {
            hw->poll_mode = 0;
            qemu_set_fd_handler (oss->fd, NULL, NULL, NULL);
        }
    }
}

static void oss_init_per_direction(AudiodevOssPerDirectionOptions *opdo)
{
    if (!opdo->has_try_poll) {
        opdo->try_poll = true;
        opdo->has_try_poll = true;
    }
}

static void *oss_audio_init(Audiodev *dev)
{
    AudiodevOssOptions *oopts;
    assert(dev->driver == AUDIODEV_DRIVER_OSS);

    oopts = &dev->u.oss;
    oss_init_per_direction(oopts->in);
    oss_init_per_direction(oopts->out);

    if (access(oopts->in->has_dev ? oopts->in->dev : "/dev/dsp",
               R_OK | W_OK) < 0 ||
        access(oopts->out->has_dev ? oopts->out->dev : "/dev/dsp",
               R_OK | W_OK) < 0) {
        return NULL;
    }
    return dev;
}

static void oss_audio_fini (void *opaque)
{
}

static struct audio_pcm_ops oss_pcm_ops = {
    .init_out = oss_init_out,
    .fini_out = oss_fini_out,
    .write    = oss_write,
    .get_buffer_out = oss_get_buffer_out,
    .put_buffer_out = oss_put_buffer_out,
    .enable_out = oss_enable_out,

    .init_in  = oss_init_in,
    .fini_in  = oss_fini_in,
    .read     = oss_read,
    .enable_in = oss_enable_in
};

static struct audio_driver oss_audio_driver = {
    .name           = "oss",
    .descr          = "OSS http://www.opensound.com",
    .init           = oss_audio_init,
    .fini           = oss_audio_fini,
    .pcm_ops        = &oss_pcm_ops,
    .can_be_default = 1,
    .max_voices_out = INT_MAX,
    .max_voices_in  = INT_MAX,
    .voice_size_out = sizeof (OSSVoiceOut),
    .voice_size_in  = sizeof (OSSVoiceIn)
};

static void register_audio_oss(void)
{
    audio_driver_register(&oss_audio_driver);
}
type_init(register_audio_oss);

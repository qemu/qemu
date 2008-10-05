/*
 * QEMU OS X CoreAudio audio driver
 *
 * Copyright (c) 2005 Mike Kronenberg
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

#include <CoreAudio/CoreAudio.h>
#include <string.h>             /* strerror */
#include <pthread.h>            /* pthread_X */

#include "qemu-common.h"
#include "audio.h"

#define AUDIO_CAP "coreaudio"
#include "audio_int.h"

struct {
    int buffer_frames;
    int nbuffers;
    int isAtexit;
} conf = {
    .buffer_frames = 512,
    .nbuffers = 4,
    .isAtexit = 0
};

typedef struct coreaudioVoiceOut {
    HWVoiceOut hw;
    pthread_mutex_t mutex;
    int isAtexit;
    AudioDeviceID outputDeviceID;
    UInt32 audioDevicePropertyBufferFrameSize;
    AudioStreamBasicDescription outputStreamBasicDescription;
    int live;
    int decr;
    int rpos;
} coreaudioVoiceOut;

static void coreaudio_logstatus (OSStatus status)
{
    char *str = "BUG";

    switch(status) {
    case kAudioHardwareNoError:
        str = "kAudioHardwareNoError";
        break;

    case kAudioHardwareNotRunningError:
        str = "kAudioHardwareNotRunningError";
        break;

    case kAudioHardwareUnspecifiedError:
        str = "kAudioHardwareUnspecifiedError";
        break;

    case kAudioHardwareUnknownPropertyError:
        str = "kAudioHardwareUnknownPropertyError";
        break;

    case kAudioHardwareBadPropertySizeError:
        str = "kAudioHardwareBadPropertySizeError";
        break;

    case kAudioHardwareIllegalOperationError:
        str = "kAudioHardwareIllegalOperationError";
        break;

    case kAudioHardwareBadDeviceError:
        str = "kAudioHardwareBadDeviceError";
        break;

    case kAudioHardwareBadStreamError:
        str = "kAudioHardwareBadStreamError";
        break;

    case kAudioHardwareUnsupportedOperationError:
        str = "kAudioHardwareUnsupportedOperationError";
        break;

    case kAudioDeviceUnsupportedFormatError:
        str = "kAudioDeviceUnsupportedFormatError";
        break;

    case kAudioDevicePermissionsError:
        str = "kAudioDevicePermissionsError";
        break;

    default:
        AUD_log (AUDIO_CAP, "Reason: status code %ld\n", status);
        return;
    }

    AUD_log (AUDIO_CAP, "Reason: %s\n", str);
}

static void GCC_FMT_ATTR (2, 3) coreaudio_logerr (
    OSStatus status,
    const char *fmt,
    ...
    )
{
    va_list ap;

    va_start (ap, fmt);
    AUD_log (AUDIO_CAP, fmt, ap);
    va_end (ap);

    coreaudio_logstatus (status);
}

static void GCC_FMT_ATTR (3, 4) coreaudio_logerr2 (
    OSStatus status,
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

    coreaudio_logstatus (status);
}

static inline UInt32 isPlaying (AudioDeviceID outputDeviceID)
{
    OSStatus status;
    UInt32 result = 0;
    UInt32 propertySize = sizeof(outputDeviceID);
    status = AudioDeviceGetProperty(
        outputDeviceID, 0, 0,
        kAudioDevicePropertyDeviceIsRunning, &propertySize, &result);
    if (status != kAudioHardwareNoError) {
        coreaudio_logerr(status,
                         "Could not determine whether Device is playing\n");
    }
    return result;
}

static void coreaudio_atexit (void)
{
    conf.isAtexit = 1;
}

static int coreaudio_lock (coreaudioVoiceOut *core, const char *fn_name)
{
    int err;

    err = pthread_mutex_lock (&core->mutex);
    if (err) {
        dolog ("Could not lock voice for %s\nReason: %s\n",
               fn_name, strerror (err));
        return -1;
    }
    return 0;
}

static int coreaudio_unlock (coreaudioVoiceOut *core, const char *fn_name)
{
    int err;

    err = pthread_mutex_unlock (&core->mutex);
    if (err) {
        dolog ("Could not unlock voice for %s\nReason: %s\n",
               fn_name, strerror (err));
        return -1;
    }
    return 0;
}

static int coreaudio_run_out (HWVoiceOut *hw)
{
    int live, decr;
    coreaudioVoiceOut *core = (coreaudioVoiceOut *) hw;

    if (coreaudio_lock (core, "coreaudio_run_out")) {
        return 0;
    }

    live = audio_pcm_hw_get_live_out (hw);

    if (core->decr > live) {
        ldebug ("core->decr %d live %d core->live %d\n",
                core->decr,
                live,
                core->live);
    }

    decr = audio_MIN (core->decr, live);
    core->decr -= decr;

    core->live = live - decr;
    hw->rpos = core->rpos;

    coreaudio_unlock (core, "coreaudio_run_out");
    return decr;
}

/* callback to feed audiooutput buffer */
static OSStatus audioDeviceIOProc(
    AudioDeviceID inDevice,
    const AudioTimeStamp* inNow,
    const AudioBufferList* inInputData,
    const AudioTimeStamp* inInputTime,
    AudioBufferList* outOutputData,
    const AudioTimeStamp* inOutputTime,
    void* hwptr)
{
    UInt32 frame, frameCount;
    float *out = outOutputData->mBuffers[0].mData;
    HWVoiceOut *hw = hwptr;
    coreaudioVoiceOut *core = (coreaudioVoiceOut *) hwptr;
    int rpos, live;
    st_sample_t *src;
#ifndef FLOAT_MIXENG
#ifdef RECIPROCAL
    const float scale = 1.f / UINT_MAX;
#else
    const float scale = UINT_MAX;
#endif
#endif

    if (coreaudio_lock (core, "audioDeviceIOProc")) {
        inInputTime = 0;
        return 0;
    }

    frameCount = core->audioDevicePropertyBufferFrameSize;
    live = core->live;

    /* if there are not enough samples, set signal and return */
    if (live < frameCount) {
        inInputTime = 0;
        coreaudio_unlock (core, "audioDeviceIOProc(empty)");
        return 0;
    }

    rpos = core->rpos;
    src = hw->mix_buf + rpos;

    /* fill buffer */
    for (frame = 0; frame < frameCount; frame++) {
#ifdef FLOAT_MIXENG
        *out++ = src[frame].l; /* left channel */
        *out++ = src[frame].r; /* right channel */
#else
#ifdef RECIPROCAL
        *out++ = src[frame].l * scale; /* left channel */
        *out++ = src[frame].r * scale; /* right channel */
#else
        *out++ = src[frame].l / scale; /* left channel */
        *out++ = src[frame].r / scale; /* right channel */
#endif
#endif
    }

    rpos = (rpos + frameCount) % hw->samples;
    core->decr += frameCount;
    core->rpos = rpos;

    coreaudio_unlock (core, "audioDeviceIOProc");
    return 0;
}

static int coreaudio_write (SWVoiceOut *sw, void *buf, int len)
{
    return audio_pcm_sw_write (sw, buf, len);
}

static int coreaudio_init_out (HWVoiceOut *hw, audsettings_t *as)
{
    OSStatus status;
    coreaudioVoiceOut *core = (coreaudioVoiceOut *) hw;
    UInt32 propertySize;
    int err;
    const char *typ = "playback";
    AudioValueRange frameRange;

    /* create mutex */
    err = pthread_mutex_init(&core->mutex, NULL);
    if (err) {
        dolog("Could not create mutex\nReason: %s\n", strerror (err));
        return -1;
    }

    audio_pcm_init_info (&hw->info, as);

    /* open default output device */
    propertySize = sizeof(core->outputDeviceID);
    status = AudioHardwareGetProperty(
        kAudioHardwarePropertyDefaultOutputDevice,
        &propertySize,
        &core->outputDeviceID);
    if (status != kAudioHardwareNoError) {
        coreaudio_logerr2 (status, typ,
                           "Could not get default output Device\n");
        return -1;
    }
    if (core->outputDeviceID == kAudioDeviceUnknown) {
        dolog ("Could not initialize %s - Unknown Audiodevice\n", typ);
        return -1;
    }

    /* get minimum and maximum buffer frame sizes */
    propertySize = sizeof(frameRange);
    status = AudioDeviceGetProperty(
        core->outputDeviceID,
        0,
        0,
        kAudioDevicePropertyBufferFrameSizeRange,
        &propertySize,
        &frameRange);
    if (status != kAudioHardwareNoError) {
        coreaudio_logerr2 (status, typ,
                           "Could not get device buffer frame range\n");
        return -1;
    }

    if (frameRange.mMinimum > conf.buffer_frames) {
        core->audioDevicePropertyBufferFrameSize = (UInt32) frameRange.mMinimum;
        dolog ("warning: Upsizing Buffer Frames to %f\n", frameRange.mMinimum);
    }
    else if (frameRange.mMaximum < conf.buffer_frames) {
        core->audioDevicePropertyBufferFrameSize = (UInt32) frameRange.mMaximum;
        dolog ("warning: Downsizing Buffer Frames to %f\n", frameRange.mMaximum);
    }
    else {
        core->audioDevicePropertyBufferFrameSize = conf.buffer_frames;
    }

    /* set Buffer Frame Size */
    propertySize = sizeof(core->audioDevicePropertyBufferFrameSize);
    status = AudioDeviceSetProperty(
        core->outputDeviceID,
        NULL,
        0,
        false,
        kAudioDevicePropertyBufferFrameSize,
        propertySize,
        &core->audioDevicePropertyBufferFrameSize);
    if (status != kAudioHardwareNoError) {
        coreaudio_logerr2 (status, typ,
                           "Could not set device buffer frame size %ld\n",
                           core->audioDevicePropertyBufferFrameSize);
        return -1;
    }

    /* get Buffer Frame Size */
    propertySize = sizeof(core->audioDevicePropertyBufferFrameSize);
    status = AudioDeviceGetProperty(
        core->outputDeviceID,
        0,
        false,
        kAudioDevicePropertyBufferFrameSize,
        &propertySize,
        &core->audioDevicePropertyBufferFrameSize);
    if (status != kAudioHardwareNoError) {
        coreaudio_logerr2 (status, typ,
                           "Could not get device buffer frame size\n");
        return -1;
    }
    hw->samples = conf.nbuffers * core->audioDevicePropertyBufferFrameSize;

    /* get StreamFormat */
    propertySize = sizeof(core->outputStreamBasicDescription);
    status = AudioDeviceGetProperty(
        core->outputDeviceID,
        0,
        false,
        kAudioDevicePropertyStreamFormat,
        &propertySize,
        &core->outputStreamBasicDescription);
    if (status != kAudioHardwareNoError) {
        coreaudio_logerr2 (status, typ,
                           "Could not get Device Stream properties\n");
        core->outputDeviceID = kAudioDeviceUnknown;
        return -1;
    }

    /* set Samplerate */
    core->outputStreamBasicDescription.mSampleRate = (Float64) as->freq;
    propertySize = sizeof(core->outputStreamBasicDescription);
    status = AudioDeviceSetProperty(
        core->outputDeviceID,
        0,
        0,
        0,
        kAudioDevicePropertyStreamFormat,
        propertySize,
        &core->outputStreamBasicDescription);
    if (status != kAudioHardwareNoError) {
        coreaudio_logerr2 (status, typ, "Could not set samplerate %d\n",
                           as->freq);
        core->outputDeviceID = kAudioDeviceUnknown;
        return -1;
    }

    /* set Callback */
    status = AudioDeviceAddIOProc(core->outputDeviceID, audioDeviceIOProc, hw);
    if (status != kAudioHardwareNoError) {
        coreaudio_logerr2 (status, typ, "Could not set IOProc\n");
        core->outputDeviceID = kAudioDeviceUnknown;
        return -1;
    }

    /* start Playback */
    if (!isPlaying(core->outputDeviceID)) {
        status = AudioDeviceStart(core->outputDeviceID, audioDeviceIOProc);
        if (status != kAudioHardwareNoError) {
            coreaudio_logerr2 (status, typ, "Could not start playback\n");
            AudioDeviceRemoveIOProc(core->outputDeviceID, audioDeviceIOProc);
            core->outputDeviceID = kAudioDeviceUnknown;
            return -1;
        }
    }

    return 0;
}

static void coreaudio_fini_out (HWVoiceOut *hw)
{
    OSStatus status;
    int err;
    coreaudioVoiceOut *core = (coreaudioVoiceOut *) hw;

    if (!conf.isAtexit) {
        /* stop playback */
        if (isPlaying(core->outputDeviceID)) {
            status = AudioDeviceStop(core->outputDeviceID, audioDeviceIOProc);
            if (status != kAudioHardwareNoError) {
                coreaudio_logerr (status, "Could not stop playback\n");
            }
        }

        /* remove callback */
        status = AudioDeviceRemoveIOProc(core->outputDeviceID,
                                         audioDeviceIOProc);
        if (status != kAudioHardwareNoError) {
            coreaudio_logerr (status, "Could not remove IOProc\n");
        }
    }
    core->outputDeviceID = kAudioDeviceUnknown;

    /* destroy mutex */
    err = pthread_mutex_destroy(&core->mutex);
    if (err) {
        dolog("Could not destroy mutex\nReason: %s\n", strerror (err));
    }
}

static int coreaudio_ctl_out (HWVoiceOut *hw, int cmd, ...)
{
    OSStatus status;
    coreaudioVoiceOut *core = (coreaudioVoiceOut *) hw;

    switch (cmd) {
    case VOICE_ENABLE:
        /* start playback */
        if (!isPlaying(core->outputDeviceID)) {
            status = AudioDeviceStart(core->outputDeviceID, audioDeviceIOProc);
            if (status != kAudioHardwareNoError) {
                coreaudio_logerr (status, "Could not resume playback\n");
            }
        }
        break;

    case VOICE_DISABLE:
        /* stop playback */
        if (!conf.isAtexit) {
            if (isPlaying(core->outputDeviceID)) {
                status = AudioDeviceStop(core->outputDeviceID, audioDeviceIOProc);
                if (status != kAudioHardwareNoError) {
                    coreaudio_logerr (status, "Could not pause playback\n");
                }
            }
        }
        break;
    }
    return 0;
}

static void *coreaudio_audio_init (void)
{
    atexit(coreaudio_atexit);
    return &coreaudio_audio_init;
}

static void coreaudio_audio_fini (void *opaque)
{
    (void) opaque;
}

static struct audio_option coreaudio_options[] = {
    {"BUFFER_SIZE", AUD_OPT_INT, &conf.buffer_frames,
     "Size of the buffer in frames", NULL, 0},
    {"BUFFER_COUNT", AUD_OPT_INT, &conf.nbuffers,
     "Number of buffers", NULL, 0},
    {NULL, 0, NULL, NULL, NULL, 0}
};

static const struct audio_pcm_ops coreaudio_pcm_ops = {
    coreaudio_init_out,
    coreaudio_fini_out,
    coreaudio_run_out,
    coreaudio_write,
    coreaudio_ctl_out,

    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

struct audio_driver coreaudio_audio_driver = {
    INIT_FIELD (name           = ) "coreaudio",
    INIT_FIELD (descr          = )
    "CoreAudio http://developer.apple.com/audio/coreaudio.html",
    INIT_FIELD (options        = ) coreaudio_options,
    INIT_FIELD (init           = ) coreaudio_audio_init,
    INIT_FIELD (fini           = ) coreaudio_audio_fini,
    INIT_FIELD (pcm_ops        = ) &coreaudio_pcm_ops,
    INIT_FIELD (can_be_default = ) 1,
    INIT_FIELD (max_voices_out = ) 1,
    INIT_FIELD (max_voices_in  = ) 0,
    INIT_FIELD (voice_size_out = ) sizeof (coreaudioVoiceOut),
    INIT_FIELD (voice_size_in  = ) 0
};

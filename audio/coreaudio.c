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

#include "qemu/osdep.h"
#include <CoreAudio/CoreAudio.h>
#include <pthread.h>            /* pthread_X */

#include "qemu-common.h"
#include "audio.h"

#define AUDIO_CAP "coreaudio"
#include "audio_int.h"

#ifndef MAC_OS_X_VERSION_10_6
#define MAC_OS_X_VERSION_10_6 1060
#endif

static int isAtexit;

typedef struct {
    int buffer_frames;
    int nbuffers;
} CoreaudioConf;

typedef struct coreaudioVoiceOut {
    HWVoiceOut hw;
    pthread_mutex_t mutex;
    AudioDeviceID outputDeviceID;
    UInt32 audioDevicePropertyBufferFrameSize;
    AudioStreamBasicDescription outputStreamBasicDescription;
    AudioDeviceIOProcID ioprocid;
    int live;
    int decr;
    int rpos;
} coreaudioVoiceOut;

#if MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_6
/* The APIs used here only become available from 10.6 */

static OSStatus coreaudio_get_voice(AudioDeviceID *id)
{
    UInt32 size = sizeof(*id);
    AudioObjectPropertyAddress addr = {
        kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMaster
    };

    return AudioObjectGetPropertyData(kAudioObjectSystemObject,
                                      &addr,
                                      0,
                                      NULL,
                                      &size,
                                      id);
}

static OSStatus coreaudio_get_framesizerange(AudioDeviceID id,
                                             AudioValueRange *framerange)
{
    UInt32 size = sizeof(*framerange);
    AudioObjectPropertyAddress addr = {
        kAudioDevicePropertyBufferFrameSizeRange,
        kAudioDevicePropertyScopeOutput,
        kAudioObjectPropertyElementMaster
    };

    return AudioObjectGetPropertyData(id,
                                      &addr,
                                      0,
                                      NULL,
                                      &size,
                                      framerange);
}

static OSStatus coreaudio_get_framesize(AudioDeviceID id, UInt32 *framesize)
{
    UInt32 size = sizeof(*framesize);
    AudioObjectPropertyAddress addr = {
        kAudioDevicePropertyBufferFrameSize,
        kAudioDevicePropertyScopeOutput,
        kAudioObjectPropertyElementMaster
    };

    return AudioObjectGetPropertyData(id,
                                      &addr,
                                      0,
                                      NULL,
                                      &size,
                                      framesize);
}

static OSStatus coreaudio_set_framesize(AudioDeviceID id, UInt32 *framesize)
{
    UInt32 size = sizeof(*framesize);
    AudioObjectPropertyAddress addr = {
        kAudioDevicePropertyBufferFrameSize,
        kAudioDevicePropertyScopeOutput,
        kAudioObjectPropertyElementMaster
    };

    return AudioObjectSetPropertyData(id,
                                      &addr,
                                      0,
                                      NULL,
                                      size,
                                      framesize);
}

static OSStatus coreaudio_get_streamformat(AudioDeviceID id,
                                           AudioStreamBasicDescription *d)
{
    UInt32 size = sizeof(*d);
    AudioObjectPropertyAddress addr = {
        kAudioDevicePropertyStreamFormat,
        kAudioDevicePropertyScopeOutput,
        kAudioObjectPropertyElementMaster
    };

    return AudioObjectGetPropertyData(id,
                                      &addr,
                                      0,
                                      NULL,
                                      &size,
                                      d);
}

static OSStatus coreaudio_set_streamformat(AudioDeviceID id,
                                           AudioStreamBasicDescription *d)
{
    UInt32 size = sizeof(*d);
    AudioObjectPropertyAddress addr = {
        kAudioDevicePropertyStreamFormat,
        kAudioDevicePropertyScopeOutput,
        kAudioObjectPropertyElementMaster
    };

    return AudioObjectSetPropertyData(id,
                                      &addr,
                                      0,
                                      NULL,
                                      size,
                                      d);
}

static OSStatus coreaudio_get_isrunning(AudioDeviceID id, UInt32 *result)
{
    UInt32 size = sizeof(*result);
    AudioObjectPropertyAddress addr = {
        kAudioDevicePropertyDeviceIsRunning,
        kAudioDevicePropertyScopeOutput,
        kAudioObjectPropertyElementMaster
    };

    return AudioObjectGetPropertyData(id,
                                      &addr,
                                      0,
                                      NULL,
                                      &size,
                                      result);
}
#else
/* Legacy versions of functions using deprecated APIs */

static OSStatus coreaudio_get_voice(AudioDeviceID *id)
{
    UInt32 size = sizeof(*id);

    return AudioHardwareGetProperty(
        kAudioHardwarePropertyDefaultOutputDevice,
        &size,
        id);
}

static OSStatus coreaudio_get_framesizerange(AudioDeviceID id,
                                             AudioValueRange *framerange)
{
    UInt32 size = sizeof(*framerange);

    return AudioDeviceGetProperty(
        id,
        0,
        0,
        kAudioDevicePropertyBufferFrameSizeRange,
        &size,
        framerange);
}

static OSStatus coreaudio_get_framesize(AudioDeviceID id, UInt32 *framesize)
{
    UInt32 size = sizeof(*framesize);

    return AudioDeviceGetProperty(
        id,
        0,
        false,
        kAudioDevicePropertyBufferFrameSize,
        &size,
        framesize);
}

static OSStatus coreaudio_set_framesize(AudioDeviceID id, UInt32 *framesize)
{
    UInt32 size = sizeof(*framesize);

    return AudioDeviceSetProperty(
        id,
        NULL,
        0,
        false,
        kAudioDevicePropertyBufferFrameSize,
        size,
        framesize);
}

static OSStatus coreaudio_get_streamformat(AudioDeviceID id,
                                           AudioStreamBasicDescription *d)
{
    UInt32 size = sizeof(*d);

    return AudioDeviceGetProperty(
        id,
        0,
        false,
        kAudioDevicePropertyStreamFormat,
        &size,
        d);
}

static OSStatus coreaudio_set_streamformat(AudioDeviceID id,
                                           AudioStreamBasicDescription *d)
{
    UInt32 size = sizeof(*d);

    return AudioDeviceSetProperty(
        id,
        0,
        0,
        0,
        kAudioDevicePropertyStreamFormat,
        size,
        d);
}

static OSStatus coreaudio_get_isrunning(AudioDeviceID id, UInt32 *result)
{
    UInt32 size = sizeof(*result);

    return AudioDeviceGetProperty(
        id,
        0,
        0,
        kAudioDevicePropertyDeviceIsRunning,
        &size,
        result);
}
#endif

static void coreaudio_logstatus (OSStatus status)
{
    const char *str = "BUG";

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
        AUD_log (AUDIO_CAP, "Reason: status code %" PRId32 "\n", (int32_t)status);
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
    status = coreaudio_get_isrunning(outputDeviceID, &result);
    if (status != kAudioHardwareNoError) {
        coreaudio_logerr(status,
                         "Could not determine whether Device is playing\n");
    }
    return result;
}

static void coreaudio_atexit (void)
{
    isAtexit = 1;
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

static int coreaudio_run_out (HWVoiceOut *hw, int live)
{
    int decr;
    coreaudioVoiceOut *core = (coreaudioVoiceOut *) hw;

    if (coreaudio_lock (core, "coreaudio_run_out")) {
        return 0;
    }

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
    struct st_sample *src;
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

static int coreaudio_init_out(HWVoiceOut *hw, struct audsettings *as,
                              void *drv_opaque)
{
    OSStatus status;
    coreaudioVoiceOut *core = (coreaudioVoiceOut *) hw;
    int err;
    const char *typ = "playback";
    AudioValueRange frameRange;
    CoreaudioConf *conf = drv_opaque;

    /* create mutex */
    err = pthread_mutex_init(&core->mutex, NULL);
    if (err) {
        dolog("Could not create mutex\nReason: %s\n", strerror (err));
        return -1;
    }

    audio_pcm_init_info (&hw->info, as);

    status = coreaudio_get_voice(&core->outputDeviceID);
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
    status = coreaudio_get_framesizerange(core->outputDeviceID,
                                          &frameRange);
    if (status != kAudioHardwareNoError) {
        coreaudio_logerr2 (status, typ,
                           "Could not get device buffer frame range\n");
        return -1;
    }

    if (frameRange.mMinimum > conf->buffer_frames) {
        core->audioDevicePropertyBufferFrameSize = (UInt32) frameRange.mMinimum;
        dolog ("warning: Upsizing Buffer Frames to %f\n", frameRange.mMinimum);
    }
    else if (frameRange.mMaximum < conf->buffer_frames) {
        core->audioDevicePropertyBufferFrameSize = (UInt32) frameRange.mMaximum;
        dolog ("warning: Downsizing Buffer Frames to %f\n", frameRange.mMaximum);
    }
    else {
        core->audioDevicePropertyBufferFrameSize = conf->buffer_frames;
    }

    /* set Buffer Frame Size */
    status = coreaudio_set_framesize(core->outputDeviceID,
                                     &core->audioDevicePropertyBufferFrameSize);
    if (status != kAudioHardwareNoError) {
        coreaudio_logerr2 (status, typ,
                           "Could not set device buffer frame size %" PRIu32 "\n",
                           (uint32_t)core->audioDevicePropertyBufferFrameSize);
        return -1;
    }

    /* get Buffer Frame Size */
    status = coreaudio_get_framesize(core->outputDeviceID,
                                     &core->audioDevicePropertyBufferFrameSize);
    if (status != kAudioHardwareNoError) {
        coreaudio_logerr2 (status, typ,
                           "Could not get device buffer frame size\n");
        return -1;
    }
    hw->samples = conf->nbuffers * core->audioDevicePropertyBufferFrameSize;

    /* get StreamFormat */
    status = coreaudio_get_streamformat(core->outputDeviceID,
                                        &core->outputStreamBasicDescription);
    if (status != kAudioHardwareNoError) {
        coreaudio_logerr2 (status, typ,
                           "Could not get Device Stream properties\n");
        core->outputDeviceID = kAudioDeviceUnknown;
        return -1;
    }

    /* set Samplerate */
    core->outputStreamBasicDescription.mSampleRate = (Float64) as->freq;
    status = coreaudio_set_streamformat(core->outputDeviceID,
                                        &core->outputStreamBasicDescription);
    if (status != kAudioHardwareNoError) {
        coreaudio_logerr2 (status, typ, "Could not set samplerate %d\n",
                           as->freq);
        core->outputDeviceID = kAudioDeviceUnknown;
        return -1;
    }

    /* set Callback */
    core->ioprocid = NULL;
    status = AudioDeviceCreateIOProcID(core->outputDeviceID,
                                       audioDeviceIOProc,
                                       hw,
                                       &core->ioprocid);
    if (status != kAudioHardwareNoError || core->ioprocid == NULL) {
        coreaudio_logerr2 (status, typ, "Could not set IOProc\n");
        core->outputDeviceID = kAudioDeviceUnknown;
        return -1;
    }

    /* start Playback */
    if (!isPlaying(core->outputDeviceID)) {
        status = AudioDeviceStart(core->outputDeviceID, core->ioprocid);
        if (status != kAudioHardwareNoError) {
            coreaudio_logerr2 (status, typ, "Could not start playback\n");
            AudioDeviceDestroyIOProcID(core->outputDeviceID, core->ioprocid);
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

    if (!isAtexit) {
        /* stop playback */
        if (isPlaying(core->outputDeviceID)) {
            status = AudioDeviceStop(core->outputDeviceID, core->ioprocid);
            if (status != kAudioHardwareNoError) {
                coreaudio_logerr (status, "Could not stop playback\n");
            }
        }

        /* remove callback */
        status = AudioDeviceDestroyIOProcID(core->outputDeviceID,
                                            core->ioprocid);
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
            status = AudioDeviceStart(core->outputDeviceID, core->ioprocid);
            if (status != kAudioHardwareNoError) {
                coreaudio_logerr (status, "Could not resume playback\n");
            }
        }
        break;

    case VOICE_DISABLE:
        /* stop playback */
        if (!isAtexit) {
            if (isPlaying(core->outputDeviceID)) {
                status = AudioDeviceStop(core->outputDeviceID,
                                         core->ioprocid);
                if (status != kAudioHardwareNoError) {
                    coreaudio_logerr (status, "Could not pause playback\n");
                }
            }
        }
        break;
    }
    return 0;
}

static CoreaudioConf glob_conf = {
    .buffer_frames = 512,
    .nbuffers = 4,
};

static void *coreaudio_audio_init (void)
{
    CoreaudioConf *conf = g_malloc(sizeof(CoreaudioConf));
    *conf = glob_conf;

    atexit(coreaudio_atexit);
    return conf;
}

static void coreaudio_audio_fini (void *opaque)
{
    g_free(opaque);
}

static struct audio_option coreaudio_options[] = {
    {
        .name  = "BUFFER_SIZE",
        .tag   = AUD_OPT_INT,
        .valp  = &glob_conf.buffer_frames,
        .descr = "Size of the buffer in frames"
    },
    {
        .name  = "BUFFER_COUNT",
        .tag   = AUD_OPT_INT,
        .valp  = &glob_conf.nbuffers,
        .descr = "Number of buffers"
    },
    { /* End of list */ }
};

static struct audio_pcm_ops coreaudio_pcm_ops = {
    .init_out = coreaudio_init_out,
    .fini_out = coreaudio_fini_out,
    .run_out  = coreaudio_run_out,
    .write    = coreaudio_write,
    .ctl_out  = coreaudio_ctl_out
};

struct audio_driver coreaudio_audio_driver = {
    .name           = "coreaudio",
    .descr          = "CoreAudio http://developer.apple.com/audio/coreaudio.html",
    .options        = coreaudio_options,
    .init           = coreaudio_audio_init,
    .fini           = coreaudio_audio_fini,
    .pcm_ops        = &coreaudio_pcm_ops,
    .can_be_default = 1,
    .max_voices_out = 1,
    .max_voices_in  = 0,
    .voice_size_out = sizeof (coreaudioVoiceOut),
    .voice_size_in  = 0
};

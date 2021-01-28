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

#include "qemu/module.h"
#include "audio.h"

#define AUDIO_CAP "coreaudio"
#include "audio_int.h"

#ifndef MAC_OS_X_VERSION_10_6
#define MAC_OS_X_VERSION_10_6 1060
#endif

typedef struct coreaudioVoiceOut {
    HWVoiceOut hw;
    pthread_mutex_t mutex;
    AudioDeviceID outputDeviceID;
    UInt32 audioDevicePropertyBufferFrameSize;
    AudioStreamBasicDescription outputStreamBasicDescription;
    AudioDeviceIOProcID ioprocid;
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

    switch (status) {
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

#define COREAUDIO_WRAPPER_FUNC(name, ret_type, args_decl, args) \
    static ret_type glue(coreaudio_, name)args_decl             \
    {                                                           \
        coreaudioVoiceOut *core = (coreaudioVoiceOut *) hw;     \
        ret_type ret;                                           \
                                                                \
        if (coreaudio_lock(core, "coreaudio_" #name)) {         \
            return 0;                                           \
        }                                                       \
                                                                \
        ret = glue(audio_generic_, name)args;                   \
                                                                \
        coreaudio_unlock(core, "coreaudio_" #name);             \
        return ret;                                             \
    }
COREAUDIO_WRAPPER_FUNC(get_buffer_out, void *, (HWVoiceOut *hw, size_t *size),
                       (hw, size))
COREAUDIO_WRAPPER_FUNC(put_buffer_out, size_t,
                       (HWVoiceOut *hw, void *buf, size_t size),
                       (hw, buf, size))
COREAUDIO_WRAPPER_FUNC(write, size_t, (HWVoiceOut *hw, void *buf, size_t size),
                       (hw, buf, size))
#undef COREAUDIO_WRAPPER_FUNC

/* callback to feed audiooutput buffer */
static OSStatus audioDeviceIOProc(
    AudioDeviceID inDevice,
    const AudioTimeStamp *inNow,
    const AudioBufferList *inInputData,
    const AudioTimeStamp *inInputTime,
    AudioBufferList *outOutputData,
    const AudioTimeStamp *inOutputTime,
    void *hwptr)
{
    UInt32 frameCount, pending_frames;
    void *out = outOutputData->mBuffers[0].mData;
    HWVoiceOut *hw = hwptr;
    coreaudioVoiceOut *core = (coreaudioVoiceOut *) hwptr;
    size_t len;

    if (coreaudio_lock (core, "audioDeviceIOProc")) {
        inInputTime = 0;
        return 0;
    }

    frameCount = core->audioDevicePropertyBufferFrameSize;
    pending_frames = hw->pending_emul / hw->info.bytes_per_frame;

    /* if there are not enough samples, set signal and return */
    if (pending_frames < frameCount) {
        inInputTime = 0;
        coreaudio_unlock (core, "audioDeviceIOProc(empty)");
        return 0;
    }

    len = frameCount * hw->info.bytes_per_frame;
    while (len) {
        size_t write_len;
        ssize_t start = ((ssize_t) hw->pos_emul) - hw->pending_emul;
        if (start < 0) {
            start += hw->size_emul;
        }
        assert(start >= 0 && start < hw->size_emul);

        write_len = MIN(MIN(hw->pending_emul, len),
                        hw->size_emul - start);

        memcpy(out, hw->buf_emul + start, write_len);
        hw->pending_emul -= write_len;
        len -= write_len;
        out += write_len;
    }

    coreaudio_unlock (core, "audioDeviceIOProc");
    return 0;
}

static int coreaudio_init_out(HWVoiceOut *hw, struct audsettings *as,
                              void *drv_opaque)
{
    OSStatus status;
    coreaudioVoiceOut *core = (coreaudioVoiceOut *) hw;
    int err;
    const char *typ = "playback";
    AudioValueRange frameRange;
    Audiodev *dev = drv_opaque;
    AudiodevCoreaudioPerDirectionOptions *cpdo = dev->u.coreaudio.out;
    int frames;
    struct audsettings obt_as;

    /* create mutex */
    err = pthread_mutex_init(&core->mutex, NULL);
    if (err) {
        dolog("Could not create mutex\nReason: %s\n", strerror (err));
        return -1;
    }

    obt_as = *as;
    as = &obt_as;
    as->fmt = AUDIO_FORMAT_F32;
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

    frames = audio_buffer_frames(
        qapi_AudiodevCoreaudioPerDirectionOptions_base(cpdo), as, 11610);
    if (frameRange.mMinimum > frames) {
        core->audioDevicePropertyBufferFrameSize = (UInt32) frameRange.mMinimum;
        dolog ("warning: Upsizing Buffer Frames to %f\n", frameRange.mMinimum);
    } else if (frameRange.mMaximum < frames) {
        core->audioDevicePropertyBufferFrameSize = (UInt32) frameRange.mMaximum;
        dolog ("warning: Downsizing Buffer Frames to %f\n", frameRange.mMaximum);
    } else {
        core->audioDevicePropertyBufferFrameSize = frames;
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
    hw->samples = (cpdo->has_buffer_count ? cpdo->buffer_count : 4) *
        core->audioDevicePropertyBufferFrameSize;

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

    return 0;
}

static void coreaudio_fini_out (HWVoiceOut *hw)
{
    OSStatus status;
    int err;
    coreaudioVoiceOut *core = (coreaudioVoiceOut *) hw;

    /* stop playback */
    if (isPlaying(core->outputDeviceID)) {
        status = AudioDeviceStop(core->outputDeviceID, core->ioprocid);
        if (status != kAudioHardwareNoError) {
            coreaudio_logerr(status, "Could not stop playback\n");
        }
    }

    /* remove callback */
    status = AudioDeviceDestroyIOProcID(core->outputDeviceID,
                                        core->ioprocid);
    if (status != kAudioHardwareNoError) {
        coreaudio_logerr(status, "Could not remove IOProc\n");
    }
    core->outputDeviceID = kAudioDeviceUnknown;

    /* destroy mutex */
    err = pthread_mutex_destroy(&core->mutex);
    if (err) {
        dolog("Could not destroy mutex\nReason: %s\n", strerror (err));
    }
}

static void coreaudio_enable_out(HWVoiceOut *hw, bool enable)
{
    OSStatus status;
    coreaudioVoiceOut *core = (coreaudioVoiceOut *) hw;

    if (enable) {
        /* start playback */
        if (!isPlaying(core->outputDeviceID)) {
            status = AudioDeviceStart(core->outputDeviceID, core->ioprocid);
            if (status != kAudioHardwareNoError) {
                coreaudio_logerr (status, "Could not resume playback\n");
            }
        }
    } else {
        /* stop playback */
        if (isPlaying(core->outputDeviceID)) {
            status = AudioDeviceStop(core->outputDeviceID,
                                     core->ioprocid);
            if (status != kAudioHardwareNoError) {
                coreaudio_logerr(status, "Could not pause playback\n");
            }
        }
    }
}

static void *coreaudio_audio_init(Audiodev *dev)
{
    return dev;
}

static void coreaudio_audio_fini (void *opaque)
{
}

static struct audio_pcm_ops coreaudio_pcm_ops = {
    .init_out = coreaudio_init_out,
    .fini_out = coreaudio_fini_out,
  /* wrapper for audio_generic_write */
    .write    = coreaudio_write,
  /* wrapper for audio_generic_get_buffer_out */
    .get_buffer_out = coreaudio_get_buffer_out,
  /* wrapper for audio_generic_put_buffer_out */
    .put_buffer_out = coreaudio_put_buffer_out,
    .enable_out = coreaudio_enable_out
};

static struct audio_driver coreaudio_audio_driver = {
    .name           = "coreaudio",
    .descr          = "CoreAudio http://developer.apple.com/audio/coreaudio.html",
    .init           = coreaudio_audio_init,
    .fini           = coreaudio_audio_fini,
    .pcm_ops        = &coreaudio_pcm_ops,
    .can_be_default = 1,
    .max_voices_out = 1,
    .max_voices_in  = 0,
    .voice_size_out = sizeof (coreaudioVoiceOut),
    .voice_size_in  = 0
};

static void register_audio_coreaudio(void)
{
    audio_driver_register(&coreaudio_audio_driver);
}
type_init(register_audio_coreaudio);

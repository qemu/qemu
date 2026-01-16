/*
 * QEMU DirectSound audio driver
 *
 * Copyright (c) 2005 Vassili Karpov (malc)
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

/*
 * SEAL 1.07 by Carlos 'pel' Hasan was used as documentation
 */

#include "qemu/osdep.h"
#include "qemu/audio.h"

#define AUDIO_CAP "dsound"
#include "audio_int.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "qom/object.h"

#include <windows.h>
#include <mmsystem.h>
#include <objbase.h>
#include <dsound.h>

#include "audio_win_int.h"

#define TYPE_AUDIO_DSOUND "audio-dsound"
OBJECT_DECLARE_SIMPLE_TYPE(AudioDsound, AUDIO_DSOUND)

static AudioBackendClass *audio_dsound_parent_class;

struct AudioDsound {
    AudioMixengBackend parent_obj;

    LPDIRECTSOUND dsound;
    LPDIRECTSOUNDCAPTURE dsound_capture;
    struct audsettings settings;
};


/* #define DEBUG_DSOUND */

typedef struct {
    HWVoiceOut hw;
    LPDIRECTSOUNDBUFFER dsound_buffer;
    bool first_time;
} DSoundVoiceOut;

typedef struct {
    HWVoiceIn hw;
    LPDIRECTSOUNDCAPTUREBUFFER dsound_capture_buffer;
    bool first_time;
} DSoundVoiceIn;

static const char *dserror(HRESULT hr)
{
    switch (hr) {
    case DS_OK:
        return "The method succeeded";
#ifdef DS_NO_VIRTUALIZATION
    case DS_NO_VIRTUALIZATION:
        return "The buffer was created, but another 3D algorithm was substituted";
#endif
#ifdef DS_INCOMPLETE
    case DS_INCOMPLETE:
        return "The method succeeded, but not all the optional effects were obtained";
#endif
#ifdef DSERR_ACCESSDENIED
    case DSERR_ACCESSDENIED:
        return "The request failed because access was denied";
#endif
#ifdef DSERR_ALLOCATED
    case DSERR_ALLOCATED:
        return "The request failed because resources, "
               "such as a priority level, were already in use "
               "by another caller";
#endif
#ifdef DSERR_ALREADYINITIALIZED
    case DSERR_ALREADYINITIALIZED:
        return "The object is already initialized";
#endif
#ifdef DSERR_BADFORMAT
    case DSERR_BADFORMAT:
        return "The specified wave format is not supported";
#endif
#ifdef DSERR_BADSENDBUFFERGUID
    case DSERR_BADSENDBUFFERGUID:
        return "The GUID specified in an audiopath file "
               "does not match a valid mix-in buffer";
#endif
#ifdef DSERR_BUFFERLOST
    case DSERR_BUFFERLOST:
        return "The buffer memory has been lost and must be restored";
#endif
#ifdef DSERR_BUFFERTOOSMALL
    case DSERR_BUFFERTOOSMALL:
        return "The buffer size is not great enough to "
               "enable effects processing";
#endif
#ifdef DSERR_CONTROLUNAVAIL
    case DSERR_CONTROLUNAVAIL:
        return "The buffer control (volume, pan, and so on) "
               "requested by the caller is not available. "
               "Controls must be specified when the buffer is created, "
               "using the dwFlags member of DSBUFFERDESC";
#endif
#ifdef DSERR_DS8_REQUIRED
    case DSERR_DS8_REQUIRED:
        return "A DirectSound object of class CLSID_DirectSound8 or later "
               "is required for the requested functionality. "
               "For more information, see IDirectSound8 Interface";
#endif
#ifdef DSERR_FXUNAVAILABLE
    case DSERR_FXUNAVAILABLE:
        return "The effects requested could not be found on the system, "
               "or they are in the wrong order or in the wrong location; "
               "for example, an effect expected in hardware "
               "was found in software";
#endif
#ifdef DSERR_GENERIC
    case DSERR_GENERIC:
        return "An undetermined error occurred inside the DirectSound subsystem";
#endif
#ifdef DSERR_INVALIDCALL
    case DSERR_INVALIDCALL:
        return "This function is not valid for the current state of this object";
#endif
#ifdef DSERR_INVALIDPARAM
    case DSERR_INVALIDPARAM:
        return "An invalid parameter was passed to the returning function";
#endif
#ifdef DSERR_NOAGGREGATION
    case DSERR_NOAGGREGATION:
        return "The object does not support aggregation";
#endif
#ifdef DSERR_NODRIVER
    case DSERR_NODRIVER:
        return "No sound driver is available for use, "
               "or the given GUID is not a valid DirectSound device ID";
#endif
#ifdef DSERR_NOINTERFACE
    case DSERR_NOINTERFACE:
        return "The requested COM interface is not available";
#endif
#ifdef DSERR_OBJECTNOTFOUND
    case DSERR_OBJECTNOTFOUND:
        return "The requested object was not found";
#endif
#ifdef DSERR_OTHERAPPHASPRIO
    case DSERR_OTHERAPPHASPRIO:
        return "Another application has a higher priority level, "
              "preventing this call from succeeding";
#endif
#ifdef DSERR_OUTOFMEMORY
    case DSERR_OUTOFMEMORY:
        return "The DirectSound subsystem could not allocate "
               "sufficient memory to complete the caller's request";
#endif
#ifdef DSERR_PRIOLEVELNEEDED
    case DSERR_PRIOLEVELNEEDED:
        return "A cooperative level of DSSCL_PRIORITY or higher is required";
#endif
#ifdef DSERR_SENDLOOP
    case DSERR_SENDLOOP:
        return "A circular loop of send effects was detected";
#endif
#ifdef DSERR_UNINITIALIZED
    case DSERR_UNINITIALIZED:
        return "The Initialize method has not been called "
               "or has not been called successfully "
               "before other methods were called";
#endif
#ifdef DSERR_UNSUPPORTED
    case DSERR_UNSUPPORTED:
        return "The function called is not supported at this time";
#endif
    default:
        return NULL;
    }

}

static void dserror_set(Error **errp, HRESULT hr, const char *msg)
{
    const char *str = dserror(hr);

    if (str) {
        error_setg(errp, "%s: %s", msg, str);
    } else {
        error_setg(errp, "%s: Unknown (HRESULT: 0x%lx)", msg, hr);
    }
}

static void dsound_log_hresult(HRESULT hr)
{
    const char *str = dserror(hr);

    if (str) {
        AUD_log (AUDIO_CAP, "Reason: %s\n", str);
    } else {
        AUD_log (AUDIO_CAP, "Reason: Unknown (HRESULT: 0x%lx)\n", hr);
    }
}

static void G_GNUC_PRINTF (2, 3) dsound_logerr (
    HRESULT hr,
    const char *fmt,
    ...
    )
{
    va_list ap;

    va_start (ap, fmt);
    AUD_vlog (AUDIO_CAP, fmt, ap);
    va_end (ap);

    dsound_log_hresult (hr);
}

static void G_GNUC_PRINTF (3, 4) dsound_logerr2 (
    HRESULT hr,
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

    dsound_log_hresult (hr);
}

#ifdef DEBUG_DSOUND
static void print_wave_format (WAVEFORMATEX *wfx)
{
    dolog ("tag             = %d\n", wfx->wFormatTag);
    dolog ("nChannels       = %d\n", wfx->nChannels);
    dolog ("nSamplesPerSec  = %ld\n", wfx->nSamplesPerSec);
    dolog ("nAvgBytesPerSec = %ld\n", wfx->nAvgBytesPerSec);
    dolog ("nBlockAlign     = %d\n", wfx->nBlockAlign);
    dolog ("wBitsPerSample  = %d\n", wfx->wBitsPerSample);
    dolog ("cbSize          = %d\n", wfx->cbSize);
}
#endif

static int dsound_restore_out (LPDIRECTSOUNDBUFFER dsb, AudioDsound *s)
{
    HRESULT hr;

    hr = IDirectSoundBuffer_Restore (dsb);

    if (hr != DS_OK) {
        dsound_logerr (hr, "Could not restore playback buffer\n");
        return -1;
    }
    return 0;
}

#include "dsound_template.h"
#define DSBTYPE_IN
#include "dsound_template.h"
#undef DSBTYPE_IN

static int dsound_get_status_out (LPDIRECTSOUNDBUFFER dsb, DWORD *statusp,
                                  AudioDsound *s)
{
    HRESULT hr;

    hr = IDirectSoundBuffer_GetStatus (dsb, statusp);
    if (FAILED (hr)) {
        dsound_logerr (hr, "Could not get playback buffer status\n");
        return -1;
    }

    if (*statusp & DSBSTATUS_BUFFERLOST) {
        dsound_restore_out(dsb, s);
        return -1;
    }

    return 0;
}

static int dsound_get_status_in (LPDIRECTSOUNDCAPTUREBUFFER dscb,
                                 DWORD *statusp)
{
    HRESULT hr;

    hr = IDirectSoundCaptureBuffer_GetStatus (dscb, statusp);
    if (FAILED (hr)) {
        dsound_logerr (hr, "Could not get capture buffer status\n");
        return -1;
    }

    return 0;
}

static void dsound_clear_sample (HWVoiceOut *hw, LPDIRECTSOUNDBUFFER dsb,
                                 AudioDsound *s)
{
    int err;
    LPVOID p1, p2;
    DWORD blen1, blen2, len1, len2;

    err = dsound_lock_out (
        dsb,
        &hw->info,
        0,
        hw->size_emul,
        &p1, &p2,
        &blen1, &blen2,
        1,
        s
        );
    if (err) {
        return;
    }

    len1 = blen1 / hw->info.bytes_per_frame;
    len2 = blen2 / hw->info.bytes_per_frame;

#ifdef DEBUG_DSOUND
    dolog ("clear %p,%ld,%ld %p,%ld,%ld\n",
           p1, blen1, len1,
           p2, blen2, len2);
#endif

    if (p1 && len1) {
        audio_pcm_info_clear_buf (&hw->info, p1, len1);
    }

    if (p2 && len2) {
        audio_pcm_info_clear_buf (&hw->info, p2, len2);
    }

    dsound_unlock_out (dsb, p1, p2, blen1, blen2);
}

static void dsound_enable_out(HWVoiceOut *hw, bool enable)
{
    HRESULT hr;
    DWORD status;
    AudioDsound *s = AUDIO_DSOUND(hw->s);
    DSoundVoiceOut *ds = (DSoundVoiceOut *) hw;
    LPDIRECTSOUNDBUFFER dsb = ds->dsound_buffer;

    if (!dsb) {
        dolog ("Attempt to control voice without a buffer\n");
        return;
    }

    if (enable) {
        if (dsound_get_status_out (dsb, &status, s)) {
            return;
        }

        if (status & DSBSTATUS_PLAYING) {
            dolog ("warning: Voice is already playing\n");
            return;
        }

        dsound_clear_sample (hw, dsb, s);

        hr = IDirectSoundBuffer_Play (dsb, 0, 0, DSBPLAY_LOOPING);
        if (FAILED (hr)) {
            dsound_logerr (hr, "Could not start playing buffer\n");
            return;
        }
    } else {
        if (dsound_get_status_out (dsb, &status, s)) {
            return;
        }

        if (status & DSBSTATUS_PLAYING) {
            hr = IDirectSoundBuffer_Stop (dsb);
            if (FAILED (hr)) {
                dsound_logerr (hr, "Could not stop playing buffer\n");
                return;
            }
        } else {
            dolog ("warning: Voice is not playing\n");
        }
    }
}

static size_t dsound_buffer_get_free(HWVoiceOut *hw)
{
    DSoundVoiceOut *ds = (DSoundVoiceOut *) hw;
    LPDIRECTSOUNDBUFFER dsb = ds->dsound_buffer;
    HRESULT hr;
    DWORD ppos, wpos;

    hr = IDirectSoundBuffer_GetCurrentPosition(
        dsb, &ppos, ds->first_time ? &wpos : NULL);
    if (FAILED(hr)) {
        dsound_logerr(hr, "Could not get playback buffer position\n");
        return 0;
    }

    if (ds->first_time) {
        hw->pos_emul = wpos;
        ds->first_time = false;
    }

    return audio_ring_dist(ppos, hw->pos_emul, hw->size_emul);
}

static void *dsound_get_buffer_out(HWVoiceOut *hw, size_t *size)
{
    DSoundVoiceOut *ds = (DSoundVoiceOut *)hw;
    LPDIRECTSOUNDBUFFER dsb = ds->dsound_buffer;
    DWORD act_size;
    size_t req_size;
    int err;
    void *ret;

    req_size = MIN(*size, hw->size_emul - hw->pos_emul);
    assert(req_size > 0);

    err = dsound_lock_out(dsb, &hw->info, hw->pos_emul, req_size, &ret, NULL,
                          &act_size, NULL, false, AUDIO_DSOUND(hw->s));
    if (err) {
        dolog("Failed to lock buffer\n");
        *size = 0;
        return NULL;
    }

    *size = act_size;
    return ret;
}

static size_t dsound_put_buffer_out(HWVoiceOut *hw, void *buf, size_t len)
{
    DSoundVoiceOut *ds = (DSoundVoiceOut *) hw;
    LPDIRECTSOUNDBUFFER dsb = ds->dsound_buffer;
    int err = dsound_unlock_out(dsb, buf, NULL, len, 0);

    if (err) {
        dolog("Failed to unlock buffer!!\n");
        return 0;
    }
    hw->pos_emul = (hw->pos_emul + len) % hw->size_emul;

    return len;
}

static void dsound_enable_in(HWVoiceIn *hw, bool enable)
{
    HRESULT hr;
    DWORD status;
    DSoundVoiceIn *ds = (DSoundVoiceIn *) hw;
    LPDIRECTSOUNDCAPTUREBUFFER dscb = ds->dsound_capture_buffer;

    if (!dscb) {
        dolog ("Attempt to control capture voice without a buffer\n");
        return;
    }

    if (enable) {
        if (dsound_get_status_in (dscb, &status)) {
            return;
        }

        if (status & DSCBSTATUS_CAPTURING) {
            dolog ("warning: Voice is already capturing\n");
            return;
        }

        /* clear ?? */

        hr = IDirectSoundCaptureBuffer_Start (dscb, DSCBSTART_LOOPING);
        if (FAILED (hr)) {
            dsound_logerr (hr, "Could not start capturing\n");
            return;
        }
    } else {
        if (dsound_get_status_in (dscb, &status)) {
            return;
        }

        if (status & DSCBSTATUS_CAPTURING) {
            hr = IDirectSoundCaptureBuffer_Stop (dscb);
            if (FAILED (hr)) {
                dsound_logerr (hr, "Could not stop capturing\n");
                return;
            }
        } else {
            dolog ("warning: Voice is not capturing\n");
        }
    }
}

static void *dsound_get_buffer_in(HWVoiceIn *hw, size_t *size)
{
    DSoundVoiceIn *ds = (DSoundVoiceIn *) hw;
    LPDIRECTSOUNDCAPTUREBUFFER dscb = ds->dsound_capture_buffer;
    HRESULT hr;
    DWORD rpos, act_size;
    size_t req_size;
    int err;
    void *ret;

    hr = IDirectSoundCaptureBuffer_GetCurrentPosition(dscb, NULL, &rpos);
    if (FAILED(hr)) {
        dsound_logerr(hr, "Could not get capture buffer position\n");
        *size = 0;
        return NULL;
    }

    if (ds->first_time) {
        hw->pos_emul = rpos;
        ds->first_time = false;
    }

    req_size = audio_ring_dist(rpos, hw->pos_emul, hw->size_emul);
    req_size = MIN(*size, MIN(req_size, hw->size_emul - hw->pos_emul));

    if (req_size == 0) {
        *size = 0;
        return NULL;
    }

    err = dsound_lock_in(dscb, &hw->info, hw->pos_emul, req_size, &ret, NULL,
                         &act_size, NULL, false, AUDIO_DSOUND(hw->s));
    if (err) {
        dolog("Failed to lock buffer\n");
        *size = 0;
        return NULL;
    }

    *size = act_size;
    return ret;
}

static void dsound_put_buffer_in(HWVoiceIn *hw, void *buf, size_t len)
{
    DSoundVoiceIn *ds = (DSoundVoiceIn *) hw;
    LPDIRECTSOUNDCAPTUREBUFFER dscb = ds->dsound_capture_buffer;
    int err = dsound_unlock_in(dscb, buf, NULL, len, 0);

    if (err) {
        dolog("Failed to unlock buffer!!\n");
        return;
    }
    hw->pos_emul = (hw->pos_emul + len) % hw->size_emul;
}

static void audio_dsound_finalize(Object *obj)
{
    AudioDsound *s = AUDIO_DSOUND(obj);
    HRESULT hr;

    if (!s->dsound) {
        return;
    }

    hr = IDirectSound_Release (s->dsound);
    if (FAILED (hr)) {
        dsound_logerr (hr, "Could not release DirectSound\n");
    }
    s->dsound = NULL;

    if (!s->dsound_capture) {
        return;
    }

    hr = IDirectSoundCapture_Release (s->dsound_capture);
    if (FAILED (hr)) {
        dsound_logerr (hr, "Could not release DirectSoundCapture\n");
    }
    s->dsound_capture = NULL;
}

static bool
audio_dsound_realize(AudioBackend *abe, Audiodev *dev, Error **errp)
{
    AudioDsound *s = AUDIO_DSOUND(abe);
    HRESULT hr;
    AudiodevDsoundOptions *dso;

    assert(dev->driver == AUDIODEV_DRIVER_DSOUND);

    if (!audio_dsound_parent_class->realize(abe, dev, errp)) {
        return false;
    }

    dso = &dev->u.dsound;

    if (!dso->has_latency) {
        dso->has_latency = true;
        dso->latency = 10000; /* 10 ms */
    }

    hr = CoInitialize (NULL);
    if (FAILED (hr)) {
        dserror_set(errp, hr, "Could not initialize COM");
        return false;
    }

    hr = CoCreateInstance (
        &CLSID_DirectSound,
        NULL,
        CLSCTX_ALL,
        &IID_IDirectSound,
        (void **) &s->dsound
        );
    if (FAILED (hr)) {
        dserror_set(errp, hr, "Could not create DirectSound instance");
        return false;
    }

    hr = IDirectSound_Initialize (s->dsound, NULL);
    if (FAILED (hr)) {
        dserror_set(errp, hr, "Could not initialize DirectSound");
        return false;
    }

    hr = CoCreateInstance (
        &CLSID_DirectSoundCapture,
        NULL,
        CLSCTX_ALL,
        &IID_IDirectSoundCapture,
        (void **) &s->dsound_capture
        );
    if (FAILED (hr)) {
        dserror_set(errp, hr, "Could not create DirectSoundCapture instance");
        return false;
    }

    hr = IDirectSoundCapture_Initialize (s->dsound_capture, NULL);
    if (FAILED(hr)) {
        dserror_set(errp, hr, "Could not initialize DirectSoundCapture");
        return false;
    }

    hr = IDirectSound_SetCooperativeLevel (
        s->dsound,
        GetDesktopWindow(),
        DSSCL_PRIORITY
    );
    if (FAILED(hr)) {
        dserror_set(errp, hr, "Could not set cooperative level");
        return false;
    }

    return true;
}

static void audio_dsound_class_init(ObjectClass *klass, const void *data)
{
    AudioBackendClass *b = AUDIO_BACKEND_CLASS(klass);
    AudioMixengBackendClass *k = AUDIO_MIXENG_BACKEND_CLASS(klass);

    audio_dsound_parent_class = AUDIO_BACKEND_CLASS(object_class_get_parent(klass));

    b->realize = audio_dsound_realize;
    k->name = "dsound";
    k->max_voices_out = INT_MAX;
    k->max_voices_in = 1;
    k->voice_size_out = sizeof(DSoundVoiceOut);
    k->voice_size_in = sizeof(DSoundVoiceIn);

    k->init_out = dsound_init_out;
    k->fini_out = dsound_fini_out;
    k->write = audio_generic_write;
    k->buffer_get_free = dsound_buffer_get_free;
    k->get_buffer_out = dsound_get_buffer_out;
    k->put_buffer_out = dsound_put_buffer_out;
    k->enable_out = dsound_enable_out;

    k->init_in = dsound_init_in;
    k->fini_in = dsound_fini_in;
    k->read = audio_generic_read;
    k->get_buffer_in = dsound_get_buffer_in;
    k->put_buffer_in = dsound_put_buffer_in;
    k->enable_in = dsound_enable_in;
}

static const TypeInfo audio_types[] = {
    {
        .name = TYPE_AUDIO_DSOUND,
        .parent = TYPE_AUDIO_MIXENG_BACKEND,
        .instance_size = sizeof(AudioDsound),
        .class_init = audio_dsound_class_init,
        .instance_finalize = audio_dsound_finalize,
    },
};

DEFINE_TYPES(audio_types)
module_obj(TYPE_AUDIO_DSOUND);

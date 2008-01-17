/*
 * QEMU DirectSound audio driver header
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
#ifdef DSBTYPE_IN
#define NAME "capture buffer"
#define NAME2 "DirectSoundCapture"
#define TYPE in
#define IFACE IDirectSoundCaptureBuffer
#define BUFPTR LPDIRECTSOUNDCAPTUREBUFFER
#define FIELD dsound_capture_buffer
#define FIELD2 dsound_capture
#else
#define NAME "playback buffer"
#define NAME2 "DirectSound"
#define TYPE out
#define IFACE IDirectSoundBuffer
#define BUFPTR LPDIRECTSOUNDBUFFER
#define FIELD dsound_buffer
#define FIELD2 dsound
#endif

static int glue (dsound_unlock_, TYPE) (
    BUFPTR buf,
    LPVOID p1,
    LPVOID p2,
    DWORD blen1,
    DWORD blen2
    )
{
    HRESULT hr;

    hr = glue (IFACE, _Unlock) (buf, p1, blen1, p2, blen2);
    if (FAILED (hr)) {
        dsound_logerr (hr, "Could not unlock " NAME "\n");
        return -1;
    }

    return 0;
}

static int glue (dsound_lock_, TYPE) (
    BUFPTR buf,
    struct audio_pcm_info *info,
    DWORD pos,
    DWORD len,
    LPVOID *p1p,
    LPVOID *p2p,
    DWORD *blen1p,
    DWORD *blen2p,
    int entire
    )
{
    HRESULT hr;
    int i;
    LPVOID p1 = NULL, p2 = NULL;
    DWORD blen1 = 0, blen2 = 0;
    DWORD flag;

#ifdef DSBTYPE_IN
    flag = entire ? DSCBLOCK_ENTIREBUFFER : 0;
#else
    flag = entire ? DSBLOCK_ENTIREBUFFER : 0;
#endif
    for (i = 0; i < conf.lock_retries; ++i) {
        hr = glue (IFACE, _Lock) (
            buf,
            pos,
            len,
            &p1,
            &blen1,
            &p2,
            &blen2,
            flag
            );

        if (FAILED (hr)) {
#ifndef DSBTYPE_IN
            if (hr == DSERR_BUFFERLOST) {
                if (glue (dsound_restore_, TYPE) (buf)) {
                    dsound_logerr (hr, "Could not lock " NAME "\n");
                    goto fail;
                }
                continue;
            }
#endif
            dsound_logerr (hr, "Could not lock " NAME "\n");
            goto fail;
        }

        break;
    }

    if (i == conf.lock_retries) {
        dolog ("%d attempts to lock " NAME " failed\n", i);
        goto fail;
    }

    if ((p1 && (blen1 & info->align)) || (p2 && (blen2 & info->align))) {
        dolog ("DirectSound returned misaligned buffer %ld %ld\n",
               blen1, blen2);
        glue (dsound_unlock_, TYPE) (buf, p1, p2, blen1, blen2);
        goto fail;
    }

    if (!p1 && blen1) {
        dolog ("warning: !p1 && blen1=%ld\n", blen1);
        blen1 = 0;
    }

    if (!p2 && blen2) {
        dolog ("warning: !p2 && blen2=%ld\n", blen2);
        blen2 = 0;
    }

    *p1p = p1;
    *p2p = p2;
    *blen1p = blen1;
    *blen2p = blen2;
    return 0;

 fail:
    *p1p = NULL - 1;
    *p2p = NULL - 1;
    *blen1p = -1;
    *blen2p = -1;
    return -1;
}

#ifdef DSBTYPE_IN
static void dsound_fini_in (HWVoiceIn *hw)
#else
static void dsound_fini_out (HWVoiceOut *hw)
#endif
{
    HRESULT hr;
#ifdef DSBTYPE_IN
    DSoundVoiceIn *ds = (DSoundVoiceIn *) hw;
#else
    DSoundVoiceOut *ds = (DSoundVoiceOut *) hw;
#endif

    if (ds->FIELD) {
        hr = glue (IFACE, _Stop) (ds->FIELD);
        if (FAILED (hr)) {
            dsound_logerr (hr, "Could not stop " NAME "\n");
        }

        hr = glue (IFACE, _Release) (ds->FIELD);
        if (FAILED (hr)) {
            dsound_logerr (hr, "Could not release " NAME "\n");
        }
        ds->FIELD = NULL;
    }
}

#ifdef DSBTYPE_IN
static int dsound_init_in (HWVoiceIn *hw, audsettings_t *as)
#else
static int dsound_init_out (HWVoiceOut *hw, audsettings_t *as)
#endif
{
    int err;
    HRESULT hr;
    dsound *s = &glob_dsound;
    WAVEFORMATEX wfx;
    audsettings_t obt_as;
#ifdef DSBTYPE_IN
    const char *typ = "ADC";
    DSoundVoiceIn *ds = (DSoundVoiceIn *) hw;
    DSCBUFFERDESC bd;
    DSCBCAPS bc;
#else
    const char *typ = "DAC";
    DSoundVoiceOut *ds = (DSoundVoiceOut *) hw;
    DSBUFFERDESC bd;
    DSBCAPS bc;
#endif

    if (!s->FIELD2) {
        dolog ("Attempt to initialize voice without " NAME2 " object\n");
        return -1;
    }

    err = waveformat_from_audio_settings (&wfx, as);
    if (err) {
        return -1;
    }

    memset (&bd, 0, sizeof (bd));
    bd.dwSize = sizeof (bd);
    bd.lpwfxFormat = &wfx;
#ifdef DSBTYPE_IN
    bd.dwBufferBytes = conf.bufsize_in;
    hr = IDirectSoundCapture_CreateCaptureBuffer (
        s->dsound_capture,
        &bd,
        &ds->dsound_capture_buffer,
        NULL
        );
#else
    bd.dwFlags = DSBCAPS_STICKYFOCUS | DSBCAPS_GETCURRENTPOSITION2;
    bd.dwBufferBytes = conf.bufsize_out;
    hr = IDirectSound_CreateSoundBuffer (
        s->dsound,
        &bd,
        &ds->dsound_buffer,
        NULL
        );
#endif

    if (FAILED (hr)) {
        dsound_logerr2 (hr, typ, "Could not create " NAME "\n");
        return -1;
    }

    hr = glue (IFACE, _GetFormat) (ds->FIELD, &wfx, sizeof (wfx), NULL);
    if (FAILED (hr)) {
        dsound_logerr2 (hr, typ, "Could not get " NAME " format\n");
        goto fail0;
    }

#ifdef DEBUG_DSOUND
    dolog (NAME "\n");
    print_wave_format (&wfx);
#endif

    memset (&bc, 0, sizeof (bc));
    bc.dwSize = sizeof (bc);

    hr = glue (IFACE, _GetCaps) (ds->FIELD, &bc);
    if (FAILED (hr)) {
        dsound_logerr2 (hr, typ, "Could not get " NAME " format\n");
        goto fail0;
    }

    err = waveformat_to_audio_settings (&wfx, &obt_as);
    if (err) {
        goto fail0;
    }

    ds->first_time = 1;
    obt_as.endianness = 0;
    audio_pcm_init_info (&hw->info, &obt_as);

    if (bc.dwBufferBytes & hw->info.align) {
        dolog (
            "GetCaps returned misaligned buffer size %ld, alignment %d\n",
            bc.dwBufferBytes, hw->info.align + 1
            );
    }
    hw->samples = bc.dwBufferBytes >> hw->info.shift;

#ifdef DEBUG_DSOUND
    dolog ("caps %ld, desc %ld\n",
           bc.dwBufferBytes, bd.dwBufferBytes);

    dolog ("bufsize %d, freq %d, chan %d, fmt %d\n",
           hw->bufsize, settings.freq, settings.nchannels, settings.fmt);
#endif
    return 0;

 fail0:
    glue (dsound_fini_, TYPE) (hw);
    return -1;
}

#undef NAME
#undef TYPE
#undef IFACE
#undef BUFPTR
#undef FIELD

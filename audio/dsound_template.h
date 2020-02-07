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
#define HWVOICE HWVoiceIn
#define DSOUNDVOICE DSoundVoiceIn
#else
#define NAME "playback buffer"
#define NAME2 "DirectSound"
#define TYPE out
#define IFACE IDirectSoundBuffer
#define BUFPTR LPDIRECTSOUNDBUFFER
#define FIELD dsound_buffer
#define FIELD2 dsound
#define HWVOICE HWVoiceOut
#define DSOUNDVOICE DSoundVoiceOut
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
    int entire,
    dsound *s
    )
{
    HRESULT hr;
    DWORD flag;

#ifdef DSBTYPE_IN
    flag = entire ? DSCBLOCK_ENTIREBUFFER : 0;
#else
    flag = entire ? DSBLOCK_ENTIREBUFFER : 0;
#endif
    hr = glue(IFACE, _Lock)(buf, pos, len, p1p, blen1p, p2p, blen2p, flag);

    if (FAILED (hr)) {
#ifndef DSBTYPE_IN
        if (hr == DSERR_BUFFERLOST) {
            if (glue (dsound_restore_, TYPE) (buf, s)) {
                dsound_logerr (hr, "Could not lock " NAME "\n");
            }
            goto fail;
        }
#endif
        dsound_logerr (hr, "Could not lock " NAME "\n");
        goto fail;
    }

    if ((p1p && *p1p && (*blen1p % info->bytes_per_frame)) ||
        (p2p && *p2p && (*blen2p % info->bytes_per_frame))) {
        dolog("DirectSound returned misaligned buffer %ld %ld\n",
              *blen1p, *blen2p);
        glue(dsound_unlock_, TYPE)(buf, *p1p, p2p ? *p2p : NULL, *blen1p,
                                   blen2p ? *blen2p : 0);
        goto fail;
    }

    if (p1p && !*p1p && *blen1p) {
        dolog("warning: !p1 && blen1=%ld\n", *blen1p);
        *blen1p = 0;
    }

    if (p2p && !*p2p && *blen2p) {
        dolog("warning: !p2 && blen2=%ld\n", *blen2p);
        *blen2p = 0;
    }

    return 0;

 fail:
    *p1p = NULL - 1;
    *blen1p = -1;
    if (p2p) {
        *p2p = NULL - 1;
        *blen2p = -1;
    }
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
static int dsound_init_in(HWVoiceIn *hw, struct audsettings *as,
                          void *drv_opaque)
#else
static int dsound_init_out(HWVoiceOut *hw, struct audsettings *as,
                           void *drv_opaque)
#endif
{
    int err;
    HRESULT hr;
    dsound *s = drv_opaque;
    WAVEFORMATEX wfx;
    struct audsettings obt_as;
#ifdef DSBTYPE_IN
    const char *typ = "ADC";
    DSoundVoiceIn *ds = (DSoundVoiceIn *) hw;
    DSCBUFFERDESC bd;
    DSCBCAPS bc;
    AudiodevPerDirectionOptions *pdo = s->dev->u.dsound.in;
#else
    const char *typ = "DAC";
    DSoundVoiceOut *ds = (DSoundVoiceOut *) hw;
    DSBUFFERDESC bd;
    DSBCAPS bc;
    AudiodevPerDirectionOptions *pdo = s->dev->u.dsound.out;
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
    bd.dwBufferBytes = audio_buffer_bytes(pdo, as, 92880);
#ifdef DSBTYPE_IN
    hr = IDirectSoundCapture_CreateCaptureBuffer (
        s->dsound_capture,
        &bd,
        &ds->dsound_capture_buffer,
        NULL
        );
#else
    bd.dwFlags = DSBCAPS_STICKYFOCUS | DSBCAPS_GETCURRENTPOSITION2;
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

    ds->first_time = true;
    obt_as.endianness = 0;
    audio_pcm_init_info (&hw->info, &obt_as);

    if (bc.dwBufferBytes % hw->info.bytes_per_frame) {
        dolog (
            "GetCaps returned misaligned buffer size %ld, alignment %d\n",
            bc.dwBufferBytes, hw->info.bytes_per_frame
            );
    }
    hw->size_emul = bc.dwBufferBytes;
    hw->samples = bc.dwBufferBytes / hw->info.bytes_per_frame;
    ds->s = s;

#ifdef DEBUG_DSOUND
    dolog ("caps %ld, desc %ld\n",
           bc.dwBufferBytes, bd.dwBufferBytes);
#endif
    return 0;

 fail0:
    glue (dsound_fini_, TYPE) (hw);
    return -1;
}

#undef NAME
#undef NAME2
#undef TYPE
#undef IFACE
#undef BUFPTR
#undef FIELD
#undef FIELD2
#undef HWVOICE
#undef DSOUNDVOICE

/* public domain */

#include "qemu-common.h"
#include "sysemu.h"
#include "audio.h"

#define AUDIO_CAP "winwave"
#include "audio_int.h"

#include <windows.h>
#include <mmsystem.h>

#include "audio_win_int.h"

static struct {
    int dac_headers;
    int dac_samples;
} conf = {
    .dac_headers = 4,
    .dac_samples = 1024
};

typedef struct {
    HWVoiceOut hw;
    HWAVEOUT hwo;
    WAVEHDR *hdrs;
    HANDLE event;
    void *pcm_buf;
    int avail;
    int pending;
    int curhdr;
    CRITICAL_SECTION crit_sect;
} WaveVoiceOut;

static void winwave_log_mmresult (MMRESULT mr)
{
    const char *str = "BUG";

    switch (mr) {
    case MMSYSERR_NOERROR:
        str = "Success";
        break;

    case MMSYSERR_INVALHANDLE:
        str = "Specified device handle is invalid";
        break;

    case MMSYSERR_BADDEVICEID:
        str = "Specified device id is out of range";
        break;

    case MMSYSERR_NODRIVER:
        str = "No device driver is present";
        break;

    case MMSYSERR_NOMEM:
        str = "Unable to allocate or locl memory";
        break;

    case WAVERR_SYNC:
        str = "Device is synchronous but waveOutOpen was called "
            "without using the WINWAVE_ALLOWSYNC flag";
        break;

    case WAVERR_UNPREPARED:
        str = "The data block pointed to by the pwh parameter "
            "hasn't been prepared";
        break;

    default:
        AUD_log (AUDIO_CAP, "Reason: Unknown (MMRESULT %#x)\n", mr);
        return;
    }

    AUD_log (AUDIO_CAP, "Reason: %s\n", str);
}

static void GCC_FMT_ATTR (2, 3) winwave_logerr (
    MMRESULT mr,
    const char *fmt,
    ...
    )
{
    va_list ap;

    va_start (ap, fmt);
    AUD_vlog (AUDIO_CAP, fmt, ap);
    va_end (ap);

    winwave_log_mmresult (mr);
}

static void winwave_anal_close_out (WaveVoiceOut *wave)
{
    MMRESULT mr;

    mr = waveOutClose (wave->hwo);
    if (mr != MMSYSERR_NOERROR) {
        winwave_logerr (mr, "waveOutClose\n");
    }
    wave->hwo = NULL;
}

static void CALLBACK winwave_callback (
    HWAVEOUT hwo,
    UINT msg,
    DWORD_PTR dwInstance,
    DWORD_PTR dwParam1,
    DWORD_PTR dwParam2
    )
{
    WaveVoiceOut *wave = (WaveVoiceOut *) dwInstance;

    switch (msg) {
    case WOM_DONE:
        {
            WAVEHDR *h = (WAVEHDR *) dwParam1;
            if (!h->dwUser) {
                h->dwUser = 1;
                EnterCriticalSection (&wave->crit_sect);
                {
                    wave->avail += conf.dac_samples;
                }
                LeaveCriticalSection (&wave->crit_sect);
                if (wave->hw.poll_mode) {
                    if (!SetEvent (wave->event)) {
                        AUD_log (AUDIO_CAP, "SetEvent failed %lx\n",
                                 GetLastError ());
                    }
                }
            }
        }
        break;

    case WOM_CLOSE:
    case WOM_OPEN:
        break;

    default:
        AUD_log (AUDIO_CAP, "unknown wave callback msg %x\n", msg);
    }
}

static int winwave_init_out (HWVoiceOut *hw, struct audsettings *as)
{
    int i;
    int err;
    MMRESULT mr;
    WAVEFORMATEX wfx;
    WaveVoiceOut *wave;

    wave = (WaveVoiceOut *) hw;

    InitializeCriticalSection (&wave->crit_sect);

    err =  waveformat_from_audio_settings (&wfx, as);
    if (err) {
        goto err0;
    }

    mr = waveOutOpen (&wave->hwo, WAVE_MAPPER, &wfx,
                      (DWORD_PTR) winwave_callback,
                      (DWORD_PTR) wave, CALLBACK_FUNCTION);
    if (mr != MMSYSERR_NOERROR) {
        winwave_logerr (mr, "waveOutOpen\n");
        goto err1;
    }

    wave->hdrs = audio_calloc (AUDIO_FUNC, conf.dac_headers,
                               sizeof (*wave->hdrs));
    if (!wave->hdrs) {
        goto err2;
    }

    audio_pcm_init_info (&hw->info, as);
    hw->samples = conf.dac_samples * conf.dac_headers;
    wave->avail = hw->samples;

    wave->pcm_buf = audio_calloc (AUDIO_FUNC, conf.dac_samples,
                                  conf.dac_headers << hw->info.shift);
    if (!wave->pcm_buf) {
        goto err3;
    }

    for (i = 0; i < conf.dac_headers; ++i) {
        WAVEHDR *h = &wave->hdrs[i];

        h->dwUser = 0;
        h->dwBufferLength = conf.dac_samples << hw->info.shift;
        h->lpData = advance (wave->pcm_buf, i * h->dwBufferLength);
        h->dwFlags = 0;

        mr = waveOutPrepareHeader (wave->hwo, h, sizeof (*h));
        if (mr != MMSYSERR_NOERROR) {
            winwave_logerr (mr, "waveOutPrepareHeader(%d)\n", wave->curhdr);
            goto err4;
        }
    }

    return 0;

 err4:
    qemu_free (wave->pcm_buf);
 err3:
    qemu_free (wave->hdrs);
 err2:
    winwave_anal_close_out (wave);
 err1:
 err0:
    return -1;
}

static int winwave_write (SWVoiceOut *sw, void *buf, int len)
{
    return audio_pcm_sw_write (sw, buf, len);
}

static int winwave_run_out (HWVoiceOut *hw, int live)
{
    WaveVoiceOut *wave = (WaveVoiceOut *) hw;
    int decr;
    int doreset;

    EnterCriticalSection (&wave->crit_sect);
    {
        decr = audio_MIN (live, wave->avail);
        decr = audio_pcm_hw_clip_out (hw, wave->pcm_buf, decr, wave->pending);
        wave->pending += decr;
        wave->avail -= decr;
    }
    LeaveCriticalSection (&wave->crit_sect);

    doreset = hw->poll_mode && (wave->pending >= conf.dac_samples);
    if (doreset && !ResetEvent (wave->event)) {
        AUD_log (AUDIO_CAP, "ResetEvent failed %lx\n", GetLastError ());
    }

    while (wave->pending >= conf.dac_samples) {
        MMRESULT mr;
        WAVEHDR *h = &wave->hdrs[wave->curhdr];

        h->dwUser = 0;
        mr = waveOutWrite (wave->hwo, h, sizeof (*h));
        if (mr != MMSYSERR_NOERROR) {
            winwave_logerr (mr, "waveOutWrite(%d)\n", wave->curhdr);
            break;
        }

        wave->pending -= conf.dac_samples;
        wave->curhdr = (wave->curhdr + 1) % conf.dac_headers;
    }

    return decr;
}

static void winwave_fini_out (HWVoiceOut *hw)
{
    WaveVoiceOut *wave = (WaveVoiceOut *) hw;

    winwave_anal_close_out (wave);

    qemu_free (wave->pcm_buf);
    wave->pcm_buf = NULL;

    qemu_free (wave->hdrs);
    wave->hdrs = NULL;

    if (wave->event) {
        if (!CloseHandle (wave->event)) {
            AUD_log (AUDIO_CAP, "CloseHandle failed %lx\n", GetLastError ());
        }
        wave->event = NULL;
    }
}

static void winwave_poll_out (void *opaque)
{
    (void) opaque;
    audio_run ("winwave_poll_out");
}

static int winwave_ctl_out (HWVoiceOut *hw, int cmd, ...)
{
    WaveVoiceOut *wave = (WaveVoiceOut *) hw;

    switch (cmd) {
    case VOICE_ENABLE:
        {
            va_list ap;
            int poll_mode;

            va_start (ap, cmd);
            poll_mode = va_arg (ap, int);
            va_end (ap);

            if (poll_mode && !wave->event) {
                wave->event = CreateEvent (NULL, TRUE, TRUE, NULL);
                if (!wave->event) {
                    AUD_log (AUDIO_CAP,
                             "CreateEvent: %lx, poll mode will be disabled\n",
                             GetLastError ());
                }
            }

            if (wave->event) {
                int ret;

                ret = qemu_add_wait_object (wave->event, winwave_poll_out,
                                            wave);
                hw->poll_mode = (ret == 0);
            }
            else {
                hw->poll_mode = 0;
            }
        }
        return 0;

    case VOICE_DISABLE:
        if (wave->event) {
            qemu_del_wait_object (wave->event, winwave_poll_out, wave);
        }
        return 0;
    }
    return -1;
}

static void *winwave_audio_init (void)
{
    return &conf;
}

static void winwave_audio_fini (void *opaque)
{
    (void) opaque;
}

static struct audio_option winwave_options[] = {
    {
        .name        = "DAC_HEADERS",
        .tag         = AUD_OPT_INT,
        .valp        = &conf.dac_headers,
        .descr       = "DAC number of headers",
    },
    {
        .name        = "DAC_SAMPLES",
        .tag         = AUD_OPT_INT,
        .valp        = &conf.dac_samples,
        .descr       = "DAC number of samples per header",
    },
    { /* End of list */ }
};

static struct audio_pcm_ops winwave_pcm_ops = {
    .init_out = winwave_init_out,
    .fini_out = winwave_fini_out,
    .run_out  = winwave_run_out,
    .write    = winwave_write,
    .ctl_out  = winwave_ctl_out
};

struct audio_driver winwave_audio_driver = {
    .name           = "winwave",
    .descr          = "Windows Waveform Audio http://msdn.microsoft.com",
    .options        = winwave_options,
    .init           = winwave_audio_init,
    .fini           = winwave_audio_fini,
    .pcm_ops        = &winwave_pcm_ops,
    .can_be_default = 1,
    .max_voices_out = INT_MAX,
    .max_voices_in  = 0,
    .voice_size_out = sizeof (WaveVoiceOut),
    .voice_size_in  = 0
};

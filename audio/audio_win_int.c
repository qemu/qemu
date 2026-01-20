/* public domain */

#include "qemu/osdep.h"

#include <windows.h>
#include <mmreg.h>
#include <mmsystem.h>

#include "qemu/audio.h"
#include "qemu/error-report.h"
#include "audio_int.h"
#include "audio_win_int.h"

int waveformat_from_audio_settings (WAVEFORMATEX *wfx,
                                    struct audsettings *as)
{
    memset (wfx, 0, sizeof (*wfx));

    wfx->nChannels = as->nchannels;
    wfx->nSamplesPerSec = as->freq;
    wfx->nAvgBytesPerSec = as->freq << (as->nchannels == 2);
    wfx->nBlockAlign = 1 << (as->nchannels == 2);
    wfx->cbSize = 0;

    switch (as->fmt) {
    case AUDIO_FORMAT_S8:
    case AUDIO_FORMAT_U8:
        wfx->wFormatTag = WAVE_FORMAT_PCM;
        wfx->wBitsPerSample = 8;
        break;

    case AUDIO_FORMAT_S16:
    case AUDIO_FORMAT_U16:
        wfx->wFormatTag = WAVE_FORMAT_PCM;
        wfx->wBitsPerSample = 16;
        wfx->nAvgBytesPerSec <<= 1;
        wfx->nBlockAlign <<= 1;
        break;

    case AUDIO_FORMAT_S32:
    case AUDIO_FORMAT_U32:
        wfx->wFormatTag = WAVE_FORMAT_PCM;
        wfx->wBitsPerSample = 32;
        wfx->nAvgBytesPerSec <<= 2;
        wfx->nBlockAlign <<= 2;
        break;

    case AUDIO_FORMAT_F32:
        wfx->wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        wfx->wBitsPerSample = 32;
        wfx->nAvgBytesPerSec <<= 2;
        wfx->nBlockAlign <<= 2;
        break;

    default:
        error_report("dsound: Internal logic error: Bad audio format %d", as->fmt);
        return -1;
    }

    return 0;
}

int waveformat_to_audio_settings (WAVEFORMATEX *wfx,
                                  struct audsettings *as)
{
    if (!wfx->nSamplesPerSec) {
        error_report("dsound: Invalid wave format, frequency is zero");
        return -1;
    }
    as->freq = wfx->nSamplesPerSec;

    switch (wfx->nChannels) {
    case 1:
        as->nchannels = 1;
        break;

    case 2:
        as->nchannels = 2;
        break;

    default:
        error_report("dsound: Invalid wave format, "
                     "number of channels is not 1 or 2, but %d",
                     wfx->nChannels);
        return -1;
    }

    if (wfx->wFormatTag == WAVE_FORMAT_PCM) {
        switch (wfx->wBitsPerSample) {
        case 8:
            as->fmt = AUDIO_FORMAT_U8;
            break;

        case 16:
            as->fmt = AUDIO_FORMAT_S16;
            break;

        case 32:
            as->fmt = AUDIO_FORMAT_S32;
            break;

        default:
            error_report("dsound: Invalid PCM wave format, bits per sample is not "
                         "8, 16 or 32, but %d",
                         wfx->wBitsPerSample);
            return -1;
        }
    } else if (wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        switch (wfx->wBitsPerSample) {
        case 32:
            as->fmt = AUDIO_FORMAT_F32;
            break;

        default:
            error_report("dsound: Invalid IEEE_FLOAT wave format, "
                         "bits per sample is not 32, but %d",
                         wfx->wBitsPerSample);
            return -1;
        }
    } else {
        error_report("dsound: Invalid wave format, "
                     "tag is not PCM and not IEEE_FLOAT, but %d",
                     wfx->wFormatTag);
        return -1;
    }

    return 0;
}


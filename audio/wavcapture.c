/* public domain */
#include "hw/hw.h"
#include "monitor.h"
#include "audio.h"

typedef struct {
    FILE *f;
    int bytes;
    char *path;
    int freq;
    int bits;
    int nchannels;
    CaptureVoiceOut *cap;
} WAVState;

/* VICE code: Store number as little endian. */
static void le_store (uint8_t *buf, uint32_t val, int len)
{
    int i;
    for (i = 0; i < len; i++) {
        buf[i] = (uint8_t) (val & 0xff);
        val >>= 8;
    }
}

static void wav_notify (void *opaque, audcnotification_e cmd)
{
    (void) opaque;
    (void) cmd;
}

static void wav_destroy (void *opaque)
{
    WAVState *wav = opaque;
    uint8_t rlen[4];
    uint8_t dlen[4];
    uint32_t datalen = wav->bytes;
    uint32_t rifflen = datalen + 36;
    Monitor *mon = cur_mon;

    if (wav->f) {
        le_store (rlen, rifflen, 4);
        le_store (dlen, datalen, 4);

        if (fseek (wav->f, 4, SEEK_SET)) {
            monitor_printf (mon, "wav_destroy: rlen fseek failed\nReason: %s\n",
                            strerror (errno));
            goto doclose;
        }
        if (fwrite (rlen, 4, 1, wav->f) != 1) {
            monitor_printf (mon, "wav_destroy: rlen fwrite failed\nReason %s\n",
                            strerror (errno));
            goto doclose;
        }
        if (fseek (wav->f, 32, SEEK_CUR)) {
            monitor_printf (mon, "wav_destroy: dlen fseek failed\nReason %s\n",
                            strerror (errno));
            goto doclose;
        }
        if (fwrite (dlen, 1, 4, wav->f) != 4) {
            monitor_printf (mon, "wav_destroy: dlen fwrite failed\nReason %s\n",
                            strerror (errno));
            goto doclose;
        }
    doclose:
        if (fclose (wav->f)) {
            fprintf (stderr, "wav_destroy: fclose failed: %s",
                     strerror (errno));
        }
    }

    g_free (wav->path);
}

static void wav_capture (void *opaque, void *buf, int size)
{
    WAVState *wav = opaque;

    if (fwrite (buf, size, 1, wav->f) != 1) {
        monitor_printf (cur_mon, "wav_capture: fwrite error\nReason: %s",
                        strerror (errno));
    }
    wav->bytes += size;
}

static void wav_capture_destroy (void *opaque)
{
    WAVState *wav = opaque;

    AUD_del_capture (wav->cap, wav);
}

static void wav_capture_info (void *opaque)
{
    WAVState *wav = opaque;
    char *path = wav->path;

    monitor_printf (cur_mon, "Capturing audio(%d,%d,%d) to %s: %d bytes\n",
                    wav->freq, wav->bits, wav->nchannels,
                    path ? path : "<not available>", wav->bytes);
}

static struct capture_ops wav_capture_ops = {
    .destroy = wav_capture_destroy,
    .info = wav_capture_info
};

int wav_start_capture (CaptureState *s, const char *path, int freq,
                       int bits, int nchannels)
{
    Monitor *mon = cur_mon;
    WAVState *wav;
    uint8_t hdr[] = {
        0x52, 0x49, 0x46, 0x46, 0x00, 0x00, 0x00, 0x00, 0x57, 0x41, 0x56,
        0x45, 0x66, 0x6d, 0x74, 0x20, 0x10, 0x00, 0x00, 0x00, 0x01, 0x00,
        0x02, 0x00, 0x44, 0xac, 0x00, 0x00, 0x10, 0xb1, 0x02, 0x00, 0x04,
        0x00, 0x10, 0x00, 0x64, 0x61, 0x74, 0x61, 0x00, 0x00, 0x00, 0x00
    };
    struct audsettings as;
    struct audio_capture_ops ops;
    int stereo, bits16, shift;
    CaptureVoiceOut *cap;

    if (bits != 8 && bits != 16) {
        monitor_printf (mon, "incorrect bit count %d, must be 8 or 16\n", bits);
        return -1;
    }

    if (nchannels != 1 && nchannels != 2) {
        monitor_printf (mon, "incorrect channel count %d, must be 1 or 2\n",
                        nchannels);
        return -1;
    }

    stereo = nchannels == 2;
    bits16 = bits == 16;

    as.freq = freq;
    as.nchannels = 1 << stereo;
    as.fmt = bits16 ? AUD_FMT_S16 : AUD_FMT_U8;
    as.endianness = 0;

    ops.notify = wav_notify;
    ops.capture = wav_capture;
    ops.destroy = wav_destroy;

    wav = g_malloc0 (sizeof (*wav));

    shift = bits16 + stereo;
    hdr[34] = bits16 ? 0x10 : 0x08;

    le_store (hdr + 22, as.nchannels, 2);
    le_store (hdr + 24, freq, 4);
    le_store (hdr + 28, freq << shift, 4);
    le_store (hdr + 32, 1 << shift, 2);

    wav->f = fopen (path, "wb");
    if (!wav->f) {
        monitor_printf (mon, "Failed to open wave file `%s'\nReason: %s\n",
                        path, strerror (errno));
        g_free (wav);
        return -1;
    }

    wav->path = g_strdup (path);
    wav->bits = bits;
    wav->nchannels = nchannels;
    wav->freq = freq;

    if (fwrite (hdr, sizeof (hdr), 1, wav->f) != 1) {
        monitor_printf (mon, "Failed to write header\nReason: %s\n",
                        strerror (errno));
        goto error_free;
    }

    cap = AUD_add_capture (&as, &ops, wav);
    if (!cap) {
        monitor_printf (mon, "Failed to add audio capture\n");
        goto error_free;
    }

    wav->cap = cap;
    s->opaque = wav;
    s->ops = wav_capture_ops;
    return 0;

error_free:
    g_free (wav->path);
    if (fclose (wav->f)) {
        monitor_printf (mon, "Failed to close wave file\nReason: %s\n",
                        strerror (errno));
    }
    g_free (wav);
    return -1;
}

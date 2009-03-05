#include "hw/hw.h"
#include "monitor.h"
#include "audio.h"

typedef struct {
    QEMUFile *f;
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

    if (wav->f) {
        le_store (rlen, rifflen, 4);
        le_store (dlen, datalen, 4);

        qemu_fseek (wav->f, 4, SEEK_SET);
        qemu_put_buffer (wav->f, rlen, 4);

        qemu_fseek (wav->f, 32, SEEK_CUR);
        qemu_put_buffer (wav->f, dlen, 4);
        qemu_fclose (wav->f);
    }

    qemu_free (wav->path);
}

static void wav_capture (void *opaque, void *buf, int size)
{
    WAVState *wav = opaque;

    qemu_put_buffer (wav->f, buf, size);
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

    monitor_printf(cur_mon, "Capturing audio(%d,%d,%d) to %s: %d bytes\n",
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
        monitor_printf(mon, "incorrect bit count %d, must be 8 or 16\n", bits);
        return -1;
    }

    if (nchannels != 1 && nchannels != 2) {
        monitor_printf(mon, "incorrect channel count %d, must be 1 or 2\n",
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

    wav = qemu_mallocz (sizeof (*wav));

    shift = bits16 + stereo;
    hdr[34] = bits16 ? 0x10 : 0x08;

    le_store (hdr + 22, as.nchannels, 2);
    le_store (hdr + 24, freq, 4);
    le_store (hdr + 28, freq << shift, 4);
    le_store (hdr + 32, 1 << shift, 2);

    wav->f = qemu_fopen (path, "wb");
    if (!wav->f) {
        monitor_printf(mon, "Failed to open wave file `%s'\nReason: %s\n",
                       path, strerror (errno));
        qemu_free (wav);
        return -1;
    }

    wav->path = qemu_strdup (path);
    wav->bits = bits;
    wav->nchannels = nchannels;
    wav->freq = freq;

    qemu_put_buffer (wav->f, hdr, sizeof (hdr));

    cap = AUD_add_capture (NULL, &as, &ops, wav);
    if (!cap) {
        monitor_printf(mon, "Failed to add audio capture\n");
        qemu_free (wav->path);
        qemu_fclose (wav->f);
        qemu_free (wav);
        return -1;
    }

    wav->cap = cap;
    s->opaque = wav;
    s->ops = wav_capture_ops;
    return 0;
}

#include "vl.h"

typedef struct {
    QEMUFile *f;
    int bytes;
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

static void wav_state_cb (void *opaque, int enabled)
{
    WAVState *wav = opaque;

    if (!enabled) {
        uint8_t rlen[4];
        uint8_t dlen[4];
        uint32_t datalen = wav->bytes;
        uint32_t rifflen = datalen + 36;

        if (!wav->f) {
            return;
        }

        le_store (rlen, rifflen, 4);
        le_store (dlen, datalen, 4);

        qemu_fseek (wav->f, 4, SEEK_SET);
        qemu_put_buffer (wav->f, rlen, 4);

        qemu_fseek (wav->f, 32, SEEK_CUR);
        qemu_put_buffer (wav->f, dlen, 4);
    }
    else {
        qemu_fseek (wav->f, 0, SEEK_END);
    }
}

static void wav_capture_cb (void *opaque, void *buf, int size)
{
    WAVState *wav = opaque;

    qemu_put_buffer (wav->f, buf, size);
    wav->bytes += size;
}

void wav_capture (const char *path, int freq, int bits16, int stereo)
{
    WAVState *wav;
    uint8_t hdr[] = {
        0x52, 0x49, 0x46, 0x46, 0x00, 0x00, 0x00, 0x00, 0x57, 0x41, 0x56,
        0x45, 0x66, 0x6d, 0x74, 0x20, 0x10, 0x00, 0x00, 0x00, 0x01, 0x00,
        0x02, 0x00, 0x44, 0xac, 0x00, 0x00, 0x10, 0xb1, 0x02, 0x00, 0x04,
        0x00, 0x10, 0x00, 0x64, 0x61, 0x74, 0x61, 0x00, 0x00, 0x00, 0x00
    };
    audsettings_t as;
    struct audio_capture_ops ops;
    int shift;

    stereo = !!stereo;
    bits16 = !!bits16;

    as.freq = freq;
    as.nchannels = 1 << stereo;
    as.fmt = bits16 ? AUD_FMT_S16 : AUD_FMT_U8;
    as.endianness = 0;

    ops.state = wav_state_cb;
    ops.capture = wav_capture_cb;

    wav = qemu_mallocz (sizeof (*wav));
    if (!wav) {
        AUD_log ("wav", "Could not allocate memory (%zu bytes)", sizeof (*wav));
        return;
    }

    shift = bits16 + stereo;
    hdr[34] = bits16 ? 0x10 : 0x08;

    le_store (hdr + 22, as.nchannels, 2);
    le_store (hdr + 24, freq, 4);
    le_store (hdr + 28, freq << shift, 4);
    le_store (hdr + 32, 1 << shift, 2);

    wav->f = fopen (path, "wb");
    if (!wav->f) {
        AUD_log ("wav", "Failed to open wave file `%s'\nReason: %s\n",
                 path, strerror (errno));
        qemu_free (wav);
        return;
    }

    qemu_put_buffer (wav->f, hdr, sizeof (hdr));
    AUD_add_capture (NULL, &as, &ops, wav);
}

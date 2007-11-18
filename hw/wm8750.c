/*
 * WM8750 audio CODEC.
 *
 * Copyright (c) 2006 Openedhand Ltd.
 * Written by Andrzej Zaborowski <balrog@zabor.org>
 *
 * This file is licensed under GNU GPL.
 */

#include "hw.h"
#include "i2c.h"
#include "audio/audio.h"

#define IN_PORT_N	3
#define OUT_PORT_N	3

#define CODEC		"wm8750"

struct wm_rate_s;
struct wm8750_s {
    i2c_slave i2c;
    uint8_t i2c_data[2];
    int i2c_len;
    QEMUSoundCard card;
    SWVoiceIn *adc_voice[IN_PORT_N];
    SWVoiceOut *dac_voice[OUT_PORT_N];
    int enable;
    void (*data_req)(void *, int, int);
    void *opaque;
    uint8_t data_in[4096];
    uint8_t data_out[4096];
    int idx_in, req_in;
    int idx_out, req_out;

    SWVoiceOut **out[2];
    uint8_t outvol[7], outmute[2];
    SWVoiceIn **in[2];
    uint8_t invol[4], inmute[2];

    uint8_t diff[2], pol, ds, monomix[2], alc, mute;
    uint8_t path[4], mpath[2], power, format;
    uint32_t inmask, outmask;
    const struct wm_rate_s *rate;
};

static inline void wm8750_in_load(struct wm8750_s *s)
{
    int acquired;
    if (s->idx_in + s->req_in <= sizeof(s->data_in))
        return;
    s->idx_in = audio_MAX(0, (int) sizeof(s->data_in) - s->req_in);
    acquired = AUD_read(*s->in[0], s->data_in + s->idx_in,
                    sizeof(s->data_in) - s->idx_in);
}

static inline void wm8750_out_flush(struct wm8750_s *s)
{
    int sent;
    if (!s->idx_out)
        return;
    sent = AUD_write(*s->out[0], s->data_out, s->idx_out);
    s->idx_out = 0;
}

static void wm8750_audio_in_cb(void *opaque, int avail_b)
{
    struct wm8750_s *s = (struct wm8750_s *) opaque;
    s->req_in = avail_b;
    s->data_req(s->opaque, s->req_out >> 2, avail_b >> 2);

#if 0
    wm8750_in_load(s);
#endif
}

static void wm8750_audio_out_cb(void *opaque, int free_b)
{
    struct wm8750_s *s = (struct wm8750_s *) opaque;
    wm8750_out_flush(s);

    s->req_out = free_b;
    s->data_req(s->opaque, free_b >> 2, s->req_in >> 2);
}

struct wm_rate_s {
    int adc;
    int adc_hz;
    int dac;
    int dac_hz;
};

static const struct wm_rate_s wm_rate_table[] = {
    {  256, 48000,  256, 48000 },	/* SR: 00000 */
    {  384, 48000,  384, 48000 },	/* SR: 00001 */
    {  256, 48000, 1536,  8000 },	/* SR: 00010 */
    {  384, 48000, 2304,  8000 },	/* SR: 00011 */
    { 1536,  8000,  256, 48000 },	/* SR: 00100 */
    { 2304,  8000,  384, 48000 },	/* SR: 00101 */
    { 1536,  8000, 1536,  8000 },	/* SR: 00110 */
    { 2304,  8000, 2304,  8000 },	/* SR: 00111 */
    { 1024, 12000, 1024, 12000 },	/* SR: 01000 */
    { 1526, 12000, 1536, 12000 },	/* SR: 01001 */
    {  768, 16000,  768, 16000 },	/* SR: 01010 */
    { 1152, 16000, 1152, 16000 },	/* SR: 01011 */
    {  384, 32000,  384, 32000 },	/* SR: 01100 */
    {  576, 32000,  576, 32000 },	/* SR: 01101 */
    {  128, 96000,  128, 96000 },	/* SR: 01110 */
    {  192, 96000,  192, 96000 },	/* SR: 01111 */
    {  256, 44100,  256, 44100 },	/* SR: 10000 */
    {  384, 44100,  384, 44100 },	/* SR: 10001 */
    {  256, 44100, 1408,  8018 },	/* SR: 10010 */
    {  384, 44100, 2112,  8018 },	/* SR: 10011 */
    { 1408,  8018,  256, 44100 },	/* SR: 10100 */
    { 2112,  8018,  384, 44100 },	/* SR: 10101 */
    { 1408,  8018, 1408,  8018 },	/* SR: 10110 */
    { 2112,  8018, 2112,  8018 },	/* SR: 10111 */
    { 1024, 11025, 1024, 11025 },	/* SR: 11000 */
    { 1536, 11025, 1536, 11025 },	/* SR: 11001 */
    {  512, 22050,  512, 22050 },	/* SR: 11010 */
    {  768, 22050,  768, 22050 },	/* SR: 11011 */
    {  512, 24000,  512, 24000 },	/* SR: 11100 */
    {  768, 24000,  768, 24000 },	/* SR: 11101 */
    {  128, 88200,  128, 88200 },	/* SR: 11110 */
    {  192, 88200,  128, 88200 },	/* SR: 11111 */
};

static void wm8750_set_format(struct wm8750_s *s)
{
    int i;
    audsettings_t in_fmt;
    audsettings_t out_fmt;
    audsettings_t monoout_fmt;

    wm8750_out_flush(s);

    if (s->in[0] && *s->in[0])
        AUD_set_active_in(*s->in[0], 0);
    if (s->out[0] && *s->out[0])
        AUD_set_active_out(*s->out[0], 0);

    for (i = 0; i < IN_PORT_N; i ++)
        if (s->adc_voice[i]) {
            AUD_close_in(&s->card, s->adc_voice[i]);
            s->adc_voice[i] = 0;
        }
    for (i = 0; i < OUT_PORT_N; i ++)
        if (s->dac_voice[i]) {
            AUD_close_out(&s->card, s->dac_voice[i]);
            s->dac_voice[i] = 0;
        }

    if (!s->enable)
        return;

    /* Setup input */
    in_fmt.endianness = 0;
    in_fmt.nchannels = 2;
    in_fmt.freq = s->rate->adc_hz;
    in_fmt.fmt = AUD_FMT_S16;

    s->adc_voice[0] = AUD_open_in(&s->card, s->adc_voice[0],
                    CODEC ".input1", s, wm8750_audio_in_cb, &in_fmt);
    s->adc_voice[1] = AUD_open_in(&s->card, s->adc_voice[1],
                    CODEC ".input2", s, wm8750_audio_in_cb, &in_fmt);
    s->adc_voice[2] = AUD_open_in(&s->card, s->adc_voice[2],
                    CODEC ".input3", s, wm8750_audio_in_cb, &in_fmt);

    /* Setup output */
    out_fmt.endianness = 0;
    out_fmt.nchannels = 2;
    out_fmt.freq = s->rate->dac_hz;
    out_fmt.fmt = AUD_FMT_S16;
    monoout_fmt.endianness = 0;
    monoout_fmt.nchannels = 1;
    monoout_fmt.freq = s->rate->dac_hz;
    monoout_fmt.fmt = AUD_FMT_S16;

    s->dac_voice[0] = AUD_open_out(&s->card, s->dac_voice[0],
                    CODEC ".speaker", s, wm8750_audio_out_cb, &out_fmt);
    s->dac_voice[1] = AUD_open_out(&s->card, s->dac_voice[1],
                    CODEC ".headphone", s, wm8750_audio_out_cb, &out_fmt);
    /* MONOMIX is also in stereo for simplicity */
    s->dac_voice[2] = AUD_open_out(&s->card, s->dac_voice[2],
                    CODEC ".monomix", s, wm8750_audio_out_cb, &out_fmt);
    /* no sense emulating OUT3 which is a mix of other outputs */

    /* We should connect the left and right channels to their
     * respective inputs/outputs but we have completely no need
     * for mixing or combining paths to different ports, so we
     * connect both channels to where the left channel is routed.  */
    if (s->in[0] && *s->in[0])
        AUD_set_active_in(*s->in[0], 1);
    if (s->out[0] && *s->out[0])
        AUD_set_active_out(*s->out[0], 1);
}

static void inline wm8750_mask_update(struct wm8750_s *s)
{
#define R_ONLY	0x0000ffff
#define L_ONLY	0xffff0000
#define BOTH	(R_ONLY | L_ONLY)
#define NONE	(R_ONLY & L_ONLY)
    s->inmask =
            (s->inmute[0] ? R_ONLY : BOTH) &
            (s->inmute[1] ? L_ONLY : BOTH) &
            (s->mute ? NONE : BOTH);
    s->outmask =
            (s->outmute[0] ? R_ONLY : BOTH) &
            (s->outmute[1] ? L_ONLY : BOTH) &
            (s->mute ? NONE : BOTH);
}

void wm8750_reset(i2c_slave *i2c)
{
    struct wm8750_s *s = (struct wm8750_s *) i2c;
    s->enable = 0;
    wm8750_set_format(s);
    s->diff[0] = 0;
    s->diff[1] = 0;
    s->ds = 0;
    s->alc = 0;
    s->in[0] = &s->adc_voice[0];
    s->invol[0] = 0x17;
    s->invol[1] = 0x17;
    s->invol[2] = 0xc3;
    s->invol[3] = 0xc3;
    s->out[0] = &s->dac_voice[0];
    s->outvol[0] = 0xff;
    s->outvol[1] = 0xff;
    s->outvol[2] = 0x79;
    s->outvol[3] = 0x79;
    s->outvol[4] = 0x79;
    s->outvol[5] = 0x79;
    s->inmute[0] = 0;
    s->inmute[1] = 0;
    s->outmute[0] = 0;
    s->outmute[1] = 0;
    s->mute = 1;
    s->path[0] = 0;
    s->path[1] = 0;
    s->path[2] = 0;
    s->path[3] = 0;
    s->mpath[0] = 0;
    s->mpath[1] = 0;
    s->format = 0x0a;
    s->idx_in = sizeof(s->data_in);
    s->req_in = 0;
    s->idx_out = 0;
    s->req_out = 0;
    wm8750_mask_update(s);
    s->i2c_len = 0;
}

static void wm8750_event(i2c_slave *i2c, enum i2c_event event)
{
    struct wm8750_s *s = (struct wm8750_s *) i2c;

    switch (event) {
    case I2C_START_SEND:
        s->i2c_len = 0;
        break;
    case I2C_FINISH:
#ifdef VERBOSE
        if (s->i2c_len < 2)
            printf("%s: message too short (%i bytes)\n",
                            __FUNCTION__, s->i2c_len);
#endif
        break;
    default:
        break;
    }
}

#define WM8750_LINVOL	0x00
#define WM8750_RINVOL	0x01
#define WM8750_LOUT1V	0x02
#define WM8750_ROUT1V	0x03
#define WM8750_ADCDAC	0x05
#define WM8750_IFACE	0x07
#define WM8750_SRATE	0x08
#define WM8750_LDAC	0x0a
#define WM8750_RDAC	0x0b
#define WM8750_BASS	0x0c
#define WM8750_TREBLE	0x0d
#define WM8750_RESET	0x0f
#define WM8750_3D	0x10
#define WM8750_ALC1	0x11
#define WM8750_ALC2	0x12
#define WM8750_ALC3	0x13
#define WM8750_NGATE	0x14
#define WM8750_LADC	0x15
#define WM8750_RADC	0x16
#define WM8750_ADCTL1	0x17
#define WM8750_ADCTL2	0x18
#define WM8750_PWR1	0x19
#define WM8750_PWR2	0x1a
#define WM8750_ADCTL3	0x1b
#define WM8750_ADCIN	0x1f
#define WM8750_LADCIN	0x20
#define WM8750_RADCIN	0x21
#define WM8750_LOUTM1	0x22
#define WM8750_LOUTM2	0x23
#define WM8750_ROUTM1	0x24
#define WM8750_ROUTM2	0x25
#define WM8750_MOUTM1	0x26
#define WM8750_MOUTM2	0x27
#define WM8750_LOUT2V	0x28
#define WM8750_ROUT2V	0x29
#define WM8750_MOUTV	0x2a

static int wm8750_tx(i2c_slave *i2c, uint8_t data)
{
    struct wm8750_s *s = (struct wm8750_s *) i2c;
    uint8_t cmd;
    uint16_t value;

    if (s->i2c_len >= 2) {
        printf("%s: long message (%i bytes)\n", __FUNCTION__, s->i2c_len);
#ifdef VERBOSE
        return 1;
#endif
    }
    s->i2c_data[s->i2c_len ++] = data;
    if (s->i2c_len != 2)
        return 0;

    cmd = s->i2c_data[0] >> 1;
    value = ((s->i2c_data[0] << 8) | s->i2c_data[1]) & 0x1ff;

    switch (cmd) {
    case WM8750_LADCIN:	/* ADC Signal Path Control (Left) */
        s->diff[0] = (((value >> 6) & 3) == 3);	/* LINSEL */
        if (s->diff[0])
            s->in[0] = &s->adc_voice[0 + s->ds * 1];
        else
            s->in[0] = &s->adc_voice[((value >> 6) & 3) * 1 + 0];
        break;

    case WM8750_RADCIN:	/* ADC Signal Path Control (Right) */
        s->diff[1] = (((value >> 6) & 3) == 3);	/* RINSEL */
        if (s->diff[1])
            s->in[1] = &s->adc_voice[0 + s->ds * 1];
        else
            s->in[1] = &s->adc_voice[((value >> 6) & 3) * 1 + 0];
        break;

    case WM8750_ADCIN:	/* ADC Input Mode */
        s->ds = (value >> 8) & 1;	/* DS */
        if (s->diff[0])
            s->in[0] = &s->adc_voice[0 + s->ds * 1];
        if (s->diff[1])
            s->in[1] = &s->adc_voice[0 + s->ds * 1];
        s->monomix[0] = (value >> 6) & 3;	/* MONOMIX */
        break;

    case WM8750_ADCTL1:	/* Additional Control (1) */
        s->monomix[1] = (value >> 1) & 1;	/* DMONOMIX */
        break;

    case WM8750_PWR1:	/* Power Management (1) */
        s->enable = ((value >> 6) & 7) == 3;	/* VMIDSEL, VREF */
        wm8750_set_format(s);
        break;

    case WM8750_LINVOL:	/* Left Channel PGA */
        s->invol[0] = value & 0x3f;		/* LINVOL */
        s->inmute[0] = (value >> 7) & 1;	/* LINMUTE */
        wm8750_mask_update(s);
        break;

    case WM8750_RINVOL:	/* Right Channel PGA */
        s->invol[1] = value & 0x3f;		/* RINVOL */
        s->inmute[1] = (value >> 7) & 1;	/* RINMUTE */
        wm8750_mask_update(s);
        break;

    case WM8750_ADCDAC:	/* ADC and DAC Control */
        s->pol = (value >> 5) & 3;		/* ADCPOL */
        s->mute = (value >> 3) & 1;		/* DACMU */
        wm8750_mask_update(s);
        break;

    case WM8750_ADCTL3:	/* Additional Control (3) */
        break;

    case WM8750_LADC:	/* Left ADC Digital Volume */
        s->invol[2] = value & 0xff;		/* LADCVOL */
        break;

    case WM8750_RADC:	/* Right ADC Digital Volume */
        s->invol[3] = value & 0xff;		/* RADCVOL */
        break;

    case WM8750_ALC1:	/* ALC Control (1) */
        s->alc = (value >> 7) & 3;		/* ALCSEL */
        break;

    case WM8750_NGATE:	/* Noise Gate Control */
    case WM8750_3D:	/* 3D enhance */
        break;

    case WM8750_LDAC:	/* Left Channel Digital Volume */
        s->outvol[0] = value & 0xff;		/* LDACVOL */
        break;

    case WM8750_RDAC:	/* Right Channel Digital Volume */
        s->outvol[1] = value & 0xff;		/* RDACVOL */
        break;

    case WM8750_BASS:	/* Bass Control */
        break;

    case WM8750_LOUTM1:	/* Left Mixer Control (1) */
        s->path[0] = (value >> 8) & 1;		/* LD2LO */
        break;

    case WM8750_LOUTM2:	/* Left Mixer Control (2) */
        s->path[1] = (value >> 8) & 1;		/* RD2LO */
        break;

    case WM8750_ROUTM1:	/* Right Mixer Control (1) */
        s->path[2] = (value >> 8) & 1;		/* LD2RO */
        break;

    case WM8750_ROUTM2:	/* Right Mixer Control (2) */
        s->path[3] = (value >> 8) & 1;		/* RD2RO */
        break;

    case WM8750_MOUTM1:	/* Mono Mixer Control (1) */
        s->mpath[0] = (value >> 8) & 1;		/* LD2MO */
        break;

    case WM8750_MOUTM2:	/* Mono Mixer Control (2) */
        s->mpath[1] = (value >> 8) & 1;		/* RD2MO */
        break;

    case WM8750_LOUT1V:	/* LOUT1 Volume */
        s->outvol[2] = value & 0x7f;		/* LOUT2VOL */
        break;

    case WM8750_LOUT2V:	/* LOUT2 Volume */
        s->outvol[4] = value & 0x7f;		/* LOUT2VOL */
        break;

    case WM8750_ROUT1V:	/* ROUT1 Volume */
        s->outvol[3] = value & 0x7f;		/* ROUT2VOL */
        break;

    case WM8750_ROUT2V:	/* ROUT2 Volume */
        s->outvol[5] = value & 0x7f;		/* ROUT2VOL */
        break;

    case WM8750_MOUTV:	/* MONOOUT Volume */
        s->outvol[6] = value & 0x7f;		/* MONOOUTVOL */
        break;

    case WM8750_ADCTL2:	/* Additional Control (2) */
        break;

    case WM8750_PWR2:	/* Power Management (2) */
        s->power = value & 0x7e;
        break;

    case WM8750_IFACE:	/* Digital Audio Interface Format */
#ifdef VERBOSE
        if (value & 0x40)			/* MS */
            printf("%s: attempt to enable Master Mode\n", __FUNCTION__);
#endif
        s->format = value;
        wm8750_set_format(s);
        break;

    case WM8750_SRATE:	/* Clocking and Sample Rate Control */
        s->rate = &wm_rate_table[(value >> 1) & 0x1f];
        wm8750_set_format(s);
        break;

    case WM8750_RESET:	/* Reset */
        wm8750_reset(&s->i2c);
        break;

#ifdef VERBOSE
    default:
        printf("%s: unknown register %02x\n", __FUNCTION__, cmd);
#endif
    }

    return 0;
}

static int wm8750_rx(i2c_slave *i2c)
{
    return 0x00;
}

static void wm8750_save(QEMUFile *f, void *opaque)
{
    struct wm8750_s *s = (struct wm8750_s *) opaque;
    int i;
    qemu_put_8s(f, &s->i2c_data[0]);
    qemu_put_8s(f, &s->i2c_data[1]);
    qemu_put_be32(f, s->i2c_len);
    qemu_put_be32(f, s->enable);
    qemu_put_be32(f, s->idx_in);
    qemu_put_be32(f, s->req_in);
    qemu_put_be32(f, s->idx_out);
    qemu_put_be32(f, s->req_out);

    for (i = 0; i < 7; i ++)
        qemu_put_8s(f, &s->outvol[i]);
    for (i = 0; i < 2; i ++)
        qemu_put_8s(f, &s->outmute[i]);
    for (i = 0; i < 4; i ++)
        qemu_put_8s(f, &s->invol[i]);
    for (i = 0; i < 2; i ++)
        qemu_put_8s(f, &s->inmute[i]);

    for (i = 0; i < 2; i ++)
        qemu_put_8s(f, &s->diff[i]);
    qemu_put_8s(f, &s->pol);
    qemu_put_8s(f, &s->ds);
    for (i = 0; i < 2; i ++)
        qemu_put_8s(f, &s->monomix[i]);
    qemu_put_8s(f, &s->alc);
    qemu_put_8s(f, &s->mute);
    for (i = 0; i < 4; i ++)
        qemu_put_8s(f, &s->path[i]);
    for (i = 0; i < 2; i ++)
        qemu_put_8s(f, &s->mpath[i]);
    qemu_put_8s(f, &s->format);
    qemu_put_8s(f, &s->power);
    qemu_put_be32s(f, &s->inmask);
    qemu_put_be32s(f, &s->outmask);
    qemu_put_byte(f, (s->rate - wm_rate_table) / sizeof(*s->rate));
    i2c_slave_save(f, &s->i2c);
}

static int wm8750_load(QEMUFile *f, void *opaque, int version_id)
{
    struct wm8750_s *s = (struct wm8750_s *) opaque;
    int i;
    qemu_get_8s(f, &s->i2c_data[0]);
    qemu_get_8s(f, &s->i2c_data[1]);
    s->i2c_len = qemu_get_be32(f);
    s->enable = qemu_get_be32(f);
    s->idx_in = qemu_get_be32(f);
    s->req_in = qemu_get_be32(f);
    s->idx_out = qemu_get_be32(f);
    s->req_out = qemu_get_be32(f);

    for (i = 0; i < 7; i ++)
        qemu_get_8s(f, &s->outvol[i]);
    for (i = 0; i < 2; i ++)
        qemu_get_8s(f, &s->outmute[i]);
    for (i = 0; i < 4; i ++)
        qemu_get_8s(f, &s->invol[i]);
    for (i = 0; i < 2; i ++)
        qemu_get_8s(f, &s->inmute[i]);

    for (i = 0; i < 2; i ++)
        qemu_get_8s(f, &s->diff[i]);
    qemu_get_8s(f, &s->pol);
    qemu_get_8s(f, &s->ds);
    for (i = 0; i < 2; i ++)
        qemu_get_8s(f, &s->monomix[i]);
    qemu_get_8s(f, &s->alc);
    qemu_get_8s(f, &s->mute);
    for (i = 0; i < 4; i ++)
        qemu_get_8s(f, &s->path[i]);
    for (i = 0; i < 2; i ++)
        qemu_get_8s(f, &s->mpath[i]);
    qemu_get_8s(f, &s->format);
    qemu_get_8s(f, &s->power);
    qemu_get_be32s(f, &s->inmask);
    qemu_get_be32s(f, &s->outmask);
    s->rate = &wm_rate_table[(uint8_t) qemu_get_byte(f) & 0x1f];
    i2c_slave_load(f, &s->i2c);
    return 0;
}

static int wm8750_iid = 0;

i2c_slave *wm8750_init(i2c_bus *bus, AudioState *audio)
{
    struct wm8750_s *s = (struct wm8750_s *)
            i2c_slave_init(bus, 0, sizeof(struct wm8750_s));
    s->i2c.event = wm8750_event;
    s->i2c.recv = wm8750_rx;
    s->i2c.send = wm8750_tx;

    AUD_register_card(audio, CODEC, &s->card);
    wm8750_reset(&s->i2c);

    register_savevm(CODEC, wm8750_iid ++, 0, wm8750_save, wm8750_load, s);

    return &s->i2c;
}

static void wm8750_fini(i2c_slave *i2c)
{
    struct wm8750_s *s = (struct wm8750_s *) i2c;
    wm8750_reset(&s->i2c);
    AUD_remove_card(&s->card);
    qemu_free(s);
}

void wm8750_data_req_set(i2c_slave *i2c,
                void (*data_req)(void *, int, int), void *opaque)
{
    struct wm8750_s *s = (struct wm8750_s *) i2c;
    s->data_req = data_req;
    s->opaque = opaque;
}

void wm8750_dac_dat(void *opaque, uint32_t sample)
{
    struct wm8750_s *s = (struct wm8750_s *) opaque;
    uint32_t *data = (uint32_t *) &s->data_out[s->idx_out];
    *data = sample & s->outmask;
    s->req_out -= 4;
    s->idx_out += 4;
    if (s->idx_out >= sizeof(s->data_out) || s->req_out <= 0)
        wm8750_out_flush(s);
}

uint32_t wm8750_adc_dat(void *opaque)
{
    struct wm8750_s *s = (struct wm8750_s *) opaque;
    uint32_t *data;
    if (s->idx_in >= sizeof(s->data_in))
        wm8750_in_load(s);
    data = (uint32_t *) &s->data_in[s->idx_in];
    s->req_in -= 4;
    s->idx_in += 4;
    return *data & s->inmask;
}

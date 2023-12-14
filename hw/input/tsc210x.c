/*
 * TI TSC2102 (touchscreen/sensors/audio controller) emulator.
 * TI TSC2301 (touchscreen/sensors/keypad).
 *
 * Copyright (c) 2006 Andrzej Zaborowski  <balrog@zabor.org>
 * Copyright (C) 2008 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "audio/audio.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "sysemu/reset.h"
#include "ui/console.h"
#include "hw/arm/omap.h"            /* For I2SCodec */
#include "hw/boards.h"              /* for current_machine */
#include "hw/input/tsc2xxx.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "qapi/error.h"

#define TSC_DATA_REGISTERS_PAGE		0x0
#define TSC_CONTROL_REGISTERS_PAGE	0x1
#define TSC_AUDIO_REGISTERS_PAGE	0x2

#define TSC_VERBOSE

#define TSC_CUT_RESOLUTION(value, p)	((value) >> (16 - resolution[p]))

typedef struct {
    qemu_irq pint;
    qemu_irq kbint;
    qemu_irq davint;
    QEMUTimer *timer;
    QEMUSoundCard card;
    uWireSlave chip;
    I2SCodec codec;
    uint8_t in_fifo[16384];
    uint8_t out_fifo[16384];
    uint16_t model;

    int32_t x, y;
    bool pressure;

    uint8_t page, offset;
    uint16_t dav;

    bool state;
    bool irq;
    bool command;
    bool busy;
    bool enabled;
    bool host_mode;
    uint8_t function, nextfunction;
    uint8_t precision, nextprecision;
    uint8_t filter;
    uint8_t pin_func;
    uint8_t ref;
    uint8_t timing;
    uint8_t noise;

    uint16_t audio_ctrl1;
    uint16_t audio_ctrl2;
    uint16_t audio_ctrl3;
    uint16_t pll[3];
    uint16_t volume;
    int64_t volume_change;
    bool softstep;
    uint16_t dac_power;
    int64_t powerdown;
    uint16_t filter_data[0x14];

    const char *name;
    SWVoiceIn *adc_voice[1];
    SWVoiceOut *dac_voice[1];
    int i2s_rx_rate;
    int i2s_tx_rate;

    int tr[8];

    struct {
        uint16_t down;
        uint16_t mask;
        int scan;
        int debounce;
        int mode;
        int intr;
    } kb;
    int64_t now; /* Time at migration */
} TSC210xState;

static const int resolution[4] = { 12, 8, 10, 12 };

#define TSC_MODE_NO_SCAN	0x0
#define TSC_MODE_XY_SCAN	0x1
#define TSC_MODE_XYZ_SCAN	0x2
#define TSC_MODE_X		0x3
#define TSC_MODE_Y		0x4
#define TSC_MODE_Z		0x5
#define TSC_MODE_BAT1		0x6
#define TSC_MODE_BAT2		0x7
#define TSC_MODE_AUX		0x8
#define TSC_MODE_AUX_SCAN	0x9
#define TSC_MODE_TEMP1		0xa
#define TSC_MODE_PORT_SCAN	0xb
#define TSC_MODE_TEMP2		0xc
#define TSC_MODE_XX_DRV		0xd
#define TSC_MODE_YY_DRV		0xe
#define TSC_MODE_YX_DRV		0xf

static const uint16_t mode_regs[16] = {
    0x0000,	/* No scan */
    0x0600,	/* X, Y scan */
    0x0780,	/* X, Y, Z scan */
    0x0400,	/* X */
    0x0200,	/* Y */
    0x0180,	/* Z */
    0x0040,	/* BAT1 */
    0x0030,	/* BAT2 */
    0x0010,	/* AUX */
    0x0010,	/* AUX scan */
    0x0004,	/* TEMP1 */
    0x0070,	/* Port scan */
    0x0002,	/* TEMP2 */
    0x0000,	/* X+, X- drivers */
    0x0000,	/* Y+, Y- drivers */
    0x0000,	/* Y+, X- drivers */
};

#define X_TRANSFORM(s)			\
    ((s->y * s->tr[0] - s->x * s->tr[1]) / s->tr[2] + s->tr[3])
#define Y_TRANSFORM(s)			\
    ((s->y * s->tr[4] - s->x * s->tr[5]) / s->tr[6] + s->tr[7])
#define Z1_TRANSFORM(s)			\
    ((400 - ((s)->x >> 7) + ((s)->pressure << 10)) << 4)
#define Z2_TRANSFORM(s)			\
    ((4000 + ((s)->y >> 7) - ((s)->pressure << 10)) << 4)

#define BAT1_VAL			0x8660
#define BAT2_VAL			0x0000
#define AUX1_VAL			0x35c0
#define AUX2_VAL			0xffff
#define TEMP1_VAL			0x8c70
#define TEMP2_VAL			0xa5b0

#define TSC_POWEROFF_DELAY		50
#define TSC_SOFTSTEP_DELAY		50

static void tsc210x_reset(TSC210xState *s)
{
    s->state = false;
    s->pin_func = 2;
    s->enabled = false;
    s->busy = false;
    s->nextfunction = 0;
    s->ref = 0;
    s->timing = 0;
    s->irq = false;
    s->dav = 0;

    s->audio_ctrl1 = 0x0000;
    s->audio_ctrl2 = 0x4410;
    s->audio_ctrl3 = 0x0000;
    s->pll[0] = 0x1004;
    s->pll[1] = 0x0000;
    s->pll[2] = 0x1fff;
    s->volume = 0xffff;
    s->dac_power = 0x8540;
    s->softstep = true;
    s->volume_change = 0;
    s->powerdown = 0;
    s->filter_data[0x00] = 0x6be3;
    s->filter_data[0x01] = 0x9666;
    s->filter_data[0x02] = 0x675d;
    s->filter_data[0x03] = 0x6be3;
    s->filter_data[0x04] = 0x9666;
    s->filter_data[0x05] = 0x675d;
    s->filter_data[0x06] = 0x7d83;
    s->filter_data[0x07] = 0x84ee;
    s->filter_data[0x08] = 0x7d83;
    s->filter_data[0x09] = 0x84ee;
    s->filter_data[0x0a] = 0x6be3;
    s->filter_data[0x0b] = 0x9666;
    s->filter_data[0x0c] = 0x675d;
    s->filter_data[0x0d] = 0x6be3;
    s->filter_data[0x0e] = 0x9666;
    s->filter_data[0x0f] = 0x675d;
    s->filter_data[0x10] = 0x7d83;
    s->filter_data[0x11] = 0x84ee;
    s->filter_data[0x12] = 0x7d83;
    s->filter_data[0x13] = 0x84ee;

    s->i2s_tx_rate = 0;
    s->i2s_rx_rate = 0;

    s->kb.scan = 1;
    s->kb.debounce = 0;
    s->kb.mask = 0x0000;
    s->kb.mode = 3;
    s->kb.intr = 0;

    qemu_set_irq(s->pint, !s->irq);
    qemu_set_irq(s->davint, !s->dav);
    qemu_irq_raise(s->kbint);
}

typedef struct {
    int rate;
    int dsor;
    int fsref;
} TSC210xRateInfo;

/*  { rate,   dsor, fsref }	*/
static const TSC210xRateInfo tsc2102_rates[] = {
    /* Fsref / 6.0 */
    { 7350,	63,	1 },
    { 8000,	63,	0 },
    /* Fsref / 6.0 */
    { 7350,	54,	1 },
    { 8000,	54,	0 },
    /* Fsref / 5.0 */
    { 8820,	45,	1 },
    { 9600,	45,	0 },
    /* Fsref / 4.0 */
    { 11025,	36,	1 },
    { 12000,	36,	0 },
    /* Fsref / 3.0 */
    { 14700,	27,	1 },
    { 16000,	27,	0 },
    /* Fsref / 2.0 */
    { 22050,	18,	1 },
    { 24000,	18,	0 },
    /* Fsref / 1.5 */
    { 29400,	9,	1 },
    { 32000,	9,	0 },
    /* Fsref */
    { 44100,	0,	1 },
    { 48000,	0,	0 },

    { 0,	0, 	0 },
};

static inline void tsc210x_out_flush(TSC210xState *s, int len)
{
    uint8_t *data = s->codec.out.fifo + s->codec.out.start;
    uint8_t *end = data + len;

    while (data < end)
        data += AUD_write(s->dac_voice[0], data, end - data) ?: (end - data);

    s->codec.out.len -= len;
    if (s->codec.out.len)
        memmove(s->codec.out.fifo, end, s->codec.out.len);
    s->codec.out.start = 0;
}

static void tsc210x_audio_out_cb(TSC210xState *s, int free_b)
{
    if (s->codec.out.len >= free_b) {
        tsc210x_out_flush(s, free_b);
        return;
    }

    s->codec.out.size = MIN(free_b, 16384);
    qemu_irq_raise(s->codec.tx_start);
}

static void tsc2102_audio_rate_update(TSC210xState *s)
{
    const TSC210xRateInfo *rate;

    s->codec.tx_rate = 0;
    s->codec.rx_rate = 0;
    if (s->dac_power & (1 << 15))				/* PWDNC */
        return;

    for (rate = tsc2102_rates; rate->rate; rate ++)
        if (rate->dsor == (s->audio_ctrl1 & 0x3f) &&		/* DACFS */
                        rate->fsref == ((s->audio_ctrl3 >> 13) & 1))/* REFFS */
            break;
    if (!rate->rate) {
        printf("%s: unknown sampling rate configured\n", __func__);
        return;
    }

    s->codec.tx_rate = rate->rate;
}

static void tsc2102_audio_output_update(TSC210xState *s)
{
    int enable;
    struct audsettings fmt;

    if (s->dac_voice[0]) {
        tsc210x_out_flush(s, s->codec.out.len);
        s->codec.out.size = 0;
        AUD_set_active_out(s->dac_voice[0], 0);
        AUD_close_out(&s->card, s->dac_voice[0]);
        s->dac_voice[0] = NULL;
    }
    s->codec.cts = 0;

    enable =
            (~s->dac_power & (1 << 15)) &&			/* PWDNC */
            (~s->dac_power & (1 << 10));			/* DAPWDN */
    if (!enable || !s->codec.tx_rate)
        return;

    /* Force our own sampling rate even in slave DAC mode */
    fmt.endianness = 0;
    fmt.nchannels = 2;
    fmt.freq = s->codec.tx_rate;
    fmt.fmt = AUDIO_FORMAT_S16;

    s->dac_voice[0] = AUD_open_out(&s->card, s->dac_voice[0],
                    "tsc2102.sink", s, (void *) tsc210x_audio_out_cb, &fmt);
    if (s->dac_voice[0]) {
        s->codec.cts = 1;
        AUD_set_active_out(s->dac_voice[0], 1);
    }
}

static uint16_t tsc2102_data_register_read(TSC210xState *s, int reg)
{
    switch (reg) {
    case 0x00:	/* X */
        s->dav &= 0xfbff;
        return TSC_CUT_RESOLUTION(X_TRANSFORM(s), s->precision) +
                (s->noise & 3);

    case 0x01:	/* Y */
        s->noise ++;
        s->dav &= 0xfdff;
        return TSC_CUT_RESOLUTION(Y_TRANSFORM(s), s->precision) ^
                (s->noise & 3);

    case 0x02:	/* Z1 */
        s->dav &= 0xfeff;
        return TSC_CUT_RESOLUTION(Z1_TRANSFORM(s), s->precision) -
                (s->noise & 3);

    case 0x03:	/* Z2 */
        s->dav &= 0xff7f;
        return TSC_CUT_RESOLUTION(Z2_TRANSFORM(s), s->precision) |
                (s->noise & 3);

    case 0x04:	/* KPData */
        if ((s->model & 0xff00) == 0x2300) {
            if (s->kb.intr && (s->kb.mode & 2)) {
                s->kb.intr = 0;
                qemu_irq_raise(s->kbint);
            }
            return s->kb.down;
        }

        return 0xffff;

    case 0x05:	/* BAT1 */
        s->dav &= 0xffbf;
        return TSC_CUT_RESOLUTION(BAT1_VAL, s->precision) +
                (s->noise & 6);

    case 0x06:	/* BAT2 */
        s->dav &= 0xffdf;
        return TSC_CUT_RESOLUTION(BAT2_VAL, s->precision);

    case 0x07:	/* AUX1 */
        s->dav &= 0xffef;
        return TSC_CUT_RESOLUTION(AUX1_VAL, s->precision);

    case 0x08:	/* AUX2 */
        s->dav &= 0xfff7;
        return 0xffff;

    case 0x09:	/* TEMP1 */
        s->dav &= 0xfffb;
        return TSC_CUT_RESOLUTION(TEMP1_VAL, s->precision) -
                (s->noise & 5);

    case 0x0a:	/* TEMP2 */
        s->dav &= 0xfffd;
        return TSC_CUT_RESOLUTION(TEMP2_VAL, s->precision) ^
                (s->noise & 3);

    case 0x0b:	/* DAC */
        s->dav &= 0xfffe;
        return 0xffff;

    default:
#ifdef TSC_VERBOSE
        fprintf(stderr, "tsc2102_data_register_read: "
                        "no such register: 0x%02x\n", reg);
#endif
        return 0xffff;
    }
}

static uint16_t tsc2102_control_register_read(
                TSC210xState *s, int reg)
{
    switch (reg) {
    case 0x00:	/* TSC ADC */
        return (s->pressure << 15) | ((!s->busy) << 14) |
                (s->nextfunction << 10) | (s->nextprecision << 8) | s->filter; 

    case 0x01:	/* Status / Keypad Control */
        if ((s->model & 0xff00) == 0x2100)
            return (s->pin_func << 14) | ((!s->enabled) << 13) |
                    (s->host_mode << 12) | ((!!s->dav) << 11) | s->dav;
        else
            return (s->kb.intr << 15) | ((s->kb.scan || !s->kb.down) << 14) |
                    (s->kb.debounce << 11);

    case 0x02:	/* DAC Control */
        if ((s->model & 0xff00) == 0x2300)
            return s->dac_power & 0x8000;
        else
            goto bad_reg;

    case 0x03:	/* Reference */
        return s->ref;

    case 0x04:	/* Reset */
        return 0xffff;

    case 0x05:	/* Configuration */
        return s->timing;

    case 0x06:	/* Secondary configuration */
        if ((s->model & 0xff00) == 0x2100)
            goto bad_reg;
        return ((!s->dav) << 15) | ((s->kb.mode & 1) << 14) | s->pll[2];

    case 0x10:	/* Keypad Mask */
        if ((s->model & 0xff00) == 0x2100)
            goto bad_reg;
        return s->kb.mask;

    default:
    bad_reg:
#ifdef TSC_VERBOSE
        fprintf(stderr, "tsc2102_control_register_read: "
                        "no such register: 0x%02x\n", reg);
#endif
        return 0xffff;
    }
}

static uint16_t tsc2102_audio_register_read(TSC210xState *s, int reg)
{
    int l_ch, r_ch;
    uint16_t val;

    switch (reg) {
    case 0x00:	/* Audio Control 1 */
        return s->audio_ctrl1;

    case 0x01:
        return 0xff00;

    case 0x02:	/* DAC Volume Control */
        return s->volume;

    case 0x03:
        return 0x8b00;

    case 0x04:	/* Audio Control 2 */
        l_ch = 1;
        r_ch = 1;
        if (s->softstep && !(s->dac_power & (1 << 10))) {
            l_ch = (qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) >
                            s->volume_change + TSC_SOFTSTEP_DELAY);
            r_ch = (qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) >
                            s->volume_change + TSC_SOFTSTEP_DELAY);
        }

        return s->audio_ctrl2 | (l_ch << 3) | (r_ch << 2);

    case 0x05:	/* Stereo DAC Power Control */
        return 0x2aa0 | s->dac_power |
                (((s->dac_power & (1 << 10)) &&
                  (qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) >
                   s->powerdown + TSC_POWEROFF_DELAY)) << 6);

    case 0x06:	/* Audio Control 3 */
        val = s->audio_ctrl3 | 0x0001;
        s->audio_ctrl3 &= 0xff3f;
        return val;

    case 0x07:	/* LCH_BASS_BOOST_N0 */
    case 0x08:	/* LCH_BASS_BOOST_N1 */
    case 0x09:	/* LCH_BASS_BOOST_N2 */
    case 0x0a:	/* LCH_BASS_BOOST_N3 */
    case 0x0b:	/* LCH_BASS_BOOST_N4 */
    case 0x0c:	/* LCH_BASS_BOOST_N5 */
    case 0x0d:	/* LCH_BASS_BOOST_D1 */
    case 0x0e:	/* LCH_BASS_BOOST_D2 */
    case 0x0f:	/* LCH_BASS_BOOST_D4 */
    case 0x10:	/* LCH_BASS_BOOST_D5 */
    case 0x11:	/* RCH_BASS_BOOST_N0 */
    case 0x12:	/* RCH_BASS_BOOST_N1 */
    case 0x13:	/* RCH_BASS_BOOST_N2 */
    case 0x14:	/* RCH_BASS_BOOST_N3 */
    case 0x15:	/* RCH_BASS_BOOST_N4 */
    case 0x16:	/* RCH_BASS_BOOST_N5 */
    case 0x17:	/* RCH_BASS_BOOST_D1 */
    case 0x18:	/* RCH_BASS_BOOST_D2 */
    case 0x19:	/* RCH_BASS_BOOST_D4 */
    case 0x1a:	/* RCH_BASS_BOOST_D5 */
        return s->filter_data[reg - 0x07];

    case 0x1b:	/* PLL Programmability 1 */
        return s->pll[0];

    case 0x1c:	/* PLL Programmability 2 */
        return s->pll[1];

    case 0x1d:	/* Audio Control 4 */
        return (!s->softstep) << 14;

    default:
#ifdef TSC_VERBOSE
        fprintf(stderr, "tsc2102_audio_register_read: "
                        "no such register: 0x%02x\n", reg);
#endif
        return 0xffff;
    }
}

static void tsc2102_data_register_write(
                TSC210xState *s, int reg, uint16_t value)
{
    switch (reg) {
    case 0x00:	/* X */
    case 0x01:	/* Y */
    case 0x02:	/* Z1 */
    case 0x03:	/* Z2 */
    case 0x05:	/* BAT1 */
    case 0x06:	/* BAT2 */
    case 0x07:	/* AUX1 */
    case 0x08:	/* AUX2 */
    case 0x09:	/* TEMP1 */
    case 0x0a:	/* TEMP2 */
        return;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "tsc2102_data_register_write: "
                                       "no such register: 0x%02x\n", reg);
    }
}

static void tsc2102_control_register_write(
                TSC210xState *s, int reg, uint16_t value)
{
    switch (reg) {
    case 0x00:	/* TSC ADC */
        s->host_mode = value >> 15;
        s->enabled = !(value & 0x4000);
        if (s->busy && !s->enabled)
            timer_del(s->timer);
        s->busy = s->busy && s->enabled;
        s->nextfunction = (value >> 10) & 0xf;
        s->nextprecision = (value >> 8) & 3;
        s->filter = value & 0xff;
        return;

    case 0x01:	/* Status / Keypad Control */
        if ((s->model & 0xff00) == 0x2100)
            s->pin_func = value >> 14;
        else {
            s->kb.scan = (value >> 14) & 1;
            s->kb.debounce = (value >> 11) & 7;
            if (s->kb.intr && s->kb.scan) {
                s->kb.intr = 0;
                qemu_irq_raise(s->kbint);
            }
        }
        return;

    case 0x02:	/* DAC Control */
        if ((s->model & 0xff00) == 0x2300) {
            s->dac_power &= 0x7fff;
            s->dac_power |= 0x8000 & value;
        } else
            goto bad_reg;
        break;

    case 0x03:	/* Reference */
        s->ref = value & 0x1f;
        return;

    case 0x04:	/* Reset */
        if (value == 0xbb00) {
            if (s->busy)
                timer_del(s->timer);
            tsc210x_reset(s);
#ifdef TSC_VERBOSE
        } else {
            fprintf(stderr, "tsc2102_control_register_write: "
                            "wrong value written into RESET\n");
#endif
        }
        return;

    case 0x05:	/* Configuration */
        s->timing = value & 0x3f;
#ifdef TSC_VERBOSE
        if (value & ~0x3f)
            fprintf(stderr, "tsc2102_control_register_write: "
                            "wrong value written into CONFIG\n");
#endif
        return;

    case 0x06:	/* Secondary configuration */
        if ((s->model & 0xff00) == 0x2100)
            goto bad_reg;
        s->kb.mode = value >> 14;
        s->pll[2] = value & 0x3ffff;
        return;

    case 0x10:	/* Keypad Mask */
        if ((s->model & 0xff00) == 0x2100)
            goto bad_reg;
        s->kb.mask = value;
        return;

    default:
    bad_reg:
        qemu_log_mask(LOG_GUEST_ERROR, "tsc2102_control_register_write: "
                                       "no such register: 0x%02x\n", reg);
    }
}

static void tsc2102_audio_register_write(
                TSC210xState *s, int reg, uint16_t value)
{
    switch (reg) {
    case 0x00:	/* Audio Control 1 */
        s->audio_ctrl1 = value & 0x0f3f;
#ifdef TSC_VERBOSE
        if ((value & ~0x0f3f) || ((value & 7) != ((value >> 3) & 7)))
            fprintf(stderr, "tsc2102_audio_register_write: "
                            "wrong value written into Audio 1\n");
#endif
        tsc2102_audio_rate_update(s);
        tsc2102_audio_output_update(s);
        return;

    case 0x01:
#ifdef TSC_VERBOSE
        if (value != 0xff00)
            fprintf(stderr, "tsc2102_audio_register_write: "
                            "wrong value written into reg 0x01\n");
#endif
        return;

    case 0x02:	/* DAC Volume Control */
        s->volume = value;
        s->volume_change = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        return;

    case 0x03:
#ifdef TSC_VERBOSE
        if (value != 0x8b00)
            fprintf(stderr, "tsc2102_audio_register_write: "
                            "wrong value written into reg 0x03\n");
#endif
        return;

    case 0x04:	/* Audio Control 2 */
        s->audio_ctrl2 = value & 0xf7f2;
#ifdef TSC_VERBOSE
        if (value & ~0xf7fd)
            fprintf(stderr, "tsc2102_audio_register_write: "
                            "wrong value written into Audio 2\n");
#endif
        return;

    case 0x05:	/* Stereo DAC Power Control */
        if ((value & ~s->dac_power) & (1 << 10))
            s->powerdown = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

        s->dac_power = value & 0x9543;
#ifdef TSC_VERBOSE
        if ((value & ~0x9543) != 0x2aa0)
            fprintf(stderr, "tsc2102_audio_register_write: "
                            "wrong value written into Power\n");
#endif
        tsc2102_audio_rate_update(s);
        tsc2102_audio_output_update(s);
        return;

    case 0x06:	/* Audio Control 3 */
        s->audio_ctrl3 &= 0x00c0;
        s->audio_ctrl3 |= value & 0xf800;
#ifdef TSC_VERBOSE
        if (value & ~0xf8c7)
            fprintf(stderr, "tsc2102_audio_register_write: "
                            "wrong value written into Audio 3\n");
#endif
        tsc2102_audio_output_update(s);
        return;

    case 0x07:	/* LCH_BASS_BOOST_N0 */
    case 0x08:	/* LCH_BASS_BOOST_N1 */
    case 0x09:	/* LCH_BASS_BOOST_N2 */
    case 0x0a:	/* LCH_BASS_BOOST_N3 */
    case 0x0b:	/* LCH_BASS_BOOST_N4 */
    case 0x0c:	/* LCH_BASS_BOOST_N5 */
    case 0x0d:	/* LCH_BASS_BOOST_D1 */
    case 0x0e:	/* LCH_BASS_BOOST_D2 */
    case 0x0f:	/* LCH_BASS_BOOST_D4 */
    case 0x10:	/* LCH_BASS_BOOST_D5 */
    case 0x11:	/* RCH_BASS_BOOST_N0 */
    case 0x12:	/* RCH_BASS_BOOST_N1 */
    case 0x13:	/* RCH_BASS_BOOST_N2 */
    case 0x14:	/* RCH_BASS_BOOST_N3 */
    case 0x15:	/* RCH_BASS_BOOST_N4 */
    case 0x16:	/* RCH_BASS_BOOST_N5 */
    case 0x17:	/* RCH_BASS_BOOST_D1 */
    case 0x18:	/* RCH_BASS_BOOST_D2 */
    case 0x19:	/* RCH_BASS_BOOST_D4 */
    case 0x1a:	/* RCH_BASS_BOOST_D5 */
        s->filter_data[reg - 0x07] = value;
        return;

    case 0x1b:	/* PLL Programmability 1 */
        s->pll[0] = value & 0xfffc;
#ifdef TSC_VERBOSE
        if (value & ~0xfffc)
            fprintf(stderr, "tsc2102_audio_register_write: "
                            "wrong value written into PLL 1\n");
#endif
        return;

    case 0x1c:	/* PLL Programmability 2 */
        s->pll[1] = value & 0xfffc;
#ifdef TSC_VERBOSE
        if (value & ~0xfffc)
            fprintf(stderr, "tsc2102_audio_register_write: "
                            "wrong value written into PLL 2\n");
#endif
        return;

    case 0x1d:	/* Audio Control 4 */
        s->softstep = !(value & 0x4000);
#ifdef TSC_VERBOSE
        if (value & ~0x4000)
            fprintf(stderr, "tsc2102_audio_register_write: "
                            "wrong value written into Audio 4\n");
#endif
        return;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "tsc2102_audio_register_write: "
                                       "no such register: 0x%02x\n", reg);
    }
}

/* This handles most of the chip logic.  */
static void tsc210x_pin_update(TSC210xState *s)
{
    int64_t expires;
    bool pin_state;

    switch (s->pin_func) {
    case 0:
        pin_state = s->pressure;
        break;
    case 1:
        pin_state = !!s->dav;
        break;
    case 2:
    default:
        pin_state = s->pressure && !s->dav;
    }

    if (!s->enabled)
        pin_state = false;

    if (pin_state != s->irq) {
        s->irq = pin_state;
        qemu_set_irq(s->pint, !s->irq);
    }

    switch (s->nextfunction) {
    case TSC_MODE_XY_SCAN:
    case TSC_MODE_XYZ_SCAN:
        if (!s->pressure)
            return;
        break;

    case TSC_MODE_X:
    case TSC_MODE_Y:
    case TSC_MODE_Z:
        if (!s->pressure)
            return;
        /* Fall through */
    case TSC_MODE_BAT1:
    case TSC_MODE_BAT2:
    case TSC_MODE_AUX:
    case TSC_MODE_TEMP1:
    case TSC_MODE_TEMP2:
        if (s->dav)
            s->enabled = false;
        break;

    case TSC_MODE_AUX_SCAN:
    case TSC_MODE_PORT_SCAN:
        break;

    case TSC_MODE_NO_SCAN:
    case TSC_MODE_XX_DRV:
    case TSC_MODE_YY_DRV:
    case TSC_MODE_YX_DRV:
    default:
        return;
    }

    if (!s->enabled || s->busy || s->dav)
        return;

    s->busy = true;
    s->precision = s->nextprecision;
    s->function = s->nextfunction;
    expires = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
        (NANOSECONDS_PER_SECOND >> 10);
    timer_mod(s->timer, expires);
}

static uint16_t tsc210x_read(TSC210xState *s)
{
    uint16_t ret = 0x0000;

    if (!s->command)
        fprintf(stderr, "tsc210x_read: SPI underrun!\n");

    switch (s->page) {
    case TSC_DATA_REGISTERS_PAGE:
        ret = tsc2102_data_register_read(s, s->offset);
        if (!s->dav)
            qemu_irq_raise(s->davint);
        break;
    case TSC_CONTROL_REGISTERS_PAGE:
        ret = tsc2102_control_register_read(s, s->offset);
        break;
    case TSC_AUDIO_REGISTERS_PAGE:
        ret = tsc2102_audio_register_read(s, s->offset);
        break;
    default:
        hw_error("tsc210x_read: wrong memory page\n");
    }

    tsc210x_pin_update(s);

    /* Allow sequential reads.  */
    s->offset ++;
    s->state = false;
    return ret;
}

static void tsc210x_write(TSC210xState *s, uint16_t value)
{
    /*
     * This is a two-state state machine for reading
     * command and data every second time.
     */
    if (!s->state) {
        s->command = (value >> 15) != 0;
        s->page = (value >> 11) & 0x0f;
        s->offset = (value >> 5) & 0x3f;
        s->state = true;
    } else {
        if (s->command)
            fprintf(stderr, "tsc210x_write: SPI overrun!\n");
        else
            switch (s->page) {
            case TSC_DATA_REGISTERS_PAGE:
                tsc2102_data_register_write(s, s->offset, value);
                break;
            case TSC_CONTROL_REGISTERS_PAGE:
                tsc2102_control_register_write(s, s->offset, value);
                break;
            case TSC_AUDIO_REGISTERS_PAGE:
                tsc2102_audio_register_write(s, s->offset, value);
                break;
            default:
                hw_error("tsc210x_write: wrong memory page\n");
            }

        tsc210x_pin_update(s);
        s->state = false;
    }
}

uint32_t tsc210x_txrx(void *opaque, uint32_t value, int len)
{
    TSC210xState *s = opaque;
    uint32_t ret = 0;

    if (len != 16) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad SPI word width %i\n", __func__, len);
        return 0;
    }

    /* TODO: sequential reads etc - how do we make sure the host doesn't
     * unintentionally read out a conversion result from a register while
     * transmitting the command word of the next command?  */
    if (!value || (s->state && s->command))
        ret = tsc210x_read(s);
    if (value || (s->state && !s->command))
        tsc210x_write(s, value);

    return ret;
}

static void tsc210x_timer_tick(void *opaque)
{
    TSC210xState *s = opaque;

    /* Timer ticked -- a set of conversions has been finished.  */

    if (!s->busy)
        return;

    s->busy = false;
    s->dav |= mode_regs[s->function];
    tsc210x_pin_update(s);
    qemu_irq_lower(s->davint);
}

static void tsc210x_touchscreen_event(void *opaque,
                int x, int y, int z, int buttons_state)
{
    TSC210xState *s = opaque;
    int p = s->pressure;

    if (buttons_state) {
        s->x = x;
        s->y = y;
    }
    s->pressure = !!buttons_state;

    /*
     * Note: We would get better responsiveness in the guest by
     * signaling TS events immediately, but for now we simulate
     * the first conversion delay for sake of correctness.
     */
    if (p != s->pressure)
        tsc210x_pin_update(s);
}

static void tsc210x_i2s_swallow(TSC210xState *s)
{
    if (s->dac_voice[0])
        tsc210x_out_flush(s, s->codec.out.len);
    else
        s->codec.out.len = 0;
}

static void tsc210x_i2s_set_rate(TSC210xState *s, int in, int out)
{
    s->i2s_tx_rate = out;
    s->i2s_rx_rate = in;
}

static int tsc210x_pre_save(void *opaque)
{
    TSC210xState *s = (TSC210xState *) opaque;
    s->now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    return 0;
}

static int tsc210x_post_load(void *opaque, int version_id)
{
    TSC210xState *s = (TSC210xState *) opaque;
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    if (s->function >= ARRAY_SIZE(mode_regs)) {
        return -EINVAL;
    }
    if (s->nextfunction >= ARRAY_SIZE(mode_regs)) {
        return -EINVAL;
    }
    if (s->precision >= ARRAY_SIZE(resolution)) {
        return -EINVAL;
    }
    if (s->nextprecision >= ARRAY_SIZE(resolution)) {
        return -EINVAL;
    }

    s->volume_change -= s->now;
    s->volume_change += now;
    s->powerdown -= s->now;
    s->powerdown += now;

    s->busy = timer_pending(s->timer);
    qemu_set_irq(s->pint, !s->irq);
    qemu_set_irq(s->davint, !s->dav);

    return 0;
}

static VMStateField vmstatefields_tsc210x[] = {
    VMSTATE_BOOL(enabled, TSC210xState),
    VMSTATE_BOOL(host_mode, TSC210xState),
    VMSTATE_BOOL(irq, TSC210xState),
    VMSTATE_BOOL(command, TSC210xState),
    VMSTATE_BOOL(pressure, TSC210xState),
    VMSTATE_BOOL(softstep, TSC210xState),
    VMSTATE_BOOL(state, TSC210xState),
    VMSTATE_UINT16(dav, TSC210xState),
    VMSTATE_INT32(x, TSC210xState),
    VMSTATE_INT32(y, TSC210xState),
    VMSTATE_UINT8(offset, TSC210xState),
    VMSTATE_UINT8(page, TSC210xState),
    VMSTATE_UINT8(filter, TSC210xState),
    VMSTATE_UINT8(pin_func, TSC210xState),
    VMSTATE_UINT8(ref, TSC210xState),
    VMSTATE_UINT8(timing, TSC210xState),
    VMSTATE_UINT8(noise, TSC210xState),
    VMSTATE_UINT8(function, TSC210xState),
    VMSTATE_UINT8(nextfunction, TSC210xState),
    VMSTATE_UINT8(precision, TSC210xState),
    VMSTATE_UINT8(nextprecision, TSC210xState),
    VMSTATE_UINT16(audio_ctrl1, TSC210xState),
    VMSTATE_UINT16(audio_ctrl2, TSC210xState),
    VMSTATE_UINT16(audio_ctrl3, TSC210xState),
    VMSTATE_UINT16_ARRAY(pll, TSC210xState, 3),
    VMSTATE_UINT16(volume, TSC210xState),
    VMSTATE_UINT16(dac_power, TSC210xState),
    VMSTATE_INT64(volume_change, TSC210xState),
    VMSTATE_INT64(powerdown, TSC210xState),
    VMSTATE_INT64(now, TSC210xState),
    VMSTATE_UINT16_ARRAY(filter_data, TSC210xState, 0x14),
    VMSTATE_TIMER_PTR(timer, TSC210xState),
    VMSTATE_END_OF_LIST()
};

static const VMStateDescription vmstate_tsc2102 = {
    .name = "tsc2102",
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = tsc210x_pre_save,
    .post_load = tsc210x_post_load,
    .fields = vmstatefields_tsc210x,
};

static const VMStateDescription vmstate_tsc2301 = {
    .name = "tsc2301",
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = tsc210x_pre_save,
    .post_load = tsc210x_post_load,
    .fields = vmstatefields_tsc210x,
};

static void tsc210x_init(TSC210xState *s,
                         const char *name,
                         const VMStateDescription *vmsd)
{
    s->tr[0] = 0;
    s->tr[1] = 1;
    s->tr[2] = 1;
    s->tr[3] = 0;
    s->tr[4] = 1;
    s->tr[5] = 0;
    s->tr[6] = 1;
    s->tr[7] = 0;

    s->chip.opaque = s;
    s->chip.send = (void *) tsc210x_write;
    s->chip.receive = (void *) tsc210x_read;

    s->codec.opaque = s;
    s->codec.tx_swallow = (void *) tsc210x_i2s_swallow;
    s->codec.set_rate = (void *) tsc210x_i2s_set_rate;
    s->codec.in.fifo = s->in_fifo;
    s->codec.out.fifo = s->out_fifo;

    tsc210x_reset(s);

    qemu_add_mouse_event_handler(tsc210x_touchscreen_event, s, 1, name);

    if (current_machine->audiodev) {
        s->card.name = g_strdup(current_machine->audiodev);
        s->card.state = audio_state_by_name(s->card.name, &error_fatal);
    }
    AUD_register_card(s->name, &s->card, &error_fatal);

    qemu_register_reset((void *) tsc210x_reset, s);
    vmstate_register(NULL, 0, vmsd, s);
}

uWireSlave *tsc2102_init(qemu_irq pint)
{
    TSC210xState *s;

    s = g_new0(TSC210xState, 1);
    s->x = 160;
    s->y = 160;
    s->pressure = 0;
    s->precision = s->nextprecision = 0;
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, tsc210x_timer_tick, s);
    s->pint = pint;
    s->model = 0x2102;
    s->name = "tsc2102";

    tsc210x_init(s, "QEMU TSC2102-driven Touchscreen", &vmstate_tsc2102);

    return &s->chip;
}

uWireSlave *tsc2301_init(qemu_irq penirq, qemu_irq kbirq, qemu_irq dav)
{
    TSC210xState *s;

    s = g_new0(TSC210xState, 1);
    s->x = 400;
    s->y = 240;
    s->pressure = 0;
    s->precision = s->nextprecision = 0;
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, tsc210x_timer_tick, s);
    s->pint = penirq;
    s->kbint = kbirq;
    s->davint = dav;
    s->model = 0x2301;
    s->name = "tsc2301";

    tsc210x_init(s, "QEMU TSC2301-driven Touchscreen", &vmstate_tsc2301);

    return &s->chip;
}

I2SCodec *tsc210x_codec(uWireSlave *chip)
{
    TSC210xState *s = (TSC210xState *) chip->opaque;

    return &s->codec;
}

/*
 * Use tslib generated calibration data to generate ADC input values
 * from the touchscreen.  Assuming 12-bit precision was used during
 * tslib calibration.
 */
void tsc210x_set_transform(uWireSlave *chip, const MouseTransformInfo *info)
{
    TSC210xState *s = (TSC210xState *) chip->opaque;
#if 0
    int64_t ltr[8];

    ltr[0] = (int64_t) info->a[1] * info->y;
    ltr[1] = (int64_t) info->a[4] * info->x;
    ltr[2] = (int64_t) info->a[1] * info->a[3] -
            (int64_t) info->a[4] * info->a[0];
    ltr[3] = (int64_t) info->a[2] * info->a[4] -
            (int64_t) info->a[5] * info->a[1];
    ltr[4] = (int64_t) info->a[0] * info->y;
    ltr[5] = (int64_t) info->a[3] * info->x;
    ltr[6] = (int64_t) info->a[4] * info->a[0] -
            (int64_t) info->a[1] * info->a[3];
    ltr[7] = (int64_t) info->a[2] * info->a[3] -
            (int64_t) info->a[5] * info->a[0];

    /* Avoid integer overflow */
    s->tr[0] = ltr[0] >> 11;
    s->tr[1] = ltr[1] >> 11;
    s->tr[2] = muldiv64(ltr[2], 1, info->a[6]);
    s->tr[3] = muldiv64(ltr[3], 1 << 4, ltr[2]);
    s->tr[4] = ltr[4] >> 11;
    s->tr[5] = ltr[5] >> 11;
    s->tr[6] = muldiv64(ltr[6], 1, info->a[6]);
    s->tr[7] = muldiv64(ltr[7], 1 << 4, ltr[6]);
#else

    /* This version assumes touchscreen X & Y axis are parallel or
     * perpendicular to LCD's  X & Y axis in some way.  */
    if (abs(info->a[0]) > abs(info->a[1])) {
        s->tr[0] = 0;
        s->tr[1] = -info->a[6] * info->x;
        s->tr[2] = info->a[0];
        s->tr[3] = -info->a[2] / info->a[0];
        s->tr[4] = info->a[6] * info->y;
        s->tr[5] = 0;
        s->tr[6] = info->a[4];
        s->tr[7] = -info->a[5] / info->a[4];
    } else {
        s->tr[0] = info->a[6] * info->y;
        s->tr[1] = 0;
        s->tr[2] = info->a[1];
        s->tr[3] = -info->a[2] / info->a[1];
        s->tr[4] = 0;
        s->tr[5] = -info->a[6] * info->x;
        s->tr[6] = info->a[3];
        s->tr[7] = -info->a[5] / info->a[3];
    }

    s->tr[0] >>= 11;
    s->tr[1] >>= 11;
    s->tr[3] <<= 4;
    s->tr[4] >>= 11;
    s->tr[5] >>= 11;
    s->tr[7] <<= 4;
#endif
}

void tsc210x_key_event(uWireSlave *chip, int key, int down)
{
    TSC210xState *s = (TSC210xState *) chip->opaque;

    if (down)
        s->kb.down |= 1 << key;
    else
        s->kb.down &= ~(1 << key);

    if (down && (s->kb.down & ~s->kb.mask) && !s->kb.intr) {
        s->kb.intr = 1;
        qemu_irq_lower(s->kbint);
    } else if (s->kb.intr && !(s->kb.down & ~s->kb.mask) &&
                    !(s->kb.mode & 1)) {
        s->kb.intr = 0;
        qemu_irq_raise(s->kbint);
    }
}

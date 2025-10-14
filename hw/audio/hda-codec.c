/*
 * Copyright (C) 2010 Red Hat, Inc.
 *
 * written by Gerd Hoffmann <kraxel@redhat.com>
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/qdev-properties.h"
#include "intel-hda.h"
#include "migration/vmstate.h"
#include "qemu/host-utils.h"
#include "qemu/module.h"
#include "intel-hda-defs.h"
#include "qemu/audio.h"
#include "trace.h"
#include "qom/object.h"

/* -------------------------------------------------------------------------- */

typedef struct desc_param {
    uint32_t id;
    uint32_t val;
} desc_param;

typedef struct desc_node {
    uint32_t nid;
    const char *name;
    const desc_param *params;
    uint32_t nparams;
    uint32_t config;
    uint32_t pinctl;
    uint32_t *conn;
    uint32_t stindex;
} desc_node;

typedef struct desc_codec {
    const char *name;
    uint32_t iid;
    const desc_node *nodes;
    uint32_t nnodes;
} desc_codec;

static const desc_param* hda_codec_find_param(const desc_node *node, uint32_t id)
{
    int i;

    for (i = 0; i < node->nparams; i++) {
        if (node->params[i].id == id) {
            return &node->params[i];
        }
    }
    return NULL;
}

static const desc_node* hda_codec_find_node(const desc_codec *codec, uint32_t nid)
{
    int i;

    for (i = 0; i < codec->nnodes; i++) {
        if (codec->nodes[i].nid == nid) {
            return &codec->nodes[i];
        }
    }
    return NULL;
}

static void hda_codec_parse_fmt(uint32_t format, struct audsettings *as)
{
    if (format & AC_FMT_TYPE_NON_PCM) {
        return;
    }

    as->freq = (format & AC_FMT_BASE_44K) ? 44100 : 48000;

    switch ((format & AC_FMT_MULT_MASK) >> AC_FMT_MULT_SHIFT) {
    case 1: as->freq *= 2; break;
    case 2: as->freq *= 3; break;
    case 3: as->freq *= 4; break;
    }

    switch ((format & AC_FMT_DIV_MASK) >> AC_FMT_DIV_SHIFT) {
    case 1: as->freq /= 2; break;
    case 2: as->freq /= 3; break;
    case 3: as->freq /= 4; break;
    case 4: as->freq /= 5; break;
    case 5: as->freq /= 6; break;
    case 6: as->freq /= 7; break;
    case 7: as->freq /= 8; break;
    }

    switch (format & AC_FMT_BITS_MASK) {
    case AC_FMT_BITS_8:  as->fmt = AUDIO_FORMAT_S8;  break;
    case AC_FMT_BITS_16: as->fmt = AUDIO_FORMAT_S16; break;
    case AC_FMT_BITS_32: as->fmt = AUDIO_FORMAT_S32; break;
    }

    as->nchannels = ((format & AC_FMT_CHAN_MASK) >> AC_FMT_CHAN_SHIFT) + 1;
}

/* -------------------------------------------------------------------------- */
/*
 * HDA codec descriptions
 */

/* some defines */

#define QEMU_HDA_ID_VENDOR  0x1af4
#define QEMU_HDA_PCM_FORMATS (AC_SUPPCM_BITS_16 |       \
                              0x1fc /* 16 -> 96 kHz */)
#define QEMU_HDA_AMP_NONE    (0)
#define QEMU_HDA_AMP_STEPS   0x4a

#define   PARAM mixemu
#define   HDA_MIXER
#include "hda-codec-common.h"

#define   PARAM nomixemu
#include  "hda-codec-common.h"

#define HDA_TIMER_TICKS (SCALE_MS)
#define B_SIZE sizeof(st->buf)
#define B_MASK (sizeof(st->buf) - 1)

/* -------------------------------------------------------------------------- */

static const char *fmt2name[] = {
    [ AUDIO_FORMAT_U8  ] = "PCM-U8",
    [ AUDIO_FORMAT_S8  ] = "PCM-S8",
    [ AUDIO_FORMAT_U16 ] = "PCM-U16",
    [ AUDIO_FORMAT_S16 ] = "PCM-S16",
    [ AUDIO_FORMAT_U32 ] = "PCM-U32",
    [ AUDIO_FORMAT_S32 ] = "PCM-S32",
};

#define TYPE_HDA_AUDIO "hda-audio"
OBJECT_DECLARE_SIMPLE_TYPE(HDAAudioState, HDA_AUDIO)

typedef struct HDAAudioStream HDAAudioStream;

struct HDAAudioStream {
    HDAAudioState *state;
    const desc_node *node;
    bool output, running;
    uint32_t stream;
    uint32_t channel;
    uint32_t format;
    uint32_t gain_left, gain_right;
    bool mute_left, mute_right;
    struct audsettings as;
    union {
        SWVoiceIn *in;
        SWVoiceOut *out;
    } voice;
    uint8_t compat_buf[HDA_BUFFER_SIZE];
    uint32_t compat_bpos;
    uint8_t buf[8192]; /* size must be power of two */
    int64_t rpos;
    int64_t wpos;
    QEMUTimer *buft;
    int64_t buft_start;
};

struct HDAAudioState {
    HDACodecDevice hda;
    const char *name;

    AudioBackend *audio_be;
    const desc_codec *desc;
    HDAAudioStream st[4];
    bool running_compat[16];
    bool running_real[2 * 16];

    /* properties */
    uint32_t debug;
    bool     mixer;
    bool     use_timer;
};

static inline uint32_t hda_bytes_per_second(HDAAudioStream *st)
{
    return 2 * (uint32_t)st->as.nchannels * (uint32_t)st->as.freq;
}

static inline void hda_timer_sync_adjust(HDAAudioStream *st, int64_t target_pos)
{
    int64_t limit = B_SIZE / 8;
    int64_t corr = 0;

    if (target_pos > limit) {
        corr = HDA_TIMER_TICKS;
    }
    if (target_pos < -limit) {
        corr = -HDA_TIMER_TICKS;
    }
    if (target_pos < -(2 * limit)) {
        corr = -(4 * HDA_TIMER_TICKS);
    }
    if (corr == 0) {
        return;
    }

    trace_hda_audio_adjust(st->node->name, target_pos);
    st->buft_start += corr;
}

static void hda_audio_input_timer(void *opaque)
{
    HDAAudioStream *st = opaque;

    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    int64_t uptime = now - st->buft_start;
    int64_t wpos = st->wpos;
    int64_t rpos = st->rpos;
    int64_t wanted_rpos;

    if (uptime <= 0) {
        /* wanted_rpos <= 0 */
        goto out_timer;
    }

    wanted_rpos = muldiv64(uptime, hda_bytes_per_second(st),
                           NANOSECONDS_PER_SECOND);
    wanted_rpos &= -4; /* IMPORTANT! clip to frames */

    if (wanted_rpos <= rpos) {
        /* we already transmitted the data */
        goto out_timer;
    }

    int64_t to_transfer = MIN(wpos - rpos, wanted_rpos - rpos);
    while (to_transfer) {
        uint32_t start = (rpos & B_MASK);
        uint32_t chunk = MIN(B_SIZE - start, to_transfer);
        int rc = hda_codec_xfer(
                &st->state->hda, st->stream, false, st->buf + start, chunk);
        if (!rc) {
            break;
        }
        rpos += chunk;
        to_transfer -= chunk;
        st->rpos += chunk;
    }

out_timer:

    if (st->running) {
        timer_mod_anticipate_ns(st->buft, now + HDA_TIMER_TICKS);
    }
}

static void hda_audio_input_cb(void *opaque, int avail)
{
    HDAAudioStream *st = opaque;

    int64_t wpos = st->wpos;
    int64_t rpos = st->rpos;

    int64_t to_transfer = MIN(B_SIZE - (wpos - rpos), avail);

    while (to_transfer) {
        uint32_t start = (uint32_t) (wpos & B_MASK);
        uint32_t chunk = (uint32_t) MIN(B_SIZE - start, to_transfer);
        uint32_t read = AUD_read(st->voice.in, st->buf + start, chunk);
        wpos += read;
        to_transfer -= read;
        st->wpos += read;
        if (chunk != read) {
            break;
        }
    }

    hda_timer_sync_adjust(st, -((wpos - rpos) - (B_SIZE >> 1)));
}

static void hda_audio_output_timer(void *opaque)
{
    HDAAudioStream *st = opaque;

    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    int64_t uptime = now - st->buft_start;
    int64_t wpos = st->wpos;
    int64_t rpos = st->rpos;
    int64_t wanted_wpos;

    if (uptime <= 0) {
        /* wanted_wpos <= 0 */
        goto out_timer;
    }

    wanted_wpos = muldiv64(uptime, hda_bytes_per_second(st),
                           NANOSECONDS_PER_SECOND);
    wanted_wpos &= -4; /* IMPORTANT! clip to frames */

    if (wanted_wpos <= wpos) {
        /* we already received the data */
        goto out_timer;
    }

    int64_t to_transfer = MIN(B_SIZE - (wpos - rpos), wanted_wpos - wpos);
    while (to_transfer) {
        uint32_t start = (wpos & B_MASK);
        uint32_t chunk = MIN(B_SIZE - start, to_transfer);
        int rc = hda_codec_xfer(
                &st->state->hda, st->stream, true, st->buf + start, chunk);
        if (!rc) {
            break;
        }
        wpos += chunk;
        to_transfer -= chunk;
        st->wpos += chunk;
    }

out_timer:

    if (st->running) {
        timer_mod_anticipate_ns(st->buft, now + HDA_TIMER_TICKS);
    }
}

static void hda_audio_output_cb(void *opaque, int avail)
{
    HDAAudioStream *st = opaque;

    int64_t wpos = st->wpos;
    int64_t rpos = st->rpos;

    int64_t to_transfer = MIN(wpos - rpos, avail);

    if (wpos - rpos == B_SIZE) {
        /* drop buffer, reset timer adjust */
        st->rpos = 0;
        st->wpos = 0;
        st->buft_start = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        trace_hda_audio_overrun(st->node->name);
        return;
    }

    while (to_transfer) {
        uint32_t start = (uint32_t) (rpos & B_MASK);
        uint32_t chunk = (uint32_t) MIN(B_SIZE - start, to_transfer);
        uint32_t written = AUD_write(st->voice.out, st->buf + start, chunk);
        rpos += written;
        to_transfer -= written;
        st->rpos += written;
        if (chunk != written) {
            break;
        }
    }

    hda_timer_sync_adjust(st, (wpos - rpos) - (B_SIZE >> 1));
}

static void hda_audio_compat_input_cb(void *opaque, int avail)
{
    HDAAudioStream *st = opaque;
    int recv = 0;
    int len;
    bool rc;

    while (avail - recv >= sizeof(st->compat_buf)) {
        if (st->compat_bpos != sizeof(st->compat_buf)) {
            len = AUD_read(st->voice.in, st->compat_buf + st->compat_bpos,
                           sizeof(st->compat_buf) - st->compat_bpos);
            st->compat_bpos += len;
            recv += len;
            if (st->compat_bpos != sizeof(st->compat_buf)) {
                break;
            }
        }
        rc = hda_codec_xfer(&st->state->hda, st->stream, false,
                            st->compat_buf, sizeof(st->compat_buf));
        if (!rc) {
            break;
        }
        st->compat_bpos = 0;
    }
}

static void hda_audio_compat_output_cb(void *opaque, int avail)
{
    HDAAudioStream *st = opaque;
    int sent = 0;
    int len;
    bool rc;

    while (avail - sent >= sizeof(st->compat_buf)) {
        if (st->compat_bpos == sizeof(st->compat_buf)) {
            rc = hda_codec_xfer(&st->state->hda, st->stream, true,
                                st->compat_buf, sizeof(st->compat_buf));
            if (!rc) {
                break;
            }
            st->compat_bpos = 0;
        }
        len = AUD_write(st->voice.out, st->compat_buf + st->compat_bpos,
                        sizeof(st->compat_buf) - st->compat_bpos);
        st->compat_bpos += len;
        sent += len;
        if (st->compat_bpos != sizeof(st->compat_buf)) {
            break;
        }
    }
}

static void hda_audio_set_running(HDAAudioStream *st, bool running)
{
    if (st->node == NULL) {
        return;
    }
    if (st->running == running) {
        return;
    }
    st->running = running;
    trace_hda_audio_running(st->node->name, st->stream, st->running);
    if (st->state->use_timer) {
        if (running) {
            int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
            st->rpos = 0;
            st->wpos = 0;
            st->buft_start = now;
            timer_mod_anticipate_ns(st->buft, now + HDA_TIMER_TICKS);
        } else {
            timer_del(st->buft);
        }
    }
    if (st->output) {
        AUD_set_active_out(st->voice.out, st->running);
    } else {
        AUD_set_active_in(st->voice.in, st->running);
    }
}

static void hda_audio_set_amp(HDAAudioStream *st)
{
    bool muted;
    uint32_t left, right;

    if (st->node == NULL) {
        return;
    }

    muted = st->mute_left && st->mute_right;
    left  = st->mute_left  ? 0 : st->gain_left;
    right = st->mute_right ? 0 : st->gain_right;

    left = left * 255 / QEMU_HDA_AMP_STEPS;
    right = right * 255 / QEMU_HDA_AMP_STEPS;

    if (!st->state->mixer) {
        return;
    }
    if (st->output) {
        AUD_set_volume_out_lr(st->voice.out, muted, left, right);
    } else {
        AUD_set_volume_in_lr(st->voice.in, muted, left, right);
    }
}

static void hda_audio_setup(HDAAudioStream *st)
{
    bool use_timer = st->state->use_timer;
    audio_callback_fn cb;

    if (st->node == NULL) {
        return;
    }

    trace_hda_audio_format(st->node->name, st->as.nchannels,
                           fmt2name[st->as.fmt], st->as.freq);

    if (st->output) {
        if (use_timer) {
            cb = hda_audio_output_cb;
            timer_del(st->buft);
        } else {
            cb = hda_audio_compat_output_cb;
        }
        st->voice.out = AUD_open_out(st->state->audio_be, st->voice.out,
                                     st->node->name, st, cb, &st->as);
    } else {
        if (use_timer) {
            cb = hda_audio_input_cb;
            timer_del(st->buft);
        } else {
            cb = hda_audio_compat_input_cb;
        }
        st->voice.in = AUD_open_in(st->state->audio_be, st->voice.in,
                                   st->node->name, st, cb, &st->as);
    }
}

static void hda_audio_command(HDACodecDevice *hda, uint32_t nid, uint32_t data)
{
    HDAAudioState *a = HDA_AUDIO(hda);
    HDAAudioStream *st;
    const desc_node *node = NULL;
    const desc_param *param;
    uint32_t verb, payload, response, count, shift;

    if ((data & 0x70000) == 0x70000) {
        /* 12/8 id/payload */
        verb = (data >> 8) & 0xfff;
        payload = data & 0x00ff;
    } else {
        /* 4/16 id/payload */
        verb = (data >> 8) & 0xf00;
        payload = data & 0xffff;
    }

    node = hda_codec_find_node(a->desc, nid);
    if (node == NULL) {
        goto fail;
    }
    dprint(a, 2, "%s: nid %d (%s), verb 0x%x, payload 0x%x\n",
           __func__, nid, node->name, verb, payload);

    switch (verb) {
    /* all nodes */
    case AC_VERB_PARAMETERS:
        param = hda_codec_find_param(node, payload);
        if (param == NULL) {
            goto fail;
        }
        hda_codec_response(hda, true, param->val);
        break;
    case AC_VERB_GET_SUBSYSTEM_ID:
        hda_codec_response(hda, true, a->desc->iid);
        break;

    /* all functions */
    case AC_VERB_GET_CONNECT_LIST:
        param = hda_codec_find_param(node, AC_PAR_CONNLIST_LEN);
        count = param ? param->val : 0;
        response = 0;
        shift = 0;
        while (payload < count && shift < 32) {
            response |= node->conn[payload] << shift;
            payload++;
            shift += 8;
        }
        hda_codec_response(hda, true, response);
        break;

    /* pin widget */
    case AC_VERB_GET_CONFIG_DEFAULT:
        hda_codec_response(hda, true, node->config);
        break;
    case AC_VERB_GET_PIN_WIDGET_CONTROL:
        hda_codec_response(hda, true, node->pinctl);
        break;
    case AC_VERB_SET_PIN_WIDGET_CONTROL:
        if (node->pinctl != payload) {
            dprint(a, 1, "unhandled pin control bit\n");
        }
        hda_codec_response(hda, true, 0);
        break;

    /* audio in/out widget */
    case AC_VERB_SET_CHANNEL_STREAMID:
        st = a->st + node->stindex;
        if (st->node == NULL) {
            goto fail;
        }
        hda_audio_set_running(st, false);
        st->stream = (payload >> 4) & 0x0f;
        st->channel = payload & 0x0f;
        dprint(a, 2, "%s: stream %d, channel %d\n",
               st->node->name, st->stream, st->channel);
        hda_audio_set_running(st, a->running_real[st->output * 16 + st->stream]);
        hda_codec_response(hda, true, 0);
        break;
    case AC_VERB_GET_CONV:
        st = a->st + node->stindex;
        if (st->node == NULL) {
            goto fail;
        }
        response = st->stream << 4 | st->channel;
        hda_codec_response(hda, true, response);
        break;
    case AC_VERB_SET_STREAM_FORMAT:
        st = a->st + node->stindex;
        if (st->node == NULL) {
            goto fail;
        }
        st->format = payload;
        hda_codec_parse_fmt(st->format, &st->as);
        hda_audio_setup(st);
        hda_codec_response(hda, true, 0);
        break;
    case AC_VERB_GET_STREAM_FORMAT:
        st = a->st + node->stindex;
        if (st->node == NULL) {
            goto fail;
        }
        hda_codec_response(hda, true, st->format);
        break;
    case AC_VERB_GET_AMP_GAIN_MUTE:
        st = a->st + node->stindex;
        if (st->node == NULL) {
            goto fail;
        }
        if (payload & AC_AMP_GET_LEFT) {
            response = st->gain_left | (st->mute_left ? AC_AMP_MUTE : 0);
        } else {
            response = st->gain_right | (st->mute_right ? AC_AMP_MUTE : 0);
        }
        hda_codec_response(hda, true, response);
        break;
    case AC_VERB_SET_AMP_GAIN_MUTE:
        st = a->st + node->stindex;
        if (st->node == NULL) {
            goto fail;
        }
        dprint(a, 1, "amp (%s): %s%s%s%s index %d  gain %3d %s\n",
               st->node->name,
               (payload & AC_AMP_SET_OUTPUT) ? "o" : "-",
               (payload & AC_AMP_SET_INPUT)  ? "i" : "-",
               (payload & AC_AMP_SET_LEFT)   ? "l" : "-",
               (payload & AC_AMP_SET_RIGHT)  ? "r" : "-",
               (payload & AC_AMP_SET_INDEX) >> AC_AMP_SET_INDEX_SHIFT,
               (payload & AC_AMP_GAIN),
               (payload & AC_AMP_MUTE) ? "muted" : "");
        if (payload & AC_AMP_SET_LEFT) {
            st->gain_left = payload & AC_AMP_GAIN;
            st->mute_left = payload & AC_AMP_MUTE;
        }
        if (payload & AC_AMP_SET_RIGHT) {
            st->gain_right = payload & AC_AMP_GAIN;
            st->mute_right = payload & AC_AMP_MUTE;
        }
        hda_audio_set_amp(st);
        hda_codec_response(hda, true, 0);
        break;

    /* not supported */
    case AC_VERB_SET_POWER_STATE:
    case AC_VERB_GET_POWER_STATE:
    case AC_VERB_GET_SDI_SELECT:
        hda_codec_response(hda, true, 0);
        break;
    default:
        goto fail;
    }
    return;

fail:
    dprint(a, 1, "%s: not handled: nid %d (%s), verb 0x%x, payload 0x%x\n",
           __func__, nid, node ? node->name : "?", verb, payload);
    hda_codec_response(hda, true, 0);
}

static void hda_audio_stream(HDACodecDevice *hda, uint32_t stnr, bool running, bool output)
{
    HDAAudioState *a = HDA_AUDIO(hda);
    int s;

    a->running_compat[stnr] = running;
    a->running_real[output * 16 + stnr] = running;
    for (s = 0; s < ARRAY_SIZE(a->st); s++) {
        if (a->st[s].node == NULL) {
            continue;
        }
        if (a->st[s].output != output) {
            continue;
        }
        if (a->st[s].stream != stnr) {
            continue;
        }
        hda_audio_set_running(&a->st[s], running);
    }
}

static void hda_audio_init(HDACodecDevice *hda,
                           const struct desc_codec *desc,
                           Error **errp)
{
    HDAAudioState *a = HDA_AUDIO(hda);
    HDAAudioStream *st;
    const desc_node *node;
    const desc_param *param;
    uint32_t i, type;

    if (!AUD_backend_check(&a->audio_be, errp)) {
        return;
    }

    a->desc = desc;
    a->name = object_get_typename(OBJECT(a));
    dprint(a, 1, "%s: cad %d\n", __func__, a->hda.cad);

    for (i = 0; i < a->desc->nnodes; i++) {
        node = a->desc->nodes + i;
        param = hda_codec_find_param(node, AC_PAR_AUDIO_WIDGET_CAP);
        if (param == NULL) {
            continue;
        }
        type = (param->val & AC_WCAP_TYPE) >> AC_WCAP_TYPE_SHIFT;
        switch (type) {
        case AC_WID_AUD_OUT:
        case AC_WID_AUD_IN:
            assert(node->stindex < ARRAY_SIZE(a->st));
            st = a->st + node->stindex;
            st->state = a;
            st->node = node;
            if (type == AC_WID_AUD_OUT) {
                /* unmute output by default */
                st->gain_left = QEMU_HDA_AMP_STEPS;
                st->gain_right = QEMU_HDA_AMP_STEPS;
                st->compat_bpos = sizeof(st->compat_buf);
                st->output = true;
                st->buft = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                        hda_audio_output_timer, st);
            } else {
                st->output = false;
                st->buft = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                        hda_audio_input_timer, st);
            }
            st->format = AC_FMT_TYPE_PCM | AC_FMT_BITS_16 |
                (1 << AC_FMT_CHAN_SHIFT);
            hda_codec_parse_fmt(st->format, &st->as);
            hda_audio_setup(st);
            break;
        }
    }
}

static void hda_audio_exit(HDACodecDevice *hda)
{
    HDAAudioState *a = HDA_AUDIO(hda);
    HDAAudioStream *st;
    int i;

    dprint(a, 1, "%s\n", __func__);
    for (i = 0; i < ARRAY_SIZE(a->st); i++) {
        st = a->st + i;
        if (st->node == NULL) {
            continue;
        }
        timer_free(st->buft);
        if (st->output) {
            AUD_close_out(a->audio_be, st->voice.out);
        } else {
            AUD_close_in(a->audio_be, st->voice.in);
        }
    }
}

static int hda_audio_post_load(void *opaque, int version)
{
    HDAAudioState *a = opaque;
    HDAAudioStream *st;
    int i;

    dprint(a, 1, "%s\n", __func__);
    if (version == 1) {
        /* assume running_compat[] is for output streams */
        for (i = 0; i < ARRAY_SIZE(a->running_compat); i++)
            a->running_real[16 + i] = a->running_compat[i];
    }

    for (i = 0; i < ARRAY_SIZE(a->st); i++) {
        st = a->st + i;
        if (st->node == NULL)
            continue;
        hda_codec_parse_fmt(st->format, &st->as);
        hda_audio_setup(st);
        hda_audio_set_amp(st);
        hda_audio_set_running(st, a->running_real[st->output * 16 + st->stream]);
    }
    return 0;
}

static void hda_audio_reset(DeviceState *dev)
{
    HDAAudioState *a = HDA_AUDIO(dev);
    HDAAudioStream *st;
    int i;

    dprint(a, 1, "%s\n", __func__);
    for (i = 0; i < ARRAY_SIZE(a->st); i++) {
        st = a->st + i;
        if (st->node != NULL) {
            hda_audio_set_running(st, false);
        }
    }
}

static bool vmstate_hda_audio_stream_buf_needed(void *opaque)
{
    HDAAudioStream *st = opaque;
    return st->state && st->state->use_timer;
}

static const VMStateDescription vmstate_hda_audio_stream_buf = {
    .name = "hda-audio-stream/buffer",
    .version_id = 1,
    .needed = vmstate_hda_audio_stream_buf_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_BUFFER(buf, HDAAudioStream),
        VMSTATE_INT64(rpos, HDAAudioStream),
        VMSTATE_INT64(wpos, HDAAudioStream),
        VMSTATE_TIMER_PTR(buft, HDAAudioStream),
        VMSTATE_INT64(buft_start, HDAAudioStream),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_hda_audio_stream = {
    .name = "hda-audio-stream",
    .version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(stream, HDAAudioStream),
        VMSTATE_UINT32(channel, HDAAudioStream),
        VMSTATE_UINT32(format, HDAAudioStream),
        VMSTATE_UINT32(gain_left, HDAAudioStream),
        VMSTATE_UINT32(gain_right, HDAAudioStream),
        VMSTATE_BOOL(mute_left, HDAAudioStream),
        VMSTATE_BOOL(mute_right, HDAAudioStream),
        VMSTATE_UINT32(compat_bpos, HDAAudioStream),
        VMSTATE_BUFFER(compat_buf, HDAAudioStream),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * const []) {
        &vmstate_hda_audio_stream_buf,
        NULL
    }
};

static const VMStateDescription vmstate_hda_audio = {
    .name = "hda-audio",
    .version_id = 2,
    .post_load = hda_audio_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(st, HDAAudioState, 4, 0,
                             vmstate_hda_audio_stream,
                             HDAAudioStream),
        VMSTATE_BOOL_ARRAY(running_compat, HDAAudioState, 16),
        VMSTATE_BOOL_ARRAY_V(running_real, HDAAudioState, 2 * 16, 2),
        VMSTATE_END_OF_LIST()
    }
};

static const Property hda_audio_properties[] = {
    DEFINE_AUDIO_PROPERTIES(HDAAudioState, audio_be),
    DEFINE_PROP_UINT32("debug", HDAAudioState, debug,   0),
    DEFINE_PROP_BOOL("mixer", HDAAudioState, mixer,  true),
    DEFINE_PROP_BOOL("use-timer", HDAAudioState, use_timer,  true),
};

static void hda_audio_init_output(HDACodecDevice *hda, Error **errp)
{
    HDAAudioState *a = HDA_AUDIO(hda);
    const struct desc_codec *desc = &output_mixemu;

    if (!a->mixer) {
        desc = &output_nomixemu;
    }

    hda_audio_init(hda, desc, errp);
}

static void hda_audio_init_duplex(HDACodecDevice *hda, Error **errp)
{
    HDAAudioState *a = HDA_AUDIO(hda);
    const struct desc_codec *desc = &duplex_mixemu;

    if (!a->mixer) {
        desc = &duplex_nomixemu;
    }

    hda_audio_init(hda, desc, errp);
}

static void hda_audio_init_micro(HDACodecDevice *hda, Error **errp)
{
    HDAAudioState *a = HDA_AUDIO(hda);
    const struct desc_codec *desc = &micro_mixemu;

    if (!a->mixer) {
        desc = &micro_nomixemu;
    }

    hda_audio_init(hda, desc, errp);
}

static void hda_audio_base_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    HDACodecDeviceClass *k = HDA_CODEC_DEVICE_CLASS(klass);

    k->exit = hda_audio_exit;
    k->command = hda_audio_command;
    k->stream = hda_audio_stream;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    device_class_set_legacy_reset(dc, hda_audio_reset);
    dc->vmsd = &vmstate_hda_audio;
    device_class_set_props(dc, hda_audio_properties);
}

static const TypeInfo hda_audio_info = {
    .name          = TYPE_HDA_AUDIO,
    .parent        = TYPE_HDA_CODEC_DEVICE,
    .instance_size = sizeof(HDAAudioState),
    .class_init    = hda_audio_base_class_init,
    .abstract      = true,
};

static void hda_audio_output_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    HDACodecDeviceClass *k = HDA_CODEC_DEVICE_CLASS(klass);

    k->init = hda_audio_init_output;
    dc->desc = "HDA Audio Codec, output-only (line-out)";
}

static const TypeInfo hda_audio_output_info = {
    .name          = "hda-output",
    .parent        = TYPE_HDA_AUDIO,
    .class_init    = hda_audio_output_class_init,
};

static void hda_audio_duplex_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    HDACodecDeviceClass *k = HDA_CODEC_DEVICE_CLASS(klass);

    k->init = hda_audio_init_duplex;
    dc->desc = "HDA Audio Codec, duplex (line-out, line-in)";
}

static const TypeInfo hda_audio_duplex_info = {
    .name          = "hda-duplex",
    .parent        = TYPE_HDA_AUDIO,
    .class_init    = hda_audio_duplex_class_init,
};

static void hda_audio_micro_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    HDACodecDeviceClass *k = HDA_CODEC_DEVICE_CLASS(klass);

    k->init = hda_audio_init_micro;
    dc->desc = "HDA Audio Codec, duplex (speaker, microphone)";
}

static const TypeInfo hda_audio_micro_info = {
    .name          = "hda-micro",
    .parent        = TYPE_HDA_AUDIO,
    .class_init    = hda_audio_micro_class_init,
};

static void hda_audio_register_types(void)
{
    type_register_static(&hda_audio_info);
    type_register_static(&hda_audio_output_info);
    type_register_static(&hda_audio_duplex_info);
    type_register_static(&hda_audio_micro_info);
}

type_init(hda_audio_register_types)

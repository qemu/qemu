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

#include "hw.h"
#include "pci.h"
#include "intel-hda.h"
#include "intel-hda-defs.h"
#include "audio/audio.h"

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
    case AC_FMT_BITS_8:  as->fmt = AUD_FMT_S8;  break;
    case AC_FMT_BITS_16: as->fmt = AUD_FMT_S16; break;
    case AC_FMT_BITS_32: as->fmt = AUD_FMT_S32; break;
    }

    as->nchannels = ((format & AC_FMT_CHAN_MASK) >> AC_FMT_CHAN_SHIFT) + 1;
}

/* -------------------------------------------------------------------------- */
/*
 * HDA codec descriptions
 */

/* some defines */

#define QEMU_HDA_ID_VENDOR  0x1af4
#define QEMU_HDA_ID_OUTPUT  ((QEMU_HDA_ID_VENDOR << 16) | 0x10)
#define QEMU_HDA_ID_DUPLEX  ((QEMU_HDA_ID_VENDOR << 16) | 0x20)

#define QEMU_HDA_PCM_FORMATS (AC_SUPPCM_BITS_16 |       \
                              0x1fc /* 16 -> 96 kHz */)
#define QEMU_HDA_AMP_NONE    (0)
#define QEMU_HDA_AMP_STEPS   0x4a

#ifdef CONFIG_MIXEMU
#define QEMU_HDA_AMP_CAPS                                               \
    (AC_AMPCAP_MUTE |                                                   \
     (QEMU_HDA_AMP_STEPS << AC_AMPCAP_OFFSET_SHIFT)    |                \
     (QEMU_HDA_AMP_STEPS << AC_AMPCAP_NUM_STEPS_SHIFT) |                \
     (3                  << AC_AMPCAP_STEP_SIZE_SHIFT))
#else
#define QEMU_HDA_AMP_CAPS    QEMU_HDA_AMP_NONE
#endif

/* common: audio output widget */
static const desc_param common_params_audio_dac[] = {
    {
        .id  = AC_PAR_AUDIO_WIDGET_CAP,
        .val = ((AC_WID_AUD_OUT << AC_WCAP_TYPE_SHIFT) |
                AC_WCAP_FORMAT_OVRD |
                AC_WCAP_AMP_OVRD |
                AC_WCAP_OUT_AMP |
                AC_WCAP_STEREO),
    },{
        .id  = AC_PAR_PCM,
        .val = QEMU_HDA_PCM_FORMATS,
    },{
        .id  = AC_PAR_STREAM,
        .val = AC_SUPFMT_PCM,
    },{
        .id  = AC_PAR_AMP_IN_CAP,
        .val = QEMU_HDA_AMP_NONE,
    },{
        .id  = AC_PAR_AMP_OUT_CAP,
        .val = QEMU_HDA_AMP_CAPS,
    },
};

/* common: pin widget (line-out) */
static const desc_param common_params_audio_lineout[] = {
    {
        .id  = AC_PAR_AUDIO_WIDGET_CAP,
        .val = ((AC_WID_PIN << AC_WCAP_TYPE_SHIFT) |
                AC_WCAP_CONN_LIST |
                AC_WCAP_STEREO),
    },{
        .id  = AC_PAR_PIN_CAP,
        .val = AC_PINCAP_OUT,
    },{
        .id  = AC_PAR_CONNLIST_LEN,
        .val = 1,
    },{
        .id  = AC_PAR_AMP_IN_CAP,
        .val = QEMU_HDA_AMP_NONE,
    },{
        .id  = AC_PAR_AMP_OUT_CAP,
        .val = QEMU_HDA_AMP_NONE,
    },
};

/* output: root node */
static const desc_param output_params_root[] = {
    {
        .id  = AC_PAR_VENDOR_ID,
        .val = QEMU_HDA_ID_OUTPUT,
    },{
        .id  = AC_PAR_SUBSYSTEM_ID,
        .val = QEMU_HDA_ID_OUTPUT,
    },{
        .id  = AC_PAR_REV_ID,
        .val = 0x00100101,
    },{
        .id  = AC_PAR_NODE_COUNT,
        .val = 0x00010001,
    },
};

/* output: audio function */
static const desc_param output_params_audio_func[] = {
    {
        .id  = AC_PAR_FUNCTION_TYPE,
        .val = AC_GRP_AUDIO_FUNCTION,
    },{
        .id  = AC_PAR_SUBSYSTEM_ID,
        .val = QEMU_HDA_ID_OUTPUT,
    },{
        .id  = AC_PAR_NODE_COUNT,
        .val = 0x00020002,
    },{
        .id  = AC_PAR_PCM,
        .val = QEMU_HDA_PCM_FORMATS,
    },{
        .id  = AC_PAR_STREAM,
        .val = AC_SUPFMT_PCM,
    },{
        .id  = AC_PAR_AMP_IN_CAP,
        .val = QEMU_HDA_AMP_NONE,
    },{
        .id  = AC_PAR_AMP_OUT_CAP,
        .val = QEMU_HDA_AMP_NONE,
    },{
        .id  = AC_PAR_GPIO_CAP,
        .val = 0,
    },{
        .id  = AC_PAR_AUDIO_FG_CAP,
        .val = 0x00000808,
    },{
        .id  = AC_PAR_POWER_STATE,
        .val = 0,
    },
};

/* output: nodes */
static const desc_node output_nodes[] = {
    {
        .nid     = AC_NODE_ROOT,
        .name    = "root",
        .params  = output_params_root,
        .nparams = ARRAY_SIZE(output_params_root),
    },{
        .nid     = 1,
        .name    = "func",
        .params  = output_params_audio_func,
        .nparams = ARRAY_SIZE(output_params_audio_func),
    },{
        .nid     = 2,
        .name    = "dac",
        .params  = common_params_audio_dac,
        .nparams = ARRAY_SIZE(common_params_audio_dac),
        .stindex = 0,
    },{
        .nid     = 3,
        .name    = "out",
        .params  = common_params_audio_lineout,
        .nparams = ARRAY_SIZE(common_params_audio_lineout),
        .config  = ((AC_JACK_PORT_COMPLEX << AC_DEFCFG_PORT_CONN_SHIFT) |
                    (AC_JACK_LINE_OUT     << AC_DEFCFG_DEVICE_SHIFT)    |
                    (AC_JACK_CONN_UNKNOWN << AC_DEFCFG_CONN_TYPE_SHIFT) |
                    (AC_JACK_COLOR_GREEN  << AC_DEFCFG_COLOR_SHIFT)     |
                    0x10),
        .pinctl  = AC_PINCTL_OUT_EN,
        .conn    = (uint32_t[]) { 2 },
    }
};

/* output: codec */
static const desc_codec output = {
    .name   = "output",
    .iid    = QEMU_HDA_ID_OUTPUT,
    .nodes  = output_nodes,
    .nnodes = ARRAY_SIZE(output_nodes),
};

/* duplex: root node */
static const desc_param duplex_params_root[] = {
    {
        .id  = AC_PAR_VENDOR_ID,
        .val = QEMU_HDA_ID_DUPLEX,
    },{
        .id  = AC_PAR_SUBSYSTEM_ID,
        .val = QEMU_HDA_ID_DUPLEX,
    },{
        .id  = AC_PAR_REV_ID,
        .val = 0x00100101,
    },{
        .id  = AC_PAR_NODE_COUNT,
        .val = 0x00010001,
    },
};

/* duplex: audio input widget */
static const desc_param duplex_params_audio_adc[] = {
    {
        .id  = AC_PAR_AUDIO_WIDGET_CAP,
        .val = ((AC_WID_AUD_IN << AC_WCAP_TYPE_SHIFT) |
                AC_WCAP_CONN_LIST |
                AC_WCAP_FORMAT_OVRD |
                AC_WCAP_AMP_OVRD |
                AC_WCAP_IN_AMP |
                AC_WCAP_STEREO),
    },{
        .id  = AC_PAR_CONNLIST_LEN,
        .val = 1,
    },{
        .id  = AC_PAR_PCM,
        .val = QEMU_HDA_PCM_FORMATS,
    },{
        .id  = AC_PAR_STREAM,
        .val = AC_SUPFMT_PCM,
    },{
        .id  = AC_PAR_AMP_IN_CAP,
        .val = QEMU_HDA_AMP_CAPS,
    },{
        .id  = AC_PAR_AMP_OUT_CAP,
        .val = QEMU_HDA_AMP_NONE,
    },
};

/* duplex: pin widget (line-in) */
static const desc_param duplex_params_audio_linein[] = {
    {
        .id  = AC_PAR_AUDIO_WIDGET_CAP,
        .val = ((AC_WID_PIN << AC_WCAP_TYPE_SHIFT) |
                AC_WCAP_STEREO),
    },{
        .id  = AC_PAR_PIN_CAP,
        .val = AC_PINCAP_IN,
    },{
        .id  = AC_PAR_AMP_IN_CAP,
        .val = QEMU_HDA_AMP_NONE,
    },{
        .id  = AC_PAR_AMP_OUT_CAP,
        .val = QEMU_HDA_AMP_NONE,
    },
};

/* duplex: audio function */
static const desc_param duplex_params_audio_func[] = {
    {
        .id  = AC_PAR_FUNCTION_TYPE,
        .val = AC_GRP_AUDIO_FUNCTION,
    },{
        .id  = AC_PAR_SUBSYSTEM_ID,
        .val = QEMU_HDA_ID_DUPLEX,
    },{
        .id  = AC_PAR_NODE_COUNT,
        .val = 0x00020004,
    },{
        .id  = AC_PAR_PCM,
        .val = QEMU_HDA_PCM_FORMATS,
    },{
        .id  = AC_PAR_STREAM,
        .val = AC_SUPFMT_PCM,
    },{
        .id  = AC_PAR_AMP_IN_CAP,
        .val = QEMU_HDA_AMP_NONE,
    },{
        .id  = AC_PAR_AMP_OUT_CAP,
        .val = QEMU_HDA_AMP_NONE,
    },{
        .id  = AC_PAR_GPIO_CAP,
        .val = 0,
    },{
        .id  = AC_PAR_AUDIO_FG_CAP,
        .val = 0x00000808,
    },{
        .id  = AC_PAR_POWER_STATE,
        .val = 0,
    },
};

/* duplex: nodes */
static const desc_node duplex_nodes[] = {
    {
        .nid     = AC_NODE_ROOT,
        .name    = "root",
        .params  = duplex_params_root,
        .nparams = ARRAY_SIZE(duplex_params_root),
    },{
        .nid     = 1,
        .name    = "func",
        .params  = duplex_params_audio_func,
        .nparams = ARRAY_SIZE(duplex_params_audio_func),
    },{
        .nid     = 2,
        .name    = "dac",
        .params  = common_params_audio_dac,
        .nparams = ARRAY_SIZE(common_params_audio_dac),
        .stindex = 0,
    },{
        .nid     = 3,
        .name    = "out",
        .params  = common_params_audio_lineout,
        .nparams = ARRAY_SIZE(common_params_audio_lineout),
        .config  = ((AC_JACK_PORT_COMPLEX << AC_DEFCFG_PORT_CONN_SHIFT) |
                    (AC_JACK_LINE_OUT     << AC_DEFCFG_DEVICE_SHIFT)    |
                    (AC_JACK_CONN_UNKNOWN << AC_DEFCFG_CONN_TYPE_SHIFT) |
                    (AC_JACK_COLOR_GREEN  << AC_DEFCFG_COLOR_SHIFT)     |
                    0x10),
        .pinctl  = AC_PINCTL_OUT_EN,
        .conn    = (uint32_t[]) { 2 },
    },{
        .nid     = 4,
        .name    = "adc",
        .params  = duplex_params_audio_adc,
        .nparams = ARRAY_SIZE(duplex_params_audio_adc),
        .stindex = 1,
        .conn    = (uint32_t[]) { 5 },
    },{
        .nid     = 5,
        .name    = "in",
        .params  = duplex_params_audio_linein,
        .nparams = ARRAY_SIZE(duplex_params_audio_linein),
        .config  = ((AC_JACK_PORT_COMPLEX << AC_DEFCFG_PORT_CONN_SHIFT) |
                    (AC_JACK_LINE_IN      << AC_DEFCFG_DEVICE_SHIFT)    |
                    (AC_JACK_CONN_UNKNOWN << AC_DEFCFG_CONN_TYPE_SHIFT) |
                    (AC_JACK_COLOR_RED    << AC_DEFCFG_COLOR_SHIFT)     |
                    0x20),
        .pinctl  = AC_PINCTL_IN_EN,
    }
};

/* duplex: codec */
static const desc_codec duplex = {
    .name   = "duplex",
    .iid    = QEMU_HDA_ID_DUPLEX,
    .nodes  = duplex_nodes,
    .nnodes = ARRAY_SIZE(duplex_nodes),
};

/* -------------------------------------------------------------------------- */

static const char *fmt2name[] = {
    [ AUD_FMT_U8  ] = "PCM-U8",
    [ AUD_FMT_S8  ] = "PCM-S8",
    [ AUD_FMT_U16 ] = "PCM-U16",
    [ AUD_FMT_S16 ] = "PCM-S16",
    [ AUD_FMT_U32 ] = "PCM-U32",
    [ AUD_FMT_S32 ] = "PCM-S32",
};

typedef struct HDAAudioState HDAAudioState;
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
    uint8_t buf[HDA_BUFFER_SIZE];
    uint32_t bpos;
};

struct HDAAudioState {
    HDACodecDevice hda;
    const char *name;

    QEMUSoundCard card;
    const desc_codec *desc;
    HDAAudioStream st[4];
    bool running[16];

    /* properties */
    uint32_t debug;
};

static void hda_audio_input_cb(void *opaque, int avail)
{
    HDAAudioStream *st = opaque;
    int recv = 0;
    int len;
    bool rc;

    while (avail - recv >= sizeof(st->buf)) {
        if (st->bpos != sizeof(st->buf)) {
            len = AUD_read(st->voice.in, st->buf + st->bpos,
                           sizeof(st->buf) - st->bpos);
            st->bpos += len;
            recv += len;
            if (st->bpos != sizeof(st->buf)) {
                break;
            }
        }
        rc = hda_codec_xfer(&st->state->hda, st->stream, false,
                            st->buf, sizeof(st->buf));
        if (!rc) {
            break;
        }
        st->bpos = 0;
    }
}

static void hda_audio_output_cb(void *opaque, int avail)
{
    HDAAudioStream *st = opaque;
    int sent = 0;
    int len;
    bool rc;

    while (avail - sent >= sizeof(st->buf)) {
        if (st->bpos == sizeof(st->buf)) {
            rc = hda_codec_xfer(&st->state->hda, st->stream, true,
                                st->buf, sizeof(st->buf));
            if (!rc) {
                break;
            }
            st->bpos = 0;
        }
        len = AUD_write(st->voice.out, st->buf + st->bpos,
                        sizeof(st->buf) - st->bpos);
        st->bpos += len;
        sent += len;
        if (st->bpos != sizeof(st->buf)) {
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
    dprint(st->state, 1, "%s: %s (stream %d)\n", st->node->name,
           st->running ? "on" : "off", st->stream);
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

    if (st->output) {
        AUD_set_volume_out(st->voice.out, muted, left, right);
    } else {
        AUD_set_volume_in(st->voice.in, muted, left, right);
    }
}

static void hda_audio_setup(HDAAudioStream *st)
{
    if (st->node == NULL) {
        return;
    }

    dprint(st->state, 1, "%s: format: %d x %s @ %d Hz\n",
           st->node->name, st->as.nchannels,
           fmt2name[st->as.fmt], st->as.freq);

    if (st->output) {
        st->voice.out = AUD_open_out(&st->state->card, st->voice.out,
                                     st->node->name, st,
                                     hda_audio_output_cb, &st->as);
    } else {
        st->voice.in = AUD_open_in(&st->state->card, st->voice.in,
                                   st->node->name, st,
                                   hda_audio_input_cb, &st->as);
    }
}

static void hda_audio_command(HDACodecDevice *hda, uint32_t nid, uint32_t data)
{
    HDAAudioState *a = DO_UPCAST(HDAAudioState, hda, hda);
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
           __FUNCTION__, nid, node->name, verb, payload);

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
        hda_audio_set_running(st, a->running[st->stream]);
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
           __FUNCTION__, nid, node ? node->name : "?", verb, payload);
    hda_codec_response(hda, true, 0);
}

static void hda_audio_stream(HDACodecDevice *hda, uint32_t stnr, bool running)
{
    HDAAudioState *a = DO_UPCAST(HDAAudioState, hda, hda);
    int s;

    a->running[stnr] = running;
    for (s = 0; s < ARRAY_SIZE(a->st); s++) {
        if (a->st[s].node == NULL) {
            continue;
        }
        if (a->st[s].stream != stnr) {
            continue;
        }
        hda_audio_set_running(&a->st[s], running);
    }
}

static int hda_audio_init(HDACodecDevice *hda, const struct desc_codec *desc)
{
    HDAAudioState *a = DO_UPCAST(HDAAudioState, hda, hda);
    HDAAudioStream *st;
    const desc_node *node;
    const desc_param *param;
    uint32_t i, type;

    a->desc = desc;
    a->name = a->hda.qdev.info->name;
    dprint(a, 1, "%s: cad %d\n", __FUNCTION__, a->hda.cad);

    AUD_register_card("hda", &a->card);
    for (i = 0; i < a->desc->nnodes; i++) {
        node = a->desc->nodes + i;
        param = hda_codec_find_param(node, AC_PAR_AUDIO_WIDGET_CAP);
        if (NULL == param)
            continue;
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
                st->bpos = sizeof(st->buf);
                st->output = true;
            } else {
                st->output = false;
            }
            st->format = AC_FMT_TYPE_PCM | AC_FMT_BITS_16 |
                (1 << AC_FMT_CHAN_SHIFT);
            hda_codec_parse_fmt(st->format, &st->as);
            hda_audio_setup(st);
            break;
        }
    }
    return 0;
}

static int hda_audio_exit(HDACodecDevice *hda)
{
    HDAAudioState *a = DO_UPCAST(HDAAudioState, hda, hda);
    HDAAudioStream *st;
    int i;

    dprint(a, 1, "%s\n", __FUNCTION__);
    for (i = 0; i < ARRAY_SIZE(a->st); i++) {
        st = a->st + i;
        if (st->node == NULL) {
            continue;
        }
        if (st->output) {
            AUD_close_out(&a->card, st->voice.out);
        } else {
            AUD_close_in(&a->card, st->voice.in);
        }
    }
    AUD_remove_card(&a->card);
    return 0;
}

static int hda_audio_post_load(void *opaque, int version)
{
    HDAAudioState *a = opaque;
    HDAAudioStream *st;
    int i;

    dprint(a, 1, "%s\n", __FUNCTION__);
    for (i = 0; i < ARRAY_SIZE(a->st); i++) {
        st = a->st + i;
        if (st->node == NULL)
            continue;
        hda_codec_parse_fmt(st->format, &st->as);
        hda_audio_setup(st);
        hda_audio_set_amp(st);
        hda_audio_set_running(st, a->running[st->stream]);
    }
    return 0;
}

static const VMStateDescription vmstate_hda_audio_stream = {
    .name = "hda-audio-stream",
    .version_id = 1,
    .fields = (VMStateField []) {
        VMSTATE_UINT32(stream, HDAAudioStream),
        VMSTATE_UINT32(channel, HDAAudioStream),
        VMSTATE_UINT32(format, HDAAudioStream),
        VMSTATE_UINT32(gain_left, HDAAudioStream),
        VMSTATE_UINT32(gain_right, HDAAudioStream),
        VMSTATE_BOOL(mute_left, HDAAudioStream),
        VMSTATE_BOOL(mute_right, HDAAudioStream),
        VMSTATE_UINT32(bpos, HDAAudioStream),
        VMSTATE_BUFFER(buf, HDAAudioStream),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_hda_audio = {
    .name = "hda-audio",
    .version_id = 1,
    .post_load = hda_audio_post_load,
    .fields = (VMStateField []) {
        VMSTATE_STRUCT_ARRAY(st, HDAAudioState, 4, 0,
                             vmstate_hda_audio_stream,
                             HDAAudioStream),
        VMSTATE_BOOL_ARRAY(running, HDAAudioState, 16),
        VMSTATE_END_OF_LIST()
    }
};

static Property hda_audio_properties[] = {
    DEFINE_PROP_UINT32("debug", HDAAudioState, debug, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static int hda_audio_init_output(HDACodecDevice *hda)
{
    return hda_audio_init(hda, &output);
}

static int hda_audio_init_duplex(HDACodecDevice *hda)
{
    return hda_audio_init(hda, &duplex);
}

static HDACodecDeviceInfo hda_audio_info_output = {
    .qdev.name    = "hda-output",
    .qdev.desc    = "HDA Audio Codec, output-only",
    .qdev.size    = sizeof(HDAAudioState),
    .qdev.vmsd    = &vmstate_hda_audio,
    .qdev.props   = hda_audio_properties,
    .init         = hda_audio_init_output,
    .exit         = hda_audio_exit,
    .command      = hda_audio_command,
    .stream       = hda_audio_stream,
};

static HDACodecDeviceInfo hda_audio_info_duplex = {
    .qdev.name    = "hda-duplex",
    .qdev.desc    = "HDA Audio Codec, duplex",
    .qdev.size    = sizeof(HDAAudioState),
    .qdev.vmsd    = &vmstate_hda_audio,
    .qdev.props   = hda_audio_properties,
    .init         = hda_audio_init_duplex,
    .exit         = hda_audio_exit,
    .command      = hda_audio_command,
    .stream       = hda_audio_stream,
};

static void hda_audio_register(void)
{
    hda_codec_register(&hda_audio_info_output);
    hda_codec_register(&hda_audio_info_duplex);
}
device_init(hda_audio_register);

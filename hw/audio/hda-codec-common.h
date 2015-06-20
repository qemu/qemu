/*
 * Common code to disable/enable mixer emulation at run time
 *
 * Copyright (C) 2013 Red Hat, Inc.
 *
 * Written by Bandan Das <bsd@redhat.com>
 * with important bits picked up from hda-codec.c
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

/*
 * HDA codec descriptions
 */

#ifdef HDA_MIXER
#define QEMU_HDA_ID_OUTPUT  ((QEMU_HDA_ID_VENDOR << 16) | 0x12)
#define QEMU_HDA_ID_DUPLEX  ((QEMU_HDA_ID_VENDOR << 16) | 0x22)
#define QEMU_HDA_ID_MICRO   ((QEMU_HDA_ID_VENDOR << 16) | 0x32)
#define QEMU_HDA_AMP_CAPS                                               \
    (AC_AMPCAP_MUTE |                                                   \
     (QEMU_HDA_AMP_STEPS << AC_AMPCAP_OFFSET_SHIFT)    |                \
     (QEMU_HDA_AMP_STEPS << AC_AMPCAP_NUM_STEPS_SHIFT) |                \
     (3                  << AC_AMPCAP_STEP_SIZE_SHIFT))
#else
#define QEMU_HDA_ID_OUTPUT  ((QEMU_HDA_ID_VENDOR << 16) | 0x11)
#define QEMU_HDA_ID_DUPLEX  ((QEMU_HDA_ID_VENDOR << 16) | 0x21)
#define QEMU_HDA_ID_MICRO   ((QEMU_HDA_ID_VENDOR << 16) | 0x31)
#define QEMU_HDA_AMP_CAPS   QEMU_HDA_AMP_NONE
#endif


/* common: audio output widget */
static const desc_param glue(common_params_audio_dac_, PARAM)[] = {
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

/* common: audio input widget */
static const desc_param glue(common_params_audio_adc_, PARAM)[] = {
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

/* common: pin widget (line-out) */
static const desc_param glue(common_params_audio_lineout_, PARAM)[] = {
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

/* common: pin widget (line-in) */
static const desc_param glue(common_params_audio_linein_, PARAM)[] = {
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

/* output: root node */
static const desc_param glue(output_params_root_, PARAM)[] = {
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
static const desc_param glue(output_params_audio_func_, PARAM)[] = {
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
static const desc_node glue(output_nodes_, PARAM)[] = {
    {
        .nid     = AC_NODE_ROOT,
        .name    = "root",
        .params  = glue(output_params_root_, PARAM),
        .nparams = ARRAY_SIZE(glue(output_params_root_, PARAM)),
    },{
        .nid     = 1,
        .name    = "func",
        .params  = glue(output_params_audio_func_, PARAM),
        .nparams = ARRAY_SIZE(glue(output_params_audio_func_, PARAM)),
    },{
        .nid     = 2,
        .name    = "dac",
        .params  = glue(common_params_audio_dac_, PARAM),
        .nparams = ARRAY_SIZE(glue(common_params_audio_dac_, PARAM)),
        .stindex = 0,
    },{
        .nid     = 3,
        .name    = "out",
        .params  = glue(common_params_audio_lineout_, PARAM),
        .nparams = ARRAY_SIZE(glue(common_params_audio_lineout_, PARAM)),
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
static const desc_codec glue(output_, PARAM) = {
    .name   = "output",
    .iid    = QEMU_HDA_ID_OUTPUT,
    .nodes  = glue(output_nodes_, PARAM),
    .nnodes = ARRAY_SIZE(glue(output_nodes_, PARAM)),
};

/* duplex: root node */
static const desc_param glue(duplex_params_root_, PARAM)[] = {
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

/* duplex: audio function */
static const desc_param glue(duplex_params_audio_func_, PARAM)[] = {
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
static const desc_node glue(duplex_nodes_, PARAM)[] = {
    {
        .nid     = AC_NODE_ROOT,
        .name    = "root",
        .params  = glue(duplex_params_root_, PARAM),
        .nparams = ARRAY_SIZE(glue(duplex_params_root_, PARAM)),
    },{
        .nid     = 1,
        .name    = "func",
        .params  = glue(duplex_params_audio_func_, PARAM),
        .nparams = ARRAY_SIZE(glue(duplex_params_audio_func_, PARAM)),
    },{
        .nid     = 2,
        .name    = "dac",
        .params  = glue(common_params_audio_dac_, PARAM),
        .nparams = ARRAY_SIZE(glue(common_params_audio_dac_, PARAM)),
        .stindex = 0,
    },{
        .nid     = 3,
        .name    = "out",
        .params  = glue(common_params_audio_lineout_, PARAM),
        .nparams = ARRAY_SIZE(glue(common_params_audio_lineout_, PARAM)),
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
        .params  = glue(common_params_audio_adc_, PARAM),
        .nparams = ARRAY_SIZE(glue(common_params_audio_adc_, PARAM)),
        .stindex = 1,
        .conn    = (uint32_t[]) { 5 },
    },{
        .nid     = 5,
        .name    = "in",
        .params  = glue(common_params_audio_linein_, PARAM),
        .nparams = ARRAY_SIZE(glue(common_params_audio_linein_, PARAM)),
        .config  = ((AC_JACK_PORT_COMPLEX << AC_DEFCFG_PORT_CONN_SHIFT) |
                    (AC_JACK_LINE_IN      << AC_DEFCFG_DEVICE_SHIFT)    |
                    (AC_JACK_CONN_UNKNOWN << AC_DEFCFG_CONN_TYPE_SHIFT) |
                    (AC_JACK_COLOR_RED    << AC_DEFCFG_COLOR_SHIFT)     |
                    0x20),
        .pinctl  = AC_PINCTL_IN_EN,
    }
};

/* duplex: codec */
static const desc_codec glue(duplex_, PARAM) = {
    .name   = "duplex",
    .iid    = QEMU_HDA_ID_DUPLEX,
    .nodes  = glue(duplex_nodes_, PARAM),
    .nnodes = ARRAY_SIZE(glue(duplex_nodes_, PARAM)),
};

/* micro: root node */
static const desc_param glue(micro_params_root_, PARAM)[] = {
    {
        .id  = AC_PAR_VENDOR_ID,
        .val = QEMU_HDA_ID_MICRO,
    },{
        .id  = AC_PAR_SUBSYSTEM_ID,
        .val = QEMU_HDA_ID_MICRO,
    },{
        .id  = AC_PAR_REV_ID,
        .val = 0x00100101,
    },{
        .id  = AC_PAR_NODE_COUNT,
        .val = 0x00010001,
    },
};

/* micro: audio function */
static const desc_param glue(micro_params_audio_func_, PARAM)[] = {
    {
        .id  = AC_PAR_FUNCTION_TYPE,
        .val = AC_GRP_AUDIO_FUNCTION,
    },{
        .id  = AC_PAR_SUBSYSTEM_ID,
        .val = QEMU_HDA_ID_MICRO,
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

/* micro: nodes */
static const desc_node glue(micro_nodes_, PARAM)[] = {
    {
        .nid     = AC_NODE_ROOT,
        .name    = "root",
        .params  = glue(micro_params_root_, PARAM),
        .nparams = ARRAY_SIZE(glue(micro_params_root_, PARAM)),
    },{
        .nid     = 1,
        .name    = "func",
        .params  = glue(micro_params_audio_func_, PARAM),
        .nparams = ARRAY_SIZE(glue(micro_params_audio_func_, PARAM)),
    },{
        .nid     = 2,
        .name    = "dac",
        .params  = glue(common_params_audio_dac_, PARAM),
        .nparams = ARRAY_SIZE(glue(common_params_audio_dac_, PARAM)),
        .stindex = 0,
    },{
        .nid     = 3,
        .name    = "out",
        .params  = glue(common_params_audio_lineout_, PARAM),
        .nparams = ARRAY_SIZE(glue(common_params_audio_lineout_, PARAM)),
        .config  = ((AC_JACK_PORT_COMPLEX << AC_DEFCFG_PORT_CONN_SHIFT) |
                    (AC_JACK_SPEAKER      << AC_DEFCFG_DEVICE_SHIFT)    |
                    (AC_JACK_CONN_UNKNOWN << AC_DEFCFG_CONN_TYPE_SHIFT) |
                    (AC_JACK_COLOR_GREEN  << AC_DEFCFG_COLOR_SHIFT)     |
                    0x10),
        .pinctl  = AC_PINCTL_OUT_EN,
        .conn    = (uint32_t[]) { 2 },
    },{
        .nid     = 4,
        .name    = "adc",
        .params  = glue(common_params_audio_adc_, PARAM),
        .nparams = ARRAY_SIZE(glue(common_params_audio_adc_, PARAM)),
        .stindex = 1,
        .conn    = (uint32_t[]) { 5 },
    },{
        .nid     = 5,
        .name    = "in",
        .params  = glue(common_params_audio_linein_, PARAM),
        .nparams = ARRAY_SIZE(glue(common_params_audio_linein_, PARAM)),
        .config  = ((AC_JACK_PORT_COMPLEX << AC_DEFCFG_PORT_CONN_SHIFT) |
                    (AC_JACK_MIC_IN       << AC_DEFCFG_DEVICE_SHIFT)    |
                    (AC_JACK_CONN_UNKNOWN << AC_DEFCFG_CONN_TYPE_SHIFT) |
                    (AC_JACK_COLOR_RED    << AC_DEFCFG_COLOR_SHIFT)     |
                    0x20),
        .pinctl  = AC_PINCTL_IN_EN,
    }
};

/* micro: codec */
static const desc_codec glue(micro_, PARAM) = {
    .name   = "micro",
    .iid    = QEMU_HDA_ID_MICRO,
    .nodes  = glue(micro_nodes_, PARAM),
    .nnodes = ARRAY_SIZE(glue(micro_nodes_, PARAM)),
};

#undef PARAM
#undef HDA_MIXER
#undef QEMU_HDA_ID_OUTPUT
#undef QEMU_HDA_ID_DUPLEX
#undef QEMU_HDA_ID_MICRO
#undef QEMU_HDA_AMP_CAPS

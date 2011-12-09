/*
 * LM4549 Audio Codec Interface
 *
 * Copyright (c) 2011
 * Written by Mathieu Sonet - www.elasticsheep.com
 *
 * This code is licensed under the GPL.
 *
 * *****************************************************************
 *
 * This driver emulates the LM4549 codec.
 *
 * It supports only one playback voice and no record voice.
 */

#include "hw.h"
#include "audio/audio.h"
#include "lm4549.h"

#if 0
#define LM4549_DEBUG  1
#endif

#if 0
#define LM4549_DUMP_DAC_INPUT 1
#endif

#ifdef LM4549_DEBUG
#define DPRINTF(fmt, ...) \
do { printf("lm4549: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) do {} while (0)
#endif

#if defined(LM4549_DUMP_DAC_INPUT)
#include <stdio.h>
static FILE *fp_dac_input;
#endif

/* LM4549 register list */
enum {
    LM4549_Reset                    = 0x00,
    LM4549_Master_Volume            = 0x02,
    LM4549_Line_Out_Volume          = 0x04,
    LM4549_Master_Volume_Mono       = 0x06,
    LM4549_PC_Beep_Volume           = 0x0A,
    LM4549_Phone_Volume             = 0x0C,
    LM4549_Mic_Volume               = 0x0E,
    LM4549_Line_In_Volume           = 0x10,
    LM4549_CD_Volume                = 0x12,
    LM4549_Video_Volume             = 0x14,
    LM4549_Aux_Volume               = 0x16,
    LM4549_PCM_Out_Volume           = 0x18,
    LM4549_Record_Select            = 0x1A,
    LM4549_Record_Gain              = 0x1C,
    LM4549_General_Purpose          = 0x20,
    LM4549_3D_Control               = 0x22,
    LM4549_Powerdown_Ctrl_Stat      = 0x26,
    LM4549_Ext_Audio_ID             = 0x28,
    LM4549_Ext_Audio_Stat_Ctrl      = 0x2A,
    LM4549_PCM_Front_DAC_Rate       = 0x2C,
    LM4549_PCM_ADC_Rate             = 0x32,
    LM4549_Vendor_ID1               = 0x7C,
    LM4549_Vendor_ID2               = 0x7E
};

static void lm4549_reset(lm4549_state *s)
{
    uint16_t *regfile = s->regfile;

    regfile[LM4549_Reset]               = 0x0d50;
    regfile[LM4549_Master_Volume]       = 0x8008;
    regfile[LM4549_Line_Out_Volume]     = 0x8000;
    regfile[LM4549_Master_Volume_Mono]  = 0x8000;
    regfile[LM4549_PC_Beep_Volume]      = 0x0000;
    regfile[LM4549_Phone_Volume]        = 0x8008;
    regfile[LM4549_Mic_Volume]          = 0x8008;
    regfile[LM4549_Line_In_Volume]      = 0x8808;
    regfile[LM4549_CD_Volume]           = 0x8808;
    regfile[LM4549_Video_Volume]        = 0x8808;
    regfile[LM4549_Aux_Volume]          = 0x8808;
    regfile[LM4549_PCM_Out_Volume]      = 0x8808;
    regfile[LM4549_Record_Select]       = 0x0000;
    regfile[LM4549_Record_Gain]         = 0x8000;
    regfile[LM4549_General_Purpose]     = 0x0000;
    regfile[LM4549_3D_Control]          = 0x0101;
    regfile[LM4549_Powerdown_Ctrl_Stat] = 0x000f;
    regfile[LM4549_Ext_Audio_ID]        = 0x0001;
    regfile[LM4549_Ext_Audio_Stat_Ctrl] = 0x0000;
    regfile[LM4549_PCM_Front_DAC_Rate]  = 0xbb80;
    regfile[LM4549_PCM_ADC_Rate]        = 0xbb80;
    regfile[LM4549_Vendor_ID1]          = 0x4e53;
    regfile[LM4549_Vendor_ID2]          = 0x4331;
}

static void lm4549_audio_transfer(lm4549_state *s)
{
    uint32_t written_bytes, written_samples;
    uint32_t i;

    /* Activate the voice */
    AUD_set_active_out(s->voice, 1);
    s->voice_is_active = 1;

    /* Try to write the buffer content */
    written_bytes = AUD_write(s->voice, s->buffer,
                              s->buffer_level * sizeof(uint16_t));
    written_samples = written_bytes >> 1;

#if defined(LM4549_DUMP_DAC_INPUT)
    fwrite(s->buffer, sizeof(uint8_t), written_bytes, fp_dac_input);
#endif

    s->buffer_level -= written_samples;

    if (s->buffer_level > 0) {
        /* Move the data back to the start of the buffer */
        for (i = 0; i < s->buffer_level; i++) {
            s->buffer[i] = s->buffer[i + written_samples];
        }
    }
}

static void lm4549_audio_out_callback(void *opaque, int free)
{
    lm4549_state *s = (lm4549_state *)opaque;
    static uint32_t prev_buffer_level;

#ifdef LM4549_DEBUG
    int size = AUD_get_buffer_size_out(s->voice);
    DPRINTF("audio_out_callback size = %i free = %i\n", size, free);
#endif

    /* Detect that no data are consumed
       => disable the voice */
    if (s->buffer_level == prev_buffer_level) {
        AUD_set_active_out(s->voice, 0);
        s->voice_is_active = 0;
    }
    prev_buffer_level = s->buffer_level;

    /* Check if a buffer transfer is pending */
    if (s->buffer_level == LM4549_BUFFER_SIZE) {
        lm4549_audio_transfer(s);

        /* Request more data */
        if (s->data_req_cb != NULL) {
            (s->data_req_cb)(s->opaque);
        }
    }
}

uint32_t lm4549_read(lm4549_state *s, target_phys_addr_t offset)
{
    uint16_t *regfile = s->regfile;
    uint32_t value = 0;

    /* Read the stored value */
    assert(offset < 128);
    value = regfile[offset];

    DPRINTF("read [0x%02x] = 0x%04x\n", offset, value);

    return value;
}

void lm4549_write(lm4549_state *s,
                  target_phys_addr_t offset, uint32_t value)
{
    uint16_t *regfile = s->regfile;

    assert(offset < 128);
    DPRINTF("write [0x%02x] = 0x%04x\n", offset, value);

    switch (offset) {
    case LM4549_Reset:
        lm4549_reset(s);
        break;

    case LM4549_PCM_Front_DAC_Rate:
        regfile[LM4549_PCM_Front_DAC_Rate] = value;
        DPRINTF("DAC rate change = %i\n", value);

        /* Re-open a voice with the new sample rate */
        struct audsettings as;
        as.freq = value;
        as.nchannels = 2;
        as.fmt = AUD_FMT_S16;
        as.endianness = 0;

        s->voice = AUD_open_out(
            &s->card,
            s->voice,
            "lm4549.out",
            s,
            lm4549_audio_out_callback,
            &as
        );
        break;

    case LM4549_Powerdown_Ctrl_Stat:
        value &= ~0xf;
        value |= regfile[LM4549_Powerdown_Ctrl_Stat] & 0xf;
        regfile[LM4549_Powerdown_Ctrl_Stat] = value;
        break;

    case LM4549_Ext_Audio_ID:
    case LM4549_Vendor_ID1:
    case LM4549_Vendor_ID2:
        DPRINTF("Write to read-only register 0x%x\n", (int)offset);
        break;

    default:
        /* Store the new value */
        regfile[offset] = value;
        break;
    }
}

uint32_t lm4549_write_samples(lm4549_state *s, uint32_t left, uint32_t right)
{
    /* The left and right samples are in 20-bit resolution.
       The LM4549 has 18-bit resolution and only uses the bits [19:2].
       This model supports 16-bit playback.
    */

    if (s->buffer_level >= LM4549_BUFFER_SIZE) {
        DPRINTF("write_sample Buffer full\n");
        return 0;
    }

    /* Store 16-bit samples in the buffer */
    s->buffer[s->buffer_level++] = (left >> 4);
    s->buffer[s->buffer_level++] = (right >> 4);

    if (s->buffer_level == LM4549_BUFFER_SIZE) {
        /* Trigger the transfer of the buffer to the audio host */
        lm4549_audio_transfer(s);
    }

    return 1;
}

static int lm4549_post_load(void *opaque, int version_id)
{
    lm4549_state *s = (lm4549_state *)opaque;
    uint16_t *regfile = s->regfile;

    /* Re-open a voice with the current sample rate */
    uint32_t freq = regfile[LM4549_PCM_Front_DAC_Rate];

    DPRINTF("post_load freq = %i\n", freq);
    DPRINTF("post_load voice_is_active = %i\n", s->voice_is_active);

    struct audsettings as;
    as.freq = freq;
    as.nchannels = 2;
    as.fmt = AUD_FMT_S16;
    as.endianness = 0;

    s->voice = AUD_open_out(
        &s->card,
        s->voice,
        "lm4549.out",
        s,
        lm4549_audio_out_callback,
        &as
    );

    /* Request data */
    if (s->voice_is_active == 1) {
        lm4549_audio_out_callback(s, AUD_get_buffer_size_out(s->voice));
    }

    return 0;
}

void lm4549_init(lm4549_state *s, lm4549_callback data_req_cb, void* opaque)
{
    struct audsettings as;

    /* Store the callback and opaque pointer */
    s->data_req_cb = data_req_cb;
    s->opaque = opaque;

    /* Init the registers */
    lm4549_reset(s);

    /* Register an audio card */
    AUD_register_card("lm4549", &s->card);

    /* Open a default voice */
    as.freq = 48000;
    as.nchannels = 2;
    as.fmt = AUD_FMT_S16;
    as.endianness = 0;

    s->voice = AUD_open_out(
        &s->card,
        s->voice,
        "lm4549.out",
        s,
        lm4549_audio_out_callback,
        &as
    );

    AUD_set_volume_out(s->voice, 0, 255, 255);

    s->voice_is_active = 0;

    /* Reset the input buffer */
    memset(s->buffer, 0x00, sizeof(s->buffer));
    s->buffer_level = 0;

#if defined(LM4549_DUMP_DAC_INPUT)
    fp_dac_input = fopen("lm4549_dac_input.pcm", "wb");
    if (!fp_dac_input) {
        hw_error("Unable to open lm4549_dac_input.pcm for writing\n");
    }
#endif
}

const VMStateDescription vmstate_lm4549_state = {
    .name = "lm4549_state",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .post_load = &lm4549_post_load,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT32(voice_is_active, lm4549_state),
        VMSTATE_UINT16_ARRAY(regfile, lm4549_state, 128),
        VMSTATE_UINT16_ARRAY(buffer, lm4549_state, LM4549_BUFFER_SIZE),
        VMSTATE_UINT32(buffer_level, lm4549_state),
        VMSTATE_END_OF_LIST()
    }
};

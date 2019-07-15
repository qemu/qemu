/*
 * xlnx_dp.c
 *
 *  Copyright (C) 2015 : GreenSocs Ltd
 *      http://www.greensocs.com/ , email: info@greensocs.com
 *
 *  Developed by :
 *  Frederic Konrad   <fred.konrad@greensocs.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option)any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/display/xlnx_dp.h"

#ifndef DEBUG_DP
#define DEBUG_DP 0
#endif

#define DPRINTF(fmt, ...) do {                                                 \
    if (DEBUG_DP) {                                                            \
        qemu_log("xlnx_dp: " fmt , ## __VA_ARGS__);                            \
    }                                                                          \
} while (0)

/*
 * Register offset for DP.
 */
#define DP_LINK_BW_SET                      (0x0000 >> 2)
#define DP_LANE_COUNT_SET                   (0x0004 >> 2)
#define DP_ENHANCED_FRAME_EN                (0x0008 >> 2)
#define DP_TRAINING_PATTERN_SET             (0x000C >> 2)
#define DP_LINK_QUAL_PATTERN_SET            (0x0010 >> 2)
#define DP_SCRAMBLING_DISABLE               (0x0014 >> 2)
#define DP_DOWNSPREAD_CTRL                  (0x0018 >> 2)
#define DP_SOFTWARE_RESET                   (0x001C >> 2)
#define DP_TRANSMITTER_ENABLE               (0x0080 >> 2)
#define DP_MAIN_STREAM_ENABLE               (0x0084 >> 2)
#define DP_FORCE_SCRAMBLER_RESET            (0x00C0 >> 2)
#define DP_VERSION_REGISTER                 (0x00F8 >> 2)
#define DP_CORE_ID                          (0x00FC >> 2)

#define DP_AUX_COMMAND_REGISTER             (0x0100 >> 2)
#define AUX_ADDR_ONLY_MASK                  (0x1000)
#define AUX_COMMAND_MASK                    (0x0F00)
#define AUX_COMMAND_SHIFT                   (8)
#define AUX_COMMAND_NBYTES                  (0x000F)

#define DP_AUX_WRITE_FIFO                   (0x0104 >> 2)
#define DP_AUX_ADDRESS                      (0x0108 >> 2)
#define DP_AUX_CLOCK_DIVIDER                (0x010C >> 2)
#define DP_TX_USER_FIFO_OVERFLOW            (0x0110 >> 2)
#define DP_INTERRUPT_SIGNAL_STATE           (0x0130 >> 2)
#define DP_AUX_REPLY_DATA                   (0x0134 >> 2)
#define DP_AUX_REPLY_CODE                   (0x0138 >> 2)
#define DP_AUX_REPLY_COUNT                  (0x013C >> 2)
#define DP_REPLY_DATA_COUNT                 (0x0148 >> 2)
#define DP_REPLY_STATUS                     (0x014C >> 2)
#define DP_HPD_DURATION                     (0x0150 >> 2)
#define DP_MAIN_STREAM_HTOTAL               (0x0180 >> 2)
#define DP_MAIN_STREAM_VTOTAL               (0x0184 >> 2)
#define DP_MAIN_STREAM_POLARITY             (0x0188 >> 2)
#define DP_MAIN_STREAM_HSWIDTH              (0x018C >> 2)
#define DP_MAIN_STREAM_VSWIDTH              (0x0190 >> 2)
#define DP_MAIN_STREAM_HRES                 (0x0194 >> 2)
#define DP_MAIN_STREAM_VRES                 (0x0198 >> 2)
#define DP_MAIN_STREAM_HSTART               (0x019C >> 2)
#define DP_MAIN_STREAM_VSTART               (0x01A0 >> 2)
#define DP_MAIN_STREAM_MISC0                (0x01A4 >> 2)
#define DP_MAIN_STREAM_MISC1                (0x01A8 >> 2)
#define DP_MAIN_STREAM_M_VID                (0x01AC >> 2)
#define DP_MSA_TRANSFER_UNIT_SIZE           (0x01B0 >> 2)
#define DP_MAIN_STREAM_N_VID                (0x01B4 >> 2)
#define DP_USER_DATA_COUNT_PER_LANE         (0x01BC >> 2)
#define DP_MIN_BYTES_PER_TU                 (0x01C4 >> 2)
#define DP_FRAC_BYTES_PER_TU                (0x01C8 >> 2)
#define DP_INIT_WAIT                        (0x01CC >> 2)
#define DP_PHY_RESET                        (0x0200 >> 2)
#define DP_PHY_VOLTAGE_DIFF_LANE_0          (0x0220 >> 2)
#define DP_PHY_VOLTAGE_DIFF_LANE_1          (0x0224 >> 2)
#define DP_TRANSMIT_PRBS7                   (0x0230 >> 2)
#define DP_PHY_CLOCK_SELECT                 (0x0234 >> 2)
#define DP_TX_PHY_POWER_DOWN                (0x0238 >> 2)
#define DP_PHY_PRECURSOR_LANE_0             (0x023C >> 2)
#define DP_PHY_PRECURSOR_LANE_1             (0x0240 >> 2)
#define DP_PHY_POSTCURSOR_LANE_0            (0x024C >> 2)
#define DP_PHY_POSTCURSOR_LANE_1            (0x0250 >> 2)
#define DP_PHY_STATUS                       (0x0280 >> 2)

#define DP_TX_AUDIO_CONTROL                 (0x0300 >> 2)
#define DP_TX_AUD_CTRL                      (1)

#define DP_TX_AUDIO_CHANNELS                (0x0304 >> 2)
#define DP_TX_AUDIO_INFO_DATA(n)            ((0x0308 + 4 * n) >> 2)
#define DP_TX_M_AUD                         (0x0328 >> 2)
#define DP_TX_N_AUD                         (0x032C >> 2)
#define DP_TX_AUDIO_EXT_DATA(n)             ((0x0330 + 4 * n) >> 2)
#define DP_INT_STATUS                       (0x03A0 >> 2)
#define DP_INT_MASK                         (0x03A4 >> 2)
#define DP_INT_EN                           (0x03A8 >> 2)
#define DP_INT_DS                           (0x03AC >> 2)

/*
 * Registers offset for Audio Video Buffer configuration.
 */
#define V_BLEND_OFFSET                      (0xA000)
#define V_BLEND_BG_CLR_0                    (0x0000 >> 2)
#define V_BLEND_BG_CLR_1                    (0x0004 >> 2)
#define V_BLEND_BG_CLR_2                    (0x0008 >> 2)
#define V_BLEND_SET_GLOBAL_ALPHA_REG        (0x000C >> 2)
#define V_BLEND_OUTPUT_VID_FORMAT           (0x0014 >> 2)
#define V_BLEND_LAYER0_CONTROL              (0x0018 >> 2)
#define V_BLEND_LAYER1_CONTROL              (0x001C >> 2)

#define V_BLEND_RGB2YCBCR_COEFF(n)          ((0x0020 + 4 * n) >> 2)
#define V_BLEND_IN1CSC_COEFF(n)             ((0x0044 + 4 * n) >> 2)

#define V_BLEND_LUMA_IN1CSC_OFFSET          (0x0068 >> 2)
#define V_BLEND_CR_IN1CSC_OFFSET            (0x006C >> 2)
#define V_BLEND_CB_IN1CSC_OFFSET            (0x0070 >> 2)
#define V_BLEND_LUMA_OUTCSC_OFFSET          (0x0074 >> 2)
#define V_BLEND_CR_OUTCSC_OFFSET            (0x0078 >> 2)
#define V_BLEND_CB_OUTCSC_OFFSET            (0x007C >> 2)

#define V_BLEND_IN2CSC_COEFF(n)             ((0x0080 + 4 * n) >> 2)

#define V_BLEND_LUMA_IN2CSC_OFFSET          (0x00A4 >> 2)
#define V_BLEND_CR_IN2CSC_OFFSET            (0x00A8 >> 2)
#define V_BLEND_CB_IN2CSC_OFFSET            (0x00AC >> 2)
#define V_BLEND_CHROMA_KEY_ENABLE           (0x01D0 >> 2)
#define V_BLEND_CHROMA_KEY_COMP1            (0x01D4 >> 2)
#define V_BLEND_CHROMA_KEY_COMP2            (0x01D8 >> 2)
#define V_BLEND_CHROMA_KEY_COMP3            (0x01DC >> 2)

/*
 * Registers offset for Audio Video Buffer configuration.
 */
#define AV_BUF_MANAGER_OFFSET               (0xB000)
#define AV_BUF_FORMAT                       (0x0000 >> 2)
#define AV_BUF_NON_LIVE_LATENCY             (0x0008 >> 2)
#define AV_CHBUF0                           (0x0010 >> 2)
#define AV_CHBUF1                           (0x0014 >> 2)
#define AV_CHBUF2                           (0x0018 >> 2)
#define AV_CHBUF3                           (0x001C >> 2)
#define AV_CHBUF4                           (0x0020 >> 2)
#define AV_CHBUF5                           (0x0024 >> 2)
#define AV_BUF_STC_CONTROL                  (0x002C >> 2)
#define AV_BUF_STC_INIT_VALUE0              (0x0030 >> 2)
#define AV_BUF_STC_INIT_VALUE1              (0x0034 >> 2)
#define AV_BUF_STC_ADJ                      (0x0038 >> 2)
#define AV_BUF_STC_VIDEO_VSYNC_TS_REG0      (0x003C >> 2)
#define AV_BUF_STC_VIDEO_VSYNC_TS_REG1      (0x0040 >> 2)
#define AV_BUF_STC_EXT_VSYNC_TS_REG0        (0x0044 >> 2)
#define AV_BUF_STC_EXT_VSYNC_TS_REG1        (0x0048 >> 2)
#define AV_BUF_STC_CUSTOM_EVENT_TS_REG0     (0x004C >> 2)
#define AV_BUF_STC_CUSTOM_EVENT_TS_REG1     (0x0050 >> 2)
#define AV_BUF_STC_CUSTOM_EVENT2_TS_REG0    (0x0054 >> 2)
#define AV_BUF_STC_CUSTOM_EVENT2_TS_REG1    (0x0058 >> 2)
#define AV_BUF_STC_SNAPSHOT0                (0x0060 >> 2)
#define AV_BUF_STC_SNAPSHOT1                (0x0064 >> 2)
#define AV_BUF_OUTPUT_AUDIO_VIDEO_SELECT    (0x0070 >> 2)
#define AV_BUF_HCOUNT_VCOUNT_INT0           (0x0074 >> 2)
#define AV_BUF_HCOUNT_VCOUNT_INT1           (0x0078 >> 2)
#define AV_BUF_DITHER_CONFIG                (0x007C >> 2)
#define AV_BUF_DITHER_CONFIG_MAX            (0x008C >> 2)
#define AV_BUF_DITHER_CONFIG_MIN            (0x0090 >> 2)
#define AV_BUF_PATTERN_GEN_SELECT           (0x0100 >> 2)
#define AV_BUF_AUD_VID_CLK_SOURCE           (0x0120 >> 2)
#define AV_BUF_SRST_REG                     (0x0124 >> 2)
#define AV_BUF_AUDIO_RDY_INTERVAL           (0x0128 >> 2)
#define AV_BUF_AUDIO_CH_CONFIG              (0x012C >> 2)

#define AV_BUF_GRAPHICS_COMP_SCALE_FACTOR(n)((0x0200 + 4 * n) >> 2)

#define AV_BUF_VIDEO_COMP_SCALE_FACTOR(n)   ((0x020C + 4 * n) >> 2)

#define AV_BUF_LIVE_VIDEO_COMP_SF(n)        ((0x0218 + 4 * n) >> 2)

#define AV_BUF_LIVE_VID_CONFIG              (0x0224 >> 2)

#define AV_BUF_LIVE_GFX_COMP_SF(n)          ((0x0228 + 4 * n) >> 2)

#define AV_BUF_LIVE_GFX_CONFIG              (0x0234 >> 2)

#define AUDIO_MIXER_REGISTER_OFFSET         (0xC000)
#define AUDIO_MIXER_VOLUME_CONTROL          (0x0000 >> 2)
#define AUDIO_MIXER_META_DATA               (0x0004 >> 2)
#define AUD_CH_STATUS_REG(n)                ((0x0008 + 4 * n) >> 2)
#define AUD_CH_A_DATA_REG(n)                ((0x0020 + 4 * n) >> 2)
#define AUD_CH_B_DATA_REG(n)                ((0x0038 + 4 * n) >> 2)

#define DP_AUDIO_DMA_CHANNEL(n)             (4 + n)
#define DP_GRAPHIC_DMA_CHANNEL              (3)
#define DP_VIDEO_DMA_CHANNEL                (0)

enum DPGraphicFmt {
    DP_GRAPHIC_RGBA8888 = 0 << 8,
    DP_GRAPHIC_ABGR8888 = 1 << 8,
    DP_GRAPHIC_RGB888 = 2 << 8,
    DP_GRAPHIC_BGR888 = 3 << 8,
    DP_GRAPHIC_RGBA5551 = 4 << 8,
    DP_GRAPHIC_RGBA4444 = 5 << 8,
    DP_GRAPHIC_RGB565 = 6 << 8,
    DP_GRAPHIC_8BPP = 7 << 8,
    DP_GRAPHIC_4BPP = 8 << 8,
    DP_GRAPHIC_2BPP = 9 << 8,
    DP_GRAPHIC_1BPP = 10 << 8,
    DP_GRAPHIC_MASK = 0xF << 8
};

enum DPVideoFmt {
    DP_NL_VID_CB_Y0_CR_Y1 = 0,
    DP_NL_VID_CR_Y0_CB_Y1 = 1,
    DP_NL_VID_Y0_CR_Y1_CB = 2,
    DP_NL_VID_Y0_CB_Y1_CR = 3,
    DP_NL_VID_YV16 = 4,
    DP_NL_VID_YV24 = 5,
    DP_NL_VID_YV16CL = 6,
    DP_NL_VID_MONO = 7,
    DP_NL_VID_YV16CL2 = 8,
    DP_NL_VID_YUV444 = 9,
    DP_NL_VID_RGB888 = 10,
    DP_NL_VID_RGBA8880 = 11,
    DP_NL_VID_RGB888_10BPC = 12,
    DP_NL_VID_YUV444_10BPC = 13,
    DP_NL_VID_YV16CL2_10BPC = 14,
    DP_NL_VID_YV16CL_10BPC = 15,
    DP_NL_VID_YV16_10BPC = 16,
    DP_NL_VID_YV24_10BPC = 17,
    DP_NL_VID_Y_ONLY_10BPC = 18,
    DP_NL_VID_YV16_420 = 19,
    DP_NL_VID_YV16CL_420 = 20,
    DP_NL_VID_YV16CL2_420 = 21,
    DP_NL_VID_YV16_420_10BPC = 22,
    DP_NL_VID_YV16CL_420_10BPC = 23,
    DP_NL_VID_YV16CL2_420_10BPC = 24,
    DP_NL_VID_FMT_MASK = 0x1F
};

typedef enum DPGraphicFmt DPGraphicFmt;
typedef enum DPVideoFmt DPVideoFmt;

static const VMStateDescription vmstate_dp = {
    .name = TYPE_XLNX_DP,
    .version_id = 1,
    .fields = (VMStateField[]){
        VMSTATE_UINT32_ARRAY(core_registers, XlnxDPState,
                             DP_CORE_REG_ARRAY_SIZE),
        VMSTATE_UINT32_ARRAY(avbufm_registers, XlnxDPState,
                             DP_AVBUF_REG_ARRAY_SIZE),
        VMSTATE_UINT32_ARRAY(vblend_registers, XlnxDPState,
                             DP_VBLEND_REG_ARRAY_SIZE),
        VMSTATE_UINT32_ARRAY(audio_registers, XlnxDPState,
                             DP_AUDIO_REG_ARRAY_SIZE),
        VMSTATE_END_OF_LIST()
    }
};

static void xlnx_dp_update_irq(XlnxDPState *s);

static uint64_t xlnx_dp_audio_read(void *opaque, hwaddr offset, unsigned size)
{
    XlnxDPState *s = XLNX_DP(opaque);

    offset = offset >> 2;
    return s->audio_registers[offset];
}

static void xlnx_dp_audio_write(void *opaque, hwaddr offset, uint64_t value,
                                unsigned size)
{
    XlnxDPState *s = XLNX_DP(opaque);

    offset = offset >> 2;

    switch (offset) {
    case AUDIO_MIXER_META_DATA:
        s->audio_registers[offset] = value & 0x00000001;
        break;
    default:
        s->audio_registers[offset] = value;
        break;
    }
}

static const MemoryRegionOps audio_ops = {
    .read = xlnx_dp_audio_read,
    .write = xlnx_dp_audio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static inline uint32_t xlnx_dp_audio_get_volume(XlnxDPState *s,
                                                uint8_t channel)
{
    switch (channel) {
    case 0:
        return extract32(s->audio_registers[AUDIO_MIXER_VOLUME_CONTROL], 0, 16);
    case 1:
        return extract32(s->audio_registers[AUDIO_MIXER_VOLUME_CONTROL], 16,
                                                                         16);
    default:
        return 0;
    }
}

static inline void xlnx_dp_audio_activate(XlnxDPState *s)
{
    bool activated = ((s->core_registers[DP_TX_AUDIO_CONTROL]
                   & DP_TX_AUD_CTRL) != 0);
    AUD_set_active_out(s->amixer_output_stream, activated);
    xlnx_dpdma_set_host_data_location(s->dpdma, DP_AUDIO_DMA_CHANNEL(0),
                                      &s->audio_buffer_0);
    xlnx_dpdma_set_host_data_location(s->dpdma, DP_AUDIO_DMA_CHANNEL(1),
                                      &s->audio_buffer_1);
}

static inline void xlnx_dp_audio_mix_buffer(XlnxDPState *s)
{
    /*
     * Audio packets are signed and have this shape:
     * | 16 | 16 | 16 | 16 | 16 | 16 | 16 | 16 |
     * | R3 | L3 | R2 | L2 | R1 | L1 | R0 | L0 |
     *
     * Output audio is 16bits saturated.
     */
    int i;

    if ((s->audio_data_available[0]) && (xlnx_dp_audio_get_volume(s, 0))) {
        for (i = 0; i < s->audio_data_available[0] / 2; i++) {
            s->temp_buffer[i] = (int64_t)(s->audio_buffer_0[i])
                              * xlnx_dp_audio_get_volume(s, 0) / 8192;
        }
        s->byte_left = s->audio_data_available[0];
    } else {
        memset(s->temp_buffer, 0, s->audio_data_available[1] / 2);
    }

    if ((s->audio_data_available[1]) && (xlnx_dp_audio_get_volume(s, 1))) {
        if ((s->audio_data_available[0] == 0)
        || (s->audio_data_available[1] == s->audio_data_available[0])) {
            for (i = 0; i < s->audio_data_available[1] / 2; i++) {
                s->temp_buffer[i] += (int64_t)(s->audio_buffer_1[i])
                                   * xlnx_dp_audio_get_volume(s, 1) / 8192;
            }
            s->byte_left = s->audio_data_available[1];
        }
    }

    for (i = 0; i < s->byte_left / 2; i++) {
        s->out_buffer[i] = MAX(-32767, MIN(s->temp_buffer[i], 32767));
    }

    s->data_ptr = 0;
}

static void xlnx_dp_audio_callback(void *opaque, int avail)
{
    /*
     * Get some data from the DPDMA and compute these datas.
     * Then wait for QEMU's audio subsystem to call this callback.
     */
    XlnxDPState *s = XLNX_DP(opaque);
    size_t written = 0;

    /* If there are already some data don't get more data. */
    if (s->byte_left == 0) {
        s->audio_data_available[0] = xlnx_dpdma_start_operation(s->dpdma, 4,
                                                                  true);
        s->audio_data_available[1] = xlnx_dpdma_start_operation(s->dpdma, 5,
                                                                  true);
        xlnx_dp_audio_mix_buffer(s);
    }

    /* Send the buffer through the audio. */
    if (s->byte_left <= MAX_QEMU_BUFFER_SIZE) {
        if (s->byte_left != 0) {
            written = AUD_write(s->amixer_output_stream,
                                &s->out_buffer[s->data_ptr], s->byte_left);
        } else {
            /*
             * There is nothing to play.. We don't have any data! Fill the
             * buffer with zero's and send it.
             */
            written = 0;
            memset(s->out_buffer, 0, 1024);
            AUD_write(s->amixer_output_stream, s->out_buffer, 1024);
        }
    } else {
        written = AUD_write(s->amixer_output_stream,
                            &s->out_buffer[s->data_ptr], MAX_QEMU_BUFFER_SIZE);
    }
    s->byte_left -= written;
    s->data_ptr += written;
}

/*
 * AUX channel related function.
 */
static void xlnx_dp_aux_clear_rx_fifo(XlnxDPState *s)
{
    fifo8_reset(&s->rx_fifo);
}

static void xlnx_dp_aux_push_rx_fifo(XlnxDPState *s, uint8_t *buf, size_t len)
{
    DPRINTF("Push %u data in rx_fifo\n", (unsigned)len);
    fifo8_push_all(&s->rx_fifo, buf, len);
}

static uint8_t xlnx_dp_aux_pop_rx_fifo(XlnxDPState *s)
{
    uint8_t ret;

    if (fifo8_is_empty(&s->rx_fifo)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Reading empty RX_FIFO\n",
                      __func__);
        /*
         * The datasheet is not clear about the reset value, it seems
         * to be unspecified. We choose to return '0'.
         */
        ret = 0;
    } else {
        ret = fifo8_pop(&s->rx_fifo);
        DPRINTF("pop 0x%" PRIX8 " from rx_fifo.\n", ret);
    }
    return ret;
}

static void xlnx_dp_aux_clear_tx_fifo(XlnxDPState *s)
{
    fifo8_reset(&s->tx_fifo);
}

static void xlnx_dp_aux_push_tx_fifo(XlnxDPState *s, uint8_t *buf, size_t len)
{
    DPRINTF("Push %u data in tx_fifo\n", (unsigned)len);
    fifo8_push_all(&s->tx_fifo, buf, len);
}

static uint8_t xlnx_dp_aux_pop_tx_fifo(XlnxDPState *s)
{
    uint8_t ret;

    if (fifo8_is_empty(&s->tx_fifo)) {
        DPRINTF("tx_fifo underflow..\n");
        abort();
    }
    ret = fifo8_pop(&s->tx_fifo);
    DPRINTF("pop 0x%2.2X from tx_fifo.\n", ret);
    return ret;
}

static uint32_t xlnx_dp_aux_get_address(XlnxDPState *s)
{
    return s->core_registers[DP_AUX_ADDRESS];
}

/*
 * Get command from the register.
 */
static void xlnx_dp_aux_set_command(XlnxDPState *s, uint32_t value)
{
    bool address_only = (value & AUX_ADDR_ONLY_MASK) != 0;
    AUXCommand cmd = (value & AUX_COMMAND_MASK) >> AUX_COMMAND_SHIFT;
    uint8_t nbytes = (value & AUX_COMMAND_NBYTES) + 1;
    uint8_t buf[16];
    int i;

    /*
     * When an address_only command is executed nothing happen to the fifo, so
     * just make nbytes = 0.
     */
    if (address_only) {
        nbytes = 0;
    }

    switch (cmd) {
    case READ_AUX:
    case READ_I2C:
    case READ_I2C_MOT:
        s->core_registers[DP_AUX_REPLY_CODE] = aux_request(s->aux_bus, cmd,
                                               xlnx_dp_aux_get_address(s),
                                               nbytes, buf);
        s->core_registers[DP_REPLY_DATA_COUNT] = nbytes;

        if (s->core_registers[DP_AUX_REPLY_CODE] == AUX_I2C_ACK) {
            xlnx_dp_aux_push_rx_fifo(s, buf, nbytes);
        }
        break;
    case WRITE_AUX:
    case WRITE_I2C:
    case WRITE_I2C_MOT:
        for (i = 0; i < nbytes; i++) {
            buf[i] = xlnx_dp_aux_pop_tx_fifo(s);
        }
        s->core_registers[DP_AUX_REPLY_CODE] = aux_request(s->aux_bus, cmd,
                                               xlnx_dp_aux_get_address(s),
                                               nbytes, buf);
        xlnx_dp_aux_clear_tx_fifo(s);
        break;
    case WRITE_I2C_STATUS:
        qemu_log_mask(LOG_UNIMP, "xlnx_dp: Write i2c status not implemented\n");
        break;
    default:
        abort();
    }

    s->core_registers[DP_INTERRUPT_SIGNAL_STATE] |= 0x04;
}

static void xlnx_dp_set_dpdma(const Object *obj, const char *name, Object *val,
                              Error **errp)
{
    XlnxDPState *s = XLNX_DP(obj);
    if (s->console) {
        DisplaySurface *surface = qemu_console_surface(s->console);
        XlnxDPDMAState *dma = XLNX_DPDMA(val);
        xlnx_dpdma_set_host_data_location(dma, DP_GRAPHIC_DMA_CHANNEL,
                                          surface_data(surface));
    }
}

static inline uint8_t xlnx_dp_global_alpha_value(XlnxDPState *s)
{
    return (s->vblend_registers[V_BLEND_SET_GLOBAL_ALPHA_REG] & 0x1FE) >> 1;
}

static inline bool xlnx_dp_global_alpha_enabled(XlnxDPState *s)
{
    /*
     * If the alpha is totally opaque (255) we consider the alpha is disabled to
     * reduce CPU consumption.
     */
    return ((xlnx_dp_global_alpha_value(s) != 0xFF) &&
           ((s->vblend_registers[V_BLEND_SET_GLOBAL_ALPHA_REG] & 0x01) != 0));
}

static void xlnx_dp_recreate_surface(XlnxDPState *s)
{
    /*
     * Two possibilities, if blending is enabled the console displays
     * bout_plane, if not g_plane is displayed.
     */
    uint16_t width = s->core_registers[DP_MAIN_STREAM_HRES];
    uint16_t height = s->core_registers[DP_MAIN_STREAM_VRES];
    DisplaySurface *current_console_surface = qemu_console_surface(s->console);

    if ((width != 0) && (height != 0)) {
        /*
         * As dpy_gfx_replace_surface calls qemu_free_displaysurface on the
         * surface we need to be careful and don't free the surface associated
         * to the console or double free will happen.
         */
        if (s->bout_plane.surface != current_console_surface) {
            qemu_free_displaysurface(s->bout_plane.surface);
        }
        if (s->v_plane.surface != current_console_surface) {
            qemu_free_displaysurface(s->v_plane.surface);
        }
        if (s->g_plane.surface != current_console_surface) {
            qemu_free_displaysurface(s->g_plane.surface);
        }

        s->g_plane.surface
                = qemu_create_displaysurface_from(width, height,
                                                  s->g_plane.format, 0, NULL);
        s->v_plane.surface
                = qemu_create_displaysurface_from(width, height,
                                                  s->v_plane.format, 0, NULL);
        if (xlnx_dp_global_alpha_enabled(s)) {
            s->bout_plane.surface =
                            qemu_create_displaysurface_from(width,
                                                            height,
                                                            s->g_plane.format,
                                                            0, NULL);
            dpy_gfx_replace_surface(s->console, s->bout_plane.surface);
        } else {
            s->bout_plane.surface = NULL;
            dpy_gfx_replace_surface(s->console, s->g_plane.surface);
        }

        xlnx_dpdma_set_host_data_location(s->dpdma, DP_GRAPHIC_DMA_CHANNEL,
                                            surface_data(s->g_plane.surface));
        xlnx_dpdma_set_host_data_location(s->dpdma, DP_VIDEO_DMA_CHANNEL,
                                            surface_data(s->v_plane.surface));
    }
}

/*
 * Change the graphic format of the surface.
 */
static void xlnx_dp_change_graphic_fmt(XlnxDPState *s)
{
    switch (s->avbufm_registers[AV_BUF_FORMAT] & DP_GRAPHIC_MASK) {
    case DP_GRAPHIC_RGBA8888:
        s->g_plane.format = PIXMAN_r8g8b8a8;
        break;
    case DP_GRAPHIC_ABGR8888:
        s->g_plane.format = PIXMAN_a8b8g8r8;
        break;
    case DP_GRAPHIC_RGB565:
        s->g_plane.format = PIXMAN_r5g6b5;
        break;
    case DP_GRAPHIC_RGB888:
        s->g_plane.format = PIXMAN_r8g8b8;
        break;
    case DP_GRAPHIC_BGR888:
        s->g_plane.format = PIXMAN_b8g8r8;
        break;
    default:
        DPRINTF("error: unsupported graphic format %u.\n",
                s->avbufm_registers[AV_BUF_FORMAT] & DP_GRAPHIC_MASK);
        abort();
    }

    switch (s->avbufm_registers[AV_BUF_FORMAT] & DP_NL_VID_FMT_MASK) {
    case 0:
        s->v_plane.format = PIXMAN_x8b8g8r8;
        break;
    case DP_NL_VID_Y0_CB_Y1_CR:
        s->v_plane.format = PIXMAN_yuy2;
        break;
    case DP_NL_VID_RGBA8880:
        s->v_plane.format = PIXMAN_x8b8g8r8;
        break;
    default:
        DPRINTF("error: unsupported video format %u.\n",
                s->avbufm_registers[AV_BUF_FORMAT] & DP_NL_VID_FMT_MASK);
        abort();
    }

    xlnx_dp_recreate_surface(s);
}

static void xlnx_dp_update_irq(XlnxDPState *s)
{
    uint32_t flags;

    flags = s->core_registers[DP_INT_STATUS] & ~s->core_registers[DP_INT_MASK];
    DPRINTF("update IRQ value = %" PRIx32 "\n", flags);
    qemu_set_irq(s->irq, flags != 0);
}

static uint64_t xlnx_dp_read(void *opaque, hwaddr offset, unsigned size)
{
    XlnxDPState *s = XLNX_DP(opaque);
    uint64_t ret = 0;

    offset = offset >> 2;

    switch (offset) {
    case DP_TX_USER_FIFO_OVERFLOW:
        /* This register is cleared after a read */
        ret = s->core_registers[DP_TX_USER_FIFO_OVERFLOW];
        s->core_registers[DP_TX_USER_FIFO_OVERFLOW] = 0;
        break;
    case DP_AUX_REPLY_DATA:
        ret = xlnx_dp_aux_pop_rx_fifo(s);
        break;
    case DP_INTERRUPT_SIGNAL_STATE:
        /*
         * XXX: Not sure it is the right thing to do actually.
         * The register is not written by the device driver so it's stuck
         * to 0x04.
         */
        ret = s->core_registers[DP_INTERRUPT_SIGNAL_STATE];
        s->core_registers[DP_INTERRUPT_SIGNAL_STATE] &= ~0x04;
        break;
    case DP_AUX_WRITE_FIFO:
    case DP_TX_AUDIO_INFO_DATA(0):
    case DP_TX_AUDIO_INFO_DATA(1):
    case DP_TX_AUDIO_INFO_DATA(2):
    case DP_TX_AUDIO_INFO_DATA(3):
    case DP_TX_AUDIO_INFO_DATA(4):
    case DP_TX_AUDIO_INFO_DATA(5):
    case DP_TX_AUDIO_INFO_DATA(6):
    case DP_TX_AUDIO_INFO_DATA(7):
    case DP_TX_AUDIO_EXT_DATA(0):
    case DP_TX_AUDIO_EXT_DATA(1):
    case DP_TX_AUDIO_EXT_DATA(2):
    case DP_TX_AUDIO_EXT_DATA(3):
    case DP_TX_AUDIO_EXT_DATA(4):
    case DP_TX_AUDIO_EXT_DATA(5):
    case DP_TX_AUDIO_EXT_DATA(6):
    case DP_TX_AUDIO_EXT_DATA(7):
    case DP_TX_AUDIO_EXT_DATA(8):
        /* write only registers */
        ret = 0;
        break;
    default:
        assert(offset <= (0x3AC >> 2));
        ret = s->core_registers[offset];
        break;
    }

    DPRINTF("core read @%" PRIx64 " = 0x%8.8" PRIX64 "\n", offset << 2, ret);
    return ret;
}

static void xlnx_dp_write(void *opaque, hwaddr offset, uint64_t value,
                          unsigned size)
{
    XlnxDPState *s = XLNX_DP(opaque);

    DPRINTF("core write @%" PRIx64 " = 0x%8.8" PRIX64 "\n", offset, value);

    offset = offset >> 2;

    switch (offset) {
    /*
     * Only special write case are handled.
     */
    case DP_LINK_BW_SET:
        s->core_registers[offset] = value & 0x000000FF;
        break;
    case DP_LANE_COUNT_SET:
    case DP_MAIN_STREAM_MISC0:
        s->core_registers[offset] = value & 0x0000000F;
        break;
    case DP_TRAINING_PATTERN_SET:
    case DP_LINK_QUAL_PATTERN_SET:
    case DP_MAIN_STREAM_POLARITY:
    case DP_PHY_VOLTAGE_DIFF_LANE_0:
    case DP_PHY_VOLTAGE_DIFF_LANE_1:
        s->core_registers[offset] = value & 0x00000003;
        break;
    case DP_ENHANCED_FRAME_EN:
    case DP_SCRAMBLING_DISABLE:
    case DP_DOWNSPREAD_CTRL:
    case DP_MAIN_STREAM_ENABLE:
    case DP_TRANSMIT_PRBS7:
        s->core_registers[offset] = value & 0x00000001;
        break;
    case DP_PHY_CLOCK_SELECT:
        s->core_registers[offset] = value & 0x00000007;
        break;
    case DP_SOFTWARE_RESET:
        /*
         * No need to update this bit as it's read '0'.
         */
        /*
         * TODO: reset IP.
         */
        break;
    case DP_TRANSMITTER_ENABLE:
        s->core_registers[offset] = value & 0x01;
        break;
    case DP_FORCE_SCRAMBLER_RESET:
        /*
         * No need to update this bit as it's read '0'.
         */
        /*
         * TODO: force a scrambler reset??
         */
        break;
    case DP_AUX_COMMAND_REGISTER:
        s->core_registers[offset] = value & 0x00001F0F;
        xlnx_dp_aux_set_command(s, s->core_registers[offset]);
        break;
    case DP_MAIN_STREAM_HTOTAL:
    case DP_MAIN_STREAM_VTOTAL:
    case DP_MAIN_STREAM_HSTART:
    case DP_MAIN_STREAM_VSTART:
        s->core_registers[offset] = value & 0x0000FFFF;
        break;
    case DP_MAIN_STREAM_HRES:
    case DP_MAIN_STREAM_VRES:
        s->core_registers[offset] = value & 0x0000FFFF;
        xlnx_dp_recreate_surface(s);
        break;
    case DP_MAIN_STREAM_HSWIDTH:
    case DP_MAIN_STREAM_VSWIDTH:
        s->core_registers[offset] = value & 0x00007FFF;
        break;
    case DP_MAIN_STREAM_MISC1:
        s->core_registers[offset] = value & 0x00000086;
        break;
    case DP_MAIN_STREAM_M_VID:
    case DP_MAIN_STREAM_N_VID:
        s->core_registers[offset] = value & 0x00FFFFFF;
        break;
    case DP_MSA_TRANSFER_UNIT_SIZE:
    case DP_MIN_BYTES_PER_TU:
    case DP_INIT_WAIT:
        s->core_registers[offset] = value & 0x00000007;
        break;
    case DP_USER_DATA_COUNT_PER_LANE:
        s->core_registers[offset] = value & 0x0003FFFF;
        break;
    case DP_FRAC_BYTES_PER_TU:
        s->core_registers[offset] = value & 0x000003FF;
        break;
    case DP_PHY_RESET:
        s->core_registers[offset] = value & 0x00010003;
        /*
         * TODO: Reset something?
         */
        break;
    case DP_TX_PHY_POWER_DOWN:
        s->core_registers[offset] = value & 0x0000000F;
        /*
         * TODO: Power down things?
         */
        break;
    case DP_AUX_WRITE_FIFO: {
        uint8_t c = value;
        xlnx_dp_aux_push_tx_fifo(s, &c, 1);
        break;
    }
    case DP_AUX_CLOCK_DIVIDER:
        break;
    case DP_AUX_REPLY_COUNT:
        /*
         * Writing to this register clear the counter.
         */
        s->core_registers[offset] = 0x00000000;
        break;
    case DP_AUX_ADDRESS:
        s->core_registers[offset] = value & 0x000FFFFF;
        break;
    case DP_VERSION_REGISTER:
    case DP_CORE_ID:
    case DP_TX_USER_FIFO_OVERFLOW:
    case DP_AUX_REPLY_DATA:
    case DP_AUX_REPLY_CODE:
    case DP_REPLY_DATA_COUNT:
    case DP_REPLY_STATUS:
    case DP_HPD_DURATION:
        /*
         * Write to read only location..
         */
        break;
    case DP_TX_AUDIO_CONTROL:
        s->core_registers[offset] = value & 0x00000001;
        xlnx_dp_audio_activate(s);
        break;
    case DP_TX_AUDIO_CHANNELS:
        s->core_registers[offset] = value & 0x00000007;
        xlnx_dp_audio_activate(s);
        break;
    case DP_INT_STATUS:
        s->core_registers[DP_INT_STATUS] &= ~value;
        xlnx_dp_update_irq(s);
        break;
    case DP_INT_EN:
        s->core_registers[DP_INT_MASK] &= ~value;
        xlnx_dp_update_irq(s);
        break;
    case DP_INT_DS:
        s->core_registers[DP_INT_MASK] |= ~value;
        xlnx_dp_update_irq(s);
        break;
    default:
        assert(offset <= (0x504C >> 2));
        s->core_registers[offset] = value;
        break;
    }
}

static const MemoryRegionOps dp_ops = {
    .read = xlnx_dp_read,
    .write = xlnx_dp_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

/*
 * This is to handle Read/Write to the Video Blender.
 */
static void xlnx_dp_vblend_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned size)
{
    XlnxDPState *s = XLNX_DP(opaque);
    bool alpha_was_enabled;

    DPRINTF("vblend: write @0x%" HWADDR_PRIX " = 0x%" PRIX32 "\n", offset,
                                                               (uint32_t)value);
    offset = offset >> 2;

    switch (offset) {
    case V_BLEND_BG_CLR_0:
    case V_BLEND_BG_CLR_1:
    case V_BLEND_BG_CLR_2:
        s->vblend_registers[offset] = value & 0x00000FFF;
        break;
    case V_BLEND_SET_GLOBAL_ALPHA_REG:
        /*
         * A write to this register can enable or disable blending. Thus we need
         * to recreate the surfaces.
         */
        alpha_was_enabled = xlnx_dp_global_alpha_enabled(s);
        s->vblend_registers[offset] = value & 0x000001FF;
        if (xlnx_dp_global_alpha_enabled(s) != alpha_was_enabled) {
            xlnx_dp_recreate_surface(s);
        }
        break;
    case V_BLEND_OUTPUT_VID_FORMAT:
        s->vblend_registers[offset] = value & 0x00000017;
        break;
    case V_BLEND_LAYER0_CONTROL:
    case V_BLEND_LAYER1_CONTROL:
        s->vblend_registers[offset] = value & 0x00000103;
        break;
    case V_BLEND_RGB2YCBCR_COEFF(0):
    case V_BLEND_RGB2YCBCR_COEFF(1):
    case V_BLEND_RGB2YCBCR_COEFF(2):
    case V_BLEND_RGB2YCBCR_COEFF(3):
    case V_BLEND_RGB2YCBCR_COEFF(4):
    case V_BLEND_RGB2YCBCR_COEFF(5):
    case V_BLEND_RGB2YCBCR_COEFF(6):
    case V_BLEND_RGB2YCBCR_COEFF(7):
    case V_BLEND_RGB2YCBCR_COEFF(8):
    case V_BLEND_IN1CSC_COEFF(0):
    case V_BLEND_IN1CSC_COEFF(1):
    case V_BLEND_IN1CSC_COEFF(2):
    case V_BLEND_IN1CSC_COEFF(3):
    case V_BLEND_IN1CSC_COEFF(4):
    case V_BLEND_IN1CSC_COEFF(5):
    case V_BLEND_IN1CSC_COEFF(6):
    case V_BLEND_IN1CSC_COEFF(7):
    case V_BLEND_IN1CSC_COEFF(8):
    case V_BLEND_IN2CSC_COEFF(0):
    case V_BLEND_IN2CSC_COEFF(1):
    case V_BLEND_IN2CSC_COEFF(2):
    case V_BLEND_IN2CSC_COEFF(3):
    case V_BLEND_IN2CSC_COEFF(4):
    case V_BLEND_IN2CSC_COEFF(5):
    case V_BLEND_IN2CSC_COEFF(6):
    case V_BLEND_IN2CSC_COEFF(7):
    case V_BLEND_IN2CSC_COEFF(8):
        s->vblend_registers[offset] = value & 0x0000FFFF;
        break;
    case V_BLEND_LUMA_IN1CSC_OFFSET:
    case V_BLEND_CR_IN1CSC_OFFSET:
    case V_BLEND_CB_IN1CSC_OFFSET:
    case V_BLEND_LUMA_IN2CSC_OFFSET:
    case V_BLEND_CR_IN2CSC_OFFSET:
    case V_BLEND_CB_IN2CSC_OFFSET:
    case V_BLEND_LUMA_OUTCSC_OFFSET:
    case V_BLEND_CR_OUTCSC_OFFSET:
    case V_BLEND_CB_OUTCSC_OFFSET:
        s->vblend_registers[offset] = value & 0x3FFF7FFF;
        break;
    case V_BLEND_CHROMA_KEY_ENABLE:
        s->vblend_registers[offset] = value & 0x00000003;
        break;
    case V_BLEND_CHROMA_KEY_COMP1:
    case V_BLEND_CHROMA_KEY_COMP2:
    case V_BLEND_CHROMA_KEY_COMP3:
        s->vblend_registers[offset] = value & 0x0FFF0FFF;
        break;
    default:
        s->vblend_registers[offset] = value;
        break;
    }
}

static uint64_t xlnx_dp_vblend_read(void *opaque, hwaddr offset,
                                    unsigned size)
{
    XlnxDPState *s = XLNX_DP(opaque);

    DPRINTF("vblend: read @0x%" HWADDR_PRIX " = 0x%" PRIX32 "\n", offset,
            s->vblend_registers[offset >> 2]);
    return s->vblend_registers[offset >> 2];
}

static const MemoryRegionOps vblend_ops = {
    .read = xlnx_dp_vblend_read,
    .write = xlnx_dp_vblend_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

/*
 * This is to handle Read/Write to the Audio Video buffer manager.
 */
static void xlnx_dp_avbufm_write(void *opaque, hwaddr offset, uint64_t value,
                                 unsigned size)
{
    XlnxDPState *s = XLNX_DP(opaque);

    DPRINTF("avbufm: write @0x%" HWADDR_PRIX " = 0x%" PRIX32 "\n", offset,
                                                               (uint32_t)value);
    offset = offset >> 2;

    switch (offset) {
    case AV_BUF_FORMAT:
        s->avbufm_registers[offset] = value & 0x00000FFF;
        xlnx_dp_change_graphic_fmt(s);
        break;
    case AV_CHBUF0:
    case AV_CHBUF1:
    case AV_CHBUF2:
    case AV_CHBUF3:
    case AV_CHBUF4:
    case AV_CHBUF5:
        s->avbufm_registers[offset] = value & 0x0000007F;
        break;
    case AV_BUF_OUTPUT_AUDIO_VIDEO_SELECT:
        s->avbufm_registers[offset] = value & 0x0000007F;
        break;
    case AV_BUF_DITHER_CONFIG:
        s->avbufm_registers[offset] = value & 0x000007FF;
        break;
    case AV_BUF_DITHER_CONFIG_MAX:
    case AV_BUF_DITHER_CONFIG_MIN:
        s->avbufm_registers[offset] = value & 0x00000FFF;
        break;
    case AV_BUF_PATTERN_GEN_SELECT:
        s->avbufm_registers[offset] = value & 0xFFFFFF03;
        break;
    case AV_BUF_AUD_VID_CLK_SOURCE:
        s->avbufm_registers[offset] = value & 0x00000007;
        break;
    case AV_BUF_SRST_REG:
        s->avbufm_registers[offset] = value & 0x00000002;
        break;
    case AV_BUF_AUDIO_CH_CONFIG:
        s->avbufm_registers[offset] = value & 0x00000003;
        break;
    case AV_BUF_GRAPHICS_COMP_SCALE_FACTOR(0):
    case AV_BUF_GRAPHICS_COMP_SCALE_FACTOR(1):
    case AV_BUF_GRAPHICS_COMP_SCALE_FACTOR(2):
    case AV_BUF_VIDEO_COMP_SCALE_FACTOR(0):
    case AV_BUF_VIDEO_COMP_SCALE_FACTOR(1):
    case AV_BUF_VIDEO_COMP_SCALE_FACTOR(2):
        s->avbufm_registers[offset] = value & 0x0000FFFF;
        break;
    case AV_BUF_LIVE_VIDEO_COMP_SF(0):
    case AV_BUF_LIVE_VIDEO_COMP_SF(1):
    case AV_BUF_LIVE_VIDEO_COMP_SF(2):
    case AV_BUF_LIVE_VID_CONFIG:
    case AV_BUF_LIVE_GFX_COMP_SF(0):
    case AV_BUF_LIVE_GFX_COMP_SF(1):
    case AV_BUF_LIVE_GFX_COMP_SF(2):
    case AV_BUF_LIVE_GFX_CONFIG:
    case AV_BUF_NON_LIVE_LATENCY:
    case AV_BUF_STC_CONTROL:
    case AV_BUF_STC_INIT_VALUE0:
    case AV_BUF_STC_INIT_VALUE1:
    case AV_BUF_STC_ADJ:
    case AV_BUF_STC_VIDEO_VSYNC_TS_REG0:
    case AV_BUF_STC_VIDEO_VSYNC_TS_REG1:
    case AV_BUF_STC_EXT_VSYNC_TS_REG0:
    case AV_BUF_STC_EXT_VSYNC_TS_REG1:
    case AV_BUF_STC_CUSTOM_EVENT_TS_REG0:
    case AV_BUF_STC_CUSTOM_EVENT_TS_REG1:
    case AV_BUF_STC_CUSTOM_EVENT2_TS_REG0:
    case AV_BUF_STC_CUSTOM_EVENT2_TS_REG1:
    case AV_BUF_STC_SNAPSHOT0:
    case AV_BUF_STC_SNAPSHOT1:
    case AV_BUF_HCOUNT_VCOUNT_INT0:
    case AV_BUF_HCOUNT_VCOUNT_INT1:
        qemu_log_mask(LOG_UNIMP, "avbufm: unimplemented register 0x%04"
                                 PRIx64 "\n",
                      offset << 2);
        break;
    default:
        s->avbufm_registers[offset] = value;
        break;
    }
}

static uint64_t xlnx_dp_avbufm_read(void *opaque, hwaddr offset,
                                    unsigned size)
{
    XlnxDPState *s = XLNX_DP(opaque);

    offset = offset >> 2;
    return s->avbufm_registers[offset];
}

static const MemoryRegionOps avbufm_ops = {
    .read = xlnx_dp_avbufm_read,
    .write = xlnx_dp_avbufm_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

/*
 * This is a global alpha blending using pixman.
 * Both graphic and video planes are multiplied with the global alpha
 * coefficient and added.
 */
static inline void xlnx_dp_blend_surface(XlnxDPState *s)
{
    pixman_fixed_t alpha1[] = { pixman_double_to_fixed(1),
                                pixman_double_to_fixed(1),
                                pixman_double_to_fixed(1.0) };
    pixman_fixed_t alpha2[] = { pixman_double_to_fixed(1),
                                pixman_double_to_fixed(1),
                                pixman_double_to_fixed(1.0) };

    if ((surface_width(s->g_plane.surface)
         != surface_width(s->v_plane.surface)) ||
        (surface_height(s->g_plane.surface)
         != surface_height(s->v_plane.surface))) {
        return;
    }

    alpha1[2] = pixman_double_to_fixed((double)(xlnx_dp_global_alpha_value(s))
                                       / 256.0);
    alpha2[2] = pixman_double_to_fixed((255.0
                                    - (double)xlnx_dp_global_alpha_value(s))
                                       / 256.0);

    pixman_image_set_filter(s->g_plane.surface->image,
                            PIXMAN_FILTER_CONVOLUTION, alpha1, 3);
    pixman_image_composite(PIXMAN_OP_SRC, s->g_plane.surface->image, 0,
                           s->bout_plane.surface->image, 0, 0, 0, 0, 0, 0,
                           surface_width(s->g_plane.surface),
                           surface_height(s->g_plane.surface));
    pixman_image_set_filter(s->v_plane.surface->image,
                            PIXMAN_FILTER_CONVOLUTION, alpha2, 3);
    pixman_image_composite(PIXMAN_OP_ADD, s->v_plane.surface->image, 0,
                           s->bout_plane.surface->image, 0, 0, 0, 0, 0, 0,
                           surface_width(s->g_plane.surface),
                           surface_height(s->g_plane.surface));
}

static void xlnx_dp_update_display(void *opaque)
{
    XlnxDPState *s = XLNX_DP(opaque);

    if ((s->core_registers[DP_TRANSMITTER_ENABLE] & 0x01) == 0) {
        return;
    }

    s->core_registers[DP_INT_STATUS] |= (1 << 13);
    xlnx_dp_update_irq(s);

    xlnx_dpdma_trigger_vsync_irq(s->dpdma);

    /*
     * Trigger the DMA channel.
     */
    if (!xlnx_dpdma_start_operation(s->dpdma, 3, false)) {
        /*
         * An error occurred don't do anything with the data..
         * Trigger an underflow interrupt.
         */
        s->core_registers[DP_INT_STATUS] |= (1 << 21);
        xlnx_dp_update_irq(s);
        return;
    }

    if (xlnx_dp_global_alpha_enabled(s)) {
        if (!xlnx_dpdma_start_operation(s->dpdma, 0, false)) {
            s->core_registers[DP_INT_STATUS] |= (1 << 21);
            xlnx_dp_update_irq(s);
            return;
        }
        xlnx_dp_blend_surface(s);
    }

    /*
     * XXX: We might want to update only what changed.
     */
    dpy_gfx_update_full(s->console);
}

static const GraphicHwOps xlnx_dp_gfx_ops = {
    .gfx_update  = xlnx_dp_update_display,
};

static void xlnx_dp_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    XlnxDPState *s = XLNX_DP(obj);

    memory_region_init(&s->container, obj, TYPE_XLNX_DP, 0xC050);

    memory_region_init_io(&s->core_iomem, obj, &dp_ops, s, TYPE_XLNX_DP
                          ".core", 0x3AF);
    memory_region_add_subregion(&s->container, 0x0000, &s->core_iomem);

    memory_region_init_io(&s->vblend_iomem, obj, &vblend_ops, s, TYPE_XLNX_DP
                          ".v_blend", 0x1DF);
    memory_region_add_subregion(&s->container, 0xA000, &s->vblend_iomem);

    memory_region_init_io(&s->avbufm_iomem, obj, &avbufm_ops, s, TYPE_XLNX_DP
                          ".av_buffer_manager", 0x238);
    memory_region_add_subregion(&s->container, 0xB000, &s->avbufm_iomem);

    memory_region_init_io(&s->audio_iomem, obj, &audio_ops, s, TYPE_XLNX_DP
                          ".audio", sizeof(s->audio_registers));
    memory_region_add_subregion(&s->container, 0xC000, &s->audio_iomem);

    sysbus_init_mmio(sbd, &s->container);
    sysbus_init_irq(sbd, &s->irq);

    object_property_add_link(obj, "dpdma", TYPE_XLNX_DPDMA,
                             (Object **) &s->dpdma,
                             xlnx_dp_set_dpdma,
                             OBJ_PROP_LINK_STRONG,
                             &error_abort);

    /*
     * Initialize AUX Bus.
     */
    s->aux_bus = aux_init_bus(DEVICE(obj), "aux");

    /*
     * Initialize DPCD and EDID..
     */
    s->dpcd = DPCD(aux_create_slave(s->aux_bus, "dpcd"));
    object_property_add_child(OBJECT(s), "dpcd", OBJECT(s->dpcd), NULL);

    s->edid = I2CDDC(qdev_create(BUS(aux_get_i2c_bus(s->aux_bus)), "i2c-ddc"));
    i2c_set_slave_address(I2C_SLAVE(s->edid), 0x50);
    object_property_add_child(OBJECT(s), "edid", OBJECT(s->edid), NULL);

    fifo8_create(&s->rx_fifo, 16);
    fifo8_create(&s->tx_fifo, 16);
}

static void xlnx_dp_realize(DeviceState *dev, Error **errp)
{
    XlnxDPState *s = XLNX_DP(dev);
    DisplaySurface *surface;
    struct audsettings as;

    qdev_init_nofail(DEVICE(s->dpcd));
    aux_map_slave(AUX_SLAVE(s->dpcd), 0x0000);

    s->console = graphic_console_init(dev, 0, &xlnx_dp_gfx_ops, s);
    surface = qemu_console_surface(s->console);
    xlnx_dpdma_set_host_data_location(s->dpdma, DP_GRAPHIC_DMA_CHANNEL,
                                      surface_data(surface));

    as.freq = 44100;
    as.nchannels = 2;
    as.fmt = AUDIO_FORMAT_S16;
    as.endianness = 0;

    AUD_register_card("xlnx_dp.audio", &s->aud_card);

    s->amixer_output_stream = AUD_open_out(&s->aud_card,
                                           s->amixer_output_stream,
                                           "xlnx_dp.audio.out",
                                           s,
                                           xlnx_dp_audio_callback,
                                           &as);
    AUD_set_volume_out(s->amixer_output_stream, 0, 255, 255);
    xlnx_dp_audio_activate(s);
}

static void xlnx_dp_reset(DeviceState *dev)
{
    XlnxDPState *s = XLNX_DP(dev);

    memset(s->core_registers, 0, sizeof(s->core_registers));
    s->core_registers[DP_VERSION_REGISTER] = 0x04010000;
    s->core_registers[DP_CORE_ID] = 0x01020000;
    s->core_registers[DP_REPLY_STATUS] = 0x00000010;
    s->core_registers[DP_MSA_TRANSFER_UNIT_SIZE] = 0x00000040;
    s->core_registers[DP_INIT_WAIT] = 0x00000020;
    s->core_registers[DP_PHY_RESET] = 0x00010003;
    s->core_registers[DP_INT_MASK] = 0xFFFFF03F;
    s->core_registers[DP_PHY_STATUS] = 0x00000043;
    s->core_registers[DP_INTERRUPT_SIGNAL_STATE] = 0x00000001;

    s->vblend_registers[V_BLEND_RGB2YCBCR_COEFF(0)] = 0x00001000;
    s->vblend_registers[V_BLEND_RGB2YCBCR_COEFF(4)] = 0x00001000;
    s->vblend_registers[V_BLEND_RGB2YCBCR_COEFF(8)] = 0x00001000;
    s->vblend_registers[V_BLEND_IN1CSC_COEFF(0)] = 0x00001000;
    s->vblend_registers[V_BLEND_IN1CSC_COEFF(4)] = 0x00001000;
    s->vblend_registers[V_BLEND_IN1CSC_COEFF(8)] = 0x00001000;
    s->vblend_registers[V_BLEND_IN2CSC_COEFF(0)] = 0x00001000;
    s->vblend_registers[V_BLEND_IN2CSC_COEFF(4)] = 0x00001000;
    s->vblend_registers[V_BLEND_IN2CSC_COEFF(8)] = 0x00001000;

    s->avbufm_registers[AV_BUF_NON_LIVE_LATENCY] = 0x00000180;
    s->avbufm_registers[AV_BUF_OUTPUT_AUDIO_VIDEO_SELECT] = 0x00000008;
    s->avbufm_registers[AV_BUF_DITHER_CONFIG_MAX] = 0x00000FFF;
    s->avbufm_registers[AV_BUF_GRAPHICS_COMP_SCALE_FACTOR(0)] = 0x00010101;
    s->avbufm_registers[AV_BUF_GRAPHICS_COMP_SCALE_FACTOR(1)] = 0x00010101;
    s->avbufm_registers[AV_BUF_GRAPHICS_COMP_SCALE_FACTOR(2)] = 0x00010101;
    s->avbufm_registers[AV_BUF_VIDEO_COMP_SCALE_FACTOR(0)] = 0x00010101;
    s->avbufm_registers[AV_BUF_VIDEO_COMP_SCALE_FACTOR(1)] = 0x00010101;
    s->avbufm_registers[AV_BUF_VIDEO_COMP_SCALE_FACTOR(2)] = 0x00010101;
    s->avbufm_registers[AV_BUF_LIVE_VIDEO_COMP_SF(0)] = 0x00010101;
    s->avbufm_registers[AV_BUF_LIVE_VIDEO_COMP_SF(1)] = 0x00010101;
    s->avbufm_registers[AV_BUF_LIVE_VIDEO_COMP_SF(2)] = 0x00010101;
    s->avbufm_registers[AV_BUF_LIVE_GFX_COMP_SF(0)] = 0x00010101;
    s->avbufm_registers[AV_BUF_LIVE_GFX_COMP_SF(1)] = 0x00010101;
    s->avbufm_registers[AV_BUF_LIVE_GFX_COMP_SF(2)] = 0x00010101;

    memset(s->audio_registers, 0, sizeof(s->audio_registers));
    s->byte_left = 0;

    xlnx_dp_aux_clear_rx_fifo(s);
    xlnx_dp_change_graphic_fmt(s);
    xlnx_dp_update_irq(s);
}

static void xlnx_dp_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = xlnx_dp_realize;
    dc->vmsd = &vmstate_dp;
    dc->reset = xlnx_dp_reset;
}

static const TypeInfo xlnx_dp_info = {
    .name          = TYPE_XLNX_DP,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XlnxDPState),
    .instance_init = xlnx_dp_init,
    .class_init    = xlnx_dp_class_init,
};

static void xlnx_dp_register_types(void)
{
    type_register_static(&xlnx_dp_info);
}

type_init(xlnx_dp_register_types)

/*
 * xlnx_dp.h
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
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef XLNX_DP_H
#define XLNX_DP_H

#include "hw/sysbus.h"
#include "ui/console.h"
#include "hw/misc/auxbus.h"
#include "hw/i2c/i2c.h"
#include "hw/display/dpcd.h"
#include "hw/display/i2c-ddc.h"
#include "qemu/fifo8.h"
#include "qemu/units.h"
#include "hw/dma/xlnx_dpdma.h"
#include "audio/audio.h"
#include "qom/object.h"

#define AUD_CHBUF_MAX_DEPTH                 (32 * KiB)
#define MAX_QEMU_BUFFER_SIZE                (4 * KiB)

#define DP_CORE_REG_ARRAY_SIZE              (0x3AF >> 2)
#define DP_AVBUF_REG_ARRAY_SIZE             (0x238 >> 2)
#define DP_VBLEND_REG_ARRAY_SIZE            (0x1DF >> 2)
#define DP_AUDIO_REG_ARRAY_SIZE             (0x50 >> 2)

struct PixmanPlane {
    pixman_format_code_t format;
    DisplaySurface *surface;
};

struct XlnxDPState {
    /*< private >*/
    SysBusDevice parent_obj;

    /* < public >*/
    MemoryRegion container;

    uint32_t core_registers[DP_CORE_REG_ARRAY_SIZE];
    MemoryRegion core_iomem;

    uint32_t avbufm_registers[DP_AVBUF_REG_ARRAY_SIZE];
    MemoryRegion avbufm_iomem;

    uint32_t vblend_registers[DP_VBLEND_REG_ARRAY_SIZE];
    MemoryRegion vblend_iomem;

    uint32_t audio_registers[DP_AUDIO_REG_ARRAY_SIZE];
    MemoryRegion audio_iomem;

    QemuConsole *console;

    /*
     * This is the planes used to display in console. When the blending is
     * enabled bout_plane is displayed in console else it's g_plane.
     */
    struct PixmanPlane g_plane;
    struct PixmanPlane v_plane;
    struct PixmanPlane bout_plane;

    QEMUSoundCard aud_card;
    SWVoiceOut *amixer_output_stream;
    int16_t audio_buffer_0[AUD_CHBUF_MAX_DEPTH];
    int16_t audio_buffer_1[AUD_CHBUF_MAX_DEPTH];
    size_t audio_data_available[2];
    int64_t temp_buffer[AUD_CHBUF_MAX_DEPTH];
    int16_t out_buffer[AUD_CHBUF_MAX_DEPTH];
    size_t byte_left; /* byte available in out_buffer. */
    size_t data_ptr;  /* next byte to be sent to QEMU. */

    /* Associated DPDMA controller. */
    XlnxDPDMAState *dpdma;

    qemu_irq irq;

    AUXBus *aux_bus;
    Fifo8 rx_fifo;
    Fifo8 tx_fifo;

    /*
     * XXX: This should be in an other module.
     */
    DPCDState *dpcd;
    I2CDDCState *edid;
};

#define TYPE_XLNX_DP "xlnx.v-dp"
OBJECT_DECLARE_SIMPLE_TYPE(XlnxDPState, XLNX_DP)

#endif

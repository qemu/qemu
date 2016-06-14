/*
 * xlnx_dpdma.c
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
 *
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/dma/xlnx_dpdma.h"

#ifndef DEBUG_DPDMA
#define DEBUG_DPDMA 0
#endif

#define DPRINTF(fmt, ...) do {                                                 \
    if (DEBUG_DPDMA) {                                                         \
        qemu_log("xlnx_dpdma: " fmt , ## __VA_ARGS__);                         \
    }                                                                          \
} while (0);

/*
 * Registers offset for DPDMA.
 */
#define DPDMA_ERR_CTRL                        (0x0000)
#define DPDMA_ISR                             (0x0004 >> 2)
#define DPDMA_IMR                             (0x0008 >> 2)
#define DPDMA_IEN                             (0x000C >> 2)
#define DPDMA_IDS                             (0x0010 >> 2)
#define DPDMA_EISR                            (0x0014 >> 2)
#define DPDMA_EIMR                            (0x0018 >> 2)
#define DPDMA_EIEN                            (0x001C >> 2)
#define DPDMA_EIDS                            (0x0020 >> 2)
#define DPDMA_CNTL                            (0x0100 >> 2)

#define DPDMA_GBL                             (0x0104 >> 2)
#define DPDMA_GBL_TRG_CH(n)                   (1 << n)
#define DPDMA_GBL_RTRG_CH(n)                  (1 << 6 << n)

#define DPDMA_ALC0_CNTL                       (0x0108 >> 2)
#define DPDMA_ALC0_STATUS                     (0x010C >> 2)
#define DPDMA_ALC0_MAX                        (0x0110 >> 2)
#define DPDMA_ALC0_MIN                        (0x0114 >> 2)
#define DPDMA_ALC0_ACC                        (0x0118 >> 2)
#define DPDMA_ALC0_ACC_TRAN                   (0x011C >> 2)
#define DPDMA_ALC1_CNTL                       (0x0120 >> 2)
#define DPDMA_ALC1_STATUS                     (0x0124 >> 2)
#define DPDMA_ALC1_MAX                        (0x0128 >> 2)
#define DPDMA_ALC1_MIN                        (0x012C >> 2)
#define DPDMA_ALC1_ACC                        (0x0130 >> 2)
#define DPDMA_ALC1_ACC_TRAN                   (0x0134 >> 2)

#define DPDMA_DSCR_STRT_ADDRE_CH(n)           ((0x0200 + n * 0x100) >> 2)
#define DPDMA_DSCR_STRT_ADDR_CH(n)            ((0x0204 + n * 0x100) >> 2)
#define DPDMA_DSCR_NEXT_ADDRE_CH(n)           ((0x0208 + n * 0x100) >> 2)
#define DPDMA_DSCR_NEXT_ADDR_CH(n)            ((0x020C + n * 0x100) >> 2)
#define DPDMA_PYLD_CUR_ADDRE_CH(n)            ((0x0210 + n * 0x100) >> 2)
#define DPDMA_PYLD_CUR_ADDR_CH(n)             ((0x0214 + n * 0x100) >> 2)

#define DPDMA_CNTL_CH(n)                      ((0x0218 + n * 0x100) >> 2)
#define DPDMA_CNTL_CH_EN                      (1)
#define DPDMA_CNTL_CH_PAUSED                  (1 << 1)

#define DPDMA_STATUS_CH(n)                    ((0x021C + n * 0x100) >> 2)
#define DPDMA_STATUS_BURST_TYPE               (1 << 4)
#define DPDMA_STATUS_MODE                     (1 << 5)
#define DPDMA_STATUS_EN_CRC                   (1 << 6)
#define DPDMA_STATUS_LAST_DSCR                (1 << 7)
#define DPDMA_STATUS_LDSCR_FRAME              (1 << 8)
#define DPDMA_STATUS_IGNR_DONE                (1 << 9)
#define DPDMA_STATUS_DSCR_DONE                (1 << 10)
#define DPDMA_STATUS_EN_DSCR_UP               (1 << 11)
#define DPDMA_STATUS_EN_DSCR_INTR             (1 << 12)
#define DPDMA_STATUS_PREAMBLE_OFF             (13)

#define DPDMA_VDO_CH(n)                       ((0x0220 + n * 0x100) >> 2)
#define DPDMA_PYLD_SZ_CH(n)                   ((0x0224 + n * 0x100) >> 2)
#define DPDMA_DSCR_ID_CH(n)                   ((0x0228 + n * 0x100) >> 2)

/*
 * Descriptor control field.
 */
#define CONTROL_PREAMBLE_VALUE                0xA5

#define DSCR_CTRL_PREAMBLE                    0xFF
#define DSCR_CTRL_EN_DSCR_DONE_INTR           (1 << 8)
#define DSCR_CTRL_EN_DSCR_UPDATE              (1 << 9)
#define DSCR_CTRL_IGNORE_DONE                 (1 << 10)
#define DSCR_CTRL_AXI_BURST_TYPE              (1 << 11)
#define DSCR_CTRL_AXCACHE                     (0x0F << 12)
#define DSCR_CTRL_AXPROT                      (0x2 << 16)
#define DSCR_CTRL_DESCRIPTOR_MODE             (1 << 18)
#define DSCR_CTRL_LAST_DESCRIPTOR             (1 << 19)
#define DSCR_CTRL_ENABLE_CRC                  (1 << 20)
#define DSCR_CTRL_LAST_DESCRIPTOR_OF_FRAME    (1 << 21)

/*
 * Descriptor timestamp field.
 */
#define STATUS_DONE                           (1 << 31)

#define DPDMA_FRAG_MAX_SZ                     (4096)

enum DPDMABurstType {
    DPDMA_INCR = 0,
    DPDMA_FIXED = 1
};

enum DPDMAMode {
    DPDMA_CONTIGOUS = 0,
    DPDMA_FRAGMENTED = 1
};

struct DPDMADescriptor {
    uint32_t control;
    uint32_t descriptor_id;
    /* transfer size in byte. */
    uint32_t xfer_size;
    uint32_t line_size_stride;
    uint32_t timestamp_lsb;
    uint32_t timestamp_msb;
    /* contains extension for both descriptor and source. */
    uint32_t address_extension;
    uint32_t next_descriptor;
    uint32_t source_address;
    uint32_t address_extension_23;
    uint32_t address_extension_45;
    uint32_t source_address2;
    uint32_t source_address3;
    uint32_t source_address4;
    uint32_t source_address5;
    uint32_t crc;
};

typedef enum DPDMABurstType DPDMABurstType;
typedef enum DPDMAMode DPDMAMode;
typedef struct DPDMADescriptor DPDMADescriptor;

static bool xlnx_dpdma_desc_is_last(DPDMADescriptor *desc)
{
    return ((desc->control & DSCR_CTRL_LAST_DESCRIPTOR) != 0);
}

static bool xlnx_dpdma_desc_is_last_of_frame(DPDMADescriptor *desc)
{
    return ((desc->control & DSCR_CTRL_LAST_DESCRIPTOR_OF_FRAME) != 0);
}

static uint64_t xlnx_dpdma_desc_get_source_address(DPDMADescriptor *desc,
                                                     uint8_t frag)
{
    uint64_t addr = 0;
    assert(frag < 5);

    switch (frag) {
    case 0:
        addr = desc->source_address
            + (extract32(desc->address_extension, 16, 12) << 20);
        break;
    case 1:
        addr = desc->source_address2
            + (extract32(desc->address_extension_23, 0, 12) << 8);
        break;
    case 2:
        addr = desc->source_address3
            + (extract32(desc->address_extension_23, 16, 12) << 20);
        break;
    case 3:
        addr = desc->source_address4
            + (extract32(desc->address_extension_45, 0, 12) << 8);
        break;
    case 4:
        addr = desc->source_address5
            + (extract32(desc->address_extension_45, 16, 12) << 20);
        break;
    default:
        addr = 0;
        break;
    }

    return addr;
}

static uint32_t xlnx_dpdma_desc_get_transfer_size(DPDMADescriptor *desc)
{
    return desc->xfer_size;
}

static uint32_t xlnx_dpdma_desc_get_line_size(DPDMADescriptor *desc)
{
    return extract32(desc->line_size_stride, 0, 18);
}

static uint32_t xlnx_dpdma_desc_get_line_stride(DPDMADescriptor *desc)
{
    return extract32(desc->line_size_stride, 18, 14) * 16;
}

static inline bool xlnx_dpdma_desc_crc_enabled(DPDMADescriptor *desc)
{
    return (desc->control & DSCR_CTRL_ENABLE_CRC) != 0;
}

static inline bool xlnx_dpdma_desc_check_crc(DPDMADescriptor *desc)
{
    uint32_t *p = (uint32_t *)desc;
    uint32_t crc = 0;
    uint8_t i;

    /*
     * CRC is calculated on the whole descriptor except the last 32bits word
     * using 32bits addition.
     */
    for (i = 0; i < 15; i++) {
        crc += p[i];
    }

    return crc == desc->crc;
}

static inline bool xlnx_dpdma_desc_completion_interrupt(DPDMADescriptor *desc)
{
    return (desc->control & DSCR_CTRL_EN_DSCR_DONE_INTR) != 0;
}

static inline bool xlnx_dpdma_desc_is_valid(DPDMADescriptor *desc)
{
    return (desc->control & DSCR_CTRL_PREAMBLE) == CONTROL_PREAMBLE_VALUE;
}

static inline bool xlnx_dpdma_desc_is_contiguous(DPDMADescriptor *desc)
{
    return (desc->control & DSCR_CTRL_DESCRIPTOR_MODE) == 0;
}

static inline bool xlnx_dpdma_desc_update_enabled(DPDMADescriptor *desc)
{
    return (desc->control & DSCR_CTRL_EN_DSCR_UPDATE) != 0;
}

static inline void xlnx_dpdma_desc_set_done(DPDMADescriptor *desc)
{
    desc->timestamp_msb |= STATUS_DONE;
}

static inline bool xlnx_dpdma_desc_is_already_done(DPDMADescriptor *desc)
{
    return (desc->timestamp_msb & STATUS_DONE) != 0;
}

static inline bool xlnx_dpdma_desc_ignore_done_bit(DPDMADescriptor *desc)
{
    return (desc->control & DSCR_CTRL_IGNORE_DONE) != 0;
}

static const VMStateDescription vmstate_xlnx_dpdma = {
    .name = TYPE_XLNX_DPDMA,
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(registers, XlnxDPDMAState,
                             XLNX_DPDMA_REG_ARRAY_SIZE),
        VMSTATE_BOOL_ARRAY(operation_finished, XlnxDPDMAState, 6),
        VMSTATE_END_OF_LIST()
    }
};

static void xlnx_dpdma_update_irq(XlnxDPDMAState *s)
{
    bool flags;

    flags = ((s->registers[DPDMA_ISR] & (~s->registers[DPDMA_IMR]))
          || (s->registers[DPDMA_EISR] & (~s->registers[DPDMA_EIMR])));
    qemu_set_irq(s->irq, flags);
}

static uint64_t xlnx_dpdma_descriptor_start_address(XlnxDPDMAState *s,
                                                      uint8_t channel)
{
    return (s->registers[DPDMA_DSCR_STRT_ADDRE_CH(channel)] << 16)
          + s->registers[DPDMA_DSCR_STRT_ADDR_CH(channel)];
}

static uint64_t xlnx_dpdma_descriptor_next_address(XlnxDPDMAState *s,
                                                     uint8_t channel)
{
    return ((uint64_t)s->registers[DPDMA_DSCR_NEXT_ADDRE_CH(channel)] << 32)
           + s->registers[DPDMA_DSCR_NEXT_ADDR_CH(channel)];
}

static bool xlnx_dpdma_is_channel_enabled(XlnxDPDMAState *s,
                                            uint8_t channel)
{
    return (s->registers[DPDMA_CNTL_CH(channel)] & DPDMA_CNTL_CH_EN) != 0;
}

static bool xlnx_dpdma_is_channel_paused(XlnxDPDMAState *s,
                                           uint8_t channel)
{
    return (s->registers[DPDMA_CNTL_CH(channel)] & DPDMA_CNTL_CH_PAUSED) != 0;
}

static inline bool xlnx_dpdma_is_channel_retriggered(XlnxDPDMAState *s,
                                                       uint8_t channel)
{
    /* Clear the retriggered bit after reading it. */
    bool channel_is_retriggered = s->registers[DPDMA_GBL]
                                & DPDMA_GBL_RTRG_CH(channel);
    s->registers[DPDMA_GBL] &= ~DPDMA_GBL_RTRG_CH(channel);
    return channel_is_retriggered;
}

static inline bool xlnx_dpdma_is_channel_triggered(XlnxDPDMAState *s,
                                                     uint8_t channel)
{
    return s->registers[DPDMA_GBL] & DPDMA_GBL_TRG_CH(channel);
}

static void xlnx_dpdma_update_desc_info(XlnxDPDMAState *s, uint8_t channel,
                                          DPDMADescriptor *desc)
{
    s->registers[DPDMA_DSCR_NEXT_ADDRE_CH(channel)] =
                                extract32(desc->address_extension, 0, 16);
    s->registers[DPDMA_DSCR_NEXT_ADDR_CH(channel)] = desc->next_descriptor;
    s->registers[DPDMA_PYLD_CUR_ADDRE_CH(channel)] =
                                extract32(desc->address_extension, 16, 16);
    s->registers[DPDMA_PYLD_CUR_ADDR_CH(channel)] = desc->source_address;
    s->registers[DPDMA_VDO_CH(channel)] =
                                extract32(desc->line_size_stride, 18, 14)
                                + (extract32(desc->line_size_stride, 0, 18)
                                  << 14);
    s->registers[DPDMA_PYLD_SZ_CH(channel)] = desc->xfer_size;
    s->registers[DPDMA_DSCR_ID_CH(channel)] = desc->descriptor_id;

    /* Compute the status register with the descriptor information. */
    s->registers[DPDMA_STATUS_CH(channel)] =
                                extract32(desc->control, 0, 8) << 13;
    if ((desc->control & DSCR_CTRL_EN_DSCR_DONE_INTR) != 0) {
        s->registers[DPDMA_STATUS_CH(channel)] |= DPDMA_STATUS_EN_DSCR_INTR;
    }
    if ((desc->control & DSCR_CTRL_EN_DSCR_UPDATE) != 0) {
        s->registers[DPDMA_STATUS_CH(channel)] |= DPDMA_STATUS_EN_DSCR_UP;
    }
    if ((desc->timestamp_msb & STATUS_DONE) != 0) {
        s->registers[DPDMA_STATUS_CH(channel)] |= DPDMA_STATUS_DSCR_DONE;
    }
    if ((desc->control & DSCR_CTRL_IGNORE_DONE) != 0) {
        s->registers[DPDMA_STATUS_CH(channel)] |= DPDMA_STATUS_IGNR_DONE;
    }
    if ((desc->control & DSCR_CTRL_LAST_DESCRIPTOR_OF_FRAME) != 0) {
        s->registers[DPDMA_STATUS_CH(channel)] |= DPDMA_STATUS_LDSCR_FRAME;
    }
    if ((desc->control & DSCR_CTRL_LAST_DESCRIPTOR) != 0) {
        s->registers[DPDMA_STATUS_CH(channel)] |= DPDMA_STATUS_LAST_DSCR;
    }
    if ((desc->control & DSCR_CTRL_ENABLE_CRC) != 0) {
        s->registers[DPDMA_STATUS_CH(channel)] |= DPDMA_STATUS_EN_CRC;
    }
    if ((desc->control & DSCR_CTRL_DESCRIPTOR_MODE) != 0) {
        s->registers[DPDMA_STATUS_CH(channel)] |= DPDMA_STATUS_MODE;
    }
    if ((desc->control & DSCR_CTRL_AXI_BURST_TYPE) != 0) {
        s->registers[DPDMA_STATUS_CH(channel)] |= DPDMA_STATUS_BURST_TYPE;
    }
}

static void xlnx_dpdma_dump_descriptor(DPDMADescriptor *desc)
{
    if (DEBUG_DPDMA) {
        qemu_log("DUMP DESCRIPTOR:\n");
        qemu_hexdump((char *)desc, stdout, "", sizeof(DPDMADescriptor));
    }
}

static uint64_t xlnx_dpdma_read(void *opaque, hwaddr offset,
                                unsigned size)
{
    XlnxDPDMAState *s = XLNX_DPDMA(opaque);

    DPRINTF("read @%" HWADDR_PRIx "\n", offset);
    offset = offset >> 2;

    switch (offset) {
    /*
     * Trying to read a write only register.
     */
    case DPDMA_GBL:
        return 0;
    default:
        assert(offset <= (0xFFC >> 2));
        return s->registers[offset];
    }
    return 0;
}

static void xlnx_dpdma_write(void *opaque, hwaddr offset,
                               uint64_t value, unsigned size)
{
    XlnxDPDMAState *s = XLNX_DPDMA(opaque);

    DPRINTF("write @%" HWADDR_PRIx " = %" PRIx64 "\n", offset, value);
    offset = offset >> 2;

    switch (offset) {
    case DPDMA_ISR:
        s->registers[DPDMA_ISR] &= ~value;
        xlnx_dpdma_update_irq(s);
        break;
    case DPDMA_IEN:
        s->registers[DPDMA_IMR] &= ~value;
        break;
    case DPDMA_IDS:
        s->registers[DPDMA_IMR] |= value;
        break;
    case DPDMA_EISR:
        s->registers[DPDMA_EISR] &= ~value;
        xlnx_dpdma_update_irq(s);
        break;
    case DPDMA_EIEN:
        s->registers[DPDMA_EIMR] &= ~value;
        break;
    case DPDMA_EIDS:
        s->registers[DPDMA_EIMR] |= value;
        break;
    case DPDMA_IMR:
    case DPDMA_EIMR:
    case DPDMA_DSCR_NEXT_ADDRE_CH(0):
    case DPDMA_DSCR_NEXT_ADDRE_CH(1):
    case DPDMA_DSCR_NEXT_ADDRE_CH(2):
    case DPDMA_DSCR_NEXT_ADDRE_CH(3):
    case DPDMA_DSCR_NEXT_ADDRE_CH(4):
    case DPDMA_DSCR_NEXT_ADDRE_CH(5):
    case DPDMA_DSCR_NEXT_ADDR_CH(0):
    case DPDMA_DSCR_NEXT_ADDR_CH(1):
    case DPDMA_DSCR_NEXT_ADDR_CH(2):
    case DPDMA_DSCR_NEXT_ADDR_CH(3):
    case DPDMA_DSCR_NEXT_ADDR_CH(4):
    case DPDMA_DSCR_NEXT_ADDR_CH(5):
    case DPDMA_PYLD_CUR_ADDRE_CH(0):
    case DPDMA_PYLD_CUR_ADDRE_CH(1):
    case DPDMA_PYLD_CUR_ADDRE_CH(2):
    case DPDMA_PYLD_CUR_ADDRE_CH(3):
    case DPDMA_PYLD_CUR_ADDRE_CH(4):
    case DPDMA_PYLD_CUR_ADDRE_CH(5):
    case DPDMA_PYLD_CUR_ADDR_CH(0):
    case DPDMA_PYLD_CUR_ADDR_CH(1):
    case DPDMA_PYLD_CUR_ADDR_CH(2):
    case DPDMA_PYLD_CUR_ADDR_CH(3):
    case DPDMA_PYLD_CUR_ADDR_CH(4):
    case DPDMA_PYLD_CUR_ADDR_CH(5):
    case DPDMA_STATUS_CH(0):
    case DPDMA_STATUS_CH(1):
    case DPDMA_STATUS_CH(2):
    case DPDMA_STATUS_CH(3):
    case DPDMA_STATUS_CH(4):
    case DPDMA_STATUS_CH(5):
    case DPDMA_VDO_CH(0):
    case DPDMA_VDO_CH(1):
    case DPDMA_VDO_CH(2):
    case DPDMA_VDO_CH(3):
    case DPDMA_VDO_CH(4):
    case DPDMA_VDO_CH(5):
    case DPDMA_PYLD_SZ_CH(0):
    case DPDMA_PYLD_SZ_CH(1):
    case DPDMA_PYLD_SZ_CH(2):
    case DPDMA_PYLD_SZ_CH(3):
    case DPDMA_PYLD_SZ_CH(4):
    case DPDMA_PYLD_SZ_CH(5):
    case DPDMA_DSCR_ID_CH(0):
    case DPDMA_DSCR_ID_CH(1):
    case DPDMA_DSCR_ID_CH(2):
    case DPDMA_DSCR_ID_CH(3):
    case DPDMA_DSCR_ID_CH(4):
    case DPDMA_DSCR_ID_CH(5):
        /*
         * Trying to write to a read only register..
         */
        break;
    case DPDMA_GBL:
        /*
         * This is a write only register so it's read as zero in the read
         * callback.
         * We store the value anyway so we can know if the channel is
         * enabled.
         */
        s->registers[offset] |= value & 0x00000FFF;
        break;
    case DPDMA_DSCR_STRT_ADDRE_CH(0):
    case DPDMA_DSCR_STRT_ADDRE_CH(1):
    case DPDMA_DSCR_STRT_ADDRE_CH(2):
    case DPDMA_DSCR_STRT_ADDRE_CH(3):
    case DPDMA_DSCR_STRT_ADDRE_CH(4):
    case DPDMA_DSCR_STRT_ADDRE_CH(5):
        value &= 0x0000FFFF;
        s->registers[offset] = value;
        break;
    case DPDMA_CNTL_CH(0):
        s->registers[DPDMA_GBL] &= ~DPDMA_GBL_TRG_CH(0);
        value &= 0x3FFFFFFF;
        s->registers[offset] = value;
        break;
    case DPDMA_CNTL_CH(1):
        s->registers[DPDMA_GBL] &= ~DPDMA_GBL_TRG_CH(1);
        value &= 0x3FFFFFFF;
        s->registers[offset] = value;
        break;
    case DPDMA_CNTL_CH(2):
        s->registers[DPDMA_GBL] &= ~DPDMA_GBL_TRG_CH(2);
        value &= 0x3FFFFFFF;
        s->registers[offset] = value;
        break;
    case DPDMA_CNTL_CH(3):
        s->registers[DPDMA_GBL] &= ~DPDMA_GBL_TRG_CH(3);
        value &= 0x3FFFFFFF;
        s->registers[offset] = value;
        break;
    case DPDMA_CNTL_CH(4):
        s->registers[DPDMA_GBL] &= ~DPDMA_GBL_TRG_CH(4);
        value &= 0x3FFFFFFF;
        s->registers[offset] = value;
        break;
    case DPDMA_CNTL_CH(5):
        s->registers[DPDMA_GBL] &= ~DPDMA_GBL_TRG_CH(5);
        value &= 0x3FFFFFFF;
        s->registers[offset] = value;
        break;
    default:
        assert(offset <= (0xFFC >> 2));
        s->registers[offset] = value;
        break;
    }
}

static const MemoryRegionOps dma_ops = {
    .read = xlnx_dpdma_read,
    .write = xlnx_dpdma_write,
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

static void xlnx_dpdma_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    XlnxDPDMAState *s = XLNX_DPDMA(obj);

    memory_region_init_io(&s->iomem, obj, &dma_ops, s,
                          TYPE_XLNX_DPDMA, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static void xlnx_dpdma_reset(DeviceState *dev)
{
    XlnxDPDMAState *s = XLNX_DPDMA(dev);
    size_t i;

    memset(s->registers, 0, sizeof(s->registers));
    s->registers[DPDMA_IMR] =  0x07FFFFFF;
    s->registers[DPDMA_EIMR] = 0xFFFFFFFF;
    s->registers[DPDMA_ALC0_MIN] = 0x0000FFFF;
    s->registers[DPDMA_ALC1_MIN] = 0x0000FFFF;

    for (i = 0; i < 6; i++) {
        s->data[i] = NULL;
        s->operation_finished[i] = true;
    }
}

static void xlnx_dpdma_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &vmstate_xlnx_dpdma;
    dc->reset = xlnx_dpdma_reset;
}

static const TypeInfo xlnx_dpdma_info = {
    .name          = TYPE_XLNX_DPDMA,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XlnxDPDMAState),
    .instance_init = xlnx_dpdma_init,
    .class_init    = xlnx_dpdma_class_init,
};

static void xlnx_dpdma_register_types(void)
{
    type_register_static(&xlnx_dpdma_info);
}

size_t xlnx_dpdma_start_operation(XlnxDPDMAState *s, uint8_t channel,
                                    bool one_desc)
{
    uint64_t desc_addr;
    uint64_t source_addr[6];
    DPDMADescriptor desc;
    bool done = false;
    size_t ptr = 0;

    assert(channel <= 5);

    DPRINTF("start dpdma channel 0x%" PRIX8 "\n", channel);

    if (!xlnx_dpdma_is_channel_triggered(s, channel)) {
        DPRINTF("Channel isn't triggered..\n");
        return 0;
    }

    if (!xlnx_dpdma_is_channel_enabled(s, channel)) {
        DPRINTF("Channel isn't enabled..\n");
        return 0;
    }

    if (xlnx_dpdma_is_channel_paused(s, channel)) {
        DPRINTF("Channel is paused..\n");
        return 0;
    }

    do {
        if ((s->operation_finished[channel])
          || xlnx_dpdma_is_channel_retriggered(s, channel)) {
            desc_addr = xlnx_dpdma_descriptor_start_address(s, channel);
            s->operation_finished[channel] = false;
        } else {
            desc_addr = xlnx_dpdma_descriptor_next_address(s, channel);
        }

        if (dma_memory_read(&address_space_memory, desc_addr, &desc,
                            sizeof(DPDMADescriptor))) {
            s->registers[DPDMA_EISR] |= ((1 << 1) << channel);
            xlnx_dpdma_update_irq(s);
            s->operation_finished[channel] = true;
            DPRINTF("Can't get the descriptor.\n");
            break;
        }

        xlnx_dpdma_update_desc_info(s, channel, &desc);

#ifdef DEBUG_DPDMA
        xlnx_dpdma_dump_descriptor(&desc);
#endif

        DPRINTF("location of the descriptor: %" PRIx64 "\n", desc_addr);
        if (!xlnx_dpdma_desc_is_valid(&desc)) {
            s->registers[DPDMA_EISR] |= ((1 << 7) << channel);
            xlnx_dpdma_update_irq(s);
            s->operation_finished[channel] = true;
            DPRINTF("Invalid descriptor..\n");
            break;
        }

        if (xlnx_dpdma_desc_crc_enabled(&desc)
            && !xlnx_dpdma_desc_check_crc(&desc)) {
            s->registers[DPDMA_EISR] |= ((1 << 13) << channel);
            xlnx_dpdma_update_irq(s);
            s->operation_finished[channel] = true;
            DPRINTF("Bad CRC for descriptor..\n");
            break;
        }

        if (xlnx_dpdma_desc_is_already_done(&desc)
            && !xlnx_dpdma_desc_ignore_done_bit(&desc)) {
            /* We are trying to process an already processed descriptor. */
            s->registers[DPDMA_EISR] |= ((1 << 25) << channel);
            xlnx_dpdma_update_irq(s);
            s->operation_finished[channel] = true;
            DPRINTF("Already processed descriptor..\n");
            break;
        }

        done = xlnx_dpdma_desc_is_last(&desc)
             || xlnx_dpdma_desc_is_last_of_frame(&desc);

        s->operation_finished[channel] = done;
        if (s->data[channel]) {
            int64_t transfer_len = xlnx_dpdma_desc_get_transfer_size(&desc);
            uint32_t line_size = xlnx_dpdma_desc_get_line_size(&desc);
            uint32_t line_stride = xlnx_dpdma_desc_get_line_stride(&desc);
            if (xlnx_dpdma_desc_is_contiguous(&desc)) {
                source_addr[0] = xlnx_dpdma_desc_get_source_address(&desc, 0);
                while (transfer_len != 0) {
                    if (dma_memory_read(&address_space_memory,
                                        source_addr[0],
                                        &s->data[channel][ptr],
                                        line_size)) {
                        s->registers[DPDMA_ISR] |= ((1 << 12) << channel);
                        xlnx_dpdma_update_irq(s);
                        DPRINTF("Can't get data.\n");
                        break;
                    }
                    ptr += line_size;
                    transfer_len -= line_size;
                    source_addr[0] += line_stride;
                }
            } else {
                DPRINTF("Source address:\n");
                int frag;
                for (frag = 0; frag < 5; frag++) {
                    source_addr[frag] =
                          xlnx_dpdma_desc_get_source_address(&desc, frag);
                    DPRINTF("Fragment %u: %" PRIx64 "\n", frag + 1,
                            source_addr[frag]);
                }

                frag = 0;
                while ((transfer_len < 0) && (frag < 5)) {
                    size_t fragment_len = DPDMA_FRAG_MAX_SZ
                                    - (source_addr[frag] % DPDMA_FRAG_MAX_SZ);

                    if (dma_memory_read(&address_space_memory,
                                        source_addr[frag],
                                        &(s->data[channel][ptr]),
                                        fragment_len)) {
                        s->registers[DPDMA_ISR] |= ((1 << 12) << channel);
                        xlnx_dpdma_update_irq(s);
                        DPRINTF("Can't get data.\n");
                        break;
                    }
                    ptr += fragment_len;
                    transfer_len -= fragment_len;
                    frag += 1;
                }
            }
        }

        if (xlnx_dpdma_desc_update_enabled(&desc)) {
            /* The descriptor need to be updated when it's completed. */
            DPRINTF("update the descriptor with the done flag set.\n");
            xlnx_dpdma_desc_set_done(&desc);
            dma_memory_write(&address_space_memory, desc_addr, &desc,
                             sizeof(DPDMADescriptor));
        }

        if (xlnx_dpdma_desc_completion_interrupt(&desc)) {
            DPRINTF("completion interrupt enabled!\n");
            s->registers[DPDMA_ISR] |= (1 << channel);
            xlnx_dpdma_update_irq(s);
        }

    } while (!done && !one_desc);

    return ptr;
}

void xlnx_dpdma_set_host_data_location(XlnxDPDMAState *s, uint8_t channel,
                                         void *p)
{
    if (!s) {
        qemu_log_mask(LOG_UNIMP, "DPDMA client not attached to valid DPDMA"
                      " instance\n");
        return;
    }

    assert(channel <= 5);
    s->data[channel] = p;
}

void xlnx_dpdma_trigger_vsync_irq(XlnxDPDMAState *s)
{
    s->registers[DPDMA_ISR] |= (1 << 27);
    xlnx_dpdma_update_irq(s);
}

type_init(xlnx_dpdma_register_types)

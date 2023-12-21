/*
 * QEMU model of the CFU Configuration Unit.
 *
 * Copyright (C) 2023, Advanced Micro Devices, Inc.
 *
 * Written by Edgar E. Iglesias <edgar.iglesias@gmail.com>,
 *            Sai Pavan Boddu <sai.pavan.boddu@amd.com>,
 *            Francisco Iglesias <francisco.iglesias@amd.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/register.h"
#include "hw/irq.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/units.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/misc/xlnx-versal-cfu.h"

#ifndef XLNX_VERSAL_CFU_APB_ERR_DEBUG
#define XLNX_VERSAL_CFU_APB_ERR_DEBUG 0
#endif

#define KEYHOLE_STREAM_4K (4 * KiB)
#define KEYHOLE_STREAM_256K (256 * KiB)
#define CFRAME_BROADCAST_ROW 0x1F

bool update_wfifo(hwaddr addr, uint64_t value,
                  uint32_t *wfifo, uint32_t *wfifo_ret)
{
    unsigned int idx = extract32(addr, 2, 2);

    wfifo[idx] = value;

    if (idx == 3) {
        memcpy(wfifo_ret, wfifo, WFIFO_SZ * sizeof(uint32_t));
        memset(wfifo, 0, WFIFO_SZ * sizeof(uint32_t));
        return true;
    }

    return false;
}

static void cfu_imr_update_irq(XlnxVersalCFUAPB *s)
{
    bool pending = s->regs[R_CFU_ISR] & ~s->regs[R_CFU_IMR];
    qemu_set_irq(s->irq_cfu_imr, pending);
}

static void cfu_isr_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxVersalCFUAPB *s = XLNX_VERSAL_CFU_APB(reg->opaque);
    cfu_imr_update_irq(s);
}

static uint64_t cfu_ier_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxVersalCFUAPB *s = XLNX_VERSAL_CFU_APB(reg->opaque);
    uint32_t val = val64;

    s->regs[R_CFU_IMR] &= ~val;
    cfu_imr_update_irq(s);
    return 0;
}

static uint64_t cfu_idr_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxVersalCFUAPB *s = XLNX_VERSAL_CFU_APB(reg->opaque);
    uint32_t val = val64;

    s->regs[R_CFU_IMR] |= val;
    cfu_imr_update_irq(s);
    return 0;
}

static uint64_t cfu_itr_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxVersalCFUAPB *s = XLNX_VERSAL_CFU_APB(reg->opaque);
    uint32_t val = val64;

    s->regs[R_CFU_ISR] |= val;
    cfu_imr_update_irq(s);
    return 0;
}

static void cfu_fgcr_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxVersalCFUAPB *s = XLNX_VERSAL_CFU_APB(reg->opaque);
    uint32_t val = (uint32_t)val64;

    /* Do a scan. It always looks good. */
    if (FIELD_EX32(val, CFU_FGCR, SC_HBC_TRIGGER)) {
        ARRAY_FIELD_DP32(s->regs, CFU_STATUS, SCAN_CLEAR_PASS, 1);
        ARRAY_FIELD_DP32(s->regs, CFU_STATUS, SCAN_CLEAR_DONE, 1);
    }
}

static const RegisterAccessInfo cfu_apb_regs_info[] = {
    {   .name = "CFU_ISR",  .addr = A_CFU_ISR,
        .rsvd = 0xfffffc00,
        .w1c = 0x3ff,
        .post_write = cfu_isr_postw,
    },{ .name = "CFU_IMR",  .addr = A_CFU_IMR,
        .reset = 0x3ff,
        .rsvd = 0xfffffc00,
        .ro = 0x3ff,
    },{ .name = "CFU_IER",  .addr = A_CFU_IER,
        .rsvd = 0xfffffc00,
        .pre_write = cfu_ier_prew,
    },{ .name = "CFU_IDR",  .addr = A_CFU_IDR,
        .rsvd = 0xfffffc00,
        .pre_write = cfu_idr_prew,
    },{ .name = "CFU_ITR",  .addr = A_CFU_ITR,
        .rsvd = 0xfffffc00,
        .pre_write = cfu_itr_prew,
    },{ .name = "CFU_PROTECT",  .addr = A_CFU_PROTECT,
        .reset = 0x1,
    },{ .name = "CFU_FGCR",  .addr = A_CFU_FGCR,
        .rsvd = 0xffff8000,
        .post_write = cfu_fgcr_postw,
    },{ .name = "CFU_CTL",  .addr = A_CFU_CTL,
        .rsvd = 0xffff0000,
    },{ .name = "CFU_CRAM_RW",  .addr = A_CFU_CRAM_RW,
        .reset = 0x401f7d9,
        .rsvd = 0xf8000000,
    },{ .name = "CFU_MASK",  .addr = A_CFU_MASK,
    },{ .name = "CFU_CRC_EXPECT",  .addr = A_CFU_CRC_EXPECT,
    },{ .name = "CFU_CFRAME_LEFT_T0",  .addr = A_CFU_CFRAME_LEFT_T0,
        .rsvd = 0xfff00000,
    },{ .name = "CFU_CFRAME_LEFT_T1",  .addr = A_CFU_CFRAME_LEFT_T1,
        .rsvd = 0xfff00000,
    },{ .name = "CFU_CFRAME_LEFT_T2",  .addr = A_CFU_CFRAME_LEFT_T2,
        .rsvd = 0xfff00000,
    },{ .name = "CFU_ROW_RANGE",  .addr = A_CFU_ROW_RANGE,
        .rsvd = 0xffffffc0,
        .ro = 0x3f,
    },{ .name = "CFU_STATUS",  .addr = A_CFU_STATUS,
        .rsvd = 0x80000000,
        .ro = 0x7fffffff,
    },{ .name = "CFU_INTERNAL_STATUS",  .addr = A_CFU_INTERNAL_STATUS,
        .rsvd = 0xff800000,
        .ro = 0x7fffff,
    },{ .name = "CFU_QWORD_CNT",  .addr = A_CFU_QWORD_CNT,
        .ro = 0xffffffff,
    },{ .name = "CFU_CRC_LIVE",  .addr = A_CFU_CRC_LIVE,
        .ro = 0xffffffff,
    },{ .name = "CFU_PENDING_READ_CNT",  .addr = A_CFU_PENDING_READ_CNT,
        .rsvd = 0xfe000000,
        .ro = 0x1ffffff,
    },{ .name = "CFU_FDRI_CNT",  .addr = A_CFU_FDRI_CNT,
        .ro = 0xffffffff,
    },{ .name = "CFU_ECO1",  .addr = A_CFU_ECO1,
    },{ .name = "CFU_ECO2",  .addr = A_CFU_ECO2,
    }
};

static void cfu_apb_reset(DeviceState *dev)
{
    XlnxVersalCFUAPB *s = XLNX_VERSAL_CFU_APB(dev);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        register_reset(&s->regs_info[i]);
    }
    memset(s->wfifo, 0, WFIFO_SZ * sizeof(uint32_t));

    s->regs[R_CFU_STATUS] |= R_CFU_STATUS_HC_COMPLETE_MASK;
    cfu_imr_update_irq(s);
}

static const MemoryRegionOps cfu_apb_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void cfu_transfer_cfi_packet(XlnxVersalCFUAPB *s, uint8_t row_addr,
                                    XlnxCfiPacket *pkt)
{
    if (row_addr == CFRAME_BROADCAST_ROW) {
        for (int i = 0; i < ARRAY_SIZE(s->cfg.cframe); i++) {
            if (s->cfg.cframe[i]) {
                xlnx_cfi_transfer_packet(s->cfg.cframe[i], pkt);
            }
        }
    } else {
            assert(row_addr < ARRAY_SIZE(s->cfg.cframe));

            if (s->cfg.cframe[row_addr]) {
                xlnx_cfi_transfer_packet(s->cfg.cframe[row_addr], pkt);
            }
    }
}

static uint64_t cfu_stream_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_GUEST_ERROR, "%s: Unsupported read from addr=%"
                  HWADDR_PRIx "\n", __func__, addr);
    return 0;
}

static void cfu_stream_write(void *opaque, hwaddr addr, uint64_t value,
                      unsigned size)
{
    XlnxVersalCFUAPB *s = XLNX_VERSAL_CFU_APB(opaque);
    uint32_t wfifo[WFIFO_SZ];

    if (update_wfifo(addr, value, s->wfifo, wfifo)) {
        uint8_t packet_type, row_addr, reg_addr;

        packet_type = extract32(wfifo[0], 24, 8);
        row_addr = extract32(wfifo[0], 16, 5);
        reg_addr = extract32(wfifo[0], 8, 6);

        /* Compressed bitstreams are not supported yet. */
        if (ARRAY_FIELD_EX32(s->regs, CFU_CTL, DECOMPRESS) == 0) {
            if (s->regs[R_CFU_FDRI_CNT]) {
                XlnxCfiPacket pkt = {
                    .reg_addr = CFRAME_FDRI,
                    .data[0] = wfifo[0],
                    .data[1] = wfifo[1],
                    .data[2] = wfifo[2],
                    .data[3] = wfifo[3]
                };

                cfu_transfer_cfi_packet(s, s->fdri_row_addr, &pkt);

                s->regs[R_CFU_FDRI_CNT]--;

            } else if (packet_type == PACKET_TYPE_CFU &&
                       reg_addr == CFRAME_FDRI) {

                /* Load R_CFU_FDRI_CNT, must be multiple of 25 */
                s->regs[R_CFU_FDRI_CNT] = wfifo[1];

                /* Store target row_addr */
                s->fdri_row_addr = row_addr;

                if (wfifo[1] % 25 != 0) {
                    qemu_log_mask(LOG_GUEST_ERROR,
                                  "CFU FDRI_CNT is not loaded with "
                                  "a multiple of 25 value\n");
                }

            } else if (packet_type == PACKET_TYPE_CFRAME) {
                XlnxCfiPacket pkt = {
                    .reg_addr = reg_addr,
                    .data[0] = wfifo[1],
                    .data[1] = wfifo[2],
                    .data[2] = wfifo[3],
                };
                cfu_transfer_cfi_packet(s, row_addr, &pkt);
            }
        }
    }
}

static uint64_t cfu_sfr_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_GUEST_ERROR, "%s: Unsupported read from addr=%"
                  HWADDR_PRIx "\n", __func__, addr);
    return 0;
}

static void cfu_sfr_write(void *opaque, hwaddr addr, uint64_t value,
                      unsigned size)
{
    XlnxVersalCFUSFR *s = XLNX_VERSAL_CFU_SFR(opaque);
    uint32_t wfifo[WFIFO_SZ];

    if (update_wfifo(addr, value, s->wfifo, wfifo)) {
        uint8_t row_addr = extract32(wfifo[0], 23, 5);
        uint32_t frame_addr = extract32(wfifo[0], 0, 23);
        XlnxCfiPacket pkt = { .reg_addr = CFRAME_SFR,
                              .data[0] = frame_addr };

        if (s->cfg.cfu) {
            cfu_transfer_cfi_packet(s->cfg.cfu, row_addr, &pkt);
        }
    }
}

static uint64_t cfu_fdro_read(void *opaque, hwaddr addr, unsigned size)
{
    XlnxVersalCFUFDRO *s = XLNX_VERSAL_CFU_FDRO(opaque);
    uint64_t ret = 0;

    if (!fifo32_is_empty(&s->fdro_data)) {
        ret = fifo32_pop(&s->fdro_data);
    }

    return ret;
}

static void cfu_fdro_write(void *opaque, hwaddr addr, uint64_t value,
                           unsigned size)
{
    qemu_log_mask(LOG_GUEST_ERROR, "%s: Unsupported write from addr=%"
                  HWADDR_PRIx "\n", __func__, addr);
}

static const MemoryRegionOps cfu_stream_ops = {
    .read = cfu_stream_read,
    .write = cfu_stream_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static const MemoryRegionOps cfu_sfr_ops = {
    .read = cfu_sfr_read,
    .write = cfu_sfr_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps cfu_fdro_ops = {
    .read = cfu_fdro_read,
    .write = cfu_fdro_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void cfu_apb_init(Object *obj)
{
    XlnxVersalCFUAPB *s = XLNX_VERSAL_CFU_APB(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    RegisterInfoArray *reg_array;
    unsigned int i;
    char *name;

    memory_region_init(&s->iomem, obj, TYPE_XLNX_VERSAL_CFU_APB, R_MAX * 4);
    reg_array =
        register_init_block32(DEVICE(obj), cfu_apb_regs_info,
                              ARRAY_SIZE(cfu_apb_regs_info),
                              s->regs_info, s->regs,
                              &cfu_apb_ops,
                              XLNX_VERSAL_CFU_APB_ERR_DEBUG,
                              R_MAX * 4);
    memory_region_add_subregion(&s->iomem,
                                0x0,
                                &reg_array->mem);
    sysbus_init_mmio(sbd, &s->iomem);
    for (i = 0; i < NUM_STREAM; i++) {
        name = g_strdup_printf(TYPE_XLNX_VERSAL_CFU_APB "-stream%d", i);
        memory_region_init_io(&s->iomem_stream[i], obj, &cfu_stream_ops, s,
                          name, i == 0 ? KEYHOLE_STREAM_4K :
                                         KEYHOLE_STREAM_256K);
        sysbus_init_mmio(sbd, &s->iomem_stream[i]);
        g_free(name);
    }
    sysbus_init_irq(sbd, &s->irq_cfu_imr);
}

static void cfu_sfr_init(Object *obj)
{
    XlnxVersalCFUSFR *s = XLNX_VERSAL_CFU_SFR(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem_sfr, obj, &cfu_sfr_ops, s,
                          TYPE_XLNX_VERSAL_CFU_SFR, KEYHOLE_STREAM_4K);
    sysbus_init_mmio(sbd, &s->iomem_sfr);
}

static void cfu_sfr_reset_enter(Object *obj, ResetType type)
{
    XlnxVersalCFUSFR *s = XLNX_VERSAL_CFU_SFR(obj);

    memset(s->wfifo, 0, WFIFO_SZ * sizeof(uint32_t));
}

static void cfu_fdro_init(Object *obj)
{
    XlnxVersalCFUFDRO *s = XLNX_VERSAL_CFU_FDRO(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem_fdro, obj, &cfu_fdro_ops, s,
                          TYPE_XLNX_VERSAL_CFU_FDRO, KEYHOLE_STREAM_4K);
    sysbus_init_mmio(sbd, &s->iomem_fdro);
    fifo32_create(&s->fdro_data, 8 * KiB / sizeof(uint32_t));
}

static void cfu_fdro_reset_enter(Object *obj, ResetType type)
{
    XlnxVersalCFUFDRO *s = XLNX_VERSAL_CFU_FDRO(obj);

    fifo32_reset(&s->fdro_data);
}

static void cfu_fdro_cfi_transfer_packet(XlnxCfiIf *cfi_if, XlnxCfiPacket *pkt)
{
    XlnxVersalCFUFDRO *s = XLNX_VERSAL_CFU_FDRO(cfi_if);

    if (fifo32_num_free(&s->fdro_data) >= ARRAY_SIZE(pkt->data)) {
        for (int i = 0; i < ARRAY_SIZE(pkt->data); i++) {
            fifo32_push(&s->fdro_data, pkt->data[i]);
        }
    } else {
        /* It is a programming error to fill the fifo. */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "CFU_FDRO: CFI data dropped due to full read fifo\n");
    }
}

static Property cfu_props[] = {
        DEFINE_PROP_LINK("cframe0", XlnxVersalCFUAPB, cfg.cframe[0],
                         TYPE_XLNX_CFI_IF, XlnxCfiIf *),
        DEFINE_PROP_LINK("cframe1", XlnxVersalCFUAPB, cfg.cframe[1],
                         TYPE_XLNX_CFI_IF, XlnxCfiIf *),
        DEFINE_PROP_LINK("cframe2", XlnxVersalCFUAPB, cfg.cframe[2],
                         TYPE_XLNX_CFI_IF, XlnxCfiIf *),
        DEFINE_PROP_LINK("cframe3", XlnxVersalCFUAPB, cfg.cframe[3],
                         TYPE_XLNX_CFI_IF, XlnxCfiIf *),
        DEFINE_PROP_LINK("cframe4", XlnxVersalCFUAPB, cfg.cframe[4],
                         TYPE_XLNX_CFI_IF, XlnxCfiIf *),
        DEFINE_PROP_LINK("cframe5", XlnxVersalCFUAPB, cfg.cframe[5],
                         TYPE_XLNX_CFI_IF, XlnxCfiIf *),
        DEFINE_PROP_LINK("cframe6", XlnxVersalCFUAPB, cfg.cframe[6],
                         TYPE_XLNX_CFI_IF, XlnxCfiIf *),
        DEFINE_PROP_LINK("cframe7", XlnxVersalCFUAPB, cfg.cframe[7],
                         TYPE_XLNX_CFI_IF, XlnxCfiIf *),
        DEFINE_PROP_LINK("cframe8", XlnxVersalCFUAPB, cfg.cframe[8],
                         TYPE_XLNX_CFI_IF, XlnxCfiIf *),
        DEFINE_PROP_LINK("cframe9", XlnxVersalCFUAPB, cfg.cframe[9],
                         TYPE_XLNX_CFI_IF, XlnxCfiIf *),
        DEFINE_PROP_LINK("cframe10", XlnxVersalCFUAPB, cfg.cframe[10],
                         TYPE_XLNX_CFI_IF, XlnxCfiIf *),
        DEFINE_PROP_LINK("cframe11", XlnxVersalCFUAPB, cfg.cframe[11],
                         TYPE_XLNX_CFI_IF, XlnxCfiIf *),
        DEFINE_PROP_LINK("cframe12", XlnxVersalCFUAPB, cfg.cframe[12],
                         TYPE_XLNX_CFI_IF, XlnxCfiIf *),
        DEFINE_PROP_LINK("cframe13", XlnxVersalCFUAPB, cfg.cframe[13],
                         TYPE_XLNX_CFI_IF, XlnxCfiIf *),
        DEFINE_PROP_LINK("cframe14", XlnxVersalCFUAPB, cfg.cframe[14],
                         TYPE_XLNX_CFI_IF, XlnxCfiIf *),
        DEFINE_PROP_END_OF_LIST(),
};

static Property cfu_sfr_props[] = {
        DEFINE_PROP_LINK("cfu", XlnxVersalCFUSFR, cfg.cfu,
                         TYPE_XLNX_VERSAL_CFU_APB, XlnxVersalCFUAPB *),
        DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vmstate_cfu_apb = {
    .name = TYPE_XLNX_VERSAL_CFU_APB,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(wfifo, XlnxVersalCFUAPB, 4),
        VMSTATE_UINT32_ARRAY(regs, XlnxVersalCFUAPB, R_MAX),
        VMSTATE_UINT8(fdri_row_addr, XlnxVersalCFUAPB),
        VMSTATE_END_OF_LIST(),
    }
};

static const VMStateDescription vmstate_cfu_fdro = {
    .name = TYPE_XLNX_VERSAL_CFU_FDRO,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_FIFO32(fdro_data, XlnxVersalCFUFDRO),
        VMSTATE_END_OF_LIST(),
    }
};

static const VMStateDescription vmstate_cfu_sfr = {
    .name = TYPE_XLNX_VERSAL_CFU_SFR,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(wfifo, XlnxVersalCFUSFR, 4),
        VMSTATE_END_OF_LIST(),
    }
};

static void cfu_apb_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = cfu_apb_reset;
    dc->vmsd = &vmstate_cfu_apb;
    device_class_set_props(dc, cfu_props);
}

static void cfu_fdro_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    XlnxCfiIfClass *xcic = XLNX_CFI_IF_CLASS(klass);

    dc->vmsd = &vmstate_cfu_fdro;
    xcic->cfi_transfer_packet = cfu_fdro_cfi_transfer_packet;
    rc->phases.enter = cfu_fdro_reset_enter;
}

static void cfu_sfr_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    device_class_set_props(dc, cfu_sfr_props);
    dc->vmsd = &vmstate_cfu_sfr;
    rc->phases.enter = cfu_sfr_reset_enter;
}

static const TypeInfo cfu_apb_info = {
    .name          = TYPE_XLNX_VERSAL_CFU_APB,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XlnxVersalCFUAPB),
    .class_init    = cfu_apb_class_init,
    .instance_init = cfu_apb_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_XLNX_CFI_IF },
        { }
    }
};

static const TypeInfo cfu_fdro_info = {
    .name          = TYPE_XLNX_VERSAL_CFU_FDRO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XlnxVersalCFUFDRO),
    .class_init    = cfu_fdro_class_init,
    .instance_init = cfu_fdro_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_XLNX_CFI_IF },
        { }
    }
};

static const TypeInfo cfu_sfr_info = {
    .name          = TYPE_XLNX_VERSAL_CFU_SFR,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XlnxVersalCFUSFR),
    .class_init    = cfu_sfr_class_init,
    .instance_init = cfu_sfr_init,
};

static void cfu_apb_register_types(void)
{
    type_register_static(&cfu_apb_info);
    type_register_static(&cfu_fdro_info);
    type_register_static(&cfu_sfr_info);
}

type_init(cfu_apb_register_types)
